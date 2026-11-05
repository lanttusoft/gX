#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: disposable-repository controls for inventory completeness and notes safety.
import json
import os
from pathlib import Path
import tempfile
import unittest

import worktree_inventory as tracking


class ParserTests(unittest.TestCase):
    def test_nul_records_preserve_paths_and_lock_reasons(self):
        rows = tracking.parse_worktrees("worktree /owned/a path\nwith newline\0HEAD abc\0"
            "branch refs/heads/topic\0locked active review\0\0"
            "worktree /owned/detached\0HEAD def\0detached\0prunable missing path\0\0")
        self.assertEqual(rows[0]["path"], "/owned/a path\nwith newline")
        self.assertEqual(rows[0]["locked"], "active review")
        self.assertIn("detached", rows[1])
        self.assertIn("prunable", rows[1])

    def test_record_without_path_fails(self):
        with self.assertRaises(ValueError):
            tracking.parse_worktrees("HEAD abc\0")

    def test_markdown_cells_cannot_inject_rows(self):
        self.assertEqual(tracking.cell("a|b\nc\rd"), "a\\|b c d")


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="gx_worktree_test_", dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.root = self.directory / "repo"
        self.root.mkdir()
        tracking.git(self.root, "init", "-q", "-b", "main")
        tracking.git(self.root, "config", "user.name", "Owned test fixture")
        tracking.git(self.root, "config", "user.email", "fixture@example.invalid")
        tracking.git(self.root, "config", "commit.gpgsign", "false")
        tracking.git(self.root, "config", "core.hooksPath", str(self.root / ".git/empty-hooks"))
        (self.root / "fixture.txt").write_text("owned fixture\n")
        tracking.git(self.root, "add", "fixture.txt")
        tracking.git(self.root, "commit", "-qm", "owned fixture")

    def linked_tree(self):
        path = self.directory / "unrelated directory name"
        tracking.git(self.root, "worktree", "add", "-q", "-b", "feature/actual-name", str(path))
        return path

    def test_branches_without_worktrees_are_included(self):
        tracking.git(self.root, "branch", "unattached")
        data = tracking.inventory(self.root, "main")
        self.assertEqual({row["branch"] for row in data["branches"]}, {"main", "unattached"})
        branch = next(row for row in data["branches"] if row["branch"] == "unattached")
        self.assertEqual(branch["worktrees"], [])
        self.assertEqual(branch["unique_commits"], 0)
        self.assertTrue(all(row["cleanup_decision"] == "unreviewed"
                            for row in data["branches"] + data["worktrees"]))

    def test_shared_location_and_actual_branch_identity(self):
        path = self.linked_tree()
        data = tracking.inventory(path, "main")
        self.assertEqual(data["common_dir"], str(self.root / ".git"))
        current = next(row for row in data["worktrees"] if row["current"])
        self.assertEqual(current["path"], str(path))
        self.assertEqual(current["branch"], "refs/heads/feature/actual-name")
        self.assertEqual(current["state"], "clean")

    def test_dirty_locked_and_missing_trees_are_not_lost(self):
        path = self.linked_tree()
        (path / "untracked.txt").write_text("owned work\n")
        tracking.git(self.root, "worktree", "lock", "--reason", "active review", str(path))
        data = tracking.inventory(self.root, "main")
        row = next(row for row in data["worktrees"] if row["path"] == str(path))
        self.assertEqual(row["state"], "dirty")
        self.assertEqual(row["locked"], "active review")
        path.rename(self.directory / "moved fixture")
        data = tracking.inventory(self.root, "main")
        row = next(row for row in data["worktrees"] if row["path"] == str(path))
        self.assertEqual(row["state"], "missing")
        self.assertEqual(row["cleanup_decision"], "unreviewed")

    def test_unique_commits_are_compared_to_explicit_base(self):
        path = self.linked_tree()
        (path / "fixture.txt").write_text("owned feature change\n")
        tracking.git(path, "add", "fixture.txt")
        tracking.git(path, "commit", "-qm", "owned feature")
        data = tracking.inventory(self.root, "main")
        branch = next(row for row in data["branches"] if row["branch"] == "feature/actual-name")
        self.assertEqual(branch["unique_commits"], 1)
        self.assertEqual(branch["behind_base"], 0)

    def test_reused_path_is_not_reported_as_a_clean_owned_worktree(self):
        path = self.linked_tree()
        path.rename(self.directory / "moved fixture")
        path.mkdir()
        tracking.git(path, "init", "-q", "-b", "unrelated")
        data = tracking.inventory(self.root, "main")
        row = next(row for row in data["worktrees"] if row["path"] == str(path))
        self.assertEqual(row["state"], "unavailable")
        self.assertIn("another Git repository", row["error"])

    def test_refresh_preserves_notes_refs_and_worktree_state(self):
        before = tracking.git(self.root, "show-ref")
        data = tracking.inventory(self.root, "main")
        output = tracking.write_inventory(data)
        notes = output / "notes.md"
        notes.write_text("# Owned note: keep this branch\n")
        tracking.write_inventory(tracking.inventory(self.root, "main"))
        self.assertEqual(notes.read_text(), "# Owned note: keep this branch\n")
        self.assertEqual(tracking.git(self.root, "show-ref"), before)
        self.assertEqual(tracking.git(self.root, "status", "--porcelain"), "")
        self.assertEqual(json.loads((output / "inventory.json").read_text())["schema"], 1)
        self.assertIn("no cleanup authorization", (output / "inventory.md").read_text())

    def test_unknown_base_fails_without_creating_reports(self):
        with self.assertRaises(RuntimeError):
            tracking.inventory(self.root, "not-a-ref")
        self.assertFalse((self.root / ".git/gx-workstreams").exists())


if __name__ == "__main__":
    unittest.main()
