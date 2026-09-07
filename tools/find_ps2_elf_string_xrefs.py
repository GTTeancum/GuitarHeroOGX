#!/usr/bin/env python3
"""Find bounded MIPS address constructions for strings in a PS2 ELF."""

from __future__ import annotations

import argparse
import io
from pathlib import Path

from capstone import (
    CS_ARCH_MIPS,
    CS_MODE_LITTLE_ENDIAN,
    CS_MODE_MIPS32,
    CS_MODE_MIPS64,
    CS_OP_IMM,
    CS_OP_REG,
    Cs,
    CsError,
)
from elftools.elf.elffile import ELFFile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--iso-member", help="Read ELF member in memory without extracting the disc")
    parser.add_argument("text", nargs="+")
    parser.add_argument("--max-results", type=int, default=64)
    parser.add_argument(
        "--mips64",
        action="store_true",
        help="decode the PS2 Emotion Engine's MIPS III instruction forms",
    )
    args = parser.parse_args()
    if args.max_results < 1 or args.max_results > 1024:
        parser.error("--max-results must be between 1 and 1024")

    if args.iso_member:
        import pycdlib
        disc = pycdlib.PyCdlib()
        disc.open(str(args.elf))
        elf_stream = io.BytesIO()
        try:
            disc.get_file_from_iso_fp(elf_stream, iso_path=args.iso_member)
        finally:
            disc.close()
        elf_stream.seek(0)
    else:
        elf_stream = args.elf.open("rb")
    with elf_stream as stream:
        elf = ELFFile(stream)
        segments = [
            segment
            for segment in elf.iter_segments()
            if segment["p_type"] == "PT_LOAD"
        ]
        targets: dict[int, str] = {}
        for segment in segments:
            data = segment.data()
            base = int(segment["p_vaddr"])
            for text in args.text:
                needle = text.encode("ascii") + b"\0"
                start = 0
                while True:
                    offset = data.find(needle, start)
                    if offset < 0:
                        break
                    targets[base + offset] = text
                    start = offset + 1

        for address, text in sorted(targets.items()):
            print(f"string 0x{address:08x} {text!r}")

        disassembler = Cs(
            CS_ARCH_MIPS,
            (CS_MODE_MIPS64 if args.mips64 else CS_MODE_MIPS32)
            | CS_MODE_LITTLE_ENDIAN,
        )
        disassembler.detail = True
        disassembler.skipdata = True
        result_count = 0
        for segment in segments:
            if not (int(segment["p_flags"]) & 1):
                continue
            base = int(segment["p_vaddr"])
            recent_lui: dict[int, tuple[int, int]] = {}
            for index, instruction in enumerate(
                disassembler.disasm(segment.data(), base)
            ):
                recent_lui = {
                    register: value
                    for register, value in recent_lui.items()
                    if index - value[1] <= 20
                }
                try:
                    operands = instruction.operands
                except CsError:
                    recent_lui.clear()
                    continue
                if (
                    instruction.mnemonic == "lui"
                    and len(operands) == 2
                    and operands[0].type == CS_OP_REG
                    and operands[1].type == CS_OP_IMM
                ):
                    recent_lui[operands[0].reg] = (
                        (int(operands[1].imm) & 0xFFFF) << 16,
                        index,
                    )
                    continue
                if (
                    instruction.mnemonic not in {"addiu", "ori"}
                    or len(operands) != 3
                    or operands[0].type != CS_OP_REG
                    or operands[1].type != CS_OP_REG
                    or operands[2].type != CS_OP_IMM
                    or operands[1].reg not in recent_lui
                ):
                    continue
                high, _ = recent_lui[operands[1].reg]
                immediate = int(operands[2].imm)
                if instruction.mnemonic == "ori":
                    address = high | (immediate & 0xFFFF)
                else:
                    low = immediate & 0xFFFF
                    if low & 0x8000:
                        low -= 0x10000
                    address = (high + low) & 0xFFFFFFFF
                if address not in targets:
                    continue
                print(
                    f"xref 0x{instruction.address:08x} "
                    f"{instruction.mnemonic} {instruction.op_str} "
                    f"-> 0x{address:08x} {targets[address]!r}"
                )
                result_count += 1
                if result_count >= args.max_results:
                    return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
