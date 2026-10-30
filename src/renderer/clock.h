/**********************************************************************************************
*
*   [gX: src/renderer/clock.h]
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


#ifndef RENDERER_CLOCK_H
#define RENDERER_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/types/state.h"

//opencode: console-boot clock — crystals, orbs, wormhole tunnel, color cycling, orb trails
#define GX_CLOCK_ORB_COUNT   7
#define GX_CLOCK_TRAIL_LEN   60

//opencode: camera constants
#define GX_CLOCK_CAMERA_DISTANCE  30.0f
#define GX_CLOCK_CAMERA_FOV       70.0f

//opencode: orb motion constants
#define GX_CLOCK_ORB_RADIUS_MIN   3.0f
#define GX_CLOCK_ORB_RADIUS_MAX   6.0f
#define GX_CLOCK_ORB_SPEED_BASE   0.3f
#define GX_CLOCK_ORB_PHASE_OFFSET 0.9f
#define GX_CLOCK_ORB_VERT_AMP     1.5f
#define GX_CLOCK_ORB_VERT_FREQ    1.3f
#define GX_CLOCK_ORB_SPRITE_SIZE  32.0f

//opencode: orb trail visual
#define GX_CLOCK_TRAIL_ALPHA_MAX  200
#define GX_CLOCK_TRAIL_THICKNESS  2.0f

//opencode: tunnel cylinder
#define GX_CLOCK_TUNNEL_Z_NEAR   -5.0f
#define GX_CLOCK_TUNNEL_Z_FAR    -95.0f
#define GX_CLOCK_TUNNEL_RADIUS   22.0f
#define GX_CLOCK_TUNNEL_SIDES    32

//opencode: crystal constants
#define GX_CLOCK_CYLINDER_RADIUS    1.0f
#define GX_CLOCK_CYLINDER_HEIGHT    8.0f
#define GX_CLOCK_CYLINDER_SIDES     6
#define GX_CLOCK_CRYSTAL_AMOUNT     12
#define GX_CLOCK_CRYSTAL_POSSCALE    5.0f
#define GX_CLOCK_CRYSTAL_EXTRASCALE  8.0f

//opencode: palette cycling (3 palettes, 30s each = 90s cycle)
#define GX_CLOCK_PALETTE_COUNT      3
#define GX_CLOCK_PALETTE_CYCLE      90.0f
#define GX_CLOCK_PALETTE_INTERVAL   30.0f

//opencode: startup fade-in
#define GX_CLOCK_FADE_DURATION      4.0f

typedef struct gX_RendererClock {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_LoadingState loadingstate;
    gX_U32 state;
    
    gX_Camera3D camera;
    gX_Model cylinderModel;
    gX_Shader cylinderShader;
    gX_I32 shader_loc_res;
    gX_I32 shader_loc_replacecolor;
    
    gX_F32v3 clockTimer;
    gX_F32    orbTimer;
    gX_Matrix clockMatrix;
    
    // Orb sprite texture (loaded from orb.png)
    gX_Texture orb;
    
    // Color cycling — lerped palette color from 3 palettes (30s each)
    gX_F32v3 paletteColor;
    
    // Orb 3D positions — computed in Update, used in Draw for trails + sprites
    gX_F32v3 orbPositions3D[GX_CLOCK_ORB_COUNT];
    
    // Orb trail screen-space history (circular buffer)
    gX_F32v2 orbTrailHistory[GX_CLOCK_ORB_COUNT][GX_CLOCK_TRAIL_LEN];
    gX_I32   orbTrailCount[GX_CLOCK_ORB_COUNT];
    
    // Startup fade-in
    gX_F64 startTime;
    gX_I1  fadeInDone;
    
} gX_RendererClock;

gX_I32 gX_RendererClock_Load(gX_RendererClock *clock);
gX_I32 gX_RendererClock_Unload(gX_RendererClock *clock);

gX_I0 gX_RendererClock_Update(gX_RendererClock *clock);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas)
gX_I0 gX_RendererClock_DrawTo(gX_RendererClock *clock, gX_RenderTexture *outputRT);
#define gX_RendererClock_Draw(clock) gX_RendererClock_DrawTo((clock), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_CLOCK_H
