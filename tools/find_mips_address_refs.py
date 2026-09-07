#!/usr/bin/env python3
"""Find bounded MIPS code references to one virtual address in a PS2 ELF.

The scanner recognizes the common two-instruction address constructions used
by GCC for the Emotion Engine: ``lui`` followed shortly by ``addiu``, ``ori``,
or a base-register load/store.  It is deliberately an analysis-only tool and
does not patch the ELF.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from capstone import CS_ARCH_MIPS, CS_MODE_LITTLE_ENDIAN, CS_MODE_MIPS64, Cs
from elftools.elf.elffile import ELFFile


def sign16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("--window", type=int, default=12)
    args = parser.parse_args()
    if not 1 <= args.window <= 64:
        parser.error("--window must be between 1 and 64 instructions")

    with args.elf.open("rb") as stream:
        elf = ELFFile(stream)
        segments = [
            (int(segment["p_vaddr"]), segment.data())
            for segment in elf.iter_segments()
            if segment["p_type"] == "PT_LOAD"
        ]

    decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_LITTLE_ENDIAN)
    decoder.skipdata = True
    found = 0
    for base, data in segments:
        words = struct.unpack(f"<{len(data) // 4}I", data[: len(data) & ~3])
        for index, word in enumerate(words):
            if word >> 26 != 0x0F:  # lui
                continue
            register = (word >> 16) & 0x1F
            high = (word & 0xFFFF) << 16
            for distance in range(1, min(args.window + 1, len(words) - index)):
                follower = words[index + distance]
                opcode = follower >> 26
                rs = (follower >> 21) & 0x1F
                rt = (follower >> 16) & 0x1F
                imm = follower & 0xFFFF
                resolved: int | None = None
                if rs == register and opcode in {0x09, 0x19}:  # addiu/daddiu
                    resolved = (high + sign16(imm)) & 0xFFFFFFFF
                elif rs == register and opcode == 0x0D:  # ori
                    resolved = high | imm
                elif rs == register and opcode in {
                    0x20, 0x21, 0x23, 0x24, 0x25, 0x27,  # loads
                    0x28, 0x29, 0x2B, 0x2C, 0x2D, 0x2F,  # stores/cache
                    0x31, 0x35, 0x39, 0x3D,
                }:
                    resolved = (high + sign16(imm)) & 0xFFFFFFFF
                if resolved != args.address:
                    continue

                start_index = max(0, index - 4)
                end_index = min(len(words), index + distance + 5)
                start_address = base + start_index * 4
                code = data[start_index * 4 : end_index * 4]
                print(
                    f"reference 0x{base + index * 4:08x} -> "
                    f"0x{base + (index + distance) * 4:08x} "
                    f"target=0x{args.address:08x}"
                )
                for instruction in decoder.disasm(code, start_address):
                    marker = "=>" if instruction.address in {
                        base + index * 4,
                        base + (index + distance) * 4,
                    } else "  "
                    print(
                        f"{marker} 0x{instruction.address:08x}: "
                        f"{instruction.mnemonic:<10} {instruction.op_str}"
                    )
                print()
                found += 1
                break

    print(f"references={found}")
    return 0 if found else 1


if __name__ == "__main__":
    raise SystemExit(main())
