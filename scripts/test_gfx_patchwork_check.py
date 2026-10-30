#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""Contract tests for the small-resolution patchwork helper."""

import argparse
import unittest

import numpy as np

import gfx_patchwork_check as patch


class ParseTests(unittest.TestCase):
    def test_size(self):
        self.assertEqual(patch.parse_size("320x240"), (320, 240))
        with self.assertRaises(argparse.ArgumentTypeError):
            patch.parse_size("320")
        with self.assertRaises(argparse.ArgumentTypeError):
            patch.parse_size("4x4")

    def test_crop(self):
        self.assertEqual(patch.parse_crop("16,32,64,48"), (16, 32, 64, 48))
        with self.assertRaises(argparse.ArgumentTypeError):
            patch.parse_crop("16,32,0,48")

    def test_variant_and_env(self):
        name, env = patch.parse_variant("no-bloom:GX_T_BLOOM=0;GX_T_HUD=0")
        self.assertEqual(name, "no-bloom")
        self.assertEqual(env, {"GX_T_BLOOM": "0", "GX_T_HUD": "0"})
        self.assertEqual(patch.parse_global_env(["A=1", "B=2"]),
                         {"A": "1", "B": "2"})
        with self.assertRaises(argparse.ArgumentTypeError):
            patch.parse_variant("baseline:not-an-assignment")


class VariantTests(unittest.TestCase):
    def test_value_grid_is_row_major(self):
        variants, cols = patch.grid_variants(
            "A", "0,1", "B", "10,20,30", {"C": "1"})
        self.assertEqual(cols, 2)
        self.assertEqual(len(variants), 6)
        self.assertEqual(variants[0], ("x=0 y=10", {"A": "0", "B": "10", "C": "1"}))
        self.assertEqual(variants[-1], ("x=1 y=30", {"A": "1", "B": "30", "C": "1"}))

    def test_bit_grid_is_8x8_for_three_bits(self):
        variants, count = patch.bit_variants("X", "Y", 3, {})
        self.assertEqual(count, 8)
        self.assertEqual(len(variants), 64)
        self.assertEqual(variants[0][0], "x=0b000 y=0b000")
        self.assertEqual(variants[0][1], {"X": "0", "Y": "0"})
        self.assertEqual(variants[-1][0], "x=0b111 y=0b111")
        self.assertEqual(variants[-1][1], {"X": "7", "Y": "7"})

    def test_bit_grid_rejects_bad_width(self):
        with self.assertRaises(ValueError):
            patch.bit_variants("X", "Y", 0, {})
        with self.assertRaises(ValueError):
            patch.bit_variants("X", "Y", 9, {})

    def test_value_grid_rejects_empty_axis(self):
        with self.assertRaises(ValueError):
            patch.grid_variants("A", "", "B", "1", {})


class CropTests(unittest.TestCase):
    def test_auto_crop_centres_on_change(self):
        baseline = np.zeros((64, 64, 3), dtype=np.int16)
        changed = baseline.copy()
        changed[40:56, 8:24, :] = 255
        x, y, width, height, delta = patch.auto_crop([baseline, changed], 16)
        self.assertEqual((width, height), (16, 16))
        self.assertEqual((x, y), (7, 39))
        self.assertGreater(delta, 0.0)

    def test_auto_crop_flat_frame_stays_centred(self):
        frame = np.zeros((64, 64, 3), dtype=np.int16)
        x, y, width, height, delta = patch.auto_crop([frame, frame], 16)
        self.assertEqual((x, y, width, height, delta), (24, 24, 16, 16, 0.0))


if __name__ == "__main__":
    unittest.main()
