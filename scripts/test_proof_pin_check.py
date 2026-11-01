#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: unit coverage for the gx-proofs pin manifest and tree checks.

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import proof_pin_check as pin


def make_tree(root, core=1, gfx=1, math=1):
    for family, count in (("GxProofs", core), ("GxProofsGfx", gfx),
                          ("GxMath", math)):
        directory = Path(root) / family
        directory.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            (directory / f"Module{index}.lean").write_text("theorem x : True := by trivial\n")


def make_pin(path, **overrides):
    document = {
        "schema": 1,
        "repo": "https://github.com/lanttusoft/gx-proofs",
        "tag": "v0.18.0-proofs",
        "commit": "0" * 40,
        "coreModules": 1,
        "fullModules": 3,
        "coreDeclarations": 10,
        "fullDeclarations": 30,
        "note": "test",
    }
    document.update(overrides)
    Path(path).write_text(json.dumps(document))
    return document


class PinTests(unittest.TestCase):
    def test_committed_pin(self):
        document = pin.load_pin(pin.DEFAULT_PIN)
        self.assertEqual(document["tag"], "v0.18.0-proofs")
        self.assertEqual(document["commit"],
                         "1760307f18ed8824afbafafa3e916afbd257f0f9")
        self.assertEqual(document["coreModules"], 52)
        self.assertEqual(document["fullModules"], 117)

    def test_missing_key(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pin.json"
            make_pin(path)
            document = json.loads(path.read_text())
            del document["commit"]
            path.write_text(json.dumps(document))
            with self.assertRaises(ValueError):
                pin.load_pin(path)

    def test_wrong_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pin.json"
            make_pin(path, schema=2)
            with self.assertRaises(ValueError):
                pin.load_pin(path)


class TreeTests(unittest.TestCase):
    def test_matching_source_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "proofs"
            make_tree(root)
            manifest = Path(directory) / "pin.json"
            make_pin(manifest)
            result = pin.check(root, manifest)
            self.assertEqual(result["total"], 3)
            self.assertIsNone(result["head"])

    def test_wrong_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "proofs"
            make_tree(root, core=2)
            manifest = Path(directory) / "pin.json"
            make_pin(manifest)
            with self.assertRaises(ValueError):
                pin.check(root, manifest)

    def test_missing_family(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "proofs"
            make_tree(root)
            (root / "GxMath" / "Module0.lean").unlink()
            (root / "GxMath").rmdir()
            manifest = Path(directory) / "pin.json"
            make_pin(manifest)
            with self.assertRaises(ValueError):
                pin.check(root, manifest)

    def test_git_head_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "proofs"
            make_tree(root)
            subprocess.run(["git", "-C", str(root), "init"], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(
                ["git", "-C", str(root), "-c", "user.email=gx@example.invalid",
                 "-c", "user.name=gX", "-c", "commit.gpgsign=false",
                 "commit", "-m", "fixture"],
                check=True, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            manifest = Path(directory) / "pin.json"
            make_pin(manifest)
            with self.assertRaises(ValueError):
                pin.check(root, manifest)


if __name__ == "__main__":
    unittest.main()
