# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: boundary, degenerate split and overlap negative controls.
from fractions import Fraction as Q
import unittest
from csm_contract_check import select,smooth,mix,source_check,check_dump


class CsmContractTests(unittest.TestCase):
    def test_exact_boundaries(self):
        for distance,expected in [(15,0),(50,1),(120,2)]:
            self.assertEqual(select(4,[15,50,120],distance),expected)

    def test_counts(self):
        for count in range(1,5):
            for d in [0,16,51,121]:
                self.assertLess(select(count,[15,50,120],d),count)

    def test_equal_and_disabled(self):
        self.assertEqual(select(4,[10,10,10],11),3)
        self.assertEqual(select(4,[0,50,120],121),0)

    def test_fade(self):
        self.assertEqual(smooth(0),0)
        self.assertEqual(smooth(1),1)
        self.assertNotEqual(mix(0,1,smooth(Q(1,2))),mix(1,0,smooth(0)))

    def test_missing_and_source(self):
        with self.assertRaises(ValueError):
            check_dump('')
        source_check()


if __name__=='__main__':
    unittest.main()
