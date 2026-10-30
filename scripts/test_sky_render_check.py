#!/usr/bin/env python3
#ai: synthetic unit tests for the sky renderer suite; no display/GPU required.
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sky_render_check as sky


def gradient_image(top=10, bottom=200):
    image = np.zeros((sky.HEIGHT, sky.WIDTH, 3), dtype=np.float32)
    rows = np.linspace(top, bottom, sky.HEIGHT, dtype=np.float32)
    image[:] = rows[:, None, None]
    return image


class MetricTests(unittest.TestCase):
    def test_gradient_is_finite_and_nonempty(self):
        metrics = sky.analyze_image(gradient_image())
        self.assertTrue(metrics["finite"])
        self.assertTrue(sky.roi_is_nonempty(metrics))
        self.assertGreater(metrics["roi_std"], 1.0)
        self.assertGreaterEqual(metrics["horizon_row"], 0)
        self.assertLess(metrics["horizon_row"], sky.HEIGHT)

    def test_flat_roi_is_rejected(self):
        metrics = sky.analyze_image(np.zeros((sky.HEIGHT, sky.WIDTH, 3),
                                              dtype=np.float32))
        self.assertFalse(sky.roi_is_nonempty(metrics))

    def test_non_finite_pixels_are_rejected(self):
        image = gradient_image()
        image[10, 10, 0] = np.nan
        with self.assertRaises(ValueError):
            sky.analyze_image(image)

    def test_horizon_tracks_step(self):
        image = np.zeros((sky.HEIGHT, sky.WIDTH, 3), dtype=np.float32)
        image[sky.HEIGHT // 2:] = 180.0
        metrics = sky.analyze_image(image)
        self.assertAlmostEqual(metrics["horizon_row"], sky.HEIGHT // 2,
                               delta=2)


class DeltaTests(unittest.TestCase):
    def test_identical_images(self):
        image = gradient_image()
        delta = sky.image_delta(image, image.copy())
        self.assertEqual(delta["max_channel"], 0.0)
        self.assertTrue(sky.repeat_is_identical(delta))
        self.assertFalse(sky.states_are_distinct(delta))

    def test_distinct_states(self):
        delta = sky.image_delta(gradient_image(0, 20),
                                gradient_image(160, 240))
        self.assertTrue(sky.states_are_distinct(delta))
        self.assertFalse(sky.repeat_is_identical(delta))

    def test_shape_mismatch_is_an_error(self):
        with self.assertRaises(ValueError):
            sky.image_delta(gradient_image(), gradient_image()[:100])


class SelectionTests(unittest.TestCase):
    def test_default_and_only(self):
        self.assertEqual(sky.select_states(None), list(sky.STATES))
        self.assertEqual(sky.select_states(["dawn,moon"]),
                         ["dawn", "moon"])
        self.assertEqual(sky.select_states(["noon"]), ["noon"])

    def test_unknown_state_is_an_error(self):
        with self.assertRaises(ValueError):
            sky.select_states(["nope"])


if __name__ == "__main__":
    unittest.main()
