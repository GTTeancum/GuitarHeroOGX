#!/usr/bin/env python3
"""Inspect GH1 camera path policy directly from an ISO, without extracting ARKs."""

import argparse
import hashlib
import io
import struct
import subprocess
import tempfile
from pathlib import Path

import pycdlib


def entry_index(disc):
    header = io.BytesIO()
    disc.get_file_from_iso_fp(header, iso_path="/GEN/MAIN.HDR;1")
    header.seek(0)

    def u32():
        return struct.unpack("<I", header.read(4))[0]

    if u32() != 3:
        raise ValueError("Expected GH1 ARK v3")
    u32()
    sizes = [u32() for _ in range(u32())]
    blob = header.read(u32())
    offsets = [u32() for _ in range(u32())]

    def string(index):
        return "" if index == 0xFFFFFFFF else blob[offsets[index]:].split(b"\0", 1)[0].decode("ascii")

    entries = []
    for _ in range(u32()):
        offset, name, folder, size, _ = struct.unpack("<5I", header.read(20))
        entries.append((f"{string(folder)}/{string(name)}", offset, size))
    return sizes, entries


def read_entry(disc, wanted, index=None):
    sizes, entries = index if index is not None else entry_index(disc)
    for path, offset, size in entries:
        if path != wanted:
            continue
        if size > 2 * 1024 * 1024:
            raise ValueError("Camera policy exceeds 2 MiB inspection limit")
        for part, part_size in enumerate(sizes):
            if offset >= part_size:
                offset -= part_size
                continue
            if offset + size > part_size:
                raise ValueError("Camera policy crosses ARK parts")
            with disc.open_file_from_iso(iso_path=f"/GEN/MAIN_{part}.ARK;1") as stream:
                stream.seek(offset)
                data = stream.read(size)
            if len(data) != size:
                raise ValueError("Truncated camera policy")
            return data
    raise ValueError(f"Missing {wanted}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--dtb-tool", type=Path)
    parser.add_argument("--raw-out", type=Path,
                        help="Retain only this bounded source entry instead of decoding a DTB")
    parser.add_argument("--entry", default="arena/gen/cam_paths.dtb")
    args = parser.parse_args()
    if not args.raw_out and not args.dtb_tool:
        parser.error("--dtb-tool or --raw-out is required")
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    try:
        data = read_entry(disc, args.entry)
    finally:
        disc.close()
    if args.raw_out:
        args.raw_out.parent.mkdir(parents=True, exist_ok=True)
        with args.raw_out.open("xb") as output:
            output.write(data)
        print(f"{args.entry}: {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()}")
        return
    with tempfile.TemporaryDirectory(prefix="ghogx-camera-policy-") as scratch:
        path = Path(scratch) / "policy.dtb"
        path.write_bytes(data)
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        result = subprocess.run([str(args.dtb_tool.resolve()), "dump", str(path)],
                                capture_output=True, check=True, creationflags=flags)
        print(result.stdout.decode("utf-8", errors="replace"), end="")


if __name__ == "__main__":
    main()
