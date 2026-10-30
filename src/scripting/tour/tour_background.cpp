/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_background.cpp]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: tour background API (gX-native remake of gameX's BgSet* surface): the
//   live background heap instance (renderer.background.data) driven through the
//   full param struct — transform/special (warp, wave, HDMA, noise)/sinusoids/
//   palette/misc — plus state toggles, texture loading, built-in presets and
//   the preset-morph transition. Two gX specifics the bindings must respect:
//     - the module's Update_Params/Colors/Sinusoids are dirty-gated, so every
//       setter marks the matching dirty flag (paramsDirty/colorsDirty/
//       sinusoidDirty) or the write would not reach the shader until some
//       motion happens;
//     - transitions tick inside gX_RendererBackground_Update, so scripts
//       just start one and wait (Sleep pairs with the ms duration).
//   Getters return the script-set `constant` (the target), not the animated
//   value, so round-trips are exact.
//opencode: raylib headers FIRST with C++ linkage — renderer headers reach
//   raylib through wrappers' extern "C" blocks, which would give raymath's
//   operator overloads C linkage (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <stdio.h>
#include <string>

#include "../../common/log.h"
#include "../../main.h"
#include "../../renderer/renderer.h"
#include "../../renderer/background.h"

// ===========================================================================
// Instance + dirty gate helpers
// ===========================================================================

static gX_RendererBackground *TourBackground_Instance(void) {
    if (!renderer.background.data || renderer.background.amount == 0) return NULL;
    return (gX_RendererBackground *)renderer.background.data;
}

static gX_RendererBackground *TourBackground_Params(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.paramsDirty = 1;
    return bg;
}

static gX_RendererBackground *TourBackground_Colors(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.colorsDirty = 1;
    return bg;
}

static gX_RendererBackground *TourBackground_MarkDirty(gX_RendererBackground *bg) {
    if (bg) {
        bg->state.paramsDirty = 1;
        bg->state.sinusoidDirty = 1;
        bg->state.colorsDirty = 1;
    }
    return bg;
}

// ===========================================================================
// Transform parameters
// ===========================================================================

static void TourBackground_SetScale(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.scale.constant = v;
}

static void TourBackground_SetScaleVel(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.scale.velocity = v;
}

static void TourBackground_SetScaleAccel(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.scale.acceleration = v;
}

static void TourBackground_SetScaleOrigin(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.scaleOrigin.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetScaleOriginVel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.scaleOrigin.velocity = gX_F32v2_Make(x, y);
}

static void TourBackground_SetRotate(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.rotate.constant = v;
}

static void TourBackground_SetRotateVel(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.rotate.velocity = v;
}

static void TourBackground_SetRotateAccel(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.rotate.acceleration = v;
}

static void TourBackground_SetRotateOrigin(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.rotateOrigin.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetRotateOriginVel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.rotateOrigin.velocity = gX_F32v2_Make(x, y);
}

static void TourBackground_SetTranslate(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.translate.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetTranslateVel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.translate.velocity = gX_F32v2_Make(x, y);
}

static void TourBackground_SetTranslateAccel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.translate.acceleration = gX_F32v2_Make(x, y);
}

