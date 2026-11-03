#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: secret-scan rule tests. Credential samples are assembled at runtime so
#   this tracked test file never contains a scannable token itself.

import tempfile
import unittest
from pathlib import Path

import secret_scan


def fake_private_key():
    return "-----BEGIN " + "RSA " + "PRIVATE KEY-----"


def fake_github_token():
    return "gh" + "p_" + "a" * 36


def fake_aws_key():
    return "AK" + "IA" + "A" * 16


def fake_google_key():
    return "AI" + "za" + "A" * 35


def fake_slack_token():
    return "xo" + "xb-" + "a" * 12


def fake_stripe_key():
    return "sk_" + "live_" + "a" * 20


def fake_jwt():
    return "ey" + "J" + "a" * 12 + "." + "b" * 12 + "." + "c" * 12


def fake_private_path():
    return "/mnt/" + "Data4/private/evidence.log"


CASES = (
    ("private-key", fake_private_key()),
    ("github-token", fake_github_token()),
    ("aws-access-key", fake_aws_key()),
    ("google-api-key", fake_google_key()),
    ("slack-token", fake_slack_token()),
    ("stripe-key", fake_stripe_key()),
    ("jwt", fake_jwt()),
    ("private-path", fake_private_path()),
)


class RuleTests(unittest.TestCase):
    def test_clean_text(self):
        text = ("gX release notes\n"
                "Set GX_FRAMES=24 and run ./build/gX_build --test-only.\n")
        self.assertEqual(secret_scan.scan_text(text), [])

    def test_each_rule_fires(self):
        for rule, token in CASES:
            with self.subTest(rule=rule):
                findings = secret_scan.scan_text("value: " + token)
                self.assertTrue(findings, rule)
                self.assertEqual(findings[0][1], rule)

    def test_allowlist_marker(self):
        text = "key: " + fake_github_token() + "  # pragma: allowlist secret"
        self.assertEqual(secret_scan.scan_text(text), [])

    def test_generic_secret_and_placeholder(self):
        real = 'api_key = "' + "s3cr3tV4lue" + '"'
        placeholder = 'api_key = "' + "changeme123" + '"'
        self.assertEqual(secret_scan.scan_text(real)[0][1], "generic-secret")
        self.assertEqual(secret_scan.scan_text(placeholder), [])

    def test_findings_do_not_echo_values(self):
        findings = secret_scan.scan_text("token: " + fake_aws_key())
        self.assertTrue(findings)
        self.assertEqual(len(findings[0]), 2)

    def test_binary_file_is_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "blob.bin"
            path.write_bytes(b"\0" + fake_github_token().encode())
            self.assertEqual(secret_scan.scan_file(path), [])

    def test_text_file_is_scanned(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "notes.txt"
            path.write_text("token " + fake_stripe_key() + "\n")
            self.assertEqual(secret_scan.scan_file(path)[0][1], "stripe-key")


if __name__ == "__main__":
    unittest.main()
