/**********************************************************************************************
*
*   [gX: src/renderer/gfx/light.c]
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


#include "light.h"
#include "motion_path.h"

#include "../gfx.h"

#include <string.h>
#include <stdlib.h>

#include "../../common/macros.h"
#include "boilerplate.h"

#include "../../gxmath/matrix.h"
#include "../../gxmath/vector3.h"
#include "../../gxmath/gxmath.h"
#include <math.h>
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"

gX_I32 gX_RendererGFX_Light_Init(gX_RendererGFX_Light *light) {
    light->dir = (gX_F32v3){1.0f,-1.0f,1.0f}; //opencode: sun shines from above, casts downward shadows
    //opencode: initialize pos to a visible default (was uninit, causing debug spheres at origin)
    light->pos = (gX_F32v3){0.0f, 5.0f, 0.0f}; //opencode: above center for shadow testing
    light->orbitEnabled = 0;
    light->orbitPaused = 0;
    light->orbitLoop = 1;
    light->orbitPlane = 1;
    light->orbitCenter = light->pos;
    light->orbitRadius = 2.0f;
    light->orbitSpeed = 45.0f;
    light->orbitPhase = 0.0f;
    light->orbitElapsed = 0.0;
    //opencode: waypoint paths start empty; circle mode is the default.
    light->pathMode = 0;
    light->pathWaypointCount = 0;
    light->pathPingPong = 0;
    light->pathSmooth = 0;
    memset(light->pathWaypoints, 0, sizeof(light->pathWaypoints));
    //opencode: in-handles mirror the out side until authored independently.
    light->pathHandlesIndependent = 0;
    memset(light->pathTangentsIn, 0, sizeof(light->pathTangentsIn));
    gX_RendererGFX_Camera_Init(&light->camera);
    light->camera.camera3D.position.x = 0.1f;
    light->brightness = 1.0f;
    light->diffuseTint = gX_WHITE;
    light->specularTint = gX_WHITE;
    light->diffuseFactor = 1.0f;
    light->specularFactor = 1.0f;
    light->color = gX_WHITE;
    light->attenConst = 1.0f;
    light->attenLinear = 0.09f;
    light->attenQuad = 0.032f;
    //opencode: light cookie defaults
    light->cookieTex = (gX_Texture){0};
    light->cookieEnabled = false;
    //opencode: spot light cone default — 0 made spot lights invisible
    //   (smoothstep(cos(0), cos(0), cosAng) = 0 for any off-axis fragment)
    light->fov = 45.0f;

    //opencode: point light shadow defaults
    light->castShadows = false;
    light->shadowSize = gX_GFX_SHADOW_MAP_SIZE;
    light->shadowFov = 90.0f;
    light->shadowFboID = 0;
    light->shadowDepth = (gX_Texture){0};
    light->shadowFar = 50.0f;
    light->shadowRange = 0.0f; //opencode: 0 = unlimited shadow range
    return 0;
}

//opencode: evaluate point-light orbit state in world space with a bounded
//   frame delta so window stalls do not teleport the light around its path.
gX_I0 gX_RendererGFX_Light_Orbit_Update(gX_RendererGFX_Light *light,
    gX_F64 deltaTime) {
    if (!light || !light->orbitEnabled || light->type != 1) return;
    if (light->pathMode == 1 && light->pathWaypointCount >= 2) {
        gX_MotionPath_Waypoint_Update(light->pathWaypoints, light->pathTangents,
            light->pathTangentsIn, light->pathHandlesIndependent,
            &light->pathWaypointCount, gX_GFX_LIGHT_WAYPOINTS, light->orbitLoop,
            light->pathPingPong, light->pathSmooth, light->pathBezier,
            light->orbitSpeed, deltaTime, &light->orbitElapsed,
            &light->orbitPaused, &light->pos);
    } else {
        if (!isfinite(light->orbitElapsed) || light->orbitElapsed < 0.0)
            light->orbitElapsed = 0.0;
        if (!light->orbitPaused && isfinite(deltaTime) && deltaTime > 0.0) {
            if (deltaTime > 0.25) deltaTime = 0.25;
            light->orbitElapsed += deltaTime;
        }
        gX_F64 speed = isfinite(light->orbitSpeed) ? light->orbitSpeed : 0.0;
        if (!light->orbitLoop && !light->orbitPaused && fabs(speed) > 0.000001) {
            gX_F64 duration = 360.0 / fabs(speed);
            if (light->orbitElapsed >= duration) {
                light->orbitElapsed = duration;
                light->orbitPaused = 1;
            }
        }
        gX_F64 phase = isfinite(light->orbitPhase) ? light->orbitPhase : 0.0;
        gX_F64 angle = fmod(phase + light->orbitElapsed * speed, 360.0);
        if (angle < 0.0) angle += 360.0;
        gX_F32 radians = (gX_F32)angle * gX_DEG2RAD;
        gX_F32 radius = isfinite(light->orbitRadius) && light->orbitRadius > 0.0f
            ? light->orbitRadius : 0.0f;
        gX_F32 x = cosf(radians) * radius;
        gX_F32 y = sinf(radians) * radius;
        switch (light->orbitPlane) {
            case 0:
                light->pos = (gX_F32v3){light->orbitCenter.x + x,
                    light->orbitCenter.y + y, light->orbitCenter.z};
                break;
            case 2:
                light->pos = (gX_F32v3){light->orbitCenter.x,
                    light->orbitCenter.y + x, light->orbitCenter.z + y};
                break;
            default:
                light->pos = (gX_F32v3){light->orbitCenter.x + x,
                    light->orbitCenter.y, light->orbitCenter.z + y};
                break;
        }
    }
    //opencode: cache the trace hook — this runs per orbiting light per frame.
    static gX_I1 s_motionTraceChecked = 0;
    static gX_I1 s_motionTrace = 0;
    if (!s_motionTraceChecked) {
        const gX_I8 *trace = getenv("GX_MOTION_TRACE");
        s_motionTrace = (trace && trace[0] && trace[0] != '0') ? 1 : 0;
        s_motionTraceChecked = 1;
    }
    if (s_motionTrace) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_MOTION: kind=light pos=(%.3f,%.3f,%.3f) elapsed=%.3f paused=%d",
            light->pos.x, light->pos.y, light->pos.z,
            light->orbitElapsed, light->orbitPaused);
    }
}

//opencode: point light shadow depth resources — a SINGLE shared 2D array
//   texture (6 layers per shadow light) replaces the per-light depth cubemaps.
//   The array is created once (gX_RendererGFX_PointShadow_InitArray); lights
//   reference it via shadowFboID/shadowDepth.
//opencode: Phase 1b — lazy storage: the full 384-layer array (~400 MB) is
//   created only for a real shadow-casting point light (see the demand scan
//   in shader.c + Light_Update below). Otherwise a tiny 1-layer placeholder
//   keeps the sampler valid (farPlanes stay 0 → never sampled).
gX_I0 gX_RendererGFX_PointShadow_InitPlaceholder(gX_RendererGFX *gfx) {
    if (gfx->common.pointShadowInited) return;
    gX_U32 tex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D_ARRAY, "point_shadow_placeholder");
    gX_RHI_Tex_Storage3D(tex, gX_RHI_TEXTURE_2D_ARRAY, 1, gX_RHI_DEPTH_COMPONENT24, 8, 8, 1);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
    gfx->common.pointShadowArray = tex;
    gfx->common.pointShadowFbo = 0;
    gfx->common.pointShadowSize = 8;
    gfx->common.pointShadowInited = true;
    gfx->common.pointShadowPlaceholder = true;
}
gX_I0 gX_RendererGFX_PointShadow_InitArray(gX_RendererGFX *gfx) {
    if (gfx->common.pointShadowInited && !gfx->common.pointShadowPlaceholder) return;
    if (gfx->common.pointShadowInited && gfx->common.pointShadowPlaceholder) {
        gX_RHI_Tex_Delete(gfx->common.pointShadowArray);
        gfx->common.pointShadowArray = 0;
        gfx->common.pointShadowPlaceholder = false;
    }
    gX_U32 size = gX_GFX_POINT_SHADOW_ARRAY_SIZE;
    gX_U32 layers = (gX_U32)gX_GFX_MAX_POINT_SHADOW_LIGHTS * 6u;
    gX_U32 tex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D_ARRAY, "point_shadow_array");
    gX_RHI_Tex_Storage3D(tex, gX_RHI_TEXTURE_2D_ARRAY, 1, gX_RHI_DEPTH_COMPONENT24, size, size, layers);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
    gX_U32 fbo = 0;
    gX_RHI_FBO_Gen(1, &fbo);
    gX_RHI_BindFBO(fbo);
    gX_RHI_FBO_AttachLayer(tex, gX_RHI_DEPTH_ATTACHMENT, 0, 0);
    gX_RHI_DrawBuffer(gX_RHI_NONE);
    gX_RHI_ReadBuffer(gX_RHI_NONE);
    if (!gX_RHI_FBO_IsComplete()) {
        gX_TraceLog_GFX(gX_LOG_ERROR, "point shadow array FBO incomplete");
        gX_RHI_FBO_Delete(fbo);
        gX_RHI_Tex_Delete(tex);
        gX_RHI_UnbindFBO();
        gX_RHI_DrawBuffer(gX_RHI_BACK);
        return;
    }
    gX_RHI_UnbindFBO();
    gX_RHI_DrawBuffer(gX_RHI_BACK);
    gfx->common.pointShadowArray = tex;
    gfx->common.pointShadowFbo = fbo;
    gfx->common.pointShadowSize = (gX_I32)size;
    gfx->common.pointShadowInited = true;
    gfx->common.pointShadowPlaceholder = false;
    gX_TraceLog_GFX(gX_LOG_NOTICE, "point shadow array created: %dx%d layers=%u tex=%u fbo=%u", size, size, layers, tex, fbo);
}

//opencode: attach a shadow light to the shared array (no per-light FBO/cubemap)
static gX_I0 gX_AttachPointShadowToArray(gX_RendererGFX *gfx, gX_RendererGFX_Light *light) {
    if (!gfx->common.pointShadowInited) return;
    light->shadowFboID = gfx->common.pointShadowFbo;
    light->shadowDepth.id = gfx->common.pointShadowArray;
    light->shadowDepth.width = gfx->common.pointShadowSize;
    light->shadowDepth.height = gfx->common.pointShadowSize;
    light->shadowSize = (gX_U32)gfx->common.pointShadowSize;
}

//opencode: compute 6 cubemap face view-projection matrices for point light shadow
static gX_I0 gX_ComputePointShadowMatrices(gX_RendererGFX_Light *light) {
    gX_F32v3 pos = light->pos;
    gX_F32 far = light->shadowFar;
    gX_Matrix proj = gX_Matrix_Perspective(light->shadowFov * gX_DEG2RAD, 1.0f, 0.1f, far); // deg→rad
    
    gX_F32v3 targets[6];
    gX_F32v3 ups[6];
    targets[0] = gX_F32v3_Add(pos, (gX_F32v3){ 1.0f, 0.0f, 0.0f}); ups[0] = (gX_F32v3){0.0f,-1.0f, 0.0f};
    targets[1] = gX_F32v3_Add(pos, (gX_F32v3){-1.0f, 0.0f, 0.0f}); ups[1] = (gX_F32v3){0.0f,-1.0f, 0.0f};
    targets[2] = gX_F32v3_Add(pos, (gX_F32v3){ 0.0f, 1.0f, 0.0f}); ups[2] = (gX_F32v3){0.0f, 0.0f, 1.0f};
    targets[3] = gX_F32v3_Add(pos, (gX_F32v3){ 0.0f,-1.0f, 0.0f}); ups[3] = (gX_F32v3){0.0f, 0.0f,-1.0f};
    targets[4] = gX_F32v3_Add(pos, (gX_F32v3){ 0.0f, 0.0f, 1.0f}); ups[4] = (gX_F32v3){0.0f,-1.0f, 0.0f};
    targets[5] = gX_F32v3_Add(pos, (gX_F32v3){ 0.0f, 0.0f,-1.0f}); ups[5] = (gX_F32v3){0.0f,-1.0f, 0.0f};
    
    for (int i = 0; i < 6; i++) {
        gX_Matrix view = gX_Matrix_LookAt(pos, targets[i], ups[i]);
        light->shadowViews[i] = gX_Matrix_Multiply(view, proj); // view then projection
    }
}

//opencode: update camera3D.position so depth pass matches matLightSpace (was modifying a local copy, leaving camera3D at init position)
gX_I0 gX_RendererGFX_Light_Update(gX_RendererGFX *gfx, gX_RendererGFX_Light *light) {
    gX_F32 near = light->camera.near;
    gX_F32 far = light->camera.far;
    gX_F32 size = light->camera.size;
    gX_F32v3 lightDir = gX_F32v3_Normalize(light->dir);
    light->camera.camera3D.position = gX_F32v3_Sub(light->camera.camera3D.target,
                                                    gX_F32v3_MulScalar(lightDir, far * 0.5f));
    //opencode: compute a safe up vector for the light camera — the default (0,1,0)
    //   is degenerate when the light direction is parallel to Y (cross product of
    //   parallel vectors is zero, producing a NaN view matrix). Tilt the up vector
    //   to Z when the view direction aligns with Y, matching the pattern in draw.c
    //   for decal orientation (line 857: "if (fabsf(d->normal.y) > 0.99f)").
    gX_F32v3 viewDir = gX_F32v3_Normalize(gX_F32v3_Sub(light->camera.camera3D.target,
                                                       light->camera.camera3D.position));
    gX_F32v3 safeUp = (gX_F32v3){0.0f, 1.0f, 0.0f};
    if (fabsf(viewDir.y) > 0.99f) {
        safeUp = (gX_F32v3){0.0f, 0.0f, 1.0f};
    }
    light->camera.view = gX_Matrix_LookAt(light->camera.camera3D.position,
                                          light->camera.camera3D.target,
                                          safeUp);
    light->camera.projection = gX_Matrix_Ortho(-size, +size, -size, +size, near, far);
    light->camera.viewProjection = gX_Matrix_Multiply(light->camera.view, light->camera.projection); //opencode: view then projection (row-major storage: matrices transpose in GLSL, so multiply order is reversed)
    
    //opencode: update point light shadow resources — the light shares the
    //   renderer-wide 2D array texture (created on demand); the light only
    //   owns its 6 face view matrices
    if (light->castShadows && !light->sun) {
        if (!gfx->common.pointShadowInited || gfx->common.pointShadowPlaceholder) {
            gX_RendererGFX_PointShadow_InitArray(gfx);
        }
        gX_AttachPointShadowToArray(gfx, light);
        gX_ComputePointShadowMatrices(light);
    }
}

//opencode: dynamic scene management via heap
gX_I32 gX_RendererGFX_Light_Add(gX_RendererGFX *gfx) {
    gX_RendererGFX_Light *light = gX_Heap_Add_FirstEmpty_NoPtr(&gfx->lights);
    if (!light) return -1;
    gX_RendererGFX_Light_Init(light);
    //opencode: return index by computing offset within heap block
    return (gX_I32)((gX_U8*)light - (gX_U8*)gfx->lights.data) / (gX_I32)sizeof(gX_RendererGFX_Light);
}

//opencode: point-shadow storage is shared and owned by gfx->common; removing
//   one light only drops its borrowed references.
static gX_I0 gX_DetachShadowResources(gX_RendererGFX_Light *light) {
    light->shadowFboID = 0;
    light->shadowDepth = (gX_Texture){0};
}

gX_I32 gX_RendererGFX_Light_Remove(gX_RendererGFX *gfx, gX_U32 index) {
    if (index >= gfx->lights.amount) return -1;
    gX_RendererGFX_Light *light = gX_Heap_SeekPtr_Index(&gfx->lights, index);
    if (light) gX_DetachShadowResources(light);
    gX_Heap_Remove_Index_NoPtr(&gfx->lights, index);
    return 0;
}

gX_I32 gX_RendererGFX_Light_RemoveAll(gX_RendererGFX *gfx) {
    GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
        gX_DetachShadowResources(light);
    }
    GX_HEAP_REMOVE_ALL(gfx->lights);
    return 0;
}

//opencode: set a spot light's cookie from a texture path — stores the path and
//   marks the cookie atlas dirty so the next lights upload rebuilds it.
gX_I0 gX_RendererGFX_Light_SetCookie(gX_RendererGFX *gfx, gX_RendererGFX_Light *light, const gX_I8 *path) {
    if (!light || !path) return;
    strncpy(light->cookiePath, path, sizeof(light->cookiePath) - 1);
    light->cookiePath[sizeof(light->cookiePath) - 1] = '\0';
    light->cookieEnabled = true;
    if (gfx) gfx->common.cookieAtlasDirty = true;
}
