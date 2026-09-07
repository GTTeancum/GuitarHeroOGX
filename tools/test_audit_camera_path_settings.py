import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from audit_camera_path_settings import main


class PathEvidenceGateTest(unittest.TestCase):
    def check(self, end=20, build="expected", extra_path=False):
        with tempfile.TemporaryDirectory(prefix="camera-evidence-gate-") as directory:
            root = Path(directory)
            native = root / "native"
            native.mkdir()
            shot = dict(name="test", path="path.tnm", frames=[
                dict(duration=0, blend=10, blend_ease=0, fov=0),
                dict(duration=10, blend=0, blend_ease=0, fov=1)])
            shots = [shot]
            if extra_path:
                shots.append({**shot, "name": "missing"})
            (root / "inventory.json").write_text(json.dumps(dict(venues=[dict(venue="v", shots=shots)])))
            rows = [f"shot_a=test shot_b=test local_frame_a=0 fov=0",
                    f"shot_a=test shot_b=test local_frame_a={end} fov={min(1, end/10)}"]
            (native / "camera.json").write_text(json.dumps(dict(
                venue="v", forced_shot="test", exe_sha256=build, smoke_pass=True,
                samples=[{}, {}], camera_transform_samples=rows)))
            argv = ["audit", str(root / "inventory.json"), str(native), "--output", str(root / "out.json"),
                    "--expect-exe-sha256", "expected", "--require-full-duration", "--require-all-paths"]
            with patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
                result = main()
            return result, json.loads((root / "out.json").read_text())

    def test_complete_matching_build_passes(self):
        result, report = self.check()
        self.assertEqual(result, 0)
        self.assertTrue(report["reports"][0]["full_duration"])

    def test_final_hold_cannot_be_omitted_even_after_zoom_finishes(self):
        result, report = self.check(end=10)
        self.assertEqual(result, 1)
        self.assertIn("complete authored duration", " ".join(report["reports"][0]["errors"]))

    def test_other_build_rejected(self):
        self.assertEqual(self.check(build="stale")[0], 1)

    def test_missing_source_path_rejected(self):
        result, report = self.check(extra_path=True)
        self.assertEqual(result, 1)
        self.assertEqual(report["missing_paths"], [["v", "missing"]])


if __name__ == "__main__":
    unittest.main()
