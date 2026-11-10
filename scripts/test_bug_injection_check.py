#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I035 harness contract tests: result classification, baseline control,
#   path/command containment and manifest validation stay deterministic without
#   building or mutating a tree.
import json
from pathlib import Path
import tempfile
import unittest

import bug_injection_check as inj


def command():
    return ["./build/gX_build", "--test-only"]


def manifest_entry(entry_id="entry", **extra):
    value = {
        "id": entry_id,
        "command": command(),
        "expect_regex": "FAIL",
        "type": "replace",
        "path": "src/x.c",
        "old": "a",
        "new": "b",
    }
    value.update(extra)
    return value


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
        status, matched = inj.classify(-6, "crash marker\n", r"crash marker",
                                       expect_crash=True)
        self.assertEqual(status, "killed")
        self.assertTrue(matched)

    def test_classify_expected_crash_requires_marker(self):
        status, matched = inj.classify(-6, "", r"crash marker",
                                       expect_crash=True)
        self.assertEqual(status, "unexpected")
        self.assertFalse(matched)

    def test_classify_expected_crash_handler_exit(self):
        status, matched = inj.classify(134, "free(): invalid size\n",
                                       r"GATE: FAIL|free\(\)",
                                       expect_crash=True)
        self.assertEqual(status, "killed")
        self.assertTrue(matched)

    def test_classify_unexpected_crash_handler_exit(self):
        status, matched = inj.classify(134, "free(): invalid size\n",
                                       r"GATE: FAIL")
        self.assertEqual(status, "unexpected")
        self.assertFalse(matched)

    def test_baseline_ok(self):
        self.assertTrue(inj.baseline_ok(0, "GATE: PASS\n", "GATE: FAIL"))
        self.assertFalse(inj.baseline_ok(1, "GATE: FAIL\n", "GATE: FAIL"))
        self.assertFalse(inj.baseline_ok(0, "GATE: FAIL\n", "GATE: FAIL"))

    def test_safe_relative_rejects_escape(self):
        for value in ("/tmp/evil", "src/../../tmp/evil", "../evil"):
            with self.assertRaises(ValueError):
                inj.safe_relative(value, "path")
        self.assertEqual(inj.safe_relative("src/x.c", "path"), "src/x.c")

    def test_command_allowlist_rejects_bypasses(self):
        for command in (["env", "rm", "-rf", "x"],
                        ["busybox", "sh"],
                        ["scripts/../../tmp/evil"],
                        ["/tmp/evil"],
                        ["python3", "-c", "print(1)"],
                        ["python3", "scripts/../../tmp/evil.py"]):
            entry = manifest_entry()
            entry["command"] = command
            with self.assertRaises(ValueError):
                inj.validate_command(entry)

    def test_manifest_accepts_replace_and_reverse(self):
        manifest = {
            "schema": 1,
            "entries": [
                manifest_entry("replace", type="replace", path="src/x.c",
                               old="a", new="b"),
                manifest_entry("reverse", type="reverse_commit",
                               fix_commit="abc123", paths=["src/x.c"]),
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            loaded = inj.load_manifest(path)
        self.assertEqual(len(loaded["entries"]), 2)

    def test_default_manifest_loads(self):
        loaded = inj.load_manifest(inj.DEFAULT_MANIFEST)
        self.assertGreaterEqual(len(loaded["entries"]), 5)

    def test_manifest_rejects_absolute_path(self):
        manifest = {
            "schema": 1,
            "entries": [manifest_entry(type="replace", path="/tmp/evil",
                                       old="a", new="b")],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_manifest_rejects_escaping_id(self):
        manifest = {
            "schema": 1,
            "entries": [manifest_entry("../escape")],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_manifest_rejects_numeric_regex(self):
        entry = manifest_entry()
        entry["expect_regex"] = 7
        manifest = {"schema": 1, "entries": [entry]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_manifest_rejects_bad_build_timeout(self):
        entry = manifest_entry(build_timeout=None)
        manifest = {"schema": 1, "entries": [entry]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_blocked_entry_loads_with_reason(self):
        entry = manifest_entry(blocked="fixture dependency")
        manifest = {"schema": 1, "entries": [entry]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            loaded = inj.load_manifest(path)
        self.assertEqual(loaded["entries"][0]["blocked"], "fixture dependency")

    def test_manifest_rejects_duplicate_id(self):
        manifest = {
            "schema": 1,
            "entries": [manifest_entry("same"), manifest_entry("same")],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)

    def test_manifest_rejects_invalid_regex(self):
        entry = manifest_entry()
        entry["expect_regex"] = "["
        manifest = {"schema": 1, "entries": [entry]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                inj.load_manifest(path)


if __name__ == "__main__":
    unittest.main()
