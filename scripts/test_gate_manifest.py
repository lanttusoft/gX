#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: gate manifest contract tests — completeness, tiers, group inputs,
#   shared harness invalidation and unmapped build inputs.

import unittest
from pathlib import Path

import gate_manifest


class ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = gate_manifest.load()
        if cls.manifest is None:
            raise AssertionError("gates.toml is missing")

    def test_manifest_is_complete(self):
        gates = self.manifest["gates"]
        self.assertGreaterEqual(len(gates), 70)
        names = [gate["name"] for gate in gates]
        self.assertEqual(len(names), len(set(names)))
        for gate in gates:
            self.assertTrue((gate_manifest.ROOT / "scripts" /
                             gate["script"]).is_file(), gate["name"])
            self.assertIn(gate["group"], self.manifest["groups"])
            self.assertTrue(gate["tiers"])
            self.assertIn(gate["cache"], gate_manifest.CACHE_POLICIES)
        for group_name, group in self.manifest["groups"].items():
            for pattern in group["inputs"]:
                if pattern.endswith("/"):
                    self.assertTrue(
                        (gate_manifest.ROOT / pattern[:-1]).is_dir(),
                        f"{group_name}: {pattern}")
        for path in self.manifest["shared"]:
            self.assertTrue((gate_manifest.ROOT / path).is_file(), path)

    def test_tiers(self):
        quick = gate_manifest.select_tier(self.manifest, "quick")
        full = gate_manifest.select_tier(self.manifest, "full")
        compute = gate_manifest.select_tier(self.manifest, "compute")
        self.assertTrue(all("quick" in gate["tiers"] for gate in quick))
        self.assertTrue(all("quick" in gate["tiers"]
                            or "full" in gate["tiers"] for gate in full))
        self.assertTrue(all("compute" in gate["tiers"] for gate in compute))
        self.assertGreaterEqual(len(full), len(quick))

    def test_full_skips_superseded_quick(self):
        quick = {gate["name"] for gate in gate_manifest.select_tier(
            self.manifest, "quick")}
        full = {gate["name"] for gate in gate_manifest.select_tier(
            self.manifest, "full")}
        for name in ("matrix-quick", "gui-quick", "gui-layout-quick"):
            self.assertIn(name, quick)
            self.assertNotIn(name, full)
        self.assertIn("matrix", full)
        self.assertIn("gui-matrix", full)
        self.assertIn("gui-layout", full)
        tooling = {gate["name"] for gate in gate_manifest.select_tier(
            self.manifest, "tooling")}
        self.assertIn("cache", tooling)
        self.assertNotIn("cache", full)

    def test_path_matching(self):
        self.assertTrue(gate_manifest.path_matches("src/renderer/water.c",
                                                   "src/renderer/"))
        self.assertTrue(gate_manifest.path_matches("src/renderer/water.c",
                                                   "src/**/*.c"))
        self.assertTrue(gate_manifest.path_matches("scripts/gates.py",
                                                   "scripts/gates.py"))
        self.assertFalse(gate_manifest.path_matches("docs/README.md",
                                                    "src/renderer/"))

    def test_render_change_selects_render_gates(self):
        selected = gate_manifest.plan(
            self.manifest, "full", ["src/renderer/water.c"])
        names = {item["gate"] for item in selected}
        self.assertIn("water", names)
        self.assertNotIn("gui-layout-quick", names)

    def test_gui_change_selects_gui_gates(self):
        selected = gate_manifest.plan(
            self.manifest, "quick", ["src/gui/widget.c"])
        names = {item["gate"] for item in selected}
        self.assertIn("gui-layout-quick", names)
        self.assertNotIn("water", names)

    def test_shared_change_selects_all(self):
        selected = gate_manifest.plan(
            self.manifest, "quick", ["gates.toml"])
        names = {item["gate"] for item in selected}
        quick = {gate["name"] for gate in gate_manifest.select_tier(
            self.manifest, "quick")}
        self.assertEqual(names, quick)
        self.assertTrue(all("shared harness" in item["reason"]
                            for item in selected))

    def test_unmapped_build_input_selects_all(self):
        selected = gate_manifest.plan(
            self.manifest, "quick", ["CMakeLists.txt"])
        names = {item["gate"] for item in selected}
        quick = {gate["name"] for gate in gate_manifest.select_tier(
            self.manifest, "quick")}
        self.assertEqual(names, quick)
        self.assertTrue(all("unmapped build input" in item["reason"]
                            for item in selected))

    def test_docs_change_selects_none(self):
        self.assertEqual(gate_manifest.plan(
            self.manifest, "quick", ["docs/README.md"]), [])

    def test_explain(self):
        gate = gate_manifest.explain(self.manifest, "water")
        self.assertIsNotNone(gate)
        self.assertEqual(gate["command"][0], "python3")
        self.assertIn("scripts/gfx_water_check.py", gate["inputs"])

    def test_as_tuples(self):
        tuples = gate_manifest.as_tuples(self.manifest)
        self.assertEqual(len(tuples), len(self.manifest["gates"]))
        self.assertEqual(len(tuples[0]), 5)


if __name__ == "__main__":
    unittest.main()
