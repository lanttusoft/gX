/**********************************************************************************************
*
*   [gX: src/renderer/background.h]
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


#ifndef RENDERER_BACKGROUND_H
#define RENDERER_BACKGROUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/text.h"
    
#include "../gxmath/gxmath.h"
    
#include "background/param.h"
#include "background/schema.h"
#include "background/shader.h"
#include "background/struct.h"
#include "background/imgpro.h"
    
//ai: polisher-facing runtime copies of the scalar defaults (see
//   background/polisher.c); the vector defaults stay constants.
extern gX_F32 gX_Bg_ScaleDefval;
extern gX_F32 gX_Bg_RotateDefval;
extern gX_F32 gX_Bg_MatrixblendDefval;
extern gX_F32 gX_Bg_WarpexpDefval;
extern gX_I32 gX_Bg_PalmodeDefval;
extern gX_F32 gX_Bg_PalamountDefval;
extern gX_F32 gX_Bg_PalspeedDefval;
extern gX_F32 gX_Bg_PalfrequencyDefval;
extern gX_F32 gX_Bg_PalbgboundDefval;
extern gX_F32 gX_Bg_PalfgboundDefval;
//ai: register the Background polisher entries (idempotent).
gX_I0 gX_Background_RegisterPolisher(gX_I0);

#define gX_BG_PARAMENABLED_DEFVAL   true

//ai: gX's native backdrop default is 1.0 (gameX's 0.25 was vestigial; the
//   LoadDefault path hardcoded 1.0, so this keeps rendering identical while
//   making the default polisher-tunable).
#define gX_BG_SCALE_DEFVAL          1.0f
#define gX_BG_SCALEORIGIN_DEFVAL    gX_F32v2_Zero()
#define gX_BG_ROTATE_DEFVAL         0.0f
#define gX_BG_ROTATEORIGIN_DEFVAL   gX_F32v2_Zero()
#define gX_BG_STRETCH_DEFVAL        gX_F32v4_Zero()
#define gX_BG_PERSPECTIVE_DEFVAL    gX_F32v2_Zero()
#define gX_BG_TRANSLATE_DEFVAL      gX_F32v2_Zero()
#define gX_BG_AFFINE_DEFVAL         gX_F32v2_Zero()
#define gX_BG_MATRIXBLEND_DEFVAL    0.0f
    
#define gX_BG_WARP_DEFVAL           gX_F32v3_Zero()
#define gX_BG_WARPEXP_DEFVAL        1.0f
#define gX_BG_WAVEAMP_DEFVAL        gX_F32v4_Zero()
#define gX_BG_WAVEFRE_DEFVAL        gX_F32v4_One()
#define gX_BG_WAVERWE_DEFVAL        gX_F32v4_One()
#define gX_BG_WAVEILE_DEFVAL        gX_F32v4_Zero()
#define gX_BG_WAVEILM_DEFVAL        gX_F32v4_MakeValue(2.0f)
#define gX_BG_WAVESPD_DEFVAL        gX_F32v4_Zero()
#define gX_BG_WAVEPHA_DEFVAL        gX_F32v4_Zero()
   
#define gX_BG_PALMODE_DEFVAL        1
#define gX_BG_PALAMOUNT_DEFVAL      16.0f
//ai: default palette size (colors) — matches the colors[256] / 16x16 CGRAM
//   layout the palette modes are written against; decompositions override it.
#define gX_BG_PALSIZE_DEFVAL        256.0f
#define gX_BG_PALSPEED_DEFVAL       0.1f
#define gX_BG_PALFREQUENCY_DEFVAL   1.0f
#define gX_BG_PALCOLOR1_DEFVAL      (gX_F32v4){0.0f,1.0f,0.0f,1.0f}
#define gX_BG_PALCOLOR2_DEFVAL      (gX_F32v4){0.0f,0.0f,1.0f,1.0f}
#define gX_BG_PALBGBOUND_DEFVAL     0.1f
#define gX_BG_PALBGCOLOR_DEFVAL     gX_F32v4_Make(0.0f,0.0f,0.0f,1.0f)
#define gX_BG_PALFGBOUND_DEFVAL     0.9f
#define gX_BG_PALFGCOLOR_DEFVAL     (gX_F32v4){1.0f,1.0f,1.0f,1.0f}
    
#define gX_BG_BOUNDARYXY_DEFVAL     (gX_F32v4){-1.0f,1.0f,-1.0f,1.0f}
#define gX_BG_BOUNDARYZ_DEFVAL      (gX_F32v2){0.0f,10.0f}
#define gX_BG_MODULO_DEFVAL         (gX_F32v2){1.0f,1.0f}
    
    
#define gX_BG_PRESETS \
    X(TEST) \
    X(TITLE2) \
    X(TITLE) \
    X(BATTLE) \
    
enum gX_RendererBackground_Presets {
#define X(preset) gX_BG_PRESET_##preset,
    gX_BG_PRESETS
#undef X
};
const gX_I8 *gX_RendererBackground_Presets_String(gX_I32 preset);

gX_I0 gX_RendererBackground_Load(gX_RendererBackground *bg);
gX_I0 gX_RendererBackground_Unload(gX_RendererBackground *bg);

gX_I0 gX_RendererBackground_LoadClear(gX_RendererBackground *bg);
gX_I0 gX_RendererBackground_LoadDefault(gX_RendererBackground *bg);
gX_I0 gX_RendererBackground_LoadRandom(gX_RendererBackground *bg);
gX_I0 gX_RendererBackground_Preset_BuiltIn(gX_RendererBackground *bg, gX_I32 index);
gX_I0 gX_RendererBackground_Preset_Load(gX_RendererBackground *bg, const gX_I8 *path);
gX_I0 gX_RendererBackground_Preset_Save(gX_RendererBackground *bg, const gX_I8 *path);

gX_I0 gX_RendererBackground_Update(gX_RendererBackground *bg);
gX_I0 gX_RendererBackground_Draw(gX_RendererBackground *bg, gX_RenderTexture *output);

//opencode: game-event reaction types (PULSE=1 SHAKE=2 FLASH=3 WARP=4)
enum {
    GX_BG_REACT_NONE = 0,
    GX_BG_REACT_PULSE,
    GX_BG_REACT_SHAKE,
    GX_BG_REACT_FLASH,
    GX_BG_REACT_WARP
};

//opencode: trigger a reaction — intensity 0..1, duration in seconds
gX_I0 gX_RendererBackground_React(gX_RendererBackground *bg, gX_I32 type, gX_F32 intensity, gX_F32 duration);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_H
