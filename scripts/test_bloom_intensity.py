#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native audit-policy regression; licensed source, matching controls and unchanged strict cuts.
import unittest
from unittest.mock import patch

import numpy as np
import bloom_audit as bloom


class BloomIntensityTests(unittest.TestCase):
    def images(self):
        control = np.full((100, 100, 3), 80.0, dtype=np.float32)
        images = {"control": control, "intensity_control": control}
        for key, count in [("intensity_0.75", 100), ("intensity_1.5", 200),
                           ("intensity_3", 400), ("intensity_glow", 800),
                           ("showcase_intensity_0.75", 100), ("showcase_intensity_1.5", 200),
                           ("showcase_intensity_3", 400), ("glow", 800)]:
            image = control.copy()
            image.reshape(-1, 3)[:count] += 12
            images[key] = image
        return images

    def evaluate(self, images):
        result = bloom.Audit()
        with patch.object(bloom.audit, "load_rgb", side_effect=lambda key: images[key]):
            bloom.check_intensity({key: key for key in images}, result)
        return result

    def test_source_and_controls(self):
        requests = bloom.requests_for({"intensity"})
        self.assertIn("control", requests)
        self.assertIn("glow", requests)
        for key in ["intensity_control", "intensity_0.75", "intensity_1.5", "intensity_3", "intensity_glow"]:
            scene, env = requests[key]
            self.assertEqual(scene, bloom.EMISSIVE)
            self.assertEqual(env["GX_DIAG_CUBE"], "1")
            self.assertEqual(env["GX_T_EMISSIVEMAP"], "1")
            self.assertEqual(env["GX_T_BLOOMTHRESH"], "0.85")
        first = requests["intensity_control"][1]
        for key in ["intensity_0.75", "intensity_1.5", "intensity_3", "intensity_glow"]:
            self.assertEqual({k: v for k, v in first.items() if k != "GX_T_BLOOMINT"},
                             {k: v for k, v in requests[key][1].items() if k != "GX_T_BLOOMINT"})

    def test_valid_ramp(self):
        self.assertEqual(self.evaluate(self.images()).failures, [])

    def test_flat_ramp_rejected(self):
        images = self.images()
        for key in ["intensity_1.5", "intensity_3", "intensity_glow"]:
            images[key] = images["intensity_0.75"]
        self.assertIn("intensity.ramp-monotonic", self.evaluate(images).failures)

    def test_reversed_ramp_rejected(self):
        images = self.images()
        images["intensity_0.75"], images["intensity_3"] = images["intensity_3"], images["intensity_0.75"]
        self.assertIn("intensity.ramp-monotonic", self.evaluate(images).failures)

    def test_washout_rejected(self):
        images = self.images()
        images["intensity_glow"] = images["intensity_control"].copy()
        images["intensity_glow"].reshape(-1, 3)[:2600] = 200
        self.assertIn("intensity.ramp-bounded", self.evaluate(images).failures)

    def test_original_showcase_still_checked(self):
        images = self.images()
        images["showcase_intensity_1.5"] = images["showcase_intensity_0.75"]
        self.assertIn("intensity.showcase-brightens", self.evaluate(images).failures)

    def test_missing_source_rejected(self):
        images = self.images()
        del images["intensity_glow"]
        self.assertIn("intensity.ramp", self.evaluate(images).failures)

    def test_subcut_changes_still_rejected_by_spatial_ramp(self):
        images = self.images()
        for key, count in [("intensity_0.75", 100), ("intensity_1.5", 200), ("intensity_3", 400)]:
            images[key] = images["intensity_control"].copy()
            images[key].reshape(-1, 3)[:count] += 9
        self.assertIn("intensity.ramp-monotonic", self.evaluate(images).failures)


if __name__ == "__main__":
    unittest.main()
