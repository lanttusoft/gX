#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: import reachability and axiom-ledger parsing must not silently hide audit omissions.
from pathlib import Path
import tempfile
import unittest

from proof_audit import audit_source, coverage, imports, parse_axioms


class ProofAuditTests(unittest.TestCase):
    def test_root_declarations_are_audited_too(self):
        source = audit_source(("GxProofs",))
        self.assertIn('origin == "GxProofs"', source)
        self.assertIn('origin.startsWith "GxProofs."', source)

    def test_nested_comments_do_not_import_modules(self):
        text = "/- import Fake\n /- import Nested -/\n-/\nimport GxProofs.A GxProofs.B -- note\n"
        self.assertEqual(imports(text), ["GxProofs.A", "GxProofs.B"])

    def test_string_comment_tokens_do_not_hide_imports(self):
        self.assertEqual(imports('def x := "-- /-"\nimport GxProofs.A\n'), ["GxProofs.A"])

    def test_multiline_strings_do_not_import_modules(self):
        self.assertEqual(imports('def x := "text\nimport GxProofs.Fake\n"\nimport GxProofs.A\n'), ["GxProofs.A"])

    def test_comments_separate_import_names(self):
        self.assertEqual(imports('import GxProofs.A/- note -/GxProofs.B\n'), ["GxProofs.A", "GxProofs.B"])

    def test_orphan_and_transitive_import(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "GxProofs").mkdir()
            (root / "GxProofs.lean").write_text("import GxProofs.A\n")
            (root / "GxProofs/A.lean").write_text("import GxProofs.B\n")
            (root / "GxProofs/B.lean").write_text("import GxProofs.A\n")
            (root / "GxProofs/Orphan.lean").write_text("")
            self.assertEqual(coverage(root, ("GxProofs",)), (3, ["GxProofs.Orphan"]))

    def test_empty_and_nonstandard_axioms_are_preserved(self):
        rows = parse_axioms("GX_AXIOMS A []\nGX_AXIOMS B [propext, sorryAx]\n")
        self.assertEqual(rows, [{"name": "A", "axioms": []}, {"name": "B", "axioms": ["propext", "sorryAx"]}])


if __name__ == "__main__":
    unittest.main()
