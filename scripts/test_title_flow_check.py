#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: M5 — the plain-launch hint gate must verify drawn pixels, not just the
#   trace line. Synthetic frames cover absent draw calls and a panel-only draw.
import unittest

import numpy as np

import title_flow_check as title


class HintPixelTests(unittest.TestCase):
    def image(self):
        return np.full((768, 1024, 3), (200.0, 200.0, 255.0), dtype=np.float32)

    def test_missing_draw_calls_fail(self):
        with self.assertRaisesRegex(RuntimeError, "panel is not visible"):
            title.assert_hint_pixels(self.image())

    def test_dark_panel_without_glyphs_fails(self):
        image = self.image()
        image[12:44, 12:262] = (20.0, 20.0, 20.0)
        with self.assertRaisesRegex(RuntimeError, "text is not visible"):
            title.assert_hint_pixels(image)

    def test_dark_panel_with_glyphs_passes(self):
        image = self.image()
        image[12:44, 12:262] = (20.0, 20.0, 20.0)
        image[20:32, 20:120] = (235.0, 240.0, 255.0)
        self.assertTrue(title.assert_hint_pixels(image))


if __name__ == "__main__":
    unittest.main()
