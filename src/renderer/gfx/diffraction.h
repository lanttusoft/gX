/**********************************************************************************************
*
*   [gX: src/renderer/gfx/diffraction.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
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


#ifndef RENDERER_GFX_DIFFRACTION_H
#define RENDERER_GFX_DIFFRACTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//ai: aperture diffraction spikes — the PRISM-1 post quirk. A half-res star
//   map is generated from HDR highlights, then added back to hdrScene before
//   bloom/tone mapping. Every parameter is scene-authored and tunable.
typedef struct gX_RendererGFX_Diffraction {
    gX_F32 threshold;   // HDR luma where spikes start
    gX_F32 knee;        // soft threshold width
    gX_F32 intensity;   // spike brightness
    gX_F32 length;      // spike reach in UV
    gX_F32 chromatic;   // RGB dispersion
    gX_F32 rotation;    // aperture rotation (radians)
    gX_F32 sparkle;     // temporal twinkle amount
    gX_I32 blades;      // 4/6/8/9 aperture blades
    gX_I32 taps;        // samples per blade direction

    gX_RendererGFX_Shader shader;
    gX_RendererGFX_RenderTexture map;

    gX_I32 sceneColorLoc;
    gX_I32 resolutionLoc;
    gX_I32 thresholdLoc;
    gX_I32 kneeLoc;
    gX_I32 intensityLoc;
    gX_I32 lengthLoc;
    gX_I32 chromaticLoc;
    gX_I32 rotationLoc;
    gX_I32 sparkleLoc;
    gX_I32 bladesLoc;
    gX_I32 tapsLoc;
    gX_I32 timeLoc;

    gX_I1 locsCached;
} gX_RendererGFX_Diffraction;

typedef struct gX_RendererGFX gX_RendererGFX;

gX_I0 gX_RendererGFX_Diffraction_Draw(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Diffraction_CacheLocs(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_DIFFRACTION_H
