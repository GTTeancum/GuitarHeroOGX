"""Check retained native path telemetry against recovered GH2 timing equations.

This is a numerical driver check, not matched-retail visual certification.
"""
import argparse
import json
import math
import re
from pathlib import Path


def reference(frame, duration, end, ease):
    if ease == 0:
        return (0 if abs(duration) < 1e-6 else frame / duration) * end
    slope = 0 if abs(duration) < 1e-6 else 2 * ease / duration
    return (math.atan(slope * frame - ease) / (2 * math.atan(ease)) + 0.5) * end


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected", type=int, default=15)
    args = parser.parse_args()
    rows = json.loads(args.matrix.read_text(encoding="utf-8"))
    reports = []
    for row in rows:
        data = json.loads((args.matrix.parent / row["venue"] / "camera.json").read_text(encoding="utf-8"))
        errors, count, eased = [], 0, 0
        max_error = 0.0
        for line in data.get("path_timing_samples", []):
            if "mapping=GH2_duration" not in line:
                continue
            values = dict(re.findall(r"(local|authored|duration|path_end|path_ease)=([-\d.]+)", line))
            if len(values) != 5:
                errors.append("Malformed native mapping diagnostic")
                continue
            frame, actual, duration, end, ease = (float(values[k]) for k in
                ("local", "authored", "duration", "path_end", "path_ease"))
            expected = reference(frame, duration, end, ease)
            error = abs(actual - expected)
            max_error = max(max_error, error)
            count += 1
            eased += int(ease != 0)
            # Inputs/output are printed to three decimals. This is not a pixel
            # or pose tolerance, and cannot certify PS2 floating-point identity.
            precision_bound = 0.002 * (1 + abs(end / duration)) if duration else 0.002
            if not math.isfinite(expected) or error > precision_bound:
                errors.append(f"Mapped path frame differs: {line}")
        max_step, max_angle, adjacent = 0.0, 0.0, 0
        samples = data.get("samples", [])
        for previous, current in zip(samples, samples[1:]):
            if previous["shot"] != current["shot"] or current["frame"] - previous["frame"] != 1:
                continue
            adjacent += 1
            max_step = max(max_step, math.dist(previous["eye"], current["eye"]))
            a, b = previous["forward"], current["forward"]
            length = math.sqrt(sum(v*v for v in a) * sum(v*v for v in b))
            if length:
                dot = sum(x*y for x, y in zip(a, b)) / length
                max_angle = max(max_angle, math.degrees(math.acos(max(-1, min(1, dot)))))
        reports.append(dict(venue=row["venue"], native_smoke_pass=row["smoke_pass"],
            mapped_samples=count, eased_samples=eased, max_frame_error=max_error,
            adjacent_same_shot_samples=adjacent, max_eye_step=max_step,
            max_forward_angle_degrees=max_angle, errors=errors))
    passed = len(rows) == args.expected and all(r["native_smoke_pass"] and not r["errors"] for r in reports)
    args.output.write_text(json.dumps(dict(scope="Source-equation telemetry; not retail visual parity",
        expected_venues=args.expected, checked_venues=len(rows), passed=passed,
        note="Step/angle maxima are observations, not authored-motion pass/fail thresholds. Zero mapped samples means this route was not exercised.",
        venues=reports), indent=2), encoding="utf-8")
    print(f"{len(rows)}/{args.expected} venues; source timing telemetry {'PASS' if passed else 'INCOMPLETE/FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
