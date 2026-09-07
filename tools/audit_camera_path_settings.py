"""Compare submitted path-shot FOV with original-disc CamShot keyframe settings.

Uses retained native frame clocks and independent interpolation of the typed
disc inventory. This checks runtime interpolation, not full retail pose parity.
"""
import argparse
import json
import math
import re
from pathlib import Path


def expected_fov(shot, frame):
    keys = shot["frames"]
    total = sum(k["duration"] + k["blend"] for k in keys)
    if shot.get("looping") and total > 0:
        frame %= total
    if frame >= total:
        return keys[-1]["fov"]
    cursor = 0.0
    for i, key in enumerate(keys):
        span = key["duration"] + key["blend"]
        if frame <= cursor + span and span > 0:
            elapsed = max(0.0, frame - cursor)
            if elapsed < key["duration"] or not key["blend"]:
                return key["fov"]
            next_index = i + 1 if i + 1 < len(keys) else (0 if shot.get("looping") else i)
            blend = min(1.0, max(0.0, (elapsed - key["duration"]) / key["blend"]))
            ease = key["blend_ease"]
            if ease:
                blend = 0.5 + math.atan((2 * blend - 1) * ease) / (2 * math.atan(ease))
            return key["fov"] + (keys[next_index]["fov"] - key["fov"]) * blend
        cursor += span
    return keys[-1]["fov"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inventory", type=Path)
    ap.add_argument("native", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--expect-exe-sha256", help="Reject evidence from another build")
    ap.add_argument("--require-full-duration", action="store_true",
                    help="Require the clock to cover zero through the final authored hold/blend")
    ap.add_argument("--require-all-paths", action="store_true",
                    help="Require every path shot in the original-disc inventory")
    args = ap.parse_args()
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    shots = {(v["venue"], s["name"]): s for v in inventory["venues"]
             for s in v["shots"] if s.get("frames")}
    reports = []
    required_paths = {(v["venue"], s["name"]) for v in inventory["venues"]
                      for s in v["shots"] if s.get("path")}
    for file in sorted(args.native.rglob("camera.json")):
        native = json.loads(file.read_text(encoding="utf-8"))
        errors = []
        checked = 0
        maximum = 0.0
        observed = []
        expected_values = []
        clocks = []
        authored = shots.get((native["venue"], native.get("forced_shot")))
        if not authored or not authored.get("path"):
            errors.append("Forced shot is not an inventoried source path")
        if args.expect_exe_sha256 and native.get("exe_sha256", "").lower() != args.expect_exe_sha256.lower():
            errors.append("Native executable hash does not match the requested build")
        for line in native.get("camera_transform_samples", []):
            name = re.search(r"shot_a=(.*?) shot_b=", line)
            clock = re.search(r" local_frame_a=([\d.eE+-]+)", line)
            fov = re.search(r" fov=([\d.eE+-]+)", line)
            if not name or not clock or not fov:
                errors.append("Malformed submitted transform")
                continue
            source = shots.get((native["venue"], name[1]))
            if not source:
                continue
            if name[1] != native.get("forced_shot"):
                if len(errors) < 8:
                    errors.append("Submitted shot differs from the requested path shot")
                continue
            actual = float(fov[1])
            clock_value = float(clock[1])
            clocks.append(clock_value)
            expected = expected_fov(source, clock_value)
            error = abs(actual - expected)
            checked += 1
            maximum = max(maximum, error)
            observed.append(actual)
            expected_values.append(expected)
            if not math.isfinite(actual) or error > 0.00005:
                if len(errors) < 8:
                    errors.append(f"{name[1]} frame {clock[1]}: {actual} != {expected}")
        if checked != len(native.get("samples", [])):
            errors.append("Not every submitted frame had a matching path-shot oracle")
        duration = sum(k["duration"] + k["blend"] for k in authored["frames"]) if authored else None
        full_duration = bool(clocks and duration is not None and clocks[0] <= 0.0001
                             and clocks[-1] >= duration - 0.0001
                             and all(b >= a for a, b in zip(clocks, clocks[1:])))
        if args.require_full_duration and not full_duration:
            errors.append("Native samples do not cover the complete authored duration from zero")
        reports.append(dict(file=str(file), venue=native["venue"],
            shot=native.get("forced_shot"), samples=checked,
            exe_sha256=native.get("exe_sha256"), authored_duration=duration,
            observed_clock_range=[clocks[0], clocks[-1]] if clocks else [],
            full_duration=full_duration,
            max_fov_error_radians=maximum,
            observed_range=[min(observed), max(observed)] if observed else [],
            expected_range=[min(expected_values), max(expected_values)] if expected_values else [],
            passed=checked > 0 and not errors and native["smoke_pass"], errors=errors))
    missing_paths = sorted(required_paths - {(r["venue"], r["shot"]) for r in reports})
    passed = bool(reports) and all(r["passed"] for r in reports) and not (args.require_all_paths and missing_paths)
    args.output.write_text(json.dumps(dict(passed=passed, scope=__doc__, reports=reports,
                                      required_exe_sha256=args.expect_exe_sha256,
                                      require_full_duration=args.require_full_duration,
                                      require_all_paths=args.require_all_paths,
                                      missing_paths=missing_paths),
                                      indent=2) + "\n", encoding="utf-8")
    for row in reports:
        print(f"{row['venue']}/{row['shot']}: pass={row['passed']} "
              f"samples={row['samples']} max_fov_error={row['max_fov_error_radians']:.9g}")
    if args.require_all_paths and missing_paths:
        print("Missing source paths: " + ", ".join(f"{v}/{s}" for v, s in missing_paths))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
