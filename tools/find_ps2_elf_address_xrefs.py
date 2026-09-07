#!/usr/bin/env python3
"""Find bounded MIPS references to virtual addresses in a PS2 ELF."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from capstone import (
    CS_ARCH_MIPS,
    CS_MODE_LITTLE_ENDIAN,
    CS_MODE_MIPS32,
    CS_MODE_MIPS64,
    CS_OP_IMM,
    CS_OP_MEM,
    CS_OP_REG,
    Cs,
    CsError,
)
from elftools.elf.elffile import ELFFile


def parse_int(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("address", nargs="+", type=parse_int)
    parser.add_argument("--max-results", type=int, default=128)
    parser.add_argument("--pointer-data", action="store_true",
                        help="Find aligned pointer words (e.g. virtual tables), not instructions")
    parser.add_argument(
        "--mips64",
        action="store_true",
        help="decode the PS2 Emotion Engine's MIPS III instruction forms",
    )
    args = parser.parse_args()
    targets = {address & 0xFFFFFFFF for address in args.address}
    if args.max_results < 1 or args.max_results > 4096:
        parser.error("--max-results must be between 1 and 4096")

    with args.elf.open("rb") as stream:
        elf = ELFFile(stream)
        segments = [
            (int(segment["p_vaddr"]), segment.data())
            for segment in elf.iter_segments()
            if segment["p_type"] == "PT_LOAD"
            and (args.pointer_data or int(segment["p_flags"]) & 1)
        ]

    if args.pointer_data:
        results = 0
        for base, data in segments:
            for target in sorted(targets):
                needle = struct.pack("<I", target)
                offset = data.find(needle)
                while offset >= 0:
                    if (base + offset) % 4 == 0:
                        print(f"pointer {base + offset:#010x} -> {target:#010x}")
                        results += 1
                        if results >= args.max_results:
                            return 0
                    offset = data.find(needle, offset + 1)
        return 0

    disassembler = Cs(
        CS_ARCH_MIPS,
        (CS_MODE_MIPS64 if args.mips64 else CS_MODE_MIPS32)
        | CS_MODE_LITTLE_ENDIAN,
    )
    disassembler.detail = True
    disassembler.skipdata = True
    results = 0
    for base, data in segments:
        recent_lui: dict[int, tuple[int, int]] = {}
        for index, instruction in enumerate(disassembler.disasm(data, base)):
            recent_lui = {
                register: value
                for register, value in recent_lui.items()
                if index - value[1] <= 32
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

            address = None
            if (
                instruction.mnemonic in {"addiu", "ori"}
                and len(operands) == 3
                and operands[1].type == CS_OP_REG
                and operands[2].type == CS_OP_IMM
                and operands[1].reg in recent_lui
            ):
                high, _ = recent_lui[operands[1].reg]
                low = int(operands[2].imm) & 0xFFFF
                if instruction.mnemonic == "addiu" and low & 0x8000:
                    low -= 0x10000
                address = (high + low) & 0xFFFFFFFF
            elif (
                instruction.mnemonic in {"j", "jal"}
                and len(operands) == 1
                and operands[0].type == CS_OP_IMM
            ):
                address = int(operands[0].imm) & 0xFFFFFFFF
            else:
                for operand in operands:
                    if (
                        operand.type == CS_OP_MEM
                        and operand.mem.base in recent_lui
                    ):
                        high, _ = recent_lui[operand.mem.base]
                        address = (high + int(operand.mem.disp)) & 0xFFFFFFFF
                        break
            if address not in targets:
                continue
            print(
                f"xref 0x{instruction.address:08x} "
                f"{instruction.mnemonic} {instruction.op_str} "
                f"-> 0x{address:08x}"
            )
            results += 1
            if results >= args.max_results:
                return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
