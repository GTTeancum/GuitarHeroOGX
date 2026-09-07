#!/usr/bin/env python3
"""Disassemble a bounded virtual-address range from a little-endian PS2 ELF."""

from __future__ import annotations

import argparse
import io
from pathlib import Path

from capstone import (
    CS_ARCH_MIPS,
    CS_MODE_LITTLE_ENDIAN,
    CS_MODE_MIPS32,
    CS_MODE_MIPS64,
    Cs,
)
from elftools.elf.elffile import ELFFile
from ps2_ee_disasm import disassemble_ee


def parse_int(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--iso-member", help="Read this ISO9660 ELF member in memory; do not extract the disc")
    parser.add_argument("--start", type=parse_int, required=True)
    parser.add_argument("--end", type=parse_int, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--mips64",
        action="store_true",
        help="decode the PS2's 64-bit GPR instructions instead of MIPS32",
    )
    args = parser.parse_args()

    if args.end <= args.start:
        parser.error("--end must be greater than --start")
    if args.start % 4 or args.end % 4:
        parser.error("EE instruction ranges must be 4-byte aligned")
    if args.end - args.start > 16 * 1024 * 1024:
        parser.error("range exceeds 16 MiB safety bound")

    if args.iso_member:
        import pycdlib
        disc = pycdlib.PyCdlib()
        disc.open(str(args.elf))
        stream = io.BytesIO()
        try:
            disc.get_file_from_iso_fp(stream, iso_path=args.iso_member)
        finally:
            disc.close()
        stream.seek(0)
    else:
        stream = args.elf.open("rb")
    with stream:
        elf = ELFFile(stream)
        containing = None
        for segment in elf.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue
            start = int(segment["p_vaddr"])
            end = start + int(segment["p_filesz"])
            if start <= args.start and args.end <= end:
                containing = segment
                break
        if containing is None:
            raise RuntimeError("requested virtual-address range is not file-backed")
        file_offset = int(containing["p_offset"]) + args.start - int(
            containing["p_vaddr"]
        )
        stream.seek(file_offset)
        code = stream.read(args.end - args.start)

    disassembler = Cs(
        CS_ARCH_MIPS,
        (CS_MODE_MIPS64 if args.mips64 else CS_MODE_MIPS32)
        | CS_MODE_LITTLE_ENDIAN,
    )
    disassembler.detail = False
    # Decode EE-specific primary/COP2 encodings before scalar Capstone. Its
    # generic MIPS tables otherwise mislabel LQC2 as bbit032 and LQ as MSA.
    lines = [
        f"# file={args.elf.resolve()}",
        f"# va=0x{args.start:08x}..0x{args.end:08x} bytes={len(code)}",
        "# decoder=EE overrides + scalar Capstone; unsupported EE ops are explicit .word",
    ]
    lines.extend(disassemble_ee(code, args.start, disassembler))
    text = "\n".join(lines) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
