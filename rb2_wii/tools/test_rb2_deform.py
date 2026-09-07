#!/usr/bin/env python3

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from rb2_deform import DEFORM_CONTROL_POINTS, DeformChannel, DeformSamples
from rb2_deform import deform_sample_weights, evaluate_deform


class Rb2DeformTests(unittest.TestCase):
    def test_control_points_select_their_authored_samples(self) -> None:
        for expected, (height, weight) in enumerate(DEFORM_CONTROL_POINTS):
            weights = deform_sample_weights(height, weight)
            self.assertAlmostEqual(sum(weights), 1.0)
            self.assertAlmostEqual(weights[expected], 1.0)
            self.assertEqual(sum(value > 1.0e-8 for value in weights), 1)

    def test_duke_is_half_neutral_and_quarter_each_tall_corner(self) -> None:
        self.assertEqual(
            deform_sample_weights(0.75, 0.5),
            [0.5, 0.25, 0.0, 0.25, 0.0],
        )

    def test_sample_evaluation_blends_vectors_and_normalizes_quaternions(self) -> None:
        channels = [
            DeformChannel("bone.pos", 1.0),
            DeformChannel("bone.quat", 1.0),
        ]
        values = [
            {"bone.pos": (float(i), 0.0, 0.0), "bone.quat": (0.0, 0.0, 0.0, 1.0)}
            for i in range(5)
        ]
        samples = DeformSamples(16, channels, [0, 1, 1, 2, 2, 2, 2], 2, 5, [], False, values, 14, 16)
        result = evaluate_deform(samples, 0.75, 0.5)
        self.assertAlmostEqual(result["bone.pos"][0], 1.0)
        self.assertAlmostEqual(
            math.sqrt(sum(value * value for value in result["bone.quat"])), 1.0
        )


if __name__ == "__main__":
    unittest.main()
