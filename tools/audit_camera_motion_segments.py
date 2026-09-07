"""Account for every submitted camera row, retaining all legitimate shot cuts.

Integrity checks are not retail parity checks. Motion extrema and short shot
returns identify frames to review, not grounds for inventing smoother cameras.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from audit_native_camera_handoff_trace import ROW, angle_degrees, vector


def audit(source: dict, expected_hash: str, expected_frames: int) -> dict:
    rows = []
    errors = []
    for index, line in enumerate(source.get("camera_transform_samples", [])):
        match = ROW.search(line)
        if not match:
            errors.append(f"unparsed sample {index}")
            continue
        try:
            row = dict(sample=index, shots=[match[1], match[2]],
                       presentation_frame=float(match[3]),
                       local_frames=[float(match[4]), float(match[5])],
                       blend=float(match[6]), position=vector(match[7]),
                       forward=vector(match[8]), up=vector(match[9]),
                       fov=float(match[10]))
            numbers = [row["presentation_frame"], *row["local_frames"],
                       row["blend"], *row["position"], *row["forward"],
                       *row["up"], row["fov"]]
            if not all(math.isfinite(v) for v in numbers):
                raise ValueError("nonfinite value")
            for key in ("position", "forward", "up"):
                if len(row[key]) != 3:
                    raise ValueError(f"{key} is not a 3-vector")
            if any(math.hypot(*row[key]) <= 1e-12 for key in ("forward", "up")):
                raise ValueError("zero direction")
            rows.append(row)
        except ValueError as exc:
            errors.append(f"invalid sample {index}: {exc}")
    for label, actual, expected in (
        ("parsed count", len(rows), expected_frames),
        ("requested count", source.get("expected_frame_count"), expected_frames),
        ("executable hash", source.get("exe_sha256"), expected_hash),
        ("process exit", source.get("exit_code"), 0),
    ):
        if actual != expected:
            errors.append(f"{label}: {actual!r} != {expected!r}")
    segments = []
    for row in rows:
        if not segments or row["shots"] != segments[-1]["shots"]:
            segments.append(dict(shots=row["shots"], start_sample=row["sample"],
                                 start_local_frames=row["local_frames"],
                                 start_presentation_frame=row["presentation_frame"],
                                 end_sample=row["sample"], sample_count=1))
        else:
            segments[-1]["end_sample"] = row["sample"]
            segments[-1]["sample_count"] += 1
    peaks = {}
    cuts = []
    for before, after in zip(rows, rows[1:]):
        if after["sample"] != before["sample"] + 1:
            continue  # Never disguise an unparsed gap as an observed frame step.
        values = dict(position_delta=math.dist(before["position"], after["position"]),
                      forward_degrees=angle_degrees(before["forward"], after["forward"]),
                      up_degrees=angle_degrees(before["up"], after["up"]),
                      fov_delta=abs(before["fov"] - after["fov"]))
        if before["shots"] != after["shots"]:
            cuts.append(dict(sample=after["sample"], **values))
        else:
            for key, value in values.items():
                if key not in peaks or value > peaks[key]["value"]:
                    peaks[key] = dict(value=value, sample=after["sample"])
    islands = [s["start_sample"] for s in segments[1:-1] if s["sample_count"] == 1]
    returns = [segments[i]["start_sample"] for i in range(2, len(segments))
               if segments[i]["shots"] == segments[i-2]["shots"]]
    return dict(venue=source.get("venue"), exe_sha256=source.get("exe_sha256"),
                integrity_pass=not errors, errors=errors, parsed_samples=len(rows),
                segments=segments, cuts=cuts, noncut_peak_steps=peaks,
                one_update_islands=islands, aba_shot_returns=returns,
                fixed_dt=source.get("fixed_dt"),
                time_note="sample * fixed_dt is elapsed capture time; presentation_frame changes clock domain at song start",
                scope="native trace integrity and review locations only; no retail pose or visual acceptance claim")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix-dir", required=True, type=Path)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--frames", required=True, type=int)
    parser.add_argument("--venues", required=True, nargs="+")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    reports = []
    for venue in args.venues:
        path = args.matrix_dir / venue / "camera.json"
        if not path.is_file():
            reports.append(dict(venue=venue, integrity_pass=False, errors=["missing capture"]))
            continue
        reports.append(audit(json.loads(path.read_text(encoding="utf-8")),
                             args.exe_sha256, args.frames))
    result = dict(schema=1, integrity_pass=all(r["integrity_pass"] for r in reports),
                  venues=reports)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    for report in reports:
        print(f"{report['venue']}: integrity={report['integrity_pass']} "
              f"samples={report.get('parsed_samples', 0)} cuts={len(report.get('cuts', []))}")
    return 0 if result["integrity_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
