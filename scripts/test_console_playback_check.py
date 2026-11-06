#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for the playback trace and OSD state.
import unittest

import console_playback_check as playback


class PlaybackGateTests(unittest.TestCase):
    def setUp(self):
        self.log = "\n".join([
            "CONSOLE_PLAYBACK: frame=1 active=1 paused=0 ff=0 step=0 "
            "delta=0.016666667 sim=0.016666667",
            "CONSOLE_PLAYBACK: frame=2 active=1 paused=1 ff=0 step=0 "
            "delta=0.000000000 sim=0.016666667",
            "CONSOLE_PLAYBACK: frame=3 active=1 paused=1 ff=0 step=0 "
            "delta=0.016666667 sim=0.033333333",
            "CONSOLE_PLAYBACK: frame=4 active=1 paused=1 ff=0 step=0 "
            "delta=0.000000000 sim=0.033333333",
        ])

    def test_valid_step_trace(self):
        playback.require_step(self.log, 1)

    def test_step_count_and_delta_are_strict(self):
        with self.assertRaises(RuntimeError):
            playback.require_step(self.log, 2)
        with self.assertRaises(RuntimeError):
            playback.require_step(self.log.replace(
                "delta=0.016666667 sim=0.033333333",
                "delta=0.033333333 sim=0.050000000"), 1)
        with self.assertRaises(RuntimeError):
            playback.require_step("", 1)

    def test_fast_forward_multiplier_and_delta(self):
        log = "\n".join([
            "CONSOLE_PLAYBACK: frame=5 active=1 paused=0 ff=4 step=0 "
            "delta=0.066666667 sim=1.000000000",
            "CONSOLE_PLAYBACK: frame=6 active=1 paused=0 ff=4 step=0 "
            "delta=0.066666667 sim=1.066666667",
        ])
        playback.require_ff(log, 4)
        with self.assertRaises(RuntimeError):
            playback.require_ff(log, 2)
        with self.assertRaises(RuntimeError):
            playback.require_ff(log.replace("0.066666667", "0.033333333"), 4)

    def test_osd_state_is_required(self):
        osd = ("CONSOLE_OSD: show render=320x240 mode=SOFT cart=Y2K CGI slot=1 "
               "state=EMPTY play=PAUSED rect=10,435,200,35 textfit=185,13")
        playback.require_osd_play(osd, "PAUSED")
        with self.assertRaises(RuntimeError):
            playback.require_osd_play(osd, "STEP")
        with self.assertRaises(RuntimeError):
            playback.require_osd_play("", "PAUSED")


if __name__ == "__main__":
    unittest.main()
