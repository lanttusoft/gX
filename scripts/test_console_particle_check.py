#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free rejection controls for the particle profile trace and save JSON.
import unittest

import console_particle_check as particle


class ParticleGateChecks(unittest.TestCase):
    def setUp(self):
        self.log = "\n".join([particle.PROFILE_CAMERA,
            "PHYSICS_SCENE: 16 props / 0 ragdolls / 20 rig links planned"])
        self.state = {
            "cartridge": "particle",
            "video": {"width": 320, "height": 240, "filter": 1},
            "camera3d": {
                "position": [10.5, 6.0, 8.5],
                "target": [2.0, 1.0, -1.5],
                "fovy": 60.0,
            },
        }

    def test_profile_requires_camera_and_content(self):
        particle.require_profile(self.log)
        for log in ("", self.log.replace(particle.PROFILE_CAMERA, ""),
                    self.log.replace("planned", "")):
            with self.subTest(log=log), self.assertRaises(RuntimeError):
                particle.require_profile(log)

    def test_save_requires_particle_video_and_camera(self):
        particle.validate_save(self.state)
        for state in (dict(self.state, cartridge="ray-1"),
                      dict(self.state, video={"width": 640, "height": 480,
                          "filter": 1}),
                      dict(self.state, video={"width": 320, "height": 240,
                          "filter": 0}),
                      dict(self.state, camera3d={"position": [0, 0, 0],
                          "target": [0, 0, 1], "fovy": 60.0})):
            with self.subTest(state=state), self.assertRaises(RuntimeError):
                particle.validate_save(state)

    def test_load_requires_state_and_camera_traces(self):
        log = "\n".join([particle.LOAD_CAMERA, particle.RESTORE_CAMERA])
        particle.require_load_camera(log)
        with self.assertRaises(RuntimeError):
            particle.require_load_camera(particle.LOAD_CAMERA)
        with self.assertRaises(RuntimeError):
            particle.require_load_camera(particle.RESTORE_CAMERA)


if __name__ == "__main__":
    unittest.main()
