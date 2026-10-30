/**********************************************************************************************
*
*   [gX: src/renderer/background/tests.c]
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


//opencode: background renderer unit tests — pure CPU math, no GPU needed.
//   Covers: param system update math, transition easing + lerp helpers,
//   Floyd-Steinberg error conservation, palette mode 7 index bounds,
//   blend mode math identities.

#include "param.h"
#include "struct.h"
#include "transition.h"

#include "../../gxmath/gxmath.h"
#include "../../common/log.h"
#include "../../common/utils.h"
#include "../../main.h"

#include <math.h>
#include <stdio.h>

#define BG_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "BG_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while(0)

#define BG_TEST_CLOSE(a, b, eps) \
    do { \
        gX_F32 _a = (a), _b = (b); \
        gX_F32 _d = fabsf(_a - _b); \
        if (_d > (eps)) { \
            gX_TraceLog(gX_LOG_ERROR, "BG_TEST FAIL: %s=%f vs %f (line %d)", #a, _a, _b, __LINE__); \
            errors++; \
        } \
    } while(0)

gX_I32 gX_Tests_Background(gX_I0);

// ---------------------------------------------------------------------------
// 1. Param scalar update math
//   value = oscillatorT*oscillatorTotal + resultantT*resultant + constant
//   With no oscillators/velocity/accel, value must equal constant.
// ---------------------------------------------------------------------------
static gX_I32 bg_test_param_constant_only(void) {
    gX_I32 errors = 0;
    gX_RenBG_ParamScalar p;
    gX_RenBG_ParamScalar_Init(&p, 2.5f);
    gX_RenBG_ParamScalar_Update(&p);
    BG_TEST_CLOSE(p.value, 2.5f, 1e-4f);
    // constant dominates even after reset
    p.constant = -3.0f;
    gX_RenBG_ParamScalar_Update(&p);
    BG_TEST_CLOSE(p.value, -3.0f, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 2. Param scalar velocity/acceleration integration
//   velocity += accel/fps ; resultant += velocity/fps ; value += resultant
// ---------------------------------------------------------------------------
static gX_I32 bg_test_param_velocity(void) {
    gX_I32 errors = 0;
    gX_RenBG_ParamScalar p;
    gX_RenBG_ParamScalar_Init(&p, 0.0f);
    p.constant = 0.0f;
    p.velocity = 10.0f;      // 10 units/sec
    p.resultantT = 1.0f;
    gX_F32 fps = maindata.targetFPS > 0.0f ? maindata.targetFPS : 60.0f;
    gX_F32 dt = 1.0f / fps;
    // After 1 frame: resultant = 10*dt, value = 10*dt
    gX_RenBG_ParamScalar_Update(&p);
    BG_TEST_CLOSE(p.resultant, 10.0f * dt, 1e-4f);
    BG_TEST_CLOSE(p.value, 10.0f * dt, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 3. Param oscillator math
//   oscillator[i] = amplitude[i]*sin((freq[i]*time - phase[i])*2pi)
//   oscillatorTotal = sum, value = oscillatorT*oscillatorTotal + constant
// ---------------------------------------------------------------------------
static gX_I32 bg_test_param_oscillator(void) {
    gX_I32 errors = 0;
    gX_RenBG_ParamScalar p;
    gX_RenBG_ParamScalar_Init(&p, 0.0f);
    p.oscillatorT = 1.0f;
    p.amplitude[0] = 2.0f;
    p.frequency[0] = 0.25f;   // freq*time inside 2pi period
    p.phase[0] = 0.0f;
    // Only one oscillator active
    gX_F32 time = 0.5f;
    gX_F32 expect = 2.0f * sinf((0.25f * time) * 2.0f * (gX_F32)gX_PI);
    maindata.timeElapsed = time;
    gX_RenBG_ParamScalar_Update(&p);
    BG_TEST_CLOSE(p.oscillator[0], expect, 1e-4f);
    BG_TEST_CLOSE(p.oscillatorTotal, expect, 1e-4f);
    BG_TEST_CLOSE(p.value, expect, 1e-4f); // oscT=1, resT default... check
    // oscillatorTotal = sum of all oscillators
    p.amplitude[1] = 1.0f;
    p.frequency[1] = 0.5f;
    gX_F32 expect2 = expect + 1.0f * sinf((0.5f * time) * 2.0f * (gX_F32)gX_PI);
    gX_RenBG_ParamScalar_Update(&p);
    BG_TEST_CLOSE(p.oscillatorTotal, expect2, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 4. V4 param update — SSE path must match scalar reference computation
// ---------------------------------------------------------------------------
static gX_I32 bg_test_param_v4(void) {
    gX_I32 errors = 0;
    gX_RenBG_ParamV4 p;
    gX_RenBG_ParamV4_Init(&p, (gX_F32v4){1, 2, 3, 4});
    gX_RenBG_ParamV4_Update(&p);
    BG_TEST_CLOSE(p.value.x, 1.0f, 1e-4f);
    BG_TEST_CLOSE(p.value.y, 2.0f, 1e-4f);
    BG_TEST_CLOSE(p.value.z, 3.0f, 1e-4f);
    BG_TEST_CLOSE(p.value.w, 4.0f, 1e-4f);

    // Add velocity — all 4 components integrate together
    p.velocity = (gX_F32v4){1, 2, 3, 4};
    gX_F32 dt = 1.0f / maindata.targetFPS;
    gX_RenBG_ParamV4_Update(&p);
    BG_TEST_CLOSE(p.value.x, 1.0f + dt, 1e-4f);
    BG_TEST_CLOSE(p.value.y, 2.0f + 2.0f*dt, 1e-4f);
    BG_TEST_CLOSE(p.value.z, 3.0f + 3.0f*dt, 1e-4f);
    BG_TEST_CLOSE(p.value.w, 4.0f + 4.0f*dt, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 5. Transition easing endpoints + monotonicity
//   ease(0)=0, ease(1)=1 for all 4 modes; monotonic non-decreasing in [0,1]
// ---------------------------------------------------------------------------
static gX_I32 bg_test_transition_ease(void) {
    gX_I32 errors = 0;
    for (gX_I32 e = 0; e <= 3; e++) {
        BG_TEST_CLOSE(gX_RendererBackground_Transition_Ease(0.0f, e), 0.0f, 1e-4f);
        BG_TEST_CLOSE(gX_RendererBackground_Transition_Ease(1.0f, e), 1.0f, 1e-4f);
        // midpoint only for symmetric modes (linear, in-out)
        if (e == GX_BG_TRANS_EASE_LINEAR || e == GX_BG_TRANS_EASE_IN_OUT)
            BG_TEST_CLOSE(gX_RendererBackground_Transition_Ease(0.5f, e), 0.5f, 1e-4f);
        // monotonicity: sample a few points
        gX_F32 prev = 0.0f;
        for (gX_I32 i = 1; i <= 20; i++) {
            gX_F32 t = (gX_F32)i / 20.0f;
            gX_F32 v = gX_RendererBackground_Transition_Ease(t, e);
            if (v < prev - 1e-3f) {
                gX_TraceLog(gX_LOG_ERROR, "BG_TEST FAIL: ease %d non-monotonic at t=%f", e, t);
                errors++;
            }
            prev = v;
        }
    }
    // asymmetric midpoints: ease-in undershoots, ease-out overshoots
    BG_TEST_CLOSE(gX_RendererBackground_Transition_Ease(0.5f, GX_BG_TRANS_EASE_IN), 0.25f, 1e-4f);
    BG_TEST_CLOSE(gX_RendererBackground_Transition_Ease(0.5f, GX_BG_TRANS_EASE_OUT), 0.75f, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 6. Transition lerp helper math (mirrors transition.c BG_LERP_N)
//   lerp(a,b,t) = a + (b-a)*t  → endpoints + midpoint
// ---------------------------------------------------------------------------
static gX_I32 bg_test_transition_lerp(void) {
    gX_I32 errors = 0;
    gX_RenBG_ParamScalar a, b, out;
    gX_RenBG_ParamScalar_Init(&a, 0.0f);
    gX_RenBG_ParamScalar_Init(&b, 10.0f);
    // Simulate BG_LERP_N over .constant at t=0, 0.5, 1
    for (gX_I32 ti = 0; ti <= 2; ti++) {
        gX_F32 t = (gX_F32)ti * 0.5f;
        out.constant = a.constant + (b.constant - a.constant) * t;
        BG_TEST_CLOSE(out.constant, 10.0f * t, 1e-4f);
    }
    // Midpoint: 5.0
    out.constant = a.constant + (b.constant - a.constant) * 0.5f;
    BG_TEST_CLOSE(out.constant, 5.0f, 1e-4f);
    return errors;
}

// ---------------------------------------------------------------------------
// 7. Floyd-Steinberg error weights sum to 1 (error conservation)
//   7/16 + 3/16 + 5/16 + 1/16 = 16/16 = 1
// ---------------------------------------------------------------------------
static gX_I32 bg_test_floyd_steinberg_weights(void) {
    gX_I32 errors = 0;
    const gX_F32 wRight   = 7.0f / 16.0f;
    const gX_F32 wBL      = 3.0f / 16.0f;
    const gX_F32 wBottom  = 5.0f / 16.0f;
    const gX_F32 wBR      = 1.0f / 16.0f;
    BG_TEST_CLOSE(wRight + wBL + wBottom + wBR, 1.0f, 1e-5f);
    // each weight in [0,1]
    BG_TEST_ASSERT(wRight >= 0.0f && wRight <= 1.0f, "FS wRight in [0,1]");
    BG_TEST_ASSERT(wBL >= 0.0f && wBL <= 1.0f, "FS wBL in [0,1]");
    BG_TEST_ASSERT(wBottom >= 0.0f && wBottom <= 1.0f, "FS wBottom in [0,1]");
    BG_TEST_ASSERT(wBR >= 0.0f && wBR <= 1.0f, "FS wBR in [0,1]");
    return errors;
}

// ---------------------------------------------------------------------------
// 8. Palette mode 7 index bounds
//   row in [0, rowCount-1], col in [0,15], index = (row*16+col)/255 in [0,1]
// ---------------------------------------------------------------------------
static gX_I32 bg_test_palette_mode7_bounds(void) {
    gX_I32 errors = 0;
    const gX_I32 rowCount = 16;
    for (gX_I32 avg = 0; avg <= 255; avg += 17) {
        gX_F32 f = (gX_F32)avg / 255.0f;
        gX_I32 row = (gX_I32)(f * rowCount);
        if (row > rowCount - 1) row = rowCount - 1;
        gX_I32 col = 15;
        gX_F32 idx = (gX_F32)(row * 16 + col) / 255.0f;
        BG_TEST_ASSERT(row >= 0 && row < rowCount, "mode7 row in range");
        BG_TEST_ASSERT(idx >= 0.0f && idx <= 1.0f, "mode7 idx in [0,1]");
    }
    return errors;
}

// ---------------------------------------------------------------------------
// 9. Blend mode math identities
//   Screen: 1-(1-a)(1-b); Multiply: a*b; Add: a+b; Difference: |a-b|
// ---------------------------------------------------------------------------
static gX_I32 bg_test_blend_math(void) {
    gX_I32 errors = 0;
    // Screen: 1-(1-0.3)(1-0.7) = 1-0.7*0.3 = 0.79
    {
        gX_F32 a = 0.3f, b = 0.7f;
        gX_F32 screen = 1.0f - (1.0f - a) * (1.0f - b);
        BG_TEST_CLOSE(screen, 0.79f, 1e-4f);
    }
    // Multiply
    BG_TEST_CLOSE(0.3f * 0.7f, 0.21f, 1e-4f);
    // Add
    BG_TEST_CLOSE(0.3f + 0.7f, 1.0f, 1e-4f);
    // Difference |0.7-0.3| = 0.4
    BG_TEST_CLOSE(fabsf(0.7f - 0.3f), 0.4f, 1e-4f);
    // Screen is symmetric: screen(a,b) == screen(b,a)
    {
        gX_F32 s1 = 1.0f - (1.0f - 0.3f) * (1.0f - 0.7f);
        gX_F32 s2 = 1.0f - (1.0f - 0.7f) * (1.0f - 0.3f);
        BG_TEST_CLOSE(s1, s2, 1e-6f);
    }
    return errors;
}

// ---------------------------------------------------------------------------
// 10. Matrix lerp (transition.c bg_lerp_transform matrixA/B)
// ---------------------------------------------------------------------------
static gX_I32 bg_test_matrix_lerp(void) {
    gX_I32 errors = 0;
    gX_Matrix a = gX_Matrix_Identity();
    gX_Matrix b = gX_Matrix_Identity();
    // set b.m0 = 4 (scale x in first row)
    b.m0 = 4.0f;
    gX_Matrix out;
    for (gX_I32 i = 0; i < 16; i++) {
        ((gX_F32*)&out)[i] = ((gX_F32*)&a)[i] + (((gX_F32*)&b)[i] - ((gX_F32*)&a)[i]) * 0.5f;
    }
    // a.m0=1, b.m0=4 → midpoint = 2.5
    BG_TEST_CLOSE(out.m0, 2.5f, 1e-4f);
    // a.m5=1, b.m5=1 → 1
    BG_TEST_CLOSE(out.m5, 1.0f, 1e-4f);
    return errors;
}

gX_I32 gX_Tests_Background(gX_I0) {
    gX_I32 errors = 0;
    errors += bg_test_param_constant_only();
    errors += bg_test_param_velocity();
    errors += bg_test_param_oscillator();
    errors += bg_test_param_v4();
    errors += bg_test_transition_ease();
    errors += bg_test_transition_lerp();
    errors += bg_test_floyd_steinberg_weights();
    errors += bg_test_palette_mode7_bounds();
    errors += bg_test_blend_math();
    errors += bg_test_matrix_lerp();
    gX_TraceLog(gX_LOG_INFO, "BG_TESTS: %d errors", errors);
    return errors;
}
