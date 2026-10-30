#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: fail on drift in source clauses abstracted by the Lean SSR coverage model.
# These checks pin selected expressions, not shader execution or visibility.
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHADER_CLAUSES = {
    "miss": "bool auxMiss = (hasPlanar == 1 && !isWater && firstMr.hitType != 1 && (firstMr.crossCount > 0 || firstMr.grazeCount > 0 || firstMr.phantomRejects > 0));",
    "wrong_side": "bool auxWrongSide = (hasPlanar == 1 && !isWater && firstMr.hitType == 1 && dnWrong > 0.05);",
    "seam": "bool auxSeam = (hasPlanar == 1 && !isWater && firstMr.hitType == 1 && originOnPlane && finalMargin < 0.35);",
    "route": "if (auxMiss || auxWrongSide || auxSeam)",
    "depth": "bool depthOk = true; if (hasPlanarDepth == 1) depthOk = texture(planarDepthMap, clamp(suvA, 0.0, 1.0)).r < 0.999; if (edgeFA > 0.001 && depthOk)",
    "origin_projection": "float dn = dot(wp, planeNormal) - planeDist; vec3 wm = wp - planeNormal * (2.0 * dn);",
    "near_projection": "vec2 suvA = (auxMiss && originOnPlane) ? suvN : suvO;",
}
C_CLAUSES = {
    "fresh_reset": "gfx->reflection.filledThisFrame = false; gfx->reflection.cubeUpdatedThisFrame = false;",
    "bind": "if (planarAuxOn && gfx->state.ENABLE_SSR_AUXVIEW && gfx->reflection.filledThisFrame && gfx->ssr.planarMapLoc >= 0)",
    "dynamic_env": "gX_I1 ssrHasEnv = (gfx->reflection.cubeUpdatedThisFrame && gfx->reflection.dynamicCubemap.id > 0);",
    "probe_env": "if (!ep->baked || ep->cube.id == 0) continue;",
}


#ai: comments are not executable correspondence evidence.
def canonical(source):
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    return re.sub(r"\s+", "", source)


def missing_clauses(source, clauses):
    normalized = canonical(source)
    return [name for name, clause in clauses.items()
            if canonical(clause) not in normalized]


class SsrCoverageContractTests(unittest.TestCase):
    def test_shader_gate_correspondence(self):
        source = (ROOT / "resources/shader/gfx_ssr.frag").read_text()
        self.assertEqual(missing_clauses(source, SHADER_CLAUSES), [])

    def test_binding_gate_correspondence(self):
        source = (ROOT / "src/renderer/gfx/draw.c").read_text()
        self.assertEqual(missing_clauses(source, C_CLAUSES), [])

    def test_every_selected_shader_clause_detects_drift(self):
        source = (ROOT / "resources/shader/gfx_ssr.frag").read_text()
        normalized = canonical(source)
        for name, clause in SHADER_CLAUSES.items():
            with self.subTest(clause=name):
                changed = normalized.replace(canonical(clause), "MUTATED", 1)
                self.assertIn(name, missing_clauses(changed, SHADER_CLAUSES))

    def test_every_selected_binding_clause_detects_drift(self):
        source = (ROOT / "src/renderer/gfx/draw.c").read_text()
        normalized = canonical(source)
        for name, clause in C_CLAUSES.items():
            with self.subTest(clause=name):
                changed = normalized.replace(canonical(clause), "MUTATED", 1)
                self.assertIn(name, missing_clauses(changed, C_CLAUSES))

    def test_comments_cannot_satisfy_gate(self):
        for name, clause in SHADER_CLAUSES.items():
            with self.subTest(clause=name):
                self.assertEqual(missing_clauses("/*" + clause + "*/", {name: clause}), [name])
                self.assertEqual(missing_clauses("//" + clause, {name: clause}), [name])


if __name__ == "__main__":
    unittest.main()
