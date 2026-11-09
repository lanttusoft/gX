#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: disposable-root controls for GX-I023 discovery, sanitization, ledger
#   preservation, drift, archive-plan linkage and read-only safety.
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

import agent_bus
import repo_ecosystem as eco
import worktree_inventory as inv


ENGINE_FILES = {
    "AGENTS.md": "owned fixture guide\n",
    "gates.toml": "[gate]\nname = \"owned\"\n",
    "proofs.lock.json": '{"repo": "https://example.invalid/proofs", '
                        '"tag": "v1", "commit": "0123456789abcdef"}\n',
    "src/fixture.c": "int fixture;\n",
    "resources/style/theme.json": "{}\n",
}


class EcosystemTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("GX_SCRATCH", "/tmp/opencode"))
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="gx_ecosystem_test_",
                                                dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.root = self.directory / "root"
        self.root.mkdir()
        self.eco = self.directory / "eco"
        self.bus = self.directory / "bus"
        self.env = {**os.environ, "GX_AGENT_BUS": str(self.bus)}
        self.engine = self.make_repo(self.root / "engine", ENGINE_FILES,
                                     "https://user:secret@github.com/"
                                     "example/gX.git")
        self.engine_wt = self.root / "engine-wt"
        inv.git(self.engine, "worktree", "add", "-q", "-b", "feature/two",
                str(self.engine_wt))
        inv.git(self.engine, "branch", "feature/no-worktree")
        inv.git(self.engine, "tag", "-m", "owned fixture tag", "v1.0")
        self.clone = self.root / "clone"
        inv.git(self.directory, "clone", "-q", str(self.engine), str(self.clone))
        inv.git(self.clone, "remote", "set-url", "origin",
                "ssh://git@github.com:22/example/gX.git")
        self.proofs = self.make_repo(self.root / "proofs", {
            "GxProofs/Fixture.lean": "theorem fixture : True := trivial\n",
            "lakefile.toml": "name = \"fixture\"\n",
            "lean-toolchain": "leanprover/lean4:fixture\n",
        }, "https://github.com/example/gx-proofs.git")
        self.lineage = self.make_repo(self.root / "gameX" / "legacy", {
            "src/old.c": "int old;\n",
        })
        self.candidate = self.make_repo(self.root / "gX-old", {
            "gates.toml": "[gate]\nname = \"owned candidate\"\n",
            "src/old.c": "int old;\n",
        })
        self.unrelated = self.make_repo(self.root / "my-gX-notes", {
            "notes.txt": "unrelated notes\n",
        })

    def make_repo(self, path, files, remote=None):
        path.mkdir(parents=True, exist_ok=True)
        inv.git(path, "init", "-q", "-b", "main")
        inv.git(path, "config", "user.name", "Owned test fixture")
        inv.git(path, "config", "user.email", "fixture@example.invalid")
        inv.git(path, "config", "commit.gpgsign", "false")
        #ai: fixture tags must not inherit the host's GPG signing configuration.
        inv.git(path, "config", "tag.gpgsign", "false")
        inv.git(path, "config", "core.hooksPath",
                str(self.directory / "empty-hooks"))
        for relative, text in files.items():
            target = path / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        inv.git(path, "add", "-A")
        inv.git(path, "commit", "-qm", "owned fixture")
        if remote:
            inv.git(path, "remote", "add", "origin", remote)
        return path

    def common(self, path):
        value = Path(inv.git(path, "rev-parse", "--git-common-dir").strip())
        if not value.is_absolute():
            value = Path(path) / value
        return value.resolve()

    def snapshot(self, max_depth=4):
        return eco.snapshot(root=self.root, max_depth=max_depth,
                            ecosystem_dir=self.eco, env=self.env)

    def group(self, data, common_dir):
        return next(row for row in data["repository_groups"]
                    if row["common_dir"] == str(common_dir))

    def tree_digest(self, path):
        digest = {}
        for directory, _names, files in os.walk(path):
            for name in files:
                target = Path(directory) / name
                relative = str(target.relative_to(path))
                try:
                    if target.is_symlink():
                        digest[relative] = "symlink:" + os.readlink(target)
                    else:
                        digest[relative] = hashlib.sha256(
                            target.read_bytes()).hexdigest()
                except OSError:
                    digest[relative] = "unreadable"
        return digest

    def test_discovery_groups_worktrees_clones_proofs_lineage_and_unrelated(self):
        data = self.snapshot()
        engine = self.group(data, self.common(self.engine))
        clone = self.group(data, self.common(self.clone))
        proofs = self.group(data, self.common(self.proofs))
        lineage = self.group(data, self.common(self.lineage))
        candidate = self.group(data, self.common(self.candidate))
        unrelated = self.group(data, self.common(self.unrelated))

        self.assertEqual(engine["kind"], "engine")
        self.assertEqual(engine["checkout_count"], 2)
        self.assertEqual(engine["relation_counts"],
                         {"primary": 1, "worktree": 1})
        self.assertIn(str(self.engine), engine["checkouts"])
        self.assertIn(str(self.engine_wt), engine["checkouts"])
        self.assertEqual(clone["kind"], "engine")
        self.assertEqual(clone["checkout_count"], 1)
        self.assertNotEqual(engine["common_dir"], clone["common_dir"])
        self.assertEqual(proofs["kind"], "proofs")
        self.assertEqual(lineage["kind"], "lineage/reference")
        self.assertEqual(candidate["kind"], "candidate/unreviewed")
        self.assertEqual(unrelated["kind"], "unrelated")

        remote = next(row for row in data["remote_groups"]
                      if row["remote"] == "github.com/example/gx")
        self.assertEqual(sorted(remote["repository_groups"]),
                         sorted([engine["common_dir"], clone["common_dir"]]))
        self.assertEqual(remote["kinds"], ["engine"])
        self.assertIn(unrelated["common_dir"],
                      [row["common_dir"] for row in data["unrelated"]])
        self.assertNotIn(unrelated["primary_path"],
                         [row["path"] for row in data["repositories"]
                          if row["kind"] != "unrelated"])

        branches = {row["branch"]: row for row in engine["branches"]}
        self.assertIn("feature/no-worktree", branches)
        self.assertEqual(branches["feature/no-worktree"]["worktrees"], [])
        self.assertEqual(branches["feature/two"]["worktrees"],
                         [str(self.engine_wt)])
        self.assertEqual(engine["tag_count"], 1)
        self.assertEqual(data["summary"]["linked_worktrees"], 1)

    def test_remotes_are_sanitized_and_grouped_without_credentials(self):
        data = self.snapshot()
        serialized = json.dumps(data)
        rendered = eco.markdown(data)
        for forbidden in ("secret", "user@", "git@", "token="):
            self.assertNotIn(forbidden, serialized)
            self.assertNotIn(forbidden, rendered)
        self.assertIn("github.com/example/gx", serialized)
        engine = self.group(data, self.common(self.engine))
        self.assertEqual(engine["remote_keys"], ["github.com/example/gx"])
        self.assertTrue(engine["remotes"])
        for urls in engine["remotes"].values():
            for url in urls:
                self.assertNotIn("@", url)

    def test_ledger_is_created_once_and_never_overwritten(self):
        data = self.snapshot()
        written = eco.write_report(data, self.eco, history=True)
        ledger = self.eco / "ledger.md"
        self.assertTrue(ledger.is_file())
        self.assertIn(str(ledger), written)
        ledger.write_text("# Owned ledger\n\nKeep this exact text.\n")
        original = ledger.read_text()
        snapshots_before = sorted((self.eco / "snapshots").glob("*.json"))

        (self.engine_wt / "new-untracked.txt").write_text("owned change\n")
        second = self.snapshot()
        eco.write_report(second, self.eco, history=True)

        self.assertEqual(ledger.read_text(), original)
        snapshots_after = sorted((self.eco / "snapshots").glob("*.json"))
        self.assertEqual(len(snapshots_after), len(snapshots_before) + 1)
        stored = json.loads((self.eco / "snapshot.json").read_text())
        worktree = next(row for row in stored["repositories"]
                        if row["path"] == str(self.engine_wt))
        self.assertEqual(worktree["state"], "dirty")
        self.assertEqual(worktree["untracked"], 1)

    def test_drift_reports_changes_without_writing_a_snapshot(self):
        baseline = self.snapshot()
        eco.write_report(baseline, self.eco)
        before = (self.eco / "snapshot.json").read_bytes()
        unchanged = eco.drift(root=self.root, ecosystem_dir=self.eco,
                              env=self.env)
        self.assertTrue(unchanged["unchanged"])

        (self.clone / "drift.txt").write_text("owned drift\n")
        inv.git(self.engine, "branch", "feature/drift-only")
        changed = eco.drift(root=self.root, ecosystem_dir=self.eco,
                            env=self.env)
        self.assertFalse(changed["unchanged"])
        self.assertIn(str(self.clone), [row["path"] for row in changed["changed"]])
        fields = next(row for row in changed["changed"]
                      if row["path"] == str(self.clone))["fields"]
        self.assertEqual(fields["state"]["to"], "dirty")
        group_change = next(
            row for row in changed["changed_groups"]
            if row["common_dir"] == str(self.common(self.engine)))
        self.assertIn("branches", group_change["fields"])
        self.assertIn("~", eco.drift_text(changed))
        self.assertEqual((self.eco / "snapshot.json").read_bytes(), before)

    def test_archive_plan_links_collection_and_excludes_unrelated(self):
        bus = agent_bus.prepare_root(self.bus)
        claim, conflicts, _now = agent_bus.add_claim(
            bus, {"session": "eco-test", "worktree": str(self.engine_wt),
                  "branch": "feature/two"},
            [str(self.engine_wt)], "owned active claim", 60, False)
        self.assertIsNotNone(claim)
        self.assertEqual(conflicts, [])
        plan = eco.archive_plan(root=self.root, ecosystem_dir=self.eco,
                                env=self.env)
        self.assertTrue(plan["plan_only"])
        self.assertIn("PLAN ONLY", eco.plan_text(plan))
        command_text = json.dumps(plan)
        self.assertIn("worktree_collection.py", command_text)
        self.assertIn("archive", command_text)
        self.assertIn("prune-plan", command_text)
        self.assertIn(str(self.eco / "archives"), command_text)
        for forbidden in ("--force", " -D ", "git clean", "reset --hard"):
            self.assertNotIn(forbidden, command_text)
        engine = next(item for item in plan["plan"]
                      if item["common_dir"] == str(self.common(self.engine)))
        self.assertEqual(engine["disposition"], "review-required")
        self.assertEqual(engine["linked_idea"], "GX-I022")
        self.assertIn("eco-test", " ".join(engine["review"]))
        self.assertTrue(any("active agent claim" in reason
                            for reason in engine["review"]))
        unrelated = self.common(self.unrelated)
        self.assertNotIn(str(unrelated),
                         [item["common_dir"] for item in plan["plan"]])
        self.assertIn(str(unrelated),
                      [item["common_dir"] for item in plan["excluded"]])
        self.assertFalse((self.eco / "archives").exists())

    def test_report_and_drift_do_not_change_refs_index_or_worktrees(self):
        baseline = self.snapshot()
        eco.write_report(baseline, self.eco)
        repositories = (self.engine, self.engine_wt, self.clone, self.proofs,
                        self.lineage, self.candidate, self.unrelated)
        before = {}
        for repository in repositories:
            before[repository] = {
                "refs": inv.git(repository, "show-ref"),
                "status": inv.git(repository, "status", "--porcelain",
                                  "--untracked-files=all"),
                "worktrees": inv.git(repository, "worktree", "list",
                                     "--porcelain", "-z"),
                "tree": self.tree_digest(repository),
            }
        snapshot = self.snapshot()
        eco.markdown(snapshot)
        drift = eco.drift(root=self.root, ecosystem_dir=self.eco,
                          env=self.env)
        eco.drift_text(drift)
        plan = eco.archive_plan(root=self.root, ecosystem_dir=self.eco,
                                env=self.env)
        eco.plan_text(plan)
        for repository in repositories:
            self.assertEqual(inv.git(repository, "show-ref"),
                             before[repository]["refs"], str(repository))
            self.assertEqual(inv.git(repository, "status", "--porcelain",
                                     "--untracked-files=all"),
                             before[repository]["status"], str(repository))
            self.assertEqual(inv.git(repository, "worktree", "list",
                                     "--porcelain", "-z"),
                             before[repository]["worktrees"], str(repository))
            self.assertEqual(self.tree_digest(repository),
                             before[repository]["tree"], str(repository))

    def test_discovery_depth_and_excluded_directories(self):
        deep = self.make_repo(self.root / "deep" / "a" / "b" / "repo", {
            "src/a.c": "int deep;\n",
        })
        self.make_repo(self.root / "vendor" / "hidden-engine", ENGINE_FILES)
        self.make_repo(self.root / "build" / "hidden-engine", ENGINE_FILES)
        os.symlink(self.engine, self.root / "engine-link")

        shallow = self.snapshot(max_depth=2)
        shallow_paths = [row["path"] for row in shallow["repositories"]]
        self.assertNotIn(str(deep), shallow_paths)
        self.assertGreaterEqual(shallow["skipped"]["counts"].get("depth", 0), 1)
        self.assertGreaterEqual(shallow["skipped"]["counts"].get("vendor", 0), 1)
        self.assertGreaterEqual(shallow["skipped"]["counts"].get("build", 0), 1)
        self.assertGreaterEqual(shallow["skipped"]["counts"].get("symlink", 0), 1)
        self.assertNotIn(str(self.root / "vendor" / "hidden-engine"),
                         shallow_paths)

        full = self.snapshot(max_depth=4)
        full_paths = [row["path"] for row in full["repositories"]]
        self.assertIn(str(deep), full_paths)
        self.assertNotIn(str(self.root / "vendor" / "hidden-engine"),
                         full_paths)
        self.assertNotIn(str(self.root / "build" / "hidden-engine"),
                         full_paths)
        self.assertNotIn(str(self.root / "engine-link"), full_paths)

    def test_active_claim_marks_checkout_group_and_branch_active(self):
        bus = agent_bus.prepare_root(self.bus)
        agent_bus.add_claim(
            bus, {"session": "eco-active", "worktree": str(self.engine_wt),
                  "branch": "feature/two"},
            [str(self.engine_wt)], "owned active claim", 60, False)
        data = self.snapshot()
        worktree = next(row for row in data["repositories"]
                        if row["path"] == str(self.engine_wt))
        engine = self.group(data, self.common(self.engine))
        self.assertTrue(worktree["active"])
        self.assertEqual(worktree["lifecycle"], "active")
        self.assertEqual(engine["lifecycle"], "active")
        self.assertEqual(engine["active_sessions"], ["eco-active"])
        branch = next(row for row in engine["branches"]
                      if row["branch"] == "feature/two")
        self.assertEqual(branch["lifecycle"], "active")
        self.assertIn("eco-active", data["active_sessions"])


if __name__ == "__main__":
    unittest.main()
