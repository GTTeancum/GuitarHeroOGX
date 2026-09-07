#!/usr/bin/env python3
"""Bake one source-defined RB2 Wii prefab into a GH2 meshbundle.

The input is the native ``milo_tool extract`` output for the prefab's resource
MILOs.  Geometry, skin weights, bind matrices, transforms, textures, palette
indices, and render flags are carried from those extracted source objects; the
JSON recipe only declares which retail prefab parts compose the character.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import re
import struct
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

from PIL import Image

from convert_rb2_instruments import parse_color_palette
from rb2_native_assets import (
    FormatError,
    Reader,
    compose_two_color,
    decode_embedded_wii_bitmap,
    identity_matrix,
    invert_affine,
    matrix4,
    matrix_multiply,
    normalize,
    parse_mesh34,
    parse_standalone_transform,
    skip_object_fields,
    source_world,
    transform_direction,
    transform_position,
)
from rb2_deform import (
    deform_sample_weights,
    evaluate_deform,
    parse_deform_clip,
)
from rb2_ambient_occlusion import bake_ambient_occlusion


MAGIC = b"GH3M2MB\0"
VERSION = 9


def asset_prefix(recipe: dict[str, Any]) -> str:
    value = str(recipe.get("asset_prefix", recipe["id"])).lower()
    value = re.sub(r"[^a-z0-9]+", "_", value).strip("_")
    if not value:
        raise ValueError("recipe id does not produce a usable asset prefix")
    return value


def quaternion_rows(value: tuple[float, ...]) -> list[list[float]]:
    x, y, z, w = value
    return [
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + z * w), 2.0 * (x * z - y * w)],
        [2.0 * (x * y - z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + x * w)],
        [2.0 * (x * z + y * w), 2.0 * (y * z - x * w), 1.0 - 2.0 * (x * x + y * y)],
    ]


def apply_deform_to_transforms(
    transforms: dict[str, Any], channels: dict[str, tuple[float, ...]]
) -> dict[str, Any]:
    """Apply RB2's authored absolute local transform channels to a rig copy."""
    result = copy.deepcopy(transforms)
    grouped: dict[str, dict[str, tuple[float, ...]]] = defaultdict(dict)
    for name, value in channels.items():
        stem, kind = name.rsplit(".", 1)
        key = stem.lower()
        if key not in result and f"{key}.mesh" in result:
            key = f"{key}.mesh"
        grouped[key][kind] = value
    for key, values in grouped.items():
        transform = result.get(key)
        if transform is None:
            continue
        local = list(transform.local)
        current_scales = [
            math.sqrt(sum(local[row * 3 + column] ** 2 for column in range(3)))
            for row in range(3)
        ]
        rows = [
            [
                local[row * 3 + column] / max(current_scales[row], 1.0e-8)
                for column in range(3)
            ]
            for row in range(3)
        ]
        if "quat" in values:
            rows = quaternion_rows(values["quat"])
        scales = list(values.get("scale", tuple(current_scales)))
        for row in range(3):
            for column in range(3):
                local[row * 3 + column] = rows[row][column] * scales[row]
        if "pos" in values:
            local[9:12] = values["pos"]
        transform.local = local
    return result


def bone_deform_matrices(
    neutral: dict[str, Any], deformed: dict[str, Any]
) -> dict[str, list[float]]:
    result: dict[str, list[float]] = {}
    for key in neutral:
        result[key] = matrix_multiply(
            invert_affine(source_world(key, neutral)),
            source_world(key, deformed),
        )
    return result


def write_u8(out: bytearray, value: int) -> None:
    out += struct.pack("<B", value)


def write_u16(out: bytearray, value: int) -> None:
    out += struct.pack("<H", value)


def write_u32(out: bytearray, value: int) -> None:
    out += struct.pack("<I", value)


def write_f32(out: bytearray, value: float) -> None:
    out += struct.pack("<f", value)


def write_string(out: bytearray, value: str) -> None:
    raw = value.encode("utf-8")
    write_u32(out, len(raw))
    out += raw


def write_bytes(out: bytearray, value: bytes) -> None:
    write_u32(out, len(value))
    out += value


def matrix12(value: list[float]) -> list[float]:
    return [
        value[0], value[1], value[2],
        value[4], value[5], value[6],
        value[8], value[9], value[10],
        value[12], value[13], value[14],
    ]


