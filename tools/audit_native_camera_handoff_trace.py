"""Audit a per-update native CamShot transform trace for shot ping-pong."""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path


ROW = re.compile(
    r"shot_a=(.*?) shot_b=(.*?) "
    r"(?:presentation_frame|time)=(\S+) "
    r"local_frame_a=(?:none/)?(\S+) "
    r"local_frame_b=(?:none/)?(\S+) blend=(\S+) "
    r"position=\(([^)]+)\) forward=\(([^)]+)\) up=\(([^)]+)\) "
    r"fov=(\S+)"
)


def vector(text: str) -> list[float]:
    return [float(value) for value in text.split()]


def angle_degrees(a: list[float], b: list[float]) -> float:
    na = math.sqrt(sum(value * value for value in a))
    nb = math.sqrt(sum(value * value for value in b))
    cosine = sum(x * y for x, y in zip(a, b)) / (na * nb)
    return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera-json", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--post-handoff-samples", type=int, default=120)
    args = parser.parse_args()

    source = json.loads(args.camera_json.read_text(encoding="utf-8"))
    rows: list[dict[str, object]] = []
    for line in source.get("camera_transform_samples", []):
        match = ROW.search(line)
        if not match:
            raise ValueError(f"unparsed camera transform row: {line}")
        rows.append({
            "shot_a": match.group(1),
            "shot_b": match.group(2),
            "presentation_frame": float(match.group(3)),
            "local_frame_a": float(match.group(4)),
            "local_frame_b": float(match.group(5)),
            "blend": float(match.group(6)),
            "position": vector(match.group(7)),
            "forward": vector(match.group(8)),
            "up": vector(match.group(9)),
            "fov": float(match.group(10)),
        })

    segments: list[dict[str, object]] = []
    start = 0
    for index in range(1, len(rows) + 1):
        if (index == len(rows) or
                (rows[index]["shot_a"], rows[index]["shot_b"]) !=
                (rows[start]["shot_a"], rows[start]["shot_b"])):
            segments.append({
                "start_sample": start,
                "end_sample": index - 1,
                "sample_count": index - start,
                "shot_a": rows[start]["shot_a"],
                "shot_b": rows[start]["shot_b"],
                "presentation_frame_start": rows[start]["presentation_frame"],
                "presentation_frame_end": rows[index - 1]["presentation_frame"],
                "local_frame_start": rows[start]["local_frame_a"],
                "local_frame_end": rows[index - 1]["local_frame_a"],
            })
            start = index

    checks: list[dict[str, object]] = []

    def check(name: str, passed: bool, actual: object, expected: object) -> None:
        checks.append({"name": name, "passed": bool(passed), "actual": actual,
                       "expected": expected})

    expected_count = int(source.get("command", ["--frames", "0"])[
        source.get("command", []).index("--frames") + 1
    ]) if "--frames" in source.get("command", []) else 0
    check("one transform row per native update", len(rows) == expected_count,
          len(rows), expected_count)
    check("exactly one intro-to-regular transform segment change",
          len(segments) == 2, len(segments), 2)

    transition_index = segments[1]["start_sample"] if len(segments) >= 2 else None
    transition: dict[str, object] = {}
    if transition_index is not None:
        before = rows[int(transition_index) - 1]
        after = rows[int(transition_index)]
        intro = str(before["shot_a"])
        regular = str(after["shot_a"])
        post_end = min(len(rows), int(transition_index) + args.post_handoff_samples)
        post = rows[int(transition_index):post_end]
        video_range = source.get("video_source_frames") or [0, -1]
        video_zero_based = int(transition_index) - int(video_range[0])
        transition = {
            "sample": transition_index,
            "video_frame_zero_based": video_zero_based,
            "video_image_one_based": video_zero_based + 1,
            "from_shot": intro,
            "to_shot": regular,
            "position_delta": math.dist(before["position"], after["position"]),
            "forward_angle_degrees": angle_degrees(before["forward"], after["forward"]),
            "up_angle_degrees": angle_degrees(before["up"], after["up"]),
            "fov_delta": abs(float(before["fov"]) - float(after["fov"])),
            "incoming_local_frame": after["local_frame_a"],
        }
        check("outgoing segment is an intro CamShot",
              intro.lower().startswith("intro"), intro, "Intro*")
        check("incoming segment is a different regular CamShot",
              regular != intro and not regular.lower().startswith("intro"),
              regular, "non-Intro and different")
        check("incoming CamShot begins at local frame zero",
              math.isclose(float(after["local_frame_a"]), 0.0, abs_tol=1e-6),
              after["local_frame_a"], 0.0)
        check("no return to outgoing intro after handoff",
              all(row["shot_a"] != intro for row in post),
              sum(row["shot_a"] == intro for row in post), 0)
        check("no one-update third-shot intrusion after handoff",
              all(row["shot_a"] == regular and row["shot_b"] == regular
                  for row in post),
              sorted({(row["shot_a"], row["shot_b"]) for row in post}),
              [[regular, regular]])
        check("handoff is inside retained video window",
              0 <= video_zero_based <= int(video_range[1]) - int(video_range[0]),
              video_zero_based,
              f"0..{int(video_range[1]) - int(video_range[0])}")

    check("all submitted values are finite",
          all(math.isfinite(value) for row in rows for key in
              ("presentation_frame", "local_frame_a", "local_frame_b", "blend", "fov")
              for value in ([float(row[key])] if not isinstance(row[key], list) else row[key]))
          and all(math.isfinite(value) for row in rows for key in
                  ("position", "forward", "up") for value in row[key]),
          "finite" if rows else "no rows", "finite")

    passed = all(bool(item["passed"]) for item in checks)
    report = {
        "schema": 1,
        "passed": passed,
        "scope": "native shot-attribution/ping-pong audit; not matched-pose retail parity",
        "camera_json": str(args.camera_json.resolve()),
        "sample_count": len(rows),
        "segments": segments,
        "transition": transition,
        "checks": checks,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{sum(bool(item['passed']) for item in checks)}/{len(checks)} checks passed")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
