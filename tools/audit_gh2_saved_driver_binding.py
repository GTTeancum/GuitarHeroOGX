"""Compare native-loaded GH2 clip inventories to an existing PCSX2 EE save.

No emulator launch, desktop capture, input, extraction, or disc mutation.
The test executable calls the production clip loader, not a second Python
MILO parser. Only the compact comparison report is retained.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

from read_pcsx2_savestate import read_member


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--hdr", type=Path, required=True)
    parser.add_argument("--ark", type=Path, required=True)
    parser.add_argument("--milo", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run = subprocess.run(
        [str(args.test_exe), "--channels", str(args.hdr), str(args.ark), *args.milo],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8",
        errors="replace", creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        timeout=120,
    )
    if run.returncode:
        raise RuntimeError("Native binding test failed: " + run.stderr[-1500:])
    inventories = {}
    for line in run.stdout.splitlines():
        parts = line.split("\t")
        if parts[0] == "BINDING":
            if parts[1] in inventories:
                raise RuntimeError("Duplicate directory identity; supply each once")
            inventories[parts[1]] = {"move_self": bool(int(parts[2])), "channels": {}}
        elif parts[0] == "CHANNEL":
            inventories[parts[1]]["channels"][parts[3]] = int(parts[2])
    if not inventories:
        raise RuntimeError("Native loader returned no binding inventories")

    memory = read_member(args.state, "eeMemory.bin")
    def valid(p, size=4):
        return 0x100000 <= p <= len(memory) - size
    def u32(p):
        return struct.unpack_from("<I", memory, p)[0]
    def text_at(p):
        if not valid(p):
            return None
        end = memory.find(b"\0", p, min(p + 256, len(memory)))
        if end <= p or any(c < 32 or c >= 127 for c in memory[p:end]):
            return None
        return memory[p:end].decode("ascii")
    def name(p):
        return text_at(u32(u32(p) + 0x14)) if valid(p) and valid(u32(p), 0x18) else None

    servos = {}
    at = memory.find(struct.pack("<I", 0x3E7E80))
    while at >= 0:
        p = at - 0x9C
        at = memory.find(struct.pack("<I", 0x3E7E80), at + 4)
        if valid(p, 0x104) and p % 4 == 0 and u32(p + 4) == 0x3E7EA0 and name(p):
            servos[p] = []
    # Main and MIDI drivers share the base bones ObjPtr (+20/+28) and
    # clips DirPtr (+2C/+30). Independent pollable-vtable identity checks
    # reject unrelated saved pointers to the same servo.
    for at in range(0x100000, len(memory) - 3, 4):
        if u32(at) not in servos:
            continue
        driver = at - 0x28
        if not valid(driver, 0x68):
            continue
        if u32(driver + 0x18) not in (0x3E7380, 0x3E7490):
            continue
        if u32(driver + 0x20) != 0x3E7440 or not name(driver):
            continue
        clip_set = u32(driver + 0x30)
        if not valid(clip_set, 0x90) or not name(clip_set):
            raise RuntimeError("Driver has no resolved saved clip set")
        servos[u32(at)].append({
            "address": hex(driver), "name": name(driver),
            "clip_set": name(clip_set), "clip_set_address": hex(clip_set),
            "retail_clip_set_move_self": bool(u32(clip_set + 0x8C)),
        })

    rows = []
    for servo, drivers in sorted(servos.items()):
        expected = {}
        missing_sets = []
        flag_matches = []
        for driver in drivers:
            inventory = inventories.get(driver["clip_set"])
            if inventory is None:
                missing_sets.append(driver["clip_set"])
                continue
            flag_matches.append(inventory["move_self"] == driver["retail_clip_set_move_self"])
            expected.update(inventory["channels"])
        # GH2 CharBones symbol vector begins servo+10/+14, counts+20,
        # offsets+48. Its nine bucket types precede total count/byte end.
        start, end = u32(servo + 0x10), u32(servo + 0x14)
        if not valid(start, max(4, end - start)) or end < start or (end - start) % 4:
            raise RuntimeError("Invalid saved CharBones symbol vector")
        actual = [text_at(u32(p)) for p in range(start, end, 4)]
        if any(v is None for v in actual):
            raise RuntimeError("Non-symbol in saved CharBones inventory")
        counts = list(struct.unpack_from("<10I", memory, servo + 0x20))
        offsets = list(struct.unpack_from("<10I", memory, servo + 0x48))
        expected_counts, expected_offsets = [0], [0]
        for kind in range(9):
            n = sum(value == kind for value in expected.values())
            expected_counts.append(expected_counts[-1] + n)
            expected_offsets.append(expected_offsets[-1] + n * (16 if kind < 3 else 4))
        expected_facing = "bone_facing_delta.pos" in expected
        actual_facing = bool(u32(servo + 0xC8))
        # AddBoneInternal 0x167A70 inserts lexically within the type bucket;
        # its 0x167B70 tail rounds the final allocation to 16 bytes.
        expected_order = sorted(expected, key=lambda key: (expected[key], key))
        expected_size = (expected_offsets[-1] + 15) & ~15
        actual_size = u32(servo + 0x70)
        row = {
            "servo": hex(servo), "owner": name(u32(servo + 8)),
            "drivers": drivers, "missing_clip_sets": missing_sets,
            "missing_channels": sorted(set(actual) - expected.keys()),
            "extra_channels": sorted(expected.keys() - set(actual)),
            "actual_counts": counts, "expected_counts": expected_counts,
            "actual_offsets": offsets, "expected_offsets": expected_offsets,
            "actual_channels": actual, "channel_order_matches": actual == expected_order,
            "actual_size": actual_size, "expected_size": expected_size,
            "facing_allocated": actual_facing,
        }
        row["pass"] = bool(drivers) and not missing_sets and all(flag_matches) and (
            actual == expected_order and actual_size == expected_size
            and counts == expected_counts and offsets == expected_offsets
            and expected_facing == actual_facing
        )
        rows.append(row)
    report = {
        "state": str(args.state.resolve()),
        "ee_sha256": hashlib.sha256(memory).hexdigest(),
        "native_test_sha256": hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),
        "sources": args.milo,
        "scope": "saved allocation only; not motion/retail playback parity",
        "servo_count": len(rows), "driver_count": sum(len(x) for x in servos.values()),
        "all_pass": bool(rows) and all(row["pass"] for row in rows), "rows": rows,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"servos={len(rows)} drivers={report['driver_count']} all_pass={report['all_pass']}")
    for row in rows:
        print(f"{row['owner']}: pass={row['pass']} channels={row['actual_counts'][-1]} "
              f"facing={row['facing_allocated']} missing_sets={row['missing_clip_sets']}")
    return 0 if report["all_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
