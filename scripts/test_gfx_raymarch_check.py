#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: negative tests pin the gate itself without launching the engine or GPU.
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

import gfx_raymarch_check as gate


class InspectorEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.pixels = np.zeros((80, 160, 3), dtype=np.float32)
        self.pixels[:, ::2] = 255
        self.lines = [f"GFX_RAYMARCH_ITEM: frame=20 name={name} visible=1 rect=8,8,140,24"
                      for name in ("selected", "type", "position", "scale")]

    def test_valid_visible_controls(self):
        self.assertEqual(len(gate.check_item_evidence("\n".join(self.lines), self.pixels)), 4)

    def test_panel_marker_alone_is_not_evidence(self):
        with self.assertRaisesRegex(RuntimeError, "no rendered-item evidence"):
            gate.check_item_evidence(gate.UI_MARKER, self.pixels)

    def test_offscreen_clipped_missing_and_empty_controls(self):
        mutations = (
            self.lines[:-1],
            self.lines[:-1] + [self.lines[-1].replace("visible=1", "visible=0")],
            self.lines[:-1] + [self.lines[-1].replace("8,8,140,24", "8,90,140,110")],
            self.lines[:-1] + [self.lines[-1].replace("8,8,140,24", "-2,8,140,24")],
        )
        for lines in mutations:
            with self.subTest(lines=lines), self.assertRaises(RuntimeError):
                gate.check_item_evidence("\n".join(lines), self.pixels)
        with self.assertRaisesRegex(RuntimeError, "looks empty"):
            gate.check_item_evidence("\n".join(self.lines), np.full_like(self.pixels, 128))

    def test_later_hidden_frame_cannot_reuse_earlier_pass(self):
        lines = self.lines + [line.replace("frame=20", "frame=21").replace("visible=1", "visible=0")
                              for line in self.lines]
        with self.assertRaisesRegex(RuntimeError, "not fully visible"):
            gate.check_item_evidence("\n".join(lines), self.pixels)


class ArtifactTests(unittest.TestCase):
    def test_failed_probe_retains_report(self):
        with tempfile.TemporaryDirectory(dir="/tmp/opencode") as temporary:
            directory = Path(temporary) / "retained"
            directory.mkdir()
            with patch.object(gate.tempfile, "mkdtemp", return_value=str(directory)):
                with self.assertRaisesRegex(RuntimeError, "injected fault"):
                    with gate.artifact_run():
                        gate.run_probe("fault", lambda: (_ for _ in ()).throw(RuntimeError("injected fault")))
            report = json.loads((directory / "report.json").read_text())
            self.assertEqual(report["verdict"], "FAIL")
            self.assertEqual(report["probes"][0]["verdict"], "FAIL")
            self.assertGreaterEqual(report["probes"][0]["seconds"], 0)


if __name__ == "__main__":
    unittest.main()
