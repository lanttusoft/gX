#!/usr/bin/env python3
#ai: synthetic HUD contrast regressions; no engine launch or display.
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

import gui_hud_contrast_check as hud


def roi_image(background, foreground, rect=(10, 5, 30, 15)):
    image = np.full((40, 60, 3), background, dtype=np.float64)
    x0, y0, x1, y1 = rect
    image[y0:y1, x0:x1] = foreground
    return image


class LuminanceTests(unittest.TestCase):
    def test_black_white_ratio(self):
        self.assertAlmostEqual(hud.relative_luminance((0, 0, 0)), 0.0)
        self.assertAlmostEqual(hud.relative_luminance((255, 255, 255)), 1.0)
        self.assertAlmostEqual(hud.contrast_ratio(
            (255, 255, 255, 255), (0, 0, 0, 255)), 21.0, places=3)

    def test_alpha_composites_against_background(self):
        blended = hud.composite_rgba((255, 0, 0, 127), (0, 0, 0, 255))
        self.assertAlmostEqual(blended[0], 127.0, delta=1.0)
        self.assertAlmostEqual(blended[3], 255.0, delta=0.1)
        alpha_ratio = hud.contrast_ratio((255, 255, 255, 127),
                                         (0, 0, 0, 255))
        self.assertGreater(alpha_ratio, 1.0)
        self.assertLess(alpha_ratio, 21.0)


class RoiTests(unittest.TestCase):
    def test_light_text_on_dark(self):
        image = roi_image(8, 240)
        values = hud.roi_contrast(image, (0, 0, 60, 40))
        self.assertGreater(values["ratio"], 10.0)
        self.assertGreater(values["dark_pixels"], 0)
        self.assertGreater(values["light_pixels"], 0)

    def test_dark_text_on_light(self):
        image = roi_image(240, 8)
        values = hud.roi_contrast(image, (0, 0, 60, 40))
        self.assertGreater(values["ratio"], 10.0)

    def test_uniform_roi_has_no_false_contrast(self):
        image = roi_image(100, 100)
        values = hud.roi_contrast(image, (0, 0, 60, 40))
        self.assertAlmostEqual(values["ratio"], 1.0, places=3)

    def test_empty_roi_is_an_error(self):
        with self.assertRaises(ValueError):
            hud.roi_contrast(np.zeros((8, 8, 3)), (10, 10, 20, 20))


class ContractTests(unittest.TestCase):
    def test_all_rois_are_inside_the_capture(self):
        for scene, rows in hud.ROIS.items():
            self.assertIn(scene, hud.SCENES)
            labels = [label for label, _ in rows]
            self.assertEqual(len(labels), len(set(labels)))
            for label, (x0, y0, x1, y1) in rows:
                self.assertTrue(label.startswith(scene + "."), label)
                self.assertTrue(0 <= x0 < x1 <= hud.WIDTH, label)
                self.assertTrue(0 <= y0 < y1 <= hud.HEIGHT, label)

    def test_scene_selection(self):
        self.assertEqual(hud.select_scenes(None), ["title", "battle"])
        self.assertEqual(hud.select_scenes(["title,battle"]),
                         ["title", "battle"])
        with self.assertRaises(ValueError):
            hud.select_scenes(["missing"])

    def test_baseline_classifies_known_and_new(self):
        report = {"scenes": {
            "title": {"rois": [
                {"label": "title.a", "pass": False},
                {"label": "title.b", "pass": True},
            ]},
            "battle": {"rois": [
                {"label": "battle.enemy", "pass": False},
            ]},
        }}
        baseline = {"known": {"title": ["title.a"],
                              "battle": ["battle.enemy", "battle.stale"]}}
        failures, new, stale = hud.classify(report, baseline)
        self.assertEqual([row["label"] for row in failures],
                         ["title.a", "battle.enemy"])
        self.assertEqual(new, [])
        self.assertEqual(stale, ["battle.stale"])
        baseline["known"]["title"] = []
        failures, new, stale = hud.classify(report, baseline)
        self.assertEqual([row["label"] for row in new], ["title.a"])
        self.assertEqual(stale, ["battle.stale"])

    def test_load_baseline_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "base.json"
            path.write_text(json.dumps({"known": {"battle": ["battle.enemy"]}}))
            self.assertEqual(hud.load_baseline(path)["known"]["battle"],
                             ["battle.enemy"])
            path.write_text("{}")
            with self.assertRaises(ValueError):
                hud.load_baseline(path)

    def test_markdown_is_deterministic_and_marks_new(self):
        row = {"label": "battle.enemy", "scene": "battle", "rect": [1, 2, 3, 4],
               "ratio": 1.6, "minimum": 4.5, "pass": False,
               "fg": [1, 2, 3, 255], "bg": [0, 0, 0, 255],
               "dark_pixels": 1, "light_pixels": 1, "pixels": 2}
        report = {
            "scenes": {"battle": {"rois": [row]}},
            "frame": 24, "notes": ["note"],
        }
        first = hud.render_markdown(report, [row], [row])
        second = hud.render_markdown(report, [row], [row])
        self.assertEqual(first, second)
        self.assertIn("NEW FAIL", first)
        self.assertIn("battle.enemy", first)


if __name__ == "__main__":
    unittest.main()
