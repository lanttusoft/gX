#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for the RAY-1 profile trace and save JSON.
import unittest

import console_ray1_check as ray1


class Ray1GateChecks(unittest.TestCase):
    def setUp(self):
        self.log = "\n".join([ray1.PROFILE_QUALITY, ray1.PROFILE_CAMERA])
        self.state = {
            "cartridge": "ray-1",
            "video": {"width": 320, "height": 240, "filter": 1},
            "camera3d": {
                "position": [6.0, 2.0, 14.0],
                "target": [0.0, -0.5, 6.0],
                "fovy": 55.0,
            },
        }

    def test_profile_requires_quality_and_camera(self):
        ray1.require_profile(self.log)
        for log in ("", self.log.replace(ray1.PROFILE_QUALITY, ""),
                    self.log.replace(ray1.PROFILE_CAMERA, "")):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                ray1.require_profile(log)

    def test_save_requires_ray1_video_and_camera(self):
        ray1.validate_save(self.state)
        for state in (dict(self.state, cartridge="y2k"),
                      dict(self.state, video={"width": 640, "height": 480,
                          "filter": 1}),
                      dict(self.state, video={"width": 320, "height": 240,
                          "filter": 0}),
                      dict(self.state, camera3d={"position": [0, 0, 0],
                          "target": [0, 0, 1], "fovy": 55.0})):
            with self.subTest(state=state), self.assertRaises(RuntimeError):
                ray1.validate_save(state)

    def test_load_requires_state_and_camera_traces(self):
        log = "\n".join([ray1.LOAD_CAMERA, ray1.RESTORE_CAMERA])
        ray1.require_load_camera(log)
        with self.assertRaises(RuntimeError):
            ray1.require_load_camera(ray1.LOAD_CAMERA)
        with self.assertRaises(RuntimeError):
            ray1.require_load_camera(ray1.RESTORE_CAMERA)


if __name__ == "__main__":
    unittest.main()
