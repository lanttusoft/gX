#ai: distance reconstruction, inside-origin and hidden-surface controls.
import unittest
from fractions import Fraction as Q
from itertools import product

import numpy as np
import probe_distance_check as distance


class ProbeDistanceContract(unittest.TestCase):
    def test_independent_exact_face_intersections(self):
        center = (Q(1), Q(0), Q(3))
        extents = (Q(1),) * 3
        for origin in ((Q(0),) * 3, center, (Q(1), Q(0), Q(5)),
                       (Q(3), Q(0), Q(3))):
            for ray in product((Q(-2), Q(0), Q(1)), repeat=3):
                if not any(ray):
                    continue
                candidates = []
                for axis in range(3):
                    if not ray[axis]:
                        continue
                    for sign in (-1, 1):
                        t = (center[axis] + sign * extents[axis] - origin[axis]) / ray[axis]
                        point = tuple(p + t * d for p, d in zip(origin, ray))
                        if t >= 0 and all(abs(p - c) <= h for p, c, h in zip(point, center, extents)):
                            candidates.append(t)
                hit, time = distance.first_hit(np.array(origin, dtype=float),
                    np.array([ray], dtype=float), np.array(center, dtype=float),
                    np.array(extents, dtype=float))
                self.assertEqual(bool(hit[0]), bool(candidates))
                if candidates:
                    self.assertAlmostEqual(time[0], float(min(candidates)))

    def test_radial_distance_reconstructs_visible_hit(self):
        hit, point, radial = distance.reconstructed_hit(
            np.zeros(3), np.array([[1., 0., 2.]]), [1., 0., 3.], [1., 1., 1.])
        self.assertTrue(hit[0])
        np.testing.assert_allclose(point, [[1., 0., 2.]])
        self.assertAlmostEqual(radial[0], np.sqrt(5))

    def test_inside_origin_uses_exit_and_parallel_outside_misses(self):
        hit, time = distance.first_hit(np.zeros(3), np.array([[0., 0., 1.]]),
                                       [0., 0., 0.], [1., 1., 1.])
        self.assertTrue(hit[0])
        self.assertEqual(time[0], 1)
        hit, time = distance.first_hit(np.array([2., 0., 0.]), np.array([[0., 0., 1.]]),
                                       [0., 0., 0.], [1., 1., 1.])
        self.assertFalse(hit[0])
        self.assertEqual(time[0], 0)

    def test_homothetic_distance_distinguishes_identical_coverage(self):
        ray = np.array([[1., 0., 2.]]) / np.sqrt(5)
        near_hit, near = distance.first_hit(np.zeros(3), ray, [1., 0., 2.], [.2] * 3)
        far_hit, far = distance.first_hit(np.zeros(3), ray, [4., 0., 8.], [.8] * 3)
        self.assertTrue(near_hit[0] and far_hit[0])
        np.testing.assert_allclose(far, 4 * near)

    def test_hidden_back_face_is_not_reconstructed(self):
        hit, point, radial = distance.reconstructed_hit(
            np.zeros(3), np.array([[0., 0., 4.]]), [0., 0., 3.], [1.] * 3)
        self.assertTrue(hit[0])
        np.testing.assert_equal(point, [[0., 0., 2.]])
        self.assertEqual(radial[0], 2)

    def test_invalid_target(self):
        with self.assertRaisesRegex(ValueError, "differ"):
            distance.reconstructed_hit(np.zeros(3), np.zeros((1, 3)), [0., 0., 3.], [1.] * 3)
        with self.assertRaisesRegex(ValueError, "finite"):
            distance.first_hit(np.zeros(3), np.array([[np.nan, 0, 1]]), [0, 0, 3], [1] * 3)


if __name__ == "__main__":
    unittest.main()
