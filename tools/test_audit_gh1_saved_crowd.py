import copy
import unittest

from audit_gh1_saved_crowd import compare


class CrowdOracleTest(unittest.TestCase):
    def setUp(self):
        a = dict(mesh="Crowd01.mm", position=[1., 2., 3.])
        b = dict(mesh="Crowd02.mm", position=[4., 5., 6.])
        self.source = dict(instances=[a, b], regions=[dict(
            index=0, members=[a, b], center=[0., 0., 0.], radius=4., plane_z=-2.)])
        self.native = copy.deepcopy(self.source)

    def test_identical_passes(self):
        self.assertTrue(compare(self.source, self.native)["passed"])

    def test_order_is_not_just_membership(self):
        self.native["regions"][0]["members"].reverse()
        result = compare(self.source, self.native)
        self.assertTrue(result["regions"][0]["membership_equal"])
        self.assertFalse(result["passed"])

    def test_missing_member_fails(self):
        self.native["regions"][0]["members"].pop()
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_duplicate_cannot_replace_member(self):
        self.native["instances"][1] = self.native["instances"][0]
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_missing_region_fails(self):
        self.native["regions"].clear()
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_wrong_ground_plane_fails(self):
        self.native["regions"][0]["plane_z"] *= -1
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_wrong_index_fails(self):
        self.native["regions"][0]["index"] = 1
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_nonfinite_bounds_fail(self):
        self.native["regions"][0]["radius"] = float("nan")
        self.assertFalse(compare(self.source, self.native)["passed"])

    def test_empty_evidence_fails(self):
        empty = dict(instances=[], regions=[])
        self.assertFalse(compare(empty, empty)["passed"])


if __name__ == "__main__":
    unittest.main()
