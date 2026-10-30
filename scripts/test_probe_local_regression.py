#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GPU-free owned fixture, bounded footprint and omission-policy contracts.
import gzip
import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import numpy as np
import probe_local_regression as probe


class FixtureTests(unittest.TestCase):
    def test_owned_cube_winding_bounds_and_emission_precedence(self):
        with TemporaryDirectory() as directory:
            preset_path = probe.generate(directory)
            preset = json.loads(preset_path.read_text())
            slab, emitter = preset["models"]
            self.assertEqual(slab["params"]["reflectionPlaneHeight"], 0)
            self.assertEqual(slab["params"]["reflectionNormal"], [0, 1, 0])
            self.assertEqual(emitter["params"]["emissiveFactor"], 0)
            self.assertEqual(emitter["params"]["emissiveColor"], [0, 0, 0, 255])
            self.assertEqual(preset["state"]["exposure"], .8)
            data = Path(emitter["path"]).read_bytes()
            magic, version, length = struct.unpack_from("<III", data)
            self.assertEqual((magic, version, length), (0x46546C67, 2, len(data)))
            json_size, kind = struct.unpack_from("<II", data, 12)
            self.assertEqual(kind, 0x4E4F534A)
            document = json.loads(data[20:20 + json_size])
            binary = data[28 + json_size:]
            for face, primitive in enumerate(document["meshes"][0]["primitives"]):
                def read_attribute(name):
                    accessor = document["accessors"][primitive["attributes"][name]]
                    view = document["bufferViews"][accessor["bufferView"]]
                    return np.frombuffer(binary, dtype="<f4", count=accessor["count"] * 3,
                                         offset=view["byteOffset"]).reshape(-1, 3)
                points, normals = read_attribute("POSITION"), read_attribute("NORMAL")
                self.assertTrue((np.abs(points) == 1).all())
                for start in (0, 3):
                    np.testing.assert_array_equal(np.cross(points[start + 1] - points[start],
                        points[start + 2] - points[start]), normals[start] * 4)
                self.assertEqual(document["materials"][face]["emissiveFactor"], probe.PALETTE[face].tolist())
            before = preset_path.read_bytes()
            with self.assertRaises(ValueError):
                probe.generate(directory)
            self.assertEqual(preset_path.read_bytes(), before)


class PolicyTests(unittest.TestCase):
    def test_sky_missing_sky_and_geometry_uncertainty(self):
        self.assertEqual(probe.route(0, 0, False, True), (1, 0))
        self.assertEqual(probe.route(0, 0, True, False), (0, 1))
        self.assertEqual(probe.route(2, 0, True, True), (0, 3))

    def test_hidden_and_uncertain_local_hits_never_become_sky(self):
        for visibility in (0, 2, 3):
            self.assertEqual(probe.route(1, visibility, True, True), (0, 2))

    def test_visible_local_requires_filter_confidence(self):
        self.assertEqual(probe.route(1, 1, False, True), (0, 5))
        self.assertEqual(probe.route(1, 1, True, True), (2, 0))

    def test_omission_preserves_base_and_other_lighting(self):
        base = np.array([.125, .25, .5])
        for weight in (0, .04, .5, 1):
            np.testing.assert_array_equal(probe.material_add(base, [1, .25, 0], weight, False), base)
            np.testing.assert_array_equal(probe.material_add(base, [1, .25, 0], weight, True),
                                          base + weight * np.array([1, .25, 0]))
        np.testing.assert_array_equal(base, [.125, .25, .5])

    def test_invalid_codes_and_light_values_fail(self):
        for geometry, visibility in ((3, 0), (-1, 1), (1, 4)):
            with self.assertRaises(ValueError):
                probe.route(geometry, visibility, True, True)
        for base, local, weight in (([0, 0], [1, 1, 1], 1),
            ([np.nan, 0, 0], [1, 1, 1], 1), ([0, 0, 0], [1, 1, 1], -1)):
            with self.assertRaises(ValueError):
                probe.material_add(base, local, weight, True)


