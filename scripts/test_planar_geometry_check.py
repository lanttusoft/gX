#ai: convention negative controls and source formula drift checks, not compiler/GPU proofs.
from fractions import Fraction as Q
from pathlib import Path
import unittest

from planar_geometry_check import check_dump, close, project, reflect

ROOT = Path(__file__).resolve().parents[1]


class PlanarGeometryTests(unittest.TestCase):
    def test_exact_unit_geometry(self):
        n, d, p = [Q(3, 5), Q(4, 5), 0], Q(5, 4), [Q(3), Q(5), Q(7)]
        self.assertEqual(reflect(reflect(p, n, d), n, d), p)
        point = [Q(3, 4), Q(1), Q(-3, 5)]
        self.assertEqual(reflect(point, n, d), point)

    def test_nonunit_and_up_offset_negative(self):
        n, p = [2, 0, 0], [1, 0, 0]
        self.assertNotEqual(reflect(reflect(p, n, 0), n, 0), p)
        self.assertNotEqual(reflect([0, 1, 0], [0, 1, 0], 0), reflect([0, 1, 0], [0, 1, 0], 1))

    def test_y_flip_and_bad_data_rejected(self):
        uv = project([Q(2), Q(1), Q(0), Q(4)])
        self.assertEqual(uv, [Q(3, 4), Q(5, 8)])
        with self.assertRaises(ValueError):
            close([uv[1]], [1-uv[1]])
        with self.assertRaises(ValueError):
            check_dump("")

    def test_camera_and_shader_source_formulas(self):
        text = (ROOT / "src/renderer/gfx/draw.c").read_text()
        self.assertIn("dPos = gX_F32v3_Dot(camPos, n) - planeDist", text)
        self.assertIn("dTgt = gX_F32v3_Dot(camTgt, n) - planeDist", text)
        self.assertIn("dUp  = gX_F32v3_Dot(camUp, n);", text)
        self.assertIn("mirrorCam.position = gX_F32v3_Sub(camPos, gX_F32v3_MulScalar(n, 2.0f * dPos))", text)
        self.assertIn("mirrorCam.target   = gX_F32v3_Sub(camTgt, gX_F32v3_MulScalar(n, 2.0f * dTgt))", text)
        self.assertIn("mirrorCam.up       = gX_F32v3_Sub(camUp,  gX_F32v3_MulScalar(n, 2.0f * dUp))", text)
        self.assertIn("gX_Matrix_Multiply(reflView, reflProj)", text)
        ssr = (ROOT / "resources/shader/gfx_ssr.frag").read_text()
        self.assertIn("vec3 wm = wp - planeNormal * (2.0 * dn)", ssr)
        self.assertIn("if (pc.w <= 0.001)", ssr)
        self.assertIn("vec2((ndcA.x + 1.0) * 0.5, (ndcA.y + 1.0) * 0.5)", ssr)
        direct = (ROOT / "resources/shader/standard.frag").read_text()
        self.assertIn("float projW = max(reflClip.w, 0.0001)", direct)
        self.assertIn("vec2 puv = reflClip.xy / projW * 0.5 + 0.5", direct)


if __name__ == "__main__":
    unittest.main()
