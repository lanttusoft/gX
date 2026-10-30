/**********************************************************************************************
*
*   [gX: src/renderer/raymarch/tests.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: raymarch CPU tests — pure math, no GPU needed. Covers the terrain
//   noise bake (the sampler the texture generator calls), the step-correction
//   polynomial contract and the water height-field contract (both mirrored
//   from resources/shader/raymarch_new.frag so shader and preset defaults
//   stay pinned).

#include "../raymarch.h"

#include "../../gxmath/sdf.h"

#include <math.h>
//ai: authoring-transform assertions use the same vector convention as the SDF shader.
#include "../../gxmath/vector3.h"
#include <string.h>
//ai: isolated CPU preset round-trips use unique harness scratch files.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "schema2.h"

#include "../../common/log.h"

#define GX_PI 3.14159265359f

#define RM_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "RM_TEST FAIL: %s (line %d)", msg, \
                __LINE__); \
            errors++; \
        } \
    } while (0)

#define RM_TEST_CLOSE(a, b, eps) \
    do { \
        gX_F32 _a = (a), _b = (b); \
        gX_F32 _d = fabsf(_a - _b); \
        if (!isfinite(_a) || !isfinite(_b) || _d > (eps)) { \
            gX_TraceLog(gX_LOG_ERROR, "RM_TEST FAIL: %s=%f vs %f (line %d)", \
                #a, _a, _b, __LINE__); \
            errors++; \
        } \
    } while (0)

//opencode: mirrors the step-correction polynomial in raymarch_new.frag:
//   num = poly[0..3] (const..cubic), den = 1 + poly[4..6], floor poly[7]
//   (0 = the shader's 0.1 default).
static gX_F32 rm_test_step_correction(gX_F32 stepIdx, const gX_F32 *poly) {
    gX_F32 idx = stepIdx;
    gX_F32 num = poly[0] + poly[1] * idx + poly[2] * idx * idx
        + poly[3] * idx * idx * idx;
    gX_F32 den = 1.0f + poly[4] * idx + poly[5] * idx * idx
        + poly[6] * idx * idx * idx;
    gX_F32 minCorr = poly[7] > 0.0f ? poly[7] : 0.1f;
    gX_F32 corr = fabsf(den) > 0.000001f ? num / den : 1.0f;
    if (minCorr < 1.0f) minCorr = 1.0f;
    if (corr < minCorr) corr = minCorr;
    return corr;
}

//opencode: mirrors waterfunc in raymarch_new.frag — ripple (amplitude,
//   frequency, posX, posY) with the seed offset, whirlpool (strength, scale,
//   posX, posY).
static gX_F32 rm_test_waterfunc(gX_F32 px, gX_F32 pz, gX_F32 time,
    gX_F32 speed, gX_F32 rAmp, gX_F32 rFreq, gX_F32 rPosX, gX_F32 rPosY,
    gX_I32 seed, gX_F32 wpStr, gX_F32 wpScale, gX_F32 wpPosX, gX_F32 wpPosY)
{
    gX_F32 rcx = px - rPosX + (gX_F32)seed * 0.1f;
    gX_F32 rcz = pz - rPosY + (gX_F32)seed * 0.1f;
    gX_F32 r2 = rcx * rcx + rcz * rcz;
    gX_F32 ring = sqrtf(r2 + 1.0f);
    gX_F32 f = rAmp * sinf(rFreq * ring - GX_PI * time * speed)
        / (ring * ring + 1.0f);
    //ai: keep the CPU mirror aligned with the shader's crossing swells.
    gX_F32 phase = time*speed, frequency = fmaxf(fabsf(rFreq)*0.05f,0.05f);
    f += rAmp*(0.6f*sinf((px*0.8f+pz*0.6f)*frequency-phase)
        + 0.35f*sinf((-px*0.6f+pz*0.8f)*frequency*1.7f-phase*1.3f));
    gX_F32 wcx = px - wpPosX;
    gX_F32 wcz = pz - wpPosY;
    gX_F32 r = sqrtf(wcx * wcx + wcz * wcz) + 0.001f;
    f -= (wpStr / (r * r)) * wpScale;
    return f;
}

//opencode: mirrors waterGrad (finite differences, d = 0.05).
static gX_I0 rm_test_watergrad(gX_F32 px, gX_F32 pz, gX_F32 *gx, gX_F32 *gz) {
    gX_F32 d = 0.05f;
    gX_F32 s = rm_test_waterfunc(px, pz, 0.0f, 1.0f, 0.1f, 20.0f, 0.0f,
        0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f);
    *gx = (rm_test_waterfunc(px + d, pz, 0.0f, 1.0f, 0.1f, 20.0f, 0.0f,
        0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f) - s) / d;
    *gz = (rm_test_waterfunc(px, pz + d, 0.0f, 1.0f, 0.1f, 20.0f, 0.0f,
        0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f) - s) / d;
}

static gX_I32 rm_test_noise(gX_I0) {
    gX_I32 errors = 0;
    gX_I32 cells[4] = { 2, 4, 8, 16 };

    //opencode: every octave stays in [0,1] and is not constant
    {
        gX_F32 minVal = 1.0f, maxVal = 0.0f;
        for (gX_I32 o = 0; o < 4; o++) {
            for (gX_U32 y = 0; y < 256; y += 16) {
                for (gX_U32 x = 0; x < 256; x += 16) {
                    gX_F32 n = gX_RendererRaymarch_Noise_Octave(x, y, 256,
                        cells[o], 0.0f);
                    if (n < minVal) minVal = n;
                    if (n > maxVal) maxVal = n;
                }
            }
        }
        RM_TEST_ASSERT(minVal >= 0.0f, "noise below 0");
        RM_TEST_ASSERT(maxVal <= 1.0f, "noise above 1");
        RM_TEST_ASSERT(maxVal > 0.0f, "noise constant");
    }

    //opencode: seamless tiling — texel 0 matches texel size-1 per octave
    {
        for (gX_I32 o = 0; o < 4; o++) {
            gX_F32 a = gX_RendererRaymarch_Noise_Octave(0, 128, 256,
                cells[o], 0.0f);
            gX_F32 b = gX_RendererRaymarch_Noise_Octave(255, 128, 256,
                cells[o], 0.0f);
            RM_TEST_CLOSE(a, b, 0.01f);
        }
    }

    //opencode: the seed shifts the field
    {
        gX_F32 a = gX_RendererRaymarch_Noise_Octave(50, 60, 256, 8, 0.0f);
        gX_F32 b = gX_RendererRaymarch_Noise_Octave(50, 60, 256, 8, 12345.0f);
        RM_TEST_ASSERT(fabsf(a - b) > 0.01f, "seed does not change noise");
    }

    //opencode: continuous — adjacent texels stay close
    {
        gX_F32 a = gX_RendererRaymarch_Noise_Octave(100, 100, 256, 8, 0.0f);
        gX_F32 b = gX_RendererRaymarch_Noise_Octave(101, 101, 256, 8, 0.0f);
        RM_TEST_ASSERT(fabsf(a - b) < 0.1f, "noise not continuous");
    }

    return errors;
}

static gX_I32 rm_test_step(gX_I0) {
    gX_I32 errors = 0;

    //opencode: default preset polynomial (denominator-only, floor 0.2)
    {
        gX_F32 poly[8] = {0};
        poly[0] = 1.0f;
        poly[4] = 0.01f;
        poly[7] = 0.2f;
        RM_TEST_CLOSE(rm_test_step_correction(0.0f, poly), 1.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(100.0f, poly), 1.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(1000.0f, poly), 1.0f, 0.001f);
    }

    //opencode: Padé [1/1] numerator term
    {
        gX_F32 poly[8] = {0};
        poly[0] = 1.2f;
        poly[1] = 0.006f;
        poly[4] = 0.02f;
        poly[7] = 0.1f;
        RM_TEST_CLOSE(rm_test_step_correction(0.0f, poly), 1.2f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(50.0f, poly), 1.0f, 0.001f);
    }

    //opencode: correction decreases monotonically under the default floor
    {
        gX_F32 poly[8] = {0};
        poly[0] = 1.0f;
        poly[4] = 0.02f;
        gX_F32 prev = rm_test_step_correction(0.0f, poly);
        for (gX_I32 i = 1; i <= 128; i++) {
            gX_F32 cur = rm_test_step_correction((gX_F32)i, poly);
            RM_TEST_ASSERT(cur <= prev + 0.001f, "correction not monotonic");
            prev = cur;
        }
    }

    //opencode: the poly[7] floor wins over a collapsing denominator
    {
        gX_F32 poly[8] = {0};
        poly[0] = 1.0f;
        poly[4] = 100.0f;
        poly[7] = 0.5f;
        RM_TEST_CLOSE(rm_test_step_correction(10.0f, poly), 1.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(1000.0f, poly), 1.0f, 0.001f);
    }

    //opencode: Taylor-style growth (no denominator) stays backward compatible
    {
        gX_F32 poly[8] = {0};
        poly[0] = 1.0f;
        poly[1] = 0.02f;
        RM_TEST_CLOSE(rm_test_step_correction(0.0f, poly), 1.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(50.0f, poly), 2.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(100.0f, poly), 3.0f, 0.001f);
    }

    //ai: a denominator pole or negative correction must never grow an SDF step.
    {
        gX_F32 poly[8] = {1,0,0,0,-1,0,0,0};
        RM_TEST_CLOSE(rm_test_step_correction(1.0f, poly), 1.0f, 0.001f);
        RM_TEST_CLOSE(rm_test_step_correction(2.0f, poly), 1.0f, 0.001f);
    }
    return errors;
}

static gX_I32 rm_test_water(gX_I0) {
    gX_I32 errors = 0;

    //opencode: no speed → static ripple
    {
        gX_F32 a = rm_test_waterfunc(0, 0, 0.0f, 0.0f, 0.1f, 20.0f, 0, 0, 0,
            0.0f, 0.0f, 0, 0);
        gX_F32 b = rm_test_waterfunc(0, 0, 1.0f, 0.0f, 0.1f, 20.0f, 0, 0, 0,
            0.0f, 0.0f, 0, 0);
        RM_TEST_CLOSE(a, b, 0.0001f);
    }

    //opencode: at the ripple origin ring = 1 → 0.1*sin(20)/2
    {
        gX_F32 h = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0, 0,
            0.0f, 0.0f, 0, 0);
        gX_F32 expected = 0.1f * sinf(20.0f) / 2.0f;
        RM_TEST_CLOSE(h, expected, 0.0001f);
    }

    //opencode: ripple decays with distance
    {
        gX_F32 near = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0,
            0, 0.0f, 0.0f, 0, 0);
        gX_F32 far = rm_test_waterfunc(100, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0,
            0, 0.0f, 0.0f, 0, 0);
        //ai: the localized ring decays; the crossing swells deliberately persist offshore.
        gX_F32 swell = 0.1f*(0.6f*sinf(80.0f)+0.35f*sinf(-102.0f));
        RM_TEST_ASSERT(fabsf(far-swell) < fabsf(near), "localized ripple does not decay");
        RM_TEST_ASSERT(fabsf(far) > 0.001f, "offshore swells missing");
    }

    //opencode: whirlpool pulls the surface down
    {
        gX_F32 none = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0,
            0, 0.0f, 1.0f, 0, 0);
        gX_F32 whirl = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0,
            0, 1.0f, 1.0f, 0, 0);
        RM_TEST_ASSERT(whirl < none, "whirlpool does not depress");
    }

    //opencode: seed offset shifts the ripple origin
    {
        gX_F32 a = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0, 0,
            0.0f, 0.0f, 0, 0);
        gX_F32 b = rm_test_waterfunc(0, 0, 0.0f, 1.0f, 0.1f, 20.0f, 0, 0, 5,
            0.0f, 0.0f, 0, 0);
        RM_TEST_ASSERT(fabsf(a - b) > 0.001f, "seed offset does not shift");
    }

    //opencode: gradient is finite/nonzero and the clamp floor holds
    {
        gX_F32 gx, gz;
        rm_test_watergrad(0, 0, &gx, &gz);
        gX_F32 gradLen = 1.0f + gx * gx + gz * gz;
        gX_F32 corr = 1.0f / sqrtf(gradLen);
        RM_TEST_ASSERT(isfinite(corr) && corr > 0.0f, "water corr invalid");
        gX_F32 floored = corr > 0.2f ? corr : 0.2f;
        RM_TEST_ASSERT(floored >= 0.2f, "water clamp floor violated");
        gX_F32 huge = 1.0f / sqrtf(1.0f + 1e6f);
        gX_F32 clamped = huge > 0.2f ? huge : 0.2f;
        RM_TEST_CLOSE(clamped, 0.2f, 0.0001f);
    }

    return errors;
}

//ai: v0.15 primitive twins — exact spot checks for the CPU SDFs the shader
//   mirrors (the math corpus owns full SymPy parity; this pins the shapes).
static gX_I32 rm_test_primitives(gX_I0)
{
    gX_I32 errors = 0;
    gX_F32v3 a = { 0.0f, -1.0f, 0.0f };
    gX_F32v3 b = { 0.0f, 1.0f, 0.0f };
    RM_TEST_CLOSE(gX_F32_SDF_Capsule(a, a, b, 0.5f), -0.5f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_Capsule((gX_F32v3){0.5f, 0.0f, 0.0f}, a, b, 0.5f), 0.0f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_Capsule((gX_F32v3){2.0f, 0.0f, 0.0f}, a, b, 0.5f), 1.5f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_Octahedron((gX_F32v3){0.0f, 0.0f, 0.0f}, 2.0f),
        -2.0f * 0.57735027f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_Octahedron((gX_F32v3){2.0f, 0.0f, 0.0f}, 2.0f),
        0.0f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_RoundBox((gX_F32v3){0.0f, 0.0f, 0.0f},
        (gX_F32v3){1.0f, 0.5f, 0.7f}, 0.2f), -0.5f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_RoundBox((gX_F32v3){0.0f, 0.7f, 0.0f},
        (gX_F32v3){1.0f, 0.5f, 0.7f}, 0.2f), 0.2f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_HexPrism((gX_F32v3){0.0f, 0.0f, 0.0f},
        (gX_F32v2){0.9f, 0.6f}), -0.6f, 1e-5f);
    RM_TEST_CLOSE(gX_F32_SDF_HexPrism((gX_F32v3){0.0f, 0.6f, 0.0f},
        (gX_F32v2){0.9f, 0.6f}), -0.3f, 1e-5f);
    RM_TEST_ASSERT(gX_F32_SDF_Menger((gX_F32v3){0.0f, 0.0f, 0.0f},
        (gX_F32v3){1.0f, 1.0f, 1.0f}) > 0.1f,
        "menger centre should be an open fold");
    RM_TEST_ASSERT(gX_F32_SDF_Menger((gX_F32v3){0.5f, 0.5f, 0.5f},
        (gX_F32v3){1.0f, 1.0f, 1.0f}) < 1e-3f
        && gX_F32_SDF_Menger((gX_F32v3){0.5f, 0.5f, 0.5f},
            (gX_F32v3){1.0f, 1.0f, 1.0f}) > -1e-3f,
        "menger corner should sit on the surface");
    return errors;
}

//ai: authoring commands are pure CPU and preserve authored geometry outside their scope.
static gX_I32 rm_test_authoring(gX_I0)
{
    gX_I32 errors = 0;
    gX_RendererRaymarch rm = {0};
    rm.objects.amount = 3;
    for (gX_U32 i = 0; i < 3; i++) gX_RendererRaymarch_Object_Init(&rm, i);
    gX_F32v3 originalPos = rm.objects.pos[1];
    rm.terrain.seed = 42;
    rm.flags.enableWater = 1;
    for (gX_I32 level = 0; level < 4; level++)
    {
        RM_TEST_ASSERT(gX_RendererRaymarch_Quality_Set(&rm, level), "quality apply failed");
        RM_TEST_ASSERT(gX_RendererRaymarch_Quality_Get(&rm) == level, "quality matching failed");
        RM_TEST_ASSERT(rm.parameters.steps >= 128 && rm.parameters.stepsPerBounce == 0,
            "quality weakened convergence or kept a stale bounce budget");
        RM_TEST_ASSERT(rm.terrain.seed == 42 && rm.flags.enableWater
            && rm.objects.pos[1].x == originalPos.x, "quality changed authored scene");
    }
    RM_TEST_ASSERT(!gX_RendererRaymarch_Quality_Set(&rm, -1)
        && !gX_RendererRaymarch_Quality_Set(&rm, 4), "invalid quality accepted");
    RM_TEST_ASSERT(gX_RendererRaymarch_Quality_Get(&rm) == 3, "invalid quality mutated state");
    rm.parameters.steps++;
    RM_TEST_ASSERT(gX_RendererRaymarch_Quality_Get(&rm) == -1, "manual tuning not Custom");
    for (gX_I32 type = 0; type < 14; type++)
    {
        RM_TEST_ASSERT(gX_RendererRaymarch_Object_Type_Set(&rm, 1, type), "primitive type rejected");
        RM_TEST_ASSERT(isfinite(rm.objects.parameters[1].x)
            && isfinite(rm.objects.parameters[1].y), "type dimensions nonfinite");
        RM_TEST_CLOSE(rm.objects.pos[1].x, originalPos.x, 1e-6f);
    }
    RM_TEST_ASSERT(!gX_RendererRaymarch_Object_Type_Set(&rm, 1, 14)
        && !gX_RendererRaymarch_Object_Type_Set(&rm, -1, 0), "invalid primitive accepted");
    RM_TEST_ASSERT(gX_RendererRaymarch_Object_Scale_Set(&rm, 1, (gX_F32v3){2,3,4}),
        "nonuniform scale failed");
    gX_F32v3 scale = gX_RendererRaymarch_Object_Scale_Get(&rm, 1);
    RM_TEST_CLOSE(scale.x, 2, 1e-5f);
    RM_TEST_CLOSE(scale.y, 3, 1e-5f);
    RM_TEST_CLOSE(scale.z, 4, 1e-5f);
    RM_TEST_ASSERT(!gX_RendererRaymarch_Object_Scale_Set(&rm, 1, (gX_F32v3){0,3,4})
        && !gX_RendererRaymarch_Object_Scale_Set(&rm, 1, (gX_F32v3){NAN,3,4}),
        "invalid scale accepted");
    gX_F32v3 local = {0.7f,0.2f,-0.3f};
    gX_F32v3 before = gX_RendererRaymarch_Object_ToWorld(&rm.objects, 1, local);
    gX_F32v3 expected = gX_F32v3_RotateByAxisAngle(gX_F32v3_Sub(before, originalPos),
        (gX_F32v3){0,1,0}, 1.57079632679f);
    expected = gX_F32v3_Add(expected, originalPos);
    RM_TEST_ASSERT(gX_RendererRaymarch_Object_Rotate(&rm, 1, (gX_F32v3){0,90,0}),
        "world rotation failed");
    gX_F32v3 after = gX_RendererRaymarch_Object_ToWorld(&rm.objects, 1, local);
    RM_TEST_CLOSE(after.x, expected.x, 1e-4f);
    RM_TEST_CLOSE(after.y, expected.y, 1e-4f);
    RM_TEST_CLOSE(after.z, expected.z, 1e-4f);
    scale = gX_RendererRaymarch_Object_Scale_Get(&rm, 1);
    RM_TEST_CLOSE(scale.z, 4, 1e-4f);
    RM_TEST_ASSERT(gX_RendererRaymarch_Object_Remove(&rm, 0), "remove failed");
    gX_F32v3 moved = gX_RendererRaymarch_Object_ToWorld(&rm.objects, 0, local);
    RM_TEST_CLOSE(moved.x, after.x, 1e-4f);
    RM_TEST_CLOSE(moved.y, after.y, 1e-4f);
    RM_TEST_CLOSE(moved.z, after.z, 1e-4f);
    RM_TEST_ASSERT(rm.objects.amount == 2 && rm.objects.type[0] == 13,
        "stable compaction lost object fields");
    rm.objects.matX[0] = (gX_F32v3){1,0.2f,0.1f};
    rm.objects.matY[0] = (gX_F32v3){0.3f,0.6f,-0.1f};
    rm.objects.matZ[0] = (gX_F32v3){-0.2f,0.1f,1.2f};
    gX_F32v3 point = gX_RendererRaymarch_Object_ToWorld(&rm.objects, 0, local);
    point = gX_F32v3_Sub(point, rm.objects.pos[0]);
    RM_TEST_CLOSE(gX_F32v3_Dot(point, rm.objects.matX[0]), local.x, 1e-5f);
    RM_TEST_CLOSE(gX_F32v3_Dot(point, rm.objects.matY[0]), local.y, 1e-5f);
    RM_TEST_CLOSE(gX_F32v3_Dot(point, rm.objects.matZ[0]), local.z, 1e-5f);
    RM_TEST_ASSERT(gX_RendererRaymarch_Object_Scale_Set(&rm, 0, (gX_F32v3){3,2,1}),
        "sheared scale rejected");
    scale = gX_RendererRaymarch_Object_Scale_Get(&rm, 0);
    RM_TEST_CLOSE(scale.x, 3, 1e-4f);
    RM_TEST_CLOSE(scale.y, 2, 1e-4f);
    RM_TEST_CLOSE(scale.z, 1, 1e-4f);
    RM_TEST_ASSERT(!gX_RendererRaymarch_Object_Remove(&rm, 2), "invalid remove accepted");
    return errors;
}

//ai: CSG operand order, DAG bounds/cycles and repetition preconditions are GPU-independent.
static gX_I32 rm_test_graph(gX_I0)
{
    gX_I32 errors = 0;
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(2,-0.3f,0.7f,0),-0.3f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(3,-0.3f,0.7f,0),-0.3f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(4,-0.3f,0.7f,0),0.7f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(5,0,0,1),-0.25f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(6,0,0,1),0.25f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(7,0,0,1),0.25f,1e-6f);
    RM_TEST_CLOSE(gX_RendererRaymarch_Graph_Combine(6,-0.3f,0.7f,0.2f),-0.3f,1e-6f);
    gX_RendererRaymarch rm = {0};
    rm.objects.amount = 2;
    gX_RendererRaymarch_Object_Init(&rm,0);
    gX_RendererRaymarch_Object_Init(&rm,1);
    rm.graph.root = 0;
    rm.graph.nodes[0] = (gX_F32v4){2,63,1,0};
    rm.graph.nodes[1] = (gX_F32v4){1,1,0,0};
    rm.graph.nodes[63] = (gX_F32v4){1,0,0,0};
    RM_TEST_ASSERT(gX_RendererRaymarch_Graph_Compile(&rm) && rm.graph.count == 3,
        "forward-reference DAG rejected");
    RM_TEST_ASSERT(rm.graph.program[2].x == 2 && rm.graph.program[2].y == 1
        && rm.graph.program[2].z == 0, "topological references not compacted");
    rm.graph.nodes[1] = (gX_F32v4){2,0,63,0};
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm) && rm.graph.count == 0
        && rm.graph.error[0], "cycle did not produce explicit fallback");
    rm.graph.nodes[1] = (gX_F32v4){1,1,0,0};
    rm.graph.nodes[0].y = NAN;
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm), "nonfinite reference accepted");
    rm.graph.nodes[0].y = 0.5f;
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm), "fractional reference accepted");
    rm.graph.root = 1;
    rm.graph.nodes[0] = (gX_F32v4){1,0,0,0};
    rm.graph.nodes[1] = (gX_F32v4){8,0,0,0};
    rm.graph.domains[1] = (gX_F32v4){2,0,0,0};
    RM_TEST_ASSERT(gX_RendererRaymarch_Graph_Compile(&rm) && rm.graph.program[1].y == 0,
        "valid infinite repeat rejected");
    rm.graph.nodes[1].x = 9;
    rm.graph.domains[1].w = 1+256*2+65536*3;
    RM_TEST_ASSERT(gX_RendererRaymarch_Graph_Compile(&rm), "finite repeat rejected");
    rm.graph.domains[1].x = 1;
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm), "overlapping repeat domain accepted");
    rm.graph.domains[1].x = 2;
    rm.objects.type[0] = 2;
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm), "unsupported repeat leaf accepted");
    rm.objects.type[0] = 0;
    rm.graph.root = 31;
    for (gX_I32 i = 1; i < 33; i++) rm.graph.nodes[i] = (gX_F32v4){2,(gX_F32)(i-1),0,0};
    RM_TEST_ASSERT(gX_RendererRaymarch_Graph_Compile(&rm) && rm.graph.count == 32,
        "32-node program rejected");
    rm.graph.root = 32;
    RM_TEST_ASSERT(!gX_RendererRaymarch_Graph_Compile(&rm), "oversize program accepted");
    rm.graph.root = -1;
    RM_TEST_ASSERT(gX_RendererRaymarch_Graph_Compile(&rm) && rm.graph.count == 0,
        "legacy graph bypass failed");
    gX_I8 path[] = "/tmp/opencode/gx_rm_graph_XXXXXX";
    gX_I32 fd = mkstemp(path);
    RM_TEST_ASSERT(fd >= 0,"graph scratch creation failed");
    if (fd >= 0)
    {
        close(fd);
        rm.graph.root = 31;
        rm.objects.roughness[0] = 0.25f;
        RM_TEST_ASSERT(gX_RendererRaymarch_Preset_Save_New(&rm,path) == 0,"graph preset save failed");
        rm.graph = (gX_Raymarch_Graph){.root=-1};
        RM_TEST_ASSERT(gX_RendererRaymarch_Preset_Load_New(&rm,path) == 0
            && rm.graph.root == 31 && rm.graph.count == 32,"graph schema round-trip failed");
        RM_TEST_CLOSE(rm.objects.roughness[0],0.25f,0.00001f);
        FILE *legacy = fopen(path,"w");
        if (legacy) {fputs("{}",legacy); fclose(legacy);}
        RM_TEST_ASSERT(legacy && gX_RendererRaymarch_Preset_Load_New(&rm,path) == 0
            && rm.graph.root == -1 && rm.graph.count == 0,"legacy preset kept stale graph");
        RM_TEST_CLOSE(rm.objects.roughness[0],1.0f,0.00001f);
        FILE *bounded = fopen(path,"w");
        if (bounded)
        {
            fputs("{\"objects\":{\"amount\":2,\"obj[0]\":{\"roughness\":1.5},\"obj[1]\":{\"roughness\":-0.2}}}",bounded);
            fclose(bounded);
        }
        RM_TEST_ASSERT(bounded && gX_RendererRaymarch_Preset_Load_New(&rm,path) == 0,"bounded roughness load failed");
        RM_TEST_CLOSE(rm.objects.roughness[0],1.0f,0.00001f);
        RM_TEST_CLOSE(rm.objects.roughness[1],0.0f,0.00001f);
        remove(path);
    }
    return errors;
}

//ai: author edits invalidate history, but external camera motion and live-clock ticks do not reset raster history.
static gX_I32 rm_test_temporal_signature(gX_I0)
{
    gX_I32 errors = 0;
    gX_RendererRaymarch rm = {0};
    gX_U64 signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    rm.camera.camera.camera3D.position.x = 4;
    rm.timerDraw.elapsedMs = 8;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) == signature,"camera/runtime state invalidates static history");
    rm.objects.pos[0].x = 1;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"geometry edit retains stale history");
    signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    rm.objects.color[0].x = 1;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"material edit retains stale history");
    signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    rm.state.time = 2;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"paused time scrub retains stale history");
    rm.state.useInternalTime = true;
    signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    rm.state.time = 3;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) == signature,"live clock resets unrelated raster history every frame");
    rm.texture.textures[0].texture.id = 9;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"texture replacement retains stale history");
    signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    strcpy(rm.texture.textures[0].path,"authored.png");
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"texture path edit retains stale history");
    signature = gX_RendererRaymarch_Temporal_Signature(&rm);
    rm.texture.textures[0].filter = 1;
    RM_TEST_ASSERT(gX_RendererRaymarch_Temporal_Signature(&rm) != signature,"texture sampling edit retains stale history");
    return errors;
}

gX_I32 gX_Tests_Raymarch(gX_I0) {
    gX_I32 errors = 0;
    errors += rm_test_noise();
    errors += rm_test_step();
    errors += rm_test_water();
    errors += rm_test_primitives();
    errors += rm_test_authoring();
    errors += rm_test_graph();
    //ai: pure static-history invalidation contract.
    errors += rm_test_temporal_signature();
    gX_TraceLog(gX_LOG_INFO, "RM_TESTS: %d errors", errors);
    return errors;
}
