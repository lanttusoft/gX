# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: readback interval and direct GPU control-reader tests, independent of native launches.
from pathlib import Path
import struct
import tempfile
import unittest
import json
import gzip

import numpy as np
import probe_proxy_gpu_check as check


class ProxyGPUReader(unittest.TestCase):
    def test_half_bounds_enclose_neighboring_representable_values(self):
        value = np.array([1., 0., -.5])
        lower, upper = check.half_bounds(value)
        self.assertTrue((lower < value).all() and (upper > value).all())
        with self.assertRaisesRegex(ValueError, "half-representable"):
            check.half_bounds(np.array([1.00001]))

    def test_normalized_bounds_include_unit_axis(self):
        lower, upper, valid = check.normalized_bounds(np.array([[.9, -.01, -.01]]),
                                                     np.array([[1.1, .01, .01]]))
        self.assertTrue(valid[0])
        self.assertTrue(check.overlapping(lower, upper, np.array([[1, 0, 0]]), np.array([[1, 0, 0]]))[0])

    def test_geometry_intervals_hit_miss_and_uncertain(self):
        points = np.array([[8., 0., -3.], [10., 0., -3.], [8., 0., -3.]])
        rays = np.array([[.01, 1., .02], [.01, 1., .02], [2 / 3, 1., .1]])
        status, _, _, _, _ = check.geometry_bounds(points - 1e-5, points + 1e-5,
                                                    rays - 1e-5, rays + 1e-5)
        np.testing.assert_equal(status, [1, 0, 2])

    def test_hdr_reader_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.hdr"
            image = np.zeros((1, 1, 4), dtype="<f4")
            header = b"GXHDR1\0\0" + struct.pack("<III", 1, 1, 4)
            path.write_bytes(header + image.tobytes())
            np.testing.assert_equal(check.read_hdr(path), image)
            archive = path.with_suffix(".hdr.gz")
            archive.write_bytes(gzip.compress(header + image.tobytes()))
            path.unlink()
            np.testing.assert_equal(check.read_hdr(path), image)
            np.testing.assert_equal(check.read_hdr(archive), image)
            path.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "payload"):
                check.read_hdr(path)
            image[0, 0, 0] = np.nan
            path.write_bytes(header + image.tobytes())
            with self.assertRaisesRegex(ValueError, "nonfinite"):
                check.read_hdr(path)

    def test_direct_case_reader_detects_corrupted_status(self):
        image = np.zeros((1, 9, 4), dtype="<f4")
        image[0, :, 0] = [1, 0, 0, 1, 2, 0, 2, 2, 1]
        image[0, :, 2] = np.array([1.8, 0, 0, 1.2, 0, 0, 0, 0, .9], dtype=np.float16)
        image[0, :, 3] = 7
        self.assertTrue(check.selftest_report(image)["pass"])
        image[0, 4, 0] = 0
        self.assertFalse(check.selftest_report(image)["pass"])

    def test_activation_and_diagnostic_source_contract(self):
        root = Path(__file__).resolve().parents[1]
        model = (root / "src/renderer/gfx/model.c").read_text()
        shader = (root / "resources/shader/standard.frag").read_text()
        self.assertIn('#if defined(GX_PROBE_COVERAGE_EXPERIMENT)\nstatic gX_I0 gfx_probe_proxy_upload', model)
        self.assertIn('getenv("GX_PROBE_PROXY_FIXTURE")', model)
        self.assertIn("lookup = fs_in.fragPosition + distance * outgoing - cubePos;", shader)
        self.assertIn("all(equal(direction, vec3(0.0)))) return 2;", shader)
        self.assertIn("finalColor = gX_Proxy_Diagnostic(calc_ssr_normal(texcoord));", shader)

    def test_fixture_assertion_rejects_unrepresented_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.json"
            fixture = {"models": [{"path": "resources/model/primitive/cube.glb", "params": {
                "pos": position, "scale": scale, "rotation": [0, 0, 0],
                "enableReflections": reflective, "planarReflection": False}}
                for position, scale, reflective in (([0, 0, 0], [8, .1, 8], True), ([8, 3, -3], [1.2] * 3, False))],
                "camera": {"position": [4, 4, 6], "target": [5.8, .1, .6]},
                "state": {"ENABLE_REFLECTIONS": True, "ENABLE_MAP_NORMAL": False, "ENABLE_SSR": False}}
            path.write_text(json.dumps(fixture))
            check.validate_fixture(path)
            fixture["models"].append(fixture["models"][1])
            path.write_text(json.dumps(fixture))
            with self.assertRaisesRegex(ValueError, "only two"):
                check.validate_fixture(path)
            fixture["models"].pop()
            path.write_text(json.dumps(fixture))
            existing = Path(directory) / "existing"
            existing.mkdir()
            (existing / "dynamic_m1.log").write_text("preserve existing log")
            with self.assertRaisesRegex(ValueError, "already contains"):
                check.capture_cases(existing, path, Path(directory) / "unused_binary")

    def test_sample_availability_is_not_hit_certification(self):
        report = check.color_availability(np.array([0., 2., 0., 2., 2.]),
                                          np.array([0, 0, 1, 1, 1]), np.array([0, 0, 1, 1, 2]))
        self.assertEqual(report["sky_nonzero_distance_response"], 1)
        self.assertEqual(report["visible_local_zero_distance_response"], 1)
        self.assertEqual(report["hidden_local_nonzero_distance_response"], 1)


if __name__ == "__main__":
    unittest.main()
