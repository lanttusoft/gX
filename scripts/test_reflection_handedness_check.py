#ai: orientation/winding negatives and renderer camera/culling source drift checks.
from fractions import Fraction as Q
from pathlib import Path
import unittest

from reflection_handedness_check import area, axes, orientation, reflect

ROOT = Path(__file__).resolve().parents[1]


class ReflectionHandednessTests(unittest.TestCase):
    def test_raw_reflection_and_rebuilt_axes(self):
        original = [[Q(1),0,0],[0,Q(1),0],[0,0,Q(1)]]
        n = [Q(3,5),Q(4,5),0]
        raw = [reflect(a,n,0) for a in original]
        self.assertEqual(orientation(raw), -1)
        rebuilt = [[-x for x in raw[0]],raw[1],raw[2]]
        self.assertEqual(orientation(rebuilt), 1)
        self.assertNotEqual(raw[0],rebuilt[0])

    def test_selected_horizontal_winding(self):
        points = [[Q(1,10),Q(1,5)],[Q(7,10),Q(3,10)],[Q(1,5),Q(4,5)]]
        mirrored = [[1-x,y] for x,y in points]
        self.assertEqual(area(mirrored), -area(points))
        self.assertNotEqual(area(mirrored)>0, area(points)>0)
        self.assertEqual(area([[0,0],[1,1],[2,2]]), 0)

    def test_basis_column_order(self):
        self.assertEqual(axes(list(range(16))), [[0,4,8],[1,5,9],[2,6,10]])
        self.assertNotEqual(axes(list(range(16)))[0], [0,1,2])

    def test_source_cross_order_and_ccw_restoration(self):
        matrix = (ROOT / "src/gxmath/matrix.c").read_text()
        self.assertIn("up.y*vz.z - up.z*vz.y",matrix)
        self.assertIn("vz.y*vx.z - vz.z*vx.y",matrix)
        draw = (ROOT / "src/renderer/gfx/draw.c").read_text()
        self.assertIn("gX_RHI_FrontFace(gX_RHI_CCW)",draw)
        self.assertIn("gX_RHI_FrontFace((gX_U32)prevFrontFace)",draw)


if __name__ == "__main__":
    unittest.main()
