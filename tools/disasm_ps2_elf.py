#!/usr/bin/env python3
"""Disassemble a bounded virtual-address range from a little-endian PS2 ELF."""

from __future__ import annotations

import argparse
from pathlib import Path

from capstone import CS_ARCH_MIPS, CS_MODE_LITTLE_ENDIAN, CS_MODE_MIPS64, Cs
from elftools.elf.elffile import ELFFile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("size", type=lambda value: int(value, 0))
    args = parser.parse_args()

    with args.elf.open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            start = int(segment["p_vaddr"])
            data = segment.data()
            end = start + len(data)
            if start <= args.address and args.address + args.size <= end:
                offset = args.address - start
                code = data[offset : offset + args.size]
                break
        else:
            raise SystemExit(
                f"range 0x{args.address:08x}+0x{args.size:x} is not in one ELF segment"
            )

    decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_LITTLE_ENDIAN)
    decoder.skipdata = True
    for instruction in decoder.disasm(code, args.address):
        raw = instruction.bytes.hex()
        print(
            f"0x{instruction.address:08x}: {raw:<8} "
            f"{instruction.mnemonic:<10} {instruction.op_str}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
