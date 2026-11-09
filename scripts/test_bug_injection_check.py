#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I035 harness contract tests: result classification and manifest
#   validation stay deterministic without building or mutating a tree.
import json
from pathlib import Path
import tempfile
import unittest

import bug_injection_check as inj


class BugInjectionTests(unittest.TestCase):
    def test_classify_killed(self):
        status, matched = inj.classify(1, "GATE: FAIL\n", r"GATE: FAIL")
        self.assertEqual(status, "killed")
        self.assertTrue(matched)

    def test_classify_survived(self):
        status, matched = inj.classify(0, "GATE: PASS\n", r"GATE: FAIL")
        self.assertEqual(status, "survived")
        self.assertFalse(matched)

    def test_classify_unexpected(self):
        status, matched = inj.classify(2, "build failed\n", r"GATE: FAIL")
        self.assertEqual(status, "unexpected")
        self.assertFalse(matched)

    def test_classify_expected_crash(self):
        status, matched = inj.classify(-6, "", r"GATE: FAIL",
                                       expect_crash=True)
        self.assertEqual(status, "killed")
        self.assertFalse(matched)

    def test_classify_unexpected_crash(self):
        status, matched = inj.classify(-6, "", r"GATE: FAIL")
        self.assertEqual(status, "unexpected")
        self.assertFalse(matched)

    def test_manifest_accepts_replace_and_reverse(self):
        manifest = {
            "schema": 1,
            "entries": [
                {
                    "id": "replace",
                    "type": "replace",
                    "path": "src/x.c",
                    "old": "a",
                    "new": "b",
                    "command": ["true"],
                    "expect_regex": "FAIL",
                },
                {
                    "id": "reverse",
                    "type": "reverse_commit",
                    "fix_commit": "abc123",
                    "paths": ["src/x.c"],
                    "command": ["true"],
                    "expect_regex": "FAIL",
                },
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            loaded = inj.load_manifest(path)
        self.assertEqual(len(loaded["entries"]), 2)

    def test_manifest_rejects_duplicate_id(self):
        manifest = {
            "schema": 1,
            "entries": [
                {"id": "same", "command": ["true"], "expect_regex": "FAIL"},
                {"id": "same", "command": ["true"], "expect_regex": "FAIL"},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_manifest_rejects_invalid_regex(self):
        manifest = {
            "schema": 1,
            "entries": [
                {"id": "bad", "command": ["true"], "expect_regex": "["},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)


if __name__ == "__main__":
    unittest.main()
