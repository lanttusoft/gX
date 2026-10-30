#ai: sampled silhouette controls and an unseen slit counterexample.
import unittest
from fractions import Fraction as Q

import numpy as np
import probe_coverage_check as coverage
from probe_distance_check import first_hit
from probe_distance_search import cube_sampler, search
from probe_footprint_check import footprint_guard
from test_probe_box_contract import face_oracle


class FootprintContract(unittest.TestCase):
    def test_uniform_neighborhood_remains_only_candidate(self):
        status, span = footprint_guard(np.full((6, 8, 8), 2.), np.array([[.1, .1, 1.]]))
        self.assertEqual(status[0], "interior_candidate")
        self.assertEqual(span[0], 0)

    def test_sky_neighbor_rejects_candidate(self):
        depth = np.full((6, 8, 8), 2.)
        depth[4, 3, 3] = 0
        status, _ = footprint_guard(depth, np.array([[.1, .1, 1.]]))
        self.assertEqual(status[0], "uncertain_silhouette")

    def test_face_boundary_stays_uncertain(self):
        status, _ = footprint_guard(np.full((6, 8, 8), 2.), np.array([[.99, .1, 1.]]))
        self.assertEqual(status[0], "uncertain_face_seam")

    def test_observed_depth_span_is_not_filtered_away(self):
        depth = np.full((6, 8, 8), 2.)
        depth[4, 3, 3] = 3
        status, span = footprint_guard(depth, np.array([[.1, .1, 1.]]))
        self.assertEqual(status[0], "interior_candidate")
        self.assertEqual(span[0], 1)

    #ai: compare unnormalized rational rays so equal first times imply identical radial distances.
    def test_slit_depth_identity_exact_rational_all_faces(self):
        zero = (Q(0),) * 3
        solid_center = (Q(0), Q(0), Q(41, 20))
        solid_extents = (Q(3), Q(3), Q(1, 20))
        slit_extents = (Q(95, 64), Q(3), Q(1, 20))

        def time_or_none(ray, center, extents):
            try:
                return face_oracle(zero, ray, center, extents)[0]
            except ValueError:
                return None

        for face in range(6):
            for raw in coverage.face_rays(face, 4, 4).reshape(-1, 3):
                ray = tuple(Q(float(v)) for v in raw)
                solid = time_or_none(ray, solid_center, solid_extents)
                times = [time_or_none(ray, (sign * Q(97, 64), Q(0), Q(41, 20)), slit_extents)
                         for sign in (-1, 1)]
                hits = [time for time in times if time is not None]
                self.assertEqual(solid, min(hits) if hits else None)

    #ai: all point-sampled depths, not just the guard stencil, are identical with/without a slit.
    def test_full_neighborhood_cannot_certify_subtexel_silhouette(self):
        size = 4
        directions = np.stack([coverage.face_rays(i, size, size) for i in range(6)])
        directions /= np.linalg.norm(directions, axis=-1)[..., None]
        _, solid_depth = first_hit(np.zeros(3), directions, [0, 0, 41 / 20], [3, 3, 1 / 20])
        depths = []
        for sign in (-1, 1):
            hit, radial = first_hit(np.zeros(3), directions,
                                     [sign * 97 / 64, 0, 41 / 20], [95 / 64, 3, 1 / 20])
            depths.append(np.where(hit, radial, np.inf))
        slit_depth = np.minimum(*depths)
        slit_depth[~np.isfinite(slit_depth)] = 0
        np.testing.assert_allclose(slit_depth, solid_depth, rtol=0, atol=1e-14)
        point = np.array([[1 / 64, 0., 1.]])
        ray = np.array([[0., 0., 1.]])
        solid_hit, _ = first_hit(point, ray, [0, 0, 41 / 20], [3, 3, 1 / 20])
        self.assertTrue(solid_hit[0])
        for sign in (-1, 1):
            hit, _ = first_hit(point, ray, [sign * 97 / 64, 0, 41 / 20], [95 / 64, 3, 1 / 20])
            self.assertFalse(hit[0])
        stored = slit_depth.astype(np.float16).astype(float)
        status, time, _ = search(point, ray, [0, 0, 0], cube_sampler(stored),
                                 max_distance=4, steps=128, refinements=8)
        self.assertEqual(status[0], "local_candidate")
        guard, _ = footprint_guard(stored, point + time[:, None] * ray)
        self.assertEqual(guard[0], "interior_candidate")


if __name__ == "__main__":
    unittest.main()
