#!/usr/bin/env python3
"""Validate an RB2 face payload against a GH2 facial control/viseme rig."""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
from pathlib import Path


SLOT_RE = re.compile(
    r"^MeshSkinSlot (?P<mesh>\S+) .* bone=(?P<bone>\S+) "
    r"weighted_vertices=(?P<count>\d+) weight_sum=(?P<weight>[-+0-9.eE]+)"
)
SAMPLE_RE = re.compile(
    r"^full\tsample=(?P<sample>\d+)\t(?P<channel>\S+)\t(?P<values>.+)$"
)
RUNTIME_EYE_RE = re.compile(
    r"^\[face\] eye (?P<eye>eye-[LR]\.mesh) .* rows=\[(?P<rows>[^]]+)\]$"
)


def run(command: list[str]) -> str:
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return result.stdout


def sample_vectors(tool: Path, clipset: Path, channel_filter: str) -> list[dict]:
    text = run([str(tool), "sample-clip", str(clipset), "visemes", "all", channel_filter])
    rows = []
    for line in text.splitlines():
        match = SAMPLE_RE.match(line)
        if not match:
            continue
        values = [float(value) for value in match.group("values").split(",")]
        rows.append({
            "sample": int(match.group("sample")),
            "channel": match.group("channel"),
            "values": values,
            "motion_magnitude": math.sqrt(sum(value * value for value in values[:-1] if len(values) == 4)
                                          if len(values) == 4 else sum(value * value for value in values)),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--character", type=Path, required=True)
    parser.add_argument("--visemes", type=Path, required=True)
    parser.add_argument("--mesh-prefix", required=True)
    parser.add_argument("--runtime-log", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    inspect = run([
        str(args.tool), "inspect-character", str(args.character),
        "--controllers", "--meshes",
    ])
    slots = []
    for line in inspect.splitlines():
        match = SLOT_RE.match(line)
        if match and match.group("mesh").startswith(args.mesh_prefix):
            slots.append({
                "mesh": match.group("mesh"),
                "bone": match.group("bone"),
                "weighted_vertices": int(match.group("count")),
                "weight_sum": float(match.group("weight")),
            })

    eye_rows = [row for row in slots if row["bone"] in {"eye-L.mesh", "eye-R.mesh"}]
    facial_names = {
        "bone_jaw.mesh",
        "bone_lowerlip-center.mesh", "bone_lowerlip-L1.mesh", "bone_lowerlip-R1.mesh",
        "bone_upperlip-center.mesh", "bone_upperlip-L1.mesh", "bone_upperlip-R1.mesh",
        "bone_lip-L-corner.mesh", "bone_lip-R-corner.mesh",
    }
    mouth_rows = [row for row in slots if row["bone"] in facial_names]
    jaw_samples = sample_vectors(args.tool, args.visemes, "jaw")
    lower_lip_samples = sample_vectors(args.tool, args.visemes, "lowerlip")

    controllers = {
        "left_eye_lookat": "CharLookAt l-eye.lookat" in inspect and "source=eye-L.mesh" in inspect,
        "right_eye_lookat": "CharLookAt r-eye.lookat" in inspect and "source=eye-R.mesh" in inspect,
        "char_eyes": "CharEyes CharEyes.eyes" in inspect,
        "facefx_lipsync": "FaceFxLipSyncServo lip.servo" in inspect,
    }
    runtime = {
        "checked": args.runtime_log is not None,
        "casey_facefx_graph_loaded": False,
        "neutral_channels_kept": 0,
        "viseme_channels_kept": 0,
        "eye_samples": {},
        "eye_max_local_rotation_delta": {},
    }
    if args.runtime_log is not None:
        runtime_text = args.runtime_log.read_text(encoding="utf-8", errors="replace")
        runtime["casey_facefx_graph_loaded"] = (
            "[facefx] graph char/guitarist.fac: 25 nodes, 11 poses" in runtime_text
        )
        neutral = re.search(r"face filtered 'neutral': kept (\d+)/(\d+) channels", runtime_text)
        visemes = re.search(r"face filtered 'visemes': kept (\d+)/(\d+) channels", runtime_text)
        runtime["neutral_channels_kept"] = int(neutral.group(1)) if neutral else 0
        runtime["viseme_channels_kept"] = int(visemes.group(1)) if visemes else 0
        runtime_eye_rows: dict[str, list[list[float]]] = {}
        for line in runtime_text.splitlines():
            match = RUNTIME_EYE_RE.match(line)
            if not match:
                continue
            values = [
                float(value)
                for row in match.group("rows").split("|")
                for value in row.split()
            ]
            runtime_eye_rows.setdefault(match.group("eye"), []).append(values)
        for eye, samples in runtime_eye_rows.items():
            first = samples[0]
            runtime["eye_samples"][eye] = len(samples)
            runtime["eye_max_local_rotation_delta"][eye] = max(
                max(abs(value - first[index]) for index, value in enumerate(sample))
                for sample in samples
            )
    checks = {
        "both_visible_eyes_bound": {row["bone"] for row in eye_rows} == {"eye-L.mesh", "eye-R.mesh"},
        "eye_vertices_weighted": sum(row["weighted_vertices"] for row in eye_rows) > 0,
        "mouth_vertices_weighted": sum(row["weighted_vertices"] for row in mouth_rows) > 0,
        "jaw_motion_authored": max((row["motion_magnitude"] for row in jaw_samples), default=0.0) > 0.001,
        "lower_lip_motion_authored": max((row["motion_magnitude"] for row in lower_lip_samples), default=0.0) > 0.01,
        "casey_controller_graph_preserved": all(controllers.values()),
    }
    if args.runtime_log is not None:
        checks.update({
            "casey_facefx_graph_loaded_at_runtime": runtime["casey_facefx_graph_loaded"],
            "all_viseme_channels_kept_at_runtime": runtime["viseme_channels_kept"] == 187,
            "both_eyes_animated_at_runtime": (
                set(runtime["eye_max_local_rotation_delta"]) == {"eye-L.mesh", "eye-R.mesh"}
                and all(
                    delta > 0.001
                    for delta in runtime["eye_max_local_rotation_delta"].values()
                )
            ),
        })
    report = {
        "schema_version": 1,
        "character": str(args.character.resolve()),
        "animation_donor": "Casey Lynch / rock1",
        "viseme_clip": "visemes",
        "checks": checks,
        "pass": all(checks.values()),
        "controllers": controllers,
        "eye_bindings": eye_rows,
        "mouth_binding_summary": {
            "slot_count": len(mouth_rows),
            "weighted_vertices": sum(row["weighted_vertices"] for row in mouth_rows),
            "weight_sum": sum(row["weight_sum"] for row in mouth_rows),
            "bones": sorted({row["bone"] for row in mouth_rows}),
        },
        "authored_motion": {
            "jaw_sample_count": len(jaw_samples),
            "jaw_max_quaternion_vector_magnitude": max(
                (row["motion_magnitude"] for row in jaw_samples), default=0.0
            ),
            "lower_lip_sample_count": len(lower_lip_samples),
            "lower_lip_max_translation": max(
                (row["motion_magnitude"] for row in lower_lip_samples), default=0.0
            ),
        },
        "runtime": runtime,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["checks"], sort_keys=True))
    print(f"pass={int(report['pass'])} out={args.out}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
