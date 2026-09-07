"""Verify GH2's saved RndCam world/projection matrix composition.

The fourth float in each saved PS2 row is alignment storage, not a matrix
component.  The audited transform is the Hmx row-vector affine form with an
implicit final column of (0, 0, 0, 1).
"""

import argparse
import json
from pathlib import Path


def matmul(a, b):
    return [[sum(a[row][k] * b[k][col] for k in range(4))
             for col in range(4)] for row in range(4)]


def inverse(matrix):
    augmented = [list(row) + [1.0 if row_index == col else 0.0
                               for col in range(4)]
                 for row_index, row in enumerate(matrix)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda row: abs(augmented[row][col]))
        if abs(augmented[pivot][col]) < 1.0e-12:
            raise ValueError("singular saved camera matrix")
        augmented[col], augmented[pivot] = augmented[pivot], augmented[col]
        divisor = augmented[col][col]
        augmented[col] = [value / divisor for value in augmented[col]]
        for row in range(4):
            if row == col:
                continue
            factor = augmented[row][col]
            augmented[row] = [augmented[row][index] -
                              factor * augmented[col][index]
                              for index in range(8)]
    return [row[4:] for row in augmented]


def hmx(rows):
    return [[float(rows[row][col]) if col < 3 else
             (1.0 if row == 3 else 0.0)
             for col in range(4)] for row in range(4)]


def first_three_delta(actual, expected):
    return max(abs(actual[row][col] - expected[row][col])
               for row in range(4) for col in range(3))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=1.0e-4)
    args = parser.parse_args()

    source = json.loads(args.input.read_text(encoding="utf-8"))
    results = []
    for camera in source.get("active_cameras", []):
        world = hmx(camera["world_rows"])
        projection = hmx(camera["projection_raw_rows"])
        saved_world_projection = hmx(camera["world_projection_raw_rows"])
        saved_inverse_world_projection = hmx(
            camera["inverse_world_projection_raw_rows"])
        derived_world_projection = matmul(inverse(world), projection)
        derived_inverse_world_projection = matmul(inverse(projection), world)
        world_projection_delta = first_three_delta(
            saved_world_projection, derived_world_projection)
        inverse_world_projection_delta = first_three_delta(
            saved_inverse_world_projection, derived_inverse_world_projection)
        results.append({
            "camera": camera["name"],
            "address": camera["address"],
            "current_shot": camera["current_shot"],
            "world_projection_formula": "inverse(world_rows) * projection_rows",
            "inverse_world_projection_formula":
                "inverse(projection_rows) * world_rows",
            "world_projection_max_abs_delta": world_projection_delta,
            "inverse_world_projection_max_abs_delta":
                inverse_world_projection_delta,
            "pass": max(world_projection_delta,
                        inverse_world_projection_delta) <= args.tolerance,
        })

    report = {
        "source": str(args.input.resolve()),
        "source_ee_sha256": source.get("ee_sha256"),
        "scope": ("Original GH2 saved EE-memory RndCam matrices; verifies "
                  "the source full-affine view/projection composition and "
                  "ignores each aligned row's non-matrix fourth float."),
        "tolerance": args.tolerance,
        "active_camera_count": len(results),
        "pass": bool(results) and all(result["pass"] for result in results),
        "cameras": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if not report["pass"]:
        raise SystemExit("saved GH2 camera projection audit failed")
    print(f"Verified {len(results)} saved GH2 active camera projection(s)")


if __name__ == "__main__":
    main()
