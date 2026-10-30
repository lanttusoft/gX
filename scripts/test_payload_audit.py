#!/usr/bin/env python3
#ai: lock payload-audit parsing to GNU binutils output shapes without requiring a build.
import unittest

from payload_audit import parse_nm_posix, parse_size_sysv, payload_metrics


#ai: verify exact sections are independent of non-gated relocation sections.
class PayloadAuditTests(unittest.TestCase):
    def test_size_sysv_sections_and_lean_payload(self):
        sections = parse_size_sysv('''
binary :
section size addr
.text 100 0
.rodata 20 100
.data 10 120
.data.rel.ro 5 130
.got 7 135
.bss 30 200
Total 172
''')
        report = payload_metrics(sections, 900)
        self.assertEqual(report['payload_bytes'], 130)
        self.assertEqual(report['bss_bytes'], 30)
        self.assertEqual(report['binary_bytes'], 900)
        self.assertEqual(report['extra_initialized_sections'],
                         {'.data.rel.ro': 5, '.got': 7})

    def test_nm_posix_symbols(self):
        symbols = parse_nm_posix('''
function T 1000 2a
local_function t 102a 10
object D 2000 8
undefined U 0 0
''')
        self.assertEqual([(s.name, s.kind, s.address, s.size) for s in symbols], [
            ('function', 'T', 0x1000, 0x2a),
            ('local_function', 't', 0x102a, 0x10),
            ('object', 'D', 0x2000, 0x8),
            ('undefined', 'U', 0, 0),
        ])


if __name__ == '__main__':
    unittest.main()
