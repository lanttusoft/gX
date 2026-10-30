# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: invariant-helper and property-control tests for the seeded math dump.
import cmath
import math
import unittest

try:
    import math_property_check
except SystemExit as exc:
    raise unittest.SkipTest(f"math_property_check unavailable: {exc}")


class PropertyHelperTests(unittest.TestCase):
    def test_close_and_nonfinite(self):
        math_property_check._assert_close("same", 1.0, 1.0)
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check._assert_close("different", 1.0, 2.0)
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check._assert_close("nan", float("nan"), 1.0)

    def test_perpendicular_and_parallel(self):
        math_property_check._assert_perp("perp", [1, 0, 0], [0, 1, 0])
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check._assert_perp("parallel", [1, 0, 0], [1, 0, 0])
        math_property_check._assert_parallel("parallel", [1, 0, 0], [2, 0, 0])
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check._assert_parallel("perp", [1, 0, 0], [0, 1, 0])

    def test_vector_properties(self):
        inputs = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5]
        outputs = [0.0, 0.0, 1.0, 0.0,
                   0.0, 0.0, 0.0,
                   0.5, 0.5, 0.0,
                   1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0]
        math_property_check.check_vectors(0, inputs, outputs)

    def test_matrix_identity_properties(self):
        identity = [1.0, 0.0, 0.0, 0.0,
                    0.0, 1.0, 0.0, 0.0,
                    0.0, 0.0, 1.0, 0.0,
                    0.0, 0.0, 0.0, 1.0]
        inputs = identity + identity + [1.0, 2.0, 3.0]
        outputs = identity + identity + [1.0] + identity + [1.0, 2.0, 3.0]
        math_property_check.check_matrix(0, inputs, outputs)

    def test_quaternion_identity_properties(self):
        inputs = [1.0, 0.0, 0.0, 0.0,
                  1.0, 0.0, 0.0, 0.0,
                  0.5, 1.0, 2.0, 3.0]
        identity = [1.0, 0.0, 0.0, 0.0]
        outputs = identity * 5 + [1.0, 2.0, 3.0]
        math_property_check.check_quaternion(0, inputs, outputs)

    def test_fft_impulse_properties(self):
        inputs = [1.0, 0.0] + [0.0] * 14
        outputs = [1.0, 0.0] * 8
        math_property_check.check_fft(0, inputs, outputs)

    #ai: a complex shifted impulse exercises the inverse sign, order and both channels.
    def test_fft_shifted_complex_impulse(self):
        inputs = [0.0] * 16
        inputs[2:4] = [1.0, 2.0]
        spectrum = [(1.0 + 2.0j) * cmath.exp(-2j * math.pi * k / 8) for k in range(8)]
        outputs = [part for value in spectrum for part in (value.real, value.imag)]
        math_property_check.check_fft(0, inputs, outputs)

    #ai: the unnormalized forward convention must reject an extra 1/N scale.
    def test_fft_wrong_normalization_rejected(self):
        inputs = [1.0, 0.0] + [0.0] * 14
        outputs = [1.0 / 8.0, 0.0] * 8
        with self.assertRaisesRegex(math_property_check.PropertyError, "Parseval"):
            math_property_check.check_fft(0, inputs, outputs)

    #ai: preserve complex energy rather than checking only the real component.
    def test_fft_imaginary_impulse_and_missing_channel(self):
        inputs = [0.0, 1.0] + [0.0] * 14
        math_property_check.check_fft(0, inputs, [0.0, 1.0] * 8)
        with self.assertRaisesRegex(math_property_check.PropertyError, "Parseval"):
            math_property_check.check_fft(0, inputs, [0.0] * 16)

    #ai: Parseval and DC alone miss a reversed frequency order; reconstruction must reject it.
    def test_fft_phase_reversal_rejected_by_inverse(self):
        inputs = [0.0] * 16
        inputs[2:4] = [1.0, 2.0]
        spectrum = [(1.0 + 2.0j) * cmath.exp(2j * math.pi * k / 8) for k in range(8)]
        self.assertAlmostEqual(sum(abs(value) ** 2 for value in spectrum) / 8.0, 5.0)
        self.assertEqual(spectrum[0], 1.0 + 2.0j)
        outputs = [part for value in spectrum for part in (value.real, value.imag)]
        with self.assertRaisesRegex(math_property_check.PropertyError, "IDFT"):
            math_property_check.check_fft(0, inputs, outputs)

    def test_bad_matrix_inverse_rejected(self):
        identity = [1.0, 0.0, 0.0, 0.0,
                    0.0, 1.0, 0.0, 0.0,
                    0.0, 0.0, 1.0, 0.0,
                    0.0, 0.0, 0.0, 1.0]
        bad_inverse = identity.copy()
        bad_inverse[0] = 2.0
        inputs = identity + identity + [1.0, 2.0, 3.0]
        outputs = identity + identity + [1.0] + bad_inverse + [1.0, 2.0, 3.0]
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check.check_matrix(0, inputs, outputs)

    #ai: asymmetric equatorial witness at latitude/longitude zero, with no formula oracle import.
    @staticmethod
    def sky_fixture():
        eq = [1.0, 2.0, 3.0]
        ce, se = math.cos(math.radians(23.4)), math.sin(math.radians(23.4))
        ecliptic = [eq[0], eq[1] * ce + eq[2] * se, eq[2] * ce - eq[1] * se]
        ecliptic_inverse = [eq[0], eq[1] * ce - eq[2] * se, eq[2] * ce + eq[1] * se]
        horizontal = [-eq[2], -eq[1], eq[0]]
        horizontal_inverse = [eq[2], -eq[1], -eq[0]]
        return (eq + [0.0, 0.0], ecliptic + ecliptic_inverse + eq + ecliptic +
                horizontal + horizontal_inverse + eq + horizontal)

    #ai: zero/nonunit vectors remain valid Cartesian inputs; angular branch assumptions are absent.
    def test_sky_zero_and_asymmetric_witnesses(self):
        math_property_check.check_convert_sky(0, *self.sky_fixture())
        math_property_check.check_convert_sky(0, [0.0, 0.0, 0.0, 0.5, -1.2], [0.0] * 24)

    #ai: each output triple is covered, including the two direct inverse helpers.
    def test_sky_output_slot_corruption_rejected(self):
        inputs, expected = self.sky_fixture()
        for offset in range(0, 24, 3):
            with self.subTest(offset=offset):
                outputs = expected.copy()
                outputs[offset:offset + 3] = [2 * value for value in outputs[offset:offset + 3]]
                with self.assertRaises(math_property_check.PropertyError):
                    math_property_check.check_convert_sky(0, inputs, outputs)

    #ai: mutually reciprocal scaling is not an orthogonal frame transform.
    def test_sky_reciprocal_scaling_rejected(self):
        inputs, outputs = self.sky_fixture()
        outputs[0:3] = [2 * value for value in outputs[0:3]]
        outputs[3:6] = [value / 2 for value in outputs[3:6]]
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check.check_convert_sky(0, inputs, outputs)

    #ai: independent synthetic Cartesian witness for the coordinate-checker controls.
    @staticmethod
    def coordinates_fixture(point):
        x, y, z = point
        radius = math.sqrt(x * x + y * y + z * z)
        azimuth = math.atan2(y, x)
        cylinder = [math.hypot(x, y), azimuth, z]
        spherical = [radius, azimuth, math.acos(z / radius)]
        return point, cylinder + point + spherical + point + spherical + cylinder

    #ai: test all azimuth quadrants and nonzero poles, not just a positive octant.
    def test_coordinate_quadrants_and_poles(self):
        for point in ([3.0, 4.0, 5.0], [-3.0, 4.0, 5.0], [-3.0, -4.0, 5.0],
                      [3.0, -4.0, 5.0], [0.0, 0.0, 2.0], [0.0, 0.0, -2.0]):
            with self.subTest(point=point):
                math_property_check.check_convert_coords(0, *self.coordinates_fixture(point))

    #ai: every native coordinate output triple is covered, including the direct interconverters.
    def test_coordinate_output_slots_rejected(self):
        inputs, expected = self.coordinates_fixture([-3.0, 4.0, 5.0])
        for offset in range(0, 18, 3):
            with self.subTest(offset=offset):
                outputs = expected.copy()
                outputs[offset] += 0.5
                with self.assertRaises(math_property_check.PropertyError):
                    math_property_check.check_convert_coords(0, inputs, outputs)

    #ai: preserve explicit domain/branch checks instead of treating angles as globally unique.
    def test_coordinate_invalid_domains_and_branches(self):
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check.check_convert_coords(0, [0.0] * 3, [0.0] * 18)
        inputs, expected = self.coordinates_fixture([3.0, 4.0, 5.0])
        for index, value in ((0, -5.0), (6, 0.0), (1, 2 * math.pi), (7, -2 * math.pi),
                             (8, -0.5), (8, math.pi + 0.5)):
            with self.subTest(index=index, value=value):
                outputs = expected.copy()
                outputs[index] = value
                with self.assertRaises(math_property_check.PropertyError):
                    math_property_check.check_convert_coords(0, inputs, outputs)

    #ai: synthetic witnesses test the Lipschitz checker, not native SDF formulas.
    def test_sdf_sharp_sphere_and_plane_bounds(self):
        inputs = [0.0] * 31
        inputs[28:31] = [1.0, 0.0, 0.0]
        outputs = [0.0] * 22
        outputs[0], outputs[11] = -1.0, 0.0
        outputs[1], outputs[12] = 0.0, 1.0
        math_property_check.check_sdf(0, inputs, outputs)
        outputs[0] -= 1024.0
        outputs[11] -= 1024.0
        math_property_check.check_sdf(0, inputs, outputs)
        outputs[1], outputs[12] = 0.0, -1.0
        math_property_check.check_sdf(0, inputs, outputs)

    #ai: each exact-primitive slot must reject a slope exceeding one.
    def test_sdf_lipschitz_violations_rejected(self):
        inputs = [0.0] * 31
        inputs[28:31] = [1.0, 0.0, 0.0]
        for index in (0, 1, 2, 3, 4, 6, 7, 8, 9):
            with self.subTest(index=index):
                outputs = [0.0] * 22
                outputs[11 + index] = 2.0
                with self.assertRaises(math_property_check.PropertyError):
                    math_property_check.check_sdf(0, inputs, outputs)

    #ai: removing native plane normalization must not silently satisfy a unit bound.
    def test_sdf_nonunit_plane_negative_control(self):
        inputs = [0.0] * 31
        inputs[0:3] = [1.0, 0.0, 0.0]
        inputs[4:7] = [2.0, 0.0, 0.0]
        outputs = [0.0] * 22
        outputs[1] = 1.0
        math_property_check.check_sdf(0, inputs, outputs)
        outputs[1] = 2.0
        with self.assertRaises(math_property_check.PropertyError):
            math_property_check.check_sdf(0, inputs, outputs)

    #ai: cone/Menger remain explicitly outside the existing one-Lipschitz guard.
    def test_sdf_approximate_primitives_stay_excluded(self):
        inputs = [0.0] * 31
        outputs = [0.0] * 22
        outputs[11 + 5] = 100.0
        outputs[11 + 10] = 100.0
        math_property_check.check_sdf(0, inputs, outputs)


if __name__ == "__main__":
    unittest.main()
