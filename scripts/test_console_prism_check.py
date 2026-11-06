#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for the PRISM-1 metrics and CPU budget.
import unittest

import numpy as np

import console_prism_check as prism


class PrismGateChecks(unittest.TestCase):
    def test_budget_accepts_and_rejects(self):
        self.assertAlmostEqual(prism.budget_ratio(5.0, 6.0), 1.2)
        with self.assertRaises(RuntimeError):
            prism.budget_ratio(1.0, 1.3)
        with self.assertRaises(RuntimeError):
            prism.budget_ratio(0.0, 1.0)

    def test_scanline_ratio_requires_a_two_row_pattern(self):
        pixels = np.zeros((240, 320, 3), dtype=np.float64)
        pixels[0::2] = 100.0
        pixels[1::2] = 50.0
        self.assertAlmostEqual(prism.scanline_ratio(pixels), 2.0)
        pixels[:] = 75.0
        self.assertAlmostEqual(prism.scanline_ratio(pixels), 1.0)

    def test_triad_residual_selects_the_expected_phase(self):
        pixels = np.full((240, 320, 3), 200.0, dtype=np.float64)
        for phase in range(3):
            for channel in range(3):
                if phase != channel:
                    pixels[:, phase::3, channel] *= 0.88
        residuals = prism.triad_residuals(pixels)
        for channel, phase in ((0, 0), (1, 1), (2, 2)):
            self.assertGreater(residuals[phase][channel],
                max(residuals[p][channel] for p in range(3) if p != phase))

    def test_channel_levels_separate_rgb565_from_full_color(self):
        gradient = np.linspace(0.0, 1.0, 320, dtype=np.float64)
        pixels = np.zeros((240, 320, 3), dtype=np.float64)
        levels = np.asarray([31.0, 63.0, 31.0])
        for channel in range(3):
            quantized = np.floor(gradient * levels[channel] + 0.5) \
                / levels[channel]
            pixels[:, :, channel] = quantized[None, :] * 255.0
        counts = prism.channel_levels(pixels)
        self.assertLessEqual(counts[0], 34)
        self.assertLessEqual(counts[1], 66)
        self.assertLessEqual(counts[2], 34)
        self.assertGreaterEqual(counts[0], 8)
        self.assertGreaterEqual(counts[1], 12)
        self.assertGreaterEqual(counts[2], 8)

    def test_quantization_rejects_unquantized_gradient(self):
        gradient = np.linspace(0.0, 1.0, 320, dtype=np.float64)
        pixels = np.zeros((240, 320, 3), dtype=np.float64)
        for channel in range(3):
            pixels[:, :, channel] = gradient[None, :] * 255.0
        counts = prism.channel_levels(pixels)
        self.assertGreater(counts[1], 72)


if __name__ == "__main__":
    unittest.main()
