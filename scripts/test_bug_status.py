#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: private bug-status operations preserve JSON content and default legacy reports to OPEN.
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from bug_status import filter_reports, initialize, is_feature, load_reports, main, read_report, set_status, summarize


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

    def test_filters_and_summary_are_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'report-1-0.json').write_text(
                '{\n  "status_code": 0,\n  "title":"[FEATURE REQUEST] export",\n'
                '  "severity":"Low",\n  "type":"Other"\n}\n')
            (root / 'report-2-0.json').write_text(
                '{\n  "status_code": 1,\n  "title":"fixed crash",\n'
                '  "severity":"High",\n  "type":"Rendering"\n}\n')
            (root / 'report-3-0.json').write_text(
                '{\n  "status_code": 0,\n  "title":"open smear",\n'
                '  "severity":"Medium",\n  "type":"Rendering"\n}\n')
            rows = load_reports(root)
            self.assertEqual([path.name for path, _, _ in filter_reports(rows, status=0)],
                             ['report-1-0.json', 'report-3-0.json'])
            self.assertEqual([path.name for path, _, _ in filter_reports(rows, severity='high')],
                             ['report-2-0.json'])
            self.assertEqual([path.name for path, _, _ in filter_reports(rows, type_name='rendering')],
                             ['report-2-0.json', 'report-3-0.json'])
            self.assertEqual([path.name for path, _, _ in filter_reports(rows, feature=True)],
                             ['report-1-0.json'])
            self.assertEqual([path.name for path, _, _ in filter_reports(rows, feature=False)],
                             ['report-2-0.json', 'report-3-0.json'])
            self.assertTrue(is_feature({'title': ' [Feature Request] x '}))
            self.assertFalse(is_feature({'title': 'feature request without tag'}))
            summary = summarize(root)
            self.assertEqual(summary['total'], 3)
            self.assertEqual((summary['open'], summary['fixed']), (2, 1))
            self.assertEqual((summary['feature_requests'], summary['open_feature_requests']), (1, 1))
            self.assertEqual(summary['by_severity'], {'High': 1, 'Low': 1, 'Medium': 1})
            self.assertEqual(summary['open_by_severity'], {'Low': 1, 'Medium': 1})
            self.assertEqual(summary['by_type'], {'Other': 1, 'Rendering': 2})
            self.assertEqual(summary['open_by_type'], {'Other': 1, 'Rendering': 1})
            self.assertEqual(read_report(root / 'report-1-0.json')[1], 0)

    def test_cli_list_filters_and_summary_json(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'report-1-0.json').write_text(
                '{\n  "status_code": 0,\n  "title":"[FEATURE REQUEST] export",\n'
                '  "severity":"Low",\n  "type":"Other"\n}\n')
            (root / 'report-2-0.json').write_text(
                '{\n  "status_code": 1,\n  "title":"fixed crash",\n'
                '  "severity":"High",\n  "type":"Rendering"\n}\n')
            output = io.StringIO()
            with patch('sys.argv', ['bug_status.py', '--directory', str(root),
                                    'list', '--status', 'OPEN', '--no-feature']):
                with redirect_stdout(output):
                    main()
            self.assertNotIn('export', output.getvalue())
            self.assertNotIn('fixed crash', output.getvalue())
            self.assertIn('BUG_STATUS: 0 of 2 reports', output.getvalue())
            output = io.StringIO()
            with patch('sys.argv', ['bug_status.py', '--directory', str(root),
                                    'summary', '--json']):
                with redirect_stdout(output):
                    main()
            result = json.loads(output.getvalue())
            self.assertEqual((result['total'], result['open'], result['fixed']), (2, 1, 1))
            self.assertEqual(result['feature_requests'], 1)



if __name__ == '__main__':
    unittest.main()
