"""Inspect source CharServoBone state in an existing PCSX2 save; no UI/input."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from read_pcsx2_savestate import read_member


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    memory = read_member(args.state, "eeMemory.bin")
    u32 = lambda p: struct.unpack_from("<I", memory, p)[0]
    valid = lambda p, n=256: 0x100000 <= p < len(memory) - n

    def text_at(p):
        if not valid(p):
            return None
        end = memory.find(b"\0", p, p + 256)
        if end <= p or any(c < 32 or c >= 127 for c in memory[p:end]):
            return None
        return memory[p:end].decode("ascii")

    def name(p):
        if not valid(p) or not valid(u32(p)):
            return None
        return text_at(u32(u32(p) + 0x14))

    def value(p, count):
        return list(struct.unpack_from(f"<{count}f", memory, p)) if valid(p) else None

    result = []
    # Original SLUS_214.47 constructor1807BC/1807C4: two independent vtables.
    pattern = struct.pack("<I", 0x3E7E80)
    at = memory.find(pattern)
    while at >= 0:
        servo = at - 0x9C
        at = memory.find(pattern, at + 4)
        if not valid(servo) or servo % 4 or u32(servo + 4) != 0x3E7EA0:
            continue
        if name(servo) is None:
            continue
        pointers = {label: u32(servo + offset) for label, offset in (
            ("pelvis", 0xC0), ("rotation_delta", 0xC4),
            ("position_delta", 0xC8), ("rotation", 0xCC), ("position", 0xD0))}
        regulate_waypoint = u32(servo + 0xE4)
        regulate_aux = u32(servo + 0x100)
        result.append(dict(address=hex(servo), name=name(servo),
            owner=name(u32(servo + 8)), pelvis=name(pointers["pelvis"]),
            move_self=u32(servo + 0xD4), delta_changed=u32(servo + 0xD8),
            regulate_waypoint=hex(regulate_waypoint),
            regulate_waypoint_name=name(regulate_waypoint),
            regulate_aux_100=hex(regulate_aux),
            regulate_aux_100_name=name(regulate_aux),
            typed_outputs_begin=hex(u32(servo + 0xA4)),
            typed_outputs_end=hex(u32(servo + 0xA8)),
            typed_outputs_empty=u32(servo + 0xA4) == u32(servo + 0xA8),
            zero_scalar_begin=hex(u32(servo + 0x8C)),
            zero_scalar_end=hex(u32(servo + 0x98)),
            zero_scalar_bytes=u32(servo + 0x98) - u32(servo + 0x8C),
            pointers={k: hex(v) for k, v in pointers.items()},
            values={k: value(pointers[k], 3 if "position" in k else 1)
                    for k in pointers if k != "pelvis"}))
    report = dict(source=str(args.state.resolve()),
        ee_sha256=hashlib.sha256(memory).hexdigest(),
        scope="Read-only saved driver state, not live all-character parity", servos=result)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for row in result:
        print(f"{row['owner']}::{row['name']} move_self={row['move_self']} "
              f"changed={row['delta_changed']} facing={row['values']['rotation']}")
    print(f"Verified servo objects: {len(result)}")
    return 0 if result else 1


if __name__ == "__main__":
    raise SystemExit(main())
