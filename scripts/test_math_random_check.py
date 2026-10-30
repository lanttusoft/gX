#ai: parser and differential-control tests for the seeded randomized math corpus.
import unittest

try:
    import math_random_check
except SystemExit as exc:
    raise unittest.SkipTest(f"math_random_check unavailable: {exc}")


def zero_line(family, probe):
    input_count, output_count = math_random_check.EXPECTED[family]
    inputs = " ".join(["0x0p+0"] * input_count)
    outputs = " ".join(["0x0p+0"] * output_count)
    return f"MATHRANDOM {family} {probe} {input_count} {inputs} {output_count} {outputs}"


def full_zero_dump(probes=math_random_check.PROBES):
    return "\n".join(
        zero_line(family, probe)
        for family in math_random_check.EXPECTED
        for probe in range(probes)
    )


class MathRandomParserTests(unittest.TestCase):
    def test_full_zero_dump_accepted(self):
        probes = math_random_check.parse_dump(full_zero_dump())
        for family in math_random_check.EXPECTED:
            self.assertEqual(len(probes[family]), math_random_check.PROBES)

    def test_custom_probe_count_accepted(self):
        probes = math_random_check.parse_dump(full_zero_dump(2), probe_count=2)
        for family in math_random_check.EXPECTED:
            self.assertEqual(len(probes[family]), 2)

    def test_empty_dump_rejected(self):
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump("")

    def test_unknown_family_rejected(self):
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump(zero_line("vectors", 0).replace("vectors", "unknown"))

    def test_wrong_input_count_rejected(self):
        line = zero_line("vectors", 0).replace(" 7 ", " 6 ", 1)
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump(line)

    def test_truncated_output_rejected(self):
        line = zero_line("vectors", 0).rsplit(" ", 1)[0]
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump(line)

    def test_duplicate_probe_rejected(self):
        lines = [zero_line(family, probe)
                 for family in math_random_check.EXPECTED
                 for probe in range(math_random_check.PROBES)]
        lines.append(zero_line("vectors", 0))
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump("\n".join(lines))

    def test_invalid_float_rejected(self):
        line = zero_line("vectors", 0).replace("0x0p+0", "nope", 1)
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.parse_dump(line)


class MathRandomDifferentialTests(unittest.TestCase):
    def test_vector_probe(self):
        inputs = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5]
        outputs = [0.0, 0.0, 1.0, 0.0,
                   0.0, 0.0, 0.0,
                   0.5, 0.5, 0.0,
                   1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0]
        worst = math_random_check.check_probe("vectors", 0, inputs, outputs)
        self.assertLessEqual(worst, math_random_check.TOL)

    def test_nonfinite_output_rejected(self):
        inputs = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5]
        outputs = [float("nan")] + [0.0] * 15
        with self.assertRaises(math_random_check.RandomCheckError):
            math_random_check.check_probe("vectors", 0, inputs, outputs)


if __name__ == "__main__":
    unittest.main()
