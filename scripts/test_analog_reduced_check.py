#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: synthetic tests for the analog reduced-motion capture gate; no display/GPU.
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analog_reduced_check as analog


def image(value=40):
    return np.full((analog.HEIGHT, analog.WIDTH, 3), value, dtype=np.float32)


class MetricTests(unittest.TestCase):
    def test_identical_frames_are_static(self):
        delta = analog.image_delta(image(), image())
        self.assertTrue(analog.reduced_is_static(delta))
        self.assertFalse(analog.normal_is_animated(delta))

    def test_changed_frames_are_animated(self):
        left = image(10)
        right = image(200)
        delta = analog.image_delta(left, right)
        self.assertFalse(analog.reduced_is_static(delta))
        self.assertTrue(analog.normal_is_animated(delta))

    def test_shape_mismatch_is_an_error(self):
        with self.assertRaises(ValueError):
            analog.image_delta(image(), image()[:100])


if __name__ == "__main__":
    unittest.main()
