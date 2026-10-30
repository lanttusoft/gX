#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: fixed filter footprint witnesses and negatives; no general GPU or reflection visibility certification.
from fractions import Fraction as Q
from itertools import product
import tempfile
from pathlib import Path
import struct
import unittest

import numpy as np

import ssr_boundary_footprint_check as footprint


class BoundaryFootprintTests(unittest.TestCase):
    def test_all_gl_faces_and_rescaling_roundtrip(self):
        for face in range(6):
            for sc, tc in product((-.7, 0., .4), repeat=2):
                ray = footprint.face_directions(face, np.array(sc), np.array(tc))
                for scale in (.001, 1., 1000.):
                    selected, u, v, tied = footprint.face_uv(ray * scale)
                    self.assertEqual(selected, face)
                    self.assertAlmostEqual(u, (sc + 1) / 2)
                    self.assertAlmostEqual(v, (tc + 1) / 2)
                    self.assertFalse(tied)

    def test_minus_x_mapping_is_not_plus_x_mapping(self):
        _, u, _, _ = footprint.face_uv(np.array([[-1., .2, .3], [1., .2, .3]]))
        np.testing.assert_allclose(u, [.65, .35])

    def test_undefined_direction_and_unknown_mip_are_rejected(self):
        for ray in ([0, 0, 0], [np.nan, 0, 1], [1, 2]):
            with self.assertRaises(ValueError):
                footprint.face_uv(np.array(ray))
        cube = np.zeros((6, 8, 8, 4))
        for lod in (-1, 1, np.nan):
            with self.assertRaises(ValueError):
                footprint.replay(cube, np.array([[0., 0., 1.]]), lod)

    def test_stencil_weights_are_convex(self):
        for face in range(6):
            ray = footprint.face_directions(face, np.array([-.2, .1, .6]), np.array([-.5, .3, .5]))
            taps, seam = footprint.bilinear_taps(ray, 8)
            self.assertFalse(seam.any())
            total = sum(tap[3] for tap in taps)
            np.testing.assert_allclose(total, 1, atol=1e-15)
            for *_, weight in taps:
                self.assertTrue(np.all(weight >= 0))

    def test_seams_remain_uncertain_even_when_clamped_color_is_red(self):
        cube = np.ones((6, 8, 8, 4))
        for ray in ([1., 1., 0.], [1., .99, 0.]):
            _, support, seam, _, _ = footprint.replay(cube, np.array([ray]), .003)
            self.assertTrue(support[0])
            self.assertTrue(seam[0])

    def test_filter_can_spill_red_without_point_hit(self):
        cube = np.zeros((6, 8, 8, 4))
        cube[4, 3, 3, 0] = 1
        ray = np.array([[.025, -.025, 1.]])
        color, support, seam, _, _ = footprint.replay(cube, ray, .003)
        self.assertEqual(footprint.nearest(cube, ray)[0, 0], 0)
        self.assertGreater(color[0, 0], 0)
        self.assertTrue(support[0])
        self.assertFalse(seam[0])

    def test_positive_support_expands_coarse_texel_to_base_cells(self):
        cube = np.zeros((6, 8, 8, 4))
        cube[4, 3, 3, 0] = 1
        ray = np.array([[.125, -.125, 1.]])
        base, base_support, _, _, _ = footprint.replay(cube, ray, 0)
        mixed, mixed_support, _, _, _ = footprint.replay(cube, ray, .5)
        self.assertEqual(base[0, 0], 0)
        self.assertFalse(base_support[0])
        self.assertGreater(mixed[0, 0], 0)
        self.assertTrue(mixed_support[0])

    def test_coarse_dominance_is_not_a_red_support_test(self):
        cube = np.zeros((6, 8, 8, 4))
        cube[4, 2:4, 2:4, 1] = 1
        cube[4, 3, 3] = [1, 0, 0, 1]
        ray = np.array([[.125, -.125, 1.]])
        color, support, _, _, _ = footprint.replay(cube, ray, .5)
        self.assertLess(color[0, 0], color[0, 1] * 3)
        self.assertTrue(support[0])

    def test_no_support_cannot_invent_red(self):
        cube = np.zeros((6, 8, 8, 4))
        cube[..., 1] = 1
        color, support, _, _, _ = footprint.replay(cube, np.array([[.2, .1, 1.]]), .003)
        self.assertEqual(color[0, 0], 0)
        self.assertFalse(support[0])

    def test_exact_convex_mix_preserves_red_ceiling(self):
        ceiling = Q(1, 10000)
        for x, y, mip in product((Q(0), Q(1, 4), Q(1, 2), Q(1)), repeat=3):
            weights = [(1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y]
            self.assertEqual(sum(weights), 1)
            for reds in product((Q(0), ceiling / 2, ceiling), repeat=4):
                low = sum(w * red for w, red in zip(weights, reds))
                high = sum(reds) / 4
                self.assertLessEqual((1 - mip) * low + mip * high, ceiling)

    def test_face_dump_validates_extent_and_nonfinite_values(self):
        with tempfile.TemporaryDirectory(dir="/tmp/opencode") as directory:
            path = Path(directory) / "face.bin"
            array = np.zeros((8, 8, 4), dtype="<f4")
            header = b"GXHDR1\0\0" + struct.pack("<III", 8, 8, 4)
            path.write_bytes(header + array.tobytes())
            self.assertEqual(footprint.read_face(path).shape, (8, 8, 4))
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                footprint.read_face(path)
            array[0, 0, 0] = np.nan
            path.write_bytes(header + array.tobytes())
            with self.assertRaises(ValueError):
                footprint.read_face(path)

    def test_diagnostic_controls_reject_missing_output_and_unexplained_color(self):
        reflection = np.array([[[1., 0., 0., 1.], [0., 1., 0., 1.], [1., 0., 0., 1.]]])
        environment = reflection.copy()
        prediction = {"ground": np.ones((1, 3), bool), "red": np.array([[True, False, False]]),
                      "interior_red": np.array([[True, False, False]]),
                      "interior_green": np.array([[False, True, False]])}
        rows = [{"red_class_mismatches": 0, "green_class_mismatches": 0} for _ in range(6)]
        support = np.ones(3, bool)
        seam = np.zeros(3, bool)
        checks = footprint.diagnostic_checks(reflection, environment, prediction, support, seam, rows)
        self.assertTrue(all(checks.values()))
        self.assertFalse(footprint.diagnostic_checks(np.zeros_like(reflection), environment,
            prediction, support, seam, rows)["red_interior_preserved"])
        self.assertFalse(footprint.diagnostic_checks(reflection, np.zeros_like(environment),
            prediction, support, seam, rows)["environment_diagnostic_executed"])
        self.assertFalse(footprint.diagnostic_checks(reflection, environment, prediction,
            np.array([True, True, False]), seam, rows)["no_unexplained_red_outside_support"])
        self.assertFalse(footprint.diagnostic_checks(reflection, environment, prediction,
            support, np.array([False, False, True]), rows)["boundary_seams_not_silently_certified"])
        leaked = reflection.copy()
        leaked[0, 1] = [1, 0, 0, 1]
        self.assertFalse(footprint.diagnostic_checks(leaked, environment, prediction,
            support, seam, rows)["deep_occlusion_remains_clear"])
        rows[0]["red_class_mismatches"] = 1
        self.assertFalse(footprint.diagnostic_checks(reflection, environment, prediction,
            support, seam, rows)["native_faces_match_first_hit_oracle"])


if __name__ == "__main__":
    unittest.main()
