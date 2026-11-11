#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I018 evidence stamp matching/rejection controls; timestamps are checked
#   for ordering but revision/input hashes decide freshness.
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import evidence_stamp


class EvidenceStampChecks(unittest.TestCase):
    def setUp(self):
        self.inputs = {name: "a" * 64 for name in evidence_stamp.REQUIRED_INPUTS}
        self.git = {"revision": "rev-1", "worktree": None}
        self.lean = evidence_stamp.make_stamp("lean", "PASS", {"modules": 60},
            utc="2026-01-01T00:00:00Z", git=self.git, inputs=self.inputs)
        self.c = evidence_stamp.make_stamp("c", "PASS", {"gates": 2},
            utc="2026-01-01T00:00:10Z", git=self.git, inputs=self.inputs)

    def test_matching_stamps_report_order(self):
        result = evidence_stamp.compare_stamps(self.lean, self.c)
        self.assertEqual(result["revision"], "rev-1")
        self.assertEqual(result["delta_seconds"], 10.0)
        self.assertEqual(result["inputs"], self.inputs)

    def test_revision_input_result_and_kind_mismatches_fail(self):
        cases = [
            ("revision", lambda stamp: stamp["git"].update(
                revision="rev-2")),
            ("worktree", lambda stamp: stamp["git"].update(worktree="dirty")),
            ("input", lambda stamp: stamp["inputs"].update(
                {"gates.toml": "b" * 64})),
            ("result", lambda stamp: stamp.update(result="FAIL")),
            ("kind", lambda stamp: stamp.update(kind="lean")),
        ]
        for label, mutate in cases:
            with self.subTest(label=label):
                broken = json.loads(json.dumps(self.c))
                mutate(broken)
                with self.assertRaises(ValueError):
                    evidence_stamp.compare_stamps(self.lean, broken)

    def test_required_input_missing_fails(self):
        broken = json.loads(json.dumps(self.c))
        del broken["inputs"]["gates.toml"]
        with self.assertRaises(ValueError):
            evidence_stamp.compare_stamps(self.lean, broken)

    def test_stamp_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stamps" / "c.json"
            evidence_stamp.write_stamp(path, self.c)
            loaded = evidence_stamp.read_stamp(path)
            self.assertEqual(loaded, self.c)

    def test_invalid_utc_is_rejected(self):
        with self.assertRaises(ValueError):
            evidence_stamp.make_stamp("lean", "PASS", utc="2026-01-01 00:00:00",
                git=self.git, inputs=self.inputs)

    def git_fixture(self, directory):
        root = Path(directory)

        def git(*arguments):
            subprocess.run(["git", "-C", str(root), *arguments],
                           check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)

        git("init", "-q")
        git("config", "user.email", "evidence@example.invalid")
        git("config", "user.name", "Evidence Fixture")
        #ai: disposable fixtures never inherit the author's commit signing.
        git("config", "commit.gpgsign", "false")
        git("config", "tag.gpgsign", "false")
        (root / "tracked.txt").write_text("tracked\n", encoding="utf-8")
        git("add", "tracked.txt")
        git("commit", "-q", "-m", "fixture")
        return root

    def test_untracked_content_changes_worktree_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.git_fixture(directory)
            untracked = root / "guest.h"
            untracked.write_text("#define VALUE 1\n", encoding="utf-8")
            first = evidence_stamp.git_state(root)
            self.assertIsNotNone(first["worktree"])
            untracked.write_text("#define VALUE 2\n", encoding="utf-8")
            second = evidence_stamp.git_state(root)
            self.assertNotEqual(first["worktree"], second["worktree"])
            untracked.write_text("#define VALUE 2\n", encoding="utf-8")
            third = evidence_stamp.git_state(root)
            self.assertEqual(second["worktree"], third["worktree"])


if __name__ == "__main__":
    unittest.main()
