#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: CPU checks pin oracle support, camera asymmetry and zero-coverage cases.
import unittest
import tempfile
import struct
from pathlib import Path

import numpy as np

from gfx_raymarch_depth_check import oracle, read_depth, msaa_metrics


class DepthOracleTests(unittest.TestCase):
    def test_visible_cases_are_nonempty_and_disjoint(self):
        for oblique in (False,True):
            for case in ("front","back","cross"):
                with self.subTest(case=case,oblique=oblique):
                    visible,hidden = oracle(640,480,case,oblique)
                    self.assertGreater(int(visible.sum()),100)
                    self.assertFalse(np.any(visible&hidden))

    def test_miss_has_no_visible_pixels(self):
        for oblique in (False,True):
            visible,hidden = oracle(640,480,"miss",oblique)
            self.assertFalse(visible.any())
            self.assertGreater(int(hidden.sum()),300000)

    def test_cameras_are_not_equivalent(self):
        axial,_ = oracle(640,480,"cross",False)
        oblique,_ = oracle(640,480,"cross",True)
        self.assertGreater(int((axial!=oblique).sum()),1000)

    def test_exported_depth_has_near_hits_and_clear_misses(self):
        _,_,depth,valid = oracle(640,480,"cross",False,depth=True)
        self.assertTrue(np.isfinite(depth).all())
        self.assertGreater(int(valid.sum()),250000)
        self.assertLess(float(depth[240,320]),1.)
        self.assertEqual(float(depth[0,0]),1.)

    def test_native_depth_orientation_and_truncation(self):
        with tempfile.TemporaryDirectory(dir="/tmp/opencode") as temporary:
            root = Path(temporary)
            path = root/"hybrid_depth_000020.gxrt"
            header = struct.pack("<8sIII",b"GXRTF32\0",2,2,1)
            payload = np.array([[.1,.2],[.3,.4]],dtype="<f4").tobytes()
            path.write_bytes(header+payload)
            np.testing.assert_allclose(read_depth(root),[[.3,.4],[.1,.2]])
            path.write_bytes(header+payload[:-1])
            with self.assertRaises(RuntimeError):
                read_depth(root)

    def test_msaa_preserves_sampled_raster_and_checks_sdf_analytically(self):
        interior,exterior,expected,_ = oracle(640,480,"cross",True,depth=True)
        baseline = np.zeros((480,640,3))
        image = baseline.copy()
        image[interior] = 255
        raster = expected.copy()
        raster[exterior] -= .00007
        depth = raster.copy()
        depth[interior] = expected[interior]
        report = msaa_metrics(image,baseline,depth,raster)
        self.assertEqual(report["sdf_depth_max_error"],0)
        self.assertEqual(report["raster_depth_preservation_max_error"],0)
        depth[interior] += .00001
        self.assertGreater(msaa_metrics(image,baseline,depth,raster)["sdf_depth_max_error"],2e-6)
        depth[exterior] += .00001
        self.assertGreater(msaa_metrics(image,baseline,depth,raster)["raster_depth_preservation_max_error"],2e-6)


if __name__ == "__main__":
    unittest.main()