static void TourBackground_SetStretch(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.stretch.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetStretchVel(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.stretch.velocity = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetPerspective(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.perspective.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetPerspectiveVel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.perspective.velocity = gX_F32v2_Make(x, y);
}

static void TourBackground_SetAffine(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.affine.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetAffineVel(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.affine.velocity = gX_F32v2_Make(x, y);
}

static void TourBackground_SetMatrixBlend(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->transform.matrixBlend.constant = v;
}

// ===========================================================================
// Direct matrices (row-major, applied with matrixBlend when enabled)
// ===========================================================================

static void TourBackground_SetMatrixA(gX_F32 m0, gX_F32 m1, gX_F32 m2, gX_F32 m3,
    gX_F32 m4, gX_F32 m5, gX_F32 m6, gX_F32 m7,
    gX_F32 m8, gX_F32 m9, gX_F32 m10, gX_F32 m11,
    gX_F32 m12, gX_F32 m13, gX_F32 m14, gX_F32 m15)
{
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    bg->transform.matrixA.m0 = m0; bg->transform.matrixA.m1 = m1;
    bg->transform.matrixA.m2 = m2; bg->transform.matrixA.m3 = m3;
    bg->transform.matrixA.m4 = m4; bg->transform.matrixA.m5 = m5;
    bg->transform.matrixA.m6 = m6; bg->transform.matrixA.m7 = m7;
    bg->transform.matrixA.m8 = m8; bg->transform.matrixA.m9 = m9;
    bg->transform.matrixA.m10 = m10; bg->transform.matrixA.m11 = m11;
    bg->transform.matrixA.m12 = m12; bg->transform.matrixA.m13 = m13;
    bg->transform.matrixA.m14 = m14; bg->transform.matrixA.m15 = m15;
}

static void TourBackground_SetMatrixB(gX_F32 m0, gX_F32 m1, gX_F32 m2, gX_F32 m3,
    gX_F32 m4, gX_F32 m5, gX_F32 m6, gX_F32 m7,
    gX_F32 m8, gX_F32 m9, gX_F32 m10, gX_F32 m11,
    gX_F32 m12, gX_F32 m13, gX_F32 m14, gX_F32 m15)
{
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    bg->transform.matrixB.m0 = m0; bg->transform.matrixB.m1 = m1;
    bg->transform.matrixB.m2 = m2; bg->transform.matrixB.m3 = m3;
    bg->transform.matrixB.m4 = m4; bg->transform.matrixB.m5 = m5;
    bg->transform.matrixB.m6 = m6; bg->transform.matrixB.m7 = m7;
    bg->transform.matrixB.m8 = m8; bg->transform.matrixB.m9 = m9;
    bg->transform.matrixB.m10 = m10; bg->transform.matrixB.m11 = m11;
    bg->transform.matrixB.m12 = m12; bg->transform.matrixB.m13 = m13;
    bg->transform.matrixB.m14 = m14; bg->transform.matrixB.m15 = m15;
}

// ===========================================================================
// Special — warp + wave
// ===========================================================================

static void TourBackground_SetWarp(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.warp.constant = gX_F32v3_Make(x, y, z);
}

static void TourBackground_SetWarpVel(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.warp.velocity = gX_F32v3_Make(x, y, z);
}

static void TourBackground_SetWarpExp(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.warpExponent.constant = v;
}

static void TourBackground_SetWaveAmplitude(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveAmplitude.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWaveFrequency(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveFrequency.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWaveSpeed(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveSpeed.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWavePhase(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.wavePhase.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWaveRegularwave(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveRegularwave.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWaveInterlaced(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveInterlaced.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetWaveInterlacedMultiplier(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.waveInterlacedMultiplier.constant = gX_F32v4_Make(x, y, z, w);
}

// ===========================================================================
// Special — HDMA scanline channels (8)
// ===========================================================================

static gX_I1 TourBackground_HdmaOk(gX_I32 idx) {
    return (idx >= 0 && idx < (gX_I32)gX_BG_HDMA_CHANNEL_COUNT) ? 1 : 0;
}

static void TourBackground_SetHdmaAmplitude(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_HdmaOk(idx)) return;
    bg->special.hdma[idx].amplitude.constant = v;
    bg->state.paramsDirty = 1;
}

static void TourBackground_SetHdmaFrequency(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_HdmaOk(idx)) return;
    bg->special.hdma[idx].frequency.constant = v;
    bg->state.paramsDirty = 1;
}

static void TourBackground_SetHdmaSpeed(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_HdmaOk(idx)) return;
    bg->special.hdma[idx].speed.constant = v;
    bg->state.paramsDirty = 1;
}

static void TourBackground_SetHdmaPhase(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_HdmaOk(idx)) return;
    bg->special.hdma[idx].phase.constant = v;
    bg->state.paramsDirty = 1;
}

static void TourBackground_SetHdmaOffset(gX_I32 idx, gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_HdmaOk(idx)) return;
    bg->special.hdma[idx].offset.constant = gX_F32v2_Make(x, y);
    bg->state.paramsDirty = 1;
}

// ===========================================================================
// Special — procedural noise
// ===========================================================================

static void TourBackground_SetNoiseMode(gX_I32 mode) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->special.noiseMode = mode;
}

static void TourBackground_SetNoiseScale(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.noiseScale.constant = v;
}

static void TourBackground_SetNoiseSpeed(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.noiseSpeed.constant = v;
}

static void TourBackground_SetNoiseOctaves(gX_I32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->special.noiseOctaves = v;
}

static void TourBackground_SetNoiseLacunarity(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.noiseLacunarity.constant = v;
}

static void TourBackground_SetNoiseGain(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->special.noiseGain.constant = v;
}

// ===========================================================================
// Sinusoids (8 x 13 fields)
// ===========================================================================

static gX_I1 TourBackground_SinusoidOk(gX_I32 idx) {
    return (idx >= 0 && idx < (gX_I32)gX_BG_SINUSOID_COUNT) ? 1 : 0;
}

static void TourBackground_SetSinusoidAmplitude(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].amplitude.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidFrequency(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].frequency.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidPhase(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].phase.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidSpeed(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].speed.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidRegularwave(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].regularwave.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidInterlaced(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].interlaced.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidInterlacedMultiplier(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].interlacedMultiplier.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidDcOffset(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].dcOffset.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidLinear(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].linear.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidQuadratic(gX_I32 idx, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].quadratic.constant = gX_F32v4_Make(x, y, z, w);
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidExponent(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].exponent.constant = v;
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidAttenuation(gX_I32 idx, gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].attenuation.constant = v;
    bg->state.sinusoidDirty = 1;
}

static void TourBackground_SetSinusoidCenter(gX_I32 idx, gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || !TourBackground_SinusoidOk(idx)) return;
    bg->special.sinusoids[idx].center.constant = gX_F32v2_Make(x, y);
    bg->state.sinusoidDirty = 1;
}

// ===========================================================================
// Palette
// ===========================================================================

static void TourBackground_SetPaletteMode(gX_I32 mode) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->palette.mode = mode;
}

