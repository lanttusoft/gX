#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free controls for the film-grain metrics and save field.
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

import console_grain_check as grain


class GrainGateChecks(unittest.TestCase):
    def test_flat_noise_separates_flat_and_textured_fields(self):
        flat = np.full((768, 1024, 3), 100.0, dtype=np.float32)
        textured = flat.copy()
        textured[grain.FLAT_CROP] += np.random.default_rng(7).integers(
            -3, 4, size=textured[grain.FLAT_CROP].shape)
        self.assertLess(grain.flat_noise(flat), grain.flat_noise(textured))

    def test_saved_grain_field(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "slot.json"
            path.write_text(json.dumps({"video": {"grain": 1}}),
                encoding="utf-8")
            self.assertEqual(grain.saved_grain(path), 1)
            path.write_text(json.dumps({"video": {"grain": 0}}),
                encoding="utf-8")
            self.assertEqual(grain.saved_grain(path), 0)

    def test_grain_thresholds_are_positive(self):
        self.assertGreater(grain.MIN_HF_GAIN, 1.0)
        self.assertGreater(grain.MIN_ANIM_DELTA, 0.0)


if __name__ == "__main__":
    unittest.main()
