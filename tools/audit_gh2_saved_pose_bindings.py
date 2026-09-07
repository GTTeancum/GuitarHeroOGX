"""Compare production CharBonesMeshes allocation/target binding to saved GH2 EE.

Reads the existing verified servo inventory and save in memory, sends a small
fixture through a hidden native test's stdin, retains only a compact report.
This proves static allocation and target identity, NOT running pose/IK parity.
"""
import argparse
import hashlib
import json
import math
import os
import shlex
import struct
import subprocess
from pathlib import Path

from read_pcsx2_savestate import read_member


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prior = json.loads(args.inventory.read_text(encoding="utf-8"))
    if not prior["all_pass"]:
        raise RuntimeError("Need a previously verified driver inventory")
    memory = read_member(Path(prior["state"]), "eeMemory.bin")
    digest = hashlib.sha256(memory).hexdigest()
    if digest != prior["ee_sha256"]:
        raise RuntimeError("Saved EE memory changed; rerun the driver inventory audit")

    def read(fmt, address):
        size = struct.calcsize(fmt)
        if not 0x100000 <= address <= len(memory) - size:
            raise ValueError(f"Out-of-range saved pointer {address:#x}")
        return struct.unpack_from(fmt, memory, address)

    def u32(address):
        return read("<I", address)[0]

    def string(address):
        if not address:
            return ""
        read("<B", address)
        end = memory.find(b"\0", address, min(address + 256, len(memory)))
        if end < 0:
            raise ValueError("Unterminated saved symbol")
        return memory[address:end].decode("ascii")

    cases = []
    lines = [str(len(prior["rows"]))]
    for saved in prior["rows"]:
        servo = int(saved["servo"], 16)
        if u32(servo + 4) != 0x3E7EA0:
            raise RuntimeError("Saved servo vtable identity changed")
        start, end = u32(servo + 0x10), u32(servo + 0x14)
        names = [string(u32(p)) for p in range(start, end, 4)]
        counts = read("<10I", servo + 0x20)
        offsets = read("<10I", servo + 0x48)
        table, table_end = u32(servo + 0xA4), u32(servo + 0xA8)
        if table_end - table != 12 * len(names) or counts[-1] != len(names):
            raise RuntimeError("Saved ObjPtr table length mismatch")
        targets = [u32(table + 12 * i + 8) for i in range(len(names))]
        dummy = u32(servo + 0xB8)
        unique = list(dict.fromkeys(targets + [dummy]))
        if not dummy or any(not p for p in unique):
            raise RuntimeError("Saved target/dummy missing")
        lines.append(f"{json.dumps(saved['owner'])} {len(unique)} {unique.index(dummy)}")
        for target in unique:
            name = string(u32(u32(target) + 0x14))
            values = [v for row in range(3) for v in read("<3f", target + 0x20 + 16 * row)]
            values.extend(read("<3f", target + 0x50))
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError("Non-finite saved local transform")
            lines.append(json.dumps(name) + " " + " ".join(format(value, ".9g") for value in values))
        lines.append(" ".join(map(str, counts)))
        lines.append(" ".join(map(str, offsets)))
        lines.append(f"{u32(servo + 0x70)} {len(names)}")
        for i, name in enumerate(names):
            kind = next(t for t in range(9) if counts[t] <= i < counts[t+1])
            lines.append(f"{json.dumps(name)} {kind} {unique.index(targets[i])}")
        cases.append({"owner": saved["owner"], "servo": saved["servo"],
                      "channels": len(names), "targets": len(set(targets)),
                      "fallback_rows": targets.count(dummy), "allocated_bytes": u32(servo + 0x70)})

    run = subprocess.run([str(args.test_exe), "--retail-bindings"],
                         input="\n".join(lines) + "\n", capture_output=True,
                         text=True, encoding="utf-8", errors="strict", timeout=60,
                         creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if run.returncode:
        raise RuntimeError(f"Native binding comparison failed ({run.returncode}): {run.stderr[-1500:]}")
    received = [shlex.split(line) for line in run.stdout.splitlines() if line.startswith("RETAIL_BINDING ")]
    if len(received) != len(cases):
        raise RuntimeError("Native comparison omitted a saved servo")
    for case, fields in zip(cases, received):
        if fields != ["RETAIL_BINDING", case["owner"], str(case["channels"]),
                      str(case["fallback_rows"]), str(case["allocated_bytes"])]:
            raise RuntimeError("Native result does not match submitted saved case")
        case["pass"] = True
    result = {"all_pass": True, "ee_sha256": digest,
              "native_test_sha256": hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),
              "scope": "static production allocation/target identity, not animated pose or retail playback parity",
              "servo_count": len(cases), "channel_count": sum(case["channels"] for case in cases),
              "cases": cases}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {result['servo_count']} retail servos / {result['channel_count']} typed output rows; layout and target identity")


if __name__ == "__main__":
    main()
