#ai: positive and negative scalar/source controls for the postprocess proof model.
from fractions import Fraction as Q
import unittest
import postprocess_contract_check as c


class Contracts(unittest.TestCase):
    def test_matching_extent(self):
        for extent in (1,512,769,2048):
            for position in (Q(0),Q(1,4),Q(3,4),Q(1)):
                self.assertEqual(c.uv(extent*position,extent),position)

    def test_half_size_counterexample_and_repeat(self):
        self.assertEqual(c.uv(1536,1024),Q(3,2))
        self.assertNotEqual(c.uv(1536,1024),c.uv(1536,2048))
        self.assertEqual(c.uv(512,1024),c.uv(1536,1024)-1)

    def test_odd_size_not_exactly_double(self):
        ratio=c.uv(1025,512)
        self.assertNotEqual(ratio,2)
        self.assertGreater(ratio,2)

    def test_zero_extent_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'zero normalization'): c.uv(1,0)

    def test_current_source(self):
        c.source_check()

    def test_denominator_and_target_drift_detected(self):
        shader=(c.ROOT/'resources/shader/postprocess/blur_gauss.frag').read_text()
        with self.assertRaisesRegex(RuntimeError,'denominator'):
            c.source_check(shader=shader.replace('gl_FragCoord.xy / res','fragTexCoord'))
        source=(c.ROOT/'src/renderer/postprocess.c').read_text()
        with self.assertRaisesRegex(RuntimeError,'Target.texture.width'):
            c.source_check(text=source.replace('gX_U32 curW = Target.texture.width;', 'gX_U32 curW = maindata.renderWidth;'))


if __name__=='__main__': unittest.main()
