#!/usr/bin/env python3
#ai: static endpoint/parity/branch witnesses, not verified C/Lean translation.
from pathlib import Path
import re
import unittest

from test_color_units_contract import c_body, compact, lean_body

ROOT = Path(__file__).resolve().parents[1]


def check_contract(c, lean):
    lean = re.sub(r"/-.*?-/|--[^\n]*", "", lean, flags=re.S)
    expected = """
        if (!func || steps == 0) return 0.0f;
        if (steps == 1) return (b - a) * 0.5f * (func(a) + func(b));
        gX_F32 h = (b - a) / (gX_F32)steps;
        gX_F32 result = func(a) + func(b);
        for (gX_U32 i = 1; i < steps; i++) {
            gX_F32 x = a + (gX_F32)i * h;
            if (i % 2) result += 4.0f * func(x);
            else result += 2.0f * func(x);
        }
        result *= h / 3.0f;
        return result;
    """
    if c_body(c, "gX_F32_Calculus_Simpson") != compact(expected):
        raise ValueError("C Simpson contract drift")
    expected_lean = {
        "simpson": """
            if steps = 0 then 0
            else if steps = 1 then (b - a) * (1 / 2) * (f a + f b)
            else let h := (b - a) / (steps : ℝ)
              (f a + f b + simpsonInterior f a h (steps - 1)) * (h / 3)
        """,
        "simpsonPanel": "(f a + 4 * f (a + h) + f (a + 2 * h)) * (h / 3)",
        "cubic": "A * x ^ 3 + B * x ^ 2 + C * x + D",
        "cubicPrimitive": "A * x ^ 4 / 4 + B * x ^ 3 / 3 + C * x ^ 2 / 2 + D * x",
    }
    for name, body in expected_lean.items():
        if lean_body(lean, name) != compact(body):
            raise ValueError(f"Lean Simpson contract drift: {name}")
    interior = """def simpsonInterior (f : ℝ → ℝ) (a h : ℝ) : ℕ → ℝ
      | 0 => 0
      | k + 1 => simpsonInterior f a h k +
          (if (k + 1) % 2 = 0 then 2 else 4) * f (a + (k + 1 : ℕ) * h)"""
    panels = """noncomputable def simpsonPanels (f : ℝ → ℝ) (a h : ℝ) : ℕ → ℝ
      | 0 => 0
      | k + 1 => simpsonPanels f a h k + simpsonPanel f (a + (2 * k : ℕ) * h) h"""
    for definition in (interior, panels):
        if compact(lean).count(compact(definition)) != 1:
            raise ValueError("missing/duplicate recursive Simpson definition")


class SimpsonContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = (ROOT / "src/gxmath/calculus.c").read_text()
        cls.lean = (ROOT.parent / "proofs/GxMath/Calculus.lean").read_text()

    def test_source_correspondence(self):
        check_contract(self.c, self.lean)

    def test_c_negative_controls(self):
        for old, new in (
            ("!func || steps == 0", "!func || steps == 1"),
            ("0.5f * (func(a) + func(b))", "0.25f * (func(a) + func(b))"),
            ("func(a) + func(b);", "func(a);"),
            ("i < steps", "i <= steps"),
            ("if (i % 2)", "if (!(i % 2))"),
            ("4.0f * func(x)", "3.0f * func(x)"),
            ("2.0f * func(x)", "4.0f * func(x)"),
            ("h / 3.0f", "h / 2.0f"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.c)
                with self.assertRaises(ValueError):
                    check_contract(self.c.replace(old, new, 1), self.lean)

    def test_lean_negative_controls(self):
        for old, new in (
            ("steps - 1", "steps"),
            ("then 2 else 4", "then 4 else 2"),
            ("(k + 1 : ℕ) * h", "(k : ℕ) * h"),
            ("f a + f b + simpsonInterior", "f a + simpsonInterior"),
            ("(a + (2 * k : ℕ) * h)", "(a + (k : ℕ) * h)"),
            ("A * x ^ 4 / 4", "A * x ^ 4 / 3"),
        ):
            with self.subTest(old=old):
                self.assertIn(old, self.lean)
                with self.assertRaises(ValueError):
                    check_contract(self.c, self.lean.replace(old, new, 1))

    def test_missing_and_duplicate_witnesses_rejected(self):
        with self.assertRaises(ValueError):
            check_contract("", self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c + self.c, self.lean)
        with self.assertRaises(ValueError):
            check_contract(self.c, self.lean + self.lean)

    def test_commented_old_recurrence_cannot_mask_drift(self):
        start = self.lean.index("def simpsonInterior")
        end = self.lean.index("noncomputable def simpson", start)
        old = self.lean[start:end]
        modified = self.lean.replace("then 2 else 4", "then 4 else 2", 1)
        with self.assertRaises(ValueError):
            check_contract(self.c, modified + "\n/- " + old + " -/\n")


if __name__ == "__main__":
    unittest.main()
