#!/usr/bin/env python3
"""Reject stale RB2 donors whose skin offsets do not match their body-shape bake.

Input is a native milo_convert_tool export-character-snapshot of the source
donor, before merging the target rig. Atlas UVs are intentionally not compared.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

from convert_rb2_preset_character import (
    apply_deform_to_transforms, bone_deform_matrices, collect_meshes,
    evaluate_deform, invert_affine, load_transforms, matrix4, matrix12,
    parse_deform_clip,
)


def read_snapshot(path: Path) -> dict:
    data = path.read_bytes()
    offset = 0

    def take(size: int) -> bytes:
        nonlocal offset
        value = data[offset:offset + size]
        if len(value) != size:
            raise ValueError("truncated character snapshot")
        offset += size
        return value

    def u32() -> int:
        return struct.unpack("<I", take(4))[0]

    def string() -> str:
        return take(u32()).decode("utf-8")

    if take(8) != b"GH2M2GLB" or u32() != 1:
        raise ValueError("unsupported character snapshot")
    string()
    string()
    for _ in range(u32()):
        string()
        string()
        take(96)
    meshes = {}
    for _ in range(u32()):
        name = string()
        string()
        string()
        take(97)
        slots = [(string(), struct.unpack("<12f", take(48))) for _ in range(4)]
        vertices = [struct.unpack("<12f", take(48)) for _ in range(u32())]
        take(u32() * 12)
        if name in meshes:
            raise ValueError("duplicate snapshot mesh: " + name)
        meshes[name] = (slots, vertices)
    if offset != len(data):
        raise ValueError("trailing character snapshot bytes")
    return meshes


def validate(snapshot: Path, root: Path, recipe_path: Path,
             deform_path: Path | None) -> dict:
    recipe = json.loads(recipe_path.read_text(encoding="utf-8-sig"))
    transforms, _, _ = load_transforms(root, [recipe["source_skeleton"]] +
        [component["directory"] for component in recipe["components"]])
    deformed = deltas = None
    if deform_path is not None:
        shape = recipe["body_shape"]
        channels = evaluate_deform(parse_deform_clip(deform_path).full,
                                   float(shape["height"]), float(shape["weight"]))
        deformed = apply_deform_to_transforms(transforms, channels)
        deltas = bone_deform_matrices(transforms, deformed)
    chunks, binds, _, _ = collect_meshes(root, recipe, transforms,
                                         recipe["materials"], {}, deltas, deformed)
    actual = read_snapshot(snapshot)
    if set(actual) != {chunk["name"] for chunk in chunks}:
        raise ValueError("source donor mesh inventory differs from recipe")
    max_position_error = max_offset_error = 0.0
    slots_checked = vertices_checked = 0
    worst_slot = None
    for chunk in chunks:
        slots, vertices = actual[chunk["name"]]
        if len(vertices) != len(chunk["vertices"]):
            raise ValueError("source vertex count differs: " + chunk["name"])
        for i, bone in enumerate(chunk["bone_slots"]):
            if slots[i][0] != bone:
                raise ValueError("source bone mapping differs: " + chunk["name"])
            expected = matrix12(invert_affine(matrix4(
                binds[chunk["bind_names"][i]]["world"])))
            error = max(abs(a-b) for a, b in zip(slots[i][1], expected))
            if not all(math.isfinite(v) for v in slots[i][1]):
                raise ValueError("nonfinite source bind offset")
            if error > max_offset_error:
                max_offset_error = error
                worst_slot = {"mesh": chunk["name"], "bone": bone}
            slots_checked += 1
        for got, expected in zip(vertices, chunk["vertices"]):
            if any(abs(a-b) > 0.000001 for a, b in
                   zip(got[6:10], expected["color_or_weights"])):
                raise ValueError("source skin weights differ: " + chunk["name"])
            error = math.dist(got[:3], expected["position"])
            if not math.isfinite(error):
                raise ValueError("nonfinite source vertex")
            max_position_error = max(max_position_error, error)
            vertices_checked += 1
    return {"passed": max(max_position_error, max_offset_error) <= 0.0001,
            "meshes": len(chunks), "vertices": vertices_checked,
            "slots": slots_checked, "max_position_error": max_position_error,
            "max_offset_error": max_offset_error, "worst_slot": worst_slot}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--component-root", type=Path, required=True)
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--deform-clip", type=Path)
    parser.add_argument("--audit", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.snapshot, args.component_root, args.recipe, args.deform_clip)
    args.audit.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
