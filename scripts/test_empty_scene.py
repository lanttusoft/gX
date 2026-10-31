#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: shipped empty scene must contain no models/lights/cameras and stay valid.
import json
from pathlib import Path
import unittest

import audit_common as audit


PRESET = audit.ROOT / "resources" / "tools" / "empty.json"


class EmptySceneTests(unittest.TestCase):
    def test_empty_preset_has_no_scene_content(self):
        document = json.loads(PRESET.read_text())
        self.assertEqual(document["models"], [])
        self.assertEqual(document["lights"], [])
        self.assertEqual(document["cameras"], [])
        self.assertEqual(document["textures"], [])
        camera = document["camera"]
        for key in ("position", "target", "up"):
            self.assertIn(key, camera)
            self.assertEqual(len(camera[key]), 3)
        self.assertGreater(camera["fovy"], 0.0)

    def test_empty_preset_disables_effects_and_keeps_sky(self):
        state = json.loads(PRESET.read_text())["state"]
        self.assertTrue(state["ENABLE_PROCEDURAL_SKY"])
        for key in ("ENABLE_SKYBOX", "ENABLE_FOG", "ENABLE_VOLUME_FOG",
                    "ENABLE_SSAO", "ENABLE_SSR", "ENABLE_REFLECTIONS",
                    "ENABLE_SHADOWS", "ENABLE_BLOOM", "ENABLE_TAA",
                    "ENABLE_FXAA", "ENABLE_GODRAYS", "ENABLE_PARTICLES",
                    "ENABLE_WATER", "ENABLE_DECALS", "ENABLE_MOTION_BLUR",
                    "ENABLE_DOF", "ENABLE_ENV_AMBIENT"):
            self.assertFalse(state[key], key)

    def test_preset_browser_contract(self):
        text = PRESET.read_text()
        self.assertIn('"state"', text)
        self.assertIn('"models"', text)


if __name__ == "__main__":
    unittest.main()
