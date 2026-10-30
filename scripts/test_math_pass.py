#ai: fail-closed parser, arity, non-finite and child-status controls for the math gate.
import subprocess
import unittest
from types import SimpleNamespace

try:
    import math_pass
except SystemExit as exc:
    raise unittest.SkipTest(f"math_pass unavailable: {exc}")


class MathPassParserTests(unittest.TestCase):
    def test_parse_valid_dump(self):
        parsed = math_pass.parse_dump("log line\nMATHDUMP probe 2 1.0 -2.5\n")
        self.assertEqual(parsed, {"probe": [1.0, -2.5]})

    def test_parse_valid_dump64(self):
        parsed = math_pass.parse_dump("MATHDUMP64 probe 2 1.0 -2.5\n")
        self.assertEqual(parsed, {"probe": [1.0, -2.5]})

    def test_no_dump_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("")

    def test_malformed_line_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("MATHDUMP probe\n")

    def test_invalid_count_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("MATHDUMP probe nope 1.0\n")

    def test_count_mismatch_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("MATHDUMP probe 2 1.0\n")

    def test_invalid_number_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("MATHDUMP probe 1 nope\n")

    def test_nonfinite_number_rejected(self):
        for token in ("nan", "inf", "-inf"):
            with self.subTest(token=token):
                with self.assertRaises(math_pass.MathPassError):
                    math_pass.parse_dump(f"MATHDUMP probe 1 {token}\n")

    def test_duplicate_case_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.parse_dump("MATHDUMP probe 1 1.0\nMATHDUMP probe 1 1.0\n")

    def test_nonzero_status_rejected(self):
        def runner(*args, **kwargs):
            return SimpleNamespace(returncode=9, stdout="MATHDUMP probe 0\n")

        with self.assertRaises(math_pass.MathPassError):
            math_pass.run_dump("unused", runner=runner)

    def test_timeout_rejected(self):
        def runner(*args, **kwargs):
            raise subprocess.TimeoutExpired("unused", 1)

        with self.assertRaises(math_pass.MathPassError):
            math_pass.run_dump("unused", runner=runner)

    def test_run_dump_accepts_zero_status(self):
        def runner(*args, **kwargs):
            return SimpleNamespace(returncode=0, stdout="MATHDUMP probe 1 1.0\n")

        self.assertEqual(math_pass.run_dump("unused", runner=runner), {"probe": [1.0]})


class MathPassCaseTests(unittest.TestCase):
    def test_case_layouts_consistent(self):
        self.assertEqual(set(math_pass.CASES), set(math_pass.CASE_INPUTS))
        for name, (count, _, _) in math_pass.CASES.items():
            with self.subTest(case=name):
                self.assertLess(math_pass.CASE_INPUTS[name], count)

    def test_classify_kinds(self):
        self.assertEqual(math_pass._classify(2.5), [0.0, 2.5])
        self.assertEqual(math_pass._classify(float("inf")), [1.0, 0.0])
        self.assertEqual(math_pass._classify(float("-inf")), [2.0, 0.0])
        self.assertEqual(math_pass._classify(float("nan")), [3.0, 0.0])

    def test_unknown_case_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.check_case("unknown", [])

    def test_native_arity_rejected(self):
        with self.assertRaises(math_pass.MathPassError):
            math_pass.check_case("vectors", [0.0] * 12)

    def test_short_oracle_rejected(self):
        original = math_pass.CASES["vectors"]
        math_pass.CASES["vectors"] = (original[0], original[1], lambda inp, conv: [])
        self.addCleanup(math_pass.CASES.__setitem__, "vectors", original)
        with self.assertRaises(math_pass.MathPassError):
            math_pass.check_case("vectors", [0.0] * 13)

    def test_nonfinite_oracle_rejected(self):
        original = math_pass.CASES["vectors"]
        math_pass.CASES["vectors"] = (original[0], original[1],
                                      lambda inp, conv: [float("nan")] * 7)
        self.addCleanup(math_pass.CASES.__setitem__, "vectors", original)
        with self.assertRaises(math_pass.MathPassError):
            math_pass.check_case("vectors", [0.0] * 13)

    def test_color_wraps_negative_hue(self):
        expected = math_pass.case_convert_color([1.0, 0.0, 0.5, 0.0, 0.0, 0.0],
                                                math_pass.f64)
        self.assertAlmostEqual(float(expected[0]), 330.0)

    def test_matrix_small_case(self):
        values = [1.0, 2.0, 3.0, 4.0,
                  1.0, 2.0, 3.0, 0.0, 4.0, 5.0, 1.0, 0.0, 6.0,
                  -2.0, 5.0, 22.0, 11.0]
        worst, tol = math_pass.check_case("matrix_small", values)
        self.assertLessEqual(worst, tol)

    def test_f64_case(self):
        values = [0.1, 1.0 / 3.0, -2.5, 1.0e-9,
                  0.4, 2.0 / 3.0, 5.5, 3.0e-9, 0.25,
                  0.175, 5.0 / 12.0, -0.5, 1.5e-9]
        worst, tol = math_pass.check_case("f64", values)
        self.assertLessEqual(worst, tol)

    def test_positive_vector_case(self):
        values = [
            1.0, 2.0, 3.0,
            -2.0, 0.5, 4.0,
            6.5, -10.0, 4.5,
            11.0,
            -1.0864197530864197, 0.2716049382716049, 2.1728395061728394,
        ]
        worst, tol = math_pass.check_case("vectors", values)
        self.assertLessEqual(worst, tol)


if __name__ == "__main__":
    unittest.main()