static void TourBackground_SetPaletteRowCount(gX_I32 rows) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->palette.rowCount = rows;
}

static void TourBackground_SetPaletteSize(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.size.constant = v;
}

static void TourBackground_SetPaletteAmount(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.amount.constant = v;
}

static void TourBackground_SetPaletteSpeed(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.speed.constant = v;
}

static void TourBackground_SetPaletteFrequency(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.frequency.constant = v;
}

static void TourBackground_SetPaletteColor1(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.color1.constant = gX_F32v4_Make(r, g, b, a);
}

static void TourBackground_SetPaletteColor2(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.color2.constant = gX_F32v4_Make(r, g, b, a);
}

static void TourBackground_SetPaletteColor(gX_I32 idx, gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg || idx < 0 || idx >= gX_BG_PALETTE_COLORS_MAX) return;
    bg->palette.colors[idx].constant = gX_F32v4_Make(r, g, b, a);
    bg->state.colorsDirty = 1;
}

static void TourBackground_SetPaletteBgBoundary(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.bgBoundary.constant = v;
}

static void TourBackground_SetPaletteFgBoundary(gX_F32 v) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.fgBoundary.constant = v;
}

static void TourBackground_SetPaletteBgColor(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.bgColor.constant = gX_F32v4_Make(r, g, b, a);
}

static void TourBackground_SetPaletteFgColor(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->palette.fgColor.constant = gX_F32v4_Make(r, g, b, a);
}

static void TourBackground_SetPaletteBlendMode(gX_I32 mode) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->palette.blendMode = mode;
}

static void TourBackground_SetPaletteColorMath(gX_I32 mode) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->palette.colorMathMode = mode;
}

static void TourBackground_SetPaletteColorMathMask(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->palette.colorMathMask = gX_F32v3_Make(x, y, z);
}

// ===========================================================================
// Misc — boundaries, modulo
// ===========================================================================

static void TourBackground_SetBoundaryXY(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->misc.boundaryXY.constant = gX_F32v4_Make(x, y, z, w);
}

static void TourBackground_SetBoundaryZ(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->misc.boundaryZ.constant = gX_F32v2_Make(x, y);
}

static void TourBackground_SetModulo(gX_F32 x, gX_F32 y) {
    gX_RendererBackground *bg = TourBackground_Params();
    if (bg) bg->misc.modulo.constant = gX_F32v2_Make(x, y);
}

// ===========================================================================
// State toggles + reactions
// ===========================================================================

static void TourBackground_SetParamEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.paramEnabled = enable ? 1 : 0;
}

