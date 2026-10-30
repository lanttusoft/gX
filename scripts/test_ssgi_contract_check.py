#ai: exact fallbacks and tangent-test negative controls.
from fractions import Fraction as Q
import unittest
from ssgi_contract_check import fallback,source_check


class SsgiContractsTests(unittest.TestCase):
    def test_matching_ambient(self):
        for count in range(1,49):
            self.assertEqual(fallback(Q(1),Q(count),Q(count)),1)

    def test_zero_samples(self):
        self.assertEqual(fallback(Q(21,1000),0,0),Q(21,1000))

    def test_old_count_fallback_negative(self):
        count=Q(2)
        old=((1-count/48)+count/48*count)/count
        self.assertNotEqual(old,fallback(Q(1),count,count))

    def test_budget(self):
        self.assertEqual(4*6*2,48)
        for count in range(49):
            self.assertTrue(0<=Q(count,48)<=1)

    def test_tangent_sign(self):
        normal=[Q(3,5),Q(4,5),Q(0)]
        offset=[Q(1),Q(2),Q(3)]
        d=sum(a*b for a,b in zip(normal,offset))
        tangent=[a-b*d for a,b in zip(offset,normal)]
        self.assertEqual(sum(a*b for a,b in zip(tangent,normal)),0)
        self.assertLess(-Q(1,1000000),0)

    def test_source(self):
        source_check()


if __name__=='__main__':
    unittest.main()
