#!/usr/bin/env python3
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
