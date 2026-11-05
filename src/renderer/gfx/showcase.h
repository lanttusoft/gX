/**********************************************************************************************
*
*   [gX: src/renderer/gfx/showcase.h]
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


#ifndef RENDERER_GFX_SHOWCASE_H
#define RENDERER_GFX_SHOWCASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../gfx.h"

//ai: serializable orbit-camera state for console save slots.
typedef struct gX_Showcase_CameraState {
    gX_F32   yaw;
    gX_F32   pitch;
    gX_F32   dist;
    gX_F32v3 target;
} gX_Showcase_CameraState;

//opencode: interactive GFX demo (--showcase) — orbit camera, effect hotkeys,
//   model focus, and a HUD legend. Pure scene controller; no new state.
gX_I0 gX_Showcase_Update(gX_RendererGFX *gfx);
gX_I0 gX_Showcase_CameraGet(gX_Showcase_CameraState *out);
gX_I0 gX_Showcase_CameraSet(const gX_Showcase_CameraState *in);
gX_I0 gX_Showcase_ResetDefaults(gX_I0);
gX_I0 gX_Showcase_DrawHUD(gX_RendererGFX *gfx, gX_RenderTexture *overlayTgt);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_SHOWCASE_H
