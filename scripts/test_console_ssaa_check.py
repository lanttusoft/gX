#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for SSAA OSD split, save field and budget.
import csv
import json
import tempfile
import unittest
from pathlib import Path

import console_ssaa_check as ssaa


class SSAAGateChecks(unittest.TestCase):
    def test_budget_accepts_and_rejects(self):
        self.assertAlmostEqual(ssaa.budget_ratio(5.0, 10.0), 2.0)
        with self.assertRaises(RuntimeError):
            ssaa.budget_ratio(1.0, 3.1)
        with self.assertRaises(RuntimeError):
            ssaa.budget_ratio(0.0, 1.0)

    def test_profile_window_is_measured(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.csv"
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                writer.writerow(["frame", "total_ms"])
                for frame in range(ssaa.WARMUP_FRAMES + ssaa.SAMPLE_FRAMES):
                    writer.writerow([frame, 99.0 if frame < ssaa.WARMUP_FRAMES else 10.0])
            self.assertAlmostEqual(ssaa.profile_total(path), 10.0)

    def test_profile_rejects_empty_window(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.csv"
            path.write_text("frame,total_ms\n1,10\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                ssaa.profile_total(path)

    def test_osd_split_is_required(self):
        log = ("CONSOLE_OSD: show render=640x480 out=320x240 mode=SSAA2x "
               "prism=off cart=Y2K CGI slot=1 state=EMPTY play=PAUSED "
               "rect=10,435,200,35 textfit=185,13")
        ssaa.osd_has(log, render=(640, 480), out=(320, 240), mode="SSAA2x")
        with self.assertRaises(RuntimeError):
            ssaa.osd_has(log, render=(320, 240), out=(320, 240), mode="SSAA2x")
        with self.assertRaises(RuntimeError):
            ssaa.osd_has(log, render=(640, 480), out=(640, 480), mode="SSAA2x")

    def test_saved_video_factor(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "slot.json"
            path.write_text(json.dumps({"video": {"ssaa": 2}}), encoding="utf-8")
            self.assertEqual(ssaa.saved_ssaa(path), 2)
            path.write_text(json.dumps({"video": {"ssaa": 0}}), encoding="utf-8")
            self.assertEqual(ssaa.saved_ssaa(path), 0)


if __name__ == "__main__":
    unittest.main()
