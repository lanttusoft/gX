#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: exact zero-encoding witness, source policy guards and precision/occlusion negatives.
from fractions import Fraction
from pathlib import Path
import re
import unittest

import numpy as np

import ssr_gap_check as gap
import ssr_normal_precision_check as precision


class NormalPrecisionTests(unittest.TestCase):
    def test_unorm8_cannot_represent_zero_after_normal_decode(self):
        for code in range(256):
            self.assertNotEqual(2 * Fraction(code, 255) - 1, 0)
        self.assertEqual(2 * Fraction(127, 255) - 1, -Fraction(1, 255))
        self.assertEqual(2 * Fraction(128, 255) - 1, Fraction(1, 255))
        self.assertEqual(np.float16(.5) * 2 - 1, 0)

    def test_flat_normal_quantization_tilts_reflection(self):
        incoming = np.array([0., -1., -1.]) / np.sqrt(2)
        flat = np.array([0., 1., 0.])
        quantized = np.array([-1 / 255, 1, -1 / 255])
        quantized /= np.linalg.norm(quantized)
        exact = incoming - 2 * np.dot(incoming, flat) * flat
        biased = incoming - 2 * np.dot(incoming, quantized) * quantized
        self.assertEqual(exact[0], 0)
        self.assertLess(biased[0], -.005)
        self.assertGreater(np.linalg.norm(exact - biased), .009)

    def test_float_normal_policy_and_disabled_allocation_gate(self):
        root = Path(__file__).resolve().parents[1]
        load = (root / "src/renderer/gfx/load.c").read_text()
        load = re.sub(r"/\*.*?\*/|//[^\n]*", "", load, flags=re.S)
        self.assertIn("gfx->ssr.normalMap.hdr = true;", load)
        self.assertIn("gfx->ssr.normalBufferEnabled = normalHook && normalHook[0] != '0';", load)
        self.assertIn("gfx->ssr.normalMap.wrapper.loadingState.loadAsap = gfx->ssr.normalBufferEnabled;", load)
        draw = (root / "src/renderer/gfx/draw.c").read_text()
        draw = re.sub(r"/\*.*?\*/|//[^\n]*", "", draw, flags=re.S)
        self.assertIn("gX_Texture_SetFilter(gfx->ssr.normalMap.wrapper.rt.texture, gX_TEXTURE_FILTER_POINT);", draw)

    def test_precision_and_occlusion_controls_fail_independently(self):
        prediction = gap.oracle(True)
        normal = np.broadcast_to([.5, 1, .5, 1], (gap.HEIGHT, gap.WIDTH, 4)).copy()
        direction = np.ones_like(normal)
        direction[..., :3] = precision.expected_directions() * .5 + .5
        reflection = np.zeros_like(normal)
        reflection[prediction["red"], 0] = 1
        reflection[prediction["green"], 1] = 1
        _, checks = precision.precision_metrics(normal, direction, reflection, prediction)
        self.assertTrue(all(checks.values()))
        normal[..., 0] = 127 / 255
        _, checks = precision.precision_metrics(normal, direction, reflection, prediction)
        self.assertFalse(checks["flat_normal_components_preserved"])
        self.assertTrue(checks["no_red_inside_occluded_interior"])
        normal[..., 0] = .5
        direction[..., 0] += .01
        _, checks = precision.precision_metrics(normal, direction, reflection, prediction)
        self.assertFalse(checks["ray_encoding_allowance"])
        direction[..., 0] -= .01
        reflection[prediction["interior_green"], :3] = [1, 0, 0]
        _, checks = precision.precision_metrics(normal, direction, reflection, prediction)
        self.assertFalse(checks["no_red_inside_occluded_interior"])


if __name__ == "__main__":
    unittest.main()
