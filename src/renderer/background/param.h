/**********************************************************************************************
*
*   [gX: src/renderer/background/param.h]
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


#ifndef RENDERER_BACKGROUND_PARAM_H
#define RENDERER_BACKGROUND_PARAM_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../../common/types.h"

#define gX_BG_OSCAMOUNT 8
#define gX_BG_PARAM_DEF(dt) \
    dt value; \
    dt constant; \
    dt defaultvalue; \
    dt T; \
    dt linear;\
     \
    dt resultantT; \
    dt resultant; \
    dt velocity; \
    dt acceleration; \
     \
    /*n oscillators*/ \
    dt oscillatorT; \
    dt oscillatorTotal; \
    dt oscillator[gX_BG_OSCAMOUNT]; \
    dt amplitude[gX_BG_OSCAMOUNT]; \
    dt frequency[gX_BG_OSCAMOUNT]; \
    dt phase[gX_BG_OSCAMOUNT]; \
    gX_U32 waveform[gX_BG_OSCAMOUNT]; \

typedef struct gX_RenBG_ParamScalar {
    gX_BG_PARAM_DEF(gX_F32)
} gX_RenBG_ParamScalar;

typedef struct gX_RenBG_ParamV2 {
    gX_BG_PARAM_DEF(gX_F32v2)
} gX_RenBG_ParamV2;

typedef struct gX_RenBG_ParamV3 {
    gX_BG_PARAM_DEF(gX_F32v3)
} gX_RenBG_ParamV3;

typedef struct gX_RenBG_ParamV4 {
    gX_BG_PARAM_DEF(gX_F32v4)
} gX_RenBG_ParamV4;

#define gX_BG_PARAM_FUNCGEN(name,vdt) \
    gX_I0 gX_RenBG_Param##name##_Clear( gX_RenBG_Param##name *param); \
    gX_I0 gX_RenBG_Param##name##_Init(  gX_RenBG_Param##name *param, gX_F32##vdt defval); \
    gX_I0 gX_RenBG_Param##name##_Reset( gX_RenBG_Param##name *param); \
    gX_I0 gX_RenBG_Param##name##_Update(gX_RenBG_Param##name *param); \
    
gX_BG_PARAM_FUNCGEN(Scalar,   )
gX_BG_PARAM_FUNCGEN(V2,     v2)
gX_BG_PARAM_FUNCGEN(V3,     v3)
gX_BG_PARAM_FUNCGEN(V4,     v4)

#define gX_BG_SINUSOID_COUNT 8
#define gX_BG_HDMA_CHANNEL_COUNT 8
#define gX_BG_PALETTE_COLORS_MAX 256

//opencode: sinusoid type — V4 wave attrs + scalar exponent + paramv2 center + quadratic term
typedef struct gX_RenBG_Sinusoid {
    gX_RenBG_ParamV4 amplitude;
    gX_RenBG_ParamV4 frequency;
    gX_RenBG_ParamV4 phase;
    gX_RenBG_ParamV4 speed;
    //lanttu: calculate the speed outside of the shader, because changing the speed value makes the image flip out, but calculating outside makes it smooth; this happens because speed is tied to the function
    gX_RenBG_ParamV4 regularwave;
    gX_RenBG_ParamV4 interlaced;
    gX_RenBG_ParamV4 interlacedMultiplier;
    gX_RenBG_ParamV4 dcOffset; //lanttu: sinusoid linear component constant value
    gX_RenBG_ParamV4 linear; //lanttu: sinusoid linear component linear value
    gX_RenBG_ParamV4 quadratic; //opencode: quadratic coefficient
    gX_RenBG_ParamScalar exponent;
    gX_RenBG_ParamScalar attenuation; //opencode: radial decay factor (exp(-dist*attn))
    gX_RenBG_ParamV2 center; //opencode: center point (animatable ParamV2)
} gX_RenBG_Sinusoid;

gX_I0 gX_RenBG_Sinusoid_Reset(gX_RenBG_Sinusoid *sin);
gX_I0 gX_RenBG_Sinusoid_Init(gX_RenBG_Sinusoid *sin);
gX_I0 gX_RenBG_Sinusoid_Update(gX_RenBG_Sinusoid *sin);

//opencode: HDMA channel — simulates SNES HDMA per-scanline wave distortion.
//   amplitude controls wave height (pixels), frequency = cycles per screen height,
//   speed = animation rate, phase = offset, offset = scroll direction (x,y).
typedef struct gX_RenBG_HDMA_Channel {
    gX_RenBG_ParamScalar amplitude;
    gX_RenBG_ParamScalar frequency;
    gX_RenBG_ParamScalar speed;
    gX_RenBG_ParamScalar phase;
    gX_RenBG_ParamV2     offset;
} gX_RenBG_HDMA_Channel;

gX_I0 gX_RenBG_HDMA_Channel_Reset(gX_RenBG_HDMA_Channel *ch);
gX_I0 gX_RenBG_HDMA_Channel_Init(gX_RenBG_HDMA_Channel *ch);
gX_I0 gX_RenBG_HDMA_Channel_Update(gX_RenBG_HDMA_Channel *ch);
    
#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_PARAM_H
