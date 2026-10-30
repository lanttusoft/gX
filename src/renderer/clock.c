/**********************************************************************************************
*
*   [gX: src/renderer/clock.c]
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


//opencode: console-boot clock — crystals, color cycling, orbs, trails, tunnel, fade-in
#include "clock.h"

#include <math.h>
#include <time.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"
#include "../gxmath/matrix.h"
#include "../gxmath/vector3.h"
#include "../main.h"
#include "../resource/resource.h"
#include "../wrappers/wrappers.h"
#include "../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)

#include "renderer.h"

//opencode: color palettes for crystal color cycling (deep blue, purple, teal)
static const gX_F32 PALETTES[3][3] = {
    {0.04f, 0.23f, 0.46f},
    {0.17f, 0.03f, 0.45f},
    {0.03f, 0.39f, 0.45f}
};

// Monotonic smooth time — avoids TIME_UTC jumps from NTP/clock adjustments
static gX_F64 smooth_time(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_F64)ts.tv_sec + (gX_F64)ts.tv_nsec / 1e9;
}

//opencode: compute 3D orbital position for orb index i
static gX_F32v3 clock_orb_position(gX_RendererClock *clock, gX_I32 i) {
    gX_F32 t = clock->orbTimer;
    gX_F32 hourProgress = clock->clockTimer.y / 60.0f;

    gX_F32 sphereRadius = GX_CLOCK_ORB_RADIUS_MIN + hourProgress * (GX_CLOCK_ORB_RADIUS_MAX - GX_CLOCK_ORB_RADIUS_MIN);

    gX_F32 speed = (gX_F32)(i + 1) * GX_CLOCK_ORB_SPEED_BASE;
    gX_F32 phaseOffset = (gX_F32)i * GX_CLOCK_ORB_PHASE_OFFSET;

    // LCD grouping — at seconds that divide 60, orbs converge into N groups
    gX_I32 secVal = (gX_I32)clock->clockTimer.z;
    gX_I32 groups = 0;
    if (secVal == 0) {
        groups = 1;
    } else if (60 % secVal == 0) {
        groups = 60 / secVal;
    }

    gX_F32 angle;
    if (groups > 0 && (gX_I32)groups <= GX_CLOCK_ORB_COUNT) {
        gX_F32 groupAngle = (gX_F32)(i % groups) * (2.0f * gX_PI) / (gX_F32)groups;
        gX_F32 naturalAngle = (2.0f * gX_PI * t / 60.0f) * speed + phaseOffset;
        // Blend toward group: strongest at exact group second, falloff within ~0.5s
        gX_F32 distToGroup = fmod(t, 60.0f / (gX_F32)groups);
        gX_F32 blend = fmaxf(0.0f, 1.0f - distToGroup * 4.0f);
        angle = naturalAngle + (groupAngle - naturalAngle) * blend;
    } else {
        angle = (2.0f * gX_PI * t / 60.0f) * speed + phaseOffset;
    }

    gX_F32 x = sphereRadius * cosf(angle);
    gX_F32 z = sphereRadius * sinf(angle);
    gX_F32 y = GX_CLOCK_ORB_VERT_AMP * sinf(angle * GX_CLOCK_ORB_VERT_FREQ + (gX_F32)i);

    return (gX_F32v3){x, y, z};
}

static gX_I0 clock_gpu_load(gX_I0 *arg) {
    gX_RendererClock *clock = (gX_RendererClock*)arg;

    // Crystal cylinder (6-sided = hexagonal prism)
    gX_Mesh cylinder = gX_Mesh_GenCylinder(GX_CLOCK_CYLINDER_RADIUS, GX_CLOCK_CYLINDER_HEIGHT, GX_CLOCK_CYLINDER_SIDES);
    clock->cylinderModel = gX_Model_LoadMesh(cylinder);
    { gX_Shader *_rsc = gX_Resource_LoadShader("simple2.vert", "simple2.frag"); if (_rsc) clock->cylinderShader = *_rsc; }
    clock->shader_loc_res          = gX_Shader_GetLocation(clock->cylinderShader, "res");
    clock->shader_loc_replacecolor = gX_Shader_GetLocation(clock->cylinderShader, "replaceColor");
    gX_Model_AttachShader(&clock->cylinderModel, clock->cylinderShader);

    // Orb sprite texture
    { gX_Texture *_rsct = gX_Resource_LoadTexture("orb.png"); if (_rsct) clock->orb = *_rsct; }

    clock->loadingstate.loaded = true;
}

static gX_I0 clock_gpu_unload(gX_I0 *arg) {
    gX_RendererClock *clock = (gX_RendererClock*)arg;
    if (!clock->loadingstate.loaded) return;
    gX_Model_Unload(clock->cylinderModel);
    //opencode: shader + orb texture are RM-managed — cleanup handled by resource manager
    clock->loadingstate.loaded = false;
}

gX_I32 gX_RendererClock_Load(gX_RendererClock *clock) {
    gX_TRACELOG_PROLOGUE("RendererClock_Load","RENDERER_CLOCK: Loading")
    strcpy(clock->name, "[gX] Clock");
    strcpy(clock->description, "Console-boot-style clock");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", clock->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", clock->description);

    // Phase 1 — console-boot camera: straight-on view, centered, 70° FOV
    clock->camera.position = (gX_F32v3){0.0f, 0.0f, GX_CLOCK_CAMERA_DISTANCE};
    clock->camera.target   = (gX_F32v3){0.0f, 0.0f, 0.0f};
    clock->camera.up       = (gX_F32v3){0.0f, 1.0f, 0.0f};
    clock->camera.fovy     = GX_CLOCK_CAMERA_FOV;
    clock->camera.projection = gX_CAMERA_PERSPECTIVE;

    clock->clockTimer = (gX_F32v3){0.0f, 0.0f, 0.0f};
    clock->orbTimer   = 0.0f;
    clock->paletteColor = (gX_F32v3){0.04f, 0.23f, 0.46f};

    clock->loadingstate = (gX_LoadingState){0};
    clock->loadingstate.loadAsap = true;

    // Phase 8 — record startup time for fade-in
    clock->startTime = smooth_time();
    clock->fadeInDone = false;

    // Reset orb trails
    for (gX_I32 i = 0; i < GX_CLOCK_ORB_COUNT; i++) {
        clock->orbTrailCount[i] = 0;
    }

    gX_TRACELOG_EPILOGUE("RENDERER_CLOCK: Load done")
    return 0;
}

gX_I32 gX_RendererClock_Unload(gX_RendererClock *clock) {
    if (clock->loadingstate.loaded) clock_gpu_unload(clock);
    return 0;
}

gX_I0 gX_RendererClock_Update(gX_RendererClock *clock) {
    gX_LoadingState_Handler(&clock->loadingstate, clock_gpu_load, clock_gpu_unload, clock);
    if (!clock->loadingstate.loaded) return;

    // Smooth monotonic time
    gX_F64 t = smooth_time();
    gX_F64 totalSec = t;

    // orbTimer: continuous seconds (0-60 loop)
    clock->orbTimer = (gX_F32)fmod(totalSec, 60.0);

    // clockTimer: hours, minutes, seconds
    clock->clockTimer.z = (gX_F32)fmod(totalSec, 60.0);
    clock->clockTimer.y = (gX_F32)fmod(floor(totalSec / 60.0), 60.0);
    clock->clockTimer.x = (gX_F32)fmod(floor(totalSec / 3600.0), 24.0);

    // Build rotation matrix from clock hands
    gX_F32 rot[3] = {
        -clock->clockTimer.z * (2.0f * gX_PI) / 60.0f,
        0.0f,
        (-clock->clockTimer.x * (4.0f * gX_PI) + 3.0f * 4.0f * gX_PI) / 24.0f
    };
    clock->clockMatrix = gX_Matrix_RotateZYX(*(gX_F32v3*)rot);

    // Phase 3 — Color cycling: 3 palettes, 30s each, smooth lerp
    gX_F32 cycleTime = fmod(totalSec, GX_CLOCK_PALETTE_CYCLE);
    gX_I32 palIdx = (gX_I32)(cycleTime / GX_CLOCK_PALETTE_INTERVAL);
    gX_F32 palFrac = fmod(cycleTime, GX_CLOCK_PALETTE_INTERVAL) / GX_CLOCK_PALETTE_INTERVAL;
    gX_I32 palNext = (palIdx + 1) % GX_CLOCK_PALETTE_COUNT;
    const gX_F32 *p0 = PALETTES[palIdx];
    const gX_F32 *p1 = PALETTES[palNext];
    clock->paletteColor.x = p0[0] + (p1[0] - p0[0]) * palFrac;
    clock->paletteColor.y = p0[1] + (p1[1] - p0[1]) * palFrac;
    clock->paletteColor.z = p0[2] + (p1[2] - p0[2]) * palFrac;

    // Phase 4 — Compute 3D orb positions for this frame
    for (gX_I32 i = 0; i < GX_CLOCK_ORB_COUNT; i++) {
        clock->orbPositions3D[i] = clock_orb_position(clock, i);
    }

}


static void draw_tunnel(gX_RendererClock *clock) {
    // Dark purple cylinder — matches the console-boot tunnel background
    gX_Cylinder_Draw((gX_Cylinder){
        .posStart = {0.0f, 0.0f, GX_CLOCK_TUNNEL_Z_NEAR},
        .posEnd   = {0.0f, 0.0f, GX_CLOCK_TUNNEL_Z_FAR},
        .radius   = {GX_CLOCK_TUNNEL_RADIUS, GX_CLOCK_TUNNEL_RADIUS},
        .sides    = GX_CLOCK_TUNNEL_SIDES
    }, (gX_Color){72, 48, 110, 200});
}

static void draw_crystals(gX_RendererClock *clock) {
    gX_I32 amount = GX_CLOCK_CRYSTAL_AMOUNT;
    for (gX_I32 i = 0; i < amount + 1; i++) {
        gX_Matrix r = gX_Matrix_RotateY(clock->clockTimer.z / gX_PI);
        gX_F32v3 angle = { 0.0f, 0.0f, -0.5f * gX_PI + 2.0f * gX_PI / (gX_F32)amount * (gX_F32)i };
        gX_Matrix rotation = gX_Matrix_RotateXYZ(angle);
        gX_Matrix rr = gX_Matrix_Multiply(r, rotation);
        clock->cylinderModel.transform = gX_Matrix_Multiply(rr, clock->clockMatrix);

        gX_F32v3 col = clock->paletteColor;
        gX_F32 posScale = GX_CLOCK_CRYSTAL_POSSCALE;
        if (i == 0) {
            // Hour crystal — white, height scales with minutes
            col = (gX_F32v3){1.0f, 1.0f, 1.0f};
            gX_Matrix scale = gX_Matrix_Scale(1.0f, clock->clockTimer.y / 60.0f, 1.0f);
            clock->cylinderModel.transform = gX_Matrix_Multiply(scale, clock->cylinderModel.transform);
        }
        if (i == amount) {
            // Minute counter — darker shade, inverse scale, opposite side
            col = (gX_F32v3){0.25f, 0.25f, 1.0f};
            gX_Matrix scale = gX_Matrix_Scale(1.0f, 1.0f - clock->clockTimer.y / 60.0f, 1.0f);
            gX_Matrix ro = gX_Matrix_RotateX(gX_PI);
            gX_Matrix sr = gX_Matrix_Multiply(scale, ro);
            clock->cylinderModel.transform = gX_Matrix_Multiply(sr, clock->cylinderModel.transform);
            posScale += GX_CLOCK_CRYSTAL_EXTRASCALE;
        }

        // Activate shader BEFORE setting per-crystal uniform (critical!)
        gX_Drawing_BeginShader(clock->cylinderShader);
        gX_Shader_SetValue_V3(clock->cylinderShader, clock->shader_loc_replacecolor, col);
        gX_Drawing_EndShader();

        gX_F32 x = posScale * cosf((2.0f * gX_PI / (gX_F32)amount) * (gX_F32)i);
        gX_F32 y = posScale * sinf((2.0f * gX_PI / (gX_F32)amount) * (gX_F32)i);
        gX_F32v3 crystalPos = { x, y, 0.0f };
        crystalPos = gX_F32v3_Transform(crystalPos, clock->clockMatrix);

        gX_Model_Parameters params = { 0 };
        params.position = crystalPos;
        params.scale = 1.0f;
        params.color = gX_WHITE;
        gX_Model_Draw_Parameters(clock->cylinderModel, params);
    }
}

static void draw_orb_trails(gX_RendererClock *clock) {
    // Draw fading cyan trail lines (additive blend)
    for (gX_I32 i = 0; i < GX_CLOCK_ORB_COUNT; i++) {
        if (clock->orbTrailCount[i] < 2) continue;
        for (gX_I32 t = 1; t < clock->orbTrailCount[i]; t++) {
            gX_F32 age = (gX_F32)t / (gX_F32)clock->orbTrailCount[i];
            gX_U8 alpha = (gX_U8)((1.0f - age) * (gX_F32)GX_CLOCK_TRAIL_ALPHA_MAX);
            gX_Line_Draw((gX_Line){
                .start = clock->orbTrailHistory[i][t - 1],
                .end   = clock->orbTrailHistory[i][t],
                .thickness = GX_CLOCK_TRAIL_THICKNESS
            }, (gX_Color){133, 255, 255, alpha});
        }
    }
}

static void draw_orbs(gX_RendererClock *clock) {
    // Project 3D orb positions to screen, update trails, draw sprites
    gX_F32v2 orbScreenPos[GX_CLOCK_ORB_COUNT];

    for (gX_I32 i = 0; i < GX_CLOCK_ORB_COUNT; i++) {
        gX_F32v3 worldPos = gX_F32v3_Transform(clock->orbPositions3D[i], clock->clockMatrix);
        orbScreenPos[i] = gX_Camera_GetWorldToScreen(worldPos, clock->camera);

        // Update trail history (circular-ish: shift and append at end)
        gX_I32 count = clock->orbTrailCount[i];
        if (count < GX_CLOCK_TRAIL_LEN) {
            clock->orbTrailHistory[i][count] = orbScreenPos[i];
            clock->orbTrailCount[i]++;
        } else {
            // Shift oldest out, append newest
            for (gX_I32 t = 0; t < GX_CLOCK_TRAIL_LEN - 1; t++) {
                clock->orbTrailHistory[i][t] = clock->orbTrailHistory[i][t + 1];
            }
            clock->orbTrailHistory[i][GX_CLOCK_TRAIL_LEN - 1] = orbScreenPos[i];
        }
    }

    // Draw trails first (additive blend)
    gX_Drawing_BeginBlendCustomSeparate(gX_BLEND_ONE, gX_BLEND_ONE, gX_BLEND_ONE, gX_BLEND_ONE);
    draw_orb_trails(clock);
    gX_Drawing_EndBlend();

    // Draw orb sprites (additive blend)
    gX_Drawing_BeginBlendCustomSeparate(gX_BLEND_ONE, gX_BLEND_ONE, gX_BLEND_ONE, gX_BLEND_ONE);
    for (gX_I32 i = 0; i < GX_CLOCK_ORB_COUNT; i++) {
        gX_Texture_Draw_Sprite(clock->orb, orbScreenPos[i], (gX_F32v2){GX_CLOCK_ORB_SPRITE_SIZE, GX_CLOCK_ORB_SPRITE_SIZE}, 0.0f);
    }
    gX_Drawing_EndBlend();
}

gX_I0 gX_RendererClock_DrawTo(gX_RendererClock *clock, gX_RenderTexture *outputRT) {
    if (!clock->loadingstate.loaded) return;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});

        // Phase 2 — Upload screen resolution to shader
        gX_Drawing_BeginShader(clock->cylinderShader);
        gX_Shader_SetValue_V2(clock->cylinderShader, clock->shader_loc_res,
            (gX_F32v2){(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight});
        gX_Drawing_EndShader();

        gX_Drawing_Begin3D(clock->camera);
            gX_RHI_Disable(gX_RHI_CULL_FACE);

            // Phase 7 — Wormhole tunnel (behind crystals)
            draw_tunnel(clock);

            // Phase 3 — Crystals with color cycling
            draw_crystals(clock);

            gX_RHI_Enable(gX_RHI_CULL_FACE);
        gX_Drawing_End3D();

        // Phase 5+6 — Orb sprites and trails (2D overlay, additive blend)
        draw_orbs(clock);

        // Phase 8 — Startup fade-in (4 seconds)
        if (!clock->fadeInDone) {
            gX_F64 elapsed = smooth_time() - clock->startTime;
            gX_F32 alpha = (gX_F32)fmin(elapsed / GX_CLOCK_FADE_DURATION, 1.0);
            if (alpha >= 1.0f) {
                clock->fadeInDone = true;
            } else {
                gX_U8 fadeAlpha = (gX_U8)((1.0f - alpha) * 255.0f);
                gX_Rectangle_Draw((gX_Rectangle){
                    0.0f, 0.0f,
                    (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight
                }, (gX_Color){0, 0, 0, fadeAlpha});
            }
        }

    gX_Drawing_EndTexture();
}
