#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: bounded native cubic/branch witnesses; no universal IEEE error or convergence claim.
from fractions import Fraction
import math
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 1e-4


def primitive(coefficients, x):
    a, b, c, d = map(Fraction, coefficients)
    x = Fraction(x)
    return a * x ** 4 / 4 + b * x ** 3 / 3 + c * x ** 2 / 2 + d * x


class SimpsonNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="simpson-probe-", dir=build)
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.binary = Path(cls.scratch.name) / "simpson-probe"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O0", str(ROOT / "scripts/simpson_probe.c"),
            str(ROOT / "src/gxmath/calculus.c"), str(ROOT / "src/gxmath/scalar.c"),
            "-lm", "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def native_values(self, cases):
        payload = "".join(" ".join(format(float(value), ".9g") for value in (*coefficients, steps, low, high))
                          + "\n" for coefficients, steps, low, high in cases)
        result = subprocess.run([str(self.binary)], input=payload, capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = result.stdout.splitlines()
        self.assertEqual(len(rows), len(cases) + 1)
        self.assertEqual(rows[-1].split(), ["SIMPSON_NULL", "0x0p+0"])
        values = []
        for row in rows[:-1]:
            parts = row.split()
            self.assertEqual(len(parts), 2)
            self.assertEqual(parts[0], "SIMPSON")
            value = float.fromhex(parts[1])
            self.assertTrue(math.isfinite(value))
            values.append(value)
        return values

    def assert_native_close(self, actual, expected):
        expected = float(expected)
        self.assertLessEqual(abs(actual - expected), TOLERANCE * max(1.0, abs(actual), abs(expected)))

    def test_degrees_zero_through_three_even_steps(self):
        coefficients = ((0, 0, 0, 2), (0, 0, -0.5, 1.25),
                        (0, 0.75, -0.5, 1), (-0.25, 0.5, 0.75, -1))
        intervals = ((-1.25, 2.5), (2.5, -1.25), (-2, -0.5), (1, 1))
        cases = [(c, steps, low, high) for c in coefficients for steps in (2, 4, 6, 10, 32)
                 for low, high in intervals]
        for case, value in zip(cases, self.native_values(cases)):
            with self.subTest(case=case):
                c, _, low, high = case
                self.assert_native_close(value, primitive(c, high) - primitive(c, low))

    def test_zero_step_and_null_callback(self):
        cases = [((1, 2, 3, 4), 0, -1, 2), ((0, 0, 0, 1), 0, 0, 1)]
        self.assertEqual(self.native_values(cases), [0.0, 0.0])
        self.assertEqual(self.native_values([]), [])

    def test_one_step_affine_and_quadratic_counterexample(self):
        cases = [((0, 0, 0.5, 1), 1, -1, 2), ((0, 0, 0.5, 1), 1, 2, -1),
                 ((0, 1, 0, 0), 1, 0, 1)]
        values = self.native_values(cases)
        for case, value in zip(cases[:2], values[:2]):
            c, _, low, high = case
            self.assert_native_close(value, primitive(c, high) - primitive(c, low))
        self.assert_native_close(values[2], Fraction(1, 2))
        self.assertGreater(abs(values[2] - float(Fraction(1, 3))), TOLERANCE)

    def test_odd_steps_are_not_silently_treated_as_exact(self):
        value, = self.native_values([((0, 0, 0, 1), 3, 0, 1)])
        self.assert_native_close(value, Fraction(8, 9))
        self.assertGreater(abs(value - 1.0), TOLERANCE)


if __name__ == "__main__":
    unittest.main()
