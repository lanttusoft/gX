# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: strict compare, range/far mismatch and conditional accuracy negative controls.
from fractions import Fraction as Q
import unittest
from point_shadow_depth_check import blocked,error_bound,source_check,check_dump


class PointShadowDepthTests(unittest.TestCase):
    def test_strict_bias(self):
        self.assertFalse(blocked(Q(25,4),Q(5,4),Q(1,2),10))
        self.assertTrue(blocked(Q(25,4),1,Q(1,2),10))

    def test_clear_range(self):
        self.assertFalse(blocked(10,0,1,10))
        self.assertTrue(blocked(11,0,1,10))

    def test_far_mismatch(self):
        self.assertTrue(blocked(6,0,Q(1,2),10))
        self.assertFalse(blocked(6,0,Q(1,2),20))

    def test_error_margin(self):
        self.assertEqual(error_bound(Q(6),Q(61,10),Q(1,10),Q(5),Q(51,100),Q(10)),Q(1,5))

    def test_pcf_bounds(self):
        for count in range(6):
            taps=[Q(1)]*count+[Q(0)]*(5-count)
            self.assertTrue(0<=sum(taps)/5<=1)

    def test_invalid_and_source(self):
        with self.assertRaises(ValueError):
            blocked(1,0,1,0)
        with self.assertRaises(ValueError):
            check_dump('')
        source_check()


if __name__=='__main__':
    unittest.main()
