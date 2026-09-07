"""Replay the source CamShot shake spring against native per-frame telemetry."""

import argparse
import json
import math
import re
import struct
from pathlib import Path


VECTOR = re.compile(r"([a-z_]+)=\(([^)]+)\)")


def nearest_f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def ee_float(value):
    nearest = nearest_f32(value)
    if not math.isfinite(value) or not math.isfinite(nearest) or nearest == 0.0:
        return nearest
    if abs(nearest) > abs(value):
        bits = struct.unpack("<I", struct.pack("<f", nearest))[0]
        bits = bits - 1 if nearest > 0.0 else bits - 1
        nearest = struct.unpack("<f", struct.pack("<I", bits))[0]
    return nearest


def ee_add(a, b):
    return ee_float(a + b)


def ee_sub(a, b):
    return ee_float(a - b)


def ee_mul(a, b):
    return ee_float(a * b)


def spring(target, output, velocity):
    diff = [ee_sub(target[axis], output[axis]) for axis in range(3)]
    squared = nearest_f32(diff[0] * diff[0])
    squared = nearest_f32(squared + nearest_f32(diff[1] * diff[1]))
    squared = nearest_f32(squared + nearest_f32(diff[2] * diff[2]))
    length = nearest_f32(math.sqrt(squared))
    correction = [0.0, 0.0, 0.0]
    if length > 0.0 and math.isfinite(length):
        scaled_length = ee_mul(length, nearest_f32(0.02))
        correction = [ee_mul(nearest_f32(value / length), scaled_length)
                      for value in diff]
    next_output = []
    next_velocity = []
    for axis in range(3):
        out = ee_add(output[axis], velocity[axis])
        vel = ee_add(velocity[axis], correction[axis])
        out = ee_add(out, correction[axis])
        vel = ee_mul(vel, nearest_f32(0.9))
        next_output.append(out)
        next_velocity.append(vel)
    return next_output, next_velocity


def parse_vector_fields(line):
    return {name: [float(value) for value in values.split()]
            for name, values in VECTOR.findall(line)}


def max_delta(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("camera_json", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=2.0e-6)
    args = parser.parse_args()

    data = json.loads(args.camera_json.read_text(encoding="utf-8"))
    lines = [line for line in data.get("shake_samples", [])
             if line.startswith("[camera-shake]")]
    previous_translation = [0.0, 0.0, 0.0]
    previous_translation_velocity = [0.0, 0.0, 0.0]
    previous_angular = [0.0, 0.0, 0.0]
    previous_angular_velocity = [0.0, 0.0, 0.0]
    maximum = 0.0
    failures = []
    for index, line in enumerate(lines):
        fields = parse_vector_fields(line)
        required = ("translation", "translation_target",
                    "translation_velocity", "euler_radians",
                    "angular_target", "angular_velocity")
        if not all(name in fields for name in required):
            failures.append({"index": index, "reason": "missing fields"})
            continue
        translation, translation_velocity = spring(
            fields["translation_target"], previous_translation,
            previous_translation_velocity)
        angular, angular_velocity = spring(
            fields["angular_target"], previous_angular,
            previous_angular_velocity)
        deltas = {
            "translation": max_delta(translation, fields["translation"]),
            "translation_velocity": max_delta(
                translation_velocity, fields["translation_velocity"]),
            "angular": max_delta(angular, fields["euler_radians"]),
            "angular_velocity": max_delta(
                angular_velocity, fields["angular_velocity"]),
        }
        maximum = max(maximum, *deltas.values())
        if max(deltas.values()) > args.tolerance:
            failures.append({"index": index, "deltas": deltas})
        previous_translation = fields["translation"]
        previous_translation_velocity = fields["translation_velocity"]
        previous_angular = fields["euler_radians"]
        previous_angular_velocity = fields["angular_velocity"]

    report = {
        "source": str(args.camera_json.resolve()),
        "source_contract": "GH2 USA CamShot::Shake 0x00263170..0x002633F0",
        "source_equation":
            "normalize(target-output) * (length * 0.02), then velocity/output integration and velocity * 0.9",
        "sample_count": len(lines),
        "impulse_count": sum(" impulse=1 " in line for line in lines),
        "tolerance": args.tolerance,
        "maximum_abs_delta": maximum,
        "failures": failures,
        "pass": bool(lines) and not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("camera shake trace audit failed")
    print(f"Verified {len(lines)} CamShot shake updates; "
          f"max delta {maximum:.9g}")


if __name__ == "__main__":
    main()
