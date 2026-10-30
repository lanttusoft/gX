#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: bounded actual-TU float witnesses, separate from the exact-real Lean contracts.
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 2e-4


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def lerp(q1, q2, amount):
    return tuple(a + amount * (b - a) for a, b in zip(q1, q2))


def normalize(q):
    length = math.sqrt(sum(component * component for component in q))
    if length == 0.0:
        length = 1.0
    return tuple(component / length for component in q)


def nlerp(q1, q2, amount):
    return normalize(lerp(q1, q2, amount))


def slerp_model(q1, q2, amount):
    cosine = sum(a * b for a, b in zip(q1, q2))
    if cosine < 0.0:
        q2 = tuple(-component for component in q2)
        cosine = -cosine
    if abs(cosine) >= 1.0:
        return q1
    if cosine > f32(0.95):
        return nlerp(q1, q2, amount)
    theta = math.acos(cosine)
    sin_half = math.sqrt(1.0 - cosine * cosine)
    if abs(sin_half) < f32(1e-6):
        return lerp(q1, q2, 0.5)
    ratio_a = math.sin((1.0 - amount) * theta) / sin_half
    ratio_b = math.sin(amount * theta) / sin_half
    return tuple(a * ratio_a + b * ratio_b for a, b in zip(q1, q2))


class QuaternionInterpolationNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="quaternion-interpolation-", dir=build)
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.binary = Path(cls.scratch.name) / "quaternion-interpolation-probe"
        sources = [ROOT / "scripts/quaternion_interpolation_probe.c"] + [
            ROOT / "src/gxmath" / name for name in ("quaternion.c", "vector3.c", "matrix.c", "scalar.c")]
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O0", *(str(path) for path in sources), "-lm", "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def native_values(self, cases):
        normalized = [(tuple(f32(x) for x in q1), tuple(f32(x) for x in q2), f32(t))
                      for q1, q2, t in cases]
        payload = "".join(" ".join(format(x, ".9g") for x in (*q1, *q2, t)) + "\n"
                          for q1, q2, t in normalized)
        result = subprocess.run([str(self.binary)], input=payload, capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = result.stdout.splitlines()
        self.assertEqual(len(rows), len(normalized))
        values = []
        for row in rows:
            fields = row.split()
            self.assertEqual(fields[0], "INTERPOLATION")
            self.assertEqual(len(fields), 13)
            parsed = [float.fromhex(token) for token in fields[1:]]
            self.assertTrue(all(math.isfinite(value) for value in parsed))
            values.append([parsed[start:start + 4] for start in (0, 4, 8)])
        return normalized, values

    def assert_close(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for a, b in zip(actual, expected):
            self.assertLessEqual(abs(a - b), TOLERANCE * max(1.0, abs(a), abs(b)))

    def check_cases(self, cases):
        normalized, outputs = self.native_values(cases)
        for (q1, q2, amount), (native_lerp, native_nlerp, native_slerp) in zip(normalized, outputs):
            with self.subTest(q1=q1, q2=q2, amount=amount):
                self.assert_close(native_lerp, lerp(q1, q2, amount))
                self.assert_close(native_nlerp, nlerp(q1, q2, amount))
                self.assert_close(native_slerp, slerp_model(q1, q2, amount))

    def test_endpoints_identical_and_zero_inputs(self):
        identity = (0, 0, 0, 1)
        q = (0.5, -0.5, 0.5, math.sqrt(0.5))
        cases = [(q, identity, 0), (q, identity, 1), (identity, identity, 0.37),
                 ((0, 0, 0, 0), (0, 0, 0, 0), 0.5)]
        self.check_cases(cases)
        _, outputs = self.native_values([((0, 0, 0, 1), (0, 0, 0, -1), 0.5)])
        self.assert_close(outputs[0][1], (0, 0, 0, 0))
        self.assert_close(outputs[0][2], (0, 0, 0, 1))

    def test_regular_slerp_endpoints_interior_and_extrapolation(self):
        identity = (0, 0, 0, 1)
        q = (math.sqrt(0.75), 0, 0, 0.5)
        cases = [(identity, q, t) for t in (0, 0.25, 0.5, 1, 1.25, -0.25)]
        cases += [(identity, (q[0], q[1], q[2], -q[3]), 0.25)]
        self.check_cases(cases)

    def test_strict_near_parallel_threshold_sides(self):
        identity = (0, 0, 0, 1)
        cases = []
        for cosine in (0.951, 0.95, 0.949):
            q2 = (math.sqrt(1 - cosine * cosine), 0, 0, cosine)
            cases.append((identity, q2, 0.37))
        self.check_cases(cases)
        normalized, outputs = self.native_values(cases)
        self.assert_close([sum(x * x for x in outputs[1][2])], [1.0])
        self.assertGreater(math.dist(outputs[0][2], outputs[1][2]), 1e-5)
        self.assertGreater(math.dist(outputs[2][2], outputs[1][2]), 1e-5)
        self.assertEqual(len(normalized), 3)

    def test_antipodal_sign_correction_and_nlerp_zero_edge(self):
        q1 = (0, 0, 0, 1)
        cases = [(q1, (0, 0, 0, -1), t) for t in (0, 0.25, 0.5, 1)]
        self.check_cases(cases)
        _, outputs = self.native_values([cases[2]])
        self.assert_close(outputs[0][1], (0, 0, 0, 0))
        self.assert_close(outputs[0][2], q1)


if __name__ == "__main__":
    unittest.main()
