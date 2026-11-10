#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: GX-I036/GX-I039 evidence-manifest contract tests. Covers weighted
#   for/against confidence, volatility, null versus passive/active zero,
#   independence/freshness discounting and validation failures.
import unittest

import evidence_manifest as em


def item(level, direction, weight, independent=True, fresh=True):
    return {
        "level": level,
        "direction": direction,
        "weight": weight,
        "artifact": "scripts/test_evidence_manifest.py",
        "independent": independent,
        "fresh": fresh,
    }


def claim(evidence, assessed=True, **extra):
    value = {
        "id": "claim",
        "claim": "synthetic claim",
        "assessed": assessed,
        "evidence": evidence,
    }
    value.update(extra)
    return value


class EvidenceManifestTests(unittest.TestCase):
    def test_passive_zero(self):
        result = em.compute_claim(claim([]))
        self.assertEqual(result["status"], "passive-zero")
        self.assertEqual(result["confidence"], 0.0)
        self.assertEqual(result["volatility"], 0.0)

    def test_null_unassessed(self):
        result = em.compute_claim(claim([], assessed=False))
        self.assertEqual(result["status"], "not-assessed")
        self.assertIsNone(result["confidence"])
        self.assertIsNone(result["volatility"])

    def test_active_zero(self):
        result = em.compute_claim(claim([
            item("formal", "for", 60),
            item("formal", "against", 60),
        ]))
        self.assertEqual(result["status"], "active-zero")
        self.assertEqual(result["confidence"], 0.0)
        self.assertEqual(result["volatility"], 100.0)

    def test_weighted_confidence(self):
        result = em.compute_claim(claim([
            item("formal", "for", 80),
            item("formal", "against", 20),
        ]))
        self.assertEqual(result["confidence"], 60.0)
        self.assertEqual(result["volatility"], 40.0)

    def test_dependent_and_stale_discount(self):
        result = em.compute_claim(claim([
            item("formal", "for", 80, independent=False),
            item("formal", "against", 40),
        ]))
        self.assertEqual(result["for_weight"], 40.0)
        self.assertEqual(result["against_weight"], 40.0)
        self.assertEqual(result["confidence"], 0.0)
        self.assertEqual(result["volatility"], 100.0)

    def test_level_cap_rejected(self):
        data = {"schema": 1, "claims": [claim([
            item("unit", "for", 80),
        ])]}
        with self.assertRaises(ValueError):
            em.validate_manifest(data)

    def test_duplicate_id_rejected(self):
        data = {"schema": 1, "claims": [claim([]), claim([])]}
        with self.assertRaises(ValueError):
            em.validate_manifest(data)

    def test_unassessed_evidence_rejected(self):
        data = {"schema": 1, "claims": [claim([
            item("unit", "for", 10),
        ], assessed=False)]}
        with self.assertRaises(ValueError):
            em.validate_manifest(data)

    def test_manual_confidence_rejected(self):
        data = {"schema": 1, "claims": [claim([], confidence=50)]}
        with self.assertRaises(ValueError):
            em.validate_manifest(data)

    def test_markdown_has_claim_and_nulls(self):
        report = {
            "schema": 1,
            "claims": [
                em.compute_claim(claim([])),
                em.compute_claim(claim([], assessed=False)),
            ],
        }
        text = em.render_markdown(report)
        self.assertIn("`claim`", text)
        self.assertIn("passive-zero", text)
        self.assertIn("not-assessed", text)


if __name__ == "__main__":
    unittest.main()
