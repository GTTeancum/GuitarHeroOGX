import unittest

from audit_camera_poll_cadence import compare


def row(shot, frame, position=0):
    return (f"shot_a={shot} shot_b={shot} presentation_frame={frame} "
            f"local_frame_a={frame} local_frame_b={frame} blend=1 "
            f"position=({position} 0 0) forward=(0 1 0) up=(0 0 1) "
            "fov=1 custom_view=1")


def source(venue, dt, rows):
    return dict(venue=venue, fixed_dt=dt, camera_transform_samples=rows,
                expected_frame_count=len(rows), exe_sha256="hash", exit_code=0)


class CadenceAuditTests(unittest.TestCase):
    def test_matches_source_clock_not_elapsed_sample(self):
        left=source("battle",1/30,[row("intro",0),row("intro",1,1)])
        right=source("battle",1/60,[row("intro",0),row("intro",.5,.5),row("intro",1,1)])
        result=compare(left,right,"hash")
        self.assertEqual(result["matched_samples"],2)
        self.assertEqual(result["missing_clock_samples"],0)
        self.assertEqual(result["phases"][0]["peak_position"]["position_component_delta"],0)

    def test_clock_rewind_is_a_new_phase(self):
        left=source("battle",1/30,[row("intro",0),row("intro",1),row("play",0)])
        right=source("battle",1/60,[row("intro",0),row("intro",.5),row("intro",1),row("play",0)])
        result=compare(left,right,"hash")
        self.assertEqual([phase["phase"] for phase in result["phases"]],[0,1])

    def test_different_shot_is_not_pose_matched(self):
        left=source("battle",1/30,[row("a",0)])
        right=source("battle",1/60,[row("b",0)])
        result=compare(left,right,"hash")
        self.assertEqual(result["matched_samples"],0)
        self.assertEqual(result["different_shot_samples"],1)


if __name__ == "__main__":
    unittest.main()
