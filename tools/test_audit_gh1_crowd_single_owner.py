import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from audit_gh1_crowd_single_owner import main, VENUES


class OwnershipEvidenceTest(unittest.TestCase):
    def check(self, mutation=None):
        with tempfile.TemporaryDirectory(prefix="ghogx-crowd-evidence-") as tmp:
            root = Path(tmp)
            for venue in VENUES:
                name = "gh1_" + venue
                before = dict(camera_transform_samples=[str(i) for i in range(181)])
                after = dict(before, exe_sha256="expected", smoke_pass=True, exit_code=0,
                             crowd_regions=[f"[crowd_region] scene={name} owner=marker",
                                             "[crowd_region] scene=lighting owner=marker children=5",
                                             "[crowd_region] single_owner=world duplicate_draws=5"])
                if mutation and venue == VENUES[0]:
                    mutation(after)
                for side, row in (("before", before), ("after", after)):
                    folder = root / side / name
                    folder.mkdir(parents=True)
                    (folder / "camera.json").write_text(json.dumps(row))
            args = ["audit", "--before", str(root / "before"), "--after", str(root / "after"),
                    "--exe-sha256", "expected", "--output", str(root / "report.json")]
            with patch("sys.argv", args), contextlib.redirect_stdout(io.StringIO()):
                return main()

    def test_identical_sequence(self):
        self.assertEqual(self.check(), 0)

    def test_reordered_frames_fail(self):
        self.assertEqual(self.check(lambda row: row.update(
            camera_transform_samples=list(reversed(row["camera_transform_samples"])))), 1)

    def test_stale_build_fails(self):
        self.assertEqual(self.check(lambda row: row.update(exe_sha256="old")), 1)

    def test_unclaimed_secondary_crowd_fails(self):
        self.assertEqual(self.check(lambda row: row["crowd_regions"].pop()), 1)

    def test_no_secondary_crowd_needs_no_exclusion(self):
        self.assertEqual(self.check(lambda row: row.update(
            crowd_regions=row["crowd_regions"][:1])), 0)


if __name__ == "__main__":
    unittest.main()
