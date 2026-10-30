/**********************************************************************************************
*
*   gxmath/easing.c
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

//opencode: ported easing functions from gamemath to gxmath

#include <math.h>

#include "easing.h"
#include "constants.h"

//opencode: gX_F32_EasingStep
gX_F32 gX_F32_EasingStep(gX_F32 start, gX_F32 end, gX_F32 t, gX_F32 s) {
    return (t >= s) ? end : start;
}

//opencode: gX_F32_EasingLinear
gX_F32 gX_F32_EasingLinear(gX_F32 start, gX_F32 end, gX_F32 t) {
    return (1.0f - t) * start + t * end;
}

//opencode: gX_F32_EasingQuad
gX_F32 gX_F32_EasingQuad(gX_F32 start, gX_F32 end, gX_F32 t) {
    return (1.0f - t) * (1.0f - t) * start + t * t * end;
}

//opencode: gX_F32_EasingPowReach
gX_F32 gX_F32_EasingPowReach(gX_F32 root, gX_F32 reach, gX_F32 t, gX_F32 s, gX_F32 p) {
    return -reach * powf(2.0f * t - 1, 2.0f * p) + reach + root;
}

//opencode: gX_F32_EasingPow
gX_F32 gX_F32_EasingPow(gX_F32 start, gX_F32 end, gX_F32 t, gX_F32 p) {
    return powf(1.0f - t, p) * start + powf(t, p) * end;
}

//opencode: gX_F32_EasingBezierS
gX_F32 gX_F32_EasingBezierS(gX_F32 start, gX_F32 end, gX_F32 t) {
    gX_F32 output = 0.0f;
    output +=        powf(1.0f - t, 3.0f) *                start;
    output += 3.0f * powf(1.0f - t, 2.0f) *      t       * start;
    output += 3.0f *     (1.0f - t)      * powf(t, 2.0f) * end;
    output +=                            powf(t, 3.0f) * end;
    return output;
}

//opencode: gX_F32_EasingBezier
gX_F32 gX_F32_EasingBezier(gX_F32 start, gX_F32 startc, gX_F32 endc, gX_F32 end, gX_F32 t) {
    gX_F32 output = 0.0f;
    output +=        powf(1.0f - t, 3.0f) *                start;
    output += 3.0f * powf(1.0f - t, 2.0f) *      t       * startc;
    output += 3.0f *     (1.0f - t)      * powf(t, 2.0f) * endc;
    output +=                            powf(t, 3.0f) * end;
    return output;
}

//opencode: gX_F32v2_EasingLinear
gX_F32v2 gX_F32v2_EasingLinear(gX_F32v2 start, gX_F32v2 end, gX_F32 t) {
    gX_F32v2 output = { 0 };
    output.x = (1.0f - t) * start.x + t * end.x;
    output.y = (1.0f - t) * start.y + t * end.y;
    return output;
}

//opencode: gX_F32v2_EasingBezier
gX_F32v2 gX_F32v2_EasingBezier(gX_F32v2 start, gX_F32v2 startc, gX_F32v2 endc, gX_F32v2 end, gX_F32 t) {
    gX_F32v2 output = { 0 };
    gX_F32 b0 =        powf(1.0f - t, 3.0f);
    gX_F32 b1 = 3.0f * powf(1.0f - t, 2.0f) * t;
    gX_F32 b2 = 3.0f * (1.0f - t) * powf(t, 2.0f);
    gX_F32 b3 =                           powf(t, 3.0f);
    output.x = b0 * start.x + b1 * startc.x + b2 * endc.x + b3 * end.x;
    output.y = b0 * start.y + b1 * startc.y + b2 * endc.y + b3 * end.y;
    return output;
}

//opencode: gX_F32v3_EasingLinear
gX_F32v3 gX_F32v3_EasingLinear(gX_F32v3 start, gX_F32v3 end, gX_F32 t) {
    gX_F32v3 output = { 0 };
    output.x = (1.0f - t) * start.x + t * end.x;
    output.y = (1.0f - t) * start.y + t * end.y;
    output.z = (1.0f - t) * start.z + t * end.z;
    return output;
}

//opencode: gX_F32v3_EasingBezier
gX_F32v3 gX_F32v3_EasingBezier(gX_F32v3 start, gX_F32v3 startc, gX_F32v3 endc, gX_F32v3 end, gX_F32 t) {
    gX_F32v3 output = { 0 };
    gX_F32 b0 =        powf(1.0f - t, 3.0f);
    gX_F32 b1 = 3.0f * powf(1.0f - t, 2.0f) * t;
    gX_F32 b2 = 3.0f * (1.0f - t) * powf(t, 2.0f);
    gX_F32 b3 =                           powf(t, 3.0f);
    output.x = b0 * start.x + b1 * startc.x + b2 * endc.x + b3 * end.x;
    output.y = b0 * start.y + b1 * startc.y + b2 * endc.y + b3 * end.y;
    output.z = b0 * start.z + b1 * startc.z + b2 * endc.z + b3 * end.z;
    return output;
}

//opencode: gX_F32v4_EasingLinear
gX_F32v4 gX_F32v4_EasingLinear(gX_F32v4 start, gX_F32v4 end, gX_F32 t) {
    gX_F32v4 output = { 0 };
    output.x = (1.0f - t) * start.x + t * end.x;
    output.y = (1.0f - t) * start.y + t * end.y;
    output.z = (1.0f - t) * start.z + t * end.z;
    output.w = (1.0f - t) * start.w + t * end.w;
    return output;
}

//opencode: gX_F32v4_EasingBezierS
gX_F32v4 gX_F32v4_EasingBezierS(gX_F32v4 start, gX_F32v4 end, gX_F32 t) {
    gX_F32v4 output = { 0 };
    output.x = gX_F32_EasingBezierS(start.x, end.x, t);
    output.y = gX_F32_EasingBezierS(start.y, end.y, t);
    output.z = gX_F32_EasingBezierS(start.z, end.z, t);
    output.w = gX_F32_EasingBezierS(start.w, end.w, t);
    return output;
}

//opencode: t-remap easing helpers — Robert Penner easing equations
//   All functions take t [0..1] and return a remapped t [0..1] (except back which may overshoot)
gX_F32 gX_F32_EaseQuadIn(gX_F32 t)       { return t * t; }
gX_F32 gX_F32_EaseQuadOut(gX_F32 t)      { return t * (2.0f - t); }
gX_F32 gX_F32_EaseQuadInOut(gX_F32 t)    { return (t < 0.5f) ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); }
gX_F32 gX_F32_EaseCubicIn(gX_F32 t)       { return t * t * t; }
gX_F32 gX_F32_EaseCubicOut(gX_F32 t)      { gX_F32 f = 1.0f - t; return 1.0f - f * f * f; }
gX_F32 gX_F32_EaseCubicInOut(gX_F32 t)    { return (t < 0.5f) ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t); }
gX_F32 gX_F32_EaseQuartIn(gX_F32 t)       { return t * t * t * t; }
gX_F32 gX_F32_EaseQuartOut(gX_F32 t)      { gX_F32 f = 1.0f - t; return 1.0f - f * f * f * f; }
gX_F32 gX_F32_EaseQuartInOut(gX_F32 t)    { return (t < 0.5f) ? 8.0f * t * t * t * t : 1.0f - 8.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t); }
gX_F32 gX_F32_EaseQuintIn(gX_F32 t)       { return t * t * t * t * t; }
gX_F32 gX_F32_EaseQuintOut(gX_F32 t)      { gX_F32 f = 1.0f - t; return 1.0f - f * f * f * f * f; }
gX_F32 gX_F32_EaseQuintInOut(gX_F32 t)    { return (t < 0.5f) ? 16.0f * t * t * t * t * t : 1.0f - 16.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t); }
gX_F32 gX_F32_EaseSineIn(gX_F32 t)        { return 1.0f - cosf(t * (gX_F32)gX_PI * 0.5f); }
gX_F32 gX_F32_EaseSineOut(gX_F32 t)       { return sinf(t * (gX_F32)gX_PI * 0.5f); }
gX_F32 gX_F32_EaseSineInOut(gX_F32 t)     { return -0.5f * (cosf((gX_F32)gX_PI * t) - 1.0f); }
gX_F32 gX_F32_EaseExpoIn(gX_F32 t)        { return (t <= 0.0f) ? 0.0f : powf(2.0f, 10.0f * (t - 1.0f)); }
gX_F32 gX_F32_EaseExpoOut(gX_F32 t)       { return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t); }
gX_F32 gX_F32_EaseExpoInOut(gX_F32 t)     { return (t <= 0.0f || t >= 1.0f) ? t : (t < 0.5f) ? 0.5f * powf(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * powf(2.0f, -20.0f * t + 10.0f); }
gX_F32 gX_F32_EaseCircIn(gX_F32 t)        { return 1.0f - sqrtf(1.0f - t * t); }
gX_F32 gX_F32_EaseCircOut(gX_F32 t)       { gX_F32 f = 1.0f - t; return sqrtf(1.0f - f * f); }
gX_F32 gX_F32_EaseCircInOut(gX_F32 t)     { return (t < 0.5f) ? 0.5f * (1.0f - sqrtf(1.0f - 4.0f * t * t)) : 0.5f * (sqrtf(1.0f - (3.0f - 2.0f * t) * (3.0f - 2.0f * t)) + 1.0f); }
gX_F32 gX_F32_EaseBackIn(gX_F32 t)        { gX_F32 s = 1.70158f; return t * t * ((s + 1.0f) * t - s); }
gX_F32 gX_F32_EaseBackOut(gX_F32 t)       { gX_F32 f = 1.0f - t; gX_F32 s = 1.70158f; return 1.0f - f * f * ((s + 1.0f) * f - s); }
gX_F32 gX_F32_EaseBackInOut(gX_F32 t)     { gX_F32 s = 1.70158f; gX_F32 s2 = s * 1.525f; return (t < 0.5f) ? 0.5f * (2.0f * t * 2.0f * t * ((s2 + 1.0f) * 2.0f * t - s2)) : 0.5f * (2.0f * t - 2.0f * (1.0f - t) * (1.0f - t) * ((s2 + 1.0f) * (1.0f - t) - s2) + 1.0f); }
gX_F32 gX_F32_EaseElasticIn(gX_F32 t)     { return (t <= 0.0f || t >= 1.0f) ? t : -powf(2.0f, 10.0f * t - 10.0f) * sinf((t * 10.0f - 10.75f) * 2.0f * (gX_F32)gX_PI / 3.0f); }
gX_F32 gX_F32_EaseElasticOut(gX_F32 t)    { return (t <= 0.0f || t >= 1.0f) ? t : powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * 2.0f * (gX_F32)gX_PI / 3.0f) + 1.0f; }
gX_F32 gX_F32_EaseElasticInOut(gX_F32 t)  { return (t <= 0.0f || t >= 1.0f) ? t : (t < 0.5f) ? -0.5f * powf(2.0f, 20.0f * t - 10.0f) * sinf((20.0f * t - 11.125f) * 2.0f * (gX_F32)gX_PI / 4.5f) : 0.5f * powf(2.0f, -20.0f * t + 10.0f) * sinf((20.0f * t - 11.125f) * 2.0f * (gX_F32)gX_PI / 4.5f) + 1.0f; }
gX_F32 gX_F32_EaseBounceIn(gX_F32 t)      { return 1.0f - gX_F32_EaseBounceOut(1.0f - t); }
gX_F32 gX_F32_EaseBounceOut(gX_F32 t)     { gX_F32 n1 = 7.5625f; gX_F32 d1 = 2.75f; if (t < 1.0f/d1) return n1*t*t; else if (t < 2.0f/d1) { t -= 1.5f/d1; return n1*t*t + 0.75f; } else if (t < 2.5f/d1) { t -= 2.25f/d1; return n1*t*t + 0.9375f; } else { t -= 2.625f/d1; return n1*t*t + 0.984375f; } }
gX_F32 gX_F32_EaseBounceInOut(gX_F32 t)   { return (t < 0.5f) ? 0.5f * gX_F32_EaseBounceIn(t * 2.0f) : 0.5f * gX_F32_EaseBounceOut(t * 2.0f - 1.0f) + 0.5f; }

//opencode: keep old name as alias for backward compat
gX_F32 gX_F32_EaseSmoothStep(gX_F32 t)    { return t * t * (3.0f - 2.0f * t); }

//opencode: unified dispatch — maps gX_Easing enum to easing function call
gX_F32 gX_Easing_Apply(gX_Easing ease, gX_F32 t) {
    switch (ease) {
        case gX_EASE_LINEAR:           return t;
        case gX_EASE_QUAD_IN:          return gX_F32_EaseQuadIn(t);
        case gX_EASE_QUAD_OUT:         return gX_F32_EaseQuadOut(t);
        case gX_EASE_QUAD_IN_OUT:      return gX_F32_EaseQuadInOut(t);
        case gX_EASE_CUBIC_IN:         return gX_F32_EaseCubicIn(t);
        case gX_EASE_CUBIC_OUT:        return gX_F32_EaseCubicOut(t);
        case gX_EASE_CUBIC_IN_OUT:     return gX_F32_EaseCubicInOut(t);
        case gX_EASE_QUART_IN:         return gX_F32_EaseQuartIn(t);
        case gX_EASE_QUART_OUT:        return gX_F32_EaseQuartOut(t);
        case gX_EASE_QUART_IN_OUT:     return gX_F32_EaseQuartInOut(t);
        case gX_EASE_QUINT_IN:         return gX_F32_EaseQuintIn(t);
        case gX_EASE_QUINT_OUT:        return gX_F32_EaseQuintOut(t);
        case gX_EASE_QUINT_IN_OUT:     return gX_F32_EaseQuintInOut(t);
        case gX_EASE_SINE_IN:          return gX_F32_EaseSineIn(t);
        case gX_EASE_SINE_OUT:         return gX_F32_EaseSineOut(t);
        case gX_EASE_SINE_IN_OUT:      return gX_F32_EaseSineInOut(t);
        case gX_EASE_EXPO_IN:          return gX_F32_EaseExpoIn(t);
        case gX_EASE_EXPO_OUT:         return gX_F32_EaseExpoOut(t);
        case gX_EASE_EXPO_IN_OUT:      return gX_F32_EaseExpoInOut(t);
        case gX_EASE_CIRC_IN:          return gX_F32_EaseCircIn(t);
        case gX_EASE_CIRC_OUT:         return gX_F32_EaseCircOut(t);
        case gX_EASE_CIRC_IN_OUT:      return gX_F32_EaseCircInOut(t);
        case gX_EASE_BACK_IN:          return gX_F32_EaseBackIn(t);
        case gX_EASE_BACK_OUT:         return gX_F32_EaseBackOut(t);
        case gX_EASE_BACK_IN_OUT:      return gX_F32_EaseBackInOut(t);
        case gX_EASE_ELASTIC_IN:       return gX_F32_EaseElasticIn(t);
        case gX_EASE_ELASTIC_OUT:      return gX_F32_EaseElasticOut(t);
        case gX_EASE_ELASTIC_IN_OUT:   return gX_F32_EaseElasticInOut(t);
        case gX_EASE_BOUNCE_IN:        return gX_F32_EaseBounceIn(t);
        case gX_EASE_BOUNCE_OUT:       return gX_F32_EaseBounceOut(t);
        case gX_EASE_BOUNCE_IN_OUT:    return gX_F32_EaseBounceInOut(t);
        case gX_EASE_SMOOTHSTEP:       return gX_F32_EaseSmoothStep(t);
        default:                       return t; // linear fallback
    }
}

//opencode: gX_F32v4_EasingBezier
gX_F32v4 gX_F32v4_EasingBezier(gX_F32v4 start, gX_F32v4 startc, gX_F32v4 endc, gX_F32v4 end, gX_F32 t) {
    gX_F32v4 output = { 0 };
    gX_F32 b0 =        powf(1.0f - t, 3.0f);
    gX_F32 b1 = 3.0f * powf(1.0f - t, 2.0f) * t;
    gX_F32 b2 = 3.0f * (1.0f - t) * powf(t, 2.0f);
    gX_F32 b3 =                           powf(t, 3.0f);
    output.x = b0 * start.x + b1 * startc.x + b2 * endc.x + b3 * end.x;
    output.y = b0 * start.y + b1 * startc.y + b2 * endc.y + b3 * end.y;
    output.z = b0 * start.z + b1 * startc.z + b2 * endc.z + b3 * end.z;
    output.w = b0 * start.w + b1 * startc.w + b2 * endc.w + b3 * end.w;
    return output;
}
