#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: source-lineage helpers — comment stripping, shingles and credit markers.

import tempfile
import unittest
from pathlib import Path

import source_lineage_audit as audit


class LineageTests(unittest.TestCase):
    def test_comments_do_not_contribute_tokens(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.c"
            path.write_text("int value = 1; // unique comment marker\n"
                            "/* another marker */\n")
            tokens = audit.tokenise(path)
            self.assertIn("value", tokens)
            self.assertNotIn("unique", tokens)
            self.assertNotIn("marker", tokens)

    def test_shingles_are_stable(self):
        tokens = ["int", "main", "(", ")", "{", "return", "0", ";", "}", "x",
                  "y", "z"]
        self.assertEqual(audit.shingles(tokens), audit.shingles(tokens))
        self.assertEqual(len(audit.shingles(tokens)), 3)

    def test_banner_credits_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ported.c"
            path.write_text("/* [gX: src/old/file.c] */\nint x;\n")
            self.assertTrue(audit.credited(path))

    def test_third_party_credit_is_recognised(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrapped.c"
            path.write_text("/* Uses borrowed code from: raylib v5.x */\n")
            self.assertTrue(audit.credited(path))

    def test_plain_file_is_not_credited(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plain.c"
            path.write_text("int main(void) { return 0; }\n")
            self.assertFalse(audit.credited(path))


if __name__ == "__main__":
    unittest.main()
