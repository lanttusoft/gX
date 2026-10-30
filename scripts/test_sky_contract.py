#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: narrow C/Lean sky-frame witnesses; not a verified translation or IEEE proof.
from pathlib import Path
import unittest

from test_color_units_contract import c_body, compact, lean_body

ROOT = Path(__file__).resolve().parents[1]


def check_contract(c, lean):
    bodies = {
        "Equatorial2Ecliptic": """
            gX_F32 e = 23.4f * gX_DEG2RAD;
            gX_F32 ce = cosf(e); gX_F32 se = sinf(e);
            gX_F32v3 result;
            result.x = p.x;
            result.y = p.y * ce + p.z * se;
            result.z = p.z * ce - p.y * se;
            return result;
        """,
        "Ecliptic2Equatorial": """
            gX_F32 e = 23.4f * gX_DEG2RAD;
            gX_F32 ce = cosf(e); gX_F32 se = sinf(e);
            gX_F32v3 result;
            result.x = p.x;
            result.y = p.y * ce - p.z * se;
            result.z = p.z * ce + p.y * se;
            return result;
        """,
        "Equatorial2Horizontal": """
            gX_F32 s_lat = sinf(latitude); gX_F32 c_lat = cosf(latitude);
            gX_F32 s_lon = sinf(longitude); gX_F32 c_lon = cosf(longitude);
            gX_F32v3 result;
            result.x = p.x * s_lat * c_lon + p.y * s_lat * s_lon - p.z * c_lat;
            result.y = p.x * s_lon - p.y * c_lon;
            result.z = p.x * c_lat * c_lon + p.y * c_lat * s_lon + p.z * s_lat;
            return result;
        """,
        "Horizontal2Equatorial": """
            gX_F32 s_lat = sinf(latitude); gX_F32 c_lat = cosf(latitude);
            gX_F32 s_lon = sinf(longitude); gX_F32 c_lon = cosf(longitude);
            gX_F32v3 result;
            result.x = p.x * s_lat * c_lon + p.z * c_lat * c_lon + p.y * s_lon;
            result.y = p.x * s_lat * s_lon + p.z * c_lat * s_lon - p.y * c_lon;
            result.z = p.z * s_lat - p.x * c_lat;
            return result;
        """,
    }
    for name, expected in bodies.items():
        if c_body(c, "gX_F32v3_Convert_" + name) != compact(expected):
            raise ValueError(f"C sky contract drift: {name}")

    ecliptic_prefix = "let ce := Real.cos obliquity let se := Real.sin obliquity"
    horizontal_prefix = """
        let sinLat := Real.sin latitude let cosLat := Real.cos latitude
        let sinLon := Real.sin longitude let cosLon := Real.cos longitude
    """
    expected_lean = {
        "equatorialToEcliptic": ecliptic_prefix + """
            {x := p.x, y := p.y * ce + p.z * se, z := p.z * ce - p.y * se}
        """,
        "eclipticToEquatorial": ecliptic_prefix + """
            {x := p.x, y := p.y * ce - p.z * se, z := p.z * ce + p.y * se}
        """,
        "equatorialToHorizontal": horizontal_prefix + """
            {x := p.x * sinLat * cosLon + p.y * sinLat * sinLon - p.z * cosLat
             y := p.x * sinLon - p.y * cosLon
             z := p.x * cosLat * cosLon + p.y * cosLat * sinLon + p.z * sinLat}
        """,
        "horizontalToEquatorial": horizontal_prefix + """
            {x := p.x * sinLat * cosLon + p.z * cosLat * cosLon + p.y * sinLon
             y := p.x * sinLat * sinLon + p.z * cosLat * sinLon - p.y * cosLon
             z := p.z * sinLat - p.x * cosLat}
        """,
    }
    for name, expected in expected_lean.items():
        if lean_body(lean, name) != compact(expected):
            raise ValueError(f"Lean sky contract drift: {name}")


class SkyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = (ROOT / "src/gxmath/convert.c").read_text()
        cls.lean = (ROOT.parent / "proofs/GxMath/Sky.lean").read_text()

    def test_source_correspondence(self):
        check_contract(self.c, self.lean)

    def test_c_negative_controls(self):
        for old, new in (
            ("23.4f * gX_DEG2RAD", "23.5f * gX_DEG2RAD"),
            ("sinf(latitude)", "sinf(longitude)"),
            ("cosf(longitude)", "cosf(0.0f)"),
            ("p.y * ce + p.z * se", "p.y * ce - p.z * se"),
            ("p.z * ce + p.y * se", "p.z * ce - p.y * se"),
            ("p.x * s_lon - p.y * c_lon", "p.x * s_lon + p.y * c_lon"),
            ("p.z * s_lat - p.x * c_lat", "p.z * c_lat - p.x * s_lat"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.c)
                with self.assertRaises(ValueError):
                    check_contract(self.c.replace(old, new, 1), self.lean)

    def test_lean_negative_controls(self):
        for old, new in (
            ("Real.sin latitude", "Real.sin longitude"),
            ("Real.cos longitude", "Real.cos 0"),
            ("p.y * ce - p.z * se", "p.y * ce + p.z * se"),
            ("p.x * sinLon - p.y * cosLon", "p.x * sinLon + p.y * cosLon"),
            ("p.z * sinLat - p.x * cosLat", "p.z * cosLat - p.x * sinLat"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.lean)
                with self.assertRaises(ValueError):
                    check_contract(self.c, self.lean.replace(old, new, 1))

    def test_mutually_inverse_wrong_convention_rejected(self):
        with self.assertRaises(ValueError):
            check_contract(self.c.replace("23.4f * gX_DEG2RAD", "-23.4f * gX_DEG2RAD"), self.lean)

    def test_missing_and_duplicate_witnesses_rejected(self):
        with self.assertRaises(ValueError):
            check_contract("", self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c + self.c, self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c, self.lean + self.lean)


if __name__ == "__main__":
    unittest.main()
