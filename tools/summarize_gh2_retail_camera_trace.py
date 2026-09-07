#!/usr/bin/env python3
"""Summarize a GH2 retail CameraManager PINE trace without sampling it down.

Every adjacent sample is evaluated.  The output groups contiguous CamShot
ownership, reports manager task-domain timing, and measures full local/world
camera-transform continuity.  It does not infer retail parity for native code.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


LOCAL_FIELDS = [
    *(f"camera_local_r{row}_{axis}" for row in range(3) for axis in "xyz"),
    *(f"camera_local_v_{axis}" for axis in "xyz"),
]
WORLD_FIELDS = [
    *(f"camera_world_r{row}_{axis}" for row in range(3) for axis in "xyz"),
    *(f"camera_world_v_{axis}" for axis in "xyz"),
]

POSITION_HOLD_TOLERANCE = 1.0e-4
BASIS_HOLD_TOLERANCE_DEGREES = 1.0e-3


def vector(sample: dict[str, object], fields: list[str]) -> list[float] | None:
    if any(sample.get(field) is None for field in fields):
        return None
    return [float(sample[field]) for field in fields]


def distance(a: list[float] | None, b: list[float] | None) -> float | None:
    if a is None or b is None or len(a) != len(b):
        return None
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def maximum_absolute_delta(
    a: list[float] | None, b: list[float] | None
) -> float | None:
    if a is None or b is None or len(a) != len(b):
        return None
    return max((abs(x - y) for x, y in zip(a, b)), default=0.0)


def row_basis(sample: dict[str, object], prefix: str) -> list[list[float]] | None:
    fields = [f"{prefix}_r{row}_{axis}" for row in range(3) for axis in "xyz"]
    flat = vector(sample, fields)
    if flat is None:
        return None
    return [flat[0:3], flat[3:6], flat[6:9]]


def basis_angle_degrees(
    a: list[list[float]] | None, b: list[list[float]] | None
) -> float | None:
    if a is None or b is None:
        return None
    # R_delta = A^T B for the row-vector Transform convention.  The retail EE
    # sine-table path leaves a slight uniform basis scale even at zero angular
    # shake, so normalize rows before treating the matrices as rotations.
    def normalized(rows: list[list[float]]) -> list[list[float]] | None:
        result: list[list[float]] = []
        for row in rows:
            length = math.sqrt(sum(value * value for value in row))
            if not math.isfinite(length) or length <= 1.0e-12:
                return None
            result.append([value / length for value in row])
        return result

    normalized_a = normalized(a)
    normalized_b = normalized(b)
    if normalized_a is None or normalized_b is None:
        return None
    trace = sum(
        normalized_a[k][i] * normalized_b[k][i]
        for i in range(3)
        for k in range(3)
    )
    cosine = max(-1.0, min(1.0, (trace - 1.0) * 0.5))
    return math.degrees(math.acos(cosine))


def position(sample: dict[str, object], prefix: str) -> list[float] | None:
    return vector(sample, [f"{prefix}_v_{axis}" for axis in "xyz"])


def task_time(sample: dict[str, object]) -> float | None:
    start = sample.get("shot_start_time")
    frame = sample.get("current_shot_frame")
    rate = sample.get("current_shot_rate")
    if start is None or frame is None or rate is None:
        return None
    frames_per_unit = 30.0 if int(rate) == 0 else 480.0
    return float(start) + float(frame) / frames_per_unit


def finite_or_none(value: float | None) -> float | None:
    return value if value is not None and math.isfinite(value) else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    payload = json.loads(args.trace.read_text(encoding="utf-8"))
    samples = payload.get("samples", [])
    if not isinstance(samples, list) or not samples:
        raise ValueError("trace contains no samples")

    segment_ranges: list[tuple[int, int]] = []
    transitions: list[dict[str, object]] = []
    adjacent: list[dict[str, object]] = []
    segment_start = 0
    pending_transition: dict[str, object] | None = None

    def identity(sample: dict[str, object]) -> tuple[object, object]:
        return sample.get("current_shot"), sample.get("current_shot_name")

    for index in range(1, len(samples)):
        previous = samples[index - 1]
        current = samples[index]
        prev_world_position = position(previous, "camera_world")
        curr_world_position = position(current, "camera_world")
        item = {
            "from_sample": index - 1,
            "to_sample": index,
            "wall_delta_seconds": float(current["seconds"])
            - float(previous["seconds"]),
            "same_shot": identity(previous) == identity(current),
            "world_position_delta": finite_or_none(
                distance(prev_world_position, curr_world_position)
            ),
            "world_basis_angle_degrees": finite_or_none(
                basis_angle_degrees(
                    row_basis(previous, "camera_world"),
                    row_basis(current, "camera_world"),
                )
            ),
            "fov_delta": (
                abs(float(current["camera_fov"]) - float(previous["camera_fov"]))
                if current.get("camera_fov") is not None
                and previous.get("camera_fov") is not None
                else None
            ),
        }
        adjacent.append(item)
        if item["same_shot"]:
            if (
                pending_transition is not None
                and identity(current)
                == (
                    pending_transition["to_pointer"],
                    pending_transition["to_shot"],
                )
            ):
                position_delta = item["world_position_delta"]
                basis_delta = item["world_basis_angle_degrees"]
                transform_held = (
                    position_delta is not None
                    and basis_delta is not None
                    and float(position_delta) <= POSITION_HOLD_TOLERANCE
                    and float(basis_delta) <= BASIS_HOLD_TOLERANCE_DEGREES
                )
                if transform_held:
                    item["phase"] = "transition_pose_hold"
                    pending_transition["observation_pose_hold_sample_count"] = (
                        int(
                            pending_transition[
                                "observation_pose_hold_sample_count"
                            ]
                        )
                        + 1
                    )
                else:
                    item["phase"] = "first_incoming_setframe"
                    outgoing = samples[int(pending_transition["from_sample"])]
                    pending_transition.update(
                        {
                            "first_incoming_setframe_sample": index,
                            "first_incoming_setframe_wall_seconds": current[
                                "seconds"
                            ],
                            "first_incoming_frame": current.get(
                                "current_shot_frame"
                            ),
                            "outgoing_to_first_incoming_world_position_delta": finite_or_none(
                                distance(
                                    position(outgoing, "camera_world"),
                                    curr_world_position,
                                )
                            ),
                            "outgoing_to_first_incoming_world_basis_angle_degrees": finite_or_none(
                                basis_angle_degrees(
                                    row_basis(outgoing, "camera_world"),
                                    row_basis(current, "camera_world"),
                                )
                            ),
                            "outgoing_to_first_incoming_fov_delta": (
                                abs(
                                    float(current["camera_fov"])
                                    - float(outgoing["camera_fov"])
                                )
                                if current.get("camera_fov") is not None
                                and outgoing.get("camera_fov") is not None
                                else None
                            ),
                        }
                    )
                    pending_transition = None
            else:
                item["phase"] = "steady_shot"
            continue
        item["phase"] = "ownership_change"
        position_delta = item["world_position_delta"]
        basis_delta = item["world_basis_angle_degrees"]
        retains_outgoing_transform = (
            position_delta is not None
            and basis_delta is not None
            and float(position_delta) <= POSITION_HOLD_TOLERANCE
            and float(basis_delta) <= BASIS_HOLD_TOLERANCE_DEGREES
        )
        transition = {
            **item,
            "wall_seconds": current["seconds"],
            "from_shot": previous.get("current_shot_name"),
            "to_shot": current.get("current_shot_name"),
            "from_pointer": previous.get("current_shot"),
            "to_pointer": current.get("current_shot"),
            "from_frame": previous.get("current_shot_frame"),
            "to_frame": current.get("current_shot_frame"),
            "from_task_time": task_time(previous),
            "to_task_time": task_time(current),
            "ownership_pose_retains_outgoing_transform": retains_outgoing_transform,
            "observation_pose_hold_sample_count": 0,
            "first_incoming_setframe_sample": None,
            "first_incoming_setframe_wall_seconds": None,
            "first_incoming_frame": None,
            "outgoing_to_first_incoming_world_position_delta": None,
            "outgoing_to_first_incoming_world_basis_angle_degrees": None,
            "outgoing_to_first_incoming_fov_delta": None,
        }
        transitions.append(transition)
        pending_transition = transition if retains_outgoing_transform else None
        segment_end = index - 1
        segment_ranges.append((segment_start, segment_end))
        segment_start = index
    segment_ranges.append((segment_start, len(samples) - 1))

    segment_rows: list[dict[str, object]] = []
    for start_index, end_index in segment_ranges:
        first = samples[start_index]
        last = samples[end_index]
        internal = [
            item
            for item in adjacent[start_index:end_index]
            if item["phase"] == "steady_shot"
        ]
        local_world_errors = [
            maximum_absolute_delta(
                vector(sample, LOCAL_FIELDS), vector(sample, WORLD_FIELDS)
            )
            for sample in samples[start_index : end_index + 1]
        ]
        local_world_errors = [
            value for value in local_world_errors if value is not None
        ]
        position_steps = [
            float(item["world_position_delta"])
            for item in internal
            if item["world_position_delta"] is not None
        ]
        angle_steps = [
            float(item["world_basis_angle_degrees"])
            for item in internal
            if item["world_basis_angle_degrees"] is not None
        ]
        segment_rows.append(
            {
                "start_sample": start_index,
                "end_sample": end_index,
                "sample_count": end_index - start_index + 1,
                "wall_start_seconds": first["seconds"],
                "wall_end_seconds": last["seconds"],
                "shot_pointer": first.get("current_shot"),
                "shot_name": first.get("current_shot_name"),
                "rate": first.get("current_shot_rate"),
                "shot_start_time": first.get("shot_start_time"),
                "frame_start": first.get("current_shot_frame"),
                "frame_end": last.get("current_shot_frame"),
                "task_time_start": task_time(first),
                "task_time_end": task_time(last),
                "world_position_start": position(first, "camera_world"),
                "world_position_end": position(last, "camera_world"),
                "fov_start": first.get("camera_fov"),
                "fov_end": last.get("camera_fov"),
                "max_local_world_transform_delta": max(
                    local_world_errors, default=None
                ),
                "max_adjacent_world_position_delta": max(
                    position_steps, default=None
                ),
                "max_adjacent_world_basis_angle_degrees": max(
                    angle_steps, default=None
                ),
            }
        )

    same_shot_steps = [item for item in adjacent if item["same_shot"]]
    steady_shot_steps = [
        item for item in adjacent if item["phase"] == "steady_shot"
    ]
    transition_phase_steps = [
        item
        for item in adjacent
        if item["phase"]
        in {
            "ownership_change",
            "transition_pose_hold",
            "first_incoming_setframe",
        }
    ]
    output = {
        "schema": 2,
        "source": str(args.trace.resolve()),
        "source_sample_count": len(samples),
        "scope": "All adjacent samples evaluated; retail observation, not native parity",
        "segments": segment_rows,
        "transitions": transitions,
        "continuity": {
            "same_shot_step_count": len(same_shot_steps),
            "steady_shot_step_count": len(steady_shot_steps),
            "transition_phase_step_count": len(transition_phase_steps),
            "max_same_shot_world_position_delta": max(
                (
                    float(item["world_position_delta"])
                    for item in steady_shot_steps
                    if item["world_position_delta"] is not None
                ),
                default=None,
            ),
            "max_same_shot_world_basis_angle_degrees": max(
                (
                    float(item["world_basis_angle_degrees"])
                    for item in steady_shot_steps
                    if item["world_basis_angle_degrees"] is not None
                ),
                default=None,
            ),
            "max_same_shot_fov_delta": max(
                (
                    float(item["fov_delta"])
                    for item in steady_shot_steps
                    if item["fov_delta"] is not None
                ),
                default=None,
            ),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "samples": len(samples),
                "segments": len(segment_rows),
                "transitions": len(transitions),
                **output["continuity"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
