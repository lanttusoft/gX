#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: private bug-status operations preserve JSON content and default legacy reports to OPEN.
from pathlib import Path
import tempfile
import unittest

from bug_status import initialize, read_report, set_status


class BugStatusTests(unittest.TestCase):
    def test_initialize_and_transition_preserve_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            legacy = root / 'report-1-0.json'
            legacy.write_text('{\n  "title":"legacy",\n  "diagnostics":{"camera":{"x":1}}\n}\n')
            fixed = root / 'report-2-0.json'
            fixed.write_text('{\n  "status_code": 1,\n  "title":"already fixed"\n}\n')
            changed, total = initialize(root)
            self.assertEqual((changed, total), (1, 2))
            document, code = read_report(legacy)
            self.assertEqual(code, 0)
            self.assertEqual(document['diagnostics'], {'camera': {'x': 1}})
            self.assertEqual(read_report(fixed)[1], 1)
            previous, code = set_status(legacy, 'FIXED')
            self.assertEqual((previous, code), (0, 1))
            self.assertEqual(read_report(legacy)[1], 1)
            previous, code = set_status(legacy, '0')
            self.assertEqual((previous, code), (1, 0))
            self.assertEqual(read_report(legacy)[1], 0)

    def test_single_space_status_is_replaced_without_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report-3-0.json'
            path.write_text('{\n "status_code": 0,\n "title":"single-space"\n}\n')
            previous, code = set_status(path, 'FIXED')
            self.assertEqual((previous, code), (0, 1))
            text = path.read_text()
            self.assertEqual(text.count('"status_code"'), 1)
            self.assertEqual(read_report(path)[1], 1)

    def test_duplicate_status_lines_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report-4-0.json'
            path.write_text('{\n "status_code": 0,\n "status_code": 1,\n "title":"duplicate"\n}\n')
            with self.assertRaises(ValueError):
                read_report(path)

    def test_invalid_status_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report-1-0.json'
            path.write_text('{\n  "title":"invalid"\n}\n')
            with self.assertRaises(ValueError):
                set_status(path, 'CLOSED')
            path.write_text('{\n  "status_code": 2,\n  "title":"invalid"\n}\n')
            with self.assertRaises(ValueError):
                read_report(path)


if __name__ == '__main__':
    unittest.main()
