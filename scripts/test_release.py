#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I040 — release-orchestrator contract tests; no writes or publication.
import io
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

import release


class ReleaseOrchestratorTests(unittest.TestCase):
    def test_version_parse_and_format(self):
        version = release.parse_version("v0.44.1")
        self.assertEqual(version, (0, 44, 1, ""))
        self.assertEqual(release.format_version(version), "0.44.1")
        self.assertEqual(release.parse_version("v1.2.3_rc1"),
                         (1, 2, 3, "rc1"))

    def test_previous_tag_is_below_target(self):
        self.assertEqual(release.previous_tag((0, 44, 1, "")), "v0.44.0")

    def test_mirror_plan_does_not_write(self):
        files = {
            "readme.txt": "header\n\nv0.44.0 (beta)\n",
            "README.md": "- Version: see `readme.txt` (`v0.44.0` beta).\n",
            "src/common/data.c": 'x = "0.44.0 beta";\n',
            "src/gui/templates/titlegx.c": '"gX ENGINE  /  v0.44.0 BETA"\n',
            "src/gui/templates/settings.c": '"v0.44.0 BETA"\n',
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            with mock.patch.object(release, "ROOT", root):
                plan = release.mirror_plan(release.parse_version("0.44.1"))
            self.assertEqual(len(plan), 5)
            for path, _old, new in plan:
                self.assertIn("0.44.1", new)
                self.assertNotIn("0.44.1", path.read_text())

    def test_owner_commands_keep_publication_explicit(self):
        output = io.StringIO()
        with redirect_stdout(output):
            release.owner_commands(release.parse_version("0.44.1"),
                                   "/tmp/notes.md")
        text = output.getvalue()
        self.assertIn("git tag -s v0.44.1", text)
        self.assertIn("git push --atomic origin main refs/tags/v0.44.1", text)
        self.assertIn("gh release create v0.44.1", text)
        self.assertIn("--verify-tag", text)

    def test_changelog_section_matches_declared_class(self):
        section, declared = release.changelog_section(
            release.parse_version("0.44.1"))
        self.assertIsNotNone(section)
        self.assertEqual(declared, "patch")


if __name__ == "__main__":
    unittest.main()
