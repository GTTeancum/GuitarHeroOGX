#!/usr/bin/env python3
"""Inspect and extract Guitar Hero III PS2 DATAP.HED/DATAP.WAD entries."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
import struct

try:
    import pycdlib
except ImportError as exc:  # pragma: no cover - environment check
    raise SystemExit("pycdlib is required to read PS2 ISO directory records") from exc


SECTOR_SIZE = 2048
DEFAULT_HED = "/DATAP.HED;1"
DEFAULT_WAD = "/DATAP.WAD;1"


@dataclass(frozen=True)
class IsoFile:
    path: str
    offset: int
    size: int


@dataclass(frozen=True)
class DatapEntry:
    sector: int
    size: int
    path: str

    @property
    def normalized_path(self) -> str:
        return self.path.lstrip("\\").replace("\\", "/")


@dataclass(frozen=True)
class SkaHeader:
    path: str
    size: int
    format: str
    duration_seconds: float
    version: int
    flags: int
    bone_count: int
    quat_changes: int
    trans_changes: int
    custom_key_count: int
    pos_custom_keys: int
    pos_quat_keys: int
    pos_trans_keys: int
    pos_bonesizes_quat: int
    pos_bonesizes_trans: int
    total_quat_size: int
    total_trans_size: int


@dataclass(frozen=True)
class PakRecord:
    index: int
    header_offset: int
    extension_checksum: int
    relative_data_offset: int
    absolute_data_offset: int
    data_size: int
    asset_context_checksum: int
    full_name_checksum: int
    name_no_ext_checksum: int
    parent_checksum: int
    flags: int
    raw_words: list[int]


@dataclass(frozen=True)
class PakSummary:
    path: str
    size: int
    first_data_offset: int
    valid_record_count: int
    terminator_offset: int | None
    table_aligned: bool
    records: list[PakRecord]


def iso_file(iso: pycdlib.PyCdlib, path: str) -> IsoFile:
    record = iso.get_record(iso_path=path)
    return IsoFile(
        path=path,
        offset=record.extent_location() * SECTOR_SIZE,
        size=record.data_length,
    )


def read_iso_slice(iso_path: Path, source: IsoFile) -> bytes:
    with iso_path.open("rb") as handle:
        handle.seek(source.offset)
        return handle.read(source.size)


def open_datap_sources(iso_path: Path) -> tuple[IsoFile, IsoFile]:
    iso = pycdlib.PyCdlib()
    try:
        iso.open(str(iso_path))
        return iso_file(iso, DEFAULT_HED), iso_file(iso, DEFAULT_WAD)
    finally:
        iso.close()


def parse_hed(data: bytes) -> list[DatapEntry]:
    entries: list[DatapEntry] = []
    offset = 0
    while offset + 8 <= len(data):
        sector = int.from_bytes(data[offset:offset + 4], "little")
        size = int.from_bytes(data[offset + 4:offset + 8], "little")
        end = data.find(b"\0", offset + 8)
        if end < 0:
            break
        raw_name = data[offset + 8:end]
        if not raw_name:
            break
        name = raw_name.decode("ascii", errors="replace")
        entries.append(DatapEntry(sector=sector, size=size, path=name))
        offset = (end + 4) & ~3
    return entries


def load_entries(iso_path: Path) -> tuple[IsoFile, IsoFile, list[DatapEntry]]:
    hed, wad = open_datap_sources(iso_path)
    entries = parse_hed(read_iso_slice(iso_path, hed))
    return hed, wad, entries


def read_datap_entry(iso_path: Path, wad: IsoFile, entry: DatapEntry) -> bytes:
    with iso_path.open("rb") as handle:
        handle.seek(wad.offset + entry.sector * SECTOR_SIZE)
        return handle.read(entry.size)


def is_midori_base(entry: DatapEntry) -> bool:
    name = entry.normalized_path.lower()
    if "_color2" in name or "_color3" in name or "_color4" in name:
        return False
    if name.startswith("anims/band/guitarist/midori/"):
        return name.endswith(".ska.ps2")
    if name == "anims/rig/defaults/gh3_guitarist_midori_default.ska.ps2":
        return True
    if name == "skeletons/gh3_guitarist_midori.ske.ps2":
        return True
    if name in {
        "models/guitarists/midori_1.skin.ps2",
        "models/guitarists/midori_1.tex.ps2",
        "models/guitarists/midori_2.skin.ps2",
        "models/guitarists/midori_2.tex.ps2",
        "pak/models/full_anims/midori_full/midori_full.pak.ps2",
        "pak/models/full_anims/midori_full/midori_full_anims.pak.ps2",
        "pak/models/guitarists/midori_1/midori_1.pak.ps2",
        "pak/models/guitarists/midori_1/midori_1_anims.pak.ps2",
        "pak/models/guitarists/midori_2/midori_2.pak.ps2",
        "pak/models/guitarists/midori_2/midori_2_anims.pak.ps2",
    }:
        return True
    if name in {
        "images/menuscreens/characterselect/character_mug_midori_a.img.ps2",
        "images/menuscreens/characterselect/character_mug_midori_b.img.ps2",
        "images/highway/highway_midori_fm_01.img.ps2",
        "images/magphotos/photo_midori_1.img.ps2",
        "images/magphotos/photo_midori_2.img.ps2",
    }:
        return True
    return False


def is_animation(entry: DatapEntry) -> bool:
    return entry.normalized_path.lower().endswith(".ska.ps2")


def is_pak(entry: DatapEntry) -> bool:
    return entry.normalized_path.lower().endswith(".pak.ps2")


def entry_kind(entry: DatapEntry) -> str:
    name = entry.normalized_path.lower()
    if name.endswith(".ska.ps2"):
        return "animation"
    if name.endswith(".ske.ps2"):
        return "skeleton"
    if name.endswith(".skin.ps2"):
        return "skin"
    if name.endswith(".tex.ps2"):
        return "texture"
    if name.endswith(".pak.ps2"):
        return "pak"
    if name.endswith(".img.ps2"):
        return "image"
    return "other"


def parse_ska_header(entry: DatapEntry, data: bytes) -> SkaHeader:
    if len(data) < 0x58:
        raise ValueError(f"{entry.normalized_path} is too small for a SKA header")
    version = int.from_bytes(data[0x00:0x04], "little")
    flags = int.from_bytes(data[0x04:0x08], "little")
    if version == 0x28:
        fmt = "gh3_console"
        bone_count = data[0x0d]
        quat_changes = int.from_bytes(data[0x0e:0x10], "little")
        trans_changes = int.from_bytes(data[0x10:0x12], "little")
        custom_key_count = int.from_bytes(data[0x12:0x14], "little")
        pos_custom_keys = int.from_bytes(data[0x14:0x18], "little", signed=True)
        pos_quat_keys = int.from_bytes(data[0x18:0x1c], "little", signed=True)
        pos_trans_keys = int.from_bytes(data[0x1c:0x20], "little", signed=True)
        pos_bonesizes_quat = int.from_bytes(data[0x20:0x24], "little", signed=True)
        pos_bonesizes_trans = int.from_bytes(data[0x24:0x28], "little", signed=True)
        total_quat_size = int.from_bytes(data[0x28:0x2c], "little")
        total_trans_size = int.from_bytes(data[0x2c:0x30], "little")
    else:
        fmt = "unknown"
        bone_count = 0
        quat_changes = trans_changes = custom_key_count = 0
        pos_custom_keys = pos_quat_keys = pos_trans_keys = -1
        pos_bonesizes_quat = pos_bonesizes_trans = -1
        total_quat_size = total_trans_size = 0
    return SkaHeader(
        path=entry.normalized_path,
        size=entry.size,
        format=fmt,
        duration_seconds=struct.unpack("<f", data[0x08:0x0c])[0],
        version=version,
        flags=flags,
        bone_count=bone_count,
        quat_changes=quat_changes,
        trans_changes=trans_changes,
        custom_key_count=custom_key_count,
        pos_custom_keys=pos_custom_keys,
        pos_quat_keys=pos_quat_keys,
        pos_trans_keys=pos_trans_keys,
        pos_bonesizes_quat=pos_bonesizes_quat,
        pos_bonesizes_trans=pos_bonesizes_trans,
        total_quat_size=total_quat_size,
        total_trans_size=total_trans_size,
    )


def parse_pak_summary(entry: DatapEntry, data: bytes, max_records: int = 8) -> PakSummary:
    if len(data) < 0x30:
        raise ValueError(f"{entry.normalized_path} is too small for a PAK table")
    terminators = {0x2CB3EF3B, 0xB524565F}
    first_data_offset = int.from_bytes(data[0x04:0x08], "little")
    table_aligned = (
        0x10 <= first_data_offset <= len(data)
        and (first_data_offset - 0x10) % 0x20 == 0
    )
    record_limit = (first_data_offset - 0x10) // 0x20 if table_aligned else 0
    records: list[PakRecord] = []
    terminator_offset: int | None = None
    valid_record_count = 0
    for index in range(record_limit + 1):
        offset = index * 0x20
        if offset + 0x20 > len(data):
            break
        words = list(struct.unpack("<8I", data[offset:offset + 0x20]))
        if words[0] in terminators:
            terminator_offset = offset
            break
        if words[0] == 0 and words[1] == 0 and words[2] == 0:
            continue
        valid_record_count += 1
        if len(records) < max_records:
            records.append(
                PakRecord(
                    index=index,
                    header_offset=offset,
                    extension_checksum=words[0],
                    relative_data_offset=words[1],
                    absolute_data_offset=words[1] + offset,
                    data_size=words[2],
                    asset_context_checksum=words[3],
                    full_name_checksum=words[4],
                    name_no_ext_checksum=words[5],
                    parent_checksum=words[6],
                    flags=words[7],
                    raw_words=words,
                )
            )
    return PakSummary(
        path=entry.normalized_path,
        size=entry.size,
        first_data_offset=first_data_offset,
        valid_record_count=valid_record_count,
        terminator_offset=terminator_offset,
        table_aligned=table_aligned,
        records=records,
    )


def write_entry(iso_path: Path, wad: IsoFile, entry: DatapEntry, output_root: Path) -> Path:
    target = output_root.joinpath(*entry.normalized_path.split("/"))
    resolved_root = output_root.resolve()
    resolved_target = target.resolve()
    if resolved_root not in (resolved_target, *resolved_target.parents):
        raise ValueError(f"refusing to write outside output root: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    with iso_path.open("rb") as source, target.open("wb") as out:
        source.seek(wad.offset + entry.sector * SECTOR_SIZE)
        remaining = entry.size
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise EOFError(f"unexpected EOF extracting {entry.path}")
            out.write(chunk)
            remaining -= len(chunk)
    return target


def command_list(args: argparse.Namespace) -> int:
    hed, wad, entries = load_entries(args.iso)
    pattern = re.compile(args.regex, re.IGNORECASE) if args.regex else None
    rows = []
    for entry in entries:
        name = entry.normalized_path
        if args.contains and args.contains.lower() not in name.lower():
            continue
        if pattern and not pattern.search(name):
            continue
        rows.append(entry)
    if args.json:
        payload = {
            "iso": str(args.iso),
            "hed": asdict(hed),
            "wad": asdict(wad),
            "entries": [asdict(entry) | {"normalized_path": entry.normalized_path} for entry in rows],
        }
        print(json.dumps(payload, indent=2))
    else:
        print(f"hed={hed.size} bytes wad={wad.size} bytes entries={len(entries)} matched={len(rows)}")
        for entry in rows[:args.limit]:
            print(f"{entry.sector:8d} {entry.size:8d} {entry.normalized_path}")
    return 0


def command_midori(args: argparse.Namespace) -> int:
    hed, wad, entries = load_entries(args.iso)
    selected = [entry for entry in entries if is_midori_base(entry)]
    payload = {
        "source": "Guitar Hero III PS2 DATAP",
        "iso": str(args.iso),
        "hed": asdict(hed),
        "wad": asdict(wad),
        "rules": {
            "include": [
                "midori_1 base model and texture",
                "midori_2 base model and texture",
                "gh3_guitarist_midori skeleton",
                "all anims/band/guitarist/midori .ska.ps2 clips",
                "midori default rig clip",
                "base midori model and animation pak files",
                "base select/highway/reference images",
            ],
            "exclude": ["midori_1_color2-4", "midori_2_color2-4"],
        },
        "entry_count": len(selected),
        "byte_count": sum(entry.size for entry in selected),
        "entries": [
            asdict(entry) | {
                "normalized_path": entry.normalized_path,
                "kind": entry_kind(entry),
            }
            for entry in selected
        ],
    }
    text = json.dumps(payload, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


def command_extract(args: argparse.Namespace) -> int:
    _, wad, entries = load_entries(args.iso)
    if args.midori:
        selected = [entry for entry in entries if is_midori_base(entry)]
    else:
        wanted = {
            line.strip().replace("\\", "/").lstrip("/")
            for line in args.paths.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        }
        selected = [entry for entry in entries if entry.normalized_path in wanted]
    for entry in selected:
        write_entry(args.iso, wad, entry, args.output)
    print(f"extracted {len(selected)} files, {sum(entry.size for entry in selected)} bytes")
    return 0


def command_analyze(args: argparse.Namespace) -> int:
    hed, wad, entries = load_entries(args.iso)
    if args.midori:
        selected = [entry for entry in entries if is_midori_base(entry)]
    elif args.contains:
        needle = args.contains.lower()
        selected = [entry for entry in entries if needle in entry.normalized_path.lower()]
    else:
        selected = entries

    ska_headers: list[SkaHeader] = []
    pak_summaries: list[PakSummary] = []
    for entry in selected:
        if is_animation(entry):
            ska_headers.append(parse_ska_header(entry, read_datap_entry(args.iso, wad, entry)))
        elif is_pak(entry):
            pak_summaries.append(
                parse_pak_summary(
                    entry,
                    read_datap_entry(args.iso, wad, entry),
                    max_records=args.pak_record_limit,
                )
            )

    payload = {
        "iso": str(args.iso),
        "hed": asdict(hed),
        "wad": asdict(wad),
        "selected_entries": len(selected),
        "ska_count": len(ska_headers),
        "pak_count": len(pak_summaries),
        "ska": [asdict(header) for header in ska_headers],
        "pak": [asdict(summary) for summary in pak_summaries],
    }
    text = json.dumps(payload, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=Path("Guitar Hero III - Legends of Rock (USA).iso"))
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="List DATAP entries")
    list_parser.add_argument("--contains")
    list_parser.add_argument("--regex")
    list_parser.add_argument("--json", action="store_true")
    list_parser.add_argument("--limit", type=int, default=80)
    list_parser.set_defaults(func=command_list)

    midori = sub.add_parser("midori", help="Emit the base Midori source manifest")
    midori.add_argument("--output", type=Path)
    midori.set_defaults(func=command_midori)

    extract = sub.add_parser("extract", help="Extract selected DATAP entries")
    group = extract.add_mutually_exclusive_group(required=True)
    group.add_argument("--midori", action="store_true")
    group.add_argument("--paths", type=Path)
    extract.add_argument("--output", type=Path, required=True)
    extract.set_defaults(func=command_extract)

    analyze = sub.add_parser("analyze", help="Analyze selected SKA and PAK payloads")
    analyze.add_argument("--midori", action="store_true")
    analyze.add_argument("--contains")
    analyze.add_argument("--pak-record-limit", type=int, default=8)
    analyze.add_argument("--output", type=Path)
    analyze.set_defaults(func=command_analyze)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.iso.exists():
        parser.error(f"ISO not found: {args.iso}")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
