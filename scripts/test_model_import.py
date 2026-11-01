#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: unit and owned-fixture coverage for the offline model importer.

import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

import model_import


FIXTURE = Path(__file__).resolve().parent / "fixtures" / "model_import"


def glb_bytes(document, binary=b""):
    encoded = json.dumps(document).encode("utf-8")
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    binary += b"\0" * ((4 - len(binary) % 4) % 4) if binary else b""
    total = 12 + 8 + len(encoded)
    if binary:
        total += 8 + len(binary)
    data = struct.pack("<III", model_import.GLB_MAGIC, 2, total)
    data += struct.pack("<II", len(encoded), model_import.GLB_JSON) + encoded
    if binary:
        data += struct.pack("<II", len(binary), model_import.GLB_BIN) + binary
    return data


class UriTests(unittest.TestCase):
    def test_safe_relative(self):
        self.assertEqual(model_import.safe_relative_uri("Textures/a.png"),
                         Path("Textures/a.png"))
        self.assertEqual(model_import.safe_relative_uri("a%20b.png"),
                         Path("a b.png"))
        self.assertEqual(model_import.safe_relative_uri("Textures\\a.png"),
                         Path("Textures/a.png"))

    def test_unsafe_relative(self):
        self.assertIsNone(model_import.safe_relative_uri("../a.png"))
        self.assertIsNone(model_import.safe_relative_uri("/tmp/a.png"))
        self.assertIsNone(model_import.safe_relative_uri("https://x/a.png"))
        self.assertIsNone(model_import.safe_relative_uri("C:/a.png"))


class GlbTests(unittest.TestCase):
    def write(self, directory, document, binary=b""):
        path = Path(directory) / "test.glb"
        path.write_bytes(glb_bytes(document, binary))
        return path

    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, {"asset": {"version": "2.0"},
                                          "meshes": [{"primitives": [{}]}]},
                              b"abcd")
            document, binary_bytes = model_import.read_glb(path)
            self.assertEqual(document["asset"]["version"], "2.0")
            self.assertEqual(binary_bytes, 4)

    def test_rejects_bad_magic(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.glb"
            path.write_bytes(b"not-a-glb")
            with self.assertRaises(ValueError):
                model_import.read_glb(path)


class TextureTests(unittest.TestCase):
    def test_copy_and_conflict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            output = root / "output"
            source.mkdir()
            (source / "tex.png").write_bytes(b"source")
            document = {"images": [{"uri": "tex.png"}]}
            result = model_import.copy_textures(document, source, output)
            self.assertEqual(result["copied"], ["tex.png"])
            self.assertEqual((output / "tex.png").read_bytes(), b"source")
            (output / "tex.png").write_bytes(b"other")
            result = model_import.copy_textures(document, source, output)
            self.assertEqual(result["conflicts"], ["tex.png"])
            result = model_import.copy_textures(document, source, output,
                                                force=True)
            self.assertEqual(result["copied"], ["tex.png"])
            self.assertEqual((output / "tex.png").read_bytes(), b"source")

    def test_missing_and_unsafe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            document = {"images": [{"uri": "missing.png"},
                                   {"uri": "../escape.png"}]}
            result = model_import.copy_textures(document, source, root / "out")
            self.assertEqual(result["missing"], ["missing.png"])
            self.assertEqual(result["unsafe"], ["../escape.png"])

    def test_same_directory_is_present(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tex.png").write_bytes(b"x")
            result = model_import.copy_textures(
                {"images": [{"uri": "tex.png"}]}, root, root)
            self.assertEqual(result["present"], ["tex.png"])


class SummaryTests(unittest.TestCase):
    def test_bone_cap_warning(self):
        document = {
            "asset": {"version": "2.0"},
            "meshes": [{"primitives": [{"attributes": {}}]}],
            "skins": [{"joints": list(range(model_import.skin_bone_cap() + 1))}],
        }
        summary = model_import.summarize(document, model_import.skin_bone_cap())
        self.assertEqual(summary["joints"], model_import.skin_bone_cap() + 1)
        self.assertTrue(any("joints" in warning
                            for warning in summary["warnings"]))

    def test_empty_mesh_is_rejected(self):
        with self.assertRaises(ValueError):
            model_import.summarize({"asset": {"version": "2.0"},
                                    "meshes": []},
                                   model_import.skin_bone_cap())


class ImportTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("assimp"), "assimp not installed")
    def test_owned_fixture_converts(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "quad.glb"
            result = model_import.import_model(
                FIXTURE / "quad.dae", output, shutil.which("assimp"),
                force=True)
            self.assertTrue(output.is_file())
            self.assertTrue((Path(directory) / "quad.png").is_file())
            self.assertEqual(result["meshes"], 1)
            self.assertEqual(result["primitives"], 1)
            self.assertEqual(result["images"], 1)
            self.assertEqual(len(result["textures"]["copied"]), 1)
            document, _ = model_import.read_glb(output)
            self.assertEqual(len(document["meshes"]), 1)

    @unittest.skipUnless(shutil.which("assimp"), "assimp not installed")
    def test_existing_output_requires_force(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "quad.glb"
            model_import.import_model(
                FIXTURE / "quad.dae", output, shutil.which("assimp"))
            with self.assertRaises(FileExistsError):
                model_import.import_model(
                    FIXTURE / "quad.dae", output, shutil.which("assimp"))
            model_import.import_model(
                FIXTURE / "quad.dae", output, shutil.which("assimp"),
                force=True)

    def test_rejects_non_dae(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "model.glb"
            source.write_bytes(b"x")
            with self.assertRaises(ValueError):
                model_import.import_model(
                    source, Path(directory) / "out.glb", "assimp")

    @unittest.skipUnless(shutil.which("assimp"), "assimp not installed")
    def test_missing_texture_warns_and_strict_rejects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "missing.dae"
            source.write_text((FIXTURE / "quad.dae").read_text().replace(
                "quad.png", "missing.png"))
            output = root / "missing.glb"
            result = model_import.import_model(
                source, output, shutil.which("assimp"))
            self.assertEqual(result["textures"]["missing"], ["missing.png"])
            self.assertTrue(result["warnings"])
            with self.assertRaises(RuntimeError):
                model_import.import_model(
                    source, output, shutil.which("assimp"), force=True,
                    strict=True)

    @unittest.skipUnless(shutil.which("assimp"), "assimp not installed")
    def test_no_textures_skips_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "quad.glb"
            model_import.import_model(
                FIXTURE / "quad.dae", output, shutil.which("assimp"),
                force=True, textures=False)
            self.assertFalse((Path(directory) / "quad.png").exists())

    @unittest.skipUnless(shutil.which("assimp"), "assimp not installed")
    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "quad.glb"
            result = model_import.import_model(
                FIXTURE / "quad.dae", output, shutil.which("assimp"),
                dry_run=True)
            self.assertEqual(result["command"][0], shutil.which("assimp"))
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
