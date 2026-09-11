"""Native hidden-process camera smoke coverage; no desktop input or capture.

This is compatibility coverage, not a claim of frame-matched retail parity.
Detailed camera samples and native screenshots are retained; verbose scratch
logs are discarded. A fixed stock performer isolates the camera from unrelated
retargeting/attachment defects. No venue-specific camera adjustments are used.
"""
import argparse
from collections import deque
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

VENUES = "battle small1 small2 big theatre fest arena stone gh1_basement gh1_small_club gh1_small_club_multi gh1_big_club gh1_theatre gh1_fest gh1_arena".split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--ark-dir", type=Path, required=True)
    parser.add_argument("--addons-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--venues", nargs="+", default=VENUES)
    parser.add_argument("--frames", type=int, default=361)
    parser.add_argument("--no-stills", action="store_true",
                        help="Retain numerical evidence without redundant screenshot files")
    parser.add_argument("--fixed-dt", type=float, default=1.0 / 60.0,
                        help="Seconds advanced per native poll (default: source 60 Hz cadence)")
    parser.add_argument("--video-fps", type=float, default=60.0,
                        help="Encoding rate for retained native frame sequences")
    parser.add_argument("--video-stride", type=int, default=1,
                        help="Capture every Nth native frame; playback fps must be set accordingly")
    parser.add_argument("--start", type=float, default=40)
    parser.add_argument("--show-highway", action="store_true",
                        help="Keep highway/HUD visible to verify intro overlap")
    parser.add_argument("--character", default="funk1")
    parser.add_argument("--character-variant")
    parser.add_argument("--source-servo", action="store_true",
                        help="Compatibility flag: source servo is already the normal runtime")
    parser.add_argument("--require-gh1-helper", action="store_true")
    parser.add_argument("--videos", nargs="*", default=[])
    parser.add_argument("--video-start-frame", type=int, default=0)
    parser.add_argument("--video-end-frame", type=int,
                        help="Inclusive capture end; keep long diagnostics from dumping every frame")
    parser.add_argument("--summary-only", action="store_true")
    parser.add_argument("--visibility-audit", action="store_true",
                        help="Retain shared shot/visibility metadata for framing diagnosis")
    parser.add_argument("--occlusion-audit", action="store_true",
                        help="Read native authored-camera mesh picks, without freecam input")
    parser.add_argument("--animation-audit", help="Retain native venue animation diagnostics containing this substring")
    parser.add_argument("--animation-stride", type=float, default=0.5,
                        help="Seconds between animation samples;0 means every update")
    parser.add_argument("--transform-audit", help="Retain per-draw local transform rows for this target")
    parser.add_argument("--transform-mesh", help="Retain only this mesh's target rows, avoiding duplicate ancestor evidence")
    parser.add_argument("--camera-transform-audit", action="store_true",
                        help="Retain every submitted camera transform with its live CamShot attribution")
    parser.add_argument("--crowd-audit", action="store_true",
                        help="Retain 3D/sprite crowd draw counts for presentation verification")
    parser.add_argument("--performer-start-audit", action="store_true",
                        help="Retain decoded start waypoints and the applied performer start transform")
    parser.add_argument("--camera-mesh-audit", action="store_true",
                        help="Retain meshes nearest to and projected across the submitted camera")
    parser.add_argument("--skip-mesh",
                        help="Diagnostic-only mesh suppression used to identify an authored-camera obstruction")
    parser.add_argument("--material-audit",
                        help="Retain the decoded material and vertex-colour state for a matching mesh")
    parser.add_argument("--profile", action="store_true",
                        help="Retain runtime phase timings and camera hitch breakdowns")
    parser.add_argument("--compact-trace", action="store_true",
                        help="Record submitted transforms without expensive RE candidate diagnostics")
    parser.add_argument("--shot", help="Pin an authored shot for a source-frame comparison, not ordinary selection coverage")
    parser.add_argument("--path-offset", type=float, default=0)
    args = parser.parse_args()
    if args.compact_trace and args.require_gh1_helper:
        parser.error("GH1 helper telemetry requires full camera diagnostics; omit --compact-trace "
                     "and use --camera-transform-audit for per-frame transforms")
    video_end = args.frames - 1 if args.video_end_frame is None else args.video_end_frame
    if not 0 <= args.video_start_frame <= video_end < args.frames:
        parser.error("video range must be within the native run's frame range")
    if args.video_stride < 1:
        parser.error("video stride must be at least 1")
    if not args.fixed_dt > 0 or not args.video_fps > 0:
        parser.error("fixed dt and video fps must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    if args.summary_only:
        rows = []
        for venue in VENUES:
            report = args.output / venue / "camera.json"
            if not report.exists():
                continue
            data = json.loads(report.read_text(encoding="utf-8"))
            row = {k: v for k, v in data.items() if k not in ("samples", "command", "sweeps")}
            row["shots"] = [shot for shot in row["shots"] if shot != "unknown"]
            row.update(sample_count=len(data["samples"]), sweep_count=len(data["sweeps"]))
            rows.append(row)
        (args.output / "matrix.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
        print(f"{sum(row['smoke_pass'] for row in rows)}/{len(rows)} native smoke checks passed")
        return 0 if len(rows) == len(VENUES) and all(row["smoke_pass"] for row in rows) else 1
    exe_hash = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    env = dict(os.environ)
    # Diagnostic camera overrides would invalidate the shared-driver smoke.
    for key in list(env):
        if key.startswith("GHOGX_"):
            del env[key]
    env.update(GHOGX_HIDE_WINDOW="1", GHOGX_DEBUG_CAMERA="1", GHOGX_DEBUG_CAMERA_MOTION="1",
               GHOGX_ADDONS_DIR=str(args.addons_dir.resolve()))
    if args.crowd_audit:
        env["GHOGX_DEBUG_WORLDCROWD"] = "1"
    if args.compact_trace:
        del env["GHOGX_DEBUG_CAMERA"]
        env["GHOGX_LOG_CAMERA_SUBMITTED_EVERY_FRAME"] = "1"
    # Source servo is the production default. Keep --source-servo accepted
    # for old audit commands, but do not write a removed opt-in variable.
    # Legacy A/B coverage belongs to audit_performer_facing_delta.py.
    if args.profile:
        env["GHOGX_PROFILE_GAMEPLAY_DRAW"] = "1"
    if not args.show_highway:
        env.update(GHOGX_HIDE_HIGHWAY="1", GHOGX_HIDE_HUD="1")
    if args.visibility_audit or args.animation_audit or args.require_gh1_helper:
        env["GHOGX_DEBUG_VENUE_FILTERS"] = "1"
        env["GHOGX_DEBUG_VENUE_FILTER_STRIDE"] = str(args.animation_stride)
    if args.transform_audit:
        env["GHOGX_LOG_MESH_ANIM_LOCAL"] = args.transform_audit
        env["GHOGX_LOG_MESH_ANIM_STRIDE"] = "1"
    if args.camera_transform_audit:
        env["GHOGX_LOG_CAMERA_SUBMITTED_EVERY_FRAME"] = "1"
        env["GHOGX_LOG_CAMERA_SHAKE_EVERY_FRAME"] = "1"
    if args.performer_start_audit:
        env["GHOGX_DEBUG_PERFORMER_START"] = "1"
    if args.camera_mesh_audit:
        env["GHOGX_DEBUG_CAMERA_MESHES"] = "1"
    if args.skip_mesh:
        env["GHOGX_SKIP_VENUE_MESH"] = args.skip_mesh
    if args.material_audit:
        env["GHOGX_LOG_VENUE_MATERIAL"] = "1"
        env["GHOGX_LOG_VENUE_MATERIAL_MATCH"] = args.material_audit
    if args.occlusion_audit:
        env.update(GHOGX_DEBUG_VENUE_PICK_AUTHORED="1", GHOGX_VENUE_PICK_RENDER_ONLY="1",
                   GHOGX_LOG_CAMERA_MATRIX="1")
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    previous = args.output / "matrix.json"
    results = json.loads(previous.read_text(encoding="utf-8")) if previous.exists() else []
    results = [row for row in results if row["venue"] not in args.venues]
    for venue in args.venues:
        output = (args.output / venue).resolve()
        output.mkdir(exist_ok=True)
        still_frames = set() if args.no_stills else {0, args.frames // 2, args.frames - 1}
        video = venue in args.videos
        video_frames = range(args.video_start_frame, video_end + 1,
                             args.video_stride)
        capture_frames = sorted(set(video_frames) | still_frames) if video else sorted(still_frames)
        command = [str(args.exe.resolve()), "--ark-dir", str(args.ark_dir.resolve()),
                   "--auto-start", "--diagnostic-character", args.character,
                   "--diagnostic-venue", venue, "--diagnostic-autoplay",
                   "--diagnostic-song-start", str(args.start), "--mute-audio",
                   "--fixed-dt", str(args.fixed_dt), "--frames", str(args.frames)]
        if capture_frames:
            command += ["--screenshot-dir", str(output), "--screenshot-frames",
                        ",".join(str(frame) for frame in capture_frames)]
        if args.character_variant:
            command += ["--diagnostic-character-variant", args.character_variant]
        if args.shot:
            command += ["--diagnostic-camera-shot", args.shot,
                        "--diagnostic-camera-path-offset", str(args.path_offset)]
        start = time.monotonic()
        with tempfile.TemporaryDirectory(prefix="ghogx-camera-audit-") as scratch:
            log = Path(scratch) / "native.log"
            with log.open("wb") as stream:
                try:
                    run = subprocess.run(command, stdout=stream, stderr=stream, env=env,
                                         creationflags=creationflags, timeout=240)
                    timeout = False
                except subprocess.TimeoutExpired:
                    run = subprocess.CompletedProcess(command, -1)
                    timeout = True
            shots, samples, sweeps = set(), [], []
            errors = ["native run timed out"] if timeout else []
            performance, final = "", ""
            current_shot = "unknown"
            solver_samples = []
            crowd_regions = []
            crowd_draw_samples = []
            helper_samples = []
            venue_source_samples = []
            presentation_samples = []
            lifecycle_samples = []
            music_start_samples = []
            frustum_samples = []
            selection_samples = []
            waypoint_samples = []
            shot_ok_samples = []
            path_timing_samples = []
            visibility_samples = []
            intro_metadata = []
            forced_shot_events = []
            occlusion_samples = []
            animation_samples = []
            transform_samples = []
            camera_transform_samples = []
            camera_result_samples = []
            shake_samples = []
            source_servo_samples = []
            performer_start_samples = []
            camera_mesh_samples = []
            material_samples = []
            profile_samples = []
            failure_tail = deque(maxlen=20)
            with log.open(encoding="utf-8", errors="replace") as stream:
                for line in stream:
                    failure_tail.append(line.rstrip()[:800])
                    if args.profile and line.startswith("[profile-"):
                        profile_samples.append(line.strip())
                    if line.startswith(("[world] venue source assembly:",
                                        "[world] venue source key:",
                                        "[world] diagnostic venue override:",
                                        "[world] GH1 regular camera records:",
                                        "[world] GH1 INTRO camera record:",
                                        "[world] legacy GH1 venue script handlers loaded")):
                        venue_source_samples.append(line.strip())
                    if line.startswith(("[world] source Character servo ready:",
                                        "[source-servo]")):
                        source_servo_samples.append(line.strip())
                    if args.performer_start_audit and line.startswith(
                            ("[world-start-waypoints]", "[world-start-waypoint]",
                             "[world-start-ledger]", "[world-start-row]",
                             "[world-start-miss]")):
                        performer_start_samples.append(line.strip())
                    if args.camera_mesh_audit and line.startswith(
                            ("[milo_scene] camera mesh proximity",
                             "[milo_scene] camera mesh projection",
                             "[milo_scene]   near[",
                             "[milo_scene]   projected[")):
                        camera_mesh_samples.append(line.strip())
                    if args.material_audit and line.startswith(
                            "[milo_scene] venue material "):
                        material_samples.append(line.strip())
                    if args.animation_audit and args.animation_audit.lower() in line.lower() and line.startswith("[world]"):
                        animation_samples.append(line.strip())
                    if (args.transform_audit and line.startswith("[milo_scene] mesh_anim_local ")
                            and f"target={args.transform_audit} " in line
                            and (not args.transform_mesh or f"mesh={args.transform_mesh} " in line)):
                        transform_samples.append(line.strip())
                    if ((args.camera_transform_audit or args.compact_trace) and
                            line.startswith("[camera-transform]")):
                        camera_transform_samples.append(line.strip())
                        submitted_shot = re.search(r"shot_a=(.*?) shot_b=", line)
                        if submitted_shot:
                            shots.add(submitted_shot[1])
                        if args.compact_trace:
                            shot = re.search(r"shot_a=(.*?) shot_b=", line)
                            frame = re.search(r"presentation_frame=([\d.eE+-]+)", line)
                            vectors = [re.search(r" " + field + r"=\(([^)]+)\)", line)
                                       for field in ("position", "forward", "up")]
                            if not shot or not frame or not all(vectors):
                                errors.append("malformed submitted camera transform")
                            else:
                                xyz, direction, vertical = [
                                    [float(v) for v in match[1].split()]
                                    for match in vectors]
                                if not all(math.isfinite(v) for v in xyz + direction + vertical):
                                    errors.append("nonfinite submitted camera")
                                current_shot = shot[1]
                                shots.add(current_shot)
                                samples.append(dict(shot=current_shot, frame=float(frame[1]),
                                                    eye=xyz, forward=direction, up=vertical))
                    if (args.camera_transform_audit and
                            line.startswith("[camera-result]") and
                            len(camera_result_samples) < 256):
                        camera_result_samples.append(line.strip())
                    if (args.camera_transform_audit and
                            line.startswith(("[world] camera Shake:",
                                             "[camera-shake]"))):
                        shake_samples.append(line.strip())
                    if args.occlusion_audit and line.startswith("[venue-freecam] pick") and len(occlusion_samples) < 100:
                        occlusion_samples.append(line.strip())
                    if args.shot and line.startswith(("[ghogx] diagnostic camera shot:",
                            "[ghogx] diagnostic camera path offset frames:",
                            "[world] camera mNextShot path offset:",
                            "[world] diagnostic camera shot hold:",
                            "[world] diagnostic camera shot missing:")):
                        forced_shot_events.append(line.strip())
                    if args.visibility_audit:
                        if line.startswith("[world] camera crowd visibility:"):
                            visibility_samples.append(line.strip())
                        if line.startswith(("[world] intro CamShot", "[world] intro camera flags:")) or (
                            line.startswith("[world] regular CamShot") and ("category=INTRO " in line or (
                                args.shot and line.startswith("[world] regular CamShot " + args.shot + " ")))):
                            intro_metadata.append(line.strip())
                    if line.startswith("[camera-select]"):
                        selection_samples.append(line.strip())
                    if line.startswith("[world] camera current_walkspot:"):
                        waypoint_samples.append(line.strip())
                    if line.startswith("[world] camera shot_ok:"):
                        shot_ok_samples.append(line.strip())
                    if line.startswith("[camera-motion] sampled"):
                        path_timing_samples.append(line.strip())
                    if line.startswith(("[world] intro camera window:",
                                        "[world] intro camera owner:",
                                        "[gameplay] pre-song presentation complete:")):
                        presentation_samples.append(line.strip())
                    if line.startswith(("[world] intro camera current:",
                                        "[world] camera downbeat:",
                                        "[world] camera PrePoll:",
                                        "[world] camera PrePoll staged:",
                                        "[world] camera PrePoll mNextShot clear:",
                                        "[world] camera PrePoll SetPreFrame:",
                                        "[world] camera SetFrame:")):
                        lifecycle_samples.append(line.strip())
                    elif line.startswith("[camera] pipeline_scope="):
                        shot = re.search(r"gameplay_shot=a:(\S+)", line)
                        if shot:
                            current_shot = shot[1]
                    elif line.startswith("[camera-result]") and " stage=submitted " in line:
                        eye = re.search(r" position=\(([^)]+)\)", line)
                        forward = re.search(r" forward=\(([^)]+)\)", line)
                        up = re.search(r" up=\(([^)]+)\)", line)
                        frame = re.search(r" frame=([\d.-]+)", line)
                        if eye and forward and up:
                            if current_shot != "unknown":
                                shots.add(current_shot)
                            xyz = [float(v) for v in eye[1].split()[:3]]
                            direction = [float(v) for v in forward[1].split()[:3]]
                            vertical = [float(v) for v in up[1].split()[:3]]
                            if not all(math.isfinite(v) for v in xyz + direction + vertical):
                                errors.append("nonfinite submitted camera")
                            samples.append(dict(shot=current_shot, frame=float(frame[1]) if frame else None,
                                                eye=xyz, forward=direction, up=vertical))
                    elif line.startswith("[gh1-camera-helper]"):
                        helper_samples.append(line.strip())
                        if "missing " in line:
                            errors.append("GH1 helper missing live target")
                    elif line.startswith("[world] authored music_start:"):
                        music_start_samples.append(line.strip())
                    elif line.startswith("[camera-matrix] authored=") and len(frustum_samples) < 4:
                        frustum_samples.append(line.strip())
                    elif line.startswith("[crowd_region]"):
                        crowd_regions.append(line.strip())
                    if args.crowd_audit and "WorldCrowd draw:" in line:
                        crowd_draw_samples.append(line.strip())
                    elif line.startswith("[world] regular camera sweep:"):
                        sweeps.append(line.strip())
                    elif line.startswith("[camera-solver]") and " refs " in line and len(solver_samples) < 4:
                        solver_samples.append(line.strip())
                    elif line.startswith("[ghogx] loop performance:"):
                        performance = line.strip()
                    elif line.startswith("[ghogx] final gameplay summary:"):
                        final = line.strip()
            captures = sorted(output.glob("*.bmp"))
            captured_numbers = {int(path.stem.split("_")[1]) for path in captures}
            if video and not set(video_frames).issubset(captured_numbers):
                errors.append("native video capture has missing frames")
            elif video:
                sequence = Path(scratch) / "video-sequence"
                sequence.mkdir()
                for index, source_frame in enumerate(video_frames):
                    os.link(output / f"frame_{source_frame:05d}.bmp",
                            sequence / f"frame_{index:05d}.bmp")
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", str(args.video_fps),
                                "-start_number", "0",
                                "-i", str(sequence / "frame_%05d.bmp"),
                                "-frames:v", str(len(video_frames)), "-c:v", "libx264",
                                "-crf", "21", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                                str(output / "camera-motion.mp4")], check=True,
                               creationflags=creationflags)
            kept = []
            for capture in captures:
                if int(capture.stem.split("_")[1]) in still_frames:
                    converted = capture.with_suffix(".png")
                    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(capture),
                                    "-frames:v", "1", str(converted)], check=True,
                                   creationflags=creationflags)
                    kept.append(converted.name)
                capture.unlink()
            if args.require_gh1_helper and venue.startswith("gh1_") and not helper_samples:
                errors.append("required GH1 helper telemetry is absent; execution is unverified")
            if args.compact_trace and len(samples) != args.frames:
                errors.append(f"expected {args.frames} submitted camera frames, got {len(samples)}")
            if args.camera_transform_audit and len(camera_transform_samples) != args.frames:
                errors.append(f"expected {args.frames} submitted transforms, got {len(camera_transform_samples)}")
            summary = dict(venue=venue, exit_code=run.returncode, exe_sha256=exe_hash,
                           expected_frame_count=args.frames,
                           addons_dir=str(args.addons_dir.resolve()),
                           sample_space="final submitted camera, not authored key eye/at",
                           wall_seconds=round(time.monotonic() - start, 2),
                           shots=sorted(shots), samples=samples, sweeps=sweeps,
                           solver_samples=solver_samples, crowd_regions=crowd_regions, helper_samples=helper_samples,
                           crowd_draw_samples=crowd_draw_samples,
                           venue_source_samples=venue_source_samples,
                           presentation_samples=presentation_samples, highway_visible=args.show_highway,
                           lifecycle_samples=lifecycle_samples,
                           music_start_samples=music_start_samples,
                           source_servo_samples=source_servo_samples,
                           performer_start_samples=performer_start_samples,
                           camera_mesh_samples=camera_mesh_samples,
                           material_samples=material_samples,
                           profile_samples=profile_samples,
                           compact_trace=args.compact_trace,
                           transform_samples=transform_samples,
                           camera_transform_samples=camera_transform_samples,
                           camera_result_samples=camera_result_samples,
                           shake_samples=shake_samples,
                           frustum_samples=frustum_samples,
                           selection_samples=selection_samples,
                           waypoint_samples=waypoint_samples,
                           shot_ok_samples=shot_ok_samples,
                           path_timing_samples=path_timing_samples,
                           visibility_samples=visibility_samples, intro_metadata=intro_metadata,
                           forced_shot=args.shot, forced_path_offset=args.path_offset if args.shot else None,
                           diagnostic_skip_mesh=args.skip_mesh,
                           forced_shot_events=forced_shot_events,
                           occlusion_samples=occlusion_samples,
                           animation_samples=animation_samples,
                           native_failure_tail=list(failure_tail) if run.returncode else [],
                           screenshots=kept, expected_still_count=len(still_frames),
                           video="camera-motion.mp4" if video else None,
                           video_source_frames=[args.video_start_frame, video_end] if video else None,
                           video_source_stride=args.video_stride if video else None,
                           fixed_dt=args.fixed_dt,
                           video_fps=args.video_fps if video else None,
                           errors=errors, performance=performance, final=final,
                           command=command, proof_scope="native compatibility, not matched-retail parity")
            summary["smoke_pass"] = (run.returncode == 0 and bool(samples)
                                     and len(kept) == len(still_frames) and not errors)
            (output / "camera.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
            compact = {k: v for k, v in summary.items()
                       if k not in ("samples", "command", "sweeps",
                                    "animation_samples", "transform_samples",
                                    "camera_transform_samples", "camera_result_samples",
                                    "shake_samples")}
            compact["sample_count"] = len(samples)
            compact["sweep_count"] = len(sweeps)
            compact["animation_sample_count"] = len(animation_samples)
            compact["transform_sample_count"] = len(transform_samples)
            compact["camera_transform_sample_count"] = len(camera_transform_samples)
            compact["camera_result_sample_count"] = len(camera_result_samples)
            compact["shake_sample_count"] = len(shake_samples)
            results.append(compact)
            (args.output / "matrix.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            print(f"{venue}: smoke={summary['smoke_pass']} shots={len(shots)} sweeps={len(sweeps)} samples={len(samples)}", flush=True)
    return 0 if all(row["smoke_pass"] for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
