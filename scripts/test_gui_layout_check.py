#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: the 175% layout gate ignores only the debug HUD canvas; template text
#   clipping must still be reported.
import unittest

import gui_layout_check as layout


def line(canvas, text, truncated=0, overflow=1):
    return (
        f'GUITRACE: element canvas={canvas} element=1 name="unnamed" '
        f'text="{text}" rect=16,14,260,20 visible=1 disabled=0 clickable=0 '
        f'focusable=0 textfit=242,21 wraps=0 truncated={truncated} '
        f'overflow={overflow} shrink=1.00 desc=""\n')


class LayoutFindingsTests(unittest.TestCase):
    def test_debug_canvas_overflow_is_ignored(self):
        problems, allowed, _shrunk = layout.findings(
            line(6, "debug display"), "SETTINGS")
        self.assertEqual(problems, [])
        self.assertEqual(allowed, [])

    def test_template_overflow_is_reported(self):
        problems, _allowed, _shrunk = layout.findings(
            line(0, "SETTINGS"), "SETTINGS")
        self.assertEqual(len(problems), 1)
        self.assertTrue(problems[0]["overflow"])

    def test_template_truncation_is_reported(self):
        problems, _allowed, _shrunk = layout.findings(
            line(2, "OPEN GFX", truncated=1, overflow=0), "ESCAPEMENU")
        self.assertEqual(len(problems), 1)
        self.assertTrue(problems[0]["truncated"])


if __name__ == "__main__":
    unittest.main()