static void TourBackground_SetWarpEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.warpEnabled = enable ? 1 : 0;
}

static void TourBackground_SetWaveEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.waveEnabled = enable ? 1 : 0;
}

static void TourBackground_SetPaletteEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.paletteEnabled = enable ? 1 : 0;
}

static void TourBackground_SetBoundaryEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.boundaryEnabled = enable ? 1 : 0;
}

static void TourBackground_SetBoundaryXYEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.boundaryXYEnabled = enable ? 1 : 0;
}

static void TourBackground_SetBoundaryZEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.boundaryZEnabled = enable ? 1 : 0;
}

static void TourBackground_SetModuloEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.moduloEnabled = enable ? 1 : 0;
}

static void TourBackground_SetUseTransformMatrix(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.useTransformMatrix = enable ? 1 : 0;
}

static void TourBackground_SetUseDirectMatrix(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.useDirectMatrix = enable ? 1 : 0;
}

static void TourBackground_SetHdmaEnable(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.hdmaEnabled = enable ? 1 : 0;
}

static void TourBackground_SetBattleProjection(gX_I1 enable) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) bg->state.battleProjectionEnabled = enable ? 1 : 0;
}

static void TourBackground_React(gX_I32 type, gX_F32 intensity, gX_F32 duration) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) gX_RendererBackground_React(bg, type, intensity, duration);
}

// ===========================================================================
// Texture
// ===========================================================================

static void TourBackground_SetTexture(const std::string &path) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    snprintf(bg->texture.wrapper.path, sizeof(bg->texture.wrapper.path), "%s", path.c_str());
    bg->texture.wrapper.loadingState.loadAsap = 1;
    bg->texture.wrapper.loadingState.loaded = 0;
}

static gX_I1 TourBackground_IsTextureReady(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->texture.wrapper.loadingState.loaded : 0;
}

// ===========================================================================
// Presets + transition
// ===========================================================================

static void TourBackground_LoadDefault(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    gX_RendererBackground_LoadDefault(bg);
    TourBackground_MarkDirty(bg);
}

static void TourBackground_LoadBuiltIn(gX_I32 index) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    gX_RendererBackground_Preset_BuiltIn(bg, index);
    TourBackground_MarkDirty(bg);
}

static void TourBackground_LoadPreset(const std::string &path) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    gX_RendererBG_Preset_Load_New(bg, path.c_str());
    TourBackground_MarkDirty(bg);
}

static void TourBackground_SavePreset(const std::string &path) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    gX_RendererBG_Preset_Save_New(bg, path.c_str());
}

static void TourBackground_Transition(const std::string &path, gX_F32 durationMs, gX_I32 ease) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (!bg) return;
    gX_F32 seconds = durationMs / 1000.0f;
    if (seconds < 0.001f) seconds = 0.001f;
    gX_RendererBackground_Transition_Start(bg, path.c_str(), seconds, ease);
}

static void TourBackground_TransitionStop(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    if (bg) gX_RendererBackground_Transition_Stop(bg);
}

static gX_I1 TourBackground_IsTransitionActive(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->transition.active : 0;
}

// ===========================================================================
// Read-backs (script-set constants)
// ===========================================================================

static gX_F32 TourBackground_GetScale(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->transform.scale.constant : 0.0f;
}

static gX_F32 TourBackground_GetRotate(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->transform.rotate.constant : 0.0f;
}

static gX_F32 TourBackground_GetMatrixBlend(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->transform.matrixBlend.constant : 0.0f;
}

