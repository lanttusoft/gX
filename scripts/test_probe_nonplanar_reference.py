#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free regression for the committed non-planar reflection
#    reference. It pins the analytic oracle to the yaw-only fixture and the
#    unchanged 0.30/0.50 gates.
import hashlib
import json
import unittest
from pathlib import Path

import probe_nonplanar_verify as NP
import probe_reflect_verify as P


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "scripts" / "fixtures" / "reflection" / "nonplanar_y45.png"
METADATA = ROOT / "scripts" / "fixtures" / "reflection" / "nonplanar_y45.json"


class NonplanarReference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.meta = json.loads(METADATA.read_text())
        cls.digest = hashlib.sha256(FIXTURE.read_bytes()).hexdigest()

    def test_reference_image_is_unchanged(self):
        self.assertEqual(self.digest, self.meta["image"]["sha256"])

    def test_reference_passes_unchanged_gates(self):
        meta = self.meta["oracle"]
        report, _ = NP.verify(
            str(FIXTURE), meta["yaw"], meta["pitch"], meta["dist"],
            cube_rot=P.parse_cube_rot(meta["cube_rot"]))
        self.assertEqual(report["verdict"], "PASS")
        self.assertGreaterEqual(report["planar_zone_frac"],
                                meta["gate_planar"])
        self.assertGreaterEqual(report["cube_zone_frac"],
                                meta["gate_cube"])
        self.assertAlmostEqual(report["planar_zone_frac"],
                               meta["planar_zone_frac"], delta=0.03)
        self.assertAlmostEqual(report["cube_zone_frac"],
                               meta["cube_zone_frac"], delta=0.03)


if __name__ == "__main__":
    unittest.main()
