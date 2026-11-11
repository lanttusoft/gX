#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: M4 — the summary runner may retry a parallel failure once, never twice,
#   and must keep the first verdict and duration in the report.
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

import gate_manifest


GATE = '''\
import pathlib
import sys

counter = pathlib.Path(sys.argv[1])
fail_before = int(sys.argv[2])
attempt = int(counter.read_text()) + 1 if counter.exists() else 1
counter.write_text(str(attempt))
ok = attempt > fail_before
print(f"TEST_GATE: {'PASS' if ok else 'FAIL'} attempt={attempt}")
raise SystemExit(0 if ok else 1)
'''


#ai: L1 — an explicitly empty --only selection must not be a green 0/0 run.
class EmptySelectionTests(unittest.TestCase):
    def test_empty_only_is_rejected(self):
        for selection in (",", " , "):
            with self.subTest(selection=selection):
                result = subprocess.run(
                    [sys.executable,
                     str(gate_manifest.ROOT / "scripts" / "audit_summary.py"),
                     "--only", selection, "--no-cache"],
                    cwd=gate_manifest.ROOT, text=True,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    timeout=30)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertIn("at least one gate name", result.stdout)
                self.assertNotIn("AUDIT_SUMMARY: PASS", result.stdout)


class RetryPolicyTests(unittest.TestCase):
    def run_summary(self, fail_before):
        binary = gate_manifest.ROOT / "build" / "gX_build"
        if not binary.is_file():
            self.skipTest("engine binary is required for the runner fixture")
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        script = root / "flaky.py"
        script.write_text(GATE, encoding="utf-8")
        counter = root / "counter.txt"
        report = root / "report.json"
        relative = os.path.relpath(script, gate_manifest.ROOT / "scripts")
        manifest = root / "gates.toml"
        manifest.write_text(textwrap.dedent(f'''\
            [defaults]
            timeout = 30

            [groups.content]
            inputs = ["docs/"]

            [gate.flaky]
            script = {json.dumps(relative)}
            args = [{json.dumps(str(counter))}, "{fail_before}"]
            tier = "full"
            group = "content"
            '''), encoding="utf-8")
        result = subprocess.run(
            [sys.executable,
             str(gate_manifest.ROOT / "scripts" / "audit_summary.py"),
             "--manifest", str(manifest), "--only", "flaky",
             "--jobs", "2", "--no-cache", "--json", str(report)],
            cwd=gate_manifest.ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        attempts = int(counter.read_text())
        data = json.loads(report.read_text()) if report.is_file() else {}
        return result, attempts, data.get("gates", [{}])[0]

    def test_one_retry_succeeds_and_records_first_verdict(self):
        result, attempts, record = self.run_summary(1)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(attempts, 2)
        self.assertTrue(record["retried"])
        self.assertIn("FAIL", record["first_verdict"])
        self.assertIn("first_seconds", record)
        self.assertIn("sequential retry", record["verdict"])

    def test_two_failures_do_not_get_a_third_attempt(self):
        result, attempts, record = self.run_summary(2)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(attempts, 2)
        self.assertTrue(record["retried"])
        self.assertFalse(record["ok"])
        self.assertIn("FAIL", record["first_verdict"])
        self.assertIn("AUDIT_SUMMARY: FAIL", result.stdout)


if __name__ == "__main__":
    unittest.main()