static gX_I32 TourBackground_GetPaletteMode(void) {
    gX_RendererBackground *bg = TourBackground_Instance();
    return bg ? bg->palette.mode : 0;
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-background registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourBackground_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Transform ---
    r = engine->RegisterGlobalFunction("void Background_SetScale(float)",
        asFUNCTION(TourBackground_SetScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetScaleVel(float)",
        asFUNCTION(TourBackground_SetScaleVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetScaleAccel(float)",
        asFUNCTION(TourBackground_SetScaleAccel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetScaleOrigin(float, float)",
        asFUNCTION(TourBackground_SetScaleOrigin), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetScaleOriginVel(float, float)",
        asFUNCTION(TourBackground_SetScaleOriginVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetRotate(float)",
        asFUNCTION(TourBackground_SetRotate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetRotateVel(float)",
        asFUNCTION(TourBackground_SetRotateVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetRotateAccel(float)",
        asFUNCTION(TourBackground_SetRotateAccel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetRotateOrigin(float, float)",
        asFUNCTION(TourBackground_SetRotateOrigin), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetRotateOriginVel(float, float)",
        asFUNCTION(TourBackground_SetRotateOriginVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetTranslate(float, float)",
        asFUNCTION(TourBackground_SetTranslate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetTranslateVel(float, float)",
        asFUNCTION(TourBackground_SetTranslateVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetTranslateAccel(float, float)",
        asFUNCTION(TourBackground_SetTranslateAccel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetStretch(float, float, float, float)",
        asFUNCTION(TourBackground_SetStretch), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetStretchVel(float, float, float, float)",
        asFUNCTION(TourBackground_SetStretchVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPerspective(float, float)",
        asFUNCTION(TourBackground_SetPerspective), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPerspectiveVel(float, float)",
        asFUNCTION(TourBackground_SetPerspectiveVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetAffine(float, float)",
        asFUNCTION(TourBackground_SetAffine), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetAffineVel(float, float)",
        asFUNCTION(TourBackground_SetAffineVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetMatrixBlend(float)",
        asFUNCTION(TourBackground_SetMatrixBlend), asCALL_CDECL); AS_CHECK(r);

    // --- Direct matrices ---
    r = engine->RegisterGlobalFunction("void Background_SetMatrixA(float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float)",
        asFUNCTION(TourBackground_SetMatrixA), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetMatrixB(float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float)",
        asFUNCTION(TourBackground_SetMatrixB), asCALL_CDECL); AS_CHECK(r);

    // --- Special: warp + wave ---
    r = engine->RegisterGlobalFunction("void Background_SetWarp(float, float, float)",
        asFUNCTION(TourBackground_SetWarp), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWarpVel(float, float, float)",
        asFUNCTION(TourBackground_SetWarpVel), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWarpExp(float)",
        asFUNCTION(TourBackground_SetWarpExp), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveAmplitude(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveAmplitude), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveFrequency(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveFrequency), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveSpeed(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWavePhase(float, float, float, float)",
        asFUNCTION(TourBackground_SetWavePhase), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveRegularwave(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveRegularwave), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveInterlaced(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveInterlaced), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveInterlacedMultiplier(float, float, float, float)",
        asFUNCTION(TourBackground_SetWaveInterlacedMultiplier), asCALL_CDECL); AS_CHECK(r);

    // --- Special: HDMA channels ---
    r = engine->RegisterGlobalFunction("void Background_SetHdmaAmplitude(int, float)",
        asFUNCTION(TourBackground_SetHdmaAmplitude), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetHdmaFrequency(int, float)",
        asFUNCTION(TourBackground_SetHdmaFrequency), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetHdmaSpeed(int, float)",
        asFUNCTION(TourBackground_SetHdmaSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetHdmaPhase(int, float)",
        asFUNCTION(TourBackground_SetHdmaPhase), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetHdmaOffset(int, float, float)",
        asFUNCTION(TourBackground_SetHdmaOffset), asCALL_CDECL); AS_CHECK(r);

    // --- Special: noise ---
    r = engine->RegisterGlobalFunction("void Background_SetNoiseMode(int)",
        asFUNCTION(TourBackground_SetNoiseMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetNoiseScale(float)",
        asFUNCTION(TourBackground_SetNoiseScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetNoiseSpeed(float)",
        asFUNCTION(TourBackground_SetNoiseSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetNoiseOctaves(int)",
        asFUNCTION(TourBackground_SetNoiseOctaves), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetNoiseLacunarity(float)",
        asFUNCTION(TourBackground_SetNoiseLacunarity), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetNoiseGain(float)",
        asFUNCTION(TourBackground_SetNoiseGain), asCALL_CDECL); AS_CHECK(r);

    // --- Sinusoids ---
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidAmplitude(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidAmplitude), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidFrequency(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidFrequency), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidPhase(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidPhase), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidSpeed(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidRegularwave(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidRegularwave), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidInterlaced(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidInterlaced), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidInterlacedMultiplier(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidInterlacedMultiplier), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidDcOffset(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidDcOffset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidLinear(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidLinear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidQuadratic(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetSinusoidQuadratic), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidExponent(int, float)",
        asFUNCTION(TourBackground_SetSinusoidExponent), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidAttenuation(int, float)",
        asFUNCTION(TourBackground_SetSinusoidAttenuation), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetSinusoidCenter(int, float, float)",
        asFUNCTION(TourBackground_SetSinusoidCenter), asCALL_CDECL); AS_CHECK(r);

    // --- Palette ---
    r = engine->RegisterGlobalFunction("void Background_SetPaletteMode(int)",
        asFUNCTION(TourBackground_SetPaletteMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteRowCount(int)",
        asFUNCTION(TourBackground_SetPaletteRowCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteSize(float)",
        asFUNCTION(TourBackground_SetPaletteSize), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteAmount(float)",
        asFUNCTION(TourBackground_SetPaletteAmount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteSpeed(float)",
        asFUNCTION(TourBackground_SetPaletteSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteFrequency(float)",
        asFUNCTION(TourBackground_SetPaletteFrequency), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteColor1(float, float, float, float)",
        asFUNCTION(TourBackground_SetPaletteColor1), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteColor2(float, float, float, float)",
        asFUNCTION(TourBackground_SetPaletteColor2), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteColor(int, float, float, float, float)",
        asFUNCTION(TourBackground_SetPaletteColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteBgBoundary(float)",
        asFUNCTION(TourBackground_SetPaletteBgBoundary), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteFgBoundary(float)",
        asFUNCTION(TourBackground_SetPaletteFgBoundary), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteBgColor(float, float, float, float)",
        asFUNCTION(TourBackground_SetPaletteBgColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteFgColor(float, float, float, float)",
        asFUNCTION(TourBackground_SetPaletteFgColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteBlendMode(int)",
        asFUNCTION(TourBackground_SetPaletteBlendMode), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteColorMath(int)",
        asFUNCTION(TourBackground_SetPaletteColorMath), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteColorMathMask(float, float, float)",
        asFUNCTION(TourBackground_SetPaletteColorMathMask), asCALL_CDECL); AS_CHECK(r);

    // --- Misc ---
    r = engine->RegisterGlobalFunction("void Background_SetBoundaryXY(float, float, float, float)",
        asFUNCTION(TourBackground_SetBoundaryXY), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetBoundaryZ(float, float)",
        asFUNCTION(TourBackground_SetBoundaryZ), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetModulo(float, float)",
        asFUNCTION(TourBackground_SetModulo), asCALL_CDECL); AS_CHECK(r);

    // --- State toggles ---
    r = engine->RegisterGlobalFunction("void Background_SetParamEnable(bool)",
        asFUNCTION(TourBackground_SetParamEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWarpEnable(bool)",
        asFUNCTION(TourBackground_SetWarpEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetWaveEnable(bool)",
        asFUNCTION(TourBackground_SetWaveEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetPaletteEnable(bool)",
        asFUNCTION(TourBackground_SetPaletteEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetBoundaryEnable(bool)",
        asFUNCTION(TourBackground_SetBoundaryEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetBoundaryXYEnable(bool)",
        asFUNCTION(TourBackground_SetBoundaryXYEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetBoundaryZEnable(bool)",
        asFUNCTION(TourBackground_SetBoundaryZEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetModuloEnable(bool)",
        asFUNCTION(TourBackground_SetModuloEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetUseTransformMatrix(bool)",
        asFUNCTION(TourBackground_SetUseTransformMatrix), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetUseDirectMatrix(bool)",
        asFUNCTION(TourBackground_SetUseDirectMatrix), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetHdmaEnable(bool)",
        asFUNCTION(TourBackground_SetHdmaEnable), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SetBattleProjection(bool)",
        asFUNCTION(TourBackground_SetBattleProjection), asCALL_CDECL); AS_CHECK(r);

    // --- Reactions ---
    r = engine->RegisterGlobalFunction("void Background_React(int, float, float)",
        asFUNCTION(TourBackground_React), asCALL_CDECL); AS_CHECK(r);

    // --- Texture ---
    r = engine->RegisterGlobalFunction("void Background_SetTexture(const string &in)",
        asFUNCTION(TourBackground_SetTexture), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Background_IsTextureReady()",
        asFUNCTION(TourBackground_IsTextureReady), asCALL_CDECL); AS_CHECK(r);

    // --- Presets + transition ---
    r = engine->RegisterGlobalFunction("void Background_LoadDefault()",
        asFUNCTION(TourBackground_LoadDefault), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_LoadBuiltIn(int)",
        asFUNCTION(TourBackground_LoadBuiltIn), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_LoadPreset(const string &in)",
        asFUNCTION(TourBackground_LoadPreset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_SavePreset(const string &in)",
        asFUNCTION(TourBackground_SavePreset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_Transition(const string &in, float, int)",
        asFUNCTION(TourBackground_Transition), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Background_TransitionStop()",
        asFUNCTION(TourBackground_TransitionStop), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Background_IsTransitionActive()",
        asFUNCTION(TourBackground_IsTransitionActive), asCALL_CDECL); AS_CHECK(r);

    // --- Read-backs ---
    r = engine->RegisterGlobalFunction("float Background_GetScale()",
        asFUNCTION(TourBackground_GetScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Background_GetRotate()",
        asFUNCTION(TourBackground_GetRotate), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Background_GetMatrixBlend()",
        asFUNCTION(TourBackground_GetMatrixBlend), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Background_GetPaletteMode()",
        asFUNCTION(TourBackground_GetPaletteMode), asCALL_CDECL); AS_CHECK(r);

    // --- Constants ---
    {
        static gX_I32 cTransLinear = GX_BG_TRANS_EASE_LINEAR;
        static gX_I32 cTransIn = GX_BG_TRANS_EASE_IN;
        static gX_I32 cTransOut = GX_BG_TRANS_EASE_OUT;
        static gX_I32 cTransInOut = GX_BG_TRANS_EASE_IN_OUT;
        r = engine->RegisterGlobalProperty("const int BG_TRANS_LINEAR", (void *)&cTransLinear); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_TRANS_IN", (void *)&cTransIn); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_TRANS_OUT", (void *)&cTransOut); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_TRANS_IN_OUT", (void *)&cTransInOut); AS_CHECK(r);

        static gX_I32 cPresetTest = gX_BG_PRESET_TEST;
        static gX_I32 cPresetTitle2 = gX_BG_PRESET_TITLE2;
        static gX_I32 cPresetTitle = gX_BG_PRESET_TITLE;
        static gX_I32 cPresetBattle = gX_BG_PRESET_BATTLE;
        r = engine->RegisterGlobalProperty("const int BG_PRESET_TEST", (void *)&cPresetTest); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_PRESET_TITLE2", (void *)&cPresetTitle2); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_PRESET_TITLE", (void *)&cPresetTitle); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_PRESET_BATTLE", (void *)&cPresetBattle); AS_CHECK(r);

        static gX_I32 cReactNone = GX_BG_REACT_NONE;
        static gX_I32 cReactPulse = GX_BG_REACT_PULSE;
        static gX_I32 cReactShake = GX_BG_REACT_SHAKE;
        static gX_I32 cReactFlash = GX_BG_REACT_FLASH;
        static gX_I32 cReactWarp = GX_BG_REACT_WARP;
        r = engine->RegisterGlobalProperty("const int BG_REACT_NONE", (void *)&cReactNone); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_REACT_PULSE", (void *)&cReactPulse); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_REACT_SHAKE", (void *)&cReactShake); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_REACT_FLASH", (void *)&cReactFlash); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_REACT_WARP", (void *)&cReactWarp); AS_CHECK(r);

        static gX_I32 cNoiseOff = 0;
        static gX_I32 cNoiseSimplex = 1;
        static gX_I32 cNoiseFractal = 2;
        static gX_I32 cNoiseVoronoi = 3;
        static gX_I32 cNoiseTurbulence = 4;
        r = engine->RegisterGlobalProperty("const int BG_NOISE_OFF", (void *)&cNoiseOff); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_NOISE_SIMPLEX", (void *)&cNoiseSimplex); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_NOISE_FRACTAL", (void *)&cNoiseFractal); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_NOISE_VORONOI", (void *)&cNoiseVoronoi); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int BG_NOISE_TURBULENCE", (void *)&cNoiseTurbulence); AS_CHECK(r);
    }

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour background API (params, palette, presets, transition)");
    return 0;
}
