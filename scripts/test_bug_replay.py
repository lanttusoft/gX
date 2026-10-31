#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: focused tests for the private bug-report replay merge path.
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import bug_replay


class BugReplayTests(unittest.TestCase):
    def test_merge_diagnostics_into_base_preset(self):
        base = {
            "state": {"ENABLE_SSR": False, "ENABLE_FOG": False},
            "camera": {"position": [0, 1, 0], "target": [0, 0, 0]},
            "cameras": [{"name": "Camera 1", "view": {"position": [0, 1, 0]}}],
            "ssr": {"maxSteps": 96, "stepSize": 0.25},
            "fogVolume": {"density": 0.0005},
            "models": [{"path": "resources/model/cube.glb"}],
        }
        report = {
            "title": "synthetic",
            "preset": "resources/tools/gfx_editor.json",
            "diagnostics": {
                "state": {"ENABLE_SSR": True},
                "camera": {"position": [1, 2, 3], "target": [4, 5, 6]},
                "render_camera": {"position": [7, 8, 9], "target": [10, 11, 12]},
                "ssr": {"maxSteps": 64},
                "fogVolume": {"density": 0.02, "color": [255, 0, 0, 255]},
            },
        }
        merged = bug_replay.merge_report(base, report)
        self.assertTrue(merged["state"]["ENABLE_SSR"])
        self.assertFalse(merged["state"]["ENABLE_FOG"])
        self.assertEqual(merged["camera"]["position"], [7, 8, 9])
        self.assertEqual(merged["cameras"][0]["view"]["position"], [7, 8, 9])
        self.assertEqual(merged["ssr"]["maxSteps"], 64)
        self.assertEqual(merged["ssr"]["stepSize"], 0.25)
        self.assertEqual(merged["fogVolume"]["density"], 0.02)
        self.assertEqual(merged["fogVolume"]["color"], [255, 0, 0, 255])
        self.assertEqual(merged["models"][0]["path"], "resources/model/cube.glb")
        self.assertFalse(base["state"]["ENABLE_SSR"])
        self.assertEqual(base["camera"]["position"], [0, 1, 0])

    def test_camera_falls_back_to_editor_camera(self):
        base = {"camera": {"position": [0, 1, 0]}}
        report = {"diagnostics": {
            "camera": {"position": [1, 2, 3]},
        }}
        merged = bug_replay.merge_report(base, report)
        self.assertEqual(merged["camera"]["position"], [1, 2, 3])

    def test_missing_diagnostics_is_rejected(self):
        with self.assertRaises(ValueError):
            bug_replay.merge_report({}, {"title": "no diagnostics"})

    def test_preset_resolution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preset = root / "base.json"
            preset.write_text("{}\n")
            report = {"preset": str(preset)}
            self.assertEqual(bug_replay.resolve_preset(report), preset)
            self.assertEqual(bug_replay.resolve_preset({}, str(preset)), preset)
            with self.assertRaises(ValueError):
                bug_replay.resolve_preset({})
            with self.assertRaises(FileNotFoundError):
                bug_replay.resolve_preset({"preset": str(root / "missing.json")})

    def test_cli_replays_without_running_the_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preset = root / "base.json"
            preset.write_text('{"state": {}}\n')
            report = root / "report-1-0.json"
            report.write_text(json.dumps({
                "title": "synthetic replay",
                "preset": str(preset),
                "diagnostics": {"state": {"ENABLE_SSR": True}},
            }))
            summary = {
                "report": str(report), "preset": str(preset),
                "out": str(root / "out.png"), "width": 1024, "height": 768,
                "frame": 120, "title": "synthetic replay",
            }
            output = io.StringIO()
            with mock.patch.object(bug_replay, "replay",
                                   return_value=summary) as replay:
                with mock.patch("sys.argv", ["bug_replay.py", str(report),
                                             "--preset", str(preset)]):
                    with redirect_stdout(output):
                        self.assertEqual(bug_replay.main(), 0)
            replay.assert_called_once()
            self.assertIn("BUG_REPLAY: synthetic replay", output.getvalue())
            self.assertIn("private scene content", output.getvalue())


if __name__ == "__main__":
    unittest.main()
