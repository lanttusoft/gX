#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: synthetic layout-report regressions; no engine launch or display.
import json
import tempfile
import unittest
from pathlib import Path

import layout_report


def element(name, kind, x, y, w, h, text=False):
    return {"name": name, "type": kind, "text": 1 if text else 0,
            "x": x, "y": y, "w": w, "h": h}


def synthetic_dump():
    return {
        "screenW": 100,
        "screenH": 100,
        "canvases": [
            {
                "name": "Main",
                "elements": [
                    element("bg", "PANEL", 0, 0, 100, 100),
                    element("label", "LABEL", 10, 10, 20, 10, text=True),
                    element("overlap", "BUTTON", 15, 12, 20, 10),
                    element("off", "BUTTON", 90, 90, 20, 20, text=True),
                ],
            },
            {"name": "Empty", "elements": []},
        ],
    }


class DumpTests(unittest.TestCase):
    def write(self, data):
        handle = tempfile.NamedTemporaryFile(
            "w", suffix=".json", delete=False)
        self.addCleanup(lambda: Path(handle.name).unlink(missing_ok=True))
        with handle:
            json.dump(data, handle)
        return Path(handle.name)

    def test_load_valid_dump(self):
        path = self.write(synthetic_dump())
        data = layout_report.load_dump(path)
        self.assertEqual(data["screenW"], 100)
        self.assertEqual(len(data["canvases"]), 2)

    def test_load_rejects_bad_schema(self):
        cases = [
            {"screenH": 100, "canvases": []},
            {"screenW": 100, "screenH": 100},
            {"screenW": 100, "screenH": 100, "canvases": [{"elements": "x"}]},
            {"screenW": 100, "screenH": 100, "canvases": [
                {"name": "c", "elements": [
                    {"name": "x", "x": "bad", "y": 0, "w": 1, "h": 1}]}]},
        ]
        for case in cases:
            with self.subTest(case=case):
                with self.assertRaises(ValueError):
                    layout_report.load_dump(self.write(case))


class AnalysisTests(unittest.TestCase):
    def test_counts_offscreen_overlaps_and_text(self):
        scene = layout_report.analyze_dump(synthetic_dump(), "title")
        self.assertEqual(scene["scene"], "title")
        self.assertEqual(scene["flag"], "--title")
        self.assertEqual(scene["canvas_count"], 2)
        self.assertEqual(scene["element_count"], 4)
        self.assertEqual(len(scene["offscreen"]), 1)
        self.assertEqual(scene["offscreen"][0]["element"], "off")
        self.assertEqual(scene["text_count"], 2)
        self.assertEqual(scene["canvases"][0]["element_count"], 4)
        self.assertEqual(scene["canvases"][1]["element_count"], 0)
        self.assertGreaterEqual(len(scene["overlaps"]), 4)
        self.assertTrue(scene["background_overlaps"])
        self.assertTrue(all("full-screen" not in item["context"]
                            for item in scene["foreground_overlaps"]))
        contexts = {tuple(item["context"]) for item in scene["overlaps"]}
        self.assertTrue(any("full-screen" in context for context in contexts))
        self.assertTrue(any("contains" in context for context in contexts))

    def test_ordering_is_deterministic(self):
        first = layout_report.analyze_dump(synthetic_dump(), "title")
        second = layout_report.analyze_dump(synthetic_dump(), "title")
        self.assertEqual(first, second)
        report = {
            "width": 100, "height": 100, "frame": 8,
            "scenes": [first],
            "notes": list(layout_report.NOTES),
            "totals": {"scenes": 1, "canvases": 2, "elements": 4,
                       "offscreen": 1, "overlaps": 4, "text": 2},
        }
        self.assertEqual(layout_report.render_json(report),
                         layout_report.render_json(
                             dict(report, scenes=[second])))
        self.assertEqual(layout_report.render_markdown(report),
                         layout_report.render_markdown(
                             dict(report, scenes=[second])))

    def test_markdown_has_tables_and_no_failure_claim(self):
        scene = layout_report.analyze_dump(synthetic_dump(), "title")
        report = {
            "width": 100, "height": 100, "frame": 8,
            "scenes": [scene],
            "notes": list(layout_report.NOTES),
            "totals": {
                "scenes": 1, "canvases": scene["canvas_count"],
                "elements": scene["element_count"],
                "offscreen": len(scene["offscreen"]),
                "overlaps": len(scene["overlaps"]),
                "text": scene["text_count"],
            },
        }
        markdown = layout_report.render_markdown(report)
        self.assertIn("| Scene | Canvases | Elements", markdown)
        self.assertIn("## title (`--title`)", markdown)
        self.assertIn("### Off-screen (1)", markdown)
        self.assertIn("### Overlaps", markdown)
        self.assertIn("### Text elements (2)", markdown)
        self.assertNotIn("FAIL", markdown)
        json_report = json.loads(layout_report.render_json(report))
        self.assertEqual(json_report["totals"]["offscreen"], 1)

    def test_empty_scene_is_reported_not_failed(self):
        data = {"screenW": 10, "screenH": 10, "canvases": []}
        scene = layout_report.analyze_dump(data, "gfx")
        self.assertEqual(scene["canvas_count"], 0)
        self.assertEqual(scene["element_count"], 0)
        self.assertIn("No GUI canvases", layout_report.render_markdown({
            "width": 10, "height": 10, "frame": 8, "scenes": [scene],
            "notes": [], "totals": {"scenes": 1, "canvases": 0,
                                    "elements": 0, "offscreen": 0,
                                    "overlaps": 0, "text": 0}}))


class SelectionTests(unittest.TestCase):
    def test_default_matrix(self):
        self.assertEqual(layout_report.select_scenes(),
                         list(layout_report.DEFAULT_SCENES))

    def test_only_accepts_commas_and_flags(self):
        self.assertEqual(layout_report.select_scenes(["title,themlab"]),
                         ["title", "themlab"])
        self.assertEqual(layout_report.select_scenes(["--themlab"]),
                         ["themlab"])

    def test_unknown_scene_is_an_error(self):
        with self.assertRaises(ValueError):
            layout_report.select_scenes(["nope"])


if __name__ == "__main__":
    unittest.main()
