#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I024 M11F-02/M11F-03 regression. Links the sanitized engine archive
#    and runs the generated-model owner plus allocation-failure paths under
#    ASan/LSan; the probe source is an owned fixture.
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
PROBE = ROOT / "scripts/fixtures/generated_model_lifecycle_probe.c"
BUILD = ROOT / "build/sanitize"


class GeneratedModelLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.engine = BUILD / "libgx_engine_core.a"
        cls.raylib = BUILD / "raylib_build/raylib/libraylib.a"
        cls.generated = BUILD / "generated"
        if not cls.engine.exists() or not cls.raylib.exists() or not cls.generated.exists():
            raise unittest.SkipTest("sanitized build missing; run make sanitize-check")

        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="generated_lifecycle_", dir=SCRATCH)
        cls.directory = Path(cls.temporary.name)
        cls.probe = cls.directory / "probe"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = [
            "-DGAME_BACKEND_GX", "-DGAME_BACKEND_RAYLIB",
            "-DGX_GPU_SKINNING=1", "-DGX_USE_ODE",
            "-DgX_ENABLE_RENDERER=1", "-DgX_ENABLE_SCENE=1",
            "-std=gnu11", "-g", "-O1", "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined", "-ffunction-sections",
            "-fdata-sections",
        ]
        for relative in (".", "src", "src/backend/raylib", "external",
                         "external/raylib/src", "external/raylib/src/external",
                         "build/sanitize/generated"):
            flags += ["-I" + str(ROOT / relative)]
        object_path = cls.directory / "probe.o"
        result = subprocess.run(compiler + flags + ["-c", str(PROBE), "-o", str(object_path)],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        libraries = [
            str(object_path), str(cls.engine), str(cls.raylib),
            "-fsanitize=address,undefined", "-Wl,--gc-sections",
            "-Wl,--wrap=malloc", "-lm", "-lpthread", "-lX11", "-lXrandr",
            "-lXinerama", "-lXcursor", "-lXi", "-lXxf86vm", "-lGL",
            "-ldl", "-lode", "-o", str(cls.probe),
        ]
        result = subprocess.run(compiler + libraries, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_mode(self, mode, expect_failure_slot=False):
        environment = dict(os.environ)
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(self.probe), mode], env=environment,
                                capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("LIFECYCLE_COMPLETED", output)
        self.assertNotIn("LeakSanitizer", output)
        self.assertNotIn("ERROR: AddressSanitizer", output)
        self.assertNotIn("runtime error:", output)
        if expect_failure_slot:
            self.assertIn("used_slots_after_failure=0", output)
        return output

    def test_normal_owner_control_is_clean(self):
        self.run_mode("normal-owner-control")

    def test_generated_owner_frees_material_maps(self):
        self.run_mode("generated-owner")

    def test_first_allocation_failure_rolls_back_slot(self):
        self.run_mode("fail1", expect_failure_slot=True)

    def test_second_allocation_failure_rolls_back_slot(self):
        self.run_mode("fail2", expect_failure_slot=True)

    def test_material_index_failure_frees_maps_and_slot(self):
        self.run_mode("fail3", expect_failure_slot=True)


if __name__ == "__main__":
    unittest.main()
