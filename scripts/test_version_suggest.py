#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: version policy tests — changelog classification, file signals and
#   the declared-class/numeric-bump agreement.

import unittest

import version_check as check
import version_suggest as suggest


class ChangelogTests(unittest.TestCase):
    def classify(self, text):
        categories = suggest.parse_changelog(text)
        return suggest.max_level(suggest.changelog_findings(categories))

    def test_added_feature_is_minor(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "### Added\n"
                "- **Color-code 8-colour mode**: opt-in palette ID 3.\n")
        self.assertEqual(self.classify(text), "minor")

    def test_added_diagnostic_is_minor(self):
        text = ("## [0.24.1] - 2026-10-31\n\n"
                "### Added\n"
                "- **Empty scene preset**: editor button and diagnostic preset.\n")
        self.assertEqual(self.classify(text), "minor")

    def test_added_policy_tooling_is_patch(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "### Added\n"
                "- **Version policy and change classifier**: docs and script.\n")
        self.assertEqual(self.classify(text), "patch")

    def test_multiline_tooling_is_patch(self):
        text = ("## [0.27.0] - 2026-10-31\n\n"
                "### Added\n"
                "- **Gate manifest**: `gates.toml` is the source of\n"
                "  truth; `scripts/gates.py` adds list and plan.\n")
        self.assertEqual(self.classify(text), "patch")

    def test_model_does_not_match_mode(self):
        text = ("## [0.26.3] - 2026-10-31\n\n"
                "### Changed\n"
                "- **Planar triage**: the model reflects in the mirror.\n")
        self.assertEqual(self.classify(text), "patch")

    def test_changed_capacity_is_minor(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "### Changed\n"
                "- **Payload cap**: raised to 56 KiB.\n")
        self.assertEqual(self.classify(text), "minor")

    def test_fixed_is_patch(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "### Fixed\n"
                "- **Null guard**: avoid a crash.\n")
        self.assertEqual(self.classify(text), "patch")

    def test_removed_is_major(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "### Removed\n"
                "- **Legacy API**: deleted.\n")
        self.assertEqual(self.classify(text), "major")


class FileTests(unittest.TestCase):
    def test_new_source_is_minor(self):
        findings = suggest.file_findings([("A", ["src/common/new_mode.c"])])
        self.assertEqual(suggest.max_level(findings), "minor")

    def test_new_resource_is_patch(self):
        findings = suggest.file_findings([("A", ["resources/tools/empty.json"])])
        self.assertEqual(suggest.max_level(findings), "patch")

    def test_deleted_header_is_major(self):
        findings = suggest.file_findings([("D", ["src/common/old.h"])])
        self.assertEqual(suggest.max_level(findings), "major")


class CheckTests(unittest.TestCase):
    def test_numeric_class(self):
        self.assertEqual(
            check.numeric_class((0, 25, 0, ""), (0, 26, 0, "")), "minor")
        self.assertEqual(
            check.numeric_class((0, 26, 0, ""), (0, 26, 1, "")), "patch")
        self.assertEqual(
            check.numeric_class((1, 0, 0, ""), (2, 0, 0, "")), "major")
        self.assertEqual(
            check.numeric_class((0, 26, 0, ""), (0, 26, 0, "rc1")), "variant")

    def test_stacked_release_baseline(self):
        versions = [(0, 27, 0, ""), (0, 26, 0, ""), (0, 25, 0, "")]
        self.assertEqual(
            check.release_baseline((0, 27, 0, ""), (0, 25, 0, ""), versions),
            (0, 26, 0, ""))
        self.assertEqual(
            check.release_baseline((0, 26, 0, ""), (0, 25, 0, ""), versions),
            (0, 25, 0, ""))

    def test_declared_class(self):
        text = ("## [0.26.0] - 2026-10-31\n\n"
                "Release-Class: minor\n\n"
                "### Changed\n- cap\n")
        self.assertEqual(check.release_class((0, 26, 0, ""), text), "minor")

    def test_under_bump_rejected(self):
        self.assertFalse(suggest.check("minor", "patch", 0))

    def test_over_bump_allowed(self):
        self.assertTrue(suggest.check("patch", "minor", 0))

    def test_variant_allowed(self):
        self.assertTrue(suggest.check("major", "variant", 0))

    def test_zero_x_breaking_allows_minor(self):
        self.assertTrue(suggest.check("major", "minor", 0))

    def test_zero_x_breaking_rejects_patch(self):
        self.assertFalse(suggest.check("major", "patch", 0))


if __name__ == "__main__":
    unittest.main()
