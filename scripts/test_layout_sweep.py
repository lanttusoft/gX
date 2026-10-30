#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: synthetic normalized-layout sweep regressions; no engine launch/display.
import json
import unittest

import layout_report
import layout_sweep


def element(name, kind, x, y, w, h, text=False):
    return {"name": name, "type": kind, "text": 1 if text else 0,
            "x": x, "y": y, "w": w, "h": h}


def analysis(width, height, elements):
    return layout_report.analyze_dump({
        "screenW": width,
        "screenH": height,
        "canvases": [{"name": "Main", "elements": elements}],
    }, "title")


class SizeParsingTests(unittest.TestCase):
    def test_defaults_and_repeated_values(self):
        self.assertEqual(layout_sweep.parse_sizes(),
                         [(800, 600), (1024, 768), (1280, 720), (1920, 1080)])
        self.assertEqual(
            layout_sweep.parse_sizes(["800x600,1024x768", "1280x720"]),
            [(800, 600), (1024, 768), (1280, 720)])

    def test_invalid_or_single_size_fails(self):
        for values in (["800x600"], ["800by600"], ["800x0", "1024x768"]):
            with self.subTest(values=values):
                with self.assertRaises(ValueError):
                    layout_sweep.parse_sizes(values)

    def test_scene_selection(self):
        self.assertEqual(layout_sweep.select_scenes(),
                         list(layout_sweep.DEFAULT_SCENES))
        self.assertEqual(layout_sweep.select_scenes(["title,themlab"]),
                         ["title", "themlab"])


class ComparisonTests(unittest.TestCase):
    def compare(self, *dumps, tolerance=0.02):
        return layout_sweep.compare_dumps(
            "title", [("100x100", dumps[0]), ("200x200", dumps[1])],
            tolerance)

    def test_pure_scaling_has_no_shifts_or_growth(self):
        base = analysis(100, 100, [
            element("a", "PANEL", 10, 10, 40, 20),
            element("b", "PANEL", 20, 10, 40, 20),
        ])
        scaled = analysis(200, 200, [
            element("a", "PANEL", 20, 20, 80, 40),
            element("b", "PANEL", 40, 20, 80, 40),
        ])
        result = self.compare(base, scaled)
        self.assertEqual(result["missing"], [])
        self.assertEqual(result["added"], [])
        self.assertEqual(result["shifts"], [])
        self.assertEqual(result["overlap_growth"], [])

    def test_real_shift_and_resize_are_reported(self):
        base = analysis(100, 100, [
            element("a", "BUTTON", 10, 10, 20, 10),
        ])
        shifted = analysis(200, 200, [
            element("a", "BUTTON", 40, 20, 40, 20),
        ])
        moved = self.compare(base, shifted)
        self.assertEqual(len(moved["shifts"]), 1)
        self.assertEqual(moved["shifts"][0]["kind"], "moved")
        resized = self.compare(base, analysis(200, 200, [
            element("a", "BUTTON", 20, 20, 60, 20),
        ]))
        self.assertEqual(len(resized["shifts"]), 1)
        self.assertEqual(resized["shifts"][0]["kind"], "resized")

    def test_missing_and_added_elements(self):
        base = analysis(100, 100, [
            element("a", "BUTTON", 0, 0, 20, 20),
            element("b", "BUTTON", 20, 20, 20, 20),
        ])
        other = analysis(200, 200, [
            element("a", "BUTTON", 0, 0, 40, 40),
            element("c", "BUTTON", 40, 40, 40, 40),
        ])
        result = self.compare(base, other)
        self.assertEqual([item["element"] for item in result["missing"]],
                         ["Main:b#1"])
        self.assertEqual([item["element"] for item in result["added"]],
                         ["Main:c#1"])

    def test_overlap_growth_is_normalized(self):
        base = analysis(100, 100, [
            element("a", "PANEL", 0, 0, 40, 40),
            element("b", "PANEL", 20, 0, 40, 40),
        ])
        scaled = analysis(200, 200, [
            element("a", "PANEL", 0, 0, 80, 80),
            element("b", "PANEL", 40, 0, 80, 80),
        ])
        self.assertEqual(self.compare(base, scaled)["overlap_growth"], [])
        changed = analysis(200, 200, [
            element("a", "PANEL", 0, 0, 80, 80),
            element("b", "PANEL", 60, 0, 80, 80),
        ])
        growth = self.compare(base, changed)["overlap_growth"]
        self.assertEqual(len(growth), 1)
        self.assertLess(growth[0]["delta_area"], 0)
        self.assertLess(growth[0]["delta_ratio"], 0)

    def test_screen_mismatch(self):
        matching = analysis(200, 100, [])
        self.assertIsNone(layout_sweep.screen_mismatch(200, 100, matching))
        mismatch = layout_sweep.screen_mismatch(100, 100, matching)
        self.assertEqual(mismatch["requested"], [100, 100])
        self.assertEqual(mismatch["actual"], [200, 100])


class ReportTests(unittest.TestCase):
    def report(self):
        base = analysis(100, 100, [element("a", "BUTTON", 10, 10, 20, 10)])
        other = analysis(200, 200, [element("a", "BUTTON", 40, 20, 40, 20)])
        compared = layout_sweep.compare_dumps(
            "title", [("100x100", base), ("200x200", other)], 0.02)
        compared["dumps"] = [
            {"size": "100x100", "screenW": 100, "screenH": 100,
             "canvas_count": 1, "element_count": 1, "offscreen": 0},
            {"size": "200x200", "screenW": 200, "screenH": 200,
             "canvas_count": 1, "element_count": 1, "offscreen": 0},
        ]
        compared["screen_mismatch"] = []
        return {
            "frame": 8, "tolerance": 0.02,
            "sizes": ["100x100", "200x200"], "scenes": [compared],
            "notes": list(layout_sweep.NOTES),
            "totals": {"scenes": 1, "dumps": 2, "missing": 0, "added": 0,
                       "shifts": 1, "overlap_growth": 0, "screen_mismatch": 0},
        }

    def test_markdown_and_json_are_deterministic(self):
        first = self.report()
        second = self.report()
        self.assertEqual(layout_sweep.render_json(first),
                         layout_sweep.render_json(second))
        self.assertEqual(layout_sweep.render_markdown(first),
                         layout_sweep.render_markdown(second))
        markdown = layout_sweep.render_markdown(first)
        self.assertIn("| Scene | Baseline | Sizes", markdown)
        self.assertIn("### Shifts (1)", markdown)
        self.assertNotIn("FAIL", markdown)
        self.assertEqual(json.loads(layout_sweep.render_json(first))
                         ["sizes"], ["100x100", "200x200"])


if __name__ == "__main__":
    unittest.main()
