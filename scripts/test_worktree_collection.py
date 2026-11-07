#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: disposable-repository controls for archive determinism and plan-only safety.
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest

import agent_bus
import worktree_collection as collection
import worktree_inventory as inv


class CollectionTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="gx_collection_test_",
                                                dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.root = self.directory / "repo"
        self.root.mkdir()
        inv.git(self.root, "init", "-q", "-b", "main")
        inv.git(self.root, "config", "user.name", "Owned test fixture")
        inv.git(self.root, "config", "user.email", "fixture@example.invalid")
        inv.git(self.root, "config", "commit.gpgsign", "false")
        inv.git(self.root, "config", "core.hooksPath",
                str(self.root / ".git/empty-hooks"))
        (self.root / "fixture.txt").write_text("owned fixture\n")
        inv.git(self.root, "add", "fixture.txt")
        inv.git(self.root, "commit", "-qm", "owned fixture")
        self.bus = agent_bus.prepare_root(self.directory / "bus")
        os.environ["GX_AGENT_BUS"] = str(self.bus)
        self.addCleanup(os.environ.pop, "GX_AGENT_BUS", None)

    def worktree(self, branch, path_name=None, detach=False):
        path = self.directory / (path_name or branch.replace("/", "_"))
        if detach:
            inv.git(self.root, "worktree", "add", "-q", "--detach", str(path), "main")
        else:
            inv.git(self.root, "worktree", "add", "-q", "-b", branch, str(path))
        return path

    def commit(self, path, text, message):
        (path / "fixture.txt").write_text(text)
        inv.git(path, "add", "fixture.txt")
        inv.git(path, "commit", "-qm", message)

    def archive(self, name="archive"):
        output = self.directory / name
        collection.archive(self.root, "main", output)
        return output

    def checksums(self, output):
        values = {}
        for line in (output / "SHA256SUMS").read_text().splitlines():
            digest, relative = line.split("  ", 1)
            values[relative] = digest
        return values

    def test_archive_contains_verified_bundles_patches_and_listings(self):
        unique = self.worktree("feature/unique")
        self.commit(unique, "unique change\n", "unique change")
        detached = self.worktree("detached", "detached", detach=True)
        self.commit(detached, "detached change\n", "detached change")
        (unique / "tracked.txt").write_text("staged\n")
        inv.git(unique, "add", "tracked.txt")
        (unique / "tracked.txt").write_text("unstaged\n")
        (unique / "untracked.txt").write_text("untracked\n")
        (unique / "ignored.txt").write_text("ignored\n")
        (unique / ".gitignore").write_text("ignored.txt\n")
        notes = Path(inv.git(self.root, "rev-parse", "--git-common-dir").strip())
        if not notes.is_absolute():
            notes = self.root / notes
        notes = notes / "gx-workstreams" / "notes.md"
        notes.parent.mkdir(parents=True, exist_ok=True)
        notes.write_text("# Owned notes\ntest\n")

        output = self.archive()
        self.assertTrue((output / "repository.bundle").is_file())
        detach_bundles = list(output.glob("detached-*.bundle"))
        self.assertEqual(len(detach_bundles), 1)
        self.assertTrue((output / "inventory.json").is_file())
        self.assertTrue((output / "inventory.md").is_file())
        self.assertTrue((output / "manifest.json").is_file())
        self.assertTrue((output / "manifest.md").is_file())
        self.assertTrue((output / "active-claims.json").is_file())
        self.assertTrue((output / "notes.md").is_file())
        self.assertTrue(list(output.glob("*-tracked.patch")))
        self.assertTrue(list(output.glob("*-cached.patch")))
        self.assertTrue(list(output.glob("*-untracked.txt")))
        self.assertTrue(list(output.glob("*-ignored.txt")))
        sums = self.checksums(output)
        for relative, digest in sums.items():
            self.assertEqual(hashlib.sha256((output / relative).read_bytes()).hexdigest(),
                             digest)
        for bundle in list(output.glob("*.bundle")):
            inv.git(self.root, "bundle", "verify", str(bundle))
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertIn("feature/unique", manifest["groups"]["unmerged"])
        self.assertTrue(manifest["bundles"])

    def test_archive_is_deterministic_for_bundles_patches_and_listings(self):
        unique = self.worktree("feature/unique")
        self.commit(unique, "unique change\n", "unique change")
        (unique / "untracked.txt").write_text("untracked\n")
        first = self.archive("first")
        second = self.archive("second")
        for pattern in ("*.bundle", "*-tracked.patch", "*-cached.patch",
                        "*-untracked.txt", "*-ignored.txt", "*-status.txt"):
            first_files = sorted(path.name for path in first.glob(pattern))
            second_files = sorted(path.name for path in second.glob(pattern))
            self.assertEqual(first_files, second_files, pattern)
            for name in first_files:
                self.assertEqual((first / name).read_bytes(),
                                 (second / name).read_bytes(), name)

    def test_archive_refuses_a_nonempty_output(self):
        output = self.directory / "not-empty"
        output.mkdir()
        (output / "keep.txt").write_text("owned\n")
        with self.assertRaises(RuntimeError):
            collection.archive(self.root, "main", output)
        self.assertEqual((output / "keep.txt").read_text(), "owned\n")

    def test_prune_plan_only_prints_safe_candidates(self):
        clean = self.worktree("feature/clean")
        dirty = self.worktree("feature/dirty")
        (dirty / "untracked.txt").write_text("owned work\n")
        unique = self.worktree("feature/unique")
        self.commit(unique, "unique change\n", "unique change")
        inv.git(self.root, "branch", "feature/merged")
        active = self.worktree("feature/active")
        identity = {"session": "owned-test", "worktree": str(active),
                    "branch": "feature/active"}
        result, conflicts, _now = agent_bus.add_claim(
            self.bus, identity, [str(active)], "owned active fixture", 60, False)
        self.assertIsNotNone(result)
        self.assertEqual(conflicts, [])

        before_refs = inv.git(self.root, "show-ref")
        before_status = inv.git(self.root, "status", "--porcelain")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            collection.prune_plan(self.root, "main")
        text = output.getvalue()
        self.assertIn("PLAN ONLY", text)
        self.assertIn("git branch -d feature/merged", text)
        self.assertIn("git worktree remove", text)
        self.assertIn("feature/clean", text)
        for name in ("feature/dirty", "feature/unique", "feature/active"):
            self.assertNotIn(f"branch -d {name}", text)
            self.assertIn(name, text)
        for forbidden in (" -D ", " --force", "reset --hard", "git clean"):
            self.assertNotIn(forbidden, text)
        self.assertEqual(inv.git(self.root, "show-ref"), before_refs)
        self.assertEqual(inv.git(self.root, "status", "--porcelain"),
                         before_status)
        self.assertTrue(clean.is_dir())

        # JSON path: call directly without touching refs.
        json_output = io.StringIO()
        with contextlib.redirect_stdout(json_output):
            collection.prune_plan(self.root, "main", as_json=True)
        plan = json.loads(json_output.getvalue())
        flattened = [" ".join(command) for command in plan["commands"]]
        self.assertTrue(any("feature/merged" in command for command in flattened))
        self.assertFalse(any("feature/unique" in command for command in flattened))
        self.assertFalse(any("feature/dirty" in command for command in flattened))
        self.assertFalse(any("feature/active" in command for command in flattened))

    def test_active_claim_is_grouped_and_blocks_prune(self):
        active = self.worktree("feature/active")
        (active / "untracked.txt").write_text("owned work\n")
        identity = {"session": "owned-test", "worktree": str(active),
                    "branch": "feature/active"}
        agent_bus.add_claim(self.bus, identity, [str(active)], "owned", 60, False)
        data = inv.inventory(self.root, "main")
        claims = collection.load_claims(Path(data["common_dir"]))
        collection.annotate(data, claims)
        self.assertIn("owned-test", data["groups"]["active"])
        row = next(row for row in data["worktrees"] if row["path"] == str(active))
        self.assertTrue(row["active"])
        json_output = io.StringIO()
        with contextlib.redirect_stdout(json_output):
            collection.prune_plan(self.root, "main", as_json=True)
        plan = json.loads(json_output.getvalue())
        flattened = [" ".join(command) for command in plan["commands"]]
        self.assertFalse(any("feature/active" in command for command in flattened))
        self.assertTrue(any("feature/active" in item.get("branch", "") or
                            "feature/active" in item.get("worktree", "")
                            for item in plan["review"]))


if __name__ == "__main__":
    unittest.main()
