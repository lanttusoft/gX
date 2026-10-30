/**********************************************************************************************
*
*   [gX: src/renderer/background/param.c]
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


#include <math.h>

#include "param.h"

#include "../../gxmath/gxmath.h"

#include "../../main.h"

//param clear

#define gX_BG_PARAM_CLEAR(dt) \
inline gX_I0 gX_RenBG_Param##dt##_Clear( gX_RenBG_Param##dt *param) { \
    *param = (gX_RenBG_Param##dt){ 0 }; \
} \

gX_BG_PARAM_CLEAR(Scalar)
gX_BG_PARAM_CLEAR(V2)
gX_BG_PARAM_CLEAR(V3)
gX_BG_PARAM_CLEAR(V4)

//param init

static inline gX_I0 gX_RenBG_ParamScalar_SetValues(gX_RenBG_ParamScalar *param) {
    param->constant = param->defaultvalue;
    param->T = 0.0f;
    
    param->resultantT = 1.0f;
    param->resultant = 0.0f;
    param->velocity = 0.0f;
    param->acceleration = 0.0f;
    
    param->oscillatorT = 1.0f;
    param->oscillatorTotal = 0.0f;
    for (gX_U32 i = 0; i < gX_BG_OSCAMOUNT; i++) {
        param->oscillator[i] = 0.0f;
        param->amplitude[i] = 0.0f;
        param->frequency[i] = 1.0f;
        param->phase[i] = 0.0f;
    }
}

#define gX_BG_PARAM_SETVALUES_VEC(n) \
static inline gX_I0 gX_RenBG_ParamV##n##_SetValues(gX_RenBG_ParamV##n *param) { \
    param->constant = param->defaultvalue; \
    param->T = gX_F32v##n##_Zero(); \
     \
    param->resultantT = gX_F32v##n##_One(); \
    param->resultant = gX_F32v##n##_Zero(); \
    param->velocity = gX_F32v##n##_Zero(); \
    param->acceleration = gX_F32v##n##_Zero(); \
     \
    param->oscillatorT = gX_F32v##n##_One(); \
    param->oscillatorTotal = gX_F32v##n##_Zero(); \
    for (gX_U32 i = 0; i < gX_BG_OSCAMOUNT; i++) { \
        param->oscillator[i] = gX_F32v##n##_Zero(); \
        param->amplitude[i] = gX_F32v##n##_Zero(); \
        param->frequency[i] = gX_F32v##n##_One(); \
        param->phase[i] = gX_F32v##n##_Zero(); \
    } \
} \

gX_BG_PARAM_SETVALUES_VEC(2)
gX_BG_PARAM_SETVALUES_VEC(3)
gX_BG_PARAM_SETVALUES_VEC(4)

#define gX_BG_PARAM_INIT(dt,dtdef) \
gX_I0 gX_RenBG_Param##dt##_Init( gX_RenBG_Param##dt *param, gX_F32##dtdef defval) { \
    gX_RenBG_Param##dt##_Clear(param); \
    param->defaultvalue = defval; \
    gX_RenBG_Param##dt##_SetValues(param); \
} \

gX_BG_PARAM_INIT(Scalar,  )
gX_BG_PARAM_INIT(V2,    v2)
gX_BG_PARAM_INIT(V3,    v3)
gX_BG_PARAM_INIT(V4,    v4)

//param reset

#define gX_BG_PARAM_RESET(dt) \
gX_I0 gX_RenBG_Param##dt##_Reset( gX_RenBG_Param##dt *param) { \
    gX_RenBG_Param##dt##_SetValues(param); \
} \

gX_BG_PARAM_RESET(Scalar)
gX_BG_PARAM_RESET(V2)
gX_BG_PARAM_RESET(V3)
gX_BG_PARAM_RESET(V4)

//param update

//opencode: per-component macros to compress V2/V3/V4 oscillator math
#define BG_PARAM_OSC_COMP(c) \
    insidesine.c = param->frequency[i].c * time - param->phase[i].c; \
    oscillator_result.c = param->amplitude[i].c * sinf(insidesine.c * tau); \
    param->oscillator[i].c = oscillator_result.c; \
    oscillator_sum.c += oscillator_result.c;

#define BG_PARAM_TAIL_COMP(c) \
    param->oscillatorTotal.c = oscillator_sum.c; \
    param->velocity.c += param->acceleration.c * invfps; \
    param->resultant.c += param->velocity.c * invfps; \
    param->value.c = (param->oscillatorT.c * oscillator_sum.c) + (param->resultantT.c * param->resultant.c) + param->constant.c;

gX_I0 gX_RenBG_ParamScalar_Update( gX_RenBG_ParamScalar *param) {
    const gX_F32 time = (gX_F32)maindata.timeElapsed;
    const gX_F32 invfps = 1.0f/maindata.targetFPS;
    const gX_F32 tau = 2.0f*gX_PI;
    
    gX_F32 oscillator_result = 0.0f;
    gX_F32 oscillator_sum = 0.0f;
    gX_F32 insidesine = 0.0f;
    //opencode: SIMD hint — ivdep tells compiler iterations are independent
    #pragma GCC ivdep
    for (unsigned i = 0; i < gX_BG_OSCAMOUNT; i++) {
        insidesine = param->frequency[i]*time - param->phase[i];
        oscillator_result = param->amplitude[i]*sinf(insidesine*tau);
        param->oscillator[i] = oscillator_result;
        oscillator_sum += oscillator_result;
    }
    
    param->oscillatorTotal = oscillator_sum;
    param->velocity += param->acceleration*invfps;
    param->resultant += param->velocity*invfps;
    param->value = (param->oscillatorT * oscillator_sum) + (param->resultantT * param->resultant) + param->constant;
}

gX_I0 gX_RenBG_ParamV2_Update( gX_RenBG_ParamV2 *param) {
    const gX_F32 time = (gX_F32)maindata.timeElapsed;
    const gX_F32 invfps = 1.0f/maindata.targetFPS;
    const gX_F32 tau = 2.0f*gX_PI;
    
    gX_F32v2 oscillator_result = (gX_F32v2){0.0f,0.0f};
    gX_F32v2 oscillator_sum = (gX_F32v2){0.0f,0.0f};
    gX_F32v2 insidesine = (gX_F32v2){0.0f,0.0f};
    for (unsigned i = 0; i < gX_BG_OSCAMOUNT; i++) {
        BG_PARAM_OSC_COMP(x)
        BG_PARAM_OSC_COMP(y)
    }
    
    BG_PARAM_TAIL_COMP(x)
    BG_PARAM_TAIL_COMP(y)
}

gX_I0 gX_RenBG_ParamV3_Update( gX_RenBG_ParamV3 *param) {
    const gX_F32 time = (gX_F32)maindata.timeElapsed;
    const gX_F32 invfps = 1.0f/maindata.targetFPS;
    const gX_F32 tau = 2.0f*gX_PI;
    
    gX_F32v3 oscillator_result = (gX_F32v3){0.0f,0.0f,0.0f};
    gX_F32v3 oscillator_sum = (gX_F32v3){0.0f,0.0f,0.0f};
    gX_F32v3 insidesine = (gX_F32v3){0.0f,0.0f,0.0f};
    for (unsigned i = 0; i < gX_BG_OSCAMOUNT; i++) {
        BG_PARAM_OSC_COMP(x)
        BG_PARAM_OSC_COMP(y)
        BG_PARAM_OSC_COMP(z)
    }
    
    BG_PARAM_TAIL_COMP(x)
    BG_PARAM_TAIL_COMP(y)
    BG_PARAM_TAIL_COMP(z)
}

//opencode: SSE-accelerated V4 update — processes 4 components in parallel
#if defined(__SSE__) && defined(__x86_64__)
#include <xmmintrin.h>
gX_I0 gX_RenBG_ParamV4_Update( gX_RenBG_ParamV4 *param) {
    const gX_F32 time = (gX_F32)maindata.timeElapsed;
    const gX_F32 invfps = 1.0f/maindata.targetFPS;
    const gX_F32 tau = 2.0f*gX_PI;
    
    __m128 osc_sum = _mm_setzero_ps();
    
    for (unsigned i = 0; i < gX_BG_OSCAMOUNT; i++) {
        __m128 fre = _mm_loadu_ps(&param->frequency[i].x);
        __m128 pha = _mm_loadu_ps(&param->phase[i].x);
        __m128 amp = _mm_loadu_ps(&param->amplitude[i].x);
        
        __m128 in = _mm_sub_ps(_mm_mul_ps(fre, _mm_set1_ps(time)), pha);
        in = _mm_mul_ps(in, _mm_set1_ps(tau));
        
        // Scalar sinf for each component (no _mm_sin_ps without SVML)
        float comp[4];
        _mm_storeu_ps(comp, in);
        comp[0] = sinf(comp[0]);
        comp[1] = sinf(comp[1]);
        comp[2] = sinf(comp[2]);
        comp[3] = sinf(comp[3]);
        __m128 res = _mm_loadu_ps(comp);
        res = _mm_mul_ps(amp, res);
        
        _mm_storeu_ps(&param->oscillator[i].x, res);
        osc_sum = _mm_add_ps(osc_sum, res);
    }
    
    _mm_storeu_ps(&param->oscillatorTotal.x, osc_sum);
    
    __m128 vel = _mm_loadu_ps(&param->velocity.x);
    __m128 acc = _mm_loadu_ps(&param->acceleration.x);
    vel = _mm_add_ps(vel, _mm_mul_ps(acc, _mm_set1_ps(invfps)));
    _mm_storeu_ps(&param->velocity.x, vel);
    
    __m128 res = _mm_loadu_ps(&param->resultant.x);
    res = _mm_add_ps(res, _mm_mul_ps(vel, _mm_set1_ps(invfps)));
    _mm_storeu_ps(&param->resultant.x, res);
    
    __m128 oscT = _mm_loadu_ps(&param->oscillatorT.x);
    __m128 resT = _mm_loadu_ps(&param->resultantT.x);
    __m128 cnst = _mm_loadu_ps(&param->constant.x);
    __m128 val = _mm_add_ps(_mm_add_ps(_mm_mul_ps(oscT, osc_sum), _mm_mul_ps(resT, res)), cnst);
    _mm_storeu_ps(&param->value.x, val);
}
#else
gX_I0 gX_RenBG_ParamV4_Update( gX_RenBG_ParamV4 *param) {
    const gX_F32 time = (gX_F32)maindata.timeElapsed;
    const gX_F32 invfps = 1.0f/maindata.targetFPS;
    const gX_F32 tau = 2.0f*gX_PI;
    
    gX_F32v4 oscillator_result = (gX_F32v4){0.0f,0.0f,0.0f,0.0f};
    gX_F32v4 oscillator_sum = (gX_F32v4){0.0f,0.0f,0.0f,0.0f};
    gX_F32v4 insidesine = (gX_F32v4){0.0f,0.0f,0.0f,0.0f};
    #pragma GCC ivdep
    for (unsigned i = 0; i < gX_BG_OSCAMOUNT; i++) {
        BG_PARAM_OSC_COMP(x)
        BG_PARAM_OSC_COMP(y)
        BG_PARAM_OSC_COMP(z)
        BG_PARAM_OSC_COMP(w)
    }
    
    BG_PARAM_TAIL_COMP(x)
    BG_PARAM_TAIL_COMP(y)
    BG_PARAM_TAIL_COMP(z)
    BG_PARAM_TAIL_COMP(w)
}
#endif

//opencode: sinusoid reset/init/update — delegates to per-field param functions
gX_I0 gX_RenBG_Sinusoid_Reset(gX_RenBG_Sinusoid *sin) {
    gX_RenBG_ParamV4_Reset(&sin->amplitude);
    gX_RenBG_ParamV4_Reset(&sin->frequency);
    gX_RenBG_ParamV4_Reset(&sin->phase);
    gX_RenBG_ParamV4_Reset(&sin->speed);
    gX_RenBG_ParamV4_Reset(&sin->regularwave);
    gX_RenBG_ParamV4_Reset(&sin->interlaced);
    gX_RenBG_ParamV4_Reset(&sin->interlacedMultiplier);
    gX_RenBG_ParamV4_Reset(&sin->dcOffset); //opencode: reset dc offset
    gX_RenBG_ParamV4_Reset(&sin->linear); //opencode: reset linear coefficient
    gX_RenBG_ParamV4_Reset(&sin->quadratic); //opencode: reset quadratic
    gX_RenBG_ParamScalar_Reset(&sin->exponent);
    gX_RenBG_ParamScalar_Reset(&sin->attenuation); //opencode: reset attenuation
    gX_RenBG_ParamV2_Reset(&sin->center); //opencode: reset center
}

gX_I0 gX_RenBG_Sinusoid_Init(gX_RenBG_Sinusoid *sin) {
    gX_RenBG_ParamV4_Init(&sin->amplitude, (gX_F32v4){0,0,0,0});
    gX_RenBG_ParamV4_Init(&sin->frequency, (gX_F32v4){1,1,1,1});
    gX_RenBG_ParamV4_Init(&sin->phase, (gX_F32v4){0,0,0,0});
    gX_RenBG_ParamV4_Init(&sin->speed, (gX_F32v4){0,0,0,0});
    gX_RenBG_ParamV4_Init(&sin->regularwave, (gX_F32v4){1,1,1,1});
    gX_RenBG_ParamV4_Init(&sin->interlaced, (gX_F32v4){0,0,0,0});
    gX_RenBG_ParamV4_Init(&sin->interlacedMultiplier, (gX_F32v4){2,2,2,2});
    gX_RenBG_ParamV4_Init(&sin->dcOffset, (gX_F32v4){0,0,0,0}); //opencode: dc offset defaults to zero
    gX_RenBG_ParamV4_Init(&sin->linear, (gX_F32v4){0,0,0,0}); //opencode: linear coefficient defaults to zero
    gX_RenBG_ParamV4_Init(&sin->quadratic, (gX_F32v4){0,0,0,0}); //opencode: quadratic defaults to zero
    gX_RenBG_ParamScalar_Init(&sin->exponent, 1.0f);
    gX_RenBG_ParamScalar_Init(&sin->attenuation, 0.0f); //opencode: attenuation defaults to 0 (no decay)
    gX_RenBG_ParamV2_Init(&sin->center, (gX_F32v2){0,0}); //opencode: center defaults to origin
}

gX_I0 gX_RenBG_Sinusoid_Update(gX_RenBG_Sinusoid *sin) {
    gX_RenBG_ParamV4_Update(&sin->amplitude);
    gX_RenBG_ParamV4_Update(&sin->frequency);
    gX_RenBG_ParamV4_Update(&sin->phase);
    gX_RenBG_ParamV4_Update(&sin->speed);
    gX_RenBG_ParamV4_Update(&sin->regularwave);
    gX_RenBG_ParamV4_Update(&sin->interlaced);
    gX_RenBG_ParamV4_Update(&sin->interlacedMultiplier);
    gX_RenBG_ParamV4_Update(&sin->dcOffset); //opencode: dc offset update
    gX_RenBG_ParamV4_Update(&sin->linear); //opencode: linear update
    gX_RenBG_ParamV4_Update(&sin->quadratic); //opencode: quadratic update
    gX_RenBG_ParamScalar_Update(&sin->exponent);
    gX_RenBG_ParamScalar_Update(&sin->attenuation); //opencode: attenuation update
    gX_RenBG_ParamV2_Update(&sin->center); //opencode: center update
}

//opencode: HDMA channel functions
gX_I0 gX_RenBG_HDMA_Channel_Reset(gX_RenBG_HDMA_Channel *ch) {
    gX_RenBG_ParamScalar_Reset(&ch->amplitude);
    gX_RenBG_ParamScalar_Reset(&ch->frequency);
    gX_RenBG_ParamScalar_Reset(&ch->speed);
    gX_RenBG_ParamScalar_Reset(&ch->phase);
    gX_RenBG_ParamV2_Reset(&ch->offset);
}

gX_I0 gX_RenBG_HDMA_Channel_Init(gX_RenBG_HDMA_Channel *ch) {
    gX_RenBG_ParamScalar_Init(&ch->amplitude, 0.0f);
    gX_RenBG_ParamScalar_Init(&ch->frequency, 1.0f);
    gX_RenBG_ParamScalar_Init(&ch->speed, 0.0f);
    gX_RenBG_ParamScalar_Init(&ch->phase, 0.0f);
    gX_RenBG_ParamV2_Init(&ch->offset, (gX_F32v2){1,0});
}

gX_I0 gX_RenBG_HDMA_Channel_Update(gX_RenBG_HDMA_Channel *ch) {
    gX_RenBG_ParamScalar_Update(&ch->amplitude);
    gX_RenBG_ParamScalar_Update(&ch->frequency);
    gX_RenBG_ParamScalar_Update(&ch->speed);
    gX_RenBG_ParamScalar_Update(&ch->phase);
    gX_RenBG_ParamV2_Update(&ch->offset);
}
