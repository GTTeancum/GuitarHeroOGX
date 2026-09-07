"""Read bounded native PCSX2 savestate members, including Zstandard ZIPs.

No emulator, desktop access, input, or mutation of the original save state.
The optional PNG is PCSX2's own saved Screenshot.png, not a screen capture.
"""
import argparse
from pathlib import Path
import struct
from zipfile import ZipFile


def read_member(path, name):
    with ZipFile(path) as archive:
        info = archive.getinfo(name)
        if info.file_size > 64 * 1024 * 1024:
            raise ValueError("Member exceeds bounded inspection size")
        if info.compress_type != 93:
            return archive.read(name)
        import zstandard
        with open(path, "rb") as stream:
            stream.seek(info.header_offset)
            header = stream.read(30)
            if header[:4] != b"PK\x03\x04":
                raise ValueError("Invalid local ZIP header")
            name_size, extra_size = struct.unpack_from("<HH", header, 26)
            stream.seek(name_size + extra_size, 1)
            packed = stream.read(info.compress_size)
        raw = zstandard.ZstdDecompressor().decompress(packed, max_output_size=info.file_size)
        import zlib
        if len(raw) != info.file_size or zlib.crc32(raw) != info.CRC:
            raise ValueError("Savestate member integrity mismatch")
        return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--screenshot-out", type=Path)
    parser.add_argument("--words", type=lambda n: int(n, 0), nargs="*", default=[])
    args = parser.parse_args()
    if len(args.words) > 32:
        parser.error("At most 32 addresses per inspection")
    if args.screenshot_out:
        args.screenshot_out.parent.mkdir(parents=True, exist_ok=True)
        args.screenshot_out.write_bytes(read_member(args.state, "Screenshot.png"))
        print(args.screenshot_out.resolve())
    if args.words:
        memory = read_member(args.state, "eeMemory.bin")
        for address in args.words:
            values = struct.unpack_from("<4I", memory, address)
            print(f"{address:#010x}: " + " ".join(f"{n:#010x}" for n in values))


if __name__ == "__main__":
    main()
