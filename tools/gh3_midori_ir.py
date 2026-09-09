#!/usr/bin/env python3
"""Export a compact Midori source IR summary from GH3 PS2 assets.

This is intentionally a thin adapter over community parsers:

- gh3_datap.py owns GH3 PS2 DATAP.HED/WAD access.
- NXTools owns .skin.ps2 and .tex.ps2 decoding.

The JSON emitted here is a compact proof/manifest for the future full
geometry-and-animation IR. It records decoded mesh, texture, skeleton, and SKA
metadata without writing extracted assets or large vertex buffers to disk.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
import struct
import zlib
from collections import Counter
from dataclasses import asdict
from pathlib import Path
from typing import Any, Iterable

import gh3_datap
from gh3_nxtools_probe import load_nxtools


MIDORI_OUTFITS = [
    {
        "name": "midori_1",
        "skin": "models/guitarists/midori_1.skin.ps2",
        "texture": "models/guitarists/midori_1.tex.ps2",
    },
    {
        "name": "midori_2",
        "skin": "models/guitarists/midori_2.skin.ps2",
        "texture": "models/guitarists/midori_2.tex.ps2",
    },
]

MIDORI_SKELETON = "skeletons/gh3_guitarist_midori.ske.ps2"
GHWT_ROCKER_BONE_LIST = "assets/BoneList_GHWTRocker.ghbones"
STANDARDKEY_Q = "anims/standardkeyq.bin"
STANDARDKEY_T = "anims/standardkeyt.bin"
STANDARDKEY_ENTRY_COUNT = 256
STANDARDKEY_ENTRY_SIZE = 8
FLAG_SINGLEBYTEVALUE = 0x4000
FLAG_SINGLEBYTEX = 0x2000
FLAG_SINGLEBYTEY = 0x1000
FLAG_SINGLEBYTEZ = 0x0800
QUAT_DIVISOR = 32768.0
TRANS_FLAGTIME_MAX = 0x40
SKAFLAG_HIRESFRAMES = 1 << 6
SKAFLAG_LONGQUATTIMES = 1 << 8
SKAFLAG_STANDARDKEY_COMPONENTS = 1 << 16
SKAFLAG_USEPARTIALFLAGS = 1 << 19
SKAFLAG_USECOMPRESSIONTABLE = 1 << 23
SKAFLAG_NOQUATCOMPRESSION = 1 << 28
SKAFLAG_FLOATTRANSTIMES = 1 << 31


def checksum(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value & 0xFFFFFFFF:08x}"


def sha256_short(data: bytes | bytearray | None) -> str | None:
    if data is None:
        return None
    return hashlib.sha256(bytes(data)).hexdigest()[:16]


def sha256_full(data: bytes | bytearray | None) -> str | None:
    if data is None:
        return None
    return hashlib.sha256(bytes(data)).hexdigest()


def read_u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little")


def read_f32x4(data: bytes, offset: int) -> list[float]:
    return list(struct.unpack_from("<4f", data, offset))


class ByteReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def at_end(self) -> bool:
        return self.offset >= len(self.data)

    def u8(self) -> int:
        value = self.data[self.offset]
        self.offset += 1
        return value

    def i8(self) -> int:
        value = struct.unpack_from("<b", self.data, self.offset)[0]
        self.offset += 1
        return value

    def u16(self) -> int:
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def i16(self) -> int:
        value = struct.unpack_from("<h", self.data, self.offset)[0]
        self.offset += 2
        return value

    def f32(self) -> float:
        value = struct.unpack_from("<f", self.data, self.offset)[0]
        self.offset += 4
        return value

    def u32(self) -> int:
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def u32_be(self) -> int:
        value = struct.unpack_from(">I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def i32(self) -> int:
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

def parse_standardkey_table(data: bytes, label: str) -> list[tuple[int, int, int, int]]:
    expected_size = STANDARDKEY_ENTRY_COUNT * STANDARDKEY_ENTRY_SIZE
    if len(data) != expected_size:
        raise ValueError(f"{label} must be exactly {expected_size} bytes, got {len(data)}")
    return [
        struct.unpack_from("<4h", data, index * STANDARDKEY_ENTRY_SIZE)
        for index in range(STANDARDKEY_ENTRY_COUNT)
    ]


def rebuild_w(x: float, y: float, z: float, negative: bool = False) -> float:
    mag_diff = 1.0 - x * x - y * y - z * z
    value = math.sqrt(0.0 if mag_diff < 0.0 else mag_diff)
    return -value if negative else value


def from_ska_quat(
    x: float,
    y: float,
    z: float,
    negative_w: bool = False,
) -> list[float]:
    if x == 0.0 and y == 0.0 and z == 0.0:
        return [1.0, 0.0, 0.0, 0.0]
    # NXTools FromSKAQuat: file stores -Y, -Z, -X; output is WXYZ.
    bx, by, bz = -z, -x, -y
    return [rebuild_w(bx, by, bz, negative_w), bx, by, bz]


def partial_allowed_bones(words: list[int], bone_count: int) -> list[int]:
    """Decode little-endian Neversoft partial-animation mask words."""
    return [
        bone_index
        for bone_index in range(bone_count)
        if words[bone_index // 32] & (1 << (bone_index % 32))
    ]


def parse_ghbones(data: bytes) -> list[str]:
    if len(data) < 8 or data[:4] != b"\xda\xda\xda\xda":
        raise ValueError("unrecognized .ghbones file")
    names = []
    offset = 8
    while offset + 4 <= len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        offset += 4
        if size <= 0 or offset + size > len(data):
            break
        names.append(data[offset:offset + size].decode("ascii", errors="replace"))
        offset += size
    return names


def load_ghwt_bone_names(nxtools_root: Path) -> list[str]:
    path = nxtools_root / GHWT_ROCKER_BONE_LIST
    if not path.exists():
        return []
    return parse_ghbones(path.read_bytes())


def apply_gh3_ps2_texture_overrides(ps2_tex: Any) -> None:
    # GH3 PS2 DATAP v6 character textures keep PSMT8 indices linear. NXTools'
    # v6 parser targets THUG/THAW-style dictionaries whose indexed payloads are
    # GS-page swizzled; applying that path corrupts Midori's palette indices.
    ps2_tex.UnswizzlePSMT8 = lambda swizzled, width, height: bytes(swizzled)


def parse_gh3_ps2_skeleton(
    path: str,
    data: bytes,
    stock_bone_names: list[str],
) -> dict[str, Any]:
    if len(data) < 0x60:
        raise ValueError(f"{path} is too small for a GH3 PS2 skeleton")
    const_a = int.from_bytes(data[0:2], "little")
    const_b = int.from_bytes(data[2:4], "little")
    bone_count = read_u32(data, 4) & 0x0000FFFF
    offsets = {
        "bone_names": read_u32(data, 16),
        "bone_parents": read_u32(data, 20),
        "bone_flips": read_u32(data, 24),
        "bone_flip_indexes": read_u32(data, 28),
        "bone_types": read_u32(data, 32),
        "matrices": read_u32(data, 36),
        "vectors": read_u32(data, 40),
        "quaternions": read_u32(data, 44),
    }
    required_end = offsets["bone_types"] + bone_count
    if required_end > len(data):
        raise ValueError(f"{path} GH3 PS2 skeleton tables run past EOF")

    bones: list[dict[str, Any]] = []
    for index in range(bone_count):
        raw_offset = read_f32x4(data, offsets["vectors"] + index * 16)
        raw_quat = read_f32x4(data, offsets["quaternions"] + index * 16)
        matrix_offset = offsets["matrices"] + index * 64
        matrix_rows = [
            read_f32x4(data, matrix_offset + row * 16)
            for row in range(4)
        ]
        name_checksum = read_u32(data, offsets["bone_names"] + index * 4)
        parent_checksum = read_u32(data, offsets["bone_parents"] + index * 4)
        flip_checksum = read_u32(data, offsets["bone_flips"] + index * 4)
        flip_index = read_u32(data, offsets["bone_flip_indexes"] + index * 4)
        stock_name = stock_bone_names[index] if index < len(stock_bone_names) else None
        bones.append({
            "index": index,
            "stock_ghwt_name": stock_name,
            "name_checksum": checksum(name_checksum),
            "parent_checksum": checksum(parent_checksum) if parent_checksum else None,
            "flip_checksum": checksum(flip_checksum) if flip_checksum else None,
            "flip_index": flip_index,
            "bone_type": data[offsets["bone_types"] + index],
            "raw_offset": raw_offset,
            "nxtools_offset": [raw_offset[2], raw_offset[0], raw_offset[1], raw_offset[3]],
            "raw_quat_xyzw": raw_quat,
            "nxtools_quat_wxyz": [raw_quat[3], -raw_quat[2], -raw_quat[0], -raw_quat[1]],
            "raw_matrix_rows_nxtools_order": matrix_rows,
        })

    checksum_to_index = {
        bone["name_checksum"]: bone["index"]
        for bone in bones
    }
    for bone in bones:
        parent = bone["parent_checksum"]
        flip = bone["flip_checksum"]
        bone["parent_index"] = checksum_to_index.get(parent) if parent else None
        bone["flip_resolved_index"] = checksum_to_index.get(flip) if flip else None

    return {
        "path": path,
        "size": len(data),
        "sha256_16": sha256_short(data),
        "const_a": const_a,
        "const_b": const_b,
        "bone_count": bone_count,
        "offsets": offsets,
        "stock_bone_list": GHWT_ROCKER_BONE_LIST if stock_bone_names else None,
        "stock_bone_name_count": len(stock_bone_names),
        "bones": bones,
    }


def export_skeleton_asset(asset_dir: Path, skeleton: dict[str, Any]) -> dict[str, Any]:
    payload = {
        "format": "gh3_midori_skeleton_ir_v1",
        "skeleton": skeleton,
    }
    path = asset_dir / "skeleton" / "gh3_guitarist_midori.skeleton_ir.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")
    data = path.read_bytes()
    return {
        "relative_path": path.relative_to(asset_dir).as_posix(),
        "byte_size": len(data),
        "sha256": sha256_full(data),
        "bone_count": skeleton["bone_count"],
    }


def decode_quat_keys(
    reader: ByteReader,
    clip: gh3_datap.SkaHeader,
    bone_index: int,
    total_size: int,
    standardkey_q: list[tuple[int, int, int, int]] | None = None,
) -> list[dict[str, Any]]:
    if total_size <= 0:
        return []
    next_bone_pos = reader.offset + total_size
    long_quat_times = bool(clip.flags & SKAFLAG_LONGQUATTIMES or clip.flags & SKAFLAG_NOQUATCOMPRESSION)
    use_compression = not bool(clip.flags & SKAFLAG_NOQUATCOMPRESSION)
    keys = []
    while reader.offset < next_bone_pos:
        if clip.flags & SKAFLAG_HIRESFRAMES:
            qx = reader.f32()
            qy = reader.f32()
            qz = reader.f32()
            time = int(reader.f32() / 60.0)
            flagtime = None
        else:
            flagtime = 0
            whole_key_index = None
            component_indices: list[int | None] = [None, None, None]
            if long_quat_times:
                time = reader.u16()
            else:
                time = 0
            if use_compression:
                flagtime = reader.u16()
            if not long_quat_times and use_compression:
                time = flagtime & 0x07FF

            x_size = y_size = z_size = 2
            qx_i = qy_i = qz_i = 0
            if use_compression and flagtime & FLAG_SINGLEBYTEVALUE:
                if flagtime & FLAG_SINGLEBYTEX:
                    x_size = 1
                if flagtime & FLAG_SINGLEBYTEY:
                    y_size = 1
                if flagtime & FLAG_SINGLEBYTEZ:
                    z_size = 1
                if x_size == 2 and y_size == 2 and z_size == 2:
                    x_size = y_size = z_size = 0
                    whole_key_index = reader.u8()
                    if standardkey_q is None:
                        raise ValueError(
                            f"bone {bone_index} uses whole-key standardkeyQ index "
                            f"{whole_key_index}, but no standardkeyQ table was supplied"
                        )
                    qx_i, qy_i, qz_i, _ = standardkey_q[whole_key_index]

            component_sizes = [x_size, y_size, z_size]
            components = [qx_i, qy_i, qz_i]
            for component_index, component_size in enumerate(component_sizes):
                if component_size == 2:
                    components[component_index] = reader.i16()
                elif component_size == 1:
                    table_index = reader.u8()
                    component_indices[component_index] = table_index
                    if clip.flags & SKAFLAG_STANDARDKEY_COMPONENTS:
                        if standardkey_q is None:
                            raise ValueError(
                                f"bone {bone_index} uses standardkeyQ component index "
                                f"{table_index}, but no standardkeyQ table was supplied"
                            )
                        components[component_index] = standardkey_q[table_index][3]
                    else:
                        components[component_index] = (
                            table_index - 256 if table_index >= 128 else table_index
                        )
            qx_i, qy_i, qz_i = components
            qx = qx_i / QUAT_DIVISOR
            qy = qy_i / QUAT_DIVISOR
            qz = qz_i / QUAT_DIVISOR

        negative_w = bool(flagtime is not None and flagtime & 0x8000)
        raw_w = rebuild_w(qx, qy, qz, negative_w) if any((qx, qy, qz)) else 1.0

        keys.append({
            "time": time,
            "quat_wxyz": from_ska_quat(qx, qy, qz, negative_w),
            "raw_xyz": [qx, qy, qz],
            "raw_xyzw": [qx, qy, qz, raw_w],
            "flagtime": flagtime,
            "negative_w": negative_w,
            "whole_key_index": whole_key_index if not (clip.flags & SKAFLAG_HIRESFRAMES) else None,
            "component_indices": component_indices if not (clip.flags & SKAFLAG_HIRESFRAMES) else [None, None, None],
        })
    if reader.offset > next_bone_pos and keys:
        keys[-1]["decode_overrun_bytes"] = reader.offset - next_bone_pos
    reader.offset = next_bone_pos
    return keys


def decode_trans_keys(
    reader: ByteReader,
    clip: gh3_datap.SkaHeader,
    bone_index: int,
    total_size: int,
) -> list[dict[str, Any]]:
    if total_size <= 0:
        return []
    next_bone_pos = reader.offset + total_size
    keys = []
    while reader.offset < next_bone_pos:
        if clip.flags & SKAFLAG_HIRESFRAMES:
            tx = reader.f32()
            ty = reader.f32()
            tz = reader.f32()
            time = int(reader.f32())
            flagtime = None
        else:
            # GH3 PS2 stores translations as fixed-point shorts. This mirrors
            # GH-Toolkit-NET's WriteCompressedTransPs2/GetPs2TransData path.
            flagtime = reader.u8()
            if flagtime & TRANS_FLAGTIME_MAX:
                time = flagtime & 0x3F
            else:
                time = reader.u16()
            tx = reader.i16() / 256.0
            ty = reader.i16() / 256.0
            tz = reader.i16() / 256.0
        keys.append({
            "time": time,
            "translation": [tz, tx, ty],
            "raw_xyz": [tx, ty, tz],
            "flagtime": flagtime,
        })
    if reader.offset > next_bone_pos and keys:
        keys[-1]["decode_overrun_bytes"] = reader.offset - next_bone_pos
    reader.offset = next_bone_pos
    return keys


def decode_gh3_console_ska(
    entry: gh3_datap.DatapEntry,
    data: bytes,
    skeleton: dict[str, Any],
    standardkey_q: list[tuple[int, int, int, int]] | None = None,
) -> dict[str, Any]:
    header = gh3_datap.parse_ska_header(entry, data)
    if header.format != "gh3_console":
        raise ValueError(f"{entry.normalized_path} is not GH3 console SKA")
    reader = ByteReader(data)
    # NXTools leaves the reader at the end of ReadAnimationInfo when GH3
    # console offsets are -1, so the following blocks are read sequentially.
    reader.offset = 0x30
    if header.pos_bonesizes_quat >= 0:
        reader.offset = header.pos_bonesizes_quat
    quat_sizes = [reader.u16() for _ in range(header.bone_count)]
    if header.pos_bonesizes_trans >= 0:
        reader.offset = header.pos_bonesizes_trans
    trans_sizes = [reader.u16() for _ in range(header.bone_count)]

    partial_animation = None
    if header.flags & SKAFLAG_USEPARTIALFLAGS:
        partial_offset = reader.offset
        flag_bone_count = reader.u32()
        flag_word_count = (flag_bone_count + 31) // 32
        # GH3 PS2 stores both the count and packed mask words little-endian.
        # NXTools switches the reader to big-endian here, but that makes every
        # partial clip disagree with its own non-empty track rows. Reading the
        # retail words little-endian matches those rows exactly across the bank.
        flag_words = [reader.u32() for _ in range(flag_word_count)]
        allowed_bones = partial_allowed_bones(
            flag_words,
            min(flag_bone_count, header.bone_count),
        )
        partial_animation = {
            "offset": partial_offset,
            "bone_count": flag_bone_count,
            "word_count": flag_word_count,
            "words": [f"0x{value:08x}" for value in flag_words],
            "count_byte_order": "little",
            "word_byte_order": "little",
            "allowed_bones": allowed_bones,
        }
    allowed_bone_set = (
        set(partial_animation["allowed_bones"])
        if partial_animation is not None
        else set(range(header.bone_count))
    )

    if header.pos_quat_keys >= 0:
        reader.offset = header.pos_quat_keys
    quat_keys_by_bone = [
        decode_quat_keys(
            reader,
            header,
            bone_index,
            quat_sizes[bone_index],
            standardkey_q,
        )
        for bone_index in range(header.bone_count)
    ]
    quat_end = reader.offset

    if header.pos_trans_keys >= 0:
        reader.offset = header.pos_trans_keys
    trans_keys_by_bone = [
        decode_trans_keys(reader, header, bone_index, trans_sizes[bone_index])
        for bone_index in range(header.bone_count)
    ]
    trans_end = reader.offset

    bones = []
    skeleton_bones = skeleton.get("bones", [])
    for bone_index in range(header.bone_count):
        quat_keys = quat_keys_by_bone[bone_index]
        trans_keys = trans_keys_by_bone[bone_index]
        if not quat_keys and not trans_keys:
            continue
        skeleton_bone = skeleton_bones[bone_index] if bone_index < len(skeleton_bones) else {}
        bones.append({
            "index": bone_index,
            "name": skeleton_bone.get("stock_ghwt_name"),
            "name_checksum": skeleton_bone.get("name_checksum"),
            "partial_flag_allowed": bone_index in allowed_bone_set,
            "quat_key_count": len(quat_keys),
            "trans_key_count": len(trans_keys),
            "quat_keys": quat_keys,
            "trans_keys": trans_keys,
        })

    return {
        "format": "gh3_console_ska_ir_v2",
        "path": entry.normalized_path,
        "size": entry.size,
        "sha256_16": sha256_short(data),
        "header": asdict(header),
        "quat_sizes": quat_sizes,
        "trans_sizes": trans_sizes,
        "partial_animation": partial_animation,
        "quat_stream_end": quat_end,
        "trans_stream_end": trans_end,
        "animated_bone_count": len(bones),
        "quat_key_count": sum(len(keys) for keys in quat_keys_by_bone),
        "trans_key_count": sum(len(keys) for keys in trans_keys_by_bone),
        "standardkey_q_applied": standardkey_q is not None,
        "bones": bones,
    }


def export_animation_assets(
    iso_path: Path,
    wad: gh3_datap.IsoFile,
    entries: list[gh3_datap.DatapEntry],
    skeleton: dict[str, Any],
    asset_dir: Path,
    standardkey_q_data: bytes,
    standardkey_t_data: bytes,
) -> dict[str, Any]:
    items = (
        (entry, gh3_datap.read_datap_entry(iso_path, wad, entry))
        for entry in entries
        if gh3_datap.is_midori_base(entry) and gh3_datap.is_animation(entry)
    )
    return export_animation_asset_items(
        items,
        skeleton,
        asset_dir,
        standardkey_q_data,
        standardkey_t_data,
    )


def export_animation_asset_items(
    items: Iterable[tuple[gh3_datap.DatapEntry, bytes]],
    skeleton: dict[str, Any],
    asset_dir: Path,
    standardkey_q_data: bytes,
    standardkey_t_data: bytes,
) -> dict[str, Any]:
    """Decode animation blobs supplied by either DATAP or an extracted tree."""
    path = asset_dir / "animations" / "midori_ska_ir.jsonl.gz"
    path.parent.mkdir(parents=True, exist_ok=True)
    standardkey_q = parse_standardkey_table(standardkey_q_data, STANDARDKEY_Q)
    parse_standardkey_table(standardkey_t_data, STANDARDKEY_T)
    table_exports = []
    for name, table_data in (
        ("standardkeyq.bin", standardkey_q_data),
        ("standardkeyt.bin", standardkey_t_data),
    ):
        table_path = path.parent / name
        table_path.write_bytes(table_data)
        table_exports.append({
            "relative_path": table_path.relative_to(asset_dir).as_posix(),
            "byte_size": len(table_data),
            "sha256": sha256_full(table_data),
        })
    clip_count = 0
    quat_key_count = 0
    trans_key_count = 0
    boundary_snap_key_count = 0
    max_boundary_snap_bytes = 0
    boundary_snap_distribution: Counter[int] = Counter()
    animated_bone_counts: Counter[int] = Counter()
    errors = []
    with gzip.open(path, "wt", encoding="utf-8", newline="\n") as handle:
        for entry, data in items:
            try:
                clip = decode_gh3_console_ska(entry, data, skeleton, standardkey_q)
            except Exception as exc:
                errors.append({
                    "path": entry.normalized_path,
                    "error": f"{type(exc).__name__}: {exc}",
                })
                continue
            handle.write(json.dumps(clip, separators=(",", ":")) + "\n")
            clip_count += 1
            quat_key_count += clip["quat_key_count"]
            trans_key_count += clip["trans_key_count"]
            animated_bone_counts[clip["animated_bone_count"]] += 1
            for bone in clip["bones"]:
                for key in bone["quat_keys"] + bone["trans_keys"]:
                    snap = key.get("decode_overrun_bytes")
                    if snap:
                        boundary_snap_key_count += 1
                        max_boundary_snap_bytes = max(max_boundary_snap_bytes, snap)
                        boundary_snap_distribution[snap] += 1
    data = path.read_bytes()
    return {
        "relative_path": path.relative_to(asset_dir).as_posix(),
        "byte_size": len(data),
        "sha256": sha256_full(data),
        "clip_count": clip_count,
        "quat_key_count": quat_key_count,
        "trans_key_count": trans_key_count,
        "boundary_snap_key_count": boundary_snap_key_count,
        "max_boundary_snap_bytes": max_boundary_snap_bytes,
        "boundary_snap_distribution": {
            str(key): value for key, value in sorted(boundary_snap_distribution.items())
        },
        "animated_bone_count_distribution": {
            str(key): value for key, value in sorted(animated_bone_counts.items())
        },
        "compression_tables": table_exports,
        "errors": errors,
    }


def finite_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def as_vec3(value: Any) -> tuple[float, float, float] | None:
    if not isinstance(value, (list, tuple)) or len(value) < 3:
        return None
    coords = tuple(finite_float(value[i]) for i in range(3))
    if any(item is None for item in coords):
        return None
    return coords  # type: ignore[return-value]


def expand_bounds(
    bounds_min: list[float] | None,
    bounds_max: list[float] | None,
    pos: tuple[float, float, float],
) -> tuple[list[float], list[float]]:
    if bounds_min is None or bounds_max is None:
        return [pos[0], pos[1], pos[2]], [pos[0], pos[1], pos[2]]
    return (
        [min(bounds_min[i], pos[i]) for i in range(3)],
        [max(bounds_max[i], pos[i]) for i in range(3)],
    )


def rounded_vertex_key(vertex: tuple[Any, ...]) -> tuple[Any, ...]:
    pos = as_vec3(vertex[0]) if len(vertex) > 0 else None
    normal = as_vec3(vertex[1]) if len(vertex) > 1 else None
    u = finite_float(vertex[2]) if len(vertex) > 2 else None
    v = finite_float(vertex[3]) if len(vertex) > 3 else None
    weights = vertex[5] if len(vertex) > 5 else None
    if weights is None:
        weight_key: tuple[Any, ...] = ()
    elif isinstance(weights, (list, tuple)) and weights and isinstance(weights[0], (list, tuple)):
        weight_key = tuple((int(item[0]), round(float(item[1]), 6)) for item in weights)
    elif isinstance(weights, (list, tuple)) and len(weights) >= 2:
        weight_key = ((int(weights[0]), round(float(weights[1]), 6)),)
    else:
        weight_key = ()
    return (
        tuple(round(item, 6) for item in pos) if pos else None,
        tuple(round(item, 6) for item in normal) if normal else None,
        round(u, 6) if u is not None else None,
        round(v, 6) if v is not None else None,
        weight_key,
    )


def vertex_weight_pairs(vertex: tuple[Any, ...]) -> list[tuple[int, float]]:
    if len(vertex) <= 5 or vertex[5] is None:
        return []
    weights = vertex[5]
    if isinstance(weights, (list, tuple)) and weights and isinstance(weights[0], (list, tuple)):
        return [(int(item[0]), float(item[1])) for item in weights]
    if isinstance(weights, (list, tuple)) and len(weights) >= 2:
        return [(int(weights[0]), float(weights[1]))]
    return []


def vertex_to_ir(vertex: tuple[Any, ...]) -> dict[str, Any]:
    pos = as_vec3(vertex[0]) if len(vertex) > 0 else None
    normal = as_vec3(vertex[1]) if len(vertex) > 1 else None
    return {
        "position": list(pos) if pos is not None else None,
        "normal": list(normal) if normal is not None else None,
        "uv": [
            finite_float(vertex[2]) if len(vertex) > 2 else None,
            finite_float(vertex[3]) if len(vertex) > 3 else None,
        ],
        "restart": bool(vertex[4]) if len(vertex) > 4 else False,
        "weights": [
            {"bone": bone_index, "weight": weight}
            for bone_index, weight in vertex_weight_pairs(vertex)
        ],
    }


def vertex_identity(vertex: tuple[Any, ...]) -> tuple[Any, ...]:
    ir = vertex_to_ir(vertex)
    weights = tuple(
        (item["bone"], item["weight"])
        for item in ir["weights"]
    )
    return (
        tuple(ir["position"]) if ir["position"] is not None else None,
        tuple(ir["normal"]) if ir["normal"] is not None else None,
        tuple(ir["uv"]),
        ir["restart"],
        weights,
    )


def summarize_mesh(mesh: Any, index: int) -> dict[str, Any]:
    bounds_min: list[float] | None = None
    bounds_max: list[float] | None = None
    unique_vertices: set[tuple[Any, ...]] = set()
    weighted_vertices = 0
    unweighted_vertices = 0
    max_influences = 0
    bone_usage: Counter[int] = Counter()
    restart_vertices = 0

    for triangle in mesh.triangles:
        for vertex in triangle[:3]:
            if not isinstance(vertex, tuple):
                continue
            pos = as_vec3(vertex[0]) if len(vertex) > 0 else None
            if pos is not None:
                bounds_min, bounds_max = expand_bounds(bounds_min, bounds_max, pos)
            unique_vertices.add(rounded_vertex_key(vertex))
            if len(vertex) > 4 and vertex[4]:
                restart_vertices += 1
            pairs = vertex_weight_pairs(vertex)
            if pairs:
                weighted_vertices += 1
                max_influences = max(max_influences, len(pairs))
                for bone_index, weight in pairs:
                    if weight:
                        bone_usage[bone_index] += 1
            else:
                unweighted_vertices += 1

    return {
        "index": index,
        "material_checksum": checksum(mesh.material_checksum),
        "texture_checksum": checksum(mesh.texture_checksum),
        "owner_object_checksum": checksum(mesh.owner_object_checksum),
        "vertex_color": checksum(mesh.vertex_color),
        "triangle_count": len(mesh.triangles),
        "unique_vertex_count": len(unique_vertices),
        "bounds_min": bounds_min,
        "bounds_max": bounds_max,
        "restart_vertex_references": restart_vertices,
        "weighted_vertex_references": weighted_vertices,
        "unweighted_vertex_references": unweighted_vertices,
        "max_influences_per_vertex": max_influences,
        "declared_bone_indices": sorted(int(item) for item in mesh.bone_indices),
        "used_bone_indices": sorted(bone_usage),
    }


def summarize_texture(ps2_tex: Any, texture: Any) -> dict[str, Any]:
    pixels = texture.pixels
    return {
        "checksum": checksum(texture.checksum),
        "width": texture.width,
        "height": texture.height,
        "psm": ps2_tex.DescribePSM(texture.psm),
        "cpsm": ps2_tex.DescribePSM(texture.cpsm),
        "rgba_byte_count": len(pixels) if pixels is not None else None,
        "rgba_sha256_16": sha256_short(pixels),
    }


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, width: int, height: int, pixels: bytes | bytearray) -> None:
    expected = width * height * 4
    if len(pixels) != expected:
        raise ValueError(
            f"RGBA pixel buffer length {len(pixels)} does not match {width}x{height}"
        )
    rows = []
    for y in range(height):
        start = y * width * 4
        rows.append(b"\x00" + bytes(pixels[start:start + width * 4]))
    payload = b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
            png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)),
            png_chunk(b"IEND", b""),
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def export_texture_assets(
    ps2_tex: Any,
    asset_dir: Path,
    outfit_name: str,
    textures: list[Any],
) -> list[dict[str, Any]]:
    exported = []
    for index, texture in enumerate(textures):
        pixels = texture.pixels
        if pixels is None:
            exported.append({
                "index": index,
                "checksum": checksum(texture.checksum),
                "png": None,
                "error": "texture pixels were not decoded",
            })
            continue
        name = f"{outfit_name}_{checksum(texture.checksum)}.png".replace("0x", "")
        path = asset_dir / "textures" / name
        write_rgba_png(path, texture.width, texture.height, pixels)
        data = path.read_bytes()
        exported.append({
            "index": index,
            "checksum": checksum(texture.checksum),
            "width": texture.width,
            "height": texture.height,
            "psm": ps2_tex.DescribePSM(texture.psm),
            "relative_path": path.relative_to(asset_dir).as_posix(),
            "byte_size": len(data),
            "sha256": sha256_full(data),
        })
    return exported


def export_mesh_assets(asset_dir: Path, outfit_name: str, meshes: list[Any]) -> dict[str, Any]:
    mesh_docs = []
    for mesh_index, mesh in enumerate(meshes):
        vertex_lookup: dict[tuple[Any, ...], int] = {}
        vertices: list[dict[str, Any]] = []
        triangles: list[list[int]] = []
        cas_flags: list[int] = []
        for triangle in mesh.triangles:
            indices = []
            for vertex in triangle[:3]:
                key = vertex_identity(vertex)
                found = vertex_lookup.get(key)
                if found is None:
                    found = len(vertices)
                    vertex_lookup[key] = found
                    vertices.append(vertex_to_ir(vertex))
                indices.append(found)
            triangles.append(indices)
            cas_flags.append(int(triangle[3]) if len(triangle) > 3 else 0)
        mesh_docs.append({
            "index": mesh_index,
            "material_checksum": checksum(mesh.material_checksum),
            "texture_checksum": checksum(mesh.texture_checksum),
            "owner_object_checksum": checksum(mesh.owner_object_checksum),
            "vertex_color": checksum(mesh.vertex_color),
            "declared_bone_indices": sorted(int(item) for item in mesh.bone_indices),
            "vertices": vertices,
            "triangles": triangles,
            "cas_flags": cas_flags,
        })

    payload = {
        "format": "gh3_midori_mesh_ir_v1",
        "outfit": outfit_name,
        "mesh_count": len(mesh_docs),
        "meshes": mesh_docs,
    }
    text = json.dumps(payload, separators=(",", ":"))
    path = asset_dir / "outfits" / f"{outfit_name}.mesh_ir.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text + "\n", encoding="utf-8")
    data = path.read_bytes()
    return {
        "relative_path": path.relative_to(asset_dir).as_posix(),
        "byte_size": len(data),
        "sha256": sha256_full(data),
        "mesh_count": len(mesh_docs),
        "vertex_count": sum(len(mesh["vertices"]) for mesh in mesh_docs),
        "triangle_count": sum(len(mesh["triangles"]) for mesh in mesh_docs),
    }


def summarize_outfit(
    ps2_skin: Any,
    ps2_tex: Any,
    iso_path: Path,
    wad: gh3_datap.IsoFile,
    by_path: dict[str, gh3_datap.DatapEntry],
    outfit: dict[str, str],
    asset_dir: Path | None = None,
) -> dict[str, Any]:
    skin_entry = by_path[outfit["skin"]]
    texture_entry = by_path[outfit["texture"]]
    skin_data = gh3_datap.read_datap_entry(iso_path, wad, skin_entry)
    texture_data = gh3_datap.read_datap_entry(iso_path, wad, texture_entry)

    tbp_cbp_map = ps2_tex.BuildTBPCBPMap(texture_data)
    scene = ps2_skin.ParsePS2Skin(skin_data, tbp_cbp_map)
    textures = ps2_tex.ParsePS2SceneTex(texture_data)
    meshes = [summarize_mesh(mesh, index) for index, mesh in enumerate(scene.meshes)]
    all_bones = sorted({bone for mesh in meshes for bone in mesh["used_bone_indices"]})
    asset_exports = None
    if asset_dir is not None:
        asset_exports = {
            "mesh_ir": export_mesh_assets(asset_dir, outfit["name"], scene.meshes),
            "textures": export_texture_assets(ps2_tex, asset_dir, outfit["name"], textures),
        }

    result = {
        "name": outfit["name"],
        "skin_path": outfit["skin"],
        "texture_path": outfit["texture"],
        "skin_size": skin_entry.size,
        "texture_size": texture_entry.size,
        "skin_sha256_16": sha256_short(skin_data),
        "texture_sha256_16": sha256_short(texture_data),
        "is_ps2_skin": bool(ps2_skin.IsPS2Skin(skin_data)),
        "is_gh5_layout": bool(ps2_skin.IsPS2SkinGH5(skin_data)),
        "material_entry_count": len(scene.entries),
        "material_names": [checksum(item) for item in scene.material_names],
        "texture_count": len(textures),
        "tbp_cbp_map_count": len(tbp_cbp_map),
        "textures": [summarize_texture(ps2_tex, texture) for texture in textures],
        "mesh_count": len(meshes),
        "triangle_count": sum(mesh["triangle_count"] for mesh in meshes),
        "unique_vertex_count": sum(mesh["unique_vertex_count"] for mesh in meshes),
        "used_bone_indices": all_bones,
        "meshes": meshes,
    }
    if asset_exports is not None:
        result["asset_exports"] = asset_exports
    return result


def write_source_package_manifest(
    asset_dir: Path,
    payload: dict[str, Any],
    skeleton_export: dict[str, Any] | None,
    animation_export: dict[str, Any] | None,
) -> None:
    outfit_paths = [
        {
            "selection": "gh3_midori_1",
            "model": "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
            "ui_model": "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
            "ui_anim": "char/gh3_midori/anims/gen/gh3_midori_ui.milo_ps2",
            "main_anim": "char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2",
            "strum_anim": "char/gh3_midori/anims/gen/gh3_midori_strum.milo_ps2",
            "fret_anim": "char/gh3_midori/anims/gen/gh3_midori_fret.milo_ps2",
            "animation_source_model": "char/gh3_midori_1/og/gen/gh3_midori_1.milo_ps2",
            "retarget_animation": False,
        },
        {
            "selection": "gh3_midori_2",
            "model": "char/gh3_midori_2/og/gen/gh3_midori_2.milo_ps2",
            "ui_model": "char/gh3_midori_2/og/gen/gh3_midori_2.milo_ps2",
            "ui_anim": "char/gh3_midori/anims/gen/gh3_midori_ui.milo_ps2",
            "main_anim": "char/gh3_midori/anims/gen/gh3_midori_main.milo_ps2",
            "strum_anim": "char/gh3_midori/anims/gen/gh3_midori_strum.milo_ps2",
            "fret_anim": "char/gh3_midori/anims/gen/gh3_midori_fret.milo_ps2",
            "animation_source_model": "char/gh3_midori_2/og/gen/gh3_midori_2.milo_ps2",
            "retarget_animation": False,
        },
    ]
    package_payload = {
        "format": "gh3_midori_source_ir_package_v1",
        "character": payload["scope"]["character"],
        "included_models": payload["scope"]["included_models"],
        "alternate_skins_excluded": payload["scope"]["alternate_skins_excluded"],
        "skeleton": payload["skeleton"],
        "skeleton_ir": skeleton_export,
        "outfits": [
            {
                "name": outfit["name"],
                "skin_path": outfit["skin_path"],
                "texture_path": outfit["texture_path"],
                "mesh_ir": outfit.get("asset_exports", {}).get("mesh_ir"),
                "textures": outfit.get("asset_exports", {}).get("textures", []),
            }
            for outfit in payload["outfits"]
        ],
        "animations": {
            "clip_count": payload["animations"]["clip_count"],
            "format_counts": payload["animations"]["format_counts"],
            "bone_count_distribution": payload["animations"]["bone_count_distribution"],
            "clips": payload["animations"]["clips"],
            "ska_ir": animation_export,
        },
        "gh2_destination": {
            "package_id": "community.gh3.midori",
            "content_root": "content",
            "outfit_paths": outfit_paths,
        },
    }
    path = asset_dir / "midori_source_ir_manifest.json"
    path.write_text(json.dumps(package_payload, indent=2) + "\n", encoding="utf-8")
    dlc_manifest = {
        "schema_version": 1,
        "id": "community.gh3.midori",
        "name": "GH3 Midori",
        "version": "0.1.0",
        "content_root": "content",
        "characters": [
            {
                "id": "gh3_midori",
                "label": "Midori",
                "outfits": [
                    {"selection": "gh3_midori_1", "label": "Outfit 1"} | outfit_paths[0],
                    {"selection": "gh3_midori_2", "label": "Outfit 2"} | outfit_paths[1],
                ],
            }
        ],
    }
    (asset_dir / "gh2_dlc_manifest.draft.json").write_text(
        json.dumps(dlc_manifest, indent=2) + "\n",
        encoding="utf-8",
    )


def summarize_animations(
    iso_path: Path,
    wad: gh3_datap.IsoFile,
    entries: list[gh3_datap.DatapEntry],
) -> dict[str, Any]:
    clips: list[dict[str, Any]] = []
    for entry in entries:
        if not gh3_datap.is_midori_base(entry) or not gh3_datap.is_animation(entry):
            continue
        data = gh3_datap.read_datap_entry(iso_path, wad, entry)
        header = gh3_datap.parse_ska_header(entry, data)
        clips.append(asdict(header) | {"sha256_16": sha256_short(data)})

    bone_counts = Counter(item["bone_count"] for item in clips)
    formats = Counter(item["format"] for item in clips)
    durations = [item["duration_seconds"] for item in clips]
    return {
        "clip_count": len(clips),
        "format_counts": dict(sorted(formats.items())),
        "bone_count_distribution": {str(key): value for key, value in sorted(bone_counts.items())},
        "duration_min": min(durations) if durations else None,
        "duration_max": max(durations) if durations else None,
        "clips": clips,
    }


def command_export(args: argparse.Namespace) -> int:
    nxtools_root = args.nxtools.resolve()
    ps2_skin, ps2_tex = load_nxtools(nxtools_root)
    apply_gh3_ps2_texture_overrides(ps2_tex)
    stock_bone_names = load_ghwt_bone_names(nxtools_root)
    hed, wad, entries = gh3_datap.load_entries(args.iso)
    by_path = {entry.normalized_path: entry for entry in entries}

    skeleton_entry = by_path[MIDORI_SKELETON]
    skeleton_data = gh3_datap.read_datap_entry(args.iso, wad, skeleton_entry)
    skeleton = parse_gh3_ps2_skeleton(MIDORI_SKELETON, skeleton_data, stock_bone_names)
    standardkey_q_data = gh3_datap.read_datap_entry(args.iso, wad, by_path[STANDARDKEY_Q])
    standardkey_t_data = gh3_datap.read_datap_entry(args.iso, wad, by_path[STANDARDKEY_T])
    skeleton_export = None
    animation_export = None
    if args.asset_dir:
        skeleton_export = export_skeleton_asset(args.asset_dir, skeleton)
        animation_export = export_animation_assets(
            args.iso,
            wad,
            entries,
            skeleton,
            args.asset_dir,
            standardkey_q_data,
            standardkey_t_data,
        )
    outfits = [
        summarize_outfit(ps2_skin, ps2_tex, args.iso, wad, by_path, outfit, args.asset_dir)
        for outfit in MIDORI_OUTFITS
    ]
    payload = {
        "iso": str(args.iso),
        "nxtools": str(nxtools_root),
        "hed": asdict(hed),
        "wad": asdict(wad),
        "scope": {
            "character": "midori",
            "included_models": [item["name"] for item in MIDORI_OUTFITS],
            "alternate_skins_excluded": True,
            "skeleton": MIDORI_SKELETON,
        },
        "compression_tables": {
            "quaternion": {
                "path": STANDARDKEY_Q,
                "byte_size": len(standardkey_q_data),
                "sha256": sha256_full(standardkey_q_data),
            },
            "translation": {
                "path": STANDARDKEY_T,
                "byte_size": len(standardkey_t_data),
                "sha256": sha256_full(standardkey_t_data),
            },
        },
        "skeleton": skeleton,
        "outfits": outfits,
        "animations": summarize_animations(args.iso, wad, entries),
    }
    if args.asset_dir:
        args.asset_dir.mkdir(parents=True, exist_ok=True)
        write_source_package_manifest(args.asset_dir, payload, skeleton_export, animation_export)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        package_manifest = (
            args.asset_dir / "midori_source_ir_manifest.json"
            if args.asset_dir
            else None
        )
        if package_manifest is None or args.output.resolve() != package_manifest.resolve():
            text = json.dumps(payload, indent=2)
            args.output.write_text(text + "\n", encoding="utf-8")
    else:
        text = json.dumps(payload, indent=2)
        print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=Path("Guitar Hero III - Legends of Rock (USA).iso"))
    parser.add_argument("--nxtools", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--asset-dir",
        type=Path,
        help="Optional directory for full mesh IR JSON and decoded PNG textures.",
    )
    parser.set_defaults(func=command_export)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.iso.exists():
        raise SystemExit(f"ISO not found: {args.iso}")
    if not (args.nxtools / "ps2_skin.py").exists():
        raise SystemExit(f"NXTools checkout missing ps2_skin.py: {args.nxtools}")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
