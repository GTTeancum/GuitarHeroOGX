#!/usr/bin/env python3
"""Dump a bounded little-endian word range from a PS2 ELF virtual address."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


def parse_int(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--start", type=parse_int, required=True)
    parser.add_argument("--count", type=int, default=32)
    parser.add_argument("--ascii", action="store_true", help="Also show the four original bytes as ASCII")
    args = parser.parse_args()
    if args.start & 3:
        parser.error("--start must be four-byte aligned")
    if args.count < 1 or args.count > 4096:
        parser.error("--count must be between 1 and 4096")

    byte_count = args.count * 4
    with args.elf.open("rb") as stream:
        elf = ELFFile(stream)
        containing = None
        for segment in elf.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue
            start = int(segment["p_vaddr"])
            end = start + int(segment["p_filesz"])
            if start <= args.start and args.start + byte_count <= end:
                containing = segment
                break
        if containing is None:
            raise RuntimeError("requested virtual-address range is not file-backed")
        file_offset = int(containing["p_offset"]) + args.start - int(
            containing["p_vaddr"]
        )
        stream.seek(file_offset)
        data = stream.read(byte_count)

    for index, (word,) in enumerate(struct.iter_unpack("<I", data)):
        suffix = ""
        if args.ascii:
            raw = data[index * 4:index * 4 + 4]
            suffix = "  " + "".join(chr(byte) if 32 <= byte < 127 else "." for byte in raw)
        print(f"0x{args.start + index * 4:08x}: 0x{word:08x}{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
