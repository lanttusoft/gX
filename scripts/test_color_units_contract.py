#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: narrow source-drift witnesses, not a verified C-to-Lean translation or IEEE proof.
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def compact(text):
    return re.sub(r"\s+", "", text)


def c_body(source, name):
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\s*\([^{};]*\)\s*\{", source))
    if len(matches) != 1:
        raise ValueError(f"expected one definition of {name}, got {len(matches)}")
    start = matches[0].end()
    depth = 1
    for end in range(start, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return compact(source[start:end])
    raise ValueError(f"unclosed definition of {name}")


def lean_body(source, name):
    source = re.sub(r"/-.*?-/|--[^\n]*", "", source, flags=re.S)
    matches = list(re.finditer(
        r"^\s*(?:noncomputable\s+)?def\s+" + re.escape(name) +
        r"\b[^=]*:=\s*(.*?)(?=\n\s*(?:(?:noncomputable\s+)?def|theorem|end)\b)",
        source, flags=re.S | re.M))
    if len(matches) != 1:
        raise ValueError(f"expected one Lean definition of {name}, got {len(matches)}")
    return compact(matches[0].group(1))


def check_contract(color, units, color_lean, units_lean):
    c_expected = {
        "gX_Color_LerpChannel": """
            gX_I32 v = (gX_I32)a + (gX_I32)(t * (gX_F32)((gX_I32)b - (gX_I32)a));
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return (gX_U8)v;
        """,
    }
    for name, rgb_only in (("gX_Color_Lerp", False), ("gX_Color_LerpRGB", True)):
        fields = "".join(
            f"result.{c} = gX_Color_LerpChannel(c1.{c}, c2.{c}, t);" for c in "rgb")
        alpha = "result.a = 255;" if rgb_only else "result.a = gX_Color_LerpChannel(c1.a, c2.a, t);"
        c_expected[name] = "gX_Color result = {0};" + fields + alpha + "return result;"
    c_expected["gX_Color_Mul"] = """
        gX_Color result = {0};
        gX_F32v4 temp1 = gX_Color_ToV4(c1);
        gX_F32v4 temp2 = gX_Color_ToV4(c2);
        temp1.x *= temp2.x; temp1.y *= temp2.y;
        temp1.z *= temp2.z; temp1.w *= temp2.w;
        result = gX_Color_FromV4(temp1);
        return result;
    """
    c_expected["gX_Color_ToV4"] = "gX_F32v4 result = {0};" + "".join(
        f"result.{v} = ((gX_F32)(col.{c}))/255.0f;" for v, c in zip("xyzw", "rgba")) + "return result;"
    c_expected["gX_Color_FromV4"] = "gX_Color result = {0};" + "".join(
        f"result.{c} = (gX_U8)(v4.{v}*255.0f);" for v, c in zip("xyzw", "rgba")) + "return result;"
    for name, expected in c_expected.items():
        if c_body(color, name) != compact(expected):
            raise ValueError(f"C color contract drift: {name}")

    unit_formulas = {
        "Foot2Meter": "0.3048f * foots",
        "Inch2Meter": "0.0254f * inches",
        "Meter2Foot": "meters / 0.3048f",
        "Meter2Inch": "meters / 0.0254f",
        "Kelvin2Celsius": "kelvin - 273.15f",
        "Celsius2Kelvin": "celsius + 273.15f",
        "Celsius2Fahrenheit": "celsius * 9.0f / 5.0f + 32.0f",
        "Fahrenheit2Celsius": "(fahrenheit - 32.0f) * 5.0f / 9.0f",
    }
    for name, formula in unit_formulas.items():
        if c_body(units, "gX_F32_Convert_" + name) != compact("return " + formula + ";"):
            raise ValueError(f"C unit contract drift: {name}")

    lean_expected = {
        "validChannel": "0 ≤ a ∧ a ≤ 255",
        "clampChannel": "min 255 (max 0 a)",
        "truncate": "if 0 ≤ x then ⌊x⌋ else ⌈x⌉",
        "lerpChannel": "clampChannel (a + truncate (t * ((b : ℝ) - (a : ℝ))))",
        "normalizeChannel": "(a : ℝ) / 255",
        "fromNormalized": "truncate (x * 255)",
        "multiplyChannel": "fromNormalized (normalizeChannel a * normalizeChannel b)",
    }
    for name, expected in lean_expected.items():
        if lean_body(color_lean, name) != compact(expected):
            raise ValueError(f"Lean color contract drift: {name}")
    for name, expected in {
        "footToMeter": "0.3048 * x", "inchToMeter": "0.0254 * x",
        "meterToFoot": "x / 0.3048", "meterToInch": "x / 0.0254",
        "kelvinToCelsius": "x - 273.15", "celsiusToKelvin": "x + 273.15",
        "celsiusToFahrenheit": "x * 9 / 5 + 32",
        "fahrenheitToCelsius": "(x - 32) * 5 / 9",
    }.items():
        if lean_body(units_lean, name) != compact(expected):
            raise ValueError(f"Lean unit contract drift: {name}")


class ColorUnitsContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = (
            (ROOT / "src/gxmath/color.c").read_text(),
            (ROOT / "src/gxmath/convert.c").read_text(),
            (ROOT.parent / "proofs/GxMath/Color.lean").read_text(),
            (ROOT.parent / "proofs/GxMath/Units.lean").read_text(),
        )

    def test_source_correspondence(self):
        check_contract(*self.sources)

    def test_negative_controls(self):
        mutations = (
            (0, "(gX_I32)a + (gX_I32)( t *", "(gX_I32)a + (gX_I32)( 1 + t *"),
            (0, "if (v < 0) v = 0;", "if (v < 1) v = 1;"),
            (0, "if (v > 255) v = 255;", "if (v > 254) v = 254;"),
            (0, "c1.r, c2.r", "c1.r, c2.b"),
            (0, "temp1.z *= temp2.z", "temp1.z += temp2.z"),
            (0, "result.a = 255;", "result.a = 0;"),
            (0, "/255.0f", "/256.0f"),
            (0, "v4.x*255.0f", "v4.x*256.0f"),
            (1, "0.3048f * foots", "0.3049f * foots"),
            (1, "meters / 0.0254f", "meters * 0.0254f"),
            (1, "kelvin - 273.15f", "kelvin + 273.15f"),
            (1, "celsius * 9.0f / 5.0f", "celsius * 5.0f / 9.0f"),
            (2, "then ⌊x⌋ else ⌈x⌉", "then ⌊x⌋ else ⌊x⌋"),
            (2, "a + truncate (t *", "a - truncate (t *"),
            (2, "(a : ℝ) / 255", "(a : ℝ) / 256"),
            (3, "0.3048 * x", "0.3049 * x"),
            (3, "x - 273.15", "x + 273.15"),
        )
        for index, old, new in mutations:
            with self.subTest(index=index, old=old):
                self.assertIn(old, self.sources[index])
                sources = list(self.sources)
                sources[index] = sources[index].replace(old, new, 1)
                with self.assertRaises(ValueError):
                    check_contract(*sources)

    def test_truncation_before_addition_is_not_floor_of_lerp(self):
        color, units, color_lean, units_lean = self.sources
        modified_c = color.replace(
            "(gX_I32)a + (gX_I32)( t * (gX_F32)((gX_I32)b - (gX_I32)a) )",
            "(gX_I32)((gX_F32)a + t * ((gX_F32)b - (gX_F32)a))")
        self.assertNotEqual(modified_c, color)
        with self.assertRaises(ValueError):
            check_contract(modified_c, units, color_lean, units_lean)
        modified = color_lean.replace(
            "a + truncate (t * ((b : ℝ) - (a : ℝ)))",
            "truncate ((a : ℝ) + t * ((b : ℝ) - (a : ℝ)))")
        self.assertNotEqual(modified, color_lean)
        with self.assertRaises(ValueError):
            check_contract(color, units, modified, units_lean)

    def test_reciprocal_pair_drift_still_fails(self):
        color, units, color_lean, units_lean = self.sources
        with self.assertRaises(ValueError):
            check_contract(color, units.replace("0.3048f", "0.3049f"),
                           color_lean, units_lean.replace("0.3048", "0.3049"))

    def test_reject_missing_duplicate_and_unclosed_functions(self):
        for source in ("", "int f() { return 0; } int f() { return 0; }", "int f() {"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                c_body(source, "f")
        with self.assertRaises(ValueError):
            lean_body("def f := 0\ndef f := 1\nend X", "f")

    def test_comments_and_whitespace_do_not_change_witnesses(self):
        sources = ["\n" + text.replace(" :=", "\n :=") for text in self.sources]
        sources[0] = "/* inert comment */\n" + sources[0]
        sources[2] = "/- inert comment -/\n" + sources[2]
        check_contract(*sources)


if __name__ == "__main__":
    unittest.main()
