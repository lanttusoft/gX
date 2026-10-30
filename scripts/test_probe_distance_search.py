#ai: synthetic search controls keep missing/bracketless outcomes distinct from sky.
import unittest
from pathlib import Path
import struct
import tempfile

import numpy as np
import probe_distance_search as search
from probe_distance_check import first_hit
import probe_coverage_check as coverage


class BoundedDistanceSearch(unittest.TestCase):
    def test_radial_surface_bracket_and_budget(self):
        sample = search.cube_sampler(np.full((6, 8, 8), 2.))
        status, time, queries = search.search([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0],
                                             sample, max_distance=3, steps=24, refinements=8)
        self.assertEqual(status[0], "local_candidate")
        self.assertLess(abs(time[0] - 1), .001)
        self.assertLessEqual(queries[0], 1 + 24 + 8 + 1)

    def test_sky_and_range_exhaustion_remain_uncertain(self):
        for depth in (0., 10.):
            sample = search.cube_sampler(np.full((6, 4, 4), depth))
            status, time, queries = search.search([[0, 0, 1]], [[0, 0, 1]], [0, 0, 0],
                                                 sample, max_distance=2, steps=8, refinements=4)
            self.assertEqual(status[0], "uncertain_unbracketed")
            self.assertEqual(time[0], 0)
            self.assertEqual(queries[0], 9)

    def test_start_behind_first_layer_is_uncertain(self):
        sample = search.cube_sampler(np.full((6, 4, 4), 2.))
        status, _, queries = search.search([[0, 0, 3]], [[0, 0, -1]], [0, 0, 0], sample)
        self.assertEqual(status[0], "uncertain_start_behind")
        self.assertEqual(queries[0], 1)

    def test_coverage_jump_cannot_create_certified_hit(self):
        def sample(direction):
            return np.where(direction[:, 2] >= 1, .5, 0)
        status, _, _ = search.search([[1, 0, 0]], [[0, 0, 1]], [0, 0, 0], sample,
                                     max_distance=3, steps=12, refinements=8)
        self.assertEqual(status[0], "uncertain_coverage_jump")

    def test_distance_discontinuity_fails_residual_check(self):
        def sample(direction):
            return np.where(direction[:, 0] < 0, 2., .5)
        status, _, _ = search.search([[-1, 0, 1]], [[1, 0, 0]], [0, 0, 0], sample,
                                     max_distance=2, steps=16, refinements=8)
        self.assertEqual(status[0], "uncertain_residual")

    def test_invalid_data_and_direction(self):
        with self.assertRaisesRegex(ValueError, "nonnegative"):
            search.cube_sampler(np.full((6, 4, 4), -1.))
        sample = search.cube_sampler(np.zeros((6, 4, 4)))
        self.assertEqual(sample(np.zeros((1, 3)))[0], 0)
        with self.assertRaisesRegex(ValueError, "nonzero"):
            search.search([[0, 0, 1]], [[0, 0, 0]], [0, 0, 0], sample)

    #ai: a tiny genuine depth map can satisfy the radial residual on a ray missing its geometry.
    def test_nearest_texel_alias_produces_false_local_candidate(self):
        size = 4
        center = [.75, .75, 3.]
        extents = [.2] * 3
        directions = np.stack([coverage.face_rays(i, size, size) for i in range(6)])
        directions /= np.linalg.norm(directions, axis=-1)[..., None]
        _, depth = first_hit(np.zeros(3), directions, center, extents)
        sample = search.cube_sampler(depth.astype(np.float16).astype(float))
        point = np.array([[0., 0., 1.]])
        ray = np.array([[.2, .25, 1.]])
        actual, _ = first_hit(point, ray, center, extents)
        self.assertFalse(actual[0])
        status, _, _ = search.search(point, ray, [0, 0, 0], sample,
                                     max_distance=4, steps=128, refinements=8)
        self.assertEqual(status[0], "local_candidate")

    def test_empty_and_incomplete_captures_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "no complete"):
                search.load_cubes(directory)
            path = Path(directory) / "30_0_0_0_0.bin"
            image = np.zeros((1, 1, 4), dtype="<f4")
            path.write_bytes(b"GXDVF32\0" + struct.pack("<IIifff", 1, 1, 0, 0, 0, 0)
                             + image.tobytes())
            with self.assertRaisesRegex(ValueError, "incomplete"):
                search.load_cubes(directory)


if __name__ == "__main__":
    unittest.main()
