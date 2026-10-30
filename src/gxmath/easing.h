/**********************************************************************************************
*
*   gxmath/easing.h
*   Copyright (c) 2025 lanttu
* 
*   Uses functions from: raymath v1.5
* 
* 
*   <raymath> LICENSE: zlib/libpng
* 
*   Copyright (c) 2015-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef GXMATH_EASING_H
#define GXMATH_EASING_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: Ported easing functions from gamemath to gxmath

//opencode: gX_F32_EasingStep
gX_F32 gX_F32_EasingStep(gX_F32 start, gX_F32 end, gX_F32 t, gX_F32 s);

//opencode: gX_F32_EasingLinear
gX_F32 gX_F32_EasingLinear(gX_F32 start, gX_F32 end, gX_F32 t);

//opencode: gX_F32_EasingQuad
gX_F32 gX_F32_EasingQuad(gX_F32 start, gX_F32 end, gX_F32 t);

//opencode: gX_F32_EasingPowReach
gX_F32 gX_F32_EasingPowReach(gX_F32 root, gX_F32 reach, gX_F32 t, gX_F32 s, gX_F32 p);

//opencode: gX_F32_EasingPow
gX_F32 gX_F32_EasingPow(gX_F32 start, gX_F32 end, gX_F32 t, gX_F32 p);

//opencode: gX_F32_EasingBezierS
gX_F32 gX_F32_EasingBezierS(gX_F32 start, gX_F32 end, gX_F32 t);

//opencode: gX_F32_EasingBezier
gX_F32 gX_F32_EasingBezier(gX_F32 start, gX_F32 startc, gX_F32 endc, gX_F32 end, gX_F32 t);

//opencode: gX_F32v2_EasingLinear
gX_F32v2 gX_F32v2_EasingLinear(gX_F32v2 start, gX_F32v2 end, gX_F32 t);

//opencode: gX_F32v2_EasingBezier
gX_F32v2 gX_F32v2_EasingBezier(gX_F32v2 start, gX_F32v2 startc, gX_F32v2 endc, gX_F32v2 end, gX_F32 t);

//opencode: gX_F32v3_EasingLinear
gX_F32v3 gX_F32v3_EasingLinear(gX_F32v3 start, gX_F32v3 end, gX_F32 t);

//opencode: gX_F32v3_EasingBezier
gX_F32v3 gX_F32v3_EasingBezier(gX_F32v3 start, gX_F32v3 startc, gX_F32v3 endc, gX_F32v3 end, gX_F32 t);

//opencode: gX_F32v4_EasingLinear
gX_F32v4 gX_F32v4_EasingLinear(gX_F32v4 start, gX_F32v4 end, gX_F32 t);

//opencode: gX_F32v4_EasingBezierS
gX_F32v4 gX_F32v4_EasingBezierS(gX_F32v4 start, gX_F32v4 end, gX_F32 t);

//opencode: gX_F32v4_EasingBezier
gX_F32v4 gX_F32v4_EasingBezier(gX_F32v4 start, gX_F32v4 startc, gX_F32v4 endc, gX_F32v4 end, gX_F32 t);

//opencode: t-remap easing helpers — maps t [0..1] through a curve (Robert Penner easing equations)
gX_F32 gX_F32_EaseQuadIn(gX_F32 t);
gX_F32 gX_F32_EaseQuadOut(gX_F32 t);
gX_F32 gX_F32_EaseQuadInOut(gX_F32 t);
gX_F32 gX_F32_EaseCubicIn(gX_F32 t);
gX_F32 gX_F32_EaseCubicOut(gX_F32 t);
gX_F32 gX_F32_EaseCubicInOut(gX_F32 t);
gX_F32 gX_F32_EaseQuartIn(gX_F32 t);
gX_F32 gX_F32_EaseQuartOut(gX_F32 t);
gX_F32 gX_F32_EaseQuartInOut(gX_F32 t);
gX_F32 gX_F32_EaseQuintIn(gX_F32 t);
gX_F32 gX_F32_EaseQuintOut(gX_F32 t);
gX_F32 gX_F32_EaseQuintInOut(gX_F32 t);
gX_F32 gX_F32_EaseSineIn(gX_F32 t);
gX_F32 gX_F32_EaseSineOut(gX_F32 t);
gX_F32 gX_F32_EaseSineInOut(gX_F32 t);
gX_F32 gX_F32_EaseExpoIn(gX_F32 t);
gX_F32 gX_F32_EaseExpoOut(gX_F32 t);
gX_F32 gX_F32_EaseExpoInOut(gX_F32 t);
gX_F32 gX_F32_EaseCircIn(gX_F32 t);
gX_F32 gX_F32_EaseCircOut(gX_F32 t);
gX_F32 gX_F32_EaseCircInOut(gX_F32 t);
gX_F32 gX_F32_EaseBackIn(gX_F32 t);
gX_F32 gX_F32_EaseBackOut(gX_F32 t);
gX_F32 gX_F32_EaseBackInOut(gX_F32 t);
gX_F32 gX_F32_EaseElasticIn(gX_F32 t);
gX_F32 gX_F32_EaseElasticOut(gX_F32 t);
gX_F32 gX_F32_EaseElasticInOut(gX_F32 t);
gX_F32 gX_F32_EaseBounceIn(gX_F32 t);
gX_F32 gX_F32_EaseBounceOut(gX_F32 t);
gX_F32 gX_F32_EaseBounceInOut(gX_F32 t);
gX_F32 gX_F32_EaseSmoothStep(gX_F32 t);

//opencode: unified easing enum — all curves in one place for transitions/animations
typedef enum {
    gX_EASE_LINEAR,
    gX_EASE_QUAD_IN,
    gX_EASE_QUAD_OUT,
    gX_EASE_QUAD_IN_OUT,
    gX_EASE_CUBIC_IN,
    gX_EASE_CUBIC_OUT,
    gX_EASE_CUBIC_IN_OUT,
    gX_EASE_QUART_IN,
    gX_EASE_QUART_OUT,
    gX_EASE_QUART_IN_OUT,
    gX_EASE_QUINT_IN,
    gX_EASE_QUINT_OUT,
    gX_EASE_QUINT_IN_OUT,
    gX_EASE_SINE_IN,
    gX_EASE_SINE_OUT,
    gX_EASE_SINE_IN_OUT,
    gX_EASE_EXPO_IN,
    gX_EASE_EXPO_OUT,
    gX_EASE_EXPO_IN_OUT,
    gX_EASE_CIRC_IN,
    gX_EASE_CIRC_OUT,
    gX_EASE_CIRC_IN_OUT,
    gX_EASE_BACK_IN,
    gX_EASE_BACK_OUT,
    gX_EASE_BACK_IN_OUT,
    gX_EASE_ELASTIC_IN,
    gX_EASE_ELASTIC_OUT,
    gX_EASE_ELASTIC_IN_OUT,
    gX_EASE_BOUNCE_IN,
    gX_EASE_BOUNCE_OUT,
    gX_EASE_BOUNCE_IN_OUT,
    gX_EASE_SMOOTHSTEP,
} gX_Easing;

//opencode: unified dispatch — replaces duplicated switch statements in transition.c/animation.c
gX_F32 gX_Easing_Apply(gX_Easing ease, gX_F32 t);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_EASING_H
