/**********************************************************************************************
*
*   [gX: src/renderer/gfx/viewport_grid.c]
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


//opencode: phase-1 GUI x GFX viewport dashboard. The four tiles share the
//   loaded showcase scene and only scope camera + final output target. This
//   keeps scene ownership singular until viewport-local scene state exists.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "viewport_grid.h"
#include "../gfx.h"

#include "../../camera.h"
#include "../../common/log.h"
#include "../../main.h"
#include "../../wrappers/wrappers.h"
#include "../../gxmath/constants.h"

//opencode: four cardinal-ish orbit offsets retain the user's target/distance
//   while making each shared-scene view visibly independent.
static const gX_F32 s_viewportGridYaw[GX_GFX_VIEWPORT_GRID_COUNT] = {
    -42.0f, 42.0f, 138.0f, -138.0f
};

gX_I1 gX_RendererGFX_ViewportGrid_Enabled(gX_I0)
{
    const gX_I8 *value = getenv("GX_GFX_GRID");
    return (arguments.showcase && !arguments.disableModuleGUI
        && value && atoi(value) != 0) ? 1 : 0;
}

gX_I0 gX_RendererGFX_ViewportGrid_Init(gX_RendererGFX *gfx)
{
    if (!gfx) return;
    memset(&gfx->viewportGrid, 0, sizeof(gfx->viewportGrid));
    strcpy(gfx->viewportGrid.name, "GFX viewport grid");
    strcpy(gfx->viewportGrid.description,
        "Shared showcase scene rendered through four scoped cameras");
}

gX_I0 gX_RendererGFX_ViewportGrid_Unload(gX_RendererGFX *gfx)
{
    if (!gfx) return;
    for (gX_U32 i = 0; i < GX_GFX_VIEWPORT_GRID_COUNT; i++) {
        if (gfx->viewportGrid.targets[i].id > 0)
            gX_RenderTexture_Unload(gfx->viewportGrid.targets[i]);
    }
    memset(gfx->viewportGrid.targets, 0, sizeof(gfx->viewportGrid.targets));
    gfx->viewportGrid.width = 0;
    gfx->viewportGrid.height = 0;
    gfx->viewportGrid.loaded = 0;
    gfx->viewportGrid.rendering = 0;
}

//opencode: allocate on demand and recreate atomically on a render-resolution
//   change. GUI texture elements retain stable addresses inside the GFX heap.
static gX_I1 gX_RendererGFX_ViewportGrid_EnsureTargets(gX_RendererGFX *gfx)
{
    gX_U32 width = (gX_U32)maindata.renderWidth / 2u;
    gX_U32 height = (gX_U32)maindata.renderHeight / 2u;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    if (gfx->viewportGrid.loaded && gfx->viewportGrid.width == width
        && gfx->viewportGrid.height == height) return 1;

    gX_RendererGFX_ViewportGrid_Unload(gfx);
    gfx->viewportGrid.width = width;
    gfx->viewportGrid.height = height;
    for (gX_U32 i = 0; i < GX_GFX_VIEWPORT_GRID_COUNT; i++) {
        gfx->viewportGrid.targets[i] = gX_RenderTexture_Load_HDR(width, height);
        if (gfx->viewportGrid.targets[i].id == 0) {
            gX_TraceLog_GFX(gX_LOG_ERROR,
                "VIEWPORT_GRID: target %u allocation failed", i);
            gX_RendererGFX_ViewportGrid_Unload(gfx);
            return 0;
        }
    }
    gfx->viewportGrid.loaded = 1;
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "VIEWPORT_GRID: targets=4 size=%ux%u shared-scene four-camera mode",
        width, height);
    return 1;
}

static gX_Camera3D gX_RendererGFX_ViewportGrid_Camera_FromBase(
    gX_Camera3D base, gX_F32 yawOffset)
{
    gX_F32 dx = base.position.x - base.target.x;
    gX_F32 dy = base.position.y - base.target.y;
    gX_F32 dz = base.position.z - base.target.z;
    gX_F32 horizontal = sqrtf(dx * dx + dz * dz);
    gX_F32 distance = sqrtf(horizontal * horizontal + dy * dy);
    gX_F32 yaw;
    gX_F32 pitch;
    if (distance < 0.001f) {
        horizontal = 8.0f;
        distance = 8.0f;
        dy = 0.0f;
        dx = 0.0f;
        dz = horizontal;
    }
    yaw = atan2f(dx, dz) + yawOffset * gX_DEG2RAD;
    pitch = atan2f(dy, horizontal);
    base.position.x = base.target.x + distance * cosf(pitch) * sinf(yaw);
    base.position.y = base.target.y + distance * sinf(pitch);
    base.position.z = base.target.z + distance * cosf(pitch) * cosf(yaw);
    return base;
}

//opencode: phase-1 excludes state that owns one temporal/screen-space history
//   or depends on a previous viewport's light grid. The saved showcase state is
//   restored after the four draws, so grid mode never changes normal controls.
static gX_I0 gX_RendererGFX_ViewportGrid_ApplySafeState(gX_RendererGFX *gfx)
{
    gfx->state.ENABLE_REFLECTIONS = 0;
    gfx->state.ENABLE_PLANAR_REFLECTIONS = 0;
    gfx->state.ENABLE_SSR = 0;
    gfx->state.ENABLE_SSR_AUXVIEW = 0;
    gfx->state.ENABLE_SSAO = 0;
    gfx->state.ENABLE_BLOOM = 0;
    gfx->state.ENABLE_FOG = 0;
    gfx->state.ENABLE_GODRAYS = 0;
    gfx->state.ENABLE_VOLUME_FOG = 0;
    gfx->state.ENABLE_LENS_FLARE = 0;
    gfx->state.ENABLE_PARTICLES = 0;
    gfx->state.ENABLE_MOTION_BLUR = 0;
    gfx->state.ENABLE_DOF = 0;
    gfx->state.ENABLE_TAA = 0;
    gfx->state.ENABLE_CLUSTERED = 0;
    gfx->state.SHOW_SSR_VISUALIZER = 0;
}

gX_I0 gX_RendererGFX_ViewportGrid_Draw(gX_RendererGFX *gfx)
{
    gX_Camera3D savedCamera;
    gX_RendererGFX_State savedState;
    gX_Matrix savedMotionBlurVP;
    gX_Matrix savedTaaVP;
    gX_RenderTexture *savedCompositeTarget;
    gX_RenderTexture *savedSecondTarget;
    if (!gfx || !gX_RendererGFX_ViewportGrid_Enabled()) return;
    if (!gX_RendererGFX_ViewportGrid_EnsureTargets(gfx)) return;

    savedCamera = camera.camera3D;
    savedState = gfx->state;
    savedMotionBlurVP = gfx->motionBlurPrevVP;
    savedTaaVP = gfx->taaPrevVP;
    savedCompositeTarget = gfx->common.compositeTarget;
    savedSecondTarget = gfx->second.rt;
    gfx->viewportGrid.rendering = 1;
    gX_RendererGFX_ViewportGrid_ApplySafeState(gfx);
    for (gX_U32 i = 0; i < GX_GFX_VIEWPORT_GRID_COUNT; i++) {
        camera.camera3D = gX_RendererGFX_ViewportGrid_Camera_FromBase(
            savedCamera, s_viewportGridYaw[i]);
        gX_RendererGFX_DrawTo(gfx, &gfx->viewportGrid.targets[i]);
    }
    camera.camera3D = savedCamera;
    gfx->state = savedState;
    gfx->motionBlurPrevVP = savedMotionBlurVP;
    gfx->taaPrevVP = savedTaaVP;
    gfx->common.compositeTarget = savedCompositeTarget;
    gfx->second.rt = savedSecondTarget;
    gfx->viewportGrid.rendering = 0;
}

gX_Texture *gX_RendererGFX_ViewportGrid_GetTexture(gX_RendererGFX *gfx,
    gX_U32 index)
{
    if (!gfx || index >= GX_GFX_VIEWPORT_GRID_COUNT) return NULL;
    return &gfx->viewportGrid.targets[index].texture;
}
