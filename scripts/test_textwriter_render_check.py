#!/usr/bin/env python3
#ai: synthetic unit tests for the textwriter render suite; no display/GPU.
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import textwriter_render_check as tw


def text_image(width=200):
    image = np.zeros((tw.HEIGHT, tw.WIDTH, 3), dtype=np.float32)
    image[80:100, 60:60 + width] = 220.0
    return image


class MetricTests(unittest.TestCase):
    def test_text_metrics_are_finite_and_nonempty(self):
        metrics = tw.text_metrics(text_image())
        self.assertTrue(metrics["finite"])
        self.assertGreaterEqual(metrics["ink_pixels"], tw.MIN_INK_PIXELS)
        self.assertIsNotNone(metrics["ink_bbox"])

    def test_empty_roi_has_no_ink(self):
        image = np.zeros((tw.HEIGHT, tw.WIDTH, 3), dtype=np.float32)
        metrics = tw.text_metrics(image)
        self.assertEqual(metrics["ink_pixels"], 0)
        self.assertIsNone(metrics["ink_bbox"])

    def test_non_finite_pixels_rejected(self):
        image = text_image()
        image[0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            tw.text_metrics(image)

    def test_reveal_progression(self):
        self.assertTrue(tw.reveal_is_progressive([
            {"ink_pixels": 30}, {"ink_pixels": 100}, {"ink_pixels": 300}]))
        self.assertFalse(tw.reveal_is_progressive([
            {"ink_pixels": 100}, {"ink_pixels": 100}, {"ink_pixels": 300}]))
        self.assertFalse(tw.reveal_is_progressive([
            {"ink_pixels": 30}, {"ink_pixels": 100}]))


class DeltaTests(unittest.TestCase):
    def test_identical_and_distinct(self):
        image = text_image()
        same = tw.image_delta(image, image.copy())
        self.assertEqual(same["max_channel"], 0.0)
        changed = image.copy()
        changed[100:120, 100:120] = 255.0
        delta = tw.image_delta(image, changed)
        self.assertTrue(tw.states_are_distinct(delta))
        self.assertGreater(delta["max_channel"], 0.0)

    def test_shape_mismatch(self):
        with self.assertRaises(ValueError):
            tw.image_delta(text_image(), text_image()[:100])


class SelectionTests(unittest.TestCase):
    def test_default_and_subset(self):
        self.assertEqual(tw.select_cases(None), list(tw.CASES))
        self.assertEqual(tw.select_cases(["plain-early,choice"]),
                         ["plain-early", "choice"])
        with self.assertRaises(ValueError):
            tw.select_cases(["missing"])

    def test_fixture_has_tags_and_choice(self):
        self.assertEqual(len(tw.FIXTURE_LINES), 5)
        tagged = tw.FIXTURE_LINES[1]
        for token in ("<color", "<red>", "<b>", "<i>", "<u>", "<size",
                      "<spacing", "<matrix", "<wave", "<rainbow",
                      "<gradient", "<shake", "<disintegrate"):
            self.assertIn(token, tagged)
        self.assertIn("<option line='3'>Go left</option>", tw.FIXTURE_LINES[2])
        self.assertIn("<option line='4'>Go right</option>", tw.FIXTURE_LINES[2])


if __name__ == "__main__":
    unittest.main()
