import unittest

from audit_camera_motion_segments import audit


def sample(shot, frame=0, forward="0 1 0", fov="1e+0"):
    return (f"shot_a={shot} shot_b={shot} presentation_frame={frame} "
            f"local_frame_a=0 local_frame_b=0 blend=1 position=(0 0 0) "
            f"forward=({forward}) up=(0 0 1) fov={fov} custom_view=1")


def run(rows, **updates):
    source = dict(camera_transform_samples=rows, expected_frame_count=len(rows),
                  exe_sha256="test", exit_code=0)
    source.update(updates)
    return audit(source, "test", len(rows))


class MotionAuditTests(unittest.TestCase):
    def test_authored_names_with_spaces_and_multiple_cuts(self):
        r = run([sample(s) for s in ["Intro01", "Intro01", "zoom out", "zoom out", "third", "third"]])
        self.assertTrue(r["integrity_pass"])
        self.assertEqual([s["shots"][0] for s in r["segments"]], ["Intro01", "zoom out", "third"])
        self.assertEqual([c["sample"] for c in r["cuts"]], [2, 4])
        self.assertFalse(r["one_update_islands"])

    def test_island_and_return_reported_not_assumed_wrong(self):
        r = run([sample(s) for s in ["a", "a", "b", "a", "a"]])
        self.assertTrue(r["integrity_pass"])
        self.assertEqual(r["one_update_islands"], [2])
        self.assertEqual(r["aba_shot_returns"], [3])

    def test_parse_failure_cannot_pass_or_bridge_gap(self):
        r = run([sample("a"), "invalid", sample("a", forward="1 0 0")])
        self.assertFalse(r["integrity_pass"])
        self.assertFalse(r["noncut_peak_steps"])

    def test_nonfinite_and_zero_directions_rejected(self):
        for row in [sample("a", fov="nan"), sample("a", forward="0 0 0")]:
            self.assertFalse(run([row])["integrity_pass"])

    def test_old_hash_and_short_capture_rejected(self):
        self.assertFalse(run([sample("a")], exe_sha256="old")["integrity_pass"])
        self.assertFalse(run([sample("a")], expected_frame_count=1051)["integrity_pass"])

    def test_spike_location_and_clock_domain_reset(self):
        r = run([sample("a", 300), sample("a", 0, forward="1 0 0")])
        self.assertTrue(r["integrity_pass"])
        self.assertEqual(r["noncut_peak_steps"]["forward_degrees"], dict(value=90., sample=1))


if __name__ == "__main__":
    unittest.main()
