#ai: exact counterexamples and inverse face mapping controls; no GPU admission.
from fractions import Fraction as Q
from itertools import product
import unittest

import numpy as np
import probe_coverage_check as coverage
import probe_lookup_coverage_check as dual
from test_probe_box_contract import lookup_direction


def exact_hit(origin, ray, center, extents):
    near, far = Q(0), None
    for p, d, c, h in zip(origin, ray, center, extents):
        if not d:
            if abs(p - c) > h:
                return False
            continue
        times = sorted(((c - h - p) / d, (c + h - p) / d))
        near = max(near, times[0])
        far = times[1] if far is None else min(far, times[1])
    return far is not None and far >= near and far > 0


class DualCoverageContract(unittest.TestCase):
    def test_inverse_face_pixel_centers(self):
        for size in (1, 7, 16):
            y, x = np.mgrid[:size, :size]
            for face in range(6):
                selected, u, v = dual.cube_texels(coverage.face_rays(face, size, size), size)
                np.testing.assert_equal(selected, face)
                np.testing.assert_equal(u, x)
                np.testing.assert_equal(v, y)
        with self.assertRaisesRegex(ValueError, "nonzero"):
            dual.cube_texels(np.zeros((1, 1, 3)), 4)

    def test_projection_and_outside_fallback(self):
        zero = np.zeros(3)
        points = np.array([[[.5, .1, -.2], [2., 0, 0]]])
        rays = np.array([[[1., 2., -3.], [-1., 1., 0.]]])
        result = dual.projected_rays(points, rays, zero, np.ones(3))
        expected = lookup_direction(tuple(map(Q, points[0, 0])),
                                    tuple(map(Q, rays[0, 0])), (Q(0),) * 3,
                                    (Q(1),) * 3, (Q(0),) * 3)
        np.testing.assert_allclose(result[0, 0], tuple(map(float, expected)))
        np.testing.assert_equal(result[0, 1], rays[0, 1])

    def witness(self, point, capture, expected):
        camera = (Q(4), Q(4), Q(6))
        ray = tuple(p - c for p, c in zip(point, camera))
        ray = (ray[0], -ray[1], ray[2])
        projected = lookup_direction(point, ray, capture,
                                     (Q(50), Q(12), Q(50)), capture)
        emitter = (Q(8), Q(3), Q(-3))
        extents = (Q(6, 5),) * 3
        result = (exact_hit(capture, ray, emitter, extents),
                  exact_hit(capture, projected, emitter, extents),
                  exact_hit(point, ray, emitter, extents))
        self.assertEqual(result, expected)
        return point, ray, projected, result

    def test_both_sky_miss_real_geometry_dynamic(self):
        self.witness((Q(168, 25), Q(1, 10), Q(-1)),
                     (Q(4), Q(-4), Q(6)), (False, False, True))

    def test_both_local_invent_geometry_dynamic(self):
        self.witness((Q(27, 5), Q(1, 10), Q(11, 5)),
                     (Q(4), Q(-4), Q(6)), (True, True, False))

    def test_both_sky_miss_real_geometry_probe(self):
        self.witness((Q(28, 5), Q(1, 10), Q(12, 25)),
                     (Q(3), Q(3, 2), Q(-3)), (False, False, True))

    #ai: homothetic objects have identical capture angular coverage at every direction.
    def test_local_distance_changes_truth_without_changing_lookup_coverage(self):
        point = (Q(1), Q(0), Q(0))
        ray = (Q(0), Q(0), Q(1))
        capture = (Q(0),) * 3
        projected = lookup_direction(point, ray, capture, (Q(10),) * 3, capture)
        near = (Q(1), Q(0), Q(2))
        far = (Q(4), Q(0), Q(8))
        near_extents = (Q(1, 5),) * 3
        far_extents = (Q(4, 5),) * 3
        for lookup in (ray, projected):
            self.assertFalse(exact_hit(capture, lookup, near, near_extents))
            self.assertFalse(exact_hit(capture, lookup, far, far_extents))
        self.assertTrue(exact_hit(point, ray, near, near_extents))
        self.assertFalse(exact_hit(point, ray, far, far_extents))
        for direction in product((Q(-2), Q(0), Q(1)), repeat=3):
            if any(direction):
                self.assertEqual(exact_hit(capture, direction, near, near_extents),
                                 exact_hit(capture, direction, far, far_extents))


if __name__ == "__main__":
    unittest.main()
