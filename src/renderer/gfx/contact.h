/**********************************************************************************************
*
*   [gX: src/renderer/gfx/contact.h]
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


#ifndef RENDERER_GFX_CONTACT_H
#define RENDERER_GFX_CONTACT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"
#include "texture.h"

//opencode: screen-space contact shadows (spike 1, BeamNG-style short-range
//   sun occlusion). Linear depth march toward the sun over a 0-2u band,
//   occlusion fraction output. Metrology only in spike 1 (no forward
//   integration): signal proven via one-shot stats + later spikes consume
//   the map in standard.frag. Default OFF via GX_T_CONTACT hook.
typedef struct gX_RendererGFX_Contact {
    gX_RendererGFX_RenderTexture map;   // half-res occlusion (1 = lit)
    gX_RendererGFX_Shader genShader;    // march program

    gX_I32 genDepthMapLoc;
    gX_I32 genInvProjLoc;
    gX_I32 genInvViewLoc;
    gX_I32 genViewProjLoc;
    gX_I32 genCamPosLoc;
    gX_I32 genSunDirLoc;
    gX_I32 genResLoc;
    gX_I32 genNearLoc;
    gX_I32 genFarLoc;

    gX_I1 locsCached;
} gX_RendererGFX_Contact;

gX_I0 gX_RendererGFX_Contact_CacheLocs(gX_RendererGFX *gfx);
gX_I0 gX_RendererGFX_Contact_Draw(gX_RendererGFX *gfx);

#ifdef __cplusplus
}
#endif

#endif
