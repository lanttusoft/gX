#!/usr/bin/env python3
#ai: bounded native witnesses; exact-real theorems do not assert universal IEEE accuracy.
import math
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 1e-4


def multiply(a, b):
    x, y, z, w = a
    u, v, s, t = b
    return (x*t + w*u + y*s - z*v, y*t + w*v + z*u - x*s,
            z*t + w*s + x*v - y*u, w*t - x*u - y*v - z*s)


def sandwich(q, v):
    return multiply(multiply(q, (*v, 0.0)), (-q[0], -q[1], -q[2], q[3]))[:3]


class QuaternionRotationNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="quaternion-rotation-", dir=build)
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.binary = Path(cls.scratch.name) / "quaternion-probe"
        sources = [ROOT / "scripts/quaternion_rotation_probe.c"] + [
            ROOT / "src/gxmath" / name for name in ("quaternion.c", "vector3.c", "matrix.c", "scalar.c")]
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O0", *(str(path) for path in sources), "-lm", "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def native_values(self, cases):
        payload = "".join(" ".join(format(value, ".9g") for value in (*q, *r, *v)) + "\n"
                          for q, r, v in cases)
        result = subprocess.run([str(self.binary)], input=payload, capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = result.stdout.splitlines()
        self.assertEqual(len(rows), len(cases))
        values = []
        for row in rows:
            fields = row.split()
            self.assertEqual(fields[0], "QUATERNION")
            self.assertEqual(len(fields), 25)
            parsed = [float.fromhex(token) for token in fields[1:]]
            self.assertTrue(all(math.isfinite(value) for value in parsed))
            values.append([parsed[start:start + 3] for start in range(0, 24, 3)])
        return values

    def assert_close(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for a, b in zip(actual, expected):
            self.assertLessEqual(abs(a-b), TOLERANCE * max(1.0, abs(a), abs(b)))

    def check_case(self, case, output, unit):
        q, r, v = case
        norm_sq = sum(value * value for value in q)
        rotated = sandwich(q, v)
        self.assert_close(output[0], rotated)
        self.assert_close(output[1], rotated)
        self.assert_close(output[2], v)
        self.assert_close(output[3], v)
        matrix = [rotated[i] + (1.0 - norm_sq) * v[i] for i in range(3)]
        self.assert_close(output[4], matrix)
        self.assert_close(output[5], matrix)
        self.assert_close(output[6], sandwich(multiply(q, r), v))
        self.assert_close(output[7], output[6])
        self.assert_close([math.dist(output[0], (0, 0, 0))], [norm_sq * math.dist(v, (0, 0, 0))])
        if unit:
            self.assert_close([norm_sq], [1.0])
            self.assert_close(output[4], output[0])

    def test_unit_rotations_and_matrix_agreement(self):
        half = math.sqrt(0.5)
        quaternions = ((0, 0, 0, 1), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0),
                       (0.5, 0.5, 0.5, 0.5), (0.5, -0.5, 0.5, -0.5),
                       (0, 0, half, half), (half, 0, 0, half))
        vectors = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1.25, -2, 0.5))
        partners = ((0.5, 0.5, 0.5, 0.5), (1, 0, 0, 0))
        cases = [(q, r, v) for q in quaternions for r in partners for v in vectors]
        for case, output in zip(cases, self.native_values(cases)):
            with self.subTest(case=case):
                self.check_case(case, output, unit=True)

    def test_nonunit_scale_and_inverse(self):
        cases = [(q, (0.5, -0.5, 0.5, -0.5), v)
                 for q in ((0, 0, 0, 2), (-1, 2, 1, 0.5))
                 for v in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1.25, -2, 0.5))]
        for case, output in zip(cases, self.native_values(cases)):
            with self.subTest(case=case):
                self.check_case(case, output, unit=False)
        counterexample, = self.native_values([((0, 0, 0, 2), (0, 0, 0, 1), (1, 0, 0))])
        self.assert_close(counterexample[0], (4, 0, 0))
        self.assert_close(counterexample[4], (1, 0, 0))

    def test_quarter_turn_handedness_and_noncommuting_order(self):
        half = math.sqrt(0.5)
        q = (0, 0, half, half)
        r = (half, 0, 0, half)
        v = (1, 0, 0)
        output, = self.native_values([(q, r, v)])
        self.assert_close(output[0], (0, 1, 0))
        self.assert_close(output[4], (0, 1, 0))
        self.assertGreater(math.dist(output[6], sandwich(multiply(r, q), v)), 0.5)
        self.check_case((q, r, v), output, unit=True)

    def test_zero_vector(self):
        cases = [(q, (1, 0, 0, 0), (0, 0, 0))
                 for q in ((0.5, 0.5, 0.5, 0.5), (0, 0, 0, 2))]
        for case, output in zip(cases, self.native_values(cases)):
            with self.subTest(case=case):
                self.check_case(case, output, unit=False)


if __name__ == "__main__":
    unittest.main()
