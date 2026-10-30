/**********************************************************************************************
*
*   [gX: src/renderer/wireframe.c]
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


//opencode: enhanced wireframe renderer — multi-model cycle, auto-rotation, color cycling
#include "wireframe.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "../camera.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/enum/keys.h"
#include "../common/path.h"
#include "../input.h"
#include "../main.h"
#include "../wrappers/wrappers.h"

#include "renderer.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_state.h"
#include "../common/utils.h"
//opencode: hunt - implicit-decl sweep
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi_buf.h"

//opencode: color cycle palette (5 colors)
static const gX_Color s_palette[] = {
    {200, 220, 255, 255},  // cool white
    {255, 200, 100, 255},  // warm gold
    {100, 255, 180, 255},  // mint
    {255, 120, 180, 255},  // pink
    {180, 140, 255, 255},  // lavender
};
#define PALETTE_COUNT (sizeof(s_palette) / sizeof(s_palette[0]))

//opencode: model paths to load (in order)
static const gX_I8 *s_modelPaths[GX_WIREFRAME_MODEL_MAX] = {
    "button.glb",
    "primitive/cube.glb",
    "primitive/torus.glb",
    "teapot.glb",
};

//opencode: background palette (cycles in sync with wireColor)
static const gX_Color s_bgPalette[] = {
    {10, 10, 20, 255},
    {20, 15, 10, 255},
    {10, 20, 15, 255},
    {20, 10, 15, 255},
    {15, 12, 22, 255},
};

static gX_I0 wireframe_gpu_load(gX_I0 *arg) {
    gX_RendererWireframe *wf = (gX_RendererWireframe*)arg;
    wf->modelCount = 0;
    for (gX_I32 mi = 0; mi < GX_WIREFRAME_MODEL_MAX; mi++) {
        wf->customVao[mi] = 0;
        gX_I8 fullPath[256];
        snprintf(fullPath, sizeof(fullPath), "resources/model/%s", s_modelPaths[mi]);
        wf->models[mi] = gX_Model_Load(fullPath);
        if (wf->models[mi].meshCount > 0) {
            wf->modelCount++;
            //opencode: create custom VAO from the mesh data (bypasses raylib's broken VAO)
            gX_Mesh *m = &wf->models[mi].meshes[0];
            if (m->vertices && m->indices) {
                wf->customVao[mi] = gX_RHI_VAO_Create("wireframe_vao");
                wf->customVbo[mi] = gX_RHI_Buffer_Create("wireframe_vbo");
                wf->customEbo[mi] = gX_RHI_Buffer_Create("wireframe_ebo");
                gX_RHI_VAO_Bind(wf->customVao[mi]);
                // Interleave position + normal + color (white) into VBO
                gX_I32 vf = m->vertexCount;
                gX_I32 floatCount = vf * 10; // 3 pos + 3 normal + 4 color
                gX_F32 *buf = (gX_F32*)malloc(floatCount * sizeof(gX_F32));
                for (gX_I32 vi = 0; vi < vf; vi++) {
                    buf[vi*10+0] = m->vertices[vi*3+0];
                    buf[vi*10+1] = m->vertices[vi*3+1];
                    buf[vi*10+2] = m->vertices[vi*3+2];
                    buf[vi*10+3] = m->normals ? m->normals[vi*3+0] : 0;
                    buf[vi*10+4] = m->normals ? m->normals[vi*3+1] : 1;
                    buf[vi*10+5] = m->normals ? m->normals[vi*3+2] : 0;
                    buf[vi*10+6] = 1; buf[vi*10+7] = 1; // white RGBA
                    buf[vi*10+8] = 1; buf[vi*10+9] = 1;
                }
                gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, wf->customVbo[mi]);
                gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, floatCount * sizeof(gX_F32), buf, gX_RHI_STATIC_DRAW);
                free(buf);
                gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, 10*4, (void*)0);
                gX_RHI_Attrib_Enable(0);
                gX_RHI_Attrib_Pointer(2, 3, gX_RHI_FLOAT, 0, 10*4, (void*)(3*4));
                gX_RHI_Attrib_Enable(2);
                gX_RHI_Attrib_Pointer(3, 4, gX_RHI_FLOAT, 0, 10*4, (void*)(6*4));
                gX_RHI_Attrib_Enable(3);
                gX_RHI_Buffer_Bind(gX_RHI_ELEMENT_ARRAY_BUFFER, wf->customEbo[mi]);
                gX_RHI_Buffer_Data(gX_RHI_ELEMENT_ARRAY_BUFFER, m->triangleCount * 3 * sizeof(gX_U16),
                    m->indices, gX_RHI_STATIC_DRAW);
                gX_RHI_VAO_Bind(0);
                wf->customTriCount[mi] = m->triangleCount;
            }
        }
    }
    if (wf->modelCount == 0) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING, "WIREFRAME: no models loaded");
        wf->loadingstate.loaded = true;
        return;
    }
    wf->currentModel = 0;
    wf->orbitAngle = 0.0f;
    wf->orbitSpeed = 0.8f;       // radians per second (~46 deg/s)
    wf->wireColor = s_palette[0];
    wf->bgColor = s_bgPalette[0];
    wf->loadingstate.loaded = true;
}

static gX_I0 wireframe_gpu_unload(gX_I0 *arg) {
    gX_RendererWireframe *wf = (gX_RendererWireframe*)arg;
    for (gX_I32 mi = 0; mi < GX_WIREFRAME_MODEL_MAX; mi++) {
        if (wf->customVao[mi]) gX_RHI_VAO_Delete(wf->customVao[mi]);
        if (wf->customVbo[mi]) gX_RHI_Buffer_Delete(wf->customVbo[mi]);
        if (wf->customEbo[mi]) gX_RHI_Buffer_Delete(wf->customEbo[mi]);
        gX_Model_Unload(wf->models[mi]);
    }
    wf->modelCount = 0;
    wf->loadingstate.loaded = false;
}

gX_I32 gX_RendererWireframe_Load(gX_RendererWireframe *wf) {
    gX_TRACELOG_PROLOGUE("RendererWireframe_Load","RENDERER_WIREFRAME: Loading")
    strcpy(wf->name, "[gX] Wireframe");
    strcpy(wf->description, "wireframe model viewer with auto-rotation and color cycling");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", wf->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", wf->description);
    wf->loadingstate = (gX_LoadingState){0};
    wf->loadingstate.loadAsap = true;
    gX_TRACELOG_EPILOGUE("RENDERER_WIREFRAME: Load done")
    return 0;
}

gX_I32 gX_RendererWireframe_Unload(gX_RendererWireframe *wf) {
    if (wf->loadingstate.loaded) wireframe_gpu_unload(wf);
    return 0;
}

gX_I0 gX_RendererWireframe_Update(gX_RendererWireframe *wf) {
    gX_LoadingState_Handler(&wf->loadingstate, wireframe_gpu_load, wireframe_gpu_unload, wf);
    if (!wf->loadingstate.loaded || wf->modelCount == 0) return;

    gX_F32 dt = (gX_F32)gX_FrameTime_Get();

    // Auto-orbit
    wf->orbitAngle += wf->orbitSpeed * dt;
    if (wf->orbitAngle > 2.0f * (gX_F32)gX_PI) {
        wf->orbitAngle -= 2.0f * (gX_F32)gX_PI;
    }

    // Color cycling (~3 seconds per color)
    gX_F32 colorPeriod = 3.0f;
    gX_F32 t = (gX_F32)maindata.timeElapsed;
    gX_I32 ci = ((gX_I32)(t / colorPeriod)) % PALETTE_COUNT;
    wf->wireColor = s_palette[ci];
    wf->bgColor = s_bgPalette[ci];

    // Keyboard controls
    if (gX_Key_IsPressed(gX_KEY_ONE))       wf->currentModel = 0;
    else if (gX_Key_IsPressed(gX_KEY_TWO))   wf->currentModel = 1;
    else if (gX_Key_IsPressed(gX_KEY_THREE)) wf->currentModel = 2;
    else if (gX_Key_IsPressed(gX_KEY_FOUR))  wf->currentModel = 3;
    if (gX_Key_IsPressed(gX_KEY_LEFT))  wf->orbitSpeed -= 0.2f;
    if (gX_Key_IsPressed(gX_KEY_RIGHT)) wf->orbitSpeed += 0.2f;
    if (wf->orbitSpeed < 0.0f) wf->orbitSpeed = 0.0f;
    if (wf->currentModel >= wf->modelCount) wf->currentModel = wf->modelCount - 1;
    if (wf->currentModel < 0) wf->currentModel = 0;
}

gX_I0 gX_RendererWireframe_DrawTo(gX_RendererWireframe *wf, gX_RenderTexture *outputRT) {
    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG(wf->bgColor);
        if (wf->loadingstate.loaded && wf->modelCount > 0) {
            gX_I32 mi = wf->currentModel;

            // Orbit camera around model at origin
            gX_F32 cx = sinf(wf->orbitAngle) * 3.0f;
            gX_F32 cz = cosf(wf->orbitAngle) * 3.0f;
            camera.camera3D.position = (gX_F32v3){cx, 0.5f, cz};
            camera.camera3D.target = (gX_F32v3){0.0f, 0.0f, 0.0f};
            camera.camera3D.up = (gX_F32v3){0.0f, 1.0f, 0.0f};
            camera.camera3D.fovy = 45.0f;

            gX_Drawing_Begin3D(camera.camera3D);
                //opencode: draw via custom VAO (bypasses raylib mesh upload)
                if (wf->customVao[mi]) {
                    gX_U32 prog = gX_RHI_Shader_GetDefault();
                    gX_RHI_Shader_Use(prog);
                    gX_I32 mvpLoc = gX_RHI_GetUniformLocation(prog, "mvp");
                    gX_I32 colLoc = gX_RHI_GetUniformLocation(prog, "colDiffuse");
                    if (mvpLoc >= 0) {
                        gX_Camera3D *cam = &camera.camera3D;
                        gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
                        gX_F32 fovRad = cam->fovy * (3.14159265f / 180.0f);
                        gX_F32 f = 1.0f / tanf(fovRad * 0.5f);
                        gX_F32 proj[16] = {
                            f/aspect, 0, 0, 0,
                            0, f, 0, 0,
                            0, 0, (1000+0.01)/(0.01-1000), (2*1000*0.01)/(0.01-1000),
                            0, 0, -1, 0
                        };
                        gX_F32v3 fwd = gX_F32v3_Normalize(gX_F32v3_Sub(cam->target, cam->position));
                        gX_F32v3 right = gX_F32v3_Normalize(gX_F32v3_Cross(fwd, cam->up));
                        gX_F32v3 up = gX_F32v3_Cross(right, fwd);
                        gX_F32 view[16] = {
                            right.x, right.y, right.z, -gX_F32v3_Dot(right, cam->position),
                            up.x, up.y, up.z, -gX_F32v3_Dot(up, cam->position),
                            -fwd.x, -fwd.y, -fwd.z, gX_F32v3_Dot(fwd, cam->position),
                            0, 0, 0, 1
                        };
                        gX_F32 mvp[16] = {0};
                        for (int k = 0; k < 4; k++)
                            for (int j = 0; j < 4; j++)
                                for (int i = 0; i < 4; i++)
                                    mvp[k*4+j] += proj[k*4+i] * view[i*4+j];
                        //opencode: row-major mvp needs transpose=1 (gxmath
                        //   convention) — transpose=0 rendered (PV)^T garbage
                        //   (squashed triangle pancake, verified visually).
                        gX_RHI_UniformMatrix(mvpLoc, 1, 1, mvp);
                    }
                    if (colLoc >= 0) { const gX_F32 col[4] = {1,1,1,1}; gX_RHI_UniformVec4(colLoc, 1, col); }
                    gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_LINE);
                    gX_RHI_Disable(gX_RHI_CULL_FACE);
                    gX_RHI_VAO_Bind(wf->customVao[mi]);
                    gX_RHI_DrawElements(gX_RHI_TRIANGLES, (gX_U32)(wf->customTriCount[mi] * 3),
                        gX_RHI_UNSIGNED_SHORT, NULL);
                    gX_RHI_Enable(gX_RHI_CULL_FACE);
                    gX_RHI_VAO_Bind(0);
                    gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, gX_RHI_FILL);
                }
            gX_Drawing_End3D();
        }
    gX_Drawing_EndTexture();
}
