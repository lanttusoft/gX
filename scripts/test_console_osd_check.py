#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for OSD evidence and intentional fault probes.
import unittest

import console_osd_check as osd


class OSDGateTests(unittest.TestCase):
    def setUp(self):
        self.log = ("CONSOLE_OSD: show render=320x240 out=320x240 mode=SOFT "
            "cart=Y2K CGI slot=1 state=EMPTY play= rect=10,435,200,35 "
            "textfit=185,13")

    def test_valid_trace(self):
        osd.assert_osd(self.log, 640, 480, render=(320, 240), mode="SOFT", state="EMPTY")

    def test_missing_or_wrong_state_is_not_a_pass(self):
        for log in ("", self.log.replace("state=EMPTY", "state=READY"),
                    self.log + "\nCONSOLE_OSD: show render=320x240 mode=SOFT"):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                osd.assert_osd(log, 640, 480, state="EMPTY")
        with self.assertRaises(RuntimeError):
            osd.assert_osd(self.log, 640, 480, mode="NATIVE")

    def test_clip_offscreen_and_tiny_text_fail(self):
        for old, new in (("textfit=185,13", "textfit=210,13"),
                         ("textfit=185,13", "textfit=185,5"),
                         ("rect=10,435,200,35", "rect=10,470,200,35")):
            with self.subTest(new=new), self.assertRaises(RuntimeError):
                osd.assert_osd(self.log.replace(old, new), 640, 480)

    def test_ssaa_render_output_split_is_parsed(self):
        log = self.log.replace("render=320x240 out=320x240 mode=SOFT",
            "render=640x480 out=320x240 mode=SSAA2x")
        osd.assert_osd(log, 640, 480, render=(640, 480), out=(320, 240),
            mode="SSAA2x")

    def test_playback_label_is_parsed(self):
        log = self.log.replace("play= ", "play=PAUSED ")
        osd.assert_osd(log, 640, 480, play="PAUSED")

    def test_multiword_failure_and_selected_slot(self):
        log = self.log.replace("slot=1 state=EMPTY", "slot=3 state=LOAD FAILED")
        osd.assert_osd(log, 640, 480, slot=3, state="LOAD FAILED")

    def test_fault_allowlist_is_narrow_and_strict_by_default(self):
        failure = "[ ERROR ] CONSOLE_STATE: expected failure"
        with self.assertRaises(RuntimeError):
            osd.console.check_errors(failure)
        osd.console.check_errors(failure, (r"CONSOLE_STATE: expected failure$",))
        with self.assertRaises(RuntimeError):
            osd.console.check_errors(failure + "\n[ERROR] unrelated renderer failure",
                (r"CONSOLE_STATE: expected failure$",))


if __name__ == "__main__":
    unittest.main()
