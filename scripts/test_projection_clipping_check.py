#ai: exact clipping and mixed-w negative controls.
from fractions import Fraction as Q
import unittest

from projection_clipping_check import check_dump,clip_polygon,determinant,lerp,near,projected_area


class ProjectionClippingTests(unittest.TestCase):
    def test_mixed_w_counterexample(self):
        a,b,c=[-1,-1,0,-1],[1,-1,0,1],[0,1,0,1]
        self.assertGreater(determinant(a,b,c),0)
        self.assertLess(projected_area(*[list(map(Q,p)) for p in (a,b,c)]),0)

    def test_intersection_and_fan(self):
        a,b,c=list(map(Q,[-1,-1,-2,1])),list(map(Q,[1,-1,0,1])),list(map(Q,[0,1,0,1]))
        s,t=-near(a)/(near(b)-near(a)),-near(a)/(near(c)-near(a))
        ab,ac=lerp(a,b,s),lerp(a,c,t)
        self.assertEqual(near(ab),0)
        self.assertEqual(determinant(ab,b,c),(1-s)*determinant(a,b,c))
        self.assertEqual(determinant(ab,c,ac),s*(1-t)*determinant(a,b,c))
        self.assertEqual(len(clip_polygon([a,b,c],near)),4)

    def test_empty_and_zero_w(self):
        self.assertEqual(clip_polygon([],near),[])
        with self.assertRaises(ValueError):
            projected_area([1,1,0,0],[1,0,0,1],[0,1,0,1])

    def test_missing_dump_rejected(self):
        with self.assertRaises(ValueError):
            check_dump("")


if __name__=="__main__":
    unittest.main()
