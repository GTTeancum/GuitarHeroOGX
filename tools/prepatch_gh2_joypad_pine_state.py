#!/usr/bin/env python3
"""Embed a one-shot, PINE-armed GH2 joypad pulse in a copied savestate.

The hook runs inside retail SLUS-21447 JoypadPoll after the hardware sample
has been read but before JoypadData edges and button messages are derived.
An external PINE client requests exactly one raw button sample by writing a
mask and then 1 to two control words.  The guest hook clears the request and
substitutes that mask for one poll.  No host input or PCSX2 window control is
involved.
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path


HOOK_SITE = 0x002A5190
HOOK_RETURN = HOOK_SITE + 8
EXPECTED_ORIGINAL = (0x8FA300C0, 0x3C04003E)
JOY0_BUTTONS = 0x00497358


def ins_j(address: int) -> int:
    return 0x08000000 | ((address >> 2) & 0x03FFFFFF)


def ins_lui(rt: int, immediate: int) -> int:
    return 0x3C000000 | (rt << 16) | (immediate & 0xFFFF)


def ins_ori(rt: int, rs: int, immediate: int) -> int:
    return 0x34000000 | (rs << 21) | (rt << 16) | (immediate & 0xFFFF)


def ins_lw(rt: int, base: int, offset: int) -> int:
    return 0x8C000000 | (base << 21) | (rt << 16) | (offset & 0xFFFF)


def ins_sw(rt: int, base: int, offset: int) -> int:
    return 0xAC000000 | (base << 21) | (rt << 16) | (offset & 0xFFFF)


def ins_beq(rs: int, rt: int, offset: int) -> int:
    return 0x10000000 | (rs << 21) | (rt << 16) | (offset & 0xFFFF)


def read_zip_member_raw(path: Path, info: zipfile.ZipInfo) -> bytes:
    with path.open("rb") as stream:
        stream.seek(info.header_offset)
        header = stream.read(30)
        if len(header) != 30 or struct.unpack_from("<I", header)[0] != 0x04034B50:
            raise RuntimeError(f"invalid ZIP local header for {info.filename}")
        name_len, extra_len = struct.unpack_from("<HH", header, 26)
        stream.seek(name_len + extra_len, 1)
        return stream.read(info.compress_size)


def read_zip_member(path: Path, archive: zipfile.ZipFile, info: zipfile.ZipInfo) -> bytes:
    if info.compress_type == 93:
        try:
            import zstandard as zstd
        except ImportError as exc:
            raise RuntimeError("Zstandard is required to read this PCSX2 state") from exc
        return zstd.ZstdDecompressor().decompress(
            read_zip_member_raw(path, info), max_output_size=info.file_size
        )
    return archive.read(info.filename)


def read_state(path: Path) -> tuple[bytearray, list[tuple[zipfile.ZipInfo, bytes]]]:
    try:
        with zipfile.ZipFile(path, "r") as archive:
            entries: list[tuple[zipfile.ZipInfo, bytes]] = []
            ee_memory = None
            for info in archive.infolist():
                data = read_zip_member(path, archive, info)
                if info.filename == "eeMemory.bin":
                    ee_memory = bytearray(data)
                else:
                    entries.append((info, data))
            if ee_memory is None:
                raise RuntimeError("savestate does not contain eeMemory.bin")
            return ee_memory, entries
    except NotImplementedError:
        with zipfile.ZipFile(path, "r") as archive:
            names = [info.filename for info in archive.infolist()]
        with tempfile.TemporaryDirectory(prefix="ghogx_p2s_") as temporary:
            root = Path(temporary)
            subprocess.run(["tar", "-xf", str(path), "-C", str(root)], check=True)
            ee_path = root / "eeMemory.bin"
            if not ee_path.exists():
                raise RuntimeError("savestate does not contain eeMemory.bin")
            entries = []
            for name in names:
                if name != "eeMemory.bin":
                    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    entries.append((info, (root / name).read_bytes()))
            return bytearray(ee_path.read_bytes()), entries


def build_stub(stub_base: int, control_base: int) -> bytes:
    # v0 is overwritten by the edge calculation after the hook; v1 is the raw
    # button sample that the original instruction loaded from 0xc0(sp).
    zero, v0, v1, a0, sp = 0, 2, 3, 4, 29
    words = [
        ins_lui(v0, (control_base >> 16) & 0xFFFF),
        ins_ori(v0, v0, control_base & 0xFFFF),
        ins_lw(v1, v0, 0),
        ins_beq(v1, zero, 6),
        0,
        ins_sw(zero, v0, 0),
        ins_lw(v1, v0, 4),
        ins_sw(v1, sp, 0xC0),
        ins_j(HOOK_RETURN),
        ins_lui(a0, 0x003E),
        ins_lw(v1, sp, 0xC0),
        ins_j(HOOK_RETURN),
        ins_lui(a0, 0x003E),
    ]
    return b"".join(struct.pack("<I", word) for word in words)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-in", required=True, type=Path)
    parser.add_argument("--state-out", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--stub-base", type=lambda text: int(text, 0), default=0x01FF0000)
    parser.add_argument("--control-base", type=lambda text: int(text, 0), default=0x01FF1000)
    args = parser.parse_args()

    ee_memory, entries = read_state(args.state_in)
    original = struct.unpack_from("<II", ee_memory, HOOK_SITE)
    if original != EXPECTED_ORIGINAL:
        raise RuntimeError(
            f"unexpected JoypadPoll pre-message site {tuple(hex(word) for word in original)}"
        )
    stub = build_stub(args.stub_base, args.control_base)
    for start, size, label in [
        (args.stub_base, len(stub), "stub"),
        (args.control_base, 8, "control"),
    ]:
        if start < 0 or start + size > len(ee_memory):
            raise RuntimeError(f"{label} range is outside eeMemory.bin")
        if any(ee_memory[start : start + size]):
            raise RuntimeError(f"{label} range at 0x{start:08x} is not empty")

    ee_memory[args.stub_base : args.stub_base + len(stub)] = stub
    struct.pack_into("<II", ee_memory, args.control_base, 0, 0)
    struct.pack_into("<II", ee_memory, HOOK_SITE, ins_j(args.stub_base), 0)

    args.state_out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.state_out, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("eeMemory.bin", bytes(ee_memory))
        for info, data in entries:
            cloned = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            cloned.compress_type = zipfile.ZIP_DEFLATED
            cloned.external_attr = info.external_attr
            archive.writestr(cloned, data)

    manifest = {
        "state_in": str(args.state_in.resolve()),
        "state_out": str(args.state_out.resolve()),
        "purpose": "throwaway process-local GH2 retail camera oracle",
        "host_input": False,
        "hook_site": f"0x{HOOK_SITE:08x}",
        "hook_return": f"0x{HOOK_RETURN:08x}",
        "original_words": [f"0x{word:08x}" for word in original],
        "stub_base": f"0x{args.stub_base:08x}",
        "stub_bytes": len(stub),
        "control_request": f"0x{args.control_base:08x}",
        "control_mask": f"0x{args.control_base + 4:08x}",
        "joy0_buttons": f"0x{JOY0_BUTTONS:08x}",
        "delivery_point": "post-hardware-sample, pre-JoypadData edge/message derivation",
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
