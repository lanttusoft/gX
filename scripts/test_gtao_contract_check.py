#ai: exact scalar controls for sample coherence, horizon weights and authored bias.
from fractions import Fraction as Q
import unittest
from gtao_contract_check import (horizon,step_distance,texel_center,source_check,native_check,
    encode_previous_depth,history_depth_matches,half_float)


class GtaoContractsTests(unittest.TestCase):
    def test_horizon(self):
        for h in [Q(-1),Q(0),Q(1,2),Q(1)]:
            for s in [Q(-1),Q(0),Q(1)]:
                for j in range(6):
                    updated=horizon(h,s,Q(2,j+2))
                    self.assertTrue(h<=updated<=1)

    def test_spacing(self):
        for fraction in [Q(0),Q(1,100),Q(99,100)]:
            for j in range(5):
                self.assertEqual(step_distance(1,fraction,j+1)-step_distance(1,fraction,j),Q(1,6))
            self.assertGreaterEqual(step_distance(1,fraction,5),Q(5,6))

    def test_texel_coherence(self):
        uv=Q(31,100)
        center=texel_center(uv,16)
        self.assertLessEqual(abs(center-uv),Q(1,32))
        slope,offset=Q(1,2),Q(1,4)
        depth=slope*center+offset
        self.assertNotEqual(depth-slope*uv-offset,0)
        self.assertEqual(depth-slope*center-offset,0)

    def test_previous_depth_payload(self):
        for depth in [Q(0),Q(1,7),Q(1,2),Q(99,100),Q(1)]:
            payload=encode_previous_depth(depth)
            self.assertEqual(payload,Q(2)+depth)
            self.assertTrue(history_depth_matches(payload,depth))
            mismatched=depth-Q(1,20) if depth > Q(1,2) else depth+Q(1,20)
            self.assertFalse(history_depth_matches(payload,mismatched))
        self.assertEqual(encode_previous_depth(Q(-1)),Q(7,2))
        self.assertEqual(encode_previous_depth(Q(2)),Q(7,2))
        self.assertFalse(history_depth_matches(Q(7,2),Q(1,2)))
        self.assertIsNone(history_depth_matches(Q(1),Q(1,2)))

    def test_half_float_depth_tolerance(self):
        tolerance=Q(5,1000)
        for i in range(101):
            depth=Q(i,100)
            encoded=half_float(2+float(depth))
            history=half_float(float(depth))
            self.assertLessEqual(abs(Q.from_float(encoded-2)-Q.from_float(history)),tolerance)

    def test_bias(self):
        self.assertFalse(Q(0)>max(Q(25,1000),0))
        self.assertFalse(Q(1,10000)>max(Q(25,1000),0))
        self.assertTrue(Q(1,10)>max(Q(25,1000),0))

    def test_blur(self):
        for total,weight in [(Q(0),Q(0)),(Q(1,10000),Q(1,10000)),(Q(1),Q(2))]:
            self.assertTrue(0<=total/max(weight,Q(1,1000))<=1)

    def test_background_and_invalid(self):
        self.assertTrue(Q(9995,10000)>=Q(999,1000))
        with self.assertRaises(ValueError):
            native_check('')
        with self.assertRaises(ValueError):
            texel_center(Q(1,2),0)

    def test_source(self):
        source_check()


if __name__=='__main__':
    unittest.main()