class FootprintTests(unittest.TestCase):
    def test_uniform_visible_face_at_all_supported_mips(self):
        for lod in probe.LODS:
            self.assertTrue(probe.filter_valid([0, 1, 0], [8, 1.5, -3], 3, lod))
            self.assertTrue(probe.filter_valid([0, 100, 0], [8, 1.5, -3], 3, lod))

    def test_wrong_face_hidden_face_boundary_and_seam_are_rejected(self):
        cases = (([0, 1, 0], [8, 1.5, -3], 2),
                 ([0, -1, 0], [8, 4.5, -3], 3),
                 ([1.2, .3, 0], [8, 1.5, -3], 3),
                 ([1, 1, 0], [8, 1.5, -3], 3),
                 ([0, 0, 0], [8, 1.5, -3], 3),
                 ([0, 1, 0], [8, 3, -3], 2))
        for lookup, origin, face in cases:
            self.assertFalse(probe.filter_valid(lookup, origin, face, 0))

    def test_unsupported_filters_and_nonfinite_inputs_fail(self):
        for lod in (1.5, 3, -1):
            with self.assertRaises(ValueError):
                probe.filter_valid([0, 1, 0], [8, 1.5, -3], 3, lod)
        with self.assertRaises(ValueError):
            probe.filter_valid([np.nan, 1, 0], [8, 1.5, -3], 3, 0)


class EvidenceTests(unittest.TestCase):
    def test_missing_experimental_contract_is_not_silently_skipped(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "explicit-binary"
            binary.write_bytes(b"owned test placeholder")
            shader = root / "profiles/omit_control/resources/shader/standard.frag"
            shader.parent.mkdir(parents=True)
            shader.write_text("#version 330\n")
            with patch("audit_common.run_scene") as run:
                with self.assertRaisesRegex(ValueError, "experimental material/filter contract missing"):
                    probe.capture(root / "output", binary, root / "profiles")
                run.assert_not_called()

    def write_hdr(self, path, image):
        path.parent.mkdir(parents=True, exist_ok=True)
        height, width, channels = image.shape
        path.write_bytes(b"GXHDR1\0\0" + struct.pack("<III", width, height, channels)
                         + np.asarray(image, dtype="<f4").tobytes())

    def test_hdr_archives_and_malformed_inputs(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "frame.hdr"
            image = np.zeros((2, 3, 4), dtype=np.float32)
            self.write_hdr(path, image)
            np.testing.assert_array_equal(probe.read_hdr(path), image)
            path.with_suffix(".hdr.gz").write_bytes(gzip.compress(path.read_bytes()))
            path.unlink()
            np.testing.assert_array_equal(probe.read_hdr(path), image)
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                probe.read_hdr(path)
            image[0, 0, 0] = np.inf
            self.write_hdr(path, image)
            with self.assertRaises(ValueError):
                probe.read_hdr(path)

    def test_material_change_or_wrong_sky_label_is_not_waived(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            base = np.full((1, 3, 4), .25, dtype=np.float32)
            codes = np.array([[[0, 5, 1, 7], [1, 0, 0, 7], [2, 0, 1, 7]]], dtype=np.float32)
            for label in probe.LABELS:
                self.write_hdr(root / f"captures/omit_control/{label}_m11.hdr", base)
                for lod in probe.LODS:
                    self.write_hdr(root / f"captures/lod{lod}/{label}_m11.hdr", base)
                    self.write_hdr(root / f"captures/lod{lod}/{label}_m12.hdr", codes)
            self.assertTrue(probe.analyze(root)["pass"])
            changed = base.copy()
            changed[0, 0, 0] += .125
            self.write_hdr(root / "captures/lod0/dynamic_m11.hdr", changed)
            self.assertFalse(probe.analyze(root)["pass"])
            self.write_hdr(root / "captures/lod0/dynamic_m11.hdr", base)
            codes[0, 0] = [1, 0, 1, 7]
            self.write_hdr(root / "captures/lod0/dynamic_m12.hdr", codes)
            self.assertFalse(probe.analyze(root)["pass"])


if __name__ == "__main__":
    unittest.main()
