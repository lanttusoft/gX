#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: SPDX SBOM contract tests — document identity, license vocabulary,
#   deterministic rendering and staleness detection.

import json
import unittest

import sbom


class SbomTests(unittest.TestCase):
    def test_document_identity(self):
        document = sbom.document()
        self.assertEqual(document["spdxVersion"], "SPDX-2.3")
        self.assertEqual(document["packages"][0]["name"], "gX")
        self.assertEqual(document["packages"][0]["versionInfo"],
                         sbom.version_string())

    def test_ids_are_unique(self):
        packages = sbom.document()["packages"]
        ids = [package["SPDXID"] for package in packages]
        self.assertEqual(len(ids), len(set(ids)))

    def test_licenses_are_known(self):
        for package in sbom.document()["packages"]:
            self.assertIn(package["licenseDeclared"], sbom.ALLOWED_LICENSES,
                          package["name"])

    def test_render_is_deterministic(self):
        self.assertEqual(sbom.render(), sbom.render())

    def test_committed_document_is_current(self):
        self.assertEqual(sbom.OUTPUT.read_text(encoding="utf-8"), sbom.render(),
                         "run python3 scripts/sbom.py --write")

    def test_validation_rejects_wrong_version(self):
        document = sbom.document()
        document["packages"][0]["versionInfo"] = "0.0.0"
        issues = sbom.validate(json.dumps(document))
        self.assertTrue(any("version" in issue for issue in issues))

    def test_validation_rejects_unknown_license(self):
        document = sbom.document()
        document["packages"][1]["licenseDeclared"] = "LicenseRef-Unknown"
        issues = sbom.validate(json.dumps(document))
        self.assertTrue(any("unexpected license" in issue for issue in issues))


if __name__ == "__main__":
    unittest.main()
