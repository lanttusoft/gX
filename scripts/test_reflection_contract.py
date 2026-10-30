# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: focused source drift checks; not compiler/GL verification.
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReflectionContractTests(unittest.TestCase):
    def test_reset_before_capture_gate(self):
        text = (ROOT / "src/renderer/gfx/draw.c").read_text().split("gX_I0 gX_RendererGFX_Draw_Reflections", 1)[1]
        gate = text.index("if (planarCaptureEnabled)")
        self.assertLess(text.index("gfx->reflection.filledThisFrame = false;"), gate)
        self.assertLess(text.index("gfx->reflection.cubeUpdatedThisFrame = false;"), gate)

    def test_ibl_resource_branches(self):
        text = (ROOT / "src/renderer/gfx/shader.c").read_text()
        self.assertIn("if (gfx->ibl.ready)", text)
        self.assertIn("gfx->reflection.dynamicCubemap.id > 0 && gfx->reflection.cubeUpdatedThisFrame", text)
        self.assertIn("if (!dynFresh && gfx->ibl.dummyCubemap.id > 0)", text)
        lean = (ROOT.parent / "proofs/GxProofs/ReflectFlags.lean").read_text()
        self.assertIn("else if dynamicExists && cubeFresh then .dynamic", lean)
        self.assertIn("else if dummyExists then .dummy else .unchanged", lean)

    def test_missing_depth_mask_legacy_contract(self):
        text = (ROOT / "resources/shader/gfx_ssr.frag").read_text()
        self.assertRegex(text, r"bool depthOk = true;\s*if \(hasPlanarDepth == 1\)\s*depthOk = texture\([^;]+< 0\.999;")
        lean = (ROOT.parent / "proofs/GxProofs/ReflectFlags.lean").read_text()
        self.assertIn("!maskAvailable || belowThreshold", lean)
        self.assertIn("missing_mask_counterexample", lean)

    def test_sky_unit_parking_matches_source(self):
        text = (ROOT / "src/renderer/gfx/constants.h").read_text()
        sky = int(re.search(r"#define gX_GFX_TEXUNIT_SKYBOX\s+(\d+)", text).group(1))
        csm = int(re.search(r"#define gX_GFX_TEXUNIT_CSM_ARRAY\s+(\d+)", text).group(1))
        lean = (ROOT.parent / "proofs/GxProofs/ReflectFlags.lean").read_text()
        self.assertIn(f"| .cube => {sky}", lean)
        self.assertIn(f"| .csm => if parked then {csm} else 0", lean)


if __name__ == "__main__":
    unittest.main()
