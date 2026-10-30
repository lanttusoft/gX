/**********************************************************************************************
*
*   [gX: src/renderer/gfx/motionblur.h]
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


#ifndef RENDERER_GFX_MOTIONBLUR_H
#define RENDERER_GFX_MOTIONBLUR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: screen-space motion blur — velocity from depth, post-process blur
typedef struct gX_RendererGFX_MotionBlur {
    gX_I1   enable;
    gX_F32  strength;
    gX_I32  maxSamples;

    gX_RendererGFX_Shader shader;
    gX_RendererGFX_RenderTexture map;

    gX_I32  colorMapLoc;
    gX_I32  depthMapLoc;
    gX_I32  prevVPLoc;
    gX_I32  invVPLoc;
    gX_I32  resolutionLoc;
    gX_I32  samplesLoc;
    gX_I32  strengthLoc;
    //opencode: per-object motion — velocity-buffer tap (same pattern as
    //   the TAA resolve: buffer when rendered this frame, depth-reproject
    //   fallback otherwise; glass pixels fall back too).
    gX_I32  velocityMapLoc;
    gX_I32  hasVelocityLoc;
    gX_I1   locsCached;
} gX_RendererGFX_MotionBlur;

typedef struct gX_RendererGFX gX_RendererGFX;

//opencode: motion blur draw (locs cached live in update.c)
gX_I0 gX_RendererGFX_MotionBlur_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_MOTIONBLUR_H
