"""Compare compatibility facing with the production source ServoBone path.

Hidden, process-local game runs; no desktop capture or synthetic OS input.
This is a native differential check, not matched-retail parity.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def angular_span(values):
    unwrapped = []
    for value in values:
        if unwrapped:
            value = unwrapped[-1] + (value - unwrapped[-1] + math.pi) % (2 * math.pi) - math.pi
        unwrapped.append(value)
    return math.degrees(max(unwrapped) - min(unwrapped)) if unwrapped else None


def angular_steps(values):
    steps = [abs(math.degrees((b - a + math.pi) % (2 * math.pi) - math.pi))
             for a, b in zip(values, values[1:])]
    return dict(peak_degrees_per_frame=max(steps, default=0),
                accumulated_degrees=sum(steps))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--ark-dir", type=Path, required=True)
    parser.add_argument("--addons-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--character", default="funk1")
    parser.add_argument("--venue", default="big")
    parser.add_argument("--frames", type=int, default=725)
    parser.add_argument("--default-only", action="store_true",
                        help="Run only the normal production source-servo path")
    parser.add_argument("--probe-only", dest="default_only", action="store_true",
                        help=argparse.SUPPRESS)
    parser.add_argument("--video", action="store_true",
                        help="Retain native frames360..720 as a6..12s 60Hz video")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("GHOGX_")}
    env.update(GHOGX_HIDE_WINDOW="1", GHOGX_ADDONS_DIR=str(args.addons_dir.resolve()),
               GHOGX_DEBUG_PERFORMER_FACING="guitarist0",
               GHOGX_DEBUG_CAMERA="1", GHOGX_DEBUG_CAMERA_MOTION="1")
    command = [str(args.exe.resolve()), "--ark-dir", str(args.ark_dir.resolve()),
               "--auto-start", "--diagnostic-character", args.character,
               "--diagnostic-venue", args.venue, "--diagnostic-autoplay",
               "--diagnostic-song-start", "0", "--mute-audio",
               "--fixed-dt", "0.016666667", "--frames", str(args.frames)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    results = []
    for source_servo in ((True,) if args.default_only else (False, True)):
        run_env = dict(env)
        if not source_servo:
            run_env["GHOGX_DISABLE_SOURCE_SERVO_RUNTIME"] = "1"
        samples, camera, errors = [], [], []
        final, performance = "", ""
        started = time.monotonic()
        with tempfile.TemporaryDirectory(prefix="ghogx-facing-delta-") as scratch:
            log = Path(scratch) / "native.log"
            run_command = list(command)
            if args.video and source_servo:
                run_command += ["--screenshot-dir", scratch, "--screenshot-frames",
                                ",".join(map(str, range(360, 721)))]
            with log.open("wb") as stream:
                completed = subprocess.run(run_command, stdout=stream, stderr=stream,
                                           env=run_env, creationflags=creationflags,
                                           timeout=240)
            for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("[facing-audit]"):
                    scalar = dict(re.findall(r"(\w+)=([^ ()]+)", line))
                    samples.append(dict(time=float(scalar["t"]), clip=scalar["clip"],
                        clip_time=float(scalar["clip_t"]), beat=float(scalar["beat"]),
                        walk=int(scalar["walk"]), probe_applied=int(scalar["servo_probe"]),
                        servo_count=int(scalar["servo_count"]),
                        stage_yaw=float(scalar["stage_yaw"]),
                        root_yaw=float(scalar["root_yaw"]),
                        effective_yaw=float(scalar["effective_yaw"]),
                        stage=[float(v) for v in re.search(r"stage=\(([^)]+)\)", line)[1].split()],
                        delta=[float(v) for v in re.search(r"delta=\(([^)]+)\)", line)[1].split()]))
                elif line.startswith("[facing-bone]") and "name=bone_pelvis.mesh " in line:
                    if not samples:
                        errors.append("bone sample has no owning frame")
                        continue
                    for field in ("w1", "stage_w1"):
                        vector = [float(v) for v in re.search(rf"\b{field}=\(([^)]+)\)", line)[1].split()]
                        samples[-1][field + "_heading"] = math.atan2(vector[0], vector[1])
                elif (line.startswith("[camera-motion]") or
                      (line.startswith("[camera-result]") and " stage=submitted " in line)):
                    camera.append(line)
                elif "[ghogx] final gameplay summary:" in line:
                    final = line
                elif "[ghogx] loop performance:" in line:
                    performance = line
            for sample in samples:
                if "stage_w1_heading" not in sample:
                    errors.append("missing pelvis heading")
                    break
            heading = lambda key: [row[key] for row in samples if key in row]
            if args.video and source_servo:
                captures = sorted(Path(scratch).glob("frame_*.bmp"))
                if len(captures) != 361:
                    errors.append(f"expected361 native frames, received{len(captures)}")
                else:
                    subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", "60",
                        "-start_number", "360", "-i", str(Path(scratch) / "frame_%05d.bmp"),
                        "-c:v", "libx264", "-crf", "21", "-pix_fmt", "yuv420p",
                        "-movflags", "+faststart", str(args.output.with_suffix(".mp4"))],
                        check=True, creationflags=creationflags)
            result = dict(source_servo=source_servo, exit_code=completed.returncode,
                wall_seconds=round(time.monotonic() - started, 2), samples=samples,
                sample_count=len(samples), errors=errors,
                model_heading_span_degrees=angular_span(heading("w1_heading")),
                stage_heading_span_degrees=angular_span(heading("stage_w1_heading")),
                root_heading_span_degrees=angular_span(heading("stage_yaw")),
                character_root_heading_span_degrees=angular_span(heading("root_yaw")),
                effective_root_heading_span_degrees=angular_span(heading("effective_yaw")),
                angular_steps=angular_steps(heading("stage_w1_heading")),
                handoff_angular_steps=angular_steps([s["stage_w1_heading"] for s in samples
                    if s["time"] >= 10.033 and "stage_w1_heading" in s]),
                camera=camera, performance=performance, final=final)
            results.append(result)
            print(f"source_servo={source_servo} exit={completed.returncode} samples={len(samples)} "
                  f"model_span={result['model_heading_span_degrees']} "
                  f"world_span={result['stage_heading_span_degrees']}", flush=True)
    report = dict(scope="native production-vs-compatibility differential; not a retail parity pass",
        exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        command=command, addons_dir=str(args.addons_dir.resolve()),
        native_video_frames=[360, 720] if args.video else None, runs=results)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if all(row["exit_code"] == 0 and row["sample_count"] > 0 and not row["errors"]
                    for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
