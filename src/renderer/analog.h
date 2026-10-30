/**********************************************************************************************
*
*   [gX: src/renderer/analog.h]
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


#ifndef RENDERER_ANALOG_H
#define RENDERER_ANALOG_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: analog CRT renderer — 3 mutually exclusive modes:
//   NTSC_PAL : CPU EMMIR modulate/demodulate (threaded)
//   LEGACY : GPU analog.frag shader
//   VHS : GPU vhs.frag with tape/machine/format components

#include "../common/types.h"
#include "../common/types/state.h"
#include "../common/threading.h"
#include "../wrappers/texture.h"
#include "../wrappers/shader.h"
#include "../wrappers/image.h"

#include "analog/ntsc_pal.h"
#include "analog/analog_shader.h"
#include "analog/vhs/vhs.h"

#define GX_ANALOG_MODE_NTSC_PAL  0
#define GX_ANALOG_MODE_LEGACY    1
#define GX_ANALOG_MODE_VHS       2

//opencode: restructured for 3-system architecture
typedef struct gX_RendererAnalog {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_LoadingState loadingstate;

    //opencode: active mode selector
    gX_I32 mode; // GX_ANALOG_MODE_*

    //ai: live Reduce Motion state — freeze time-based glitch/noise and the
    //   NTSC interlace field while preserving the static CRT look.
    gX_I1 reducedMotion;

    //opencode: input texture (any source, default = testcard)
    gX_Texture inputTexture;
    gX_I1      hasInputTexture; // true after SetInput call

    //opencode: deferred GPU load tracking for shaders
    gX_I1 legacyShaderLoaded;
    gX_I1 vhsShaderLoaded;

    //opencode: CPU NTSC/PAL path
    gX_Analog_NTSCPAL ntscPal;

    //opencode: legacy analog.frag GPU path
    gX_Shader          legacyShader;
    gX_Analog_LegacyShader legacyShaderState;

    //opencode: VHS GPU path
    gX_Shader      vhsShader;
    gX_Analog_VHS  vhsState;

    //opencode: CPU-side input image (for NTSC/PAL path that needs pixel data)
    gX_Image inputImg;
} gX_RendererAnalog;

gX_I32 gX_RendererAnalog_Load(gX_RendererAnalog *analog);
gX_I32 gX_RendererAnalog_Unload(gX_RendererAnalog *analog);

gX_I0 gX_RendererAnalog_Update(gX_RendererAnalog *analog);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas). The
//   compositor routes layer content to its intermediate RTs via this.
gX_I0 gX_RendererAnalog_DrawTo(gX_RendererAnalog *analog, gX_RenderTexture *outputRT);
//opencode: backward-compatible alias — module macro calls this (draws to canvas)
#define gX_RendererAnalog_Draw(analog) gX_RendererAnalog_DrawTo((analog), NULL)

//opencode: set input texture (overrides default testcard). Pass {0} to reset.
gX_I0 gX_RendererAnalog_SetInput(gX_RendererAnalog *analog, gX_Texture texture);

//opencode: set active mode
gX_I0 gX_RendererAnalog_SetMode(gX_RendererAnalog *analog, gX_I32 mode);

//opencode: get mode string for debug/ImGui display
const gX_I8 *gX_RendererAnalog_ModeString(gX_I32 mode);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_ANALOG_H
