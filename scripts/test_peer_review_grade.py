#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

"""Contract tests for the peer-review grade bands and 7/10 gate."""

import unittest

import peer_review_grade as review


class BandTests(unittest.TestCase):
    def test_band_edges(self):
        cases = {
            10.0: "S+", 9.5: "S+", 9.4: "S", 9.0: "S",
            8.7: "A+", 8.3: "A", 8.0: "A-", 7.7: "B+",
            7.3: "B", 7.0: "B-", 6.7: "C+", 6.3: "C",
            6.0: "C-", 5.7: "D+", 5.3: "D", 5.0: "D-",
            4.9: "F", 0.0: "F",
        }
        for score, expected in cases.items():
            self.assertEqual(review.grade(score), expected, score)

    def test_out_of_range_clamps(self):
        self.assertEqual(review.grade(-1.0), "F")
        self.assertEqual(review.grade(11.0), "S+")


class GateTests(unittest.TestCase):
    def test_merge_gate_requires_all_three(self):
        passed, rows = review.gate([7.0, 8.2, 6.9])
        self.assertFalse(passed)
        self.assertEqual([row[2] for row in rows], ["B-", "A-", "C+"])
        self.assertEqual([row[3] for row in rows], [True, True, False])

    def test_target_is_eight(self):
        passed, _ = review.gate([8.0, 8.0, 8.0])
        self.assertTrue(passed)

    def test_parse_rejects_bad_values(self):
        with self.assertRaises(ValueError):
            review.parse_scores([7.0, 8.0])
        with self.assertRaises(ValueError):
            review.parse_scores([7.0, 8.0, 10.1])
        with self.assertRaises(ValueError):
            review.parse_scores([7.0, 8.0, 8.0, 8.0], "unknown")

    def test_test_profile(self):
        self.assertEqual(review.PROFILES["test"],
                         ("contract", "coverage", "determinism", "diagnostics"))
        passed, rows = review.gate([7.5, 8.0, 7.0, 7.8], profile="test")
        self.assertTrue(passed)
        self.assertEqual([row[2] for row in rows],
                         ["B", "A-", "B-", "B+"])
        passed, _ = review.gate([7.5, 8.0, 7.0, 6.9], profile="test")
        self.assertFalse(passed)

    def test_profile_rejects_wrong_count(self):
        with self.assertRaises(ValueError):
            review.parse_scores([7.5, 8.0, 7.0], "test")
        with self.assertRaises(ValueError):
            review.gate([7.5, 8.0, 7.0], profile="test")


if __name__ == "__main__":
    unittest.main()
