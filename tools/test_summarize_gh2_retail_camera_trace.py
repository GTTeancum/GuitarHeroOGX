#!/usr/bin/env python3
"""Regression tests for retail camera-transition phase classification."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("summarize_gh2_retail_camera_trace.py")


def sample(index: int, shot: str, x: float, fov: float) -> dict[str, object]:
    row_values = {
        "r0_x": 1.0,
        "r0_y": 0.0,
        "r0_z": 0.0,
        "r1_x": 0.0,
        "r1_y": 1.0,
        "r1_z": 0.0,
        "r2_x": 0.0,
        "r2_y": 0.0,
        "r2_z": 1.0,
    }
    result: dict[str, object] = {
        "seconds": index / 60.0,
        "current_shot": 0x1000 if shot == "outgoing" else 0x2000,
        "current_shot_name": shot,
        "current_shot_frame": float(index),
        "current_shot_rate": 0,
        "shot_start_time": 0.0,
        "camera_fov": fov,
    }
    for prefix in ("camera_local", "camera_world"):
        for field, value in row_values.items():
            result[f"{prefix}_{field}"] = value
        result[f"{prefix}_v_x"] = x
        result[f"{prefix}_v_y"] = 0.0
        result[f"{prefix}_v_z"] = 0.0
    return result


def summarize(samples: list[dict[str, object]]) -> dict[str, object]:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        trace = root / "trace.json"
        output = root / "summary.json"
        trace.write_text(json.dumps({"samples": samples}), encoding="utf-8")
        subprocess.run(
            [sys.executable, str(SCRIPT), str(trace), "--output", str(output)],
            check=True,
            capture_output=True,
            text=True,
        )
        return json.loads(output.read_text(encoding="utf-8"))


class CameraTransitionSummaryTest(unittest.TestCase):
    def test_two_phase_handoff_excludes_first_setframe_from_continuity(self) -> None:
        summary = summarize(
            [
                sample(0, "outgoing", 0.0, 0.7),
                sample(1, "incoming", 0.0, 1.0),
                sample(2, "incoming", 10.0, 1.0),
                sample(3, "incoming", 11.0, 1.0),
            ]
        )
        transition = summary["transitions"][0]
        self.assertTrue(transition["ownership_pose_retains_outgoing_transform"])
        self.assertEqual(transition["first_incoming_setframe_sample"], 2)
        self.assertEqual(
            transition["outgoing_to_first_incoming_world_position_delta"], 10.0
        )
        self.assertEqual(summary["continuity"]["steady_shot_step_count"], 1)
        self.assertEqual(
            summary["continuity"]["transition_phase_step_count"], 2
        )
        self.assertEqual(
            summary["continuity"]["max_same_shot_world_position_delta"], 1.0
        )
        self.assertEqual(
            summary["segments"][1]["max_adjacent_world_position_delta"], 1.0
        )

    def test_atomic_handoff_needs_no_settlement_phase(self) -> None:
        summary = summarize(
            [
                sample(0, "outgoing", 0.0, 0.7),
                sample(1, "incoming", 10.0, 1.0),
                sample(2, "incoming", 11.0, 1.0),
            ]
        )
        transition = summary["transitions"][0]
        self.assertFalse(transition["ownership_pose_retains_outgoing_transform"])
        self.assertIsNone(transition["first_incoming_setframe_sample"])
        self.assertEqual(summary["continuity"]["steady_shot_step_count"], 1)
        self.assertEqual(
            summary["continuity"]["transition_phase_step_count"], 1
        )
        self.assertEqual(
            summary["continuity"]["max_same_shot_world_position_delta"], 1.0
        )


if __name__ == "__main__":
    unittest.main()
