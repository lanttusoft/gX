/**********************************************************************************************
*
*   [gX: src/module/devcube.c]
*   DevCube — the engine avatar (green wireframe cube, pink 3:2 Lissajous, RGB orbs)
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


//ai: lab module (docs/dynamic_modules.md) — loaded only with --module or
//    GX_MODULE, built under build/labs/ so the host never auto-loads it. It
//    draws a native-resolution overlay after the final scene/renderer/GUI
//    frame and advances from CLOCK_MONOTONIC at a 60 Hz phase, never from the
//    game's dt, render scale, or connection-quality lag.
#include "module.h"
#include "devcube_phase.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "../common/log.h"
#include "../main.h"
#include "../rhi/gl_state.h"
#include "../rhi/rhi_enums.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi_state.h"
#include "../wrappers/drawing.h"
#include "../wrappers/shape.h"

#define GX_DEVCUBE_PI       3.14159265358979323846
#define GX_DEVCUBE_SEGMENTS 160

typedef struct gX_DevCube_State {
    gX_I64 startNs;
    gX_I64 phaseTicks;
    gX_F64 phase;
    gX_I1  active;
} gX_DevCube_State;

typedef struct gX_DevCube_Projected {
    gX_F32v2 p;
    gX_F32   depth;
    gX_I1    visible;
} gX_DevCube_Projected;

typedef struct gX_DevCube_Orb {
    gX_F32v2 p;
    gX_F32   radius;
    gX_F32   depth;
    gX_Color color;
} gX_DevCube_Orb;

static gX_DevCube_State g_devcube;

static const gX_F32v3 g_devcubeCorners[8] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
};

static const gX_U8 g_devcubeEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static const gX_Color g_devcubeOrbColors[3] = {
    {255,  70,  70, 255},
    { 80, 255, 110, 255},
    { 80, 140, 255, 255},
};

static gX_I64 gX_DevCube_NowNs(gX_I0)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (gX_I64)ts.tv_sec * GX_DEVCUBE_NS_PER_SECOND + (gX_I64)ts.tv_nsec;
}

static gX_I0 gX_DevCube_Reset(gX_I1 active)
{
    memset(&g_devcube, 0, sizeof(g_devcube));
    g_devcube.startNs = gX_DevCube_NowNs();
    g_devcube.active = active;
}

static gX_I0 gX_DevCube_Update(gX_I0)
{
    if (!g_devcube.active) return;
    gX_I64 now = gX_DevCube_NowNs();
    if (now <= 0) return;
    gX_I64 elapsed = now - g_devcube.startNs;
    if (elapsed < 0) elapsed = 0;
    g_devcube.phaseTicks = gX_DevCube_PhaseTicks(elapsed);
    g_devcube.phase = (gX_F64)g_devcube.phaseTicks / (gX_F64)GX_DEVCUBE_HZ;
}

static gX_I0 gX_DevCube_SceneEnter(gX_I0)
{
    gX_DevCube_Reset(1);
}

static gX_I0 gX_DevCube_SceneExit(gX_I0)
{
    g_devcube.active = 0;
}

static gX_I0 gX_DevCube_Shutdown(gX_I0)
{
    memset(&g_devcube, 0, sizeof(g_devcube));
}

static gX_F32v3 gX_DevCube_Rotate(gX_F32v3 p, gX_F32 ax, gX_F32 ay)
{
    gX_F32 cy = cosf(ay), sy = sinf(ay);
    gX_F32 x = p.x * cy + p.z * sy;
    gX_F32 z = -p.x * sy + p.z * cy;
    gX_F32 cx = cosf(ax), sx = sinf(ax);
    gX_F32 y = p.y * cx - z * sx;
    gX_F32 z2 = p.y * sx + z * cx;
    return (gX_F32v3){x, y, z2};
}

static gX_DevCube_Projected gX_DevCube_Project(gX_F32v3 p, gX_F32 w, gX_F32 h)
{
    const gX_F32 camZ = 3.6f;
    const gX_F32 focal = h * 0.5f / tanf((gX_F32)(GX_DEVCUBE_PI * 0.125));
    gX_F32 depth = camZ - p.z;
    if (depth < 0.15f) depth = 0.15f;
    gX_DevCube_Projected out;
    out.p.x = w * 0.5f + p.x * focal / depth;
    out.p.y = h * 0.5f - p.y * focal / depth;
    out.depth = depth;
    out.visible = out.p.x > -w && out.p.x < w * 2.0f
        && out.p.y > -h && out.p.y < h * 2.0f;
    return out;
}

static gX_Color gX_DevCube_Alpha(gX_Color color, gX_U8 alpha)
{
    color.a = alpha;
    return color;
}

static gX_I0 gX_DevCube_GlowLine(gX_F32v2 a, gX_F32v2 b, gX_Color core)
{
    gX_F32 dx = b.x - a.x, dy = b.y - a.y;
    gX_F32 length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) return;
    gX_F32 nx = -dy / length, ny = dx / length;
    gX_Color halo = {(gX_U8)(core.r / 5), (gX_U8)(core.g / 5),
        (gX_U8)(core.b / 5), 80};
    const gX_F32 offsets[4][2] = {
        {-1.5f, 0.0f}, {1.5f, 0.0f}, {0.0f, -1.5f}, {0.0f, 1.5f},
    };
    for (gX_U32 i = 0; i < 4; i++)
    {
        gX_F32 ox = nx * offsets[i][0];
        gX_F32 oy = ny * offsets[i][1];
        gX_Line_Draw((gX_Line){
            {a.x + ox, a.y + oy}, {b.x + ox, b.y + oy}, 1.0f}, halo);
    }
    gX_Line_Draw((gX_Line){a, b, 1.0f}, core);
}

static gX_I0 gX_DevCube_DrawOrb(gX_DevCube_Orb orb)
{
    gX_Circle_Draw((gX_Circle){orb.p, orb.radius * 2.2f},
        gX_DevCube_Alpha(orb.color, 45));
    gX_Circle_Draw((gX_Circle){orb.p, orb.radius * 1.6f},
        gX_DevCube_Alpha(orb.color, 90));
    gX_Circle_Draw((gX_Circle){orb.p, orb.radius}, orb.color);
    gX_Circle_Draw((gX_Circle){orb.p, orb.radius * 0.36f},
        (gX_Color){245, 250, 255, 230});
}

static gX_I0 gX_DevCube_Draw(gX_I0)
{
    if (!g_devcube.active || !maindata.backendLoaded) return;

    gX_I32 w = (gX_I32)maindata.windowWidth;
    gX_I32 h = (gX_I32)maindata.windowHeight;
    if (w <= 0 || h <= 0)
    {
        w = (gX_I32)maindata.screenWidth;
        h = (gX_I32)maindata.screenHeight;
    }
    if (w <= 0 || h <= 0) return;

    gX_F32 fw = (gX_F32)w;
    gX_F32 fh = (gX_F32)h;
    gX_F64 phase = g_devcube.phase;
    gX_F32 ax = (gX_F32)fmod(phase * 0.31 + 0.45, GX_DEVCUBE_PI * 2.0);
    gX_F32 ay = (gX_F32)fmod(phase * 0.55, GX_DEVCUBE_PI * 2.0);
    gX_Color green = {70, 255, 120, 255};

    //ai: full save/restore — the overlay may land on any scene/GUI state and
    //    must leave the final frame pipeline untouched.
    gX_GL_State_Push();
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Write(0);
    gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
    gX_RHI_Disable(gX_RHI_CULL_FACE);
    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate(gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA,
        gX_RHI_SRC_ALPHA, gX_RHI_ONE_MINUS_SRC_ALPHA);
    gX_RHI_Viewport(0, 0, w, h);

    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_Push();
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0.0, (gX_F64)w, (gX_F64)h, 0.0, 0.0, 1.0);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_Push();
    gX_RHI_Matrix_LoadIdentity();

    //ai: green wireframe cube, rotated from the 60 Hz phase.
    gX_F32v2 projected[8];
    gX_I1 projectedOk[8];
    for (gX_U32 i = 0; i < 8; i++)
    {
        gX_F32v3 p = gX_DevCube_Rotate((gX_F32v3){
            g_devcubeCorners[i].x * 0.62f,
            g_devcubeCorners[i].y * 0.62f,
            g_devcubeCorners[i].z * 0.62f}, ax, ay);
        gX_DevCube_Projected q = gX_DevCube_Project(p, fw, fh);
        projected[i] = q.p;
        projectedOk[i] = q.visible;
    }
    for (gX_U32 i = 0; i < 12; i++)
    {
        gX_U8 a = g_devcubeEdges[i][0];
        gX_U8 b = g_devcubeEdges[i][1];
        if (!projectedOk[a] || !projectedOk[b]) continue;
        gX_DevCube_GlowLine(projected[a], projected[b], green);
    }

    //ai: pink 3:2 Lissajous inside the cube (3x and 2x angular rates).
    gX_F32 lissPhase = (gX_F32)fmod(phase * 0.90, GX_DEVCUBE_PI * 2.0);
    gX_Color pink = {255, 86, 196, 235};
    gX_F32v3 previous = {0};
    for (gX_I32 i = 0; i <= GX_DEVCUBE_SEGMENTS; i++)
    {
        gX_F32 t = (gX_F32)(GX_DEVCUBE_PI * 2.0 * (gX_F64)i
            / (gX_F64)GX_DEVCUBE_SEGMENTS);
        gX_F32v3 p = {
            0.42f * sinf(3.0f * t + lissPhase),
            0.42f * sinf(2.0f * t),
            0.10f * sinf(2.0f * t + lissPhase * 0.5f),
        };
        p = gX_DevCube_Rotate(p, ax, ay);
        if (i > 0)
        {
            gX_DevCube_Projected a = gX_DevCube_Project(previous, fw, fh);
            gX_DevCube_Projected b = gX_DevCube_Project(p, fw, fh);
            if (a.visible && b.visible)
                gX_Line_Draw((gX_Line){a.p, b.p, 1.0f}, pink);
        }
        previous = p;
    }
    {
        gX_F32 t = (gX_F32)fmod(phase * 0.37, 1.0) * (gX_F32)(GX_DEVCUBE_PI * 2.0);
        gX_F32v3 p = gX_DevCube_Rotate((gX_F32v3){
            0.42f * sinf(3.0f * t + lissPhase),
            0.42f * sinf(2.0f * t),
            0.10f * sinf(2.0f * t + lissPhase * 0.5f)}, ax, ay);
        gX_DevCube_Projected q = gX_DevCube_Project(p, fw, fh);
        if (q.visible)
            gX_Circle_Draw((gX_Circle){q.p, 3.0f},
                (gX_Color){255, 220, 245, 255});
    }

    //ai: three RGB orbs orbit the avatar; draw far-to-near for a stable read.
    gX_DevCube_Orb orbs[3];
    for (gX_U32 i = 0; i < 3; i++)
    {
        gX_F64 angle = phase * 1.10
            + (gX_F64)i * (GX_DEVCUBE_PI * 2.0 / 3.0);
        gX_F32v3 p = {
            0.95f * (gX_F32)cos(angle),
            0.30f * (gX_F32)sin(angle * 1.7 + (gX_F64)i * 1.3),
            0.95f * (gX_F32)sin(angle),
        };
        gX_DevCube_Projected q = gX_DevCube_Project(p, fw, fh);
        orbs[i].p = q.p;
        orbs[i].depth = q.depth;
        orbs[i].radius = 0.060f * (fh * 0.5f)
            / tanf((gX_F32)(GX_DEVCUBE_PI * 0.125)) / q.depth;
        orbs[i].color = g_devcubeOrbColors[i];
    }
    for (gX_U32 i = 0; i < 3; i++)
    {
        for (gX_U32 j = i + 1; j < 3; j++)
        {
            if (orbs[j].depth > orbs[i].depth)
            {
                gX_DevCube_Orb swap = orbs[i];
                orbs[i] = orbs[j];
                orbs[j] = swap;
            }
        }
    }
    for (gX_U32 i = 0; i < 3; i++)
        gX_DevCube_DrawOrb(orbs[i]);

    //ai: flush while the overlay projection is still active, then restore.
    gX_Drawing_Flush();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_Pop();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_Pop();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_GL_State_Pop();
}

//=============================================================================
// module entry
//=============================================================================

gX_I32 gX_Module_Entry(const gX_Module_Host *host);
gX_I32 gX_Module_Entry(const gX_Module_Host *host)
{
    if (!host || host->abiVersion != GX_MODULE_ABI_VERSION
        || host->structSize < sizeof(gX_Module_Host))
    {
        gX_TraceLog(gX_LOG_ERROR,
            "DEVCUBE: host ABI mismatch (host %u/%u, module %u)",
            host ? host->abiVersion : 0, host ? host->structSize : 0,
            (gX_U32)GX_MODULE_ABI_VERSION);
        return 1;
    }

    gX_DevCube_Reset(1);
    gX_I32 errors = 0;
    errors += host->RegisterUpdate(gX_DevCube_Update) != 0 ? 1 : 0;
    errors += host->RegisterDraw(gX_DevCube_Draw) != 0 ? 1 : 0;
    errors += host->RegisterSceneEnter(gX_DevCube_SceneEnter) != 0 ? 1 : 0;
    errors += host->RegisterSceneExit(gX_DevCube_SceneExit) != 0 ? 1 : 0;
    errors += host->RegisterShutdown(gX_DevCube_Shutdown) != 0 ? 1 : 0;
    gX_TraceLog(gX_LOG_NOTICE, "DEVCUBE: entry (engine %s)",
        host->engineVersion ? host->engineVersion : "?");
    return errors != 0 ? 1 : 0;
}

gX_U32 gX_Module_ABI(gX_I0);
gX_U32 gX_Module_ABI(gX_I0)
{
    return GX_MODULE_ABI_VERSION;
}