def material_flags(path: Path) -> dict[str, Any]:
    reader = Reader(path.read_bytes())
    revision = reader.i32()
    if revision <= 21:
        raise FormatError(f"expected modern RndMat in {path}, got {revision}")
    skip_object_fields(reader)
    blend = reader.u32()
    reader.floats(4)
    reader.u8()
    reader.u8()
    z_mode = reader.i32()
    alpha_cut = bool(reader.u8())
    if revision > 0x25:
        reader.i32()
    alpha_write = bool(reader.u8())
    reader.i32()
    reader.i32()
    reader.floats(12)
    reader.string()
    reader.string()
    reader.u8()
    cull = bool(reader.u8())
    return {
        "blend": blend,
        "z_mode": z_mode,
        "alpha_cut": alpha_cut,
        "alpha_write": alpha_write,
        "cull": cull,
    }


def palette_color(
    palettes: dict[str, list[tuple[int, int, int]]], name: str, index: int
) -> tuple[int, int, int]:
    if name not in palettes:
        raise KeyError(f"missing RB2 palette {name}")
    if index < 0 or index >= len(palettes[name]):
        raise IndexError(f"palette {name} has no color {index}")
    return palettes[name][index]


def compose_unmasked_two_color(
    diffuse: Image.Image,
    primary: tuple[int, int, int],
    secondary: tuple[int, int, int],
) -> Image.Image:
    """Apply RB2 diffuse-alpha palette interpolation without a fixed mask."""
    base = diffuse.convert("RGBA")
    source = base.tobytes()
    output = bytearray(len(source))
    for offset in range(0, len(source), 4):
        interpolation = source[offset + 3]
        for channel in range(3):
            tint = (
                (255 - interpolation) * primary[channel]
                + interpolation * secondary[channel]
                + 127
            ) // 255
            output[offset + channel] = (source[offset + channel] * tint + 127) // 255
        output[offset + 3] = source[offset + 3]
    return Image.frombytes("RGBA", base.size, bytes(output))


def compose_single_palette_color(
    diffuse: Image.Image,
    primary: tuple[int, int, int],
) -> Image.Image:
    """Reproduce an RB2 material whose runtime changes only color one.

    ``CompositeCharacter::SetEyeColor`` writes the selected palette entry to
    the material's first color and immediately calls ``RndMat::CompositeTwoColor``.
    The retail material's untouched second color is white.  The diffuse alpha
    interpolates from the selected color at zero to white at 255, preserving
    fixed-color areas such as the sclera while tinting the iris.  Skin and
    other one-channel character materials use the same compositor contract.
    """
    return compose_unmasked_two_color(diffuse, primary, (255, 255, 255))


def validate_material_texture_spec(spec: dict[str, Any]) -> None:
    """Reject RB2 compositor outputs being mistaken for authored masks.

    Character outfit ``*_comp`` textures are zero-mip scratch/output surfaces
    populated by RB2's CompositeTwoColor pass.  Their on-disc pixels are not an
    authored mask and can contain uninitialised tile data.  Authored fixed-color
    masks use the ``*_mask`` convention.  When no mask exists, the diffuse alpha
    alone drives the two palette colors.
    """
    mask = spec.get("mask")
    if mask is None:
        return
    if Path(str(mask)).name.lower().endswith("_comp"):
        raise ValueError(
            f"{mask}: RB2 *_comp textures are compositor outputs, not masks; "
            "omit mask or select the authored *_mask texture"
        )


def load_texture(root: Path, spec: str) -> Image.Image:
    directory, stem = spec.split("/", 1)
    path = root / directory / f"Tex__{stem}.tex"
    if not path.is_file():
        raise FileNotFoundError(path)
    return decode_embedded_wii_bitmap(path.read_bytes())


