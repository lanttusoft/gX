#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free tests for the advisory cross-session bus: atomic posts,
#   claims/conflicts/expiry/release and scope matching. No worktree is touched.

import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import agent_bus


def identity(session):
    return {"session": session, "worktree": "/tmp/worktree",
        "branch": "feature/test"}


class AgentBusTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent_bus_test_")
        self.root = agent_bus.prepare_root(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_post_and_read(self):
        message = agent_bus.post_message(self.root, identity("alpha"),
            "review reflection", ["src/renderer/gfx/**"], "working")
        claims, messages = agent_bus.read_state(self.root)
        self.assertEqual(claims["claims"], [])
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0]["id"], message["id"])
        self.assertEqual(messages[0]["intent"], "review reflection")
        self.assertEqual(messages[0]["paths"], ["src/renderer/gfx/**"])

    def test_claim_conflict_and_release(self):
        result, conflicts, _ = agent_bus.add_claim(self.root,
            identity("alpha"), ["src/renderer/gfx/**"], "owner", 60, False)
        self.assertIsNotNone(result)
        self.assertEqual(conflicts, [])
        result, conflicts, _ = agent_bus.add_claim(self.root,
            identity("beta"), ["src/renderer/gfx/scene.c"], "", 60, False)
        self.assertIsNone(result)
        self.assertEqual(conflicts[0]["session"], "alpha")
        result, conflicts, _ = agent_bus.add_claim(self.root,
            identity("beta"), ["src/renderer/gfx/scene.c"], "", 60, True)
        self.assertIsNotNone(result)
        self.assertEqual(conflicts, [])
        removed = agent_bus.release_claims(self.root, identity("beta"),
            [], True)
        self.assertEqual(removed, 1)
        claims, _ = agent_bus.read_state(self.root)
        active = agent_bus.active_claims(claims, agent_bus.utc_now())
        self.assertEqual([claim["session"] for claim in active], ["alpha"])

    def test_expiry_and_prune(self):
        agent_bus.add_claim(self.root, identity("alpha"), ["src/x.c"],
            "", -1, False)
        claims, _ = agent_bus.read_state(self.root)
        self.assertEqual(agent_bus.active_claims(claims, agent_bus.utc_now()), [])
        self.assertEqual(agent_bus.prune_claims(self.root), 1)
        claims, _ = agent_bus.read_state(self.root)
        self.assertEqual(claims["claims"], [])

    def test_scope_matching(self):
        self.assertTrue(agent_bus.scope_matches("src/gui/**", "src/gui/a.c"))
        self.assertTrue(agent_bus.scope_matches("src/gui/**", "src/gui"))
        self.assertFalse(agent_bus.scope_matches("src/gui/**", "src/gfx/a.c"))
        self.assertTrue(agent_bus.scopes_overlap("src/gui/**", "src/gui/a.c"))
        self.assertFalse(agent_bus.scopes_overlap("src/gui/**", "src/gfx/**"))

    def test_concurrent_posts_are_complete_lines(self):
        script = str(Path(__file__).with_name("agent_bus.py"))
        def post(index):
            return subprocess.run([
                sys.executable, script, "--root", str(self.root),
                "--session", f"s{index}", "post", "--intent", f"m{index}"
            ], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=60)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(post, range(8)))
        for result in results:
            self.assertEqual(result.returncode, 0, result.stderr)
        _, messages = agent_bus.read_state(self.root)
        self.assertEqual(len(messages), 8)
        self.assertEqual(len({message["id"] for message in messages}), 8)
        self.assertEqual({message["intent"] for message in messages},
            {f"m{index}" for index in range(8)})

    def test_malformed_message_is_skipped(self):
        (self.root / "bus.jsonl").write_text(
            '{"schema":1,"id":"ok"}\nnot-json\n', encoding="utf-8")
        _, messages = agent_bus.read_state(self.root)
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0]["id"], "ok")


if __name__ == "__main__":
    unittest.main()
