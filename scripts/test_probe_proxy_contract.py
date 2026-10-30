#ai: authored exact-solid proxy contracts; no arbitrary mesh-bound hit certification.
from fractions import Fraction as Q
from itertools import product
import unittest

import numpy as np
from probe_proxy_contract import proxy_lookup
from test_probe_box_contract import face_oracle


def box(center=(0, 0, 3), extents=(1, 1, 1), exact=True):
    return {"kind": "solid_aabb", "exact": exact, "center": center, "half_extents": extents}


class ProxyContract(unittest.TestCase):
    def test_geometry_hit_uses_authored_surface_and_capture_origin(self):
        result = proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [1, 0, 0], [box()], True)
        status, time, owner, lookup, hit, visibility = result
        self.assertEqual(status[0], "local_geometry")
        self.assertEqual(time[0], 1)
        self.assertEqual(owner[0], 0)
        np.testing.assert_equal(hit, [[0, 0, 2]])
        np.testing.assert_equal(lookup, [[-1, 0, 2]])
        self.assertEqual(visibility[0], "capture_visible")

    def test_sky_preserves_outgoing_direction_only_for_complete_exact_scene(self):
        result = proxy_lookup([[0, 0, 1]], [[2, 0, 0]], [4, 5, 6], [box()], True)
        self.assertEqual(result[0][0], "sky_direction")
        np.testing.assert_equal(result[3], [[1, 0, 0]])
        self.assertEqual(result[5][0], "not_certified")

    def test_incomplete_scene_neither_hit_nor_miss_is_certified(self):
        for ray in ([[0, 0, 1]], [[1, 0, 0]]):
            result = proxy_lookup([[0, 0, 1]], ray, [0, 0, 0], [box()], False)
            self.assertEqual(result[0][0], "uncertain_scene_contract")
            self.assertEqual(result[5][0], "not_certified")

    def test_approximate_bounds_cannot_certify_solid_hit(self):
        result = proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0], [box(exact=False)], True)
        self.assertEqual(result[0][0], "uncertain_scene_contract")

    def test_capture_hidden_surface_is_distinct_from_geometry_hit(self):
        result = proxy_lookup([[0, 0, 5]], [[0, 0, -1]], [0, 0, 0], [box()], True)
        self.assertEqual(result[0][0], "local_geometry")
        np.testing.assert_equal(result[4], [[0, 0, 4]])
        self.assertEqual(result[5][0], "uncertain_capture_hidden")

    def test_capture_at_hit_uses_nonzero_fallback_and_uncertain_visibility(self):
        result = proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [0, 0, 2], [box()], True)
        self.assertEqual(result[0][0], "local_geometry")
        self.assertEqual(result[5][0], "uncertain_capture_origin")
        np.testing.assert_equal(result[3], [[0, 0, 1]])

    def test_capture_occluder_is_not_receiver_hit_owner(self):
        occluder = box(center=(3, 0, 3), extents=(.25, .25, .25))
        result = proxy_lookup([[2, 0, 3]], [[-1, 0, 0]], [4, 0, 3], [box(), occluder], True)
        self.assertEqual(result[0][0], "local_geometry")
        self.assertEqual(result[2][0], 0)
        self.assertEqual(result[5][0], "uncertain_capture_hidden")

    def test_nearest_proxy_wins_regardless_of_order(self):
        near, far = box(), box(center=(0, 0, 7))
        result = proxy_lookup([[0, 0, 0]], [[0, 0, 1]], [0, 0, 0], [far, near], True)
        self.assertEqual(result[2][0], 1)
        self.assertEqual(result[1][0], 2)

    def test_explicit_slit_geometry_distinguishes_identical_sampled_depth_scenes(self):
        panels = [box(center=(sign * 97 / 64, 0, 41 / 20), extents=(95 / 64, 3, 1 / 20))
                  for sign in (-1, 1)]
        point, ray = [[1 / 64, 0, 1]], [[0, 0, 1]]
        slit = proxy_lookup(point, ray, [0, 0, 0], panels, True)
        solid = proxy_lookup(point, ray, [0, 0, 0],
                             [box(center=(0, 0, 41 / 20), extents=(3, 3, 1 / 20))], True)
        self.assertEqual(slit[0][0], "sky_direction")
        self.assertEqual(solid[0][0], "local_geometry")

    def test_exact_rational_face_reference(self):
        origins = ((Q(0),) * 3, (Q(0), Q(0), Q(1)), (Q(0), Q(0), Q(5)))
        center, extents = (Q(0), Q(0), Q(3)), (Q(1),) * 3
        for origin in origins:
            for ray in product((Q(-2), Q(0), Q(1)), repeat=3):
                if not any(ray):
                    continue
                try:
                    expected = face_oracle(origin, ray, center, extents)[0]
                except ValueError:
                    expected = None
                result = proxy_lookup([origin], [ray], [0, 0, 0], [box()], True)
                if result[0][0] == "uncertain_boundary":
                    self.assertIsNotNone(expected)
                    hit = tuple(p + expected * d for p, d in zip(origin, ray))
                    self.assertGreaterEqual(sum(abs(p - c) == h for p, c, h in zip(hit, center, extents)), 2)
                    self.assertEqual(result[5][0], "not_certified")
                    continue
                self.assertEqual(result[0][0] == "local_geometry", expected is not None)
                if expected is not None:
                    self.assertAlmostEqual(result[1][0], float(expected) * np.linalg.norm(np.array(ray, dtype=float)))

    def test_contract_and_direction_validation(self):
        with self.assertRaisesRegex(ValueError, "completeness"):
            proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0], [], "yes")
        with self.assertRaisesRegex(ValueError, "nonzero"):
            proxy_lookup([[0, 0, 1]], [[0, 0, 0]], [0, 0, 0], [], True)
        with self.assertRaisesRegex(ValueError, "declaration"):
            proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0], [{"kind": "mesh_bounds"}], True)
        empty = proxy_lookup([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0], [], True)
        self.assertEqual(empty[0][0], "sky_direction")


if __name__ == "__main__":
    unittest.main()
