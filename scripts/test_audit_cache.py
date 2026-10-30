#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: focused regressions for the source-input audit cache (AUD-06).
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import audit_cache
import audit_common


class BuildInputDigestTests(unittest.TestCase):
    def make_root(self, directory):
        root = Path(directory)
        (root / "src").mkdir()
        (root / "src" / "main.c").write_text("int main(void) { return 0; }\n")
        (root / "resources").mkdir()
        (root / "resources" / "texture.bin").write_text("texture-v1\n")
        (root / "scripts").mkdir()
        (root / "scripts" / "audit_common.py").write_text("# helper v1\n")
        (root / "cmake").mkdir()
        (root / "cmake" / "toolchain.cmake").write_text("# toolchain v1\n")
        (root / "CMakeLists.txt").write_text("project(gx)\n")
        (root / "build.sh").write_text("#!/bin/sh\n")
        (root / "Makefile").write_text("all:\n")
        (root / ".gitmodules").write_text("[submodule \"external/raylib\"]\n")
        (root / "build").mkdir()
        (root / "build" / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\n"
            "CMAKE_C_FLAGS:STRING=-O0 -g\n"
            "CMAKE_C_COMPILER:FILEPATH=/usr/bin/cc\n")
        return root

    def digest(self, root, binary=None):
        return audit_cache.build_input_digest(root, binary=binary, use_cache=False)

    def test_unchanged_inputs_reuse_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            self.assertEqual(self.digest(root), self.digest(root))

    def test_source_change_invalidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            before = self.digest(root)
            (root / "src" / "main.c").write_text("int main(void) { return 1; }\n")
            self.assertNotEqual(before, self.digest(root))

    def test_resource_change_invalidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            before = self.digest(root)
            (root / "resources" / "texture.bin").write_text("texture-v2\n")
            self.assertNotEqual(before, self.digest(root))

    def test_build_profile_change_invalidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            before = self.digest(root)
            (root / "build" / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Release\n"
                "CMAKE_C_FLAGS:STRING=-O3\n"
                "CMAKE_C_COMPILER:FILEPATH=/usr/bin/cc\n")
            self.assertNotEqual(before, self.digest(root))

    def test_helper_change_invalidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            before = self.digest(root)
            (root / "scripts" / "audit_common.py").write_text("# helper v2\n")
            self.assertNotEqual(before, self.digest(root))

    def test_custom_binary_profile_invalidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_root(directory)
            build = root / "build" / "gl-level33"
            build.mkdir()
            binary = build / "gX_build"
            binary.write_text("binary\n")
            (build / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Release\n"
                "CMAKE_C_FLAGS:STRING=-O3\n")
            before = self.digest(root, binary=binary)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Debug\n"
                "CMAKE_C_FLAGS:STRING=-O0\n")
            self.assertNotEqual(before, self.digest(root, binary=binary))


class CapturePoolFingerprintTests(unittest.TestCase):
    def test_source_digest_drives_the_capture_fingerprint(self):
        with mock.patch.object(audit_cache, "build_input_digest",
                               return_value="inputs-a"):
            first = audit_common._fingerprint(
                "--gfx", {}, 1024, 768, 60, 10)
        with mock.patch.object(audit_cache, "build_input_digest",
                               return_value="inputs-b"):
            second = audit_common._fingerprint(
                "--gfx", {}, 1024, 768, 60, 10)
        self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()