def encode_hmx_texture(image: Image.Image) -> dict[str, Any]:
    image = image.convert("RGBA")
    rgba = image.tobytes()
    hmx = bytearray()
    for offset in range(0, len(rgba), 4):
        hmx.extend(rgba[offset:offset + 3])
        hmx.append(min(128, (rgba[offset + 3] + 1) // 2))
    return {
        "width": image.width,
        "height": image.height,
        "bits_per_pixel": 32,
        "header_kind": 1,
        "encoding": 3,
        "mipmap_count": 0,
        "bytes_per_line": image.width * 4,
        "wii_alpha": 0,
        "data": bytes(hmx),
    }


def make_texture(
    root: Path,
    spec: dict[str, Any],
    palettes: dict[str, list[tuple[int, int, int]]],
) -> tuple[dict[str, Any], dict[str, Any], Image.Image]:
    validate_material_texture_spec(spec)
    diffuse = load_texture(root, spec["texture"])
    primary = None
    secondary = None
    if "primary" in spec:
        primary = palette_color(
            palettes,
            spec.get("primary_palette", spec.get("palette")),
            int(spec["primary"]),
        )
    if "secondary" in spec:
        secondary = palette_color(
            palettes,
            spec.get("secondary_palette", spec.get("palette")),
            int(spec["secondary"]),
        )
    if primary is not None and secondary is not None:
        if "mask" in spec:
            image = compose_two_color(
                diffuse, load_texture(root, spec["mask"]), primary, secondary
            )
        else:
            image = compose_unmasked_two_color(diffuse, primary, secondary)
    elif primary is not None and "mask" in spec:
        image = compose_two_color(
            diffuse, load_texture(root, spec["mask"]), primary, primary
        )
    elif primary is not None:
        image = compose_single_palette_color(diffuse, primary)
    else:
        image = diffuse.convert("RGBA")

    overlay_audit: list[dict[str, Any]] = []
    for overlay_spec in spec.get("overlays", []):
        overlay = load_texture(root, overlay_spec["texture"]).convert("RGBA")
        source_size = list(overlay.size)
        fit = str(overlay_spec.get("fit", "normalized_uv"))
        if fit != "normalized_uv":
            raise ValueError(f"unsupported texture overlay fit {fit!r}")
        if overlay.size != image.size:
            # RB2's character-compositor maps these independent makeup maps
            # over the normalized head UV domain.  Resizing to the authored
            # head-map dimensions reproduces that mapping without a
            # character-specific pixel offset.
            overlay = overlay.resize(image.size, Image.Resampling.BILINEAR)
        image = Image.alpha_composite(image.convert("RGBA"), overlay)
        overlay_audit.append({
            "source": overlay_spec["texture"],
            "source_size": source_size,
            "fit": fit,
            "output_size": list(image.size),
        })

    preserve_alpha = bool(spec.get("preserve_alpha", False))
    if not preserve_alpha:
        image = image.copy()
        image.putalpha(255)
    texture = encode_hmx_texture(image)
    audit = {
        "source": spec["texture"],
        "size": [image.width, image.height],
        "primary_rgb": primary,
        "secondary_rgb": secondary,
        "preserve_alpha": preserve_alpha,
        "alpha_range": list(image.getchannel("A").getextrema()),
        "overlays": overlay_audit,
    }
    return texture, audit, image


def _paste_with_gutter(
    atlas: Image.Image, image: Image.Image, x: int, y: int, padding: int
) -> None:
    image = image.convert("RGBA")
    width, height = image.size
    atlas.paste(image, (x, y))
    if padding <= 0:
        return
    atlas.paste(image.crop((0, 0, 1, height)).resize((padding, height)), (x - padding, y))
    atlas.paste(image.crop((width - 1, 0, width, height)).resize((padding, height)), (x + width, y))
    atlas.paste(image.crop((0, 0, width, 1)).resize((width, padding)), (x, y - padding))
    atlas.paste(image.crop((0, height - 1, width, height)).resize((width, padding)), (x, y + height))
    corners = [
        ((0, 0, 1, 1), (x - padding, y - padding)),
        ((width - 1, 0, width, 1), (x + width, y - padding)),
        ((0, height - 1, 1, height), (x - padding, y + height)),
        ((width - 1, height - 1, width, height), (x + width, y + height)),
    ]
    for crop, position in corners:
        atlas.paste(image.crop(crop).resize((padding, padding)), position)


def build_texture_atlases(
    recipe: dict[str, Any],
    material_images: dict[str, Image.Image],
    chunks: list[dict[str, Any]],
) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    config = recipe.get("texture_atlas")
    if not config or not bool(config.get("enabled", False)):
        return {}, {"enabled": False}
    size = int(config.get("size", 512))
    padding = int(config.get("padding", 4))
    if size <= 0 or padding < 0:
        raise ValueError("texture_atlas size/padding must be non-negative")

    placements: dict[str, dict[str, int]] = {}
    pages: list[dict[str, Any]] = []
    ordered = sorted(
        material_images.items(),
        key=lambda row: (-row[1].height, -row[1].width, row[0].lower()),
    )
    for material, image in ordered:
        item_padding = min(
            padding,
            max(0, (size - image.width) // 2),
            max(0, (size - image.height) // 2),
        )
        # Exact half/full-page maps tile without a gutter.  Retaining their
        # authored resolution is preferable to resampling; Duke's source UVs
        # stay inside the map edges and the output carries no mip chain.
        if image.width >= size // 2 or image.height >= size // 2:
            item_padding = 0
        outer_w = image.width + item_padding * 2
        outer_h = image.height + item_padding * 2
        if outer_w > size or outer_h > size:
            raise ValueError(
                f"{material} texture {image.size} exceeds {size}px atlas with gutter"
            )
        chosen = None
        for page_index, page in enumerate(pages):
            x, y, row_height = page["x"], page["y"], page["row_height"]
            if x + outer_w > size:
                x, y, row_height = 0, y + row_height, 0
            if y + outer_h <= size:
                chosen = (page_index, x, y, row_height)
                break
        if chosen is None:
            pages.append({"x": 0, "y": 0, "row_height": 0})
            chosen = (len(pages) - 1, 0, 0, 0)
        page_index, outer_x, outer_y, row_height = chosen
        page = pages[page_index]
        page["x"] = outer_x + outer_w
        page["y"] = outer_y
        page["row_height"] = max(row_height, outer_h)
        placements[material] = {
            "page": page_index,
            "x": outer_x + item_padding,
            "y": outer_y + item_padding,
            "width": image.width,
            "height": image.height,
            "padding": item_padding,
        }

    prefix = asset_prefix(recipe)
    atlases = [Image.new("RGBA", (size, size), (0, 0, 0, 0)) for _ in pages]
    for material, image in material_images.items():
        place = placements[material]
        _paste_with_gutter(
            atlases[place["page"]], image, place["x"], place["y"],
            place["padding"],
        )
    textures = {
        f"{prefix}_atlas_{index}.tex": encode_hmx_texture(image)
        for index, image in enumerate(atlases)
    }
    for chunk in chunks:
        place = placements[chunk["material"]]
        chunk["texture"] = f"{prefix}_atlas_{place['page']}.tex"
        for vertex in chunk["vertices"]:
            u, v = vertex["uv"]
            # Retail Duke UVs are authored for edge-clamped character maps;
            # clamp their small artist bleed into the duplicated atlas gutter.
            u = min(1.0, max(0.0, u))
            v = min(1.0, max(0.0, v))
            vertex["uv"] = [
                (place["x"] + u * place["width"]) / size,
                (place["y"] + v * place["height"]) / size,
            ]
    return textures, {
        "enabled": True,
        "size": [size, size],
        "padding": padding,
        "source_textures": len(material_images),
        "atlas_pages": len(atlases),
        "placements": placements,
    }


def load_transforms(
    root: Path, directories: Iterable[str]
) -> tuple[dict[str, Any], dict[str, str], list[dict[str, Any]]]:
    transforms: dict[str, Any] = {}
    names: dict[str, str] = {}
    origins: dict[str, str] = {}
    conflicts: list[dict[str, Any]] = []
    for directory in directories:
        for path in sorted((root / directory).glob("Trans__*")):
            name = path.name[len("Trans__"):]
            value = parse_standalone_transform(path.read_bytes())
            key = name.lower()
            if key in transforms:
                previous = transforms[key]
                delta = max(
                    abs(a - b)
                    for a, b in zip(previous.local, value.local)
                )
                if delta > 1.0e-4 or previous.parent.lower() != value.parent.lower():
                    conflicts.append({
                        "name": name,
                        "kept": origins[key],
                        "other": directory,
                        "max_local_delta": delta,
                        "kept_parent": previous.parent,
                        "other_parent": value.parent,
                    })
                continue
            transforms[key] = value
            names[key] = name
            origins[key] = directory
    return transforms, names, conflicts


def canonical_name(name: str, transforms: dict[str, Any]) -> str:
    key = name.lower()
    if key not in transforms:
        return name
    for candidate in transforms:
        if candidate == key:
            break
    # Extracted object filenames preserve the desired case in their parent refs;
    # use the first matching parent/name encountered below when necessary.
    return name


def output_transform(
    name: str,
    transform: Any,
    transforms: dict[str, Any],
    package_name: str,
) -> dict[str, Any]:
    parent_key = transform.parent.lower()
    parent = transform.parent if parent_key in transforms else package_name
    world = source_world(name, transforms)
    return {
        "source_name": name,
        "parent_name": parent,
        "local": list(transform.local),
        "world": matrix12(world),
    }


def sphere(points: list[list[float]]) -> list[float]:
    if not points:
        return [0.0, 0.0, 0.0, 0.0]
    center = [sum(point[axis] for point in points) / len(points) for axis in range(3)]
    radius = max(
        math.sqrt(sum((point[axis] - center[axis]) ** 2 for axis in range(3)))
        for point in points
    )
    return center + [radius]


def collect_meshes(
    root: Path,
    recipe: dict[str, Any],
    transforms: dict[str, Any],
    material_specs: dict[str, dict[str, Any]],
    source_materials: dict[str, Path],
    deform_matrices: dict[str, list[float]] | None = None,
    deformed_transforms: dict[str, Any] | None = None,
) -> tuple[
    list[dict[str, Any]],
    dict[str, dict[str, Any]],
    dict[str, list[list[float]]],
    list[dict[str, Any]],
]:
    chunks: list[dict[str, Any]] = []
    bind_transforms: dict[str, dict[str, Any]] = {}
    observed_bind_worlds: dict[str, list[list[float]]] = defaultdict(list)
    component_selection: list[dict[str, Any]] = []
    package_name = recipe["package_name"]
    prefix = asset_prefix(recipe)
    for component in recipe["components"]:
        directory = component["directory"]
        pattern = re.compile(component["include"])
        inventory = sorted((root / directory).glob("Mesh__*.mesh"))
        parsed_inventory = {
            path: parse_mesh34(path.read_bytes()) for path in inventory
        }
        selected = [path for path in inventory if pattern.fullmatch(path.name)]
        if not selected:
            raise RuntimeError(f"{directory}: include selected no meshes")
        required_materials = set(component.get("require_full_material_coverage", []))
        available_by_material = {
            material: [
                path.name
                for path, mesh in parsed_inventory.items()
                if mesh.material == material
            ]
            for material in sorted(required_materials)
        }
        missing_materials = [
            material
            for material, paths in available_by_material.items()
            if not paths
        ]
        if missing_materials:
            raise RuntimeError(
                f"{directory}: required source materials have no meshes: "
                + ", ".join(missing_materials)
            )
        selected_set = set(selected)
        omitted = [
            path.name
            for path, mesh in parsed_inventory.items()
            if mesh.material in required_materials and path not in selected_set
        ]
        if omitted:
            raise RuntimeError(
                f"{directory}: include omitted meshes belonging to required "
                f"materials: {', '.join(omitted)}"
            )
        component_selection.append({
            "directory": directory,
            "inventory_meshes": len(inventory),
            "selected_meshes": [path.name for path in selected],
            "required_full_material_coverage": sorted(required_materials),
            "available_by_required_material": available_by_material,
        })
        for path in selected:
            mesh = parsed_inventory[path]
            source_material = mesh.material
            if source_material not in material_specs:
                raise KeyError(
                    f"no material recipe for {source_material} ({path})"
                )
            material = (
                str(recipe.get("material_prefix", "")) + source_material
            )
            mat_path = root / directory / f"Mat__{source_material}"
            if mat_path.is_file():
                source_materials.setdefault(material, mat_path)
            chunk_index = len(chunks)
            chunk_name = f"{prefix}_{chunk_index:03d}.mesh"
            vertices: list[dict[str, Any]] = []
            bone_slots = [bone.name for bone in mesh.bones]
            bind_names: list[str] = []

            if mesh.bones:
                for slot_index, bone in enumerate(mesh.bones):
                    bind_world = matrix12(invert_affine(matrix4(bone.matrix)))
                    if deformed_transforms is not None and deform_matrices is not None:
                        delta = deform_matrices.get(bone.name.lower())
                        if delta is not None:
                            # Keep the component's authored bind offset while
                            # moving its joint to the same CharDeform pose that
                            # was baked into the vertices below.
                            bind_world = matrix12(
                                matrix_multiply(matrix4(bind_world), delta)
                            )
                    observed_bind_worlds[bone.name.lower()].append(bind_world)
                    bind_name = f"{prefix}_bind_{chunk_index:03d}_{slot_index}.mesh"
                    bind_names.append(bind_name)
                    bind_transforms[bind_name] = {
                        "source_name": bone.name,
                        "parent_name": package_name,
                        "local": bind_world,
                        "world": bind_world,
                    }
                for source in mesh.vertices:
                    weights = [0.0] * len(bone_slots)
                    for influence in range(4):
                        weight = source.weights[influence]
                        index = source.bones[influence]
                        if weight <= 1.0e-8:
                            continue
                        if index >= len(bone_slots):
                            raise FormatError(
                                f"{path.name}: vertex bone {index} >= {len(bone_slots)}"
                            )
                        weights[index] += weight
                    total = sum(weights)
                    if total <= 1.0e-8:
                        weights[0] = 1.0
                    else:
                        weights = [value / total for value in weights]
                    weights += [0.0] * (4 - len(weights))
                    position = list(source.position)
                    neutral_position = list(position)
                    normal = normalize(source.normal)
                    if deform_matrices is not None:
                        deformed_position = [0.0, 0.0, 0.0]
                        deformed_normal = [0.0, 0.0, 0.0]
                        for slot_index, weight in enumerate(weights):
                            if weight <= 1.0e-8:
                                continue
                            delta = deform_matrices.get(bone_slots[slot_index].lower())
                            if delta is None:
                                continue
                            point = transform_position(position, delta)
                            direction = transform_direction(normal, delta)
                            for axis in range(3):
                                deformed_position[axis] += weight * point[axis]
                                deformed_normal[axis] += weight * direction[axis]
                        position = deformed_position
                        normal = normalize(deformed_normal)
                    vertices.append({
                        "position": position,
                        "normal": normal,
                        "deform_distance": math.sqrt(sum(
                            (position[axis] - neutral_position[axis]) ** 2
                            for axis in range(3)
                        )),
                        "color_or_weights": weights[:4],
                        "uv": list(source.uv),
                    })
            else:
                parent = mesh.transform.parent
                if parent.lower() not in transforms:
                    raise FormatError(
                        f"{path.name}: rigid parent {parent!r} is not a transform"
                    )
                bone_slots = [parent]
                parent_world = source_world(parent, transforms)
                mesh_world = matrix_multiply(matrix4(mesh.transform.local), parent_world)
                bind_world_matrix = parent_world
                if deformed_transforms is not None and deform_matrices is not None:
                    delta = deform_matrices.get(parent.lower())
                    if delta is not None:
                        bind_world_matrix = matrix_multiply(parent_world, delta)
                bind_world = matrix12(bind_world_matrix)
                observed_bind_worlds[parent.lower()].append(bind_world)
                bind_name = f"{prefix}_bind_{chunk_index:03d}_0.mesh"
                bind_names = [bind_name]
                bind_transforms[bind_name] = {
                    "source_name": parent,
                    "parent_name": package_name,
                    "local": bind_world,
                    "world": bind_world,
                }
                for source in mesh.vertices:
                    position = transform_position(source.position, mesh_world)
                    neutral_position = list(position)
                    normal = normalize(transform_direction(source.normal, mesh_world))
                    if deform_matrices is not None:
                        delta = deform_matrices.get(parent.lower())
                        if delta is not None:
                            position = transform_position(position, delta)
                            normal = normalize(transform_direction(normal, delta))
                    vertices.append({
                        "position": position,
                        "normal": normal,
                        "deform_distance": math.sqrt(sum(
                            (position[axis] - neutral_position[axis]) ** 2
                            for axis in range(3)
                        )),
                        "color_or_weights": [1.0, 0.0, 0.0, 0.0],
                        "uv": list(source.uv),
                    })

            chunks.append({
                "name": chunk_name,
                "source": f"{directory}/{path.name}",
                "material": material,
                "source_material": source_material,
                "texture": f"{prefix}_{Path(material).stem}.tex",
                "sphere": sphere([vertex["position"] for vertex in vertices]),
                "bone_slots": bone_slots,
                "bind_names": bind_names,
                "vertices": vertices,
                "faces": [list(face) for face in mesh.faces],
            })
    return chunks, bind_transforms, observed_bind_worlds, component_selection


def write_bundle(
    path: Path,
    recipe: dict[str, Any],
    chunks: list[dict[str, Any]],
    transforms: dict[str, dict[str, Any]],
    textures: dict[str, dict[str, Any]],
    render: dict[str, dict[str, Any]],
) -> None:
    out = bytearray(MAGIC)
    write_u32(out, VERSION)
    write_string(out, recipe["id"])
    write_string(out, recipe["package_name"])
    write_u32(out, len(transforms))
    for name in sorted(transforms, key=str.lower):
        value = transforms[name]
        write_string(out, name)
        write_string(out, value["source_name"])
        write_string(out, value["parent_name"])
        for number in value["local"]:
            write_f32(out, number)
        for number in value["world"]:
            write_f32(out, number)
    write_u32(out, len(textures))
    for name in sorted(textures):
        texture = textures[name]
        write_string(out, name)
        write_u32(out, texture["width"])
        write_u32(out, texture["height"])
        write_u32(out, texture["bits_per_pixel"])
        write_u8(out, texture["header_kind"])
        write_u8(out, texture["bits_per_pixel"])
        write_u32(out, texture["encoding"])
        write_u8(out, texture["mipmap_count"])
        write_u16(out, texture["width"])
        write_u16(out, texture["height"])
        write_u16(out, texture["bytes_per_line"])
        write_u16(out, texture["wii_alpha"])
        write_bytes(out, texture["data"])
    write_u32(out, len(chunks))
    for chunk in chunks:
        flags = render[chunk["material"]]
        write_string(out, chunk["name"])
        write_string(out, chunk["material"])
        write_string(out, chunk["texture"])
        write_u8(out, int(flags["alpha_cut"]))
        write_u8(out, int(flags["alpha_write"]))
        write_u32(out, int(flags["z_mode"]))
        write_u8(out, int(flags["cull"]))
        write_u32(out, int(flags["blend"]))
        for number in chunk["sphere"]:
            write_f32(out, number)
        write_u32(out, len(chunk["bone_slots"]))
        for bone, bind_name in zip(chunk["bone_slots"], chunk["bind_names"]):
            write_string(out, bone)
            write_string(out, bind_name)
        write_u32(out, len(chunk["vertices"]))
        for vertex in chunk["vertices"]:
            for key in ("position", "normal", "color_or_weights", "uv"):
                for number in vertex[key]:
                    write_f32(out, number)
        write_u32(out, len(chunk["faces"]))
        for face in chunk["faces"]:
            for index in face:
                write_u16(out, index)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--component-root", type=Path, required=True)
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--target-transform-root", type=Path)
    parser.add_argument(
        "--deform-clip",
        type=Path,
        help="extracted RB2 CharClipSamples__deform object for source-exact body shape",
    )
    parser.add_argument("--out-bundle", type=Path, required=True)
    parser.add_argument("--audit", type=Path, required=True)
    args = parser.parse_args()
    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    root = args.component_root.resolve()

    palette_root = root / "colorpalettes"
    palettes = {
        path.stem[len("ColorPalette__"):]: parse_color_palette(path)
        for path in palette_root.glob("ColorPalette__*.pal")
    }
    directories = [recipe["source_skeleton"]] + [
        component["directory"] for component in recipe["components"]
    ]
    source_transforms, transform_names, transform_conflicts = load_transforms(
        root, directories
    )
    deform_audit: dict[str, Any] | None = None
    deform_matrices = None
    deformed_transforms = None
    if args.deform_clip is not None:
        shape = recipe["body_shape"]
        deform_clip = parse_deform_clip(args.deform_clip)
        deform_channels = evaluate_deform(
            deform_clip.full, float(shape["height"]), float(shape["weight"])
        )
        deformed_transforms = apply_deform_to_transforms(
            source_transforms, deform_channels
        )
        deform_matrices = bone_deform_matrices(
            source_transforms, deformed_transforms
        )
        deform_audit = {
            "clip": str(args.deform_clip.resolve()),
            "height": float(shape["height"]),
            "weight": float(shape["weight"]),
            "sample_weights": deform_sample_weights(
                float(shape["height"]), float(shape["weight"])
            ),
            "channels": len(deform_channels),
            "mapped_transforms": sum(
                1
                for name in deform_channels
                if (
                    name.rsplit(".", 1)[0].lower() in source_transforms
                    or f"{name.rsplit('.', 1)[0].lower()}.mesh" in source_transforms
                )
            ),
            "method": "RB2 CharDeform five-point triangle bake",
        }
    source_materials: dict[str, Path] = {}
    chunks, bind_transforms, observed_bind_worlds, component_selection = collect_meshes(
        root, recipe, source_transforms, recipe["materials"], source_materials,
        deform_matrices,
        deformed_transforms,
    )

    rig_transforms = deformed_transforms or source_transforms
    used_bones = {bone.lower(): bone for chunk in chunks for bone in chunk["bone_slots"]}
    output_transforms: dict[str, dict[str, Any]] = {
        transform_names[key]: output_transform(
            transform_names[key], transform, rig_transforms,
            recipe["package_name"]
        )
        for key, transform in rig_transforms.items()
    }
    for name in recipe.get("target_required_transforms", []):
        if args.target_transform_root is None:
            raise RuntimeError(
                f"recipe requires target transform {name}; pass "
                "--target-transform-root"
            )
        path = args.target_transform_root / f"Trans__{name}"
        target = parse_standalone_transform(path.read_bytes())
        output_transforms[name] = {
            "source_name": name,
            "parent_name": target.parent,
            "local": list(target.local),
            "world": list(target.world),
        }
    missing_bones: list[str] = []
    pending = list(used_bones.values())
    while pending:
        name = pending.pop()
        key = name.lower()
        if any(existing.lower() == key for existing in output_transforms):
            continue
        transform = rig_transforms.get(key)
        if transform is None:
            missing_bones.append(name)
            output_transforms[name] = {
                "source_name": name,
                "parent_name": recipe["package_name"],
                "local": matrix12(identity_matrix()),
                "world": matrix12(identity_matrix()),
            }
            continue
        output_transforms[name] = output_transform(
            name, transform, rig_transforms, recipe["package_name"]
        )
        if transform.parent.lower() in rig_transforms:
            pending.append(transform.parent)
    output_transforms.update(bind_transforms)

    textures: dict[str, dict[str, Any]] = {}
    material_images: dict[str, Image.Image] = {}
    texture_audit: dict[str, Any] = {}
    render: dict[str, dict[str, Any]] = {}
    for source_material, spec in recipe["materials"].items():
        material = str(recipe.get("material_prefix", "")) + source_material
        texture_name = f"{asset_prefix(recipe)}_{Path(material).stem}.tex"
        texture, texture_audit[material], material_images[material] = make_texture(
            root, spec, palettes
        )
        textures[texture_name] = texture
        if material not in source_materials:
            candidates = list(root.glob(f"*/Mat__{material}"))
            if not candidates:
                raise FileNotFoundError(f"source RndMat {material}")
            source_materials[material] = candidates[0]
        render[material] = material_flags(source_materials[material])
        for key in ("blend", "z_mode", "alpha_cut", "alpha_write", "cull"):
            if key in spec:
                render[material][key] = spec[key]

    ao_config = recipe.get("ambient_occlusion", {})
    ao_audit: dict[str, Any] = {"enabled": False}
    if bool(ao_config.get("enabled", False)):
        ao_audit = bake_ambient_occlusion(
            chunks,
            material_images,
            {
                str(recipe.get("material_prefix", "")) + source_material
                for source_material, spec in recipe["materials"].items()
                if bool(spec.get("preserve_alpha", False))
            },
            samples=int(ao_config.get("samples", 8)),
            max_distance=float(ao_config.get("max_distance", 5.0)),
            bias=float(ao_config.get("bias", 0.06)),
            strength=float(ao_config.get("strength", 0.15)),
        )

    atlas_textures, atlas_audit = build_texture_atlases(
        recipe, material_images, chunks
    )
    if atlas_textures:
        textures = atlas_textures

    bind_consistency: dict[str, Any] = {}
    for bone, worlds in observed_bind_worlds.items():
        baseline = worlds[0]
        bind_consistency[bone] = {
            "observations": len(worlds),
            "max_delta": max(
                max(abs(a - b) for a, b in zip(baseline, world))
                for world in worlds
            ),
        }
    write_bundle(
        args.out_bundle, recipe, chunks, output_transforms, textures, render
    )
    audit = {
        "schema": 1,
        "source": {
            "prefab": recipe["source_prefab"],
            "display_name": recipe["display_name"],
            "skeleton": recipe["source_skeleton"],
            "body_shape": recipe["body_shape"],
            "preferred_guitar": recipe.get("preferred_guitar"),
        },
        "target": {
            "package_name": recipe["package_name"],
            "bundle": str(args.out_bundle.resolve()),
            "bundle_version": VERSION,
        },
        "counts": {
            "chunks": len(chunks),
            "vertices": sum(len(chunk["vertices"]) for chunk in chunks),
            "faces": sum(len(chunk["faces"]) for chunk in chunks),
            "animated_bones": len(used_bones),
            "transforms": len(output_transforms),
            "textures": len(textures),
        },
        "chunks": [
            {
                "name": chunk["name"],
                "source": chunk["source"],
                "material": chunk["material"],
                "source_material": chunk["source_material"],
                "vertices": len(chunk["vertices"]),
                "faces": len(chunk["faces"]),
                "bones": chunk["bone_slots"],
            }
            for chunk in chunks
        ],
        "component_selection": component_selection,
        "textures": texture_audit,
        "texture_atlas": atlas_audit,
        "ambient_occlusion": ao_audit,
        "render": render,
        "bind_consistency": bind_consistency,
        "missing_bones": sorted(set(missing_bones), key=str.lower),
        "transform_conflicts": transform_conflicts,
        "body_shape_bake": deform_audit,
        "body_shape_note": (
            "RB2 C-a-C height/weight were baked from the authored revision-14 "
            "deform clip using CharDeform's five-point triangle surface."
            if deform_audit is not None else
            "No deform clip was supplied; the neutral source component shape was retained."
        ),
    }
    if deform_audit is not None:
        distances = [
            vertex["deform_distance"]
            for chunk in chunks
            for vertex in chunk["vertices"]
        ]
        deform_audit["vertices_affected"] = sum(
            value > 1.0e-6 for value in distances
        )
        deform_audit["max_vertex_displacement"] = max(distances, default=0.0)
        deform_audit["rms_vertex_displacement"] = math.sqrt(
            sum(value * value for value in distances) / max(1, len(distances))
        )
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    args.audit.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(
        f"prefab={recipe['source_prefab']} chunks={len(chunks)} "
        f"vertices={audit['counts']['vertices']} faces={audit['counts']['faces']} "
        f"missing_bones={len(audit['missing_bones'])} "
        f"bundle={args.out_bundle}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
