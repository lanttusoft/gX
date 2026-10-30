/**********************************************************************************************
*
*   [gX: src/gxmath/gxmath_tests.c]
*   gxmath unit tests
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   This software is provided "as-is", without any express or implied warranty.
*   In no event will the authors be held liable for any damages arising from
*   the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose,
*   including commercial applications, and to alter it and redistribute it
*   freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not
*     claim that you wrote the original software. If you use this software
*     in a product, an acknowledgment would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not
*     be misrepresented as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
*
*   gX addition to gxmath; distributed under the gxmath zlib/libpng terms.
*
**********************************************************************************************/

/**********************************************************************************************
*
*   gxmath/gxmath_tests.c
*   Math library tests — called from main_tests.c via gX_Tests().
*
*   //opencode: ported from game3 gamemath_tests.c
*
**********************************************************************************************/
//opencode: suppress double-promotion — test printf calls use variadic float
#pragma GCC diagnostic ignored "-Wdouble-promotion"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gxmath.h"
#include "../common/log.h"

//opencode: declaration for test entry point called from main_tests.c
gX_I32 gX_Tests_Math(gX_I0);

#define CHECK(cond) do { if (!(cond)) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL %s", #cond); } } while(0)
#define CHECK_FEQ(a,b) do { if (fabsf((a)-(b)) > 0.0001f) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL %s: %.6f != %.6f", #a "==" #b, (gX_F32)(a), (gX_F32)(b)); } } while(0)

