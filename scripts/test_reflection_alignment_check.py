#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: display-free negative controls for the reflection-alignment ownership
#   parser; the GPU oracle itself is exercised by make reflection-alignment-check.

import unittest

import reflection_alignment_check as check


GOOD = (
    "SHOWCASE-Y2K: mirror planar=1 receiveSSR=0\n"
    "GFX_PLAN feature=planar-reflections cost=2 passes=reflections\n"
)


class ReflectionAlignmentCheckTests(unittest.TestCase):
    def test_good_log_passes(self):
        self.assertEqual(check.check_ownership(GOOD), [])

    def test_missing_mirror_flags_fail(self):
        errors = check.check_ownership(
            "GFX_PLAN feature=planar-reflections\n")
        self.assertTrue(any("mirror" in error for error in errors))

    def test_missing_planar_feature_fails(self):
        errors = check.check_ownership(
            "SHOWCASE-Y2K: mirror planar=1 receiveSSR=0\n"
            "GFX_PLAN feature=ssr cost=2\n")
        self.assertTrue(any("planar-reflection" in error for error in errors))

    def test_ssr_enabled_fails(self):
        errors = check.check_ownership(GOOD + "GFX_PLAN feature=ssr cost=2\n")
        self.assertTrue(any("SSR" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
