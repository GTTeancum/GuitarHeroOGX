#!/usr/bin/env python3
"""Deterministic, nondirectional ambient-occlusion bake for RB2 outfit meshes."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

from PIL import Image


Vec3 = tuple[float, float, float]


def _add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _mul(a: Vec3, value: float) -> Vec3:
    return (a[0] * value, a[1] * value, a[2] * value)


def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _unit(a: Vec3) -> Vec3:
    length = math.sqrt(max(0.0, _dot(a, a)))
    if length <= 1.0e-10:
        return (0.0, 0.0, 1.0)
    return _mul(a, 1.0 / length)


@dataclass
class Triangle:
    a: Vec3
    b: Vec3
    c: Vec3
    low: Vec3
    high: Vec3
    center: Vec3


@dataclass
class BvhNode:
    low: Vec3
    high: Vec3
    triangles: list[int]
    left: "BvhNode | None" = None
    right: "BvhNode | None" = None


def _bounds(triangles: list[Triangle], indices: list[int]) -> tuple[Vec3, Vec3]:
    return (
        tuple(min(triangles[index].low[axis] for index in indices) for axis in range(3)),
        tuple(max(triangles[index].high[axis] for index in indices) for axis in range(3)),
    )  # type: ignore[return-value]


def _build_bvh(triangles: list[Triangle], indices: list[int]) -> BvhNode:
    low, high = _bounds(triangles, indices)
    if len(indices) <= 10:
        return BvhNode(low, high, indices)
    extents = [high[axis] - low[axis] for axis in range(3)]
    axis = max(range(3), key=lambda value: extents[value])
    indices.sort(key=lambda index: triangles[index].center[axis])
    middle = len(indices) // 2
    return BvhNode(
        low,
        high,
        [],
        _build_bvh(triangles, indices[:middle]),
        _build_bvh(triangles, indices[middle:]),
    )


def _ray_box(origin: Vec3, direction: Vec3, low: Vec3, high: Vec3, limit: float) -> bool:
    near, far = 0.0, limit
    for axis in range(3):
        if abs(direction[axis]) < 1.0e-10:
            if origin[axis] < low[axis] or origin[axis] > high[axis]:
                return False
            continue
        inv = 1.0 / direction[axis]
        a = (low[axis] - origin[axis]) * inv
        b = (high[axis] - origin[axis]) * inv
        if a > b:
            a, b = b, a
        near, far = max(near, a), min(far, b)
        if near > far:
            return False
    return True


def _ray_triangle(origin: Vec3, direction: Vec3, tri: Triangle, limit: float) -> bool:
    edge1 = _sub(tri.b, tri.a)
    edge2 = _sub(tri.c, tri.a)
    p = _cross(direction, edge2)
    determinant = _dot(edge1, p)
    if abs(determinant) < 1.0e-9:
        return False
    inverse = 1.0 / determinant
    t = _sub(origin, tri.a)
    u = _dot(t, p) * inverse
    if u < 0.0 or u > 1.0:
        return False
    q = _cross(t, edge1)
    v = _dot(direction, q) * inverse
    if v < 0.0 or u + v > 1.0:
        return False
    distance = _dot(edge2, q) * inverse
    return 0.0 < distance <= limit


def _occluded(
    node: BvhNode,
    triangles: list[Triangle],
    origin: Vec3,
    direction: Vec3,
    limit: float,
) -> bool:
    if not _ray_box(origin, direction, node.low, node.high, limit):
        return False
    if node.triangles:
        return any(
            _ray_triangle(origin, direction, triangles[index], limit)
            for index in node.triangles
        )
    return bool(
        (node.left and _occluded(node.left, triangles, origin, direction, limit))
        or (node.right and _occluded(node.right, triangles, origin, direction, limit))
    )


def _hemisphere(normal: Vec3, count: int) -> list[Vec3]:
    normal = _unit(normal)
    helper = (0.0, 0.0, 1.0) if abs(normal[2]) < 0.9 else (0.0, 1.0, 0.0)
    tangent = _unit(_cross(helper, normal))
    bitangent = _cross(normal, tangent)
    result = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for index in range(count):
        # Cosine-weighted deterministic hemisphere samples.
        radius = math.sqrt((index + 0.5) / count)
        angle = index * golden
        x, y = radius * math.cos(angle), radius * math.sin(angle)
        z = math.sqrt(max(0.0, 1.0 - radius * radius))
        result.append(_unit(_add(_add(_mul(tangent, x), _mul(bitangent, y)), _mul(normal, z))))
    return result


def vertex_occlusion(
    chunks: list[dict[str, Any]], samples: int, max_distance: float, bias: float
) -> tuple[list[list[float]], dict[str, Any]]:
    triangles: list[Triangle] = []
    for chunk in chunks:
        positions = [tuple(row["position"]) for row in chunk["vertices"]]
        for face in chunk["faces"]:
            a, b, c = (positions[index] for index in face)
            if math.sqrt(_dot(_cross(_sub(b, a), _sub(c, a)), _cross(_sub(b, a), _sub(c, a)))) < 1.0e-8:
                continue
            low = tuple(min(a[axis], b[axis], c[axis]) for axis in range(3))
            high = tuple(max(a[axis], b[axis], c[axis]) for axis in range(3))
            center = tuple((a[axis] + b[axis] + c[axis]) / 3.0 for axis in range(3))
            triangles.append(Triangle(a, b, c, low, high, center))  # type: ignore[arg-type]
    if not triangles:
        return [[0.0] * len(chunk["vertices"]) for chunk in chunks], {"triangles": 0}
    bvh = _build_bvh(triangles, list(range(len(triangles))))
    rows: list[list[float]] = []
    for chunk in chunks:
        values = []
        for vertex in chunk["vertices"]:
            point = tuple(vertex["position"])
            normal = _unit(tuple(vertex["normal"]))
            origin = _add(point, _mul(normal, bias))
            hits = sum(
                _occluded(bvh, triangles, origin, ray, max_distance)
                for ray in _hemisphere(normal, samples)
            )
            values.append(hits / samples)
        rows.append(values)
    flat = [value for row in rows for value in row]
    return rows, {
        "triangles": len(triangles),
        "vertices": len(flat),
        "mean_raw_occlusion": sum(flat) / max(1, len(flat)),
        "max_raw_occlusion": max(flat, default=0.0),
    }


def _rasterize_triangle(
    factors: list[float], coverage: bytearray, size: tuple[int, int],
    uv: list[list[float]], values: list[float]
) -> None:
    width, height = size
    points = [(row[0] * (width - 1), row[1] * (height - 1)) for row in uv]
    denominator = (
        (points[1][1] - points[2][1]) * (points[0][0] - points[2][0])
        + (points[2][0] - points[1][0]) * (points[0][1] - points[2][1])
    )
    if abs(denominator) < 1.0e-8:
        return
    x0 = max(0, int(math.floor(min(row[0] for row in points))))
    x1 = min(width - 1, int(math.ceil(max(row[0] for row in points))))
    y0 = max(0, int(math.floor(min(row[1] for row in points))))
    y1 = min(height - 1, int(math.ceil(max(row[1] for row in points))))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px, py = x + 0.5, y + 0.5
            a = ((points[1][1] - points[2][1]) * (px - points[2][0]) + (points[2][0] - points[1][0]) * (py - points[2][1])) / denominator
            b = ((points[2][1] - points[0][1]) * (px - points[2][0]) + (points[0][0] - points[2][0]) * (py - points[2][1])) / denominator
            c = 1.0 - a - b
            if min(a, b, c) < -1.0e-5:
                continue
            index = y * width + x
            value = a * values[0] + b * values[1] + c * values[2]
            factors[index] = min(factors[index], value) if coverage[index] else value
            coverage[index] = 1


def bake_ambient_occlusion(
    chunks: list[dict[str, Any]],
    images: dict[str, Image.Image],
    excluded_materials: set[str],
    *,
    samples: int = 8,
    max_distance: float = 5.0,
    bias: float = 0.06,
    strength: float = 0.15,
) -> dict[str, Any]:
    values, audit = vertex_occlusion(chunks, samples, max_distance, bias)
    by_material: dict[str, list[tuple[dict[str, Any], list[float]]]] = {}
    for chunk, row in zip(chunks, values):
        by_material.setdefault(chunk["material"], []).append((chunk, row))
    material_audit: dict[str, Any] = {}
    for material, image in images.items():
        if material in excluded_materials:
            material_audit[material] = {"excluded": "transparent material"}
            continue
        width, height = image.size
        factors = [0.0] * (width * height)
        coverage = bytearray(width * height)
        for chunk, row in by_material.get(material, []):
            for face in chunk["faces"]:
                _rasterize_triangle(
                    factors,
                    coverage,
                    image.size,
                    [chunk["vertices"][index]["uv"] for index in face],
                    [row[index] for index in face],
                )
        pixels = bytearray(image.convert("RGBA").tobytes())
        covered = 0
        darkest = 1.0
        for index, present in enumerate(coverage):
            if not present:
                continue
            factor = max(1.0 - strength, 1.0 - strength * factors[index])
            darkest = min(darkest, factor)
            covered += 1
            offset = index * 4
            for channel in range(3):
                pixels[offset + channel] = round(pixels[offset + channel] * factor)
        images[material] = Image.frombytes("RGBA", image.size, bytes(pixels))
        material_audit[material] = {
            "covered_texels": covered,
            "darkest_multiplier": darkest,
        }
    audit.update({
        "enabled": True,
        "method": "deterministic cosine-hemisphere ray occlusion",
        "samples": samples,
        "max_distance": max_distance,
        "bias": bias,
        "strength": strength,
        "materials": material_audit,
    })
    return audit
