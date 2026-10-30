# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: asymmetric negative-X, tie and cross-edge controls for cubemap geometry.
from fractions import Fraction as Q
import unittest
from cubemap_geometry_check import direction,face_uv,source_check,check_dump


class CubemapGeometryTests(unittest.TestCase):
    def test_negative_x(self):
        self.assertEqual(face_uv(list(map(Q,[-2,1,1]))),(1,Q(3,4),Q(1,4)))

    def test_ties(self):
        self.assertEqual(face_uv(list(map(Q,[1,1,1])))[0],0)
        self.assertEqual(face_uv(list(map(Q,[0,-1,1])))[0],3)

    def test_edges(self):
        for f in range(6):
            d=direction(f,Q(9,8),Q(-3,8))
            selected,u,v=face_uv(d)
            self.assertNotEqual(selected,f)
            m=max(map(abs,d))
            self.assertEqual(tuple(x*m for x in direction(selected,2*u-1,2*v-1)),tuple(d))

    def test_invalid(self):
        with self.assertRaises(ValueError):
            face_uv([0,0,0])
        with self.assertRaises(ValueError):
            check_dump('')

    def test_source(self):
        source_check()


if __name__=='__main__':
    unittest.main()
