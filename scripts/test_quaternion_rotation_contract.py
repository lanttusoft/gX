#!/usr/bin/env python3
#ai: pin C/Lean quaternion/vector/matrix conventions without claiming a verified translation.
from pathlib import Path
import unittest

from test_color_units_contract import c_body, compact, lean_body

ROOT = Path(__file__).resolve().parents[1]


def check_contract(quaternion, vector, lean, matrix, macros):
    expected_rotate = """
        gX_F32v3 result = {0};
        result.x = v.x*(q.x*q.x + q.w*q.w - q.y*q.y - q.z*q.z) + v.y*(2*q.x*q.y - 2*q.w*q.z) + v.z*(2*q.x*q.z + 2*q.w*q.y);
        result.y = v.x*(2*q.w*q.z + 2*q.x*q.y) + v.y*(q.w*q.w - q.x*q.x + q.y*q.y - q.z*q.z) + v.z*(-2*q.w*q.x + 2*q.y*q.z);
        result.z = v.x*(-2*q.w*q.y + 2*q.x*q.z) + v.y*(2*q.w*q.x + 2*q.y*q.z) + v.z*(q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z);
        return result;
    """
    expected_matrix = """
        gX_Matrix r = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        gX_F32 a2 = q.x*q.x, b2 = q.y*q.y, c2 = q.z*q.z;
        gX_F32 ac = q.x*q.z, ab = q.x*q.y, bc = q.y*q.z;
        gX_F32 ad = q.w*q.x, bd = q.w*q.y, cd = q.w*q.z;
        r.m0 = 1 - 2*(b2 + c2); r.m1 = 2*(ab + cd); r.m2 = 2*(ac - bd);
        r.m4 = 2*(ab - cd); r.m5 = 1 - 2*(a2 + c2); r.m6 = 2*(bc + ad);
        r.m8 = 2*(ac + bd); r.m9 = 2*(bc - ad); r.m10 = 1 - 2*(a2 + b2);
        return r;
    """
    expected_inverse = """
        gX_F32Qua r = q;
        gX_F32 lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
        if (lenSq != 0.0f) {
            gX_F32 invLen = 1.0f / lenSq;
            r.x *= -invLen; r.y *= -invLen; r.z *= -invLen; r.w *= invLen;
        }
        return r;
    """
    expected_product = """
        gX_F32Qua r = {0};
        gX_F32 q1x = q1.x, q1y = q1.y, q1z = q1.z, q1w = q1.w;
        gX_F32 q2x = q2.x, q2y = q2.y, q2z = q2.z, q2w = q2.w;
        r.x = q1x*q2w + q1w*q2x + q1y*q2z - q1z*q2y;
        r.y = q1y*q2w + q1w*q2y + q1z*q2x - q1x*q2z;
        r.z = q1z*q2w + q1w*q2z + q1x*q2y - q1y*q2x;
        r.w = q1w*q2w - q1x*q2x - q1y*q2y - q1z*q2z;
        return r;
    """
    expected_lerp = """
        gX_F32Qua r = {0};
        r.x = q1.x + amount*(q2.x - q1.x);
        r.y = q1.y + amount*(q2.y - q1.y);
        r.z = q1.z + amount*(q2.z - q1.z);
        r.w = q1.w + amount*(q2.w - q1.w);
        return r;
    """
    expected_normalize = """
        gX_F32Qua r = {0};
        gX_F32 len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        if (len == 0.0f) len = 1.0f;
        gX_F32 ilen = 1.0f / len;
        r.x = q.x * ilen; r.y = q.y * ilen; r.z = q.z * ilen; r.w = q.w * ilen;
        return r;
    """
    expected_nlerp = """
        gX_F32Qua r = gX_F32Qua_Lerp(q1, q2, amount);
        return gX_F32Qua_Normalize(r);
    """
    expected_slerp = """
        gX_F32Qua r = {0};
        gX_F32 cosHalfTheta = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;
        if (cosHalfTheta < 0) {
            q2.x = -q2.x; q2.y = -q2.y; q2.z = -q2.z; q2.w = -q2.w;
            cosHalfTheta = -cosHalfTheta;
        }
        if (fabsf(cosHalfTheta) >= 1.0f) r = q1;
        else if (cosHalfTheta > 0.95f) r = gX_F32Qua_Nlerp(q1, q2, amount);
        else {
            gX_F32 halfTheta = acosf(cosHalfTheta);
            gX_F32 sinHalfTheta = sqrtf(1.0f - cosHalfTheta*cosHalfTheta);
            if (fabsf(sinHalfTheta) < gX_F32_EPSILON) {
                r.x = (q1.x + q2.x) * 0.5f; r.y = (q1.y + q2.y) * 0.5f;
                r.z = (q1.z + q2.z) * 0.5f; r.w = (q1.w + q2.w) * 0.5f;
            } else {
                gX_F32 ratioA = sinf((1.0f - amount) * halfTheta) / sinHalfTheta;
                gX_F32 ratioB = sinf(amount * halfTheta) / sinHalfTheta;
                r.x = q1.x*ratioA + q2.x*ratioB; r.y = q1.y*ratioA + q2.y*ratioB;
                r.z = q1.z*ratioA + q2.z*ratioB; r.w = q1.w*ratioA + q2.w*ratioB;
            }
        }
        return r;
    """
    expected_transform = """
        gX_F32v3 result = {0};
        gX_F32 x = v.x; gX_F32 y = v.y; gX_F32 z = v.z;
        result.x = mat.m0*x + mat.m4*y + mat.m8*z + mat.m12;
        result.y = mat.m1*x + mat.m5*y + mat.m9*z + mat.m13;
        result.z = mat.m2*x + mat.m6*y + mat.m10*z + mat.m14;
        return result;
    """
    for source, name, body in (
        (vector, "gX_F32v3_RotateByQuaternion", expected_rotate),
        (vector, "gX_F32v3_Transform", expected_transform),
        (quaternion, "gX_F32Qua_ToMatrix", expected_matrix),
        (quaternion, "gX_F32Qua_Invert", expected_inverse),
        (quaternion, "gX_F32Qua_Multiply", expected_product),
        (quaternion, "gX_F32Qua_Lerp", expected_lerp),
        (quaternion, "gX_F32Qua_Normalize", expected_normalize),
        (quaternion, "gX_F32Qua_Nlerp", expected_nlerp),
        (quaternion, "gX_F32Qua_Slerp", expected_slerp),
    ):
        if c_body(source, name) != compact(body):
            raise ValueError(f"C quaternion rotation contract drift: {name}")

    expected_lean = {
        "pure": "{x := v.x, y := v.y, z := v.z, w := 0}",
        "rotate": """
          {x := v.x * (q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z) +
                v.y * (2 * q.x * q.y - 2 * q.w * q.z) + v.z * (2 * q.x * q.z + 2 * q.w * q.y)
           y := v.x * (2 * q.w * q.z + 2 * q.x * q.y) +
                v.y * (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) + v.z * (-2 * q.w * q.x + 2 * q.y * q.z)
           z := v.x * (-2 * q.w * q.y + 2 * q.x * q.z) +
                v.y * (2 * q.w * q.x + 2 * q.y * q.z) + v.z * (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z)}
        """,
        "toMatrix": """
          {Matrix.identity with
            m0 := 1 - 2 * (q.y * q.y + q.z * q.z)
            m1 := 2 * (q.x * q.y + q.w * q.z)
            m2 := 2 * (q.x * q.z - q.w * q.y)
            m4 := 2 * (q.x * q.y - q.w * q.z)
            m5 := 1 - 2 * (q.x * q.x + q.z * q.z)
            m6 := 2 * (q.y * q.z + q.w * q.x)
            m8 := 2 * (q.x * q.z + q.w * q.y)
            m9 := 2 * (q.y * q.z - q.w * q.x)
            m10 := 1 - 2 * (q.x * q.x + q.y * q.y)}
        """,
    }
    for name, body in expected_lean.items():
        if lean_body(lean, name) != compact(body):
            raise ValueError(f"Lean quaternion rotation contract drift: {name}")
    expected_interpolation = {
        "dot": "a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w",
        "neg": "{x := -q.x, y := -q.y, z := -q.z, w := -q.w}",
        "lerpQua": "{x := q1.x + t * (q2.x - q1.x) y := q1.y + t * (q2.y - q1.y) z := q1.z + t * (q2.z - q1.z) w := q1.w + t * (q2.w - q1.w)}",
        "normalize": "if normSq q = 0 then q else scale q (1 / norm q)",
        "nlerp": "normalize (lerpQua q1 q2 t)",
        "weighted": "{x := q1.x * a + q2.x * b y := q1.y * a + q2.y * b z := q1.z * a + q2.z * b w := q1.w * a + q2.w * b}",
        "slerpRegular": "let sinHalf := Real.sqrt (1 - c ^ 2) weighted q1 (Real.sin ((1 - t) * Real.arccos c) / sinHalf) q2 (Real.sin (t * Real.arccos c) / sinHalf)",
        "slerp": "let cosine := dot q1 q2 let q2Adjusted := if cosine < 0 then neg q2 else q2 let cosineAdjusted := if cosine < 0 then -cosine else cosine if |cosineAdjusted| >= 1 then q1 else if cosineAdjusted > 19 / 20 then nlerp q1 q2Adjusted t else let sinHalf := Real.sqrt (1 - cosineAdjusted ^ 2) if |sinHalf| < epsilon then lerpQua q1 q2Adjusted (1 / 2) else slerpRegular q1 q2Adjusted cosineAdjusted t",
    }
    for name, body in expected_interpolation.items():
        if lean_body(lean, name) != compact(body):
            raise ValueError(f"Lean quaternion interpolation model drift: {name}")
    if "#definegX_F32_EPSILON0.000001f" not in compact(macros):
        raise ValueError("native quaternion interpolation epsilon drift")
    for name, affine in (("transformDir", False), ("transformVec", True)):
        offsets = (" + m.m12", " + m.m13", " + m.m14") if affine else ("", "", "")
        body = "{x := m.m0 * v.x + m.m4 * v.y + m.m8 * v.z" + offsets[0] + "," + \
               "y := m.m1 * v.x + m.m5 * v.y + m.m9 * v.z" + offsets[1] + "," + \
               "z := m.m2 * v.x + m.m6 * v.y + m.m10 * v.z" + offsets[2] + "}"
        if lean_body(matrix, name) != compact(body):
            raise ValueError(f"Lean matrix convention drift: {name}")


class QuaternionRotationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = ((ROOT / "src/gxmath/quaternion.c").read_text(),
                       (ROOT / "src/gxmath/vector3.c").read_text(),
                       (ROOT.parent / "proofs/GxMath/Quaternion.lean").read_text(),
                       (ROOT.parent / "proofs/GxMath/Matrix.lean").read_text(),
                       (ROOT / "src/gxmath/macros.h").read_text())

    def test_source_correspondence(self):
        check_contract(*self.sources)

    def test_negative_controls(self):
        for index, old, new in (
            (0, "2*(ab + cd)", "2*(ab - cd)"),
            (0, "r.w *= invLen", "r.w *= -invLen"),
            (0, "q1y*q2z - q1z*q2y", "q1z*q2y - q1y*q2z"),
            (1, "2*q.x*q.y - 2*q.w*q.z", "2*q.x*q.y + 2*q.w*q.z"),
            (1, "mat.m4*y + mat.m8*z", "mat.m1*y + mat.m2*z"),
            (2, "q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z", "1 - 2 * q.y * q.y - 2 * q.z * q.z"),
            (2, "m1 := 2 * (q.x * q.y + q.w * q.z)", "m1 := 2 * (q.x * q.y - q.w * q.z)"),
            (3, "m.m4 * v.y + m.m8  * v.z", "m.m1 * v.y + m.m2 * v.z"),
            (0, "cosHalfTheta > 0.95f", "cosHalfTheta >= 0.95f"),
            (0, "if (cosHalfTheta < 0)", "if (cosHalfTheta <= 0)"),
            (0, "fabsf(sinHalfTheta) < gX_F32_EPSILON", "fabsf(sinHalfTheta) <= gX_F32_EPSILON"),
            (2, "cosineAdjusted > 19 / 20", "cosineAdjusted >= 19 / 20"),
            (2, "if normSq q = 0 then q", "if normSq q = 0 then zero"),
            (4, "0.000001f", "0.00001f"),
        ):
            with self.subTest(index=index, old=old):
                self.assertIn(old, self.sources[index])
                modified = list(self.sources)
                modified[index] = modified[index].replace(old, new, 1)
                with self.assertRaises(ValueError):
                    check_contract(*modified)

    def test_missing_duplicate_witnesses_rejected(self):
        for index in range(4):
            with self.subTest(index=index):
                for value in ("", self.sources[index] + self.sources[index]):
                    modified = list(self.sources)
                    modified[index] = value
                    with self.assertRaises(ValueError):
                        check_contract(*modified)


if __name__ == "__main__":
    unittest.main()
