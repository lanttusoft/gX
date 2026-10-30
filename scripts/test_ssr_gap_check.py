#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: self-contained CPU/format/control negatives for the owned SSR coverage fixture.
import json
from pathlib import Path
import re
import struct
import tempfile
import unittest

import numpy as np

import ssr_gap_check as gap


class OwnedGapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.open = gap.oracle(False)
        cls.blocked = gap.oracle(True)

    def test_source_fully_offscreen_but_reflected(self):
        self.assertEqual(self.open["primary_source_pixels"], 0)
        self.assertEqual(self.blocked["primary_source_pixels"], 0)
        self.assertGreater(self.open["interior_red"].sum(), 1000)
        self.assertGreater(self.blocked["interior_red"].sum(), 1000)
        self.assertEqual(self.open["green"].sum(), 0)

    def test_blocker_removes_red_without_camera_motion(self):
        self.assertTrue(np.any(self.open["red"] & self.blocked["green"]))
        self.assertTrue(np.all(~self.blocked["red"] | self.open["red"]))
        self.assertLess(self.blocked["red"].sum(), self.open["red"].sum())
        np.testing.assert_array_equal(self.open["ground"], self.blocked["ground"])

    def test_slab_parallel_and_zero_ray(self):
        lower, upper = np.array([-1., -1., -1.]), np.array([1., 1., 1.])
        direction = np.array([[[0., 0., 1.]]])
        self.assertEqual(gap.box_time(np.array([0., 0., -3.]), direction, lower, upper).item(), 2)
        self.assertTrue(np.isinf(gap.box_time(np.array([2., 0., -3.]), direction, lower, upper).item()))
        self.assertTrue(np.isinf(gap.box_time(np.zeros(3), np.zeros_like(direction), lower, upper).item()))

    def test_mesh_winding_and_bounds(self):
        vertices, normals = gap.box_mesh(gap.SOURCE_LO, gap.SOURCE_HI)
        vertices = np.array(vertices)
        np.testing.assert_allclose(vertices.min(axis=0), gap.SOURCE_LO)
        np.testing.assert_allclose(vertices.max(axis=0), gap.SOURCE_HI)
        for index in range(0, len(vertices), 3):
            a, b, c = vertices[index:index + 3]
            self.assertGreater(np.dot(np.cross(b - a, c - a), normals[index]), 0)

    def test_owned_glb_is_complete_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "owned.glb"
            gap.write_glb(path, *gap.box_mesh(gap.SOURCE_LO, gap.SOURCE_HI), [1, 0, 0])
            data = path.read_bytes()
            magic, version, length = struct.unpack_from("<III", data)
            self.assertEqual((magic, version, length), (0x46546c67, 2, len(data)))
            text_size, text_tag = struct.unpack_from("<II", data, 12)
            self.assertEqual(text_tag, 0x4e4f534a)
            document = json.loads(data[20:20 + text_size])
            binary_size, binary_tag = struct.unpack_from("<II", data, 20 + text_size)
            self.assertEqual(binary_tag, 0x004e4942)
            self.assertEqual(binary_size, document["buffers"][0]["byteLength"])
            self.assertEqual(document["materials"][0]["emissiveFactor"], [1, 0, 0])
            self.assertNotIn("uri", document["buffers"][0])
            with self.assertRaises(FileExistsError):
                gap.write_glb(path, *gap.box_mesh(gap.SOURCE_LO, gap.SOURCE_HI), [1, 0, 0])

    def test_independent_source_controls_and_supported_schema(self):
        for mode in gap.MODES:
            preset = gap.make_preset(Path("/owned"), mode, True)
            self.assertEqual(len(preset["models"]), 3)
            self.assertEqual(preset["camera"], gap.CAMERA)
            self.assertFalse(preset["state"]["ENABLE_SSR_AUXVIEW"])
            self.assertFalse(preset["bloom"]["autoExposure"])
            self.assertEqual(preset["ssr"]["maxBounces"], 1)
            self.assertNotIn("multiBounce", preset["ssr"])
            self.assertNotIn("temporalEnabled", preset["ssr"])
            self.assertTrue(preset["models"][0]["params"]["usePBR"])
            for model in preset["models"]:
                self.assertNotIn("reflectionBias", model["params"])
            self.assertEqual(preset["state"]["ENABLE_SSR"], mode.startswith("ssr"))
            self.assertEqual(preset["state"]["ENABLE_PLANAR_REFLECTIONS"], mode == "planar")
        with self.assertRaises(ValueError):
            gap.make_preset(Path("/owned"), "unowned", False)

    def test_every_fixture_field_belongs_to_its_schema_object(self):
        source = (gap.audit.ROOT / "src/renderer/gfx/schema.c").read_text()
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
        for mode in gap.MODES:
            preset = gap.make_preset(Path("/owned"), mode, True)
            sections = {key: preset[key] for key in (
                "state", "camera", "ssr", "reflection", "bloom", "sky", "sun")}
            sections["model_params"] = preset["models"][0]["params"]
            for key, values in sections.items():
                array = re.search(r"static gX_SchemaField gfx_" + key
                    + r"_fields\[\] = \{(.*?)\n\};", source, re.S)
                self.assertIsNotNone(array, key)
                known = set(re.findall(r'GX_SCHEMA_\w+\("([^"]+)"', array[1]))
                self.assertEqual(set(values) - known, set(), (mode, key))

    def synthetic(self, prediction):
        image = np.zeros((gap.HEIGHT, gap.WIDTH, 4), dtype=np.float32)
        image[prediction["red"], 0] = 1
        image[prediction["green"], 1] = 1
        image[..., 3] = 1
        return image

    def test_planar_correct_masks_and_occlusion_mutation(self):
        image = self.synthetic(self.blocked)
        _, checks = gap.controls("planar", image, self.blocked)
        self.assertTrue(all(checks.values()))
        image[self.blocked["interior_green"], :3] = [1, 0, 0]
        _, checks = gap.controls("planar", image, self.blocked)
        self.assertFalse(checks["planar_occlusion"])
        self.assertFalse(checks["planar_no_invalid_red"])

    def test_zero_diagnostic_is_not_coverage_evidence(self):
        image = np.zeros((gap.HEIGHT, gap.WIDTH, 4), dtype=np.float32)
        _, checks = gap.controls("ssr_classes", image, self.open)
        self.assertFalse(checks["diagnostic_pass_executed"])
        image[self.open["interior_red"]] = [0, 1, 0, 1]
        _, checks = gap.controls("ssr_classes", image, self.open)
        self.assertTrue(all(checks.values()))
        image[self.open["interior_red"]] = [1, 0, 0, 1]
        _, checks = gap.controls("ssr_classes", image, self.open)
        self.assertFalse(checks["offscreen_source_region_has_no_march_geometry_hit"])

    def test_cube_metrics_do_not_certify_visibility(self):
        image = self.synthetic(self.open)
        metrics, checks = gap.controls("ssr_env", image, self.blocked)
        self.assertGreater(metrics["red_on_occluded_interior_pixels"], 1000)
        self.assertEqual(checks, {})

    def test_weak_direct_cube_signal_is_not_hidden_by_neutral_base(self):
        base = np.full((gap.HEIGHT, gap.WIDTH, 4), .3, dtype=np.float32)
        image = base.copy()
        image[self.open["interior_red"], 0] += .03
        self.assertEqual(gap.measure(image, self.open)["red_interior_recall"], 0)
        delta = gap.material_delta(image, base, self.open)
        self.assertEqual(delta["positive_rgb_metrics"]["red_interior_recall"], 1)
        self.assertFalse(delta["visibility_certified"])
        with self.assertRaises(ValueError):
            gap.material_delta(image[:-1], base, self.open)

    def test_depth_control_rejects_wrong_projection_and_zero_buffer(self):
        distance = self.open["view_distance"]
        image = (1000 / 999.9 - 100 / (999.9 * distance))[..., None]
        log = "ORACLEDUMP f=35 camera frozen near=0.1 far=1000 size=800x600"
        self.assertTrue(gap.depth_control(image, self.open, log)["pass"])
        self.assertFalse(gap.depth_control(np.zeros_like(image), self.open, log)["pass"])
        with self.assertRaises(ValueError):
            gap.depth_control(image, self.open, log.replace("near=0.1", "near=0.2"))
        with self.assertRaises(ValueError):
            gap.depth_control(image, self.open, "")

    def test_hdr_rejects_bad_and_nonfinite_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.hdr"
            header = b"GXHDR1\0\0" + struct.pack("<III", gap.WIDTH, gap.HEIGHT, 4)
            image = np.zeros((gap.HEIGHT, gap.WIDTH, 4), dtype="<f4")
            image[0, 0] = [1, 2, 3, 4]
            path.write_bytes(header + image.tobytes())
            np.testing.assert_array_equal(gap.read_hdr(path)[-1, 0], [1, 2, 3, 4])
            image[0, 0, 0] = np.nan
            path.write_bytes(header + image.tobytes())
            with self.assertRaises(ValueError):
                gap.read_hdr(path)
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                gap.read_hdr(path)
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                gap.read_hdr(path)


if __name__ == "__main__":
    unittest.main()