static gX_I32 gX_Tests_Vectors(gX_I0) {
    gX_I32 errors = 0;
    gX_F32 vf;
    gX_F32v2 v2;

    CHECK_FEQ(gX_F32_FastCos_Taylor3(0.0f), 1.0f);
    CHECK_FEQ(gX_F32_FastCos_Taylor4(0.0f), 1.0f);
    CHECK_FEQ(gX_F32_FastCos_Taylor5(0.0f), 1.0f);
    CHECK_FEQ(gX_F32_FastSin_Taylor3(0.0f), 0.0f);
    CHECK_FEQ(gX_F32_FastSin_Taylor4(0.0f), 0.0f);
    CHECK_FEQ(gX_F32_FastSin_Taylor5(0.0f), 0.0f);

    //opencode: tolerance widened to 0.001f — Simpson is numerical, not exact
    { gX_F32 sv = gX_F32_Calculus_Simpson(sinf, 128, 0.0f, 1.0f * gX_PI); if (fabsf(sv - 2.00f) > 0.001f) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL Simpson sin 0..pi: %.6f != 2.0", sv); } }
    { gX_F32 sv = gX_F32_Calculus_Simpson(sinf, 128, 0.0f, 2.0f * gX_PI); if (fabsf(sv - 0.00f) > 0.001f) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL Simpson sin 0..2pi: %.6f != 0.0", sv); } }
    { gX_F32 sv = gX_F32_Calculus_Simpson(cosf, 128, 0.0f, 0.5f * gX_PI); if (fabsf(sv - 1.00f) > 0.001f) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL Simpson cos 0..pi/2: %.6f != 1.0", sv); } }

    v2 = gX_F32v2_Zero();            CHECK(v2.x == 0 && v2.y == 0);
    v2 = gX_F32v2_One();             CHECK(v2.x == 1 && v2.y == 1);
    v2 = gX_F32v2_Add((gX_F32v2){1,2}, (gX_F32v2){3,4}); CHECK_FEQ(v2.x, 4); CHECK_FEQ(v2.y, 6);
    v2 = gX_F32v2_Sub((gX_F32v2){5,5}, (gX_F32v2){2,3}); CHECK_FEQ(v2.x, 3); CHECK_FEQ(v2.y, 2);
    vf = gX_F32v2_Length((gX_F32v2){3,4}); CHECK_FEQ(vf, 5.0f);
    vf = gX_F32v2_Dot((gX_F32v2){1,0}, (gX_F32v2){0,1}); CHECK_FEQ(vf, 0.0f);
    v2 = gX_F32v2_Normalize((gX_F32v2){3,4}); CHECK_FEQ(gX_F32v2_Length(v2), 1.0f);

    gX_F32v3 v3 = gX_F32v3_Cross((gX_F32v3){1,0,0}, (gX_F32v3){0,1,0});
    CHECK_FEQ(v3.x, 0); CHECK_FEQ(v3.y, 0); CHECK_FEQ(v3.z, 1);
    CHECK_FEQ(gX_F32v3_Dot((gX_F32v3){1,0,0}, (gX_F32v3){0,1,0}), 0.0f);
    CHECK_FEQ(gX_F32v3_Dot((gX_F32v3){1,0,0}, (gX_F32v3){1,0,0}), 1.0f);

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: vectors done — %i errors", errors);
    return errors;
}

static gX_I32 gX_Tests_Convert(gX_I0) {
    gX_I32 errors = 0;

    // MeterPrefix
    CHECK_FEQ(gX_F32_Convert_MeterPrefix(1000.0f, 3), 1.0f);    // 1000 m = 1 km
    CHECK_FEQ(gX_F32_Convert_MeterPrefix(1.0f, -3), 1000.0f);    // 1 m = 1000 mm
    CHECK_FEQ(gX_F32_Convert_MeterPrefix(1.0f, -2), 100.0f);     // 1 m = 100 cm
    CHECK_FEQ(gX_F32_Convert_MeterPrefix(1.0f, -9), 1.0e9f);     // 1 m = 1e9 nm

    // Unit conversions
    CHECK_FEQ(gX_F32_Convert_Foot2Meter(1.0f), 0.3048f);
    CHECK_FEQ(gX_F32_Convert_Meter2Foot(0.3048f), 1.0f);
    CHECK_FEQ(gX_F32_Convert_Inch2Meter(1.0f), 0.0254f);
    CHECK_FEQ(gX_F32_Convert_Meter2Inch(0.0254f), 1.0f);

    // Temperature
    CHECK_FEQ(gX_F32_Convert_Celsius2Kelvin(0.0f), 273.15f);
    CHECK_FEQ(gX_F32_Convert_Kelvin2Celsius(273.15f), 0.0f);

    // RGB ↔ HSV roundtrip
    gX_F32v3 rgb = {0.5f, 0.3f, 0.7f};
    gX_F32v3 hsv = gX_F32v3_Convert_RGB2HSV(rgb);
    gX_F32v3 rgb2 = gX_F32v3_Convert_HSV2RGB(hsv);
    CHECK_FEQ(rgb2.x, rgb.x); CHECK_FEQ(rgb2.y, rgb.y); CHECK_FEQ(rgb2.z, rgb.z);

    // Coordinate system roundtrips
    gX_F32v3 cart = {1.0f, 2.0f, 3.0f};
    gX_F32v3 cyl = gX_F32_Convert_cartesian2cylinder(cart);
    gX_F32v3 cart2 = gX_F32_Convert_cylinder2cartesian(cyl);
    CHECK_FEQ(cart2.x, cart.x); CHECK_FEQ(cart2.y, cart.y); CHECK_FEQ(cart2.z, cart.z);

    gX_F32v3 sph = gX_F32_Convert_cartesian2spherical(cart);
    gX_F32v3 cart3 = gX_F32_Convert_spherical2cartesian(sph);
    CHECK_FEQ(cart3.x, cart.x); CHECK_FEQ(cart3.y, cart.y); CHECK_FEQ(cart3.z, cart.z);

    // Cylinder ↔ Spherical roundtrip
    gX_F32v3 sph2 = gX_F32_Convert_cylinder2spherical(cyl);
    gX_F32v3 cyl2 = gX_F32_Convert_spherical2cylinder(sph2);
    CHECK_FEQ(cyl2.x, cyl.x); CHECK_FEQ(cyl2.y, cyl.y); CHECK_FEQ(cyl2.z, cyl.z);

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: convert done — %i errors", errors);
    return errors;
}

static gX_I32 gX_Tests_Quaternions(gX_I0) {
    gX_I32 errors = 0;
    gX_F32Qua q1, q2, qr;
    gX_F32v3 axis;
    gX_F32 angle;

    // Identity
    q1 = gX_F32Qua_Identity();
    CHECK_FEQ(q1.w, 1.0f); CHECK_FEQ(q1.x, 0); CHECK_FEQ(q1.y, 0); CHECK_FEQ(q1.z, 0);

    // Add
    q1 = (gX_F32Qua){1,2,3,4}; q2 = (gX_F32Qua){5,6,7,8};
    qr = gX_F32Qua_Add(q1, q2);
    CHECK_FEQ(qr.x, 6); CHECK_FEQ(qr.y, 8); CHECK_FEQ(qr.z, 10); CHECK_FEQ(qr.w, 12);

    // Length + Normalize
    q1 = (gX_F32Qua){0,0,0,1};
    CHECK_FEQ(gX_F32Qua_Length(q1), 1.0f);
    qr = gX_F32Qua_Normalize((gX_F32Qua){1,0,0,0});
    CHECK_FEQ(gX_F32Qua_Length(qr), 1.0f);

    // Multiply (two 90-degree rotations = 180)
    q1 = gX_F32Qua_FromAxisAngle((gX_F32v3){0,1,0}, gX_PI / 2.0f);
    q2 = gX_F32Qua_FromAxisAngle((gX_F32v3){0,1,0}, gX_PI / 2.0f);
    qr = gX_F32Qua_Multiply(q1, q2);
    axis = (gX_F32v3){0}; angle = 0;
    gX_F32Qua_ToAxisAngle(qr, &axis, &angle);
    CHECK_FEQ(angle, gX_PI);  // 90+90 = 180 degrees

    // From/To Euler
    q1 = gX_F32Qua_FromEuler(0, 0, 0);
    CHECK_FEQ(q1.w, 1.0f);
    gX_F32v3 euler = gX_F32Qua_ToEuler(gX_F32Qua_Identity());
    CHECK_FEQ(euler.x, 0); CHECK_FEQ(euler.y, 0); CHECK_FEQ(euler.z, 0);

    // From/To Matrix (rotation around Y by 90 deg)
    q1 = gX_F32Qua_FromAxisAngle((gX_F32v3){0,1,0}, gX_PI / 2.0f);
    gX_Matrix mat = gX_F32Qua_ToMatrix(q1);
    q2 = gX_F32Qua_FromMatrix(mat);
    CHECK(gX_F32Qua_Equals(q1, q2));

    // Slerp
    q1 = gX_F32Qua_Identity();
    q2 = gX_F32Qua_FromAxisAngle((gX_F32v3){0,1,0}, gX_PI);
    qr = gX_F32Qua_Slerp(q1, q2, 0.5f);
    CHECK_FEQ(gX_F32Qua_Length(qr), 1.0f);

    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: quaternions done — %i errors", errors);
    return errors;
}

//ai: asymmetric multiplication/inversion witnesses and physical member-order evidence for Lean contracts.
static gX_I32 gX_Tests_MatrixCorrespondence(gX_I0)
{
    gX_I32 errors = 0;
    gX_Matrix Labelled = {.m0=0,.m1=1,.m2=2,.m3=3,.m4=4,.m5=5,.m6=6,.m7=7,
        .m8=8,.m9=9,.m10=10,.m11=11,.m12=12,.m13=13,.m14=14,.m15=15};
    const gX_F32 Expected[] = {0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15};
    gX_F32 Raw[16];
    CHECK(sizeof(Labelled) == sizeof(Raw));
    memcpy(Raw,&Labelled,sizeof(Raw));
    for (gX_U32 Index = 0; Index < 16; Index++) CHECK(Raw[Index] == Expected[Index]);
    gX_Matrix Translate = gX_Matrix_Identity();
    gX_Matrix Scale = gX_Matrix_Identity();
    Translate.m12 = 2.0f;
    Scale.m0 = 3.0f;
    gX_Matrix Forward = gX_Matrix_Multiply(Translate,Scale);
    gX_Matrix Reverse = gX_Matrix_Multiply(Scale,Translate);
    CHECK_FEQ(Forward.m12,6.0f);
    CHECK_FEQ(Reverse.m12,2.0f);
    gX_F32v3 Origin = {0};
    gX_F32v3 ForwardPoint = gX_F32v3_Transform(Origin,Forward);
    gX_F32v3 ReversePoint = gX_F32v3_Transform(Origin,Reverse);
    CHECK_FEQ(ForwardPoint.x,6.0f);
    CHECK_FEQ(ReversePoint.x,2.0f);
    gX_Matrix Inverse = gX_Matrix_Invert(Forward);
    gX_Matrix Roundtrip = gX_Matrix_Multiply(Forward,Inverse);
    gX_F32v3 Point = {1.0f,2.0f,3.0f};
    gX_F32v3 Restored = gX_F32v3_Transform(Point,Roundtrip);
    CHECK_FEQ(Restored.x,Point.x);
    CHECK_FEQ(Restored.y,Point.y);
    CHECK_FEQ(Restored.z,Point.z);
    gX_TraceLog(gX_LOG_INFO,"GX_TESTS: matrix correspondence done — %i errors",errors);
    return errors;
}

//ai: near-singular IEEE witnesses; residuals use double products of the stored float inputs/outputs.
// Power-of-two cases pin exact residuals; Hilbert residual/condition proxy are measurements, not a universal bound.
static gX_I32 gX_Tests_MatrixNearSingular(gX_I0)
{
    gX_I32 errors = 0;
    const gX_I8 *Names[] = {"diagonal","dependent","rounded_singular","hilbert"};
    for (gX_U32 Case = 0; Case < 4; Case++)
    {
        gX_F32 Input[16] = {0};
        for (gX_U32 Index = 0; Index < 4; Index++) Input[Index*5] = 1.0f;
        if (Case == 0) Input[15] = 0x1p-20f;
        if (Case == 1 || Case == 2)
        {
            Input[1] = Input[4] = 1.0f;
            Input[5] = 1.0f + (Case == 1 ? 0x1p-20f : 0x1p-24f);
        }
        if (Case == 3)
            for (gX_U32 Row = 0; Row < 4; Row++)
                for (gX_U32 Column = 0; Column < 4; Column++)
                    Input[Row+4*Column] = 1.0f/(gX_F32)(Row+Column+1);
        gX_Matrix Matrix;
        memcpy(&Matrix,Input,sizeof(Input));
        gX_Matrix Inverse = gX_Matrix_Invert(Matrix);
        gX_F32 Output[16];
        memcpy(Output,&Inverse,sizeof(Output));
        gX_I1 Finite = true;
        for (gX_U32 Index = 0; Index < 16; Index++) Finite = Finite && isfinite(Output[Index]);
        CHECK(Finite == (Case != 2));
        gX_F64 Residual = -1.0;
        gX_F64 ConditionProxy = -1.0;
        if (Finite)
        {
            Residual = 0.0;
            gX_F64 InputNorm = 0.0, InverseNorm = 0.0;
            for (gX_U32 Row = 0; Row < 4; Row++)
            {
                gX_F64 InputSum = 0.0, InverseSum = 0.0;
                for (gX_U32 Column = 0; Column < 4; Column++)
                {
                    InputSum += fabs((gX_F64)Input[Row+4*Column]);
                    InverseSum += fabs((gX_F64)Output[Row+4*Column]);
                    gX_F64 Left = 0.0, Right = 0.0;
                    for (gX_U32 Inner = 0; Inner < 4; Inner++)
                    {
                        Left += (gX_F64)Input[Row+4*Inner]*(gX_F64)Output[Inner+4*Column];
                        Right += (gX_F64)Output[Row+4*Inner]*(gX_F64)Input[Inner+4*Column];
                    }
                    gX_F64 Identity = Row == Column ? 1.0 : 0.0;
                    Residual = fmax(Residual,fmax(fabs(Left-Identity),fabs(Right-Identity)));
                }
                InputNorm = fmax(InputNorm,InputSum);
                InverseNorm = fmax(InverseNorm,InverseSum);
            }
            ConditionProxy = InputNorm*InverseNorm;
            if (Case < 2) CHECK(Residual == 0.0);
        }
        gX_TraceLog(gX_LOG_INFO,"GX_TESTS: inverse witness=%s finite=%d residual=%.12g condition_proxy=%.12g",
            Names[Case],(gX_I32)Finite,Residual,ConditionProxy);
    }
    return errors;
}

//ai: cofactor/adjugate and column-basis constructor witnesses.
static gX_I32 gX_Tests_MatrixAux(gX_I0)
{
    gX_I32 errors = 0;
    gX_Matrix A = {
        .m0=0,.m1=1,.m2=0,.m3=0,
        .m4=-1,.m5=0,.m6=0,.m7=0,
        .m8=0,.m9=0,.m10=2,.m11=0,
        .m12=3,.m13=4,.m14=5,.m15=1,
    };
    gX_Matrix Cofactor = gX_Matrix_Cofactor(A);
    gX_Matrix Adjucant = gX_Matrix_Adjucant(A);
    gX_F32a16 AdjugateValues = gX_Matrix_To_F32a16(Adjucant);
    gX_F32a16 TransposedValues = gX_Matrix_To_F32a16(gX_Matrix_Transpose(Cofactor));
    for (gX_I32 Index = 0; Index < 16; Index++)
        CHECK_FEQ(AdjugateValues.v[Index],TransposedValues.v[Index]);
    gX_F32 Determinant = gX_F32m4x4_Determinant(A);
    gX_Matrix Product = gX_Matrix_Multiply(A,Adjucant);
    gX_F32a16 ProductValues = gX_Matrix_To_F32a16(Product);
    for (gX_I32 Row = 0; Row < 4; Row++)
        for (gX_I32 Column = 0; Column < 4; Column++)
            CHECK_FEQ(ProductValues.v[Row*4+Column],Row == Column ? Determinant : 0.0f);
    gX_F32v4 c0 = {1,2,3,4}, c1 = {5,6,7,8}, c2 = {9,10,11,12}, c3 = {13,14,15,16};
    gX_Matrix Basis = gX_Matrix_From_F32v4(c0,c1,c2,c3);
    CHECK_FEQ(Basis.m0,1); CHECK_FEQ(Basis.m1,2); CHECK_FEQ(Basis.m2,3); CHECK_FEQ(Basis.m3,4);
    CHECK_FEQ(Basis.m4,5); CHECK_FEQ(Basis.m5,6); CHECK_FEQ(Basis.m6,7); CHECK_FEQ(Basis.m7,8);
    CHECK_FEQ(Basis.m8,9); CHECK_FEQ(Basis.m9,10); CHECK_FEQ(Basis.m10,11); CHECK_FEQ(Basis.m11,12);
    CHECK_FEQ(Basis.m12,13); CHECK_FEQ(Basis.m13,14); CHECK_FEQ(Basis.m14,15); CHECK_FEQ(Basis.m15,16);
    gX_F32v3 Point = {1,0,0};
    gX_F32v3 Transformed = gX_F32v3_Transform(Point,Basis);
    CHECK_FEQ(Transformed.x,14); CHECK_FEQ(Transformed.y,16); CHECK_FEQ(Transformed.z,18);
    gX_F32m2x2 Small2 = {.m0=1,.m1=2,.m2=3,.m3=4};
    CHECK_FEQ(gX_F32m2x2_Determinant(Small2),-2);
    CHECK_FEQ(gX_F32m2x2_Trace(Small2),5);
    gX_F32m3x3 Small3 = {.m0=1,.m1=2,.m2=3,.m3=0,.m4=4,.m5=5,.m6=1,.m7=0,.m8=6};
    CHECK_FEQ(gX_F32m3x3_Determinant(Small3),22);
    CHECK_FEQ(gX_F32m3x3_Trace(Small3),11);
    gX_TraceLog(gX_LOG_INFO,"GX_TESTS: matrix auxiliary done — %i errors",errors);
    return errors;
}

//ai: double-precision lerp witness; 1/3 and 5/12 would lose precision through F32.
static gX_I32 gX_Tests_Vector64(gX_I0)
{
    gX_I32 errors = 0;
    gX_F64v4 A = {0.1,1.0/3.0,-2.5,1.0e-9};
    gX_F64v4 B = {0.4,2.0/3.0,5.5,3.0e-9};
    gX_F64v4 R = gX_F64v4_Lerp(A,B,0.25);
    CHECK(fabs(R.x - 0.175) < 1.0e-15);
    CHECK(fabs(R.y - 5.0/12.0) < 1.0e-15);
    CHECK(fabs(R.z + 0.5) < 1.0e-15);
    CHECK(fabs(R.w - 1.5e-9) < 1.0e-24);
    gX_TraceLog(gX_LOG_INFO,"GX_TESTS: vector64 done — %i errors",errors);
    return errors;
}

gX_I32 gX_Tests_Math(gX_I0) {
    gX_I32 errors = 0;
    errors += gX_Tests_MatrixCorrespondence();
    errors += gX_Tests_MatrixAux();
    errors += gX_Tests_MatrixNearSingular();
    errors += gX_Tests_Vector64();
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: ====================");
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: Running math tests");
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: ====================");
    errors += gX_Tests_Vectors();
    errors += gX_Tests_Convert();
    errors += gX_Tests_Quaternions();
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: ====================");
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: Total: %i errors", errors);
    gX_TraceLog(gX_LOG_INFO, "GX_TESTS: ====================");
    return errors;
}

//opencode: math-pass corpus dump — scripts/math_pass.py verifies every case
//   against an independent SymPy oracle (exact rationals where the inputs are
//   exact, float64 formulas where trig/sqrt is involved). Format, one line per
//   case:  MATHDUMP <case> <count> <floats...>
//   The float layout per case is documented in both this file and the script:
//     multiply    L(16) R(16) O(16)    invert      M(16) O(16)
//     transform   M(16) v(3)  O(3)     determinant M(16) O(1)
//     perspective fov aspect near far(4) O(16)
//     rotate_xyz  rx ry rz(3) O(16)    lookat      eye(3) target(3) up(3) O(16)
//     vectors     a(3) b(3) cross(3) dot(1) proj(3)
//     sdf         p(3) r planeN(3) cubeB(3) torusT(2) cylH(2) coneC(3)
//                 capsuleA(3) capsuleB(3) capsuleR octaR roundR hexH(2) O(11)
//     sdf_ext     p(3) b(3) e m(3) param(4) c(3) a(3) d(3) O(8)
//     quaternion  q1(4) q2(4) qa(4) qb(4) t(1) O(24)
//     quaternion_rot axis(3) angle euler(3) q(4) O(35)
//     matrix_aux  a(16) b(16) frustum(6) ortho(6) vectors(16) O(176)
//     matrix_small m2(4) m3(9) O(4)
//     f64         a(4) b(4) t(1) O(4), emitted as MATHDUMP64
//     edge_cases  singular(16) O(86), scalar pairs: kind 0 finite/1 +inf/
//                 2 -inf/3 NaN, then the finite value
//     easing      start end t s p startc endc(7) O(5)
//     scalar      value min max inS inE outS outE lerpA lerpB lerpT(10) O(5)
//     int_math    n a b c d e f g h(9) O(5)
//     color       c1(4) c2(4) t(1) O(16)
//     physics     force(4) mass g h vel(4) kmass length speed(14) O(7)
//     relativity  velocity lightSpeed addU addV time length
//                 dopplerBeta rapidityBeta(8) O(7)
//     planck      temperature wavelength(2) O(5)
//     fft_delta   input re/im (16) O(16)   fft_sine   input re/im (16) O(16)
//     calculus    x d steps a b px py pz(8) O(10)
//     convert_units meters feet inches kelvin celsius prefix(6) O(7)
//     convert_color rgb(3) hsv(3) O(6)
//     convert_coords p(3) O(18)      convert_sky eq(3) lat lon(5) O(24)
//     convert_new linear srgb amp db note freq celsius fahrenheit(8) O(8)
//     convert_proj p(3) O(10)        convert_ball p(3) O(5)
//     random      seed_hi seed_lo stream_hi stream_lo, then N x (hi, lo)
//                 raw u32 draws — hi/lo 16-bit halves keep every value exact
//                 in the float dump format
//   GX_MATH_RANDOM_DUMP=1 instead emits seeded MATHRANDOM lines for the
//   differential checker; inputs and outputs are hexadecimal floats.
gX_I32 gX_Tests_MathDump(gX_I0);

static gX_I0 gX_Tests_MathDump_Line(const char *name, const gX_F32 *v, gX_I32 n) {
    printf("MATHDUMP %s %d", name, (int)n);
    for (gX_I32 i = 0; i < n; i++) printf(" %.9g", (double)v[i]);
    printf("\n");
}

//ai: double-precision dump line for APIs whose precision would be lost through F32.
static gX_I0 gX_Tests_MathDump_Line64(const char *name, const gX_F64 *v, gX_I32 n) {
    printf("MATHDUMP64 %s %d", name, (int)n);
    for (gX_I32 i = 0; i < n; i++) printf(" %.17g", v[i]);
    printf("\n");
}

static gX_I0 gX_Tests_MathDump_V3(gX_F32 *buf, gX_I32 *n, gX_F32v3 v) {
    buf[(*n)++] = v.x; buf[(*n)++] = v.y; buf[(*n)++] = v.z;
}

static gX_I0 gX_Tests_MathDump_V2(gX_F32 *buf, gX_I32 *n, gX_F32v2 v) {
    buf[(*n)++] = v.x; buf[(*n)++] = v.y;
}

//ai: V4 dump helper for the quaternion corpus probes.
static gX_I0 gX_Tests_MathDump_V4(gX_F32 *buf, gX_I32 *n, gX_F32v4 v) {
    buf[(*n)++] = v.x; buf[(*n)++] = v.y; buf[(*n)++] = v.z; buf[(*n)++] = v.w;
}

static gX_Matrix gX_Tests_MathDump_Mat(const gX_F32 *rowMajor) {
    gX_Matrix M = { 0 };
    M.m0 = rowMajor[0];   M.m1 = rowMajor[1];   M.m2 = rowMajor[2];   M.m3 = rowMajor[3];
    M.m4 = rowMajor[4];   M.m5 = rowMajor[5];   M.m6 = rowMajor[6];   M.m7 = rowMajor[7];
    M.m8 = rowMajor[8];   M.m9 = rowMajor[9];   M.m10 = rowMajor[10]; M.m11 = rowMajor[11];
    M.m12 = rowMajor[12]; M.m13 = rowMajor[13]; M.m14 = rowMajor[14]; M.m15 = rowMajor[15];
    return M;
}

static gX_I0 gX_Tests_MathDump_Mat16(gX_F32 *out, gX_Matrix M) {
    out[0] = M.m0;   out[1] = M.m1;   out[2] = M.m2;   out[3] = M.m3;
    out[4] = M.m4;   out[5] = M.m5;   out[6] = M.m6;   out[7] = M.m7;
    out[8] = M.m8;   out[9] = M.m9;   out[10] = M.m10; out[11] = M.m11;
    out[12] = M.m12; out[13] = M.m13; out[14] = M.m14; out[15] = M.m15;
}

//ai: append one matrix to a corpus buffer in field order.
static gX_I0 gX_Tests_MathDump_MatAppend(gX_F32 *buf, gX_I32 *n, gX_Matrix M) {
    gX_F32 values[16];
    gX_Tests_MathDump_Mat16(values,M);
    for (gX_I32 i = 0; i < 16; i++) buf[(*n)++] = values[i];
}

//ai: edge cases encode non-finite results as (kind, value): 0 finite, 1 +inf, 2 -inf, 3 NaN.
static gX_I0 gX_Tests_EdgeAppend(gX_F32 *buf, gX_I32 *n, gX_F32 value) {
    gX_F32 kind = 0.0f;
    gX_F32 stored = value;
    if (isnan(value)) {
        kind = 3.0f;
        stored = 0.0f;
    } else if (isinf(value)) {
        kind = value > 0.0f ? 1.0f : 2.0f;
        stored = 0.0f;
    }
    buf[(*n)++] = kind;
    buf[(*n)++] = stored;
}

static gX_I0 gX_Tests_EdgeAppendV3(gX_F32 *buf, gX_I32 *n, gX_F32v3 value) {
    gX_Tests_EdgeAppend(buf,n,value.x);
    gX_Tests_EdgeAppend(buf,n,value.y);
    gX_Tests_EdgeAppend(buf,n,value.z);
}

static gX_I0 gX_Tests_EdgeAppendV4(gX_F32 *buf, gX_I32 *n, gX_F32v4 value) {
    gX_Tests_EdgeAppend(buf,n,value.x);
    gX_Tests_EdgeAppend(buf,n,value.y);
    gX_Tests_EdgeAppend(buf,n,value.z);
    gX_Tests_EdgeAppend(buf,n,value.w);
}

//ai: random differential lines export exact inputs and outputs as hex floats.
static gX_I0 gX_Tests_RandomLine(const char *family, gX_I32 probe,
    const gX_F32 *inputs, gX_I32 inputCount,
    const gX_F32 *outputs, gX_I32 outputCount) {
    printf("MATHRANDOM %s %d %d", family, (int)probe, (int)inputCount);
    for (gX_I32 i = 0; i < inputCount; i++) printf(" %a", (gX_F64)inputs[i]);
    printf(" %d", (int)outputCount);
    for (gX_I32 i = 0; i < outputCount; i++) printf(" %a", (gX_F64)outputs[i]);
    printf("\n");
}

static gX_F32v3 gX_Tests_RandomV3(gX_Random32 *r, gX_F32 min, gX_F32 max) {
    return (gX_F32v3){gX_Random32_Range(r,min,max),
        gX_Random32_Range(r,min,max),gX_Random32_Range(r,min,max)};
}

static gX_F32v3 gX_Tests_RandomV3NonZero(gX_Random32 *r, gX_F32 min, gX_F32 max,
    gX_F32 minSquared) {
    gX_F32v3 value;
    do {
        value = gX_Tests_RandomV3(r,min,max);
    } while (value.x*value.x + value.y*value.y + value.z*value.z < minSquared);
    return value;
}

//ai: CPU float row action used only by headless witnesses; this is not a GLSL execution model.
static gX_I0 gX_Tests_ReprojectionRow(const gX_F32 Vector[4], const gX_F32 Matrix[16], gX_F32 Output[4])
{
    for (gX_U32 Column = 0; Column < 4; Column++)
        Output[Column] = Vector[0]*Matrix[Column]+Vector[1]*Matrix[4+Column]+
            Vector[2]*Matrix[8+Column]+Vector[3]*Matrix[12+Column];
}

//ai: exact hex exports let the independent checker separate stored-float algebra from CPU rounding.
static gX_I0 gX_Tests_ProjectionClipDump(gX_I0)
{
    const gX_I8 *Names[] = {"positive","cross_near","cross_camera"};
    gX_F32 Projection[16];
    gX_Tests_MathDump_Mat16(Projection,gX_Matrix_Perspective(gX_PI/3.0f,1.4f,0.1f,100.0f));
    for (gX_U32 Case = 0; Case < 3; Case++)
    {
        gX_F32 Vertices[3][4] = {{-0.3f,-0.2f,-2.0f,1},{0.7f,-0.1f,-3.0f,1},{-0.1f,0.9f,-4.0f,1}};
        if (Case > 0)
        {
            Vertices[0][2] = Case == 1 ? -0.01f : 0.5f;
            Vertices[1][2] = -1.0f;
            Vertices[2][2] = -2.0f;
        }
        gX_F32 Clips[3][4];
        for (gX_U32 Index = 0; Index < 3; Index++) gX_Tests_ReprojectionRow(Vertices[Index],Projection,Clips[Index]);
        printf("PROJECTION_CLIP %s",Names[Case]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Projection[Index]);
        for (gX_U32 Vertex = 0; Vertex < 3; Vertex++)
            for (gX_U32 Field = 0; Field < 4; Field++) printf(" %a",(gX_F64)Vertices[Vertex][Field]);
        for (gX_U32 Vertex = 0; Vertex < 3; Vertex++)
            for (gX_U32 Field = 0; Field < 4; Field++) printf(" %a",(gX_F64)Clips[Vertex][Field]);
        printf("\n");
    }
}

//ai: CPU radial encoding/comparison witnesses; not the GPU sqrt or texture pipeline.
static gX_I0 gX_Tests_CsmDump(gX_I0)
{
    const gX_F32 Distances[9] = {14.999f,15,15.001f,49.999f,50,50.001f,119.999f,120,120.001f};
    for (gX_U32 Count = 1; Count <= 4; Count++)
    {
        for (gX_U32 Index = 0; Index < 9; Index++)
        {
            gX_F32 Distance = Distances[Index];
            gX_U32 Layer = 0;
            if (Count > 1)
            {
                if (Distance > 120 && Count >= 4) Layer = 3;
                else if (Distance > 50 && Count >= 3) Layer = 2;
                else if (Distance > 15) Layer = 1;
            }
            gX_F32 Boundary = Layer == 1 ? 15.0f : Layer == 2 ? 50.0f : 120.0f;
            gX_F32 U = fminf(fmaxf((Distance-Boundary)/(Boundary*1.1f-Boundary),0.0f),1.0f);
            gX_F32 Weight = U*U*(3.0f-2.0f*U);
            gX_F32 Mixed = (1.0f-Weight)*0.2f+Weight*0.8f;
            printf("CSM_CONTRACT %u %u %a %a %a %a\n",Count,Layer,(gX_F64)Distance,
                (gX_F64)U,(gX_F64)Weight,(gX_F64)Mixed);
        }
    }
    gX_F32 Projection[16];
    gX_Tests_MathDump_Mat16(Projection,gX_Matrix_Ortho(-8,8,-8,8,0.1f,100));
    const gX_F32 Points[3][4] = {{3,-2,-20,1},{-9,1,-20,1},{3,-2,1,1}};
    for (gX_U32 Index = 0; Index < 3; Index++)
    {
        gX_F32 Clip[4];
        gX_Tests_ReprojectionRow(Points[Index],Projection,Clip);
        printf("CSM_PROJECTION %u",Index);
        for (gX_U32 Field = 0; Field < 4; Field++) printf(" %a",(gX_F64)Clip[Field]);
        printf("\n");
    }
}

//ai: CPU radial encoding/comparison witnesses; not the GPU sqrt or texture pipeline.
static gX_I0 gX_Tests_PointShadowDepthDump(gX_I0)
{
    const gX_I8 *Names[6] = {"self","blocked","bias_equal","clear_inside","clear_beyond","rounded"};
    const gX_F32v3 Casters[6] = {{3,4,0},{3,4,0},{3,4,0},{3,4,0},{3,4,0},{0.7f,-1.3f,2.1f}};
    const gX_F32v3 Receivers[6] = {{3,4,0},{6,8,0},{3.75f,5,0},{6,8,0},{6.6f,8.8f,0},{0.8f,-1.4f,2.2f}};
    const gX_F32 Biases[6] = {0,0.125f,1.25f,0,0,0.03f};
    const gX_F32 Fars[6] = {10,10,10,10,10,37};
    for (gX_U32 Case = 0; Case < 6; Case++)
    {
        gX_F32 Caster = gX_F32v3_Distance((gX_F32v3){0,0,0},Casters[Case]);
        gX_F32 Receiver = gX_F32v3_Distance((gX_F32v3){0,0,0},Receivers[Case]);
        gX_F32 Encoded = Caster/Fars[Case];
        gX_F32 Stored = Case == 3 || Case == 4 ? 1.0f : Encoded;
        gX_F32 Decoded = Stored*Fars[Case];
        gX_F32 Biased = Receiver-Biases[Case];
        gX_F32 Fields[15] = {Casters[Case].x,Casters[Case].y,Casters[Case].z,
            Receivers[Case].x,Receivers[Case].y,Receivers[Case].z,Fars[Case],Biases[Case],
            Caster,Receiver,Encoded,Stored,Decoded,Biased,Biased>Decoded ? 1.0f : 0.0f};
        printf("POINT_SHADOW_DEPTH %s",Names[Case]);
        for (gX_U32 Field = 0; Field < 15; Field++) printf(" %a",(gX_F64)Fields[Field]);
        printf("\n");
    }
}

//ai: native LookAt/perspective witnesses for the six GL cubemap capture frames.
static gX_I0 gX_Tests_CubemapDump(gX_I0)
{
    const gX_F32v3 Targets[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const gX_F32v3 Ups[6] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    const gX_F32v3 Position = {2.25f,-1.5f,0.75f};
    const gX_F32 ST[4][2] = {{0.25f,-0.625f},{-0.75f,0.375f},{1.0f,0.25f},{1.125f,-0.375f}};
    for (gX_U32 Face = 0; Face < 6; Face++)
    {
        gX_F32 View[16], Projection[16];
        gX_Tests_MathDump_Mat16(View,gX_Matrix_LookAt(Position,gX_F32v3_Add(Position,Targets[Face]),Ups[Face]));
        gX_Tests_MathDump_Mat16(Projection,gX_Matrix_Perspective(gX_PI/2.0f,1.0f,0.1f,100.0f));
        for (gX_U32 Sample = 0; Sample < 4; Sample++)
        {
            gX_F32 S = ST[Sample][0], T = ST[Sample][1];
            gX_F32v3 Directions[6] = {{1,-T,-S},{-1,-T,S},{S,1,T},{S,-1,-T},{S,-T,1},{-S,-T,-1}};
            gX_F32v3 World = gX_F32v3_Add(Position,gX_F32v3_MulScalar(Directions[Face],3.0f));
            gX_F32 Vertex[4] = {World.x,World.y,World.z,1};
            gX_F32 Eye[4], Clip[4];
            gX_Tests_ReprojectionRow(Vertex,View,Eye);
            gX_Tests_ReprojectionRow(Eye,Projection,Clip);
            printf("CUBEMAP_GEOMETRY %u %u %a %a",Face,Sample,(gX_F64)S,(gX_F64)T);
            for (gX_U32 Field = 0; Field < 4; Field++) printf(" %a",(gX_F64)Eye[Field]);
            for (gX_U32 Field = 0; Field < 4; Field++) printf(" %a",(gX_F64)Clip[Field]);
            printf("\n");
        }
    }
}

//ai: exact hex exports let the independent checker separate stored-float algebra from CPU rounding.
static gX_F32v3 gX_Tests_PlanarReflect(gX_F32v3 Vector, gX_F32v3 Normal, gX_F32 Offset)
{
    gX_F32 Distance = gX_F32v3_Dot(Vector,Normal)-Offset;
    return gX_F32v3_Sub(Vector,gX_F32v3_MulScalar(Normal,2.0f*Distance));
}

//ai: asymmetric native camera/LookAt/VP witnesses; hex exports retain float normal approximation.
static gX_I0 gX_Tests_PlanarDump(gX_I0)
{
    const gX_I8 *Names[] = {"axis","oblique"};
    const gX_F32v3 Normals[] = {{0,1,0},{0.6f,0.8f,0}};
    gX_F32v3 Position = {3,5,7}, Target = {-1,0.4f,-2}, Up = {0.1f,1,0.2f};
    for (gX_U32 Case = 0; Case < 2; Case++)
    {
        gX_F32v3 Normal = Normals[Case];
        gX_F32 Offset = 1.25f;
        gX_F32v3 MirrorPosition = gX_Tests_PlanarReflect(Position,Normal,Offset);
        gX_F32v3 MirrorTarget = gX_Tests_PlanarReflect(Target,Normal,Offset);
        gX_F32v3 MirrorUp = gX_Tests_PlanarReflect(Up,Normal,0);
        gX_Matrix View = gX_Matrix_LookAt(MirrorPosition,MirrorTarget,MirrorUp);
        gX_Matrix Projection = gX_Matrix_Perspective(gX_PI/3.0f,1.4f,0.1f,100.0f);
        gX_Matrix VP = gX_Matrix_Multiply(View,Projection);
        gX_F32v3 Point = Case == 0 ? (gX_F32v3){0.75f,1.25f,-0.6f} : (gX_F32v3){0.75f,1.0f,-0.6f};
        gX_F32v3 ReflectedPoint = gX_Tests_PlanarReflect(Point,Normal,Offset);
        gX_F32v3 Twice = gX_Tests_PlanarReflect(MirrorPosition,Normal,Offset);
        gX_F32 NumericVP[16], Clip[4], Vertex[4] = {ReflectedPoint.x,ReflectedPoint.y,ReflectedPoint.z,1};
        gX_Tests_MathDump_Mat16(NumericVP,VP);
        gX_Tests_ReprojectionRow(Vertex,NumericVP,Clip);
        gX_F32 UV[2] = {(Clip[0]/Clip[3]+1.0f)*0.5f,(Clip[1]/Clip[3]+1.0f)*0.5f};
        printf("PLANAR_GEOMETRY %s",Names[Case]);
        gX_F32v3 Vectors[] = {Normal,Position,Target,Up,MirrorPosition,MirrorTarget,MirrorUp,Point,ReflectedPoint,Twice};
        printf(" %a",(gX_F64)Offset);
        for (gX_U32 Index = 0; Index < 10; Index++)
            printf(" %a %a %a",(gX_F64)Vectors[Index].x,(gX_F64)Vectors[Index].y,(gX_F64)Vectors[Index].z);
        gX_Matrix Matrices[] = {View,Projection,VP};
        for (gX_U32 Index = 0; Index < 3; Index++)
        {
            gX_F32 Values[16];
            gX_Tests_MathDump_Mat16(Values,Matrices[Index]);
            for (gX_U32 Field = 0; Field < 16; Field++) printf(" %a",(gX_F64)Values[Field]);
        }
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)Clip[Index]);
        printf(" %a %a",(gX_F64)UV[0],(gX_F64)UV[1]);
        gX_F32 OriginalView[16];
        gX_Tests_MathDump_Mat16(OriginalView,gX_Matrix_LookAt(Position,Target,Up));
        for (gX_U32 Field = 0; Field < 16; Field++) printf(" %a",(gX_F64)OriginalView[Field]);
        printf("\n");
    }
}

//ai: exact hex exports let the independent checker separate stored-float algebra from CPU rounding.
static gX_I0 gX_Tests_DepthInputDump(gX_I0)
{
    const gX_I8 *Names[] = {"near","mid","far"};
    const gX_F32 Distances[] = {0.125f,3.0f,900.0f};
    gX_Matrix Camera = gX_Matrix_Perspective(gX_PI/3.0f,4.0f/3.0f,0.1f,1000.0f);
    gX_Matrix PreviousCamera = Camera;
    PreviousCamera.m12 = 0.05f;
    gX_F32 Current[16],Inverse[16],Previous[16];
    gX_Tests_MathDump_Mat16(Current,Camera);
    gX_Tests_MathDump_Mat16(Inverse,gX_Matrix_Invert(Camera));
    gX_Tests_MathDump_Mat16(Previous,PreviousCamera);
    for (gX_U32 Case = 0; Case < 3; Case++)
    {
        gX_F32 World[4] = {0.025f,-0.0125f,-Distances[Case],1.0f};
        gX_F32 Clip[4],Recovered[4],PreviousClip[4],Projected[3];
        gX_Tests_ReprojectionRow(World,Current,Clip);
        gX_F32 JitterX = 0.5f/640.0f, JitterY = -0.5f/480.0f;
        gX_F32 UsedJitterX = JitterX+0x1p-16f, UsedJitterY = JitterY-0x1p-16f;
        gX_F32 U = (Clip[0]/Clip[3]+JitterX)*0.5f+0.5f;
        gX_F32 V = (Clip[1]/Clip[3]+JitterY)*0.5f+0.5f;
        gX_F32 NativeDepth = Clip[2]/Clip[3]*0.5f+0.5f;
        gX_F64 GridIndex = round((gX_F64)NativeDepth*16777215.0);
        gX_F32 Depth = (gX_F32)(GridIndex/16777215.0);
        gX_F32 Input[4] = {2.0f*U-1.0f-UsedJitterX,2.0f*V-1.0f-UsedJitterY,2.0f*Depth-1.0f,1.0f};
        gX_Tests_ReprojectionRow(Input,Inverse,Recovered);
        gX_F32 RecoveredW = Recovered[3];
        for (gX_U32 Index = 0; Index < 4; Index++) Recovered[Index] /= RecoveredW;
        gX_Tests_ReprojectionRow(Recovered,Previous,PreviousClip);
        for (gX_U32 Index = 0; Index < 3; Index++) Projected[Index] = PreviousClip[Index]/PreviousClip[3]*0.5f+0.5f;
        printf("DEPTH_INPUT %s",Names[Case]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Current[Index]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Inverse[Index]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Previous[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)World[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)Recovered[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)PreviousClip[Index]);
        for (gX_U32 Index = 0; Index < 3; Index++) printf(" %a",(gX_F64)Projected[Index]);
        printf(" %a %a %a %a %a %a %a %a %a\n",(gX_F64)U,(gX_F64)V,(gX_F64)UsedJitterX,
            (gX_F64)UsedJitterY,(gX_F64)Depth,(gX_F64)JitterX,(gX_F64)JitterY,GridIndex,(gX_F64)NativeDepth);
    }
}

//ai: exact hex exports let the independent checker separate stored-float algebra from CPU rounding.
static gX_I0 gX_Tests_ReprojectionDump(gX_I0)
{
    const gX_I8 *Names[] = {"perspective","diagonal","dependent","hilbert","rounded_singular"};
    const gX_F32 World[4] = {0.25f,-0.125f,-3.0f,1.0f};
    gX_Matrix PreviousMatrix = gX_Matrix_Perspective(gX_PI/3.0f,4.0f/3.0f,0.1f,100.0f);
    PreviousMatrix.m12 = 0.05f;
    gX_F32 Previous[16];
    gX_Tests_MathDump_Mat16(Previous,PreviousMatrix);
    for (gX_U32 Case = 0; Case < 5; Case++)
    {
        gX_F32 Current[16] = {0};
        for (gX_U32 Index = 0; Index < 4; Index++) Current[Index*5] = 1.0f;
        if (Case == 0) gX_Tests_MathDump_Mat16(Current,gX_Matrix_Perspective(gX_PI/3.0f,4.0f/3.0f,0.1f,100.0f));
        if (Case == 1) Current[10] = 0x1p-20f;
        if (Case == 2 || Case == 4)
        {
            Current[1] = Current[4] = 1.0f;
            Current[5] = 1.0f+(Case == 2 ? 0x1p-20f : 0x1p-24f);
        }
        if (Case == 3)
            for (gX_U32 Row = 0; Row < 4; Row++)
                for (gX_U32 Column = 0; Column < 4; Column++)
                    Current[4*Row+Column] = 1.0f/(gX_F32)(Row+Column+1);
        gX_F32 Inverse[16], Clip[4], Recovered[4], PreviousClip[4], Projected[3];
        gX_Tests_MathDump_Mat16(Inverse,gX_Matrix_Invert(gX_Tests_MathDump_Mat(Current)));
        gX_Tests_ReprojectionRow(World,Current,Clip);
        gX_F32 ClipW = Clip[3];
        for (gX_U32 Index = 0; Index < 4; Index++) Clip[Index] /= ClipW;
        gX_Tests_ReprojectionRow(Clip,Inverse,Recovered);
        gX_F32 RecoveredW = Recovered[3];
        for (gX_U32 Index = 0; Index < 4; Index++) Recovered[Index] /= RecoveredW;
        gX_Tests_ReprojectionRow(Recovered,Previous,PreviousClip);
        for (gX_U32 Index = 0; Index < 3; Index++) Projected[Index] = PreviousClip[Index]/PreviousClip[3]*0.5f+0.5f;
        printf("REPROJECTION %s",Names[Case]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Current[Index]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Inverse[Index]);
        for (gX_U32 Index = 0; Index < 16; Index++) printf(" %a",(gX_F64)Previous[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)World[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)Recovered[Index]);
        for (gX_U32 Index = 0; Index < 4; Index++) printf(" %a",(gX_F64)PreviousClip[Index]);
        for (gX_U32 Index = 0; Index < 3; Index++) printf(" %a",(gX_F64)Projected[Index]);
        printf("\n");
    }
}

//ai: seeded randomized differential corpus; input/output floats stay exact in hex.
static gX_F32 mathdump_sq(gX_F32 x);
static gX_F32 mathdump_sq2(gX_F32 x);
static gX_F32 mathdump_f3(gX_F32 x, gX_F32 y, gX_F32 z);
static gX_F32v3 mathdump_v3(gX_F32 x, gX_F32 y, gX_F32 z);

#define GX_MATH_RANDOM_PROBES_DEFAULT 64
#define GX_MATH_RANDOM_PROBES_MAX 4096

static gX_I0 gX_Tests_MathRandomDump(gX_I0)
{
    gX_I32 ProbeCount = GX_MATH_RANDOM_PROBES_DEFAULT;
    const gX_I8 *ProbeEnv = getenv("GX_MATH_RANDOM_PROBES");
    if (ProbeEnv)
    {
        gX_I32 Requested = atoi(ProbeEnv);
        if (Requested > 0) ProbeCount = Requested > GX_MATH_RANDOM_PROBES_MAX
            ? GX_MATH_RANDOM_PROBES_MAX : Requested;
    }
    gX_Random32 Random;
    gX_Random32_Seed(&Random, 0x9E3779B97F4A7C15ull, 0xD1B54A32D192ED03ull);
    for (gX_I32 Probe = 0; Probe < ProbeCount; Probe++)
    {
        gX_F32v3 A, B;
        do
        {
            A = (gX_F32v3){gX_Random32_Range(&Random,-2.0f,2.0f),
                gX_Random32_Range(&Random,-2.0f,2.0f),
                gX_Random32_Range(&Random,-2.0f,2.0f)};
        } while (A.x*A.x + A.y*A.y + A.z*A.z < 0.25f);
        do
        {
            B = (gX_F32v3){gX_Random32_Range(&Random,-2.0f,2.0f),
                gX_Random32_Range(&Random,-2.0f,2.0f),
                gX_Random32_Range(&Random,-2.0f,2.0f)};
        } while (B.x*B.x + B.y*B.y + B.z*B.z < 0.25f);
        gX_F32 T = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 VectorIn[7] = {A.x,A.y,A.z,B.x,B.y,B.z,T};
        gX_F32 VectorOut[16];
        gX_F32v3 Cross = gX_F32v3_Cross(A,B);
        gX_F32v3 Project = gX_F32v3_Project(A,B);
        gX_F32v3 Lerp = gX_F32v3_Lerp(A,B,T);
        gX_F32v3 NormA = gX_F32v3_Normalize(A);
        gX_F32v3 NormB = gX_F32v3_Normalize(B);
        VectorOut[0] = Cross.x; VectorOut[1] = Cross.y; VectorOut[2] = Cross.z;
        VectorOut[3] = gX_F32v3_Dot(A,B);
        VectorOut[4] = Project.x; VectorOut[5] = Project.y; VectorOut[6] = Project.z;
        VectorOut[7] = Lerp.x; VectorOut[8] = Lerp.y; VectorOut[9] = Lerp.z;
        VectorOut[10] = NormA.x; VectorOut[11] = NormA.y; VectorOut[12] = NormA.z;
        VectorOut[13] = NormB.x; VectorOut[14] = NormB.y; VectorOut[15] = NormB.z;
        gX_Tests_RandomLine("vectors",Probe,VectorIn,7,VectorOut,16);

        gX_F32 AValues[16] = {0}, BValues[16] = {0};
        for (gX_I32 Row = 0; Row < 4; Row++)
        {
            for (gX_I32 Column = 0; Column < 4; Column++)
            {
                if (Row == Column) AValues[Row*4+Column] = gX_Random32_Range(&Random,1.5f,2.5f);
                else if (Column < Row) AValues[Row*4+Column] = gX_Random32_Range(&Random,-0.5f,0.5f);
                BValues[Row*4+Column] = gX_Random32_Range(&Random,-1.0f,1.0f);
            }
        }
        gX_Matrix MatrixA = gX_Tests_MathDump_Mat(AValues);
        gX_Matrix MatrixB = gX_Tests_MathDump_Mat(BValues);
        gX_F32v3 Point = {gX_Random32_Range(&Random,-2.0f,2.0f),
            gX_Random32_Range(&Random,-2.0f,2.0f),
            gX_Random32_Range(&Random,-2.0f,2.0f)};
        gX_F32 MatrixIn[35], MatrixOut[52];
        for (gX_I32 i = 0; i < 16; i++) MatrixIn[i] = AValues[i];
        for (gX_I32 i = 0; i < 16; i++) MatrixIn[16+i] = BValues[i];
        MatrixIn[32] = Point.x; MatrixIn[33] = Point.y; MatrixIn[34] = Point.z;
        gX_Tests_MathDump_Mat16(MatrixOut,gX_Matrix_Multiply(MatrixA,MatrixB));
        gX_Tests_MathDump_Mat16(MatrixOut+16,gX_Matrix_Transpose(MatrixA));
        MatrixOut[32] = gX_F32m4x4_Determinant(MatrixA);
        gX_Tests_MathDump_Mat16(MatrixOut+33,gX_Matrix_Invert(MatrixA));
        gX_F32v3 Transformed = gX_F32v3_Transform(Point,MatrixA);
        MatrixOut[49] = Transformed.x; MatrixOut[50] = Transformed.y; MatrixOut[51] = Transformed.z;
        gX_Tests_RandomLine("matrix",Probe,MatrixIn,35,MatrixOut,52);

        gX_F32m2x2 SmallRandom2 = {
            .m0=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m1=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m2=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m3=gX_Random32_Range(&Random,-2.0f,2.0f),
        };
        gX_F32m3x3 SmallRandom3 = {
            .m0=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m1=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m2=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m3=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m4=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m5=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m6=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m7=gX_Random32_Range(&Random,-2.0f,2.0f),
            .m8=gX_Random32_Range(&Random,-2.0f,2.0f),
        };
        gX_F32 SmallIn[22], SmallOut[8];
        SmallIn[0] = SmallRandom2.m0; SmallIn[1] = SmallRandom2.m1;
        SmallIn[2] = SmallRandom2.m2; SmallIn[3] = SmallRandom2.m3;
        SmallIn[4] = SmallRandom3.m0; SmallIn[5] = SmallRandom3.m1;
        SmallIn[6] = SmallRandom3.m2; SmallIn[7] = SmallRandom3.m3;
        SmallIn[8] = SmallRandom3.m4; SmallIn[9] = SmallRandom3.m5;
        SmallIn[10] = SmallRandom3.m6; SmallIn[11] = SmallRandom3.m7;
        SmallIn[12] = SmallRandom3.m8;
        for (gX_I32 i = 0; i < 4; i++) SmallIn[13+i] = gX_Random32_Range(&Random,-2.0f,2.0f);
        for (gX_I32 i = 0; i < 4; i++) SmallIn[17+i] = gX_Random32_Range(&Random,-2.0f,2.0f);
        SmallIn[21] = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F64v4 SmallLerpA = {SmallIn[13],SmallIn[14],SmallIn[15],SmallIn[16]};
        gX_F64v4 SmallLerpB = {SmallIn[17],SmallIn[18],SmallIn[19],SmallIn[20]};
        gX_F64v4 SmallLerpR = gX_F64v4_Lerp(SmallLerpA,SmallLerpB,SmallIn[21]);
        SmallOut[0] = gX_F32m2x2_Determinant(SmallRandom2);
        SmallOut[1] = gX_F32m2x2_Trace(SmallRandom2);
        SmallOut[2] = gX_F32m3x3_Determinant(SmallRandom3);
        SmallOut[3] = gX_F32m3x3_Trace(SmallRandom3);
        SmallOut[4] = (gX_F32)SmallLerpR.x; SmallOut[5] = (gX_F32)SmallLerpR.y;
        SmallOut[6] = (gX_F32)SmallLerpR.z; SmallOut[7] = (gX_F32)SmallLerpR.w;
        gX_Tests_RandomLine("matrix_small",Probe,SmallIn,22,SmallOut,8);

        gX_F32 Q1Values[4], Q2Values[4];
        do
        {
            for (gX_I32 i = 0; i < 4; i++) Q1Values[i] = gX_Random32_Range(&Random,-1.0f,1.0f);
        } while (Q1Values[0]*Q1Values[0] + Q1Values[1]*Q1Values[1]
            + Q1Values[2]*Q1Values[2] + Q1Values[3]*Q1Values[3] < 0.25f);
        do
        {
            for (gX_I32 i = 0; i < 4; i++) Q2Values[i] = gX_Random32_Range(&Random,-1.0f,1.0f);
        } while (Q2Values[0]*Q2Values[0] + Q2Values[1]*Q2Values[1]
            + Q2Values[2]*Q2Values[2] + Q2Values[3]*Q2Values[3] < 0.25f);
        gX_F32Qua Q1 = {Q1Values[0],Q1Values[1],Q1Values[2],Q1Values[3]};
        gX_F32Qua Q2 = {Q2Values[0],Q2Values[1],Q2Values[2],Q2Values[3]};
        gX_F32 QuaternionT = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32v3 QuaternionVector = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32 QuaternionIn[12], QuaternionOut[23];
        for (gX_I32 i = 0; i < 4; i++) QuaternionIn[i] = Q1Values[i];
        for (gX_I32 i = 0; i < 4; i++) QuaternionIn[4+i] = Q2Values[i];
        QuaternionIn[8] = QuaternionT;
        QuaternionIn[9] = QuaternionVector.x;
        QuaternionIn[10] = QuaternionVector.y;
        QuaternionIn[11] = QuaternionVector.z;
        gX_F32Qua QMultiply = gX_F32Qua_Multiply(Q1,Q2);
        gX_F32Qua QNormalize = gX_F32Qua_Normalize(Q1);
        gX_F32Qua QNlerp = gX_F32Qua_Nlerp(Q1,Q2,QuaternionT);
        gX_F32Qua QSlerp = gX_F32Qua_Slerp(Q1,Q2,QuaternionT);
        gX_F32Qua QInverse = gX_F32Qua_Invert(Q1);
        gX_F32v3 QRotated = gX_F32v3_RotateByQuaternion(QuaternionVector,QNormalize);
        QuaternionOut[0] = QMultiply.x; QuaternionOut[1] = QMultiply.y;
        QuaternionOut[2] = QMultiply.z; QuaternionOut[3] = QMultiply.w;
        QuaternionOut[4] = QNormalize.x; QuaternionOut[5] = QNormalize.y;
        QuaternionOut[6] = QNormalize.z; QuaternionOut[7] = QNormalize.w;
        QuaternionOut[8] = QNlerp.x; QuaternionOut[9] = QNlerp.y;
        QuaternionOut[10] = QNlerp.z; QuaternionOut[11] = QNlerp.w;
        QuaternionOut[12] = QSlerp.x; QuaternionOut[13] = QSlerp.y;
        QuaternionOut[14] = QSlerp.z; QuaternionOut[15] = QSlerp.w;
        QuaternionOut[16] = QInverse.x; QuaternionOut[17] = QInverse.y;
        QuaternionOut[18] = QInverse.z; QuaternionOut[19] = QInverse.w;
        QuaternionOut[20] = QRotated.x; QuaternionOut[21] = QRotated.y;
        QuaternionOut[22] = QRotated.z;
        gX_Tests_RandomLine("quaternion",Probe,QuaternionIn,12,QuaternionOut,23);

        gX_F32 Value = gX_Random32_Range(&Random,-5.0f,5.0f);
        gX_F32 Min = gX_Random32_Range(&Random,-5.0f,-0.5f);
        gX_F32 Max = gX_Random32_Range(&Random,0.5f,5.0f);
        gX_F32 InStart = gX_Random32_Range(&Random,-3.0f,3.0f);
        gX_F32 InEnd = InStart + gX_Random32_Range(&Random,0.5f,3.0f);
        gX_F32 OutStart = gX_Random32_Range(&Random,-10.0f,10.0f);
        gX_F32 OutEnd = OutStart + gX_Random32_Range(&Random,0.5f,5.0f);
        gX_F32 LerpA = gX_Random32_Range(&Random,-5.0f,5.0f);
        gX_F32 LerpB = gX_Random32_Range(&Random,-5.0f,5.0f);
        gX_F32 LerpT = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 ScalarIn[10] = {Value,Min,Max,InStart,InEnd,OutStart,OutEnd,LerpA,LerpB,LerpT};
        gX_F32 ScalarOut[5] = {
            gX_F32_Clamp(Value,Min,Max),
            gX_F32_Lerp(LerpA,LerpB,LerpT),
            gX_F32_Normalize(Value,InStart,InEnd),
            gX_F32_Remap(Value,InStart,InEnd,OutStart,OutEnd),
            gX_F32_Wrap(Value,Min,Max),
        };
        gX_Tests_RandomLine("scalar",Probe,ScalarIn,10,ScalarOut,5);

        gX_F32 Start = gX_Random32_Range(&Random,-10.0f,10.0f);
        gX_F32 End = gX_Random32_Range(&Random,-10.0f,10.0f);
        gX_F32 EaseT = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 EaseS = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 EaseP = gX_Random32_Range(&Random,1.2f,3.5f);
        gX_F32 Control0 = gX_Random32_Range(&Random,-10.0f,10.0f);
        gX_F32 Control1 = gX_Random32_Range(&Random,-10.0f,10.0f);
        gX_F32 EasingIn[7] = {Start,End,EaseT,EaseS,EaseP,Control0,Control1};
        gX_F32 EasingOut[5] = {
            gX_F32_EasingStep(Start,End,EaseT,EaseS),
            gX_F32_EasingLinear(Start,End,EaseT),
            gX_F32_EasingQuad(Start,End,EaseT),
            gX_F32_EasingPow(Start,End,EaseT,EaseP),
            gX_F32_EasingBezier(Start,Control0,Control1,End,EaseT),
        };
        gX_Tests_RandomLine("easing",Probe,EasingIn,7,EasingOut,5);

        gX_F32v3 SdfP = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 SdfQ = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32 SdfR = gX_Random32_Range(&Random,0.25f,1.5f);
        gX_F32v3 SdfN = gX_Tests_RandomV3NonZero(&Random,-1.0f,1.0f,0.25f);
        gX_F32v3 SdfBox = gX_Tests_RandomV3(&Random,0.25f,1.5f);
        gX_F32v2 SdfTorus = {gX_Random32_Range(&Random,1.25f,2.5f),
            gX_Random32_Range(&Random,0.1f,0.75f)};
        gX_F32v2 SdfCylinder = {gX_Random32_Range(&Random,0.25f,1.5f),
            gX_Random32_Range(&Random,0.25f,1.5f)};
        gX_F32 SdfTheta = gX_Random32_Range(&Random,0.0f,gX_TAU);
        gX_F32v3 SdfCone = {cosf(SdfTheta),sinf(SdfTheta),
            gX_Random32_Range(&Random,0.25f,1.5f)};
        gX_F32v3 SdfCapsuleA = gX_Tests_RandomV3(&Random,-1.0f,1.0f);
        gX_F32v3 SdfCapsuleDelta = gX_Tests_RandomV3NonZero(&Random,-1.0f,1.0f,0.25f);
        gX_F32v3 SdfCapsuleB = gX_F32v3_Add(SdfCapsuleA,SdfCapsuleDelta);
        gX_F32 SdfCapsuleR = gX_Random32_Range(&Random,0.1f,0.5f);
        gX_F32 SdfOctaR = gX_Random32_Range(&Random,0.25f,1.5f);
        gX_F32 SdfRoundR = gX_Random32_Range(&Random,0.05f,0.4f);
        gX_F32v2 SdfHex = {gX_Random32_Range(&Random,0.25f,1.5f),
            gX_Random32_Range(&Random,0.25f,1.5f)};
        gX_F32 SdfIn[31] = {
            SdfP.x,SdfP.y,SdfP.z,SdfR,
            SdfN.x,SdfN.y,SdfN.z,
            SdfBox.x,SdfBox.y,SdfBox.z,
            SdfTorus.x,SdfTorus.y,
            SdfCylinder.x,SdfCylinder.y,
            SdfCone.x,SdfCone.y,SdfCone.z,
            SdfCapsuleA.x,SdfCapsuleA.y,SdfCapsuleA.z,
            SdfCapsuleB.x,SdfCapsuleB.y,SdfCapsuleB.z,SdfCapsuleR,
            SdfOctaR,SdfRoundR,SdfHex.x,SdfHex.y,
            SdfQ.x,SdfQ.y,SdfQ.z,
        };
        gX_F32 SdfOut[22] = {
            gX_F32_SDF_Sphere(SdfP,SdfR),
            gX_F32_SDF_Plane(SdfP,SdfN),
            gX_F32_SDF_Cube(SdfP,SdfBox),
            gX_F32_SDF_Torus(SdfP,SdfTorus),
            gX_F32_SDF_Cylinder(SdfP,SdfCylinder),
            gX_F32_SDF_Cone(SdfP,SdfCone),
            gX_F32_SDF_Capsule(SdfP,SdfCapsuleA,SdfCapsuleB,SdfCapsuleR),
            gX_F32_SDF_Octahedron(SdfP,SdfOctaR),
            gX_F32_SDF_RoundBox(SdfP,SdfBox,SdfRoundR),
            gX_F32_SDF_HexPrism(SdfP,SdfHex),
            gX_F32_SDF_Menger(SdfP,SdfBox),
            gX_F32_SDF_Sphere(SdfQ,SdfR),
            gX_F32_SDF_Plane(SdfQ,SdfN),
            gX_F32_SDF_Cube(SdfQ,SdfBox),
            gX_F32_SDF_Torus(SdfQ,SdfTorus),
            gX_F32_SDF_Cylinder(SdfQ,SdfCylinder),
            gX_F32_SDF_Cone(SdfQ,SdfCone),
            gX_F32_SDF_Capsule(SdfQ,SdfCapsuleA,SdfCapsuleB,SdfCapsuleR),
            gX_F32_SDF_Octahedron(SdfQ,SdfOctaR),
            gX_F32_SDF_RoundBox(SdfQ,SdfBox,SdfRoundR),
            gX_F32_SDF_HexPrism(SdfQ,SdfHex),
            gX_F32_SDF_Menger(SdfQ,SdfBox),
        };
        gX_Tests_RandomLine("sdf",Probe,SdfIn,31,SdfOut,22);

        gX_F32 EmQ1 = gX_Random32_Range(&Random,-2.0e-6f,2.0e-6f);
        gX_F32 EmQ2 = gX_Random32_Range(&Random,-2.0e-6f,2.0e-6f);
        gX_F32 EmDistance = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 EmFieldCharge = gX_Random32_Range(&Random,-2.0e-6f,2.0e-6f);
        gX_F32v3 EmOffset = gX_Tests_RandomV3NonZero(&Random,-1.0f,1.0f,0.04f);
        gX_F32 EmLorentzCharge = gX_Random32_Range(&Random,-2.0e-6f,2.0e-6f);
        gX_F32v3 EmVelocity = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 EmMagnetic = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32 EmMass = gX_Random32_Range(&Random,0.5f,2.0f);
        gX_F32 EmSpeed = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 EmCharge = gX_Random32_Range(&Random,0.2f,1.0f);
        gX_F32 EmB = gX_Random32_Range(&Random,0.2f,2.0f);
        gX_F32 EmWavelength = gX_Random32_Range(&Random,100.0e-9f,1000.0e-9f);
        gX_F32 EmIn[19] = {
            EmQ1,EmQ2,EmDistance,
            EmFieldCharge,EmOffset.x,EmOffset.y,EmOffset.z,
            EmLorentzCharge,EmVelocity.x,EmVelocity.y,EmVelocity.z,
            EmMagnetic.x,EmMagnetic.y,EmMagnetic.z,
            EmMass,EmSpeed,EmCharge,EmB,EmWavelength,
        };
        gX_F32v3 EmField = gX_F32v3_Physics_ElectricField(EmFieldCharge,EmOffset);
        gX_F32v3 EmLorentz = gX_F32v3_Physics_LorentzForce(EmLorentzCharge,
            EmVelocity,EmMagnetic);
        gX_F32 EmOut[10] = {
            gX_F32_Physics_CoulombForce(EmQ1,EmQ2,EmDistance),
            gX_F32_Physics_CoulombPotential(EmQ1,EmQ2,EmDistance),
            EmField.x,EmField.y,EmField.z,
            EmLorentz.x,EmLorentz.y,EmLorentz.z,
            gX_F32_Physics_CyclotronRadius(EmMass,EmSpeed,EmCharge,EmB),
            gX_F32_Physics_PhotonEnergy(EmWavelength),
        };
        gX_Tests_RandomLine("em",Probe,EmIn,19,EmOut,10);

        gX_F32v3 DipoleM1 = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 DipoleM2 = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 DipoleOffset = gX_Tests_RandomV3NonZero(&Random,-2.0f,2.0f,0.04f);
        gX_F32 CoaxM1 = gX_Random32_Range(&Random,0.5f,2.0f);
        gX_F32 CoaxM2 = gX_Random32_Range(&Random,0.5f,2.0f);
        gX_F32 CoaxR = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 AxisM = gX_Random32_Range(&Random,0.5f,2.0f);
        gX_F32 AxisR = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 EqM = gX_Random32_Range(&Random,0.5f,2.0f);
        gX_F32 EqR = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 DipoleIn[16] = {
            DipoleM1.x,DipoleM1.y,DipoleM1.z,
            DipoleM2.x,DipoleM2.y,DipoleM2.z,
            DipoleOffset.x,DipoleOffset.y,DipoleOffset.z,
            CoaxM1,CoaxM2,CoaxR,AxisM,AxisR,EqM,EqR,
        };
        gX_F32v3 DipoleField = gX_F32v3_Physics_DipoleField(DipoleM1,DipoleOffset);
        gX_F32v3 DipoleTorque = gX_F32v3_Physics_DipoleTorque(DipoleM2,DipoleField);
        gX_F32v3 DipoleForce = gX_F32v3_Physics_DipoleForce(DipoleM1,DipoleM2,
            DipoleOffset);
        gX_F32 DipoleOut[13] = {
            DipoleField.x,DipoleField.y,DipoleField.z,
            DipoleTorque.x,DipoleTorque.y,DipoleTorque.z,
            DipoleForce.x,DipoleForce.y,DipoleForce.z,
            gX_F32_Physics_DipoleForceOnAxis(CoaxM1,CoaxM2,CoaxR),
            gX_F32_Physics_DipolePotential(DipoleM1,DipoleM2,DipoleOffset),
            gX_F32_Physics_DipoleFieldOnAxis(AxisM,AxisR),
            gX_F32_Physics_DipoleFieldEquatorial(EqM,EqR),
        };
        gX_Tests_RandomLine("dipole",Probe,DipoleIn,16,DipoleOut,13);

        gX_F32v3 CoordP = gX_Tests_RandomV3NonZero(&Random,-2.0f,2.0f,0.25f);
        gX_F32 CoordIn[3] = {CoordP.x,CoordP.y,CoordP.z};
        gX_F32v3 CoordCyl = gX_F32_Convert_cartesian2cylinder(CoordP);
        gX_F32v3 CoordSph = gX_F32_Convert_cartesian2spherical(CoordP);
        gX_F32 CoordOut[18];
        gX_I32 CoordN = 0;
        gX_Tests_MathDump_V3(CoordOut,&CoordN,CoordCyl);
        gX_Tests_MathDump_V3(CoordOut,&CoordN,gX_F32_Convert_cylinder2cartesian(CoordCyl));
        gX_Tests_MathDump_V3(CoordOut,&CoordN,CoordSph);
        gX_Tests_MathDump_V3(CoordOut,&CoordN,gX_F32_Convert_spherical2cartesian(CoordSph));
        gX_Tests_MathDump_V3(CoordOut,&CoordN,gX_F32_Convert_cylinder2spherical(CoordCyl));
        gX_Tests_MathDump_V3(CoordOut,&CoordN,gX_F32_Convert_spherical2cylinder(CoordSph));
        gX_Tests_RandomLine("convert_coords",Probe,CoordIn,3,CoordOut,18);

        gX_F32v3 SkyEq = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32 SkyLat = gX_Random32_Range(&Random,-1.4f,1.4f);
        gX_F32 SkyLon = gX_Random32_Range(&Random,-gX_PI,gX_PI);
        gX_F32 SkyIn[5] = {SkyEq.x,SkyEq.y,SkyEq.z,SkyLat,SkyLon};
        gX_F32v3 SkyEcl = gX_F32v3_Convert_Equatorial2Ecliptic(SkyEq);
        gX_F32v3 SkyEqBack = gX_F32v3_Convert_Ecliptic2Equatorial(SkyEcl);
        gX_F32v3 SkyHor = gX_F32v3_Convert_Equatorial2Horizontal(SkyEq,SkyLat,SkyLon);
        gX_F32 SkyOut[24];
        gX_I32 SkyN = 0;
        gX_Tests_MathDump_V3(SkyOut,&SkyN,SkyEcl);
        gX_Tests_MathDump_V3(SkyOut,&SkyN,gX_F32v3_Convert_Ecliptic2Equatorial(SkyEq));
        gX_Tests_MathDump_V3(SkyOut,&SkyN,SkyEqBack);
        gX_Tests_MathDump_V3(SkyOut,&SkyN,gX_F32v3_Convert_Equatorial2Ecliptic(SkyEqBack));
        gX_Tests_MathDump_V3(SkyOut,&SkyN,SkyHor);
        gX_Tests_MathDump_V3(SkyOut,&SkyN,
            gX_F32v3_Convert_Horizontal2Equatorial(SkyEq,SkyLat,SkyLon));
        gX_Tests_MathDump_V3(SkyOut,&SkyN,
            gX_F32v3_Convert_Horizontal2Equatorial(SkyHor,SkyLat,SkyLon));
        gX_Tests_MathDump_V3(SkyOut,&SkyN,gX_F32v3_Convert_Equatorial2Horizontal(
            gX_F32v3_Convert_Horizontal2Equatorial(SkyHor,SkyLat,SkyLon),SkyLat,SkyLon));
        gX_Tests_RandomLine("convert_sky",Probe,SkyIn,5,SkyOut,24);

        gX_F32 NewLinear = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 NewSrgb = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 NewAmp = gX_Random32_Range(&Random,0.1f,2.0f);
        gX_F32 NewDb = gX_Random32_Range(&Random,-20.0f,20.0f);
        gX_F32 NewNote = gX_Random32_Range(&Random,0.0f,127.0f);
        gX_F32 NewFreq = gX_Random32_Range(&Random,20.0f,20000.0f);
        gX_F32 NewCelsius = gX_Random32_Range(&Random,-40.0f,100.0f);
        gX_F32 NewFahrenheit = gX_Random32_Range(&Random,-40.0f,212.0f);
        gX_F32 NewIn[8] = {NewLinear,NewSrgb,NewAmp,NewDb,NewNote,NewFreq,
            NewCelsius,NewFahrenheit};
        gX_F32 NewLinearSrgb = gX_F32_Convert_Linear2SRGB(NewLinear);
        gX_F32 NewSrgbLinear = gX_F32_Convert_SRGB2Linear(NewSrgb);
        gX_F32 NewAmpDb = gX_F32_Convert_Amplitude2Decibel(NewAmp);
        gX_F32 NewDbAmp = gX_F32_Convert_Decibel2Amplitude(NewDb);
        gX_F32 NewNoteFreq = gX_F32_Convert_MidiNote2Frequency(NewNote);
        gX_F32 NewFreqNote = gX_F32_Convert_Frequency2MidiNote(NewFreq);
        gX_F32 NewCelsiusF = gX_F32_Convert_Celsius2Fahrenheit(NewCelsius);
        gX_F32 NewFahrenheitC = gX_F32_Convert_Fahrenheit2Celsius(NewFahrenheit);
        gX_F32 NewOut[16] = {
            NewLinearSrgb,NewSrgbLinear,NewAmpDb,NewDbAmp,
            NewNoteFreq,NewFreqNote,NewCelsiusF,NewFahrenheitC,
            gX_F32_Convert_SRGB2Linear(NewLinearSrgb),
            gX_F32_Convert_Linear2SRGB(NewSrgbLinear),
            gX_F32_Convert_Decibel2Amplitude(NewAmpDb),
            gX_F32_Convert_Amplitude2Decibel(NewDbAmp),
            gX_F32_Convert_Frequency2MidiNote(NewNoteFreq),
            gX_F32_Convert_MidiNote2Frequency(NewFreqNote),
            gX_F32_Convert_Fahrenheit2Celsius(NewCelsiusF),
            gX_F32_Convert_Celsius2Fahrenheit(NewFahrenheitC),
        };
        gX_Tests_RandomLine("convert_new",Probe,NewIn,8,NewOut,16);

        gX_F32v3 ExtP = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 ExtQ = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32v3 ExtBox = gX_Tests_RandomV3(&Random,0.25f,1.5f);
        gX_F32 ExtEdge = gX_Random32_Range(&Random,0.05f,0.5f);
        gX_F32v3 ExtM = gX_Tests_RandomV3(&Random,0.2f,1.0f);
        gX_F32v4 ExtParam = {gX_Random32_Range(&Random,0.5f,1.5f),
            gX_Random32_Range(&Random,2.0f,8.0f),
            (gX_F32)(4 + gX_Random32_Bounded(&Random,5)),
            gX_Random32_Range(&Random,1.5f,3.0f)};
        gX_F32v3 ExtC = gX_Tests_RandomV3(&Random,1.5f,3.0f);
        gX_F32v3 ExtA = gX_Tests_RandomV3(&Random,1.0f,3.0f);
        gX_F32v3 ExtD = gX_Tests_RandomV3(&Random,0.5f,2.0f);
        gX_F32 ExtIn[26] = {
            ExtP.x,ExtP.y,ExtP.z,ExtBox.x,ExtBox.y,ExtBox.z,ExtEdge,
            ExtM.x,ExtM.y,ExtM.z,
            ExtParam.x,ExtParam.y,ExtParam.z,ExtParam.w,
            ExtC.x,ExtC.y,ExtC.z,ExtA.x,ExtA.y,ExtA.z,ExtD.x,ExtD.y,ExtD.z,
            ExtQ.x,ExtQ.y,ExtQ.z,
        };
        gX_F32v3 ExtRepeated = gX_F32v3_SDF_RepeatInfinite(ExtP,ExtC);
        gX_F32v3 ExtFinite = gX_F32v3_SDF_RepeatFinite(ExtP,ExtA,ExtD);
        gX_F32 ExtOut[9] = {
            gX_F32_SDF_BoxFrame(ExtP,ExtBox,ExtEdge),
            gX_F32_SDF_Mandelbulb(ExtM,ExtParam),
            ExtRepeated.x,ExtRepeated.y,ExtRepeated.z,
            ExtFinite.x,ExtFinite.y,ExtFinite.z,
            gX_F32_SDF_BoxFrame(ExtQ,ExtBox,ExtEdge),
        };
        gX_Tests_RandomLine("sdf_ext",Probe,ExtIn,26,ExtOut,9);

        gX_F32 UnitsMeters = gX_Random32_Range(&Random,0.1f,1000.0f);
        gX_F32 UnitsFeet = gX_Random32_Range(&Random,1.0f,100.0f);
        gX_F32 UnitsInches = gX_Random32_Range(&Random,1.0f,1000.0f);
        gX_F32 UnitsKelvin = gX_Random32_Range(&Random,100.0f,500.0f);
        gX_F32 UnitsCelsius = gX_Random32_Range(&Random,-50.0f,150.0f);
        const gX_I32 Prefixes[8] = {-9,-6,-3,-2,-1,1,2,3};
        gX_I32 UnitsPrefix = Prefixes[gX_Random32_Bounded(&Random,8)];
        gX_F32 UnitsIn[6] = {UnitsMeters,UnitsFeet,UnitsInches,UnitsKelvin,
            UnitsCelsius,(gX_F32)UnitsPrefix};
        gX_F32 UnitsOut[13] = {
            gX_F32_Convert_Foot2Meter(UnitsFeet),
            gX_F32_Convert_Meter2Foot(UnitsMeters),
            gX_F32_Convert_Inch2Meter(UnitsInches),
            gX_F32_Convert_Meter2Inch(UnitsMeters),
            gX_F32_Convert_Kelvin2Celsius(UnitsKelvin),
            gX_F32_Convert_Celsius2Kelvin(UnitsCelsius),
            gX_F32_Convert_MeterPrefix(UnitsMeters,UnitsPrefix),
            gX_F32_Convert_Meter2Foot(gX_F32_Convert_Foot2Meter(UnitsFeet)),
            gX_F32_Convert_Foot2Meter(gX_F32_Convert_Meter2Foot(UnitsMeters)),
            gX_F32_Convert_Meter2Inch(gX_F32_Convert_Inch2Meter(UnitsInches)),
            gX_F32_Convert_Inch2Meter(gX_F32_Convert_Meter2Inch(UnitsMeters)),
            gX_F32_Convert_Celsius2Kelvin(gX_F32_Convert_Kelvin2Celsius(UnitsKelvin)),
            gX_F32_Convert_Kelvin2Celsius(gX_F32_Convert_Celsius2Kelvin(UnitsCelsius)),
        };
        gX_Tests_RandomLine("convert_units",Probe,UnitsIn,6,UnitsOut,13);

        gX_F32v3 ColorRGB = gX_Tests_RandomV3(&Random,0.0f,1.0f);
        gX_F32v3 ColorHSV = {gX_Random32_Range(&Random,0.0f,360.0f),
            gX_Random32_Range(&Random,0.0f,1.0f),
            gX_Random32_Range(&Random,0.0f,1.0f)};
        gX_F32 ColorIn[6] = {ColorRGB.x,ColorRGB.y,ColorRGB.z,
            ColorHSV.x,ColorHSV.y,ColorHSV.z};
        gX_F32v3 ColorFromRGB = gX_F32v3_Convert_RGB2HSV(ColorRGB);
        gX_F32v3 ColorFromHSV = gX_F32v3_Convert_HSV2RGB(ColorHSV);
        gX_F32v3 ColorBackRGB = gX_F32v3_Convert_HSV2RGB(ColorFromRGB);
        gX_F32v3 ColorBackHSV = gX_F32v3_Convert_RGB2HSV(ColorFromHSV);
        gX_F32 ColorOut[12] = {
            ColorFromRGB.x,ColorFromRGB.y,ColorFromRGB.z,
            ColorFromHSV.x,ColorFromHSV.y,ColorFromHSV.z,
            ColorBackRGB.x,ColorBackRGB.y,ColorBackRGB.z,
            ColorBackHSV.x,ColorBackHSV.y,ColorBackHSV.z,
        };
        gX_Tests_RandomLine("convert_color",Probe,ColorIn,6,ColorOut,12);

        gX_F32v3 ProjP = gX_F32v3_Normalize(
            gX_Tests_RandomV3NonZero(&Random,-2.0f,2.0f,0.25f));
        gX_F32 ProjIn[3] = {ProjP.x,ProjP.y,ProjP.z};
        gX_F32v2 ProjH = gX_F32v2_Convert_Vector2Horizontal(ProjP);
        gX_F32v2 ProjE = gX_F32v2_Convert_Vector2Equatorial(ProjP);
        gX_F32 ProjOut[10];
        gX_I32 ProjN = 0;
        gX_Tests_MathDump_V2(ProjOut,&ProjN,ProjH);
        gX_Tests_MathDump_V3(ProjOut,&ProjN,gX_F32v3_Convert_Horizontal2Vector(ProjH));
        gX_Tests_MathDump_V2(ProjOut,&ProjN,ProjE);
        gX_Tests_MathDump_V3(ProjOut,&ProjN,gX_F32v3_Convert_Equatorial2Vector(ProjE));
        gX_Tests_RandomLine("convert_proj",Probe,ProjIn,3,ProjOut,10);

        gX_F32v3 BallP = gX_F32v3_Normalize(
            gX_Tests_RandomV3NonZero(&Random,-2.0f,2.0f,0.25f));
        gX_F32 BallIn[3] = {BallP.x,BallP.y,BallP.z};
        gX_F32v2 BallB = gX_F32_Convert_space2ball(BallP);
        gX_F32 BallOut[5];
        gX_I32 BallN = 0;
        gX_Tests_MathDump_V2(BallOut,&BallN,BallB);
        gX_Tests_MathDump_V3(BallOut,&BallN,gX_F32_Convert_ball2space(BallB));
        gX_Tests_RandomLine("convert_ball",Probe,BallIn,3,BallOut,5);

        gX_Fourier_Workspace *Fourier = gX_Fourier_Workspace_Create(3);
        if (Fourier)
        {
            gX_F32_complex FourierData[8];
            gX_F32 FourierIn[16], FourierOut[16];
            for (gX_I32 i = 0; i < 8; i++)
            {
                gX_F32 Real = gX_Random32_Range(&Random,-2.0f,2.0f);
                gX_F32 Imag = gX_Random32_Range(&Random,-2.0f,2.0f);
                FourierData[i] = CMPLXF(Real,Imag);
                FourierIn[2*i] = Real; FourierIn[2*i+1] = Imag;
            }
            gX_Fourier_FFT(Fourier,FourierData);
            for (gX_I32 i = 0; i < 8; i++)
            {
                FourierOut[2*i] = crealf(FourierData[i]);
                FourierOut[2*i+1] = cimagf(FourierData[i]);
            }
            gX_Tests_RandomLine("fft",Probe,FourierIn,16,FourierOut,16);
            gX_Fourier_Workspace_Destroy(Fourier);
        }

        gX_F32 CalcX = gX_Random32_Range(&Random,-2.0f,2.0f);
        gX_F32 CalcD = 5.0e-4f;
        gX_F32 CalcA = gX_Random32_Range(&Random,-2.0f,2.0f);
        gX_F32 CalcB = CalcA + gX_Random32_Range(&Random,0.5f,3.0f);
        gX_F32v3 CalcP = gX_Tests_RandomV3(&Random,-2.0f,2.0f);
        gX_F32 CalcIn[8] = {CalcX,CalcD,100.0f,CalcA,CalcB,
            CalcP.x,CalcP.y,CalcP.z};
        gX_F32 CalcOut[10];
        gX_I32 CalcN = 0;
        CalcOut[CalcN++] = gX_F32_Calculus_Differentiate(mathdump_sq,CalcX,CalcD);
        gX_F32v3 CalcGrad = gX_F32v3_Calculus_Nabla(mathdump_f3,CalcP,CalcD);
        gX_Tests_MathDump_V3(CalcOut,&CalcN,CalcGrad);
        CalcOut[CalcN++] = gX_F32_Calculus_Diverge(mathdump_v3,CalcP,CalcD);
        gX_F32v3 CalcCurl = gX_F32v3_Calculus_Curl(mathdump_v3,CalcP,CalcD);
        gX_Tests_MathDump_V3(CalcOut,&CalcN,CalcCurl);
        CalcOut[CalcN++] = gX_F32_Calculus_Simpson(mathdump_sq,100,CalcA,CalcB);
        CalcOut[CalcN++] = gX_F32_Newton(mathdump_sq2,1.0f,CalcD,20);
        gX_Tests_RandomLine("calculus",Probe,CalcIn,8,CalcOut,10);

        gX_Color ColorC1 = {
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
        };
        gX_Color ColorC2 = {
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
            (gX_U8)gX_Random32_Bounded(&Random,256),
        };
        gX_F32 ColorT = gX_Random32_Range(&Random,0.0f,1.0f);
        gX_F32 NamedColorIn[9] = {
            ColorC1.r,ColorC1.g,ColorC1.b,ColorC1.a,
            ColorC2.r,ColorC2.g,ColorC2.b,ColorC2.a,ColorT,
        };
        gX_Color ColorLerp = gX_Color_Lerp(ColorC1,ColorC2,ColorT);
        gX_Color ColorLerpRGB = gX_Color_LerpRGB(ColorC1,ColorC2,ColorT);
        gX_Color ColorMul = gX_Color_Mul(ColorC1,ColorC2);
        gX_F32v4 ColorV4 = gX_Color_ToV4(ColorC1);
        gX_F32 NamedColorOut[16] = {
            ColorLerp.r,ColorLerp.g,ColorLerp.b,ColorLerp.a,
            ColorLerpRGB.r,ColorLerpRGB.g,ColorLerpRGB.b,ColorLerpRGB.a,
            ColorMul.r,ColorMul.g,ColorMul.b,ColorMul.a,
            ColorV4.x,ColorV4.y,ColorV4.z,ColorV4.w,
        };
        gX_Tests_RandomLine("color",Probe,NamedColorIn,9,NamedColorOut,16);
    }
    fflush(stdout);
}

//opencode: fixed calculus test functions for the math-pass dump — the C API
//   takes function pointers, so the dump carries the concrete probes.
static gX_F32 mathdump_sq(gX_F32 x) { return x * x; }
static gX_F32 mathdump_sq2(gX_F32 x) { return x * x - 2.0f; }
static gX_F32 mathdump_f3(gX_F32 x, gX_F32 y, gX_F32 z) { return x * x + y * z; }
static gX_F32v3 mathdump_v3(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_F32v3 r = { x * y, y * z, z * x };
    return r;
}

//ai: opt-in native reprojection fixtures do not alter the existing CAS corpus.
gX_I32 gX_Tests_MathDump(gX_I0) {
    const gX_I8 *ReprojectionDump = getenv("GX_REPROJECTION_DUMP");
    const gX_I8 *PlanarDump = getenv("GX_PLANAR_GEOMETRY_DUMP");
    const gX_I8 *ProjectionClipDump = getenv("GX_PROJECTION_CLIP_DUMP");
    const gX_I8 *CubemapDump = getenv("GX_CUBEMAP_GEOMETRY_DUMP");
    const gX_I8 *PointShadowDump = getenv("GX_POINT_SHADOW_DEPTH_DUMP");
    const gX_I8 *CsmDump = getenv("GX_CSM_CONTRACT_DUMP");
    const gX_I8 *RandomDump = getenv("GX_MATH_RANDOM_DUMP");
    if (RandomDump && RandomDump[0] == '1')
    {
        gX_Tests_MathRandomDump();
        return 0;
    }
    if (CsmDump && CsmDump[0] == '1')
    {
        gX_Tests_CsmDump();
        return 0;
    }
    if (PointShadowDump && PointShadowDump[0] == '1')
    {
        gX_Tests_PointShadowDepthDump();
        return 0;
    }
    if (CubemapDump && CubemapDump[0] == '1')
    {
        gX_Tests_CubemapDump();
        return 0;
    }
    if (ProjectionClipDump && ProjectionClipDump[0] == '1')
    {
        gX_Tests_ProjectionClipDump();
        return 0;
    }
    if (PlanarDump && PlanarDump[0] == '1')
    {
        gX_Tests_PlanarDump();
        return 0;
    }
    if (ReprojectionDump && ReprojectionDump[0] == '1')
    {
        gX_Tests_ReprojectionDump();
        gX_Tests_DepthInputDump();
        return 0;
    }
    gX_F32 buf[256];
    // A TRS-like matrix with exact entries: rotate 90 deg about Z, scale 2 on
    // Z, translate (3,4,5) — invertible with a rational inverse.
    const gX_F32 trs[16] = {
        0, 1, 0, 0,
        -1, 0, 0, 0,
        0, 0, 2, 0,
        3, 4, 5, 1,
    };
    {
        const gX_F32 l[16] = { 1, 2, 0, -1,  0, 3, 1, 0,  -2, 0, 1, 4,  0, 0, 0, 1 };
        const gX_F32 r[16] = { 2, 0, -1, 0,  1, 1, 0, 0,  0, -3, 2, 1,  4, 0, 0, 1 };
        gX_Matrix O = gX_Matrix_Multiply(gX_Tests_MathDump_Mat(l), gX_Tests_MathDump_Mat(r));
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = l[i];
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = r[i];
        gX_F32 o[16]; gX_Tests_MathDump_Mat16(o, O);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = o[i];
        gX_Tests_MathDump_Line("multiply", buf, n);
    }
    {
        gX_Matrix O = gX_Matrix_Invert(gX_Tests_MathDump_Mat(trs));
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = trs[i];
        gX_F32 o[16]; gX_Tests_MathDump_Mat16(o, O);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = o[i];
        gX_Tests_MathDump_Line("invert", buf, n);
    }
    {
        gX_F32v3 v = { 1, 2, 3 };
        gX_F32v3 o = gX_F32v3_Transform(v, gX_Tests_MathDump_Mat(trs));
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = trs[i];
        buf[n++] = v.x; buf[n++] = v.y; buf[n++] = v.z;
        buf[n++] = o.x; buf[n++] = o.y; buf[n++] = o.z;
        gX_Tests_MathDump_Line("transform", buf, n);
    }
    {
        gX_F32 d = gX_F32m4x4_Determinant(gX_Tests_MathDump_Mat(trs));
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = trs[i];
        buf[n++] = d;
        gX_Tests_MathDump_Line("determinant", buf, n);
    }
    {
        const gX_F32 args[4] = { 1.0471975512f, 1.5f, 0.1f, 100.0f }; // 60 deg
        gX_Matrix O = gX_Matrix_Perspective(args[0], args[1], args[2], args[3]);
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 4; i++) buf[n++] = args[i];
        gX_F32 o[16]; gX_Tests_MathDump_Mat16(o, O);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = o[i];
        gX_Tests_MathDump_Line("perspective", buf, n);
    }
    {
        gX_F32v3 ang = { 0.5235987756f, 0.7853981634f, 1.0471975512f }; // 30/45/60 deg
        gX_Matrix O = gX_Matrix_RotateXYZ(ang);
        gX_I32 n = 0;
        buf[n++] = ang.x; buf[n++] = ang.y; buf[n++] = ang.z;
        gX_F32 o[16]; gX_Tests_MathDump_Mat16(o, O);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = o[i];
        gX_Tests_MathDump_Line("rotate_xyz", buf, n);
    }
    {
        gX_F32v3 eye = { 3, 2, 4 };
        gX_F32v3 target = { 0, 1, 0 };
        gX_F32v3 up = { 0, 1, 0 };
        gX_Matrix O = gX_Matrix_LookAt(eye, target, up);
        gX_I32 n = 0;
        buf[n++] = eye.x; buf[n++] = eye.y; buf[n++] = eye.z;
        buf[n++] = target.x; buf[n++] = target.y; buf[n++] = target.z;
        buf[n++] = up.x; buf[n++] = up.y; buf[n++] = up.z;
        gX_F32 o[16]; gX_Tests_MathDump_Mat16(o, O);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = o[i];
        gX_Tests_MathDump_Line("lookat", buf, n);
    }
    {
        // PCG32: the published demo seed/stream (42, 54) plus the engine's
        // own path — the oracle reassembles the hi/lo halves exactly.
        gX_Random32 r;
        gX_Random32_Seed(&r, 42u, 54u);
        gX_I32 n = 0;
        buf[n++] = (gX_F32)(42u >> 16); buf[n++] = (gX_F32)(42u & 0xFFFFu);
        buf[n++] = (gX_F32)(54u >> 16); buf[n++] = (gX_F32)(54u & 0xFFFFu);
        for (gX_I32 i = 0; i < 6; i++) {
            gX_U32 v = gX_Random32_Next(&r);
            buf[n++] = (gX_F32)(v >> 16); buf[n++] = (gX_F32)(v & 0xFFFFu);
        }
        gX_Random32_Seed(&r, 42u, 54u);
        for (gX_I32 i = 0; i < 4; i++) {
            gX_U32 v = gX_Random32_Bounded(&r, 100u);
            buf[n++] = (gX_F32)(v >> 16); buf[n++] = (gX_F32)(v & 0xFFFFu);
        }
        gX_Tests_MathDump_Line("random", buf, n);
    }
    {
        gX_F32v3 a = { 1, 2, 3 };
        gX_F32v3 b = { -2, 0.5f, 4 };
        gX_F32v3 c = gX_F32v3_Cross(a, b);
        gX_F32 d = gX_F32v3_Dot(a, b);
        gX_F32v3 pv = gX_F32v3_Project(a, b);
        gX_I32 n = 0;
        buf[n++] = a.x; buf[n++] = a.y; buf[n++] = a.z;
        buf[n++] = b.x; buf[n++] = b.y; buf[n++] = b.z;
        buf[n++] = c.x; buf[n++] = c.y; buf[n++] = c.z;
        buf[n++] = d;
        buf[n++] = pv.x; buf[n++] = pv.y; buf[n++] = pv.z;
        gX_Tests_MathDump_Line("vectors", buf, n);
    }
    //ai: quaternion algebra corpus — multiply/invert/normalize/lerp/nlerp/slerp.
    {
        // quaternion: q1(4) q2(4) qa(4) qb(4) t(1)
        //   -> multiply(4), invert(4), normalize(4), lerp(4), nlerp(4), slerp(4)
        const gX_F32Qua q1 = { 1.0f, 2.0f, 3.0f, 4.0f };
        const gX_F32Qua q2 = { -2.0f, 0.5f, 1.5f, -3.0f };
        const gX_F32Qua qa = { 0.6f, 0.8f, 0.0f, 0.0f };
        const gX_F32Qua qb = { 0.0f, 0.0f, 0.6f, 0.8f };
        gX_I32 n = 0;
        gX_Tests_MathDump_V4(buf, &n, q1);
        gX_Tests_MathDump_V4(buf, &n, q2);
        gX_Tests_MathDump_V4(buf, &n, qa);
        gX_Tests_MathDump_V4(buf, &n, qb);
        buf[n++] = 0.25f;
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Multiply(q1, q2));
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Invert(q1));
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Normalize(q1));
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Lerp(q1, q2, 0.25f));
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Nlerp(q1, q2, 0.25f));
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_Slerp(qa, qb, 0.25f));
        gX_Tests_MathDump_Line("quaternion", buf, n);
    }
    //ai: quaternion construction/decomposition and matrix round-trip corpus.
    {
        // quaternion_rot: axis(3) angle euler(3) q(4)
        //   -> fromAxis(4), toAxis(3)+angle(1), fromEuler(4),
        //      toEuler(3), toMatrix(16), fromMatrix(4)
        const gX_F32v3 axis = { 0.0f, 2.0f, 0.0f };
        const gX_F32 angle = gX_PI / 3.0f;
        const gX_F32v3 euler = { 0.3f, -0.4f, 0.5f };
        const gX_F32Qua q = { 0.0f, 0.6f, 0.0f, 0.8f };
        const gX_F32Qua fromAxis = gX_F32Qua_FromAxisAngle(axis, angle);
        const gX_F32Qua fromEuler = gX_F32Qua_FromEuler(euler.x, euler.y, euler.z);
        const gX_Matrix matrix = gX_F32Qua_ToMatrix(q);
        gX_F32v3 outAxis;
        gX_F32 outAngle;
        gX_F32 values[16];
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, axis);
        buf[n++] = angle;
        gX_Tests_MathDump_V3(buf, &n, euler);
        gX_Tests_MathDump_V4(buf, &n, q);
        gX_Tests_MathDump_V4(buf, &n, fromAxis);
        gX_F32Qua_ToAxisAngle(fromAxis, &outAxis, &outAngle);
        gX_Tests_MathDump_V3(buf, &n, outAxis);
        buf[n++] = outAngle;
        gX_Tests_MathDump_V4(buf, &n, fromEuler);
        gX_Tests_MathDump_V3(buf, &n, gX_F32Qua_ToEuler(fromEuler));
        gX_Tests_MathDump_Mat16(values, matrix);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = values[i];
        gX_Tests_MathDump_V4(buf, &n, gX_F32Qua_FromMatrix(matrix));
        gX_Tests_MathDump_Line("quaternion_rot", buf, n);
    }
    //ai: matrix auxiliary corpus — transpose/add/subtract/elementwise/cofactor/adjugate/projections/constructor.
    {
        // matrix_aux: a(16) b(16) frustum(6) ortho(6) vectors(16)
        //   -> transpose, add, subtract, multiplyIndices, divideIndices,
        //      cofactor, adjugate, frustum, ortho, To_F32a16, From_F32v4 (16 each)
        const gX_F32 a[16] = { 0,1,0,0, -1,0,0,0, 0,0,2,0, 3,4,5,1 };
        const gX_F32 b[16] = { 2,1,-1,3, -2,4,1,0.5f, 0.5f,-3,2,1, 4,-1,2,5 };
        const gX_F32v4 v1 = { 1,2,3,4 };
        const gX_F32v4 v2 = { 5,6,7,8 };
        const gX_F32v4 v3 = { 9,10,11,12 };
        const gX_F32v4 v4 = { 13,14,15,16 };
        gX_Matrix A = gX_Tests_MathDump_Mat(a);
        gX_Matrix B = gX_Tests_MathDump_Mat(b);
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = a[i];
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = b[i];
        buf[n++] = -2; buf[n++] = 3; buf[n++] = -1;
        buf[n++] = 4; buf[n++] = 0.5f; buf[n++] = 100;
        buf[n++] = -8; buf[n++] = 8; buf[n++] = -4;
        buf[n++] = 6; buf[n++] = 0.1f; buf[n++] = 50;
        gX_Tests_MathDump_V4(buf, &n, v1);
        gX_Tests_MathDump_V4(buf, &n, v2);
        gX_Tests_MathDump_V4(buf, &n, v3);
        gX_Tests_MathDump_V4(buf, &n, v4);
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Transpose(A));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Add(A, B));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Subtract(A, B));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_MultiplyIndices(A, B));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_DivideIndices(A, B));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Cofactor(A));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Adjucant(A));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Frustum(-2,3,-1,4,0.5f,100));
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_Ortho(-8,8,-4,6,0.1f,50));
        gX_F32a16 Flat = gX_Matrix_To_F32a16(A);
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = Flat.v[i];
        gX_Tests_MathDump_MatAppend(buf, &n, gX_Matrix_From_F32v4(v1, v2, v3, v4));
        gX_Tests_MathDump_Line("matrix_aux", buf, n);
    }
    //ai: small-matrix determinant/trace corpus.
    {
        const gX_F32m2x2 M2 = {.m0=1,.m1=2,.m2=3,.m3=4};
        const gX_F32m3x3 M3 = {.m0=1,.m1=2,.m2=3,.m3=0,.m4=4,.m5=5,
            .m6=1,.m7=0,.m8=6};
        gX_I32 n = 0;
        buf[n++] = M2.m0; buf[n++] = M2.m1; buf[n++] = M2.m2; buf[n++] = M2.m3;
        buf[n++] = M3.m0; buf[n++] = M3.m1; buf[n++] = M3.m2;
        buf[n++] = M3.m3; buf[n++] = M3.m4; buf[n++] = M3.m5;
        buf[n++] = M3.m6; buf[n++] = M3.m7; buf[n++] = M3.m8;
        buf[n++] = gX_F32m2x2_Determinant(M2);
        buf[n++] = gX_F32m2x2_Trace(M2);
        buf[n++] = gX_F32m3x3_Determinant(M3);
        buf[n++] = gX_F32m3x3_Trace(M3);
        gX_Tests_MathDump_Line("matrix_small", buf, n);
    }
    //ai: F64v4 lerp corpus with full double precision.
    {
        const gX_F64 A[4] = {0.1,1.0/3.0,-2.5,1.0e-9};
        const gX_F64 B[4] = {0.4,2.0/3.0,5.5,3.0e-9};
        gX_F64 V[13];
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 4; i++) V[n++] = A[i];
        for (gX_I32 i = 0; i < 4; i++) V[n++] = B[i];
        V[n++] = 0.25;
        gX_F64v4 R = gX_F64v4_Lerp((gX_F64v4){A[0],A[1],A[2],A[3]},
            (gX_F64v4){B[0],B[1],B[2],B[3]},0.25);
        V[n++] = R.x; V[n++] = R.y; V[n++] = R.z; V[n++] = R.w;
        gX_Tests_MathDump_Line64("f64", V, n);
    }
    //ai: edge-case corpus — singular, zero-length and degenerate inputs with classified outputs.
    {
        // edge_cases: singular(16) -> classified scalars in the documented order.
        const gX_F32 singular[16] = { 1,2,3,4, 2,4,6,8, 0,1,0,1, 1,0,1,0 };
        gX_Matrix Singular = gX_Tests_MathDump_Mat(singular);
        gX_Matrix Inverse = gX_Matrix_Invert(Singular);
        gX_F32 inverseValues[16];
        gX_Tests_MathDump_Mat16(inverseValues,Inverse);
        gX_I32 finiteCount = 0, nanCount = 0, infCount = 0;
        for (gX_I32 i = 0; i < 16; i++) {
            if (isnan(inverseValues[i])) nanCount++;
            else if (isinf(inverseValues[i])) infCount++;
            else finiteCount++;
        }
        gX_F32v3 Projected = gX_F32v3_Project((gX_F32v3){1,2,3},(gX_F32v3){0,0,0});
        gX_F32v3 Repeated = gX_F32v3_SDF_RepeatInfinite((gX_F32v3){1,2,3},(gX_F32v3){0,0,0});
        gX_F32v3 FiniteRepeated = gX_F32v3_SDF_RepeatFinite((gX_F32v3){1,2,3},
            (gX_F32v3){1,1,1},(gX_F32v3){0,0,0});
        gX_F32v3 IdentityAxis;
        gX_F32 IdentityAngle;
        gX_F32Qua_ToAxisAngle(gX_F32Qua_Identity(),&IdentityAxis,&IdentityAngle);
        gX_I32 n = 0;
        for (gX_I32 i = 0; i < 16; i++) buf[n++] = singular[i];
        gX_Tests_EdgeAppendV3(buf,&n,gX_F32v3_Normalize((gX_F32v3){0,0,0}));
        gX_Tests_EdgeAppendV3(buf,&n,Projected);
        gX_Tests_EdgeAppend(buf,&n,gX_F32v3_Angle((gX_F32v3){0,0,0},(gX_F32v3){0,0,0}));
        gX_Tests_EdgeAppend(buf,&n,gX_F32v4_Angle((gX_F32v4){0,0,0,0},(gX_F32v4){0,0,0,0}));
        gX_Tests_EdgeAppendV4(buf,&n,gX_F32Qua_Normalize((gX_F32Qua){0,0,0,0}));
        gX_Tests_EdgeAppendV4(buf,&n,gX_F32Qua_FromAxisAngle((gX_F32v3){0,0,0},1.0f));
        gX_Tests_EdgeAppendV3(buf,&n,IdentityAxis);
        gX_Tests_EdgeAppend(buf,&n,IdentityAngle);
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Normalize(2.0f,1.0f,1.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Wrap(2.0f,1.0f,1.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_SDF_Plane((gX_F32v3){1,2,3},(gX_F32v3){0,0,0}));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_SDF_Capsule((gX_F32v3){1,2,3},
            (gX_F32v3){0,0,0},(gX_F32v3){0,0,0},0.5f));
        gX_Tests_EdgeAppendV3(buf,&n,Repeated);
        gX_Tests_EdgeAppendV3(buf,&n,FiniteRepeated);
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Physics_CoulombForce(1.0f,1.0f,0.0f));
        gX_Tests_EdgeAppendV3(buf,&n,gX_F32v3_Physics_ElectricField(1.0f,(gX_F32v3){0,0,0}));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Physics_CyclotronRadius(1.0f,1.0f,1.0f,0.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Physics_PhotonEnergy(0.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Physics_LorentzLength(100.0f,
            (gX_F32)gX_PHYSICS_C*2.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Convert_Amplitude2Decibel(0.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32_Convert_Frequency2MidiNote(0.0f));
        gX_Tests_EdgeAppend(buf,&n,gX_F32m4x4_Determinant(Singular));
        gX_Tests_EdgeAppend(buf,&n,(gX_F32)finiteCount);
        gX_Tests_EdgeAppend(buf,&n,(gX_F32)nanCount);
        gX_Tests_EdgeAppend(buf,&n,(gX_F32)infCount);
        gX_Tests_MathDump_Line("edge_cases", buf, n);
    }
    {
        // sdf: p(3) sphereR(1) planeN(3) cubeB(3) torusT(2) cylH(2) coneC(3)
        //   -> sphere, plane, cube, torus, cylinder, cone (6)
        gX_F32v3 p = { 1.5f, -0.75f, 2.25f };
        gX_F32v3 planeN = { 0, 2, 0 };
        gX_F32v3 cubeB = { 1, 1, 1 };
        gX_F32v2 torusT = { 2, 0.5f };
        gX_F32v2 cylH = { 0.75f, 1.25f };
        gX_F32v3 coneC = { 0.8f, -0.6f, 1.0f };
        //ai: v0.15 primitive twins — capsule/octahedron/rounded box/hex
        //   prism/menger (inputs appended after the classic set).
        gX_F32v3 capsuleA = { 0.2f, -0.6f, 0.4f };
        gX_F32v3 capsuleB = { 0.9f, 0.5f, -0.3f };
        gX_F32 capsuleR = 0.4f;
        gX_F32 octaR = 1.25f;
        gX_F32 roundR = 0.2f;
        gX_F32v2 hexH = { 0.9f, 0.6f };
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, p);
        buf[n++] = 0.75f;
        gX_Tests_MathDump_V3(buf, &n, planeN);
        gX_Tests_MathDump_V3(buf, &n, cubeB);
        gX_Tests_MathDump_V2(buf, &n, torusT);
        gX_Tests_MathDump_V2(buf, &n, cylH);
        gX_Tests_MathDump_V3(buf, &n, coneC);
        gX_Tests_MathDump_V3(buf, &n, capsuleA);
        gX_Tests_MathDump_V3(buf, &n, capsuleB);
        buf[n++] = capsuleR;
        buf[n++] = octaR;
        buf[n++] = roundR;
        gX_Tests_MathDump_V2(buf, &n, hexH);
        buf[n++] = gX_F32_SDF_Sphere(p, 0.75f);
        buf[n++] = gX_F32_SDF_Plane(p, planeN);
        buf[n++] = gX_F32_SDF_Cube(p, cubeB);
        buf[n++] = gX_F32_SDF_Torus(p, torusT);
        buf[n++] = gX_F32_SDF_Cylinder(p, cylH);
        buf[n++] = gX_F32_SDF_Cone(p, coneC);
        buf[n++] = gX_F32_SDF_Capsule(p, capsuleA, capsuleB, capsuleR);
        buf[n++] = gX_F32_SDF_Octahedron(p, octaR);
        buf[n++] = gX_F32_SDF_RoundBox(p, cubeB, roundR);
        buf[n++] = gX_F32_SDF_HexPrism(p, hexH);
        buf[n++] = gX_F32_SDF_Menger(p, cubeB);
        gX_Tests_MathDump_Line("sdf", buf, n);
    }
    //ai: extended SDF corpus — box frame, Mandelbulb and repeat helpers.
    {
        // sdf_ext: p(3) b(3) e m(3) param(4) c(3) a(3) d(3)
        //   -> boxFrame(1), mandelbulb(1), repeatInfinite(3), repeatFinite(3)
        const gX_F32v3 p = { 3.5f, -2.25f, 7.75f };
        const gX_F32v3 b = { 1.0f, 1.0f, 1.0f };
        const gX_F32 e = 0.25f;
        const gX_F32v3 m = { 0.3f, 0.4f, 0.5f };
        const gX_F32v4 param = { 1.0f, 8.0f, 6.0f, 2.0f };
        const gX_F32v3 c = { 2.0f, 3.0f, 4.0f };
        const gX_F32v3 a = { 1.0f, 2.0f, 1.0f };
        const gX_F32v3 d = { 1.5f, 0.75f, 2.5f };
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, p);
        gX_Tests_MathDump_V3(buf, &n, b);
        buf[n++] = e;
        gX_Tests_MathDump_V3(buf, &n, m);
        gX_Tests_MathDump_V4(buf, &n, param);
        gX_Tests_MathDump_V3(buf, &n, c);
        gX_Tests_MathDump_V3(buf, &n, a);
        gX_Tests_MathDump_V3(buf, &n, d);
        buf[n++] = gX_F32_SDF_BoxFrame(p, b, e);
        buf[n++] = gX_F32_SDF_Mandelbulb(m, param);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_SDF_RepeatInfinite(p, c));
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_SDF_RepeatFinite(p, a, d));
        gX_Tests_MathDump_Line("sdf_ext", buf, n);
    }
    {
        // easing: start end t s p startc endc (7)
        //   -> step, linear, quad, pow, bezier (5)
        const gX_F32 st = 2.0f, en = 10.0f, tt = 0.35f, ss = 0.5f, pp = 2.5f;
        const gX_F32 c0 = 3.0f, c1 = 12.0f;
        gX_I32 n = 0;
        buf[n++] = st; buf[n++] = en; buf[n++] = tt;
        buf[n++] = ss; buf[n++] = pp; buf[n++] = c0; buf[n++] = c1;
        buf[n++] = gX_F32_EasingStep(st, en, tt, ss);
        buf[n++] = gX_F32_EasingLinear(st, en, tt);
        buf[n++] = gX_F32_EasingQuad(st, en, tt);
        buf[n++] = gX_F32_EasingPow(st, en, tt, pp);
        buf[n++] = gX_F32_EasingBezier(st, c0, c1, en, tt);
        gX_Tests_MathDump_Line("easing", buf, n);
    }
    {
        // scalar: value min max inStart inEnd outStart outEnd lerpA lerpB lerpT
        //   (10) -> clamp, lerp, normalize, remap, wrap (5)
        const gX_F32 v = 7.0f, mn = 0.0f, mx = 5.0f;
        const gX_F32 is = 5.0f, ie = 9.0f, os = 10.0f, oe = 20.0f;
        const gX_F32 la = 2.0f, lb = 10.0f, lt = 0.25f;
        gX_I32 n = 0;
        buf[n++] = v; buf[n++] = mn; buf[n++] = mx;
        buf[n++] = is; buf[n++] = ie; buf[n++] = os; buf[n++] = oe;
        buf[n++] = la; buf[n++] = lb; buf[n++] = lt;
        buf[n++] = gX_F32_Clamp(v, mn, mx);
        buf[n++] = gX_F32_Lerp(la, lb, lt);
        buf[n++] = gX_F32_Normalize(v, is, ie);
        buf[n++] = gX_F32_Remap(v, is, ie, os, oe);
        buf[n++] = gX_F32_Wrap(v, mn, mx);
        gX_Tests_MathDump_Line("scalar", buf, n);
    }
    {
        // int_math: n a b c d e f g h (9)
        //   -> factorial(n), gcd(a,b), lcm(c,d), nCr(e,f), nPr(g,h) (5)
        gX_I32 n = 0;
        buf[n++] = 12; buf[n++] = 48; buf[n++] = 36; buf[n++] = 4; buf[n++] = 6;
        buf[n++] = 10; buf[n++] = 3; buf[n++] = 10; buf[n++] = 3;
        buf[n++] = (gX_F32)gX_I32_Factorial(12);
        buf[n++] = (gX_F32)gX_I32_GCD(48, 36);
        buf[n++] = (gX_F32)gX_I32_LCM(4, 6);
        buf[n++] = (gX_F32)gX_I32_nCr(10, 3);
        buf[n++] = (gX_F32)gX_I32_nPr(10, 3);
        gX_Tests_MathDump_Line("int_math", buf, n);
    }
    {
        // color: c1(4) c2(4) t(1) -> lerp(4), lerpRGB(4), mul(4), ToV4(c1)(4)
        gX_Color c1 = { 40, 80, 120, 200 };
        gX_Color c2 = { 200, 160, 100, 255 };
        gX_I32 n = 0;
        buf[n++] = c1.r; buf[n++] = c1.g; buf[n++] = c1.b; buf[n++] = c1.a;
        buf[n++] = c2.r; buf[n++] = c2.g; buf[n++] = c2.b; buf[n++] = c2.a;
        buf[n++] = 0.25f;
        gX_Color lerp = gX_Color_Lerp(c1, c2, 0.25f);
        gX_Color lerpRGB = gX_Color_LerpRGB(c1, c2, 0.25f);
        gX_Color mul = gX_Color_Mul(c1, c2);
        gX_F32v4 v4 = gX_Color_ToV4(c1);
        buf[n++] = lerp.r; buf[n++] = lerp.g; buf[n++] = lerp.b; buf[n++] = lerp.a;
        buf[n++] = lerpRGB.r; buf[n++] = lerpRGB.g; buf[n++] = lerpRGB.b; buf[n++] = lerpRGB.a;
        buf[n++] = mul.r; buf[n++] = mul.g; buf[n++] = mul.b; buf[n++] = mul.a;
        buf[n++] = v4.x; buf[n++] = v4.y; buf[n++] = v4.z; buf[n++] = v4.w;
        gX_Tests_MathDump_Line("color", buf, n);
    }
    {
        // physics: force(4) mass g h vel(4) kmass length speed (14)
        //   -> acc(4), potential, kinetic, Lorentz (7)
        gX_F32v4 force = { 12, -6, 3, 0 };
        gX_F32v4 vel = { 3, 4, 0, 0 };
        gX_I32 n = 0;
        buf[n++] = force.x; buf[n++] = force.y; buf[n++] = force.z; buf[n++] = force.w;
        buf[n++] = 4.0f;                 // mass
        buf[n++] = 9.81f; buf[n++] = 2.5f; // g, height
        buf[n++] = vel.x; buf[n++] = vel.y; buf[n++] = vel.z; buf[n++] = vel.w;
        buf[n++] = 2.0f;                 // kinetic mass
        buf[n++] = 100.0f; buf[n++] = 1.5e8f; // length, speed
        gX_F32v4 acc = gX_F32v4_Physics_Newton2GetAcceleration(force, 4.0f);
        buf[n++] = acc.x; buf[n++] = acc.y; buf[n++] = acc.z; buf[n++] = acc.w;
        buf[n++] = gX_F32_Physics_PotentialEnergy(4.0f, 9.81f, 2.5f);
        buf[n++] = gX_F32_Physics_KineticEnergy(vel, 2.0f);
        buf[n++] = gX_F32_Physics_LorentzLength(100.0f, 1.5e8f);
        gX_Tests_MathDump_Line("physics", buf, n);
    }
    {
        // relativity: velocity lightSpeed addU addV time length
        //   dopplerBeta rapidityBeta (8)
        //   -> beta gamma contracted dilated doppler velocityAdd rapidity (7)
        gX_I32 n = 0;
        buf[n++] = 10.8f; buf[n++] = 12.0f;   // 0.9c at 12 m/s light speed
        buf[n++] = 7.2f; buf[n++] = 8.4f;     // 0.6c + 0.7c
        buf[n++] = 2.0f; buf[n++] = 5.0f;     // time0, length0
        buf[n++] = 0.9f; buf[n++] = 0.6f;     // doppler beta, rapidity beta
        buf[n++] = gX_F32_Physics_RelativityBeta(10.8f, 12.0f);
        buf[n++] = gX_F32_Physics_RelativityGammaForVelocity(10.8f, 12.0f);
        buf[n++] = gX_F32_Physics_RelativityContractedLength(5.0f, 0.9f);
        buf[n++] = gX_F32_Physics_RelativityDilatedTime(2.0f, 0.9f);
        buf[n++] = gX_F32_Physics_RelativityDopplerFactor(0.9f);
        buf[n++] = gX_F32_Physics_RelativityVelocityAdd(7.2f, 8.4f, 12.0f);
        buf[n++] = gX_F32_Physics_RelativityRapidity(0.6f);
        gX_Tests_MathDump_Line("relativity", buf, n);
    }
    {
        // planck: temperature wavelength (2) -> RayleighJeans, Planck (2),
        //   then the RGB (650/520/460 nm) triple for the same temperature (3)
        gX_I32 n = 0;
        buf[n++] = 5800.0f; buf[n++] = 550.0e-9f;
        buf[n++] = gX_F32_Physics_Temp_RayleighJeans(5800.0f, 550.0e-9f);
        buf[n++] = gX_F32_Physics_Temp_Planck(5800.0f, 550.0e-9f);
        gX_F32v3 rgb = gX_F32v3_Physics_Temp_Planck(5800.0f);
        buf[n++] = rgb.x; buf[n++] = rgb.y; buf[n++] = rgb.z;
        gX_Tests_MathDump_Line("planck", buf, n);
    }
    {
        // em: q1 q2 r (3), field charge + offset (4), Lorentz charge+vel+B (7),
        //   cyclotron mass speed charge B (4), wavelength (1) = 19 inputs
        //   -> coulomb force, coulomb potential, E field (3), Lorentz (3),
        //      cyclotron radius, photon energy = 10 outputs
        gX_I32 n = 0;
        buf[n++] = 1.0e-6f; buf[n++] = -2.0e-6f; buf[n++] = 0.5f;
        buf[n++] = 3.0e-6f; buf[n++] = 0.2f; buf[n++] = -0.1f; buf[n++] = 0.4f;
        buf[n++] = 1.5e-6f;
        buf[n++] = 2.0f; buf[n++] = 0.0f; buf[n++] = 1.0f;
        buf[n++] = 0.0f; buf[n++] = 2.0f; buf[n++] = 0.0f;
        buf[n++] = 2.0f; buf[n++] = 3.0f; buf[n++] = 1.0e-6f; buf[n++] = 0.5f;
        buf[n++] = 550.0e-9f;
        buf[n++] = gX_F32_Physics_CoulombForce(1.0e-6f, -2.0e-6f, 0.5f);
        buf[n++] = gX_F32_Physics_CoulombPotential(1.0e-6f, -2.0e-6f, 0.5f);
        gX_F32v3 eField = gX_F32v3_Physics_ElectricField(3.0e-6f,
            (gX_F32v3){0.2f, -0.1f, 0.4f});
        buf[n++] = eField.x; buf[n++] = eField.y; buf[n++] = eField.z;
        gX_F32v3 lorentz = gX_F32v3_Physics_LorentzForce(1.5e-6f,
            (gX_F32v3){2.0f, 0.0f, 1.0f}, (gX_F32v3){0.0f, 2.0f, 0.0f});
        buf[n++] = lorentz.x; buf[n++] = lorentz.y; buf[n++] = lorentz.z;
        buf[n++] = gX_F32_Physics_CyclotronRadius(2.0f, 3.0f, 1.0e-6f, 0.5f);
        buf[n++] = gX_F32_Physics_PhotonEnergy(550.0e-9f);
        gX_Tests_MathDump_Line("em", buf, n);
    }
    {
        // dipole: m1 (3), m2 (3), offset (3), coaxial force m1 m2 r (3),
        //   on-axis field m r (2), equatorial field m r (2) = 16 inputs
        //   -> field B1 (3), torque on m2 (3), force on m2 (3), on-axis
        //      force, pair potential, on-axis field, equatorial field = 13
        //      outputs
        gX_I32 n = 0;
        buf[n++] = 0.0f; buf[n++] = 0.0f; buf[n++] = 2.0f;
        buf[n++] = 1.5f; buf[n++] = 0.0f; buf[n++] = 0.5f;
        buf[n++] = 0.3f; buf[n++] = -0.2f; buf[n++] = 0.4f;
        buf[n++] = 2.0f; buf[n++] = 1.5f; buf[n++] = 0.25f;
        buf[n++] = 2.0f; buf[n++] = 0.5f;
        buf[n++] = 2.0f; buf[n++] = 0.5f;
        gX_F32v3 dipoleM1 = {0.0f, 0.0f, 2.0f};
        gX_F32v3 dipoleM2 = {1.5f, 0.0f, 0.5f};
        gX_F32v3 dipoleOffset = {0.3f, -0.2f, 0.4f};
        gX_F32v3 dipoleField = gX_F32v3_Physics_DipoleField(dipoleM1,
            dipoleOffset);
        buf[n++] = dipoleField.x; buf[n++] = dipoleField.y; buf[n++] = dipoleField.z;
        gX_F32v3 dipoleTorque = gX_F32v3_Physics_DipoleTorque(dipoleM2,
            dipoleField);
        buf[n++] = dipoleTorque.x; buf[n++] = dipoleTorque.y; buf[n++] = dipoleTorque.z;
        gX_F32v3 dipoleForce = gX_F32v3_Physics_DipoleForce(dipoleM1,
            dipoleM2, dipoleOffset);
        buf[n++] = dipoleForce.x; buf[n++] = dipoleForce.y; buf[n++] = dipoleForce.z;
        buf[n++] = gX_F32_Physics_DipoleForceOnAxis(2.0f, 1.5f, 0.25f);
        buf[n++] = gX_F32_Physics_DipolePotential(dipoleM1, dipoleM2,
            dipoleOffset);
        buf[n++] = gX_F32_Physics_DipoleFieldOnAxis(2.0f, 0.5f);
        buf[n++] = gX_F32_Physics_DipoleFieldEquatorial(2.0f, 0.5f);
        gX_Tests_MathDump_Line("dipole", buf, n);
    }
    {
        // thermal: temperature, area, emissivity (3) -> blackbody RGB (3),
        //   Stefan-Boltzmann power, Wien peak (2) = 5 outputs
        gX_I32 n = 0;
        buf[n++] = 3000.0f; buf[n++] = 2.0f; buf[n++] = 0.5f;
        gX_F32v3 blackbody = gX_F32v3_Physics_Temp_BlackbodyRGB(3000.0f);
        buf[n++] = blackbody.x; buf[n++] = blackbody.y; buf[n++] = blackbody.z;
        buf[n++] = gX_F32_Physics_Temp_StefanBoltzmann(3000.0f, 2.0f, 0.5f);
        buf[n++] = gX_F32_Physics_Temp_WienPeak(3000.0f);
        gX_Tests_MathDump_Line("thermal", buf, n);
    }
    {
        // fft_delta / fft_sine: input (2N), output (2N), N = 8
        gX_Fourier_Workspace *ws = gX_Fourier_Workspace_Create(3);
        if (ws) {
            gX_F32_complex data[8];
            for (gX_I32 i = 0; i < 8; i++) data[i] = (i == 0) ? 1.0f : 0.0f;
            gX_I32 n = 0;
            for (gX_I32 i = 0; i < 8; i++) { buf[n++] = crealf(data[i]); buf[n++] = cimagf(data[i]); }
            gX_Fourier_FFT(ws, data);
            for (gX_I32 i = 0; i < 8; i++) { buf[n++] = crealf(data[i]); buf[n++] = cimagf(data[i]); }
            gX_Tests_MathDump_Line("fft_delta", buf, n);
            for (gX_I32 i = 0; i < 8; i++)
                data[i] = CMPLXF(sinf(2.0f * gX_PI * (gX_F32)i / 8.0f), 0.0f);
            n = 0;
            for (gX_I32 i = 0; i < 8; i++) { buf[n++] = crealf(data[i]); buf[n++] = cimagf(data[i]); }
            gX_Fourier_FFT(ws, data);
            for (gX_I32 i = 0; i < 8; i++) { buf[n++] = crealf(data[i]); buf[n++] = cimagf(data[i]); }
            gX_Tests_MathDump_Line("fft_sine", buf, n);
            gX_Fourier_Workspace_Destroy(ws);
        }
    }
    {
        // calculus: x d steps a b px py pz (8)
        //   -> differentiate(f1), nabla(f3)(3), diverge(v3), curl(v3)(3),
        //      simpson(f1), newton(f2) (10)
        const gX_F32 x = 3.0f, d = 1.0e-3f, a = 0.0f, b = 1.0f;
        gX_F32v3 p = { 1.5f, 2.0f, 3.0f };
        gX_I32 n = 0;
        buf[n++] = x; buf[n++] = d; buf[n++] = 100.0f; buf[n++] = a; buf[n++] = b;
        gX_Tests_MathDump_V3(buf, &n, p);
        buf[n++] = gX_F32_Calculus_Differentiate(mathdump_sq, x, d);
        gX_F32v3 grad = gX_F32v3_Calculus_Nabla(mathdump_f3, p, d);
        gX_Tests_MathDump_V3(buf, &n, grad);
        buf[n++] = gX_F32_Calculus_Diverge(mathdump_v3, p, d);
        gX_F32v3 curl = gX_F32v3_Calculus_Curl(mathdump_v3, p, d);
        gX_Tests_MathDump_V3(buf, &n, curl);
        buf[n++] = gX_F32_Calculus_Simpson(mathdump_sq, 100, a, b);
        buf[n++] = gX_F32_Newton(mathdump_sq2, 1.0f, d, 20);
        gX_Tests_MathDump_Line("calculus", buf, n);
    }
    {
        // convert_units: meters feet inches kelvin celsius prefix (6)
        //   -> f2m m2f i2m m2i k2c c2k prefix(meters) (7)
        const gX_F32 meters = 1234.5f, feet = 100.0f, inches = 39.37f;
        const gX_F32 kelvin = 300.0f, celsius = 26.85f;
        gX_I32 n = 0;
        buf[n++] = meters; buf[n++] = feet; buf[n++] = inches;
        buf[n++] = kelvin; buf[n++] = celsius; buf[n++] = -3;
        buf[n++] = gX_F32_Convert_Foot2Meter(feet);
        buf[n++] = gX_F32_Convert_Meter2Foot(meters);
        buf[n++] = gX_F32_Convert_Inch2Meter(inches);
        buf[n++] = gX_F32_Convert_Meter2Inch(meters);
        buf[n++] = gX_F32_Convert_Kelvin2Celsius(kelvin);
        buf[n++] = gX_F32_Convert_Celsius2Kelvin(celsius);
        buf[n++] = gX_F32_Convert_MeterPrefix(meters, -3);
        gX_Tests_MathDump_Line("convert_units", buf, n);
    }
    {
        // convert_color: rgb(3) hsv(3) -> RGB2HSV(rgb)(3) HSV2RGB(hsv)(3)
        gX_F32v3 rgb = { 0.2f, 0.6f, 0.9f };
        gX_F32v3 hsv = { 210.0f, 0.5f, 0.8f };
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, rgb);
        gX_Tests_MathDump_V3(buf, &n, hsv);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_RGB2HSV(rgb));
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_HSV2RGB(hsv));
        gX_Tests_MathDump_Line("convert_color", buf, n);
    }
    {
        // convert_coords: p(3) -> c2cyl(p), cyl2c(c2cyl(p)), c2sph(p),
        //   sph2c(c2sph(p)) (12) — the back halves must equal p
        gX_F32v3 p = { 1.5f, -2.0f, 3.0f };
        gX_F32v3 cyl = gX_F32_Convert_cartesian2cylinder(p);
        gX_F32v3 sph = gX_F32_Convert_cartesian2spherical(p);
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, p);
        gX_Tests_MathDump_V3(buf, &n, cyl);
        gX_Tests_MathDump_V3(buf, &n, gX_F32_Convert_cylinder2cartesian(cyl));
        gX_Tests_MathDump_V3(buf, &n, sph);
        gX_Tests_MathDump_V3(buf, &n, gX_F32_Convert_spherical2cartesian(sph));
        gX_Tests_MathDump_V3(buf, &n, gX_F32_Convert_cylinder2spherical(cyl));
        gX_Tests_MathDump_V3(buf, &n, gX_F32_Convert_spherical2cylinder(sph));
        gX_Tests_MathDump_Line("convert_coords", buf, n);
    }
    {
        // convert_sky: eq(3) latitude longitude (5) -> eq2ecl(eq), ecl2eq(eq),
        //   ecl2eq(eq2ecl(eq)), eq2ecl(ecl2eq(eq)), eq2hor(eq, lat, lon),
        //   hor2eq(eq, lat, lon), hor2eq(eq2hor(eq)), eq2hor(hor2eq(eq)) (24)
        //   — the 3rd/4th and 7th/8th triples are round trips
        gX_F32v3 eq = { 1.2f, 0.4f, 0.8f };
        const gX_F32 lat = 0.7853981634f;  // 45 deg
        const gX_F32 lon = 0.3f;
        gX_F32v3 ecl = gX_F32v3_Convert_Equatorial2Ecliptic(eq);
        gX_F32v3 eqBack = gX_F32v3_Convert_Ecliptic2Equatorial(ecl);
        gX_F32v3 hor = gX_F32v3_Convert_Equatorial2Horizontal(eq, lat, lon);
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, eq);
        buf[n++] = lat; buf[n++] = lon;
        gX_Tests_MathDump_V3(buf, &n, ecl);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Ecliptic2Equatorial(eq));
        gX_Tests_MathDump_V3(buf, &n, eqBack);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Equatorial2Ecliptic(eqBack));
        gX_Tests_MathDump_V3(buf, &n, hor);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Horizontal2Equatorial(eq, lat, lon));
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Horizontal2Equatorial(hor, lat, lon));
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Equatorial2Horizontal(
            gX_F32v3_Convert_Horizontal2Equatorial(hor, lat, lon), lat, lon));
        gX_Tests_MathDump_Line("convert_sky", buf, n);
    }
    {
        // convert_new: linear srgb amp db note freq celsius fahrenheit (8)
        //   -> l2s s2l a2d d2a n2f f2n c2f f2c (8)
        gX_I32 n = 0;
        buf[n++] = 0.25f; buf[n++] = 0.6f;
        buf[n++] = 0.5f; buf[n++] = -6.0f;
        buf[n++] = 60.0f; buf[n++] = 220.0f;
        buf[n++] = 100.0f; buf[n++] = 212.0f;
        buf[n++] = gX_F32_Convert_Linear2SRGB(0.25f);
        buf[n++] = gX_F32_Convert_SRGB2Linear(0.6f);
        buf[n++] = gX_F32_Convert_Amplitude2Decibel(0.5f);
        buf[n++] = gX_F32_Convert_Decibel2Amplitude(-6.0f);
        buf[n++] = gX_F32_Convert_MidiNote2Frequency(60.0f);
        buf[n++] = gX_F32_Convert_Frequency2MidiNote(220.0f);
        buf[n++] = gX_F32_Convert_Celsius2Fahrenheit(100.0f);
        buf[n++] = gX_F32_Convert_Fahrenheit2Celsius(212.0f);
        gX_Tests_MathDump_Line("convert_new", buf, n);
    }
    {
        // convert_proj: p(3) -> v2h(p)(2), h2v(v2h(p))(3), v2e(p)(2),
        //   e2v(v2e(p))(3) (10) — the 3-triples are round trips
        gX_F32v3 p = { 0.6f, -0.6f, 0.5291503f };
        gX_F32v2 h = gX_F32v2_Convert_Vector2Horizontal(p);
        gX_F32v2 e = gX_F32v2_Convert_Vector2Equatorial(p);
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, p);
        gX_Tests_MathDump_V2(buf, &n, h);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Horizontal2Vector(h));
        gX_Tests_MathDump_V2(buf, &n, e);
        gX_Tests_MathDump_V3(buf, &n, gX_F32v3_Convert_Equatorial2Vector(e));
        gX_Tests_MathDump_Line("convert_proj", buf, n);
    }
    {
        // convert_ball: p(3) -> s2b(p)(2), b2s(s2b(p))(3) (5)
        gX_F32v3 p = { 0.5f, 0.5f, 0.70710678f };
        gX_F32v2 b = gX_F32_Convert_space2ball(p);
        gX_I32 n = 0;
        gX_Tests_MathDump_V3(buf, &n, p);
        gX_Tests_MathDump_V2(buf, &n, b);
        gX_Tests_MathDump_V3(buf, &n, gX_F32_Convert_ball2space(b));
        gX_Tests_MathDump_Line("convert_ball", buf, n);
    }
    fflush(stdout);
    return 0;
}
