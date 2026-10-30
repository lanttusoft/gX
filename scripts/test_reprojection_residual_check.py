#ai: independent oracle shape, convention and negative controls; no empirical accuracy threshold.
import unittest

from fractions import Fraction as Q

from reprojection_residual_check import check_case, check_dump, normalize, residual, row, weighted_action, weighted_bounds


class ReprojectionResidualTests(unittest.TestCase):
    def test_row_convention_and_residual(self):
        identity = [int(r == c) for r in range(4) for c in range(4)]
        translate = identity.copy()
        translate[12] = 2
        inverse = identity.copy()
        inverse[12] = -2
        self.assertEqual(row([1, 2, 3, 1], translate), [3, 2, 3, 1])
        self.assertEqual(residual(translate, inverse), [0]*16)
        wrong = identity.copy()
        wrong[3] = -2
        self.assertNotEqual(residual(translate, wrong), [0]*16)

    def test_normalize_zero_rejected(self):
        with self.assertRaises(ValueError):
            normalize([1, 2, 3, 0])

    def test_missing_and_unknown_fixtures_rejected(self):
        with self.assertRaises(ValueError):
            check_dump("")
        with self.assertRaises(ValueError):
            check_dump("REPROJECTION unknown 0x0p+0")

    def test_bad_shape_and_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            check_case("perspective", ["0x0p+0"]*62)
        with self.assertRaises(ValueError):
            check_case("perspective", ["nan"]*63)

    def test_false_singular_fixture_rejected(self):
        with self.assertRaises(ValueError):
            check_case("rounded_singular", ["0x1p+0"]*63)

    def test_weighted_columns_not_rows(self):
        matrix = list(range(16))
        self.assertEqual(weighted_action([1, 0, 0, 0], matrix), [0, 1, 2, 3])
        self.assertNotEqual(weighted_action([1, 0, 0, 0], matrix), [0, 4, 8, 12])
        self.assertEqual(weighted_action([0, 0, 0, 1], matrix), [12, 13, 14, 15])

    def test_sparse_component_projection(self):
        identity = [int(r == c) for r in range(4) for c in range(4)]
        error = [Q(0)]*16
        error[12] = Q(1, 100)
        recovery, world, clip, projected = weighted_bounds(
            [Q(0), Q(0), Q(0), Q(1)], error, identity, Q(1),
            [Q(1, 100), Q(0), Q(0), Q(1)], [Q(0), Q(0), Q(0), Q(1)])
        self.assertEqual(recovery, [Q(1, 100), 0, 0, 0])
        self.assertEqual(world, recovery)
        self.assertEqual(clip, recovery)
        self.assertEqual(projected, [Q(1, 200), 0, 0])

    def test_normalized_w_error_is_zero(self):
        identity = [int(r == c) for r in range(4) for c in range(4)]
        error = [Q(0)]*16
        error[15] = Q(1, 10)
        _, normalized, _, _ = weighted_bounds(
            [Q(1), Q(2), Q(3), Q(1)], error, identity, Q(11, 10),
            [Q(10, 11), Q(20, 11), Q(30, 11), Q(1)], [Q(1), Q(2), Q(3), Q(1)])
        self.assertEqual(normalized, [Q(1, 11), Q(2, 11), Q(3, 11), 0])


if __name__ == "__main__":
    unittest.main()
