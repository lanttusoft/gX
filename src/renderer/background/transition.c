/**********************************************************************************************
*
*   [gX: src/renderer/background/transition.c]
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


//opencode: background transition — param morphing between presets
#include "transition.h"
#include "struct.h"
#include "schema.h"

#include "../../common/log.h"
#include "../../wrappers/wrappers.h"

// ---------------------------------------------------------------------------
// Param lerp helpers — lerp constant/velocity/acceleration + oscillator
//   amplitude/frequency/phase between two params of the same type.
//   Scalar/V2/V3/V4 share the same layout (gX_BG_PARAM_DEF), only component
//   count differs. We operate via component-count generic helpers.
// ---------------------------------------------------------------------------

#define BG_LERP_N(dst, a, b, n, t) \
    for (gX_I32 _i = 0; _i < (n); _i++) (dst)[_i] = (a)[_i] + ((b)[_i] - (a)[_i]) * (t)

static gX_I0 bg_lerp_scalar(gX_RenBG_ParamScalar *dst, const gX_RenBG_ParamScalar *a, const gX_RenBG_ParamScalar *b, gX_F32 t) {
    BG_LERP_N(&dst->constant, &a->constant, &b->constant, 1, t);
    BG_LERP_N(&dst->velocity, &a->velocity, &b->velocity, 1, t);
    BG_LERP_N(&dst->acceleration, &a->acceleration, &b->acceleration, 1, t);
    for (gX_I32 o = 0; o < (gX_I32)gX_BG_OSCAMOUNT; o++) {
        BG_LERP_N(&dst->amplitude[o], &a->amplitude[o], &b->amplitude[o], 1, t);
        BG_LERP_N(&dst->frequency[o], &a->frequency[o], &b->frequency[o], 1, t);
        BG_LERP_N(&dst->phase[o], &a->phase[o], &b->phase[o], 1, t);
    }
}

static gX_I0 bg_lerp_v2(gX_RenBG_ParamV2 *dst, const gX_RenBG_ParamV2 *a, const gX_RenBG_ParamV2 *b, gX_F32 t) {
    BG_LERP_N(&dst->constant.x, &a->constant.x, &b->constant.x, 2, t);
    BG_LERP_N(&dst->velocity.x, &a->velocity.x, &b->velocity.x, 2, t);
    BG_LERP_N(&dst->acceleration.x, &a->acceleration.x, &b->acceleration.x, 2, t);
    for (gX_I32 o = 0; o < (gX_I32)gX_BG_OSCAMOUNT; o++) {
        BG_LERP_N(&dst->amplitude[o].x, &a->amplitude[o].x, &b->amplitude[o].x, 2, t);
        BG_LERP_N(&dst->frequency[o].x, &a->frequency[o].x, &b->frequency[o].x, 2, t);
        BG_LERP_N(&dst->phase[o].x, &a->phase[o].x, &b->phase[o].x, 2, t);
    }
}

static gX_I0 bg_lerp_v3(gX_RenBG_ParamV3 *dst, const gX_RenBG_ParamV3 *a, const gX_RenBG_ParamV3 *b, gX_F32 t) {
    BG_LERP_N(&dst->constant.x, &a->constant.x, &b->constant.x, 3, t);
    BG_LERP_N(&dst->velocity.x, &a->velocity.x, &b->velocity.x, 3, t);
    BG_LERP_N(&dst->acceleration.x, &a->acceleration.x, &b->acceleration.x, 3, t);
    for (gX_I32 o = 0; o < (gX_I32)gX_BG_OSCAMOUNT; o++) {
        BG_LERP_N(&dst->amplitude[o].x, &a->amplitude[o].x, &b->amplitude[o].x, 3, t);
        BG_LERP_N(&dst->frequency[o].x, &a->frequency[o].x, &b->frequency[o].x, 3, t);
        BG_LERP_N(&dst->phase[o].x, &a->phase[o].x, &b->phase[o].x, 3, t);
    }
}

static gX_I0 bg_lerp_v4(gX_RenBG_ParamV4 *dst, const gX_RenBG_ParamV4 *a, const gX_RenBG_ParamV4 *b, gX_F32 t) {
    BG_LERP_N(&dst->constant.x, &a->constant.x, &b->constant.x, 4, t);
    BG_LERP_N(&dst->velocity.x, &a->velocity.x, &b->velocity.x, 4, t);
    BG_LERP_N(&dst->acceleration.x, &a->acceleration.x, &b->acceleration.x, 4, t);
    for (gX_I32 o = 0; o < (gX_I32)gX_BG_OSCAMOUNT; o++) {
        BG_LERP_N(&dst->amplitude[o].x, &a->amplitude[o].x, &b->amplitude[o].x, 4, t);
        BG_LERP_N(&dst->frequency[o].x, &a->frequency[o].x, &b->frequency[o].x, 4, t);
        BG_LERP_N(&dst->phase[o].x, &a->phase[o].x, &b->phase[o].x, 4, t);
    }
}

// ---------------------------------------------------------------------------
// Per-section lerp
// ---------------------------------------------------------------------------
static gX_I0 bg_lerp_sinusoid(gX_RenBG_Sinusoid *dst, const gX_RenBG_Sinusoid *a, const gX_RenBG_Sinusoid *b, gX_F32 t) {
    bg_lerp_v4(&dst->amplitude, &a->amplitude, &b->amplitude, t);
    bg_lerp_v4(&dst->frequency, &a->frequency, &b->frequency, t);
    bg_lerp_v4(&dst->phase, &a->phase, &b->phase, t);
    bg_lerp_v4(&dst->speed, &a->speed, &b->speed, t);
    bg_lerp_v4(&dst->regularwave, &a->regularwave, &b->regularwave, t);
    bg_lerp_v4(&dst->interlaced, &a->interlaced, &b->interlaced, t);
    bg_lerp_v4(&dst->interlacedMultiplier, &a->interlacedMultiplier, &b->interlacedMultiplier, t);
    bg_lerp_v4(&dst->dcOffset, &a->dcOffset, &b->dcOffset, t);
    bg_lerp_v4(&dst->linear, &a->linear, &b->linear, t);
    bg_lerp_v4(&dst->quadratic, &a->quadratic, &b->quadratic, t);
    bg_lerp_scalar(&dst->exponent, &a->exponent, &b->exponent, t);
    bg_lerp_scalar(&dst->attenuation, &a->attenuation, &b->attenuation, t);
    bg_lerp_v2(&dst->center, &a->center, &b->center, t);
}

static gX_I0 bg_lerp_hdma(gX_RenBG_HDMA_Channel *dst, const gX_RenBG_HDMA_Channel *a, const gX_RenBG_HDMA_Channel *b, gX_F32 t) {
    bg_lerp_scalar(&dst->amplitude, &a->amplitude, &b->amplitude, t);
    bg_lerp_scalar(&dst->frequency, &a->frequency, &b->frequency, t);
    bg_lerp_scalar(&dst->speed, &a->speed, &b->speed, t);
    bg_lerp_scalar(&dst->phase, &a->phase, &b->phase, t);
    bg_lerp_v2(&dst->offset, &a->offset, &b->offset, t);
}

static gX_I0 bg_lerp_transform(gX_RendererBackground_Transform *dst,
    const gX_RendererBackground_Transform *a, const gX_RendererBackground_Transform *b, gX_F32 t) {
    bg_lerp_scalar(&dst->scale, &a->scale, &b->scale, t);
    bg_lerp_v2(&dst->scaleOrigin, &a->scaleOrigin, &b->scaleOrigin, t);
    bg_lerp_scalar(&dst->rotate, &a->rotate, &b->rotate, t);
    bg_lerp_v2(&dst->rotateOrigin, &a->rotateOrigin, &b->rotateOrigin, t);
    bg_lerp_v4(&dst->stretch, &a->stretch, &b->stretch, t);
    bg_lerp_v2(&dst->perspective, &a->perspective, &b->perspective, t);
    bg_lerp_v2(&dst->translate, &a->translate, &b->translate, t);
    bg_lerp_v2(&dst->affine, &a->affine, &b->affine, t);
    bg_lerp_scalar(&dst->matrixBlend, &a->matrixBlend, &b->matrixBlend, t);
    for (gX_I32 i = 0; i < 16; i++) {
        const gX_F32 *am = (const gX_F32*)&a->matrixA;
        const gX_F32 *bm = (const gX_F32*)&b->matrixA;
        ((gX_F32*)&dst->matrixA)[i] = am[i] + (bm[i] - am[i]) * t;
        am = (const gX_F32*)&a->matrixB;
        bm = (const gX_F32*)&b->matrixB;
        ((gX_F32*)&dst->matrixB)[i] = am[i] + (bm[i] - am[i]) * t;
    }
    dst->transformation = dst->matrixA; // placeholder — recomputed in param update
}

static gX_I0 bg_lerp_special(gX_RendererBackground_Special *dst,
    const gX_RendererBackground_Special *a, const gX_RendererBackground_Special *b, gX_F32 t) {
    bg_lerp_v3(&dst->warp, &a->warp, &b->warp, t);
    bg_lerp_scalar(&dst->warpExponent, &a->warpExponent, &b->warpExponent, t);
    bg_lerp_v4(&dst->waveAmplitude, &a->waveAmplitude, &b->waveAmplitude, t);
    bg_lerp_v4(&dst->waveFrequency, &a->waveFrequency, &b->waveFrequency, t);
    bg_lerp_v4(&dst->waveRegularwave, &a->waveRegularwave, &b->waveRegularwave, t);
    bg_lerp_v4(&dst->waveInterlaced, &a->waveInterlaced, &b->waveInterlaced, t);
    bg_lerp_v4(&dst->waveInterlacedMultiplier, &a->waveInterlacedMultiplier, &b->waveInterlacedMultiplier, t);
    bg_lerp_v4(&dst->waveSpeed, &a->waveSpeed, &b->waveSpeed, t);
    bg_lerp_v4(&dst->wavePhase, &a->wavePhase, &b->wavePhase, t);
    for (gX_I32 i = 0; i < (gX_I32)gX_BG_SINUSOID_COUNT; i++)
        bg_lerp_sinusoid(&dst->sinusoids[i], &a->sinusoids[i], &b->sinusoids[i], t);
    for (gX_I32 i = 0; i < (gX_I32)gX_BG_HDMA_CHANNEL_COUNT; i++)
        bg_lerp_hdma(&dst->hdma[i], &a->hdma[i], &b->hdma[i], t);
    // noise params
    bg_lerp_scalar(&dst->noiseScale, &a->noiseScale, &b->noiseScale, t);
    bg_lerp_scalar(&dst->noiseSpeed, &a->noiseSpeed, &b->noiseSpeed, t);
    bg_lerp_scalar(&dst->noiseLacunarity, &a->noiseLacunarity, &b->noiseLacunarity, t);
    bg_lerp_scalar(&dst->noiseGain, &a->noiseGain, &b->noiseGain, t);
}

static gX_I0 bg_lerp_palette(gX_RendererBackground_Palette *dst,
    const gX_RendererBackground_Palette *a, const gX_RendererBackground_Palette *b, gX_F32 t) {
    bg_lerp_scalar(&dst->size, &a->size, &b->size, t);
    bg_lerp_scalar(&dst->amount, &a->amount, &b->amount, t);
    bg_lerp_scalar(&dst->speed, &a->speed, &b->speed, t);
    bg_lerp_scalar(&dst->frequency, &a->frequency, &b->frequency, t);
    for (gX_I32 i = 0; i < (gX_I32)gX_BG_PALETTE_COLORS_MAX; i++)
        bg_lerp_v4(&dst->colors[i], &a->colors[i], &b->colors[i], t);
    bg_lerp_v4(&dst->color1, &a->color1, &b->color1, t);
    bg_lerp_v4(&dst->color2, &a->color2, &b->color2, t);
    bg_lerp_scalar(&dst->bgBoundary, &a->bgBoundary, &b->bgBoundary, t);
    bg_lerp_v4(&dst->bgColor, &a->bgColor, &b->bgColor, t);
    bg_lerp_scalar(&dst->fgBoundary, &a->fgBoundary, &b->fgBoundary, t);
    bg_lerp_v4(&dst->fgColor, &a->fgColor, &b->fgColor, t);
}

static gX_I0 bg_lerp_misc(gX_RendererBackground_Misc *dst,
    const gX_RendererBackground_Misc *a, const gX_RendererBackground_Misc *b, gX_F32 t) {
    bg_lerp_v4(&dst->boundaryXY, &a->boundaryXY, &b->boundaryXY, t);
    bg_lerp_v2(&dst->boundaryZ, &a->boundaryZ, &b->boundaryZ, t);
    bg_lerp_v2(&dst->modulo, &a->modulo, &b->modulo, t);
}

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------
gX_F32 gX_RendererBackground_Transition_Ease(gX_F32 t, gX_I32 ease) {
    if (ease == GX_BG_TRANS_EASE_IN) return t * t;
    if (ease == GX_BG_TRANS_EASE_OUT) return t * (2.0f - t);
    if (ease == GX_BG_TRANS_EASE_IN_OUT) {
        return (t < 0.5f) ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    }
    return t; // linear
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
gX_I32 gX_RendererBackground_Transition_Start(gX_RendererBackground *bg,
    const gX_I8 *presetPath, gX_F32 duration, gX_I32 ease)
{
    if (!bg || !presetPath) return -1;
    gX_RendererBackground_Transition *tr = &bg->transition;

    // snapshot current (source) params
    tr->srcTransform = bg->transform;
    tr->srcSpecial   = bg->special;
    tr->srcPalette   = bg->palette;
    tr->srcMisc      = bg->misc;

    // load target preset into live bg, then snapshot it
    gX_I32 ret = gX_RendererBG_Preset_Load_New(bg, presetPath);
    if (ret != 0) {
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR,
            "BG_TRANSITION: preset load failed for %s", presetPath);
        return -1;
    }
    tr->dstTransform = bg->transform;
    tr->dstSpecial   = bg->special;
    tr->dstPalette   = bg->palette;
    tr->dstMisc      = bg->misc;

    // restore source params into live bg so transition starts from current look
    bg->transform = tr->srcTransform;
    bg->special   = tr->srcSpecial;
    bg->palette   = tr->srcPalette;
    bg->misc      = tr->srcMisc;

    tr->active = 1;
    tr->loadedTarget = 1;
    tr->duration = duration > 0.0f ? duration : 1.0f;
    tr->elapsed = 0.0f;
    tr->ease = (gX_BG_Transition_Ease)ease;

    // mark dirty so values upload on first transition frame
    bg->state.paramsDirty = true;
    bg->state.sinusoidDirty = true;
    bg->state.colorsDirty = true;

    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "BG_TRANSITION: started -> %s (%.2fs, ease %d)", presetPath, duration, ease);
    return 0;
}

gX_I0 gX_RendererBackground_Transition_Update(gX_RendererBackground *bg) {
    gX_RendererBackground_Transition *tr = &bg->transition;
    if (!bg || !tr->active) return;

    tr->elapsed += (gX_F32)gX_FrameTime_Get();
    gX_F32 t = tr->duration > 0.0f ? (tr->elapsed / tr->duration) : 1.0f;
    if (t >= 1.0f) {
        // snap to target, end transition
        bg->transform = tr->dstTransform;
        bg->special   = tr->dstSpecial;
        bg->palette   = tr->dstPalette;
        bg->misc      = tr->dstMisc;
        bg->state.paramsDirty = true;
        bg->state.sinusoidDirty = true;
        bg->state.colorsDirty = true;
        tr->active = 0;
        tr->loadedTarget = 0;
        gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_TRANSITION: finished");
        return;
    }

    gX_F32 e = gX_RendererBackground_Transition_Ease(t, (gX_I32)tr->ease);

    bg_lerp_transform(&bg->transform, &tr->srcTransform, &tr->dstTransform, e);
    bg_lerp_special(&bg->special, &tr->srcSpecial, &tr->dstSpecial, e);
    bg_lerp_palette(&bg->palette, &tr->srcPalette, &tr->dstPalette, e);
    bg_lerp_misc(&bg->misc, &tr->srcMisc, &tr->dstMisc, e);

    bg->state.paramsDirty = true;
    bg->state.sinusoidDirty = true;
    bg->state.colorsDirty = true;
}

gX_I0 gX_RendererBackground_Transition_Stop(gX_RendererBackground *bg) {
    if (!bg) return;
    bg->transition.active = 0;
    bg->transition.loadedTarget = 0;
}
