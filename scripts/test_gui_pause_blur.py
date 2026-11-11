#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: M6 — the pause-panel opacity row must reject a lowered theme alpha and a
#   rendered fill that does not match the declared alpha.
import unittest
from unittest import mock

import numpy as np

import gui_pause_blur_check as gate


class PanelOpacityTests(unittest.TestCase):
    PANEL = [20, 15, 35, 245]
    OVERLAY = [40, 30, 60, 80]

    def rows(self, theme_alpha, rendered_alpha):
        scene = np.full((768, 1024, 3), 200.0, dtype=np.float64)
        backdrop = gate.dim_overlay(gate.blur_scene(scene), self.OVERLAY)
        menu = backdrop.copy()
        height, width = scene.shape[:2]
        x0, y0, x1, y1 = (int(gate.PANEL_PATCH[0] * width),
                          int(gate.PANEL_PATCH[1] * height),
                          int(gate.PANEL_PATCH[2] * width),
                          int(gate.PANEL_PATCH[3] * height))
        mix = rendered_alpha / 255.0
        menu[y0:y1, x0:x1] = backdrop[y0:y1, x0:x1] * (1.0 - mix) \
            + np.array(self.PANEL[:3], dtype=np.float64) * mix
        rows = gate.Rows(None)
        rows.images["scene"] = scene
        rows.images["menu"] = menu
        escape = {"panelBg": [self.PANEL[0], self.PANEL[1], self.PANEL[2],
                              theme_alpha],
                  "dimOverlay": self.OVERLAY}
        with mock.patch.object(gate, "load_escape", return_value=escape):
            return gate.row_panel_opacity(rows)

    def test_shipped_alpha_matches_rendered_fill(self):
        ok, note = self.rows(245, 245)
        self.assertTrue(ok, note)

    def test_lowered_theme_alpha_fails(self):
        ok, note = self.rows(200, 245)
        self.assertFalse(ok)
        self.assertIn("alpha 200", note)

    def test_rendered_fill_mismatch_fails(self):
        ok, note = self.rows(245, 200)
        self.assertFalse(ok)
        self.assertIn("panel fill", note)


if __name__ == "__main__":
    unittest.main()
