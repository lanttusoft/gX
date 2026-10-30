#!/usr/bin/env python3
#ai: source-drift witnesses for branch-aware coordinates, not verified IEEE/atan2 translation.
from pathlib import Path
import unittest

from test_color_units_contract import c_body, compact, lean_body

ROOT = Path(__file__).resolve().parents[1]


def check_contract(c, lean):
    expected_c = {
        "cartesian2cylinder": """
            gX_F32v3 result;
            result.x = sqrtf(p.x * p.x + p.y * p.y);
            result.y = atan2f(p.y, p.x);
            result.z = p.z;
            return result;
        """,
        "cartesian2spherical": """
            gX_F32v3 result;
            result.x = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
            result.y = atan2f(p.y, p.x);
            result.z = acosf(p.z / result.x);
            return result;
        """,
        "cylinder2cartesian": """
            gX_F32v3 result;
            result.x = c.x * cosf(c.y);
            result.y = c.x * sinf(c.y);
            result.z = c.z;
            return result;
        """,
        "cylinder2spherical": """
            gX_F32v3 result;
            result.x = sqrtf(c.x * c.x + c.z * c.z);
            result.y = c.y;
            result.z = acosf(c.z / result.x);
            return result;
        """,
        "spherical2cartesian": """
            gX_F32v3 result;
            result.x = s.x * sinf(s.z) * cosf(s.y);
            result.y = s.x * sinf(s.z) * sinf(s.y);
            result.z = s.x * cosf(s.z);
            return result;
        """,
        "spherical2cylinder": """
            gX_F32v3 result;
            result.x = s.x * sinf(s.z);
            result.y = s.y;
            result.z = s.x * cosf(s.z);
            return result;
        """,
    }
    for name, expected in expected_c.items():
        if c_body(c, "gX_F32_Convert_" + name) != compact(expected):
            raise ValueError(f"C coordinate contract drift: {name}")

    expected_lean = {
        "xy": "⟨p.x, p.y⟩",
        "planarRadius": "Real.sqrt (p.x * p.x + p.y * p.y)",
        "cartesianToCylinder": "{x := planarRadius p, y := Complex.arg (xy p), z := p.z}",
        "cartesianToSpherical": "{x := p.length, y := Complex.arg (xy p), z := Real.arccos (p.z / p.length)}",
        "cylinderToCartesian": "{x := c.x * Real.cos c.y, y := c.x * Real.sin c.y, z := c.z}",
        "cylinderToSpherical": """
            let radius := Real.sqrt (c.x * c.x + c.z * c.z)
            {x := radius, y := c.y, z := Real.arccos (c.z / radius)}
        """,
        "sphericalToCartesian": """
            {x := s.x * Real.sin s.z * Real.cos s.y
             y := s.x * Real.sin s.z * Real.sin s.y
             z := s.x * Real.cos s.z}
        """,
        "sphericalToCylinder": "{x := s.x * Real.sin s.z, y := s.y, z := s.x * Real.cos s.z}",
    }
    for name, expected in expected_lean.items():
        if lean_body(lean, name) != compact(expected):
            raise ValueError(f"Lean coordinate contract drift: {name}")


class CoordinateContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = (ROOT / "src/gxmath/convert.c").read_text()
        cls.lean = (ROOT.parent / "proofs/GxMath/Coordinates.lean").read_text()

    def test_source_correspondence(self):
        check_contract(self.c, self.lean)

    def test_c_negative_controls(self):
        for old, new in (
            ("atan2f(p.y, p.x)", "atan2f(p.x, p.y)"),
            ("acosf(p.z / result.x)", "asinf(p.z / result.x)"),
            ("p.x * p.x + p.y * p.y);", "p.x * p.x + p.z * p.z);"),
            ("c.x * cosf(c.y)", "c.x * sinf(c.y)"),
            ("acosf(c.z / result.x)", "acosf(c.x / result.x)"),
            ("s.x * sinf(s.z) * cosf(s.y)", "s.x * cosf(s.z) * cosf(s.y)"),
            ("result.y = s.y;", "result.y = s.z;"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.c)
                with self.assertRaises(ValueError):
                    check_contract(self.c.replace(old, new, 1), self.lean)

    def test_lean_negative_controls(self):
        for old, new in (
            ("⟨p.x, p.y⟩", "⟨p.y, p.x⟩"),
            ("Real.arccos (p.z / p.length)", "Real.arcsin (p.z / p.length)"),
            ("c.x * Real.cos c.y", "c.x * Real.sin c.y"),
            ("Real.arccos (c.z / radius)", "Real.arccos (c.x / radius)"),
            ("s.x * Real.sin s.z * Real.cos s.y", "s.x * Real.cos s.z * Real.cos s.y"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.lean)
                with self.assertRaises(ValueError):
                    check_contract(self.c, self.lean.replace(old, new, 1))

    def test_joint_axis_drift_is_not_hidden_by_roundtrips(self):
        with self.assertRaises(ValueError):
            check_contract(self.c.replace("atan2f(p.y, p.x)", "atan2f(p.x, p.y)"),
                           self.lean.replace("⟨p.x, p.y⟩", "⟨p.y, p.x⟩"))

    def test_missing_and_duplicate_witnesses_rejected(self):
        with self.assertRaises(ValueError):
            check_contract("", self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c + self.c, self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c, self.lean + self.lean)


if __name__ == "__main__":
    unittest.main()
