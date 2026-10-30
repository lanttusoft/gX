#ai: exact clip factors, jitter sign and denominator negative controls.
from fractions import Fraction as Q
import unittest

from depth_input_check import check_case, check_dump, input_weights, normalization_weights, shader_clip


class DepthInputTests(unittest.TestCase):
    def test_clip_factors_and_jitter_sign(self):
        self.assertEqual(shader_clip(Q(1, 2), Q(1, 4), Q(1, 10), Q(-1, 10), Q(3, 4)),
                         [Q(-1, 10), Q(-2, 5), Q(1, 2), 1])
        self.assertEqual(input_weights(1, 2, 3, 4, 5), [5, 8, 10, 0])
        self.assertNotEqual(input_weights(0, 0, 0, 0, 1)[2], 1)

    def test_denominator_contribution(self):
        self.assertEqual(normalization_weights([1, 2, 3, 1], [0, 0, 0, Q(1, 10)], Q(1)),
                         [Q(1, 10), Q(1, 5), Q(3, 10), 0])

    def test_unsafe_margins_rejected(self):
        for mu, reference_w in [(0, 1), (-1, 1), (1, 0)]:
            with self.assertRaises(ValueError):
                normalization_weights([0, 0, 0, reference_w], [1, 1, 1, 1], mu)

    def test_missing_unknown_and_bad_shape(self):
        for text in ["", "DEPTH_INPUT unknown 0x0p+0", "DEPTH_INPUT near 0x0p+0"]:
            with self.assertRaises(ValueError):
                check_dump(text)
        with self.assertRaises(ValueError):
            check_case(["nan"]*72)


if __name__ == "__main__":
    unittest.main()
