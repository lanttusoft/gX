#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free controls for the 4x SSAA probe budget and profile parser.
import csv
import tempfile
import unittest
from pathlib import Path

import console_ssaa4x_probe as probe


class SSAA4xProbeChecks(unittest.TestCase):
    def test_probe_budget_accepts_and_rejects(self):
        self.assertAlmostEqual(probe.budget_ratio(5.0, 20.0, "4x"), 4.0)
        with self.assertRaises(RuntimeError):
            probe.budget_ratio(1.0, 0.0, "4x")
        with self.assertRaises(RuntimeError):
            probe.budget_ratio(1.0, probe.PROBE_MAX_RATIO + 1.0, "4x")

    def test_profile_window_is_measured(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.csv"
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                writer.writerow(["frame", "total_ms"])
                for frame in range(probe.WARMUP_FRAMES + probe.SAMPLE_FRAMES):
                    writer.writerow([frame,
                        99.0 if frame < probe.WARMUP_FRAMES else 10.0])
            self.assertAlmostEqual(probe.profile_total(path), 10.0)

    def test_profile_rejects_empty_window(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.csv"
            path.write_text("frame,total_ms\n1,10\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                probe.profile_total(path)


if __name__ == "__main__":
    unittest.main()
