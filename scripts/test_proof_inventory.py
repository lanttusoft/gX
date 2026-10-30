# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: parser and declaration-classification controls for the generated proof catalog.
import unittest

try:
    import proof_inventory
except SystemExit as exc:
    raise unittest.SkipTest(f"proof_inventory unavailable: {exc}")


class ProofInventoryParserTests(unittest.TestCase):
    def test_clean_strips_comments_and_strings(self):
        text = 'def kept : Nat := 1 -- comment\ntheorem gone : False := by\n  /- block -/\n  sorry\n'
        cleaned = proof_inventory.clean(text)
        self.assertIn("def kept", cleaned)
        self.assertNotIn("comment", cleaned)
        self.assertNotIn("block", cleaned)
        self.assertIn("theorem gone", cleaned)

    def test_declaration_classification(self):
        cases = {
            "theorem t : True := by trivial": "theorem",
            "lemma l : True := by trivial": "lemma",
            "noncomputable def d : Nat := 0": "def",
            "private theorem p : True := by trivial": "theorem",
            "@[simp] theorem s : True := by trivial": "theorem",
            "structure S where\n  x : Nat": "structure",
            "instance : Inhabited Nat := ⟨0⟩": "instance",
        }
        for source, kind in cases.items():
            with self.subTest(source=source):
                match = proof_inventory.DECL_RE.match(source)
                self.assertIsNotNone(match)
                self.assertEqual(match.group(1), kind)

    def test_imports_ignore_comments(self):
        text = "-- import Fake.Module\nimport Real.Module\n/- import Also.Fake -/\nimport Other.Module\n"
        self.assertEqual(proof_inventory.imports(text), ["Real.Module", "Other.Module"])

    def test_importance_manifest(self):
        data = proof_inventory.load_importance()
        self.assertEqual(data["modules"]["GxProofs.MatrixAccess"]["significance"], 95)
        self.assertEqual(data["modules"]["GxProofs.MatrixAccess"]["importance"], "K")

    def test_generated_catalog_mentions_new_modules(self):
        text = proof_inventory.markdown()
        self.assertIn("GxMath.MatrixSmall", text)
        self.assertIn("GxMath.Vector64", text)
        self.assertIn("GxMath.Sdf", text)
        self.assertIn("GxMath.Scalar", text)
        self.assertIn("GxMath.Fourier", text)
        self.assertIn("GxMath.Calculus", text)
        self.assertIn("| color | `GxMath.Color` |", text)
        self.assertIn("GxMath.Sky", text)
        self.assertIn("GxMath.Coordinates", text)
        self.assertIn("| convert | `GxMath.Units/Sky/Coordinates` |", text)
        self.assertIn("| SDF | `GxMath.Sdf` |", text)
        self.assertIn("sphere/unit-normal plane Lipschitz and clearance", text)
        self.assertIn("| calculus | `GxMath.Calculus` |", text)
        self.assertIn("positive-even-step Simpson cubic integral", text)
        self.assertIn("unit-only matrix agreement", text)
        self.assertIn("| Fourier/FFT | `GxMath.Fourier` |", text)
        self.assertIn("inverse round trips and Parseval for nonzero length", text)
        self.assertIn("| scalar/int math | `GxMath.Scalar` |", text)
        self.assertIn("## Domain map", text)
        self.assertIn("## Matrix models", text)
        self.assertIn("## Kernel coverage", text)
        self.assertIn("## Significance review", text)
        self.assertIn("## Importance gaps", text)
        self.assertIn("Project `axiom` declarations: 0", text)


if __name__ == "__main__":
    unittest.main()
