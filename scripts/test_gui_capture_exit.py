#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: H3 — screenshot captures must reject a failing engine exit even when the
#   PNG exists. Fault injection returns 134 after writing a valid capture.
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image

import audit_common as audit
import gui_cursor_light_check as cursor_light
import gui_depth_check as depth
import gui_effect_contrast_check as effect_contrast
import gui_glass_check as glass
import gui_hud_contrast_check as hud
import gui_pause_blur_check as pause
import gui_shadow_check as shadow


class CaptureExitTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.theme = self.root / "theme.json"
        self.theme.write_text("{}", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def capture(self, function, directory, *args, size=(8, 8)):
        directory.mkdir(parents=True, exist_ok=True)
        Image.new("RGB", size, (0, 0, 0)).save(directory / "gXss_0001.png")
        with mock.patch.object(audit.subprocess, "run") as run:
            run.return_value = mock.Mock(returncode=134,
                                         stdout="injected engine crash")
            with self.assertRaisesRegex(RuntimeError, "134"):
                function(*args)

    def test_pause_blur_rejects_failed_exit(self):
        directory = self.root / "pause"
        self.capture(pause.capture, directory, True, directory)

    def test_glass_rejects_failed_exit(self):
        directory = self.root / "glass"
        self.capture(glass.capture, directory, self.theme, directory)

    def test_shadow_rejects_failed_exit(self):
        directory = self.root / "shadow"
        self.capture(shadow.capture, directory, "Canvas", {}, directory)

    def test_cursor_light_rejects_failed_exit(self):
        directory = self.root / "cursor"
        self.capture(cursor_light.capture, directory, self.theme,
                     (512, 259), directory)
        glint = self.root / "glint"
        self.capture(cursor_light.capture_glint, glint, self.theme, glint,
                     True)

    def test_effect_contrast_rejects_failed_exit(self):
        directory = self.root / "effect"
        self.capture(effect_contrast.capture, directory, self.theme,
                     (512, 259), directory)

    def test_hud_contrast_rejects_failed_exit(self):
        directory = self.root / "hud"
        scene = next(iter(hud.SCENES))
        self.capture(hud.capture_scene, directory, scene, directory,
                     size=(hud.WIDTH, hud.HEIGHT))

    def test_depth_captures_reject_failed_exit(self):
        directory = self.root / "depth"
        self.capture(depth.capture, directory, self.theme, directory)
        directory = self.root / "depth-log"
        self.capture(depth.capture_log, directory, self.theme, directory)


if __name__ == "__main__":
    unittest.main()
