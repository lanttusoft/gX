/**********************************************************************************************
* 
*   [wrappers/drawing.c]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib drawing/mode functions
#include "wrappers_header.h"
#include "drawing.h"
#include "misc.h"
#include "text.h"
#include "../rhi/gl_state.h"
#include "../rhi/rhi_buffer_audit.h"
#include <stdlib.h>
//opencode: hunt - implicit-decl sweep (stdlib for calloc-use in raylib.h)
#include "../gxmath/constants.h"
#include "../gxmath/matrix.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi_state.h"
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_buf.h"
#include "../rhi/batch.h"

//opencode: PS2 backend uses GS draw library
#include "raylib.h"
//opencode: glad for direct GL state management (glIsEnabled/glEnable/glDisable).
//   Required for End3D's state save/restore (both backends now use the same
//   rlgl-based End3D instead of raylib's EndMode3D). Raylib initializes glad
//   during InitGraphicsDevice, so the function pointers are valid.

#include "../main.h"



//opencode: PS1 wrapper shared state — definitions live in main.c for PS1 builds

gX_DrawingInfo drawinfo;

//opencode: aspect-ratio-preserving viewport — centers content within the window
//   while maintaining screen aspect ratio, with black letterbox/pillarbox bars.
static gX_I0 gX_Drawing_SetViewport(gX_I0) {
    //opencode: v0.6.9 — single pillarbox impl (viewportBox): fullscreen
    //   until docked (identical math to the old maximize below), custom
    //   centered game rect with margins when docked. drawinfo mirrors it
    //   so wipe/composite/rlgl followers stay aligned.
    extern gX_I0 gX_GUI_Viewport_Refresh(gX_I0);
    extern gX_Rectangle gX_GUI_ViewportBox_Get(gX_I0);
    gX_GUI_Viewport_Refresh();
    gX_Rectangle vb = gX_GUI_ViewportBox_Get();
    drawinfo.vpX = (gX_I32)vb.x;
    drawinfo.vpY = (gX_I32)vb.y;
    drawinfo.vpW = (gX_I32)vb.width;
    drawinfo.vpH = (gX_I32)vb.height;
    gX_RHI_Viewport(drawinfo.vpX, drawinfo.vpY, drawinfo.vpW, drawinfo.vpH);
}

gX_I0 gX_Drawing_Begin(gX_I0) {
    if (!maindata.backendLoaded) { gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "DRAW: Begin called before backend init"); return; }
#if gX_ENABLE_RENDERER == 1
    extern gX_I0 gX_Renderer_BeginFrame(gX_I0);
    gX_Renderer_BeginFrame();
#endif
    raylib_Drawing_Begin();
    //opencode: unified clear + ortho projection via RHI matrix stack
    gX_Batch_Flush();
    gX_RHI_Viewport(0, 0, (gX_I32)maindata.windowWidth, (gX_I32)maindata.windowHeight);
    gX_RHI_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
    gX_Drawing_SetViewport();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0, (gX_F64)maindata.screenWidth, (gX_F64)maindata.screenHeight, 0, 0.0, 1.0);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Shader_Use(gX_RHI_Shader_GetDefault());
    return;
}

gX_I0 gX_Drawing_End(gX_I0) {
    raylib_Drawing_End();
}

//opencode: execute every queued draw path now — mixed passes depend on it
//   (raylib-native shapes/text defer to raylib's flush, batch quads are
//   immediate; without this the deferred queue paints over batch output).
gX_I0 gX_Drawing_Flush(gX_I0) {
    raylib_Drawing_Flush();
    gX_Batch_Flush();
}

gX_I0 gX_Drawing_ClearBG(gX_Color color) {
    //opencode: scissor-clear within the current viewport (drawinfo.vp rect).
    //   Inside a render texture, drawinfo.vp is (0,0,RT_width,RT_height) — set
    //   by BeginTexture. On default FB, it's the letterbox-aware rect from
    //   SetViewport. This keeps pillarbox/letterbox bars black while clearing
    //   only the render area inside the viewport. Direct rlgl calls replace
    //   raylib's ClearBackground (which clears the whole FB regardless of viewport).
    gX_I32 vpX = drawinfo.vpX, vpY = drawinfo.vpY;
    gX_I32 vpW = drawinfo.vpW, vpH = drawinfo.vpH;
    gX_I32 colorMask[4] = {1, 1, 1, 1};
    gX_I32 stencilMask = -1;
    gX_I1 depthMask = 1;
    gX_Batch_Flush();
    //opencode: glClear honors write masks; temporarily enable all attachment channels.
    gX_RHI_GetIntegerv(gX_RHI_COLOR_WRITEMASK, colorMask);
    gX_RHI_GetBooleanv(gX_RHI_DEPTH_WRITEMASK, &depthMask);
    gX_RHI_GetIntegerv(gX_RHI_STENCIL_WRITEMASK, &stencilMask);
    gX_RHI_Color_Mask(1, 1, 1, 1);
    gX_RHI_Depth_Write(1);
    gX_RHI_Stencil_Mask(0xffffffffu);
    gX_RHI_Enable(gX_RHI_SCISSOR_TEST);
    gX_RHI_Scissor(vpX, vpY, vpW, vpH);
    gX_RHI_ClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT | gX_RHI_STENCIL_BUFFER_BIT);
    gX_RHI_Color_Mask(colorMask[0] != 0, colorMask[1] != 0,
        colorMask[2] != 0, colorMask[3] != 0);
    gX_RHI_Depth_Write(depthMask);
    gX_RHI_Stencil_Mask((gX_U32)stencilMask);
    gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
    //opencode: Step-3-bis — removed the ClearBG DIAG readback block (3×
    //   glReadPixels + /tmp/clearbg_diag.txt write on the first 20 clears).
    //   It errored on multisample targets ("FBO anti-alias method is not
    //   valid for read pixels", NVIDIA GL_DEBUG) and served its one-off
    //   top-left-pixel diagnosis long ago.
}

//opencode: draw a fullscreen quad from (x,y) to (x+w,y+h) with white tint and UV 0..1.
//   Used by all shader-based fullscreen passes (sky, bloom, SSAO, etc.).
//   Bypasses the batch system — uses its own VAO so custom shaders work correctly.
gX_I0 gX_Drawing_DrawScreenQuad(gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    static gX_U32 vao = 0, vbo = 0;
    if (!vao) {
        vao = gX_RHI_VAO_Create("screenquad_vao");
        vbo = gX_RHI_Buffer_Create("screenquad_vbo");
        gX_RHI_VAO_Bind(vao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, vbo);
        gX_RHI_Attrib_Pointer(0, 2, gX_RHI_FLOAT, gX_RHI_FALSE, 16, (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, gX_RHI_FALSE, 16, (void*)8);
        gX_RHI_Attrib_Enable(1);
        gX_RHI_VAO_Bind(0);
    }
    //opencode: explicit triangles avoid driver-specific GL_TRIANGLE_FAN errors.
    float verts[] = {
        x,   y,   0, 0, x,   y+h, 0, 1, x+w, y+h, 1, 1,
        x,   y,   0, 0, x+w, y+h, 1, 1, x+w, y,   1, 0
    };
    //opencode: upload mvp uniform if current shader has it.
    //   Fragment-only shaders using the default vertex shader (gx_default_vert)
    //   require an orthographic mvp to transform screen-coord vertices to NDC.
    //   Without this, identity mvp leaves vertices outside [-1,1] → fully clipped.
    gX_I32 curProg = 0;
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &curProg);
    if (curProg > 0)
    {
        gX_I32 mvpLoc = gX_RHI_GetUniformLocation((gX_U32)curProg, "mvp");
        if (mvpLoc >= 0)
        {
            gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
            gX_RHI_UniformMatrix(mvpLoc, 1, 1, &mvp.m0);
        }
    }
    gX_RHI_VAO_Bind(vao);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, vbo);
    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, sizeof(verts), verts, gX_RHI_STREAM_DRAW);
    gX_RHI_DrawArrays(gX_RHI_TRIANGLES, 0, 6);
    gX_RHI_VAO_Bind(0);
}

//opencode: generic dynamic-geometry draw with the current program.
//   verts: interleaved x,y,u,v (4 floats per vertex), primMode e.g.
//   GL_TRIANGLE_FAN / GL_TRIANGLES. The caller enables the shader; this
//   uploads mvp from the RHI matrix stack if the program has it.
//   VAO/VBO are rebound on EVERY call (bindings must not rely on init-time
//   state — the screen-quad VAO bug).
gX_I0 gX_Drawing_DrawGeometry(const gX_F32 *verts, gX_U32 vertCount, gX_U32 primMode) {
    if (!verts || vertCount == 0) return;
    static gX_U32 vao = 0, vbo = 0;
    if (!vao) {
        vao = gX_RHI_VAO_Create("geom_vao");
        vbo = gX_RHI_Buffer_Create("geom_vbo");
        gX_RHI_VAO_Bind(vao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, vbo);
        gX_RHI_Attrib_Pointer(0, 2, gX_RHI_FLOAT, gX_RHI_FALSE, 16, (void*)0);
        gX_RHI_Attrib_Enable(0);
        gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, gX_RHI_FALSE, 16, (void*)8);
        gX_RHI_Attrib_Enable(1);
        gX_RHI_VAO_Bind(0);
    }
    gX_I32 curProg = 0;
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &curProg);
    if (curProg > 0)
    {
        gX_I32 mvpLoc = gX_RHI_GetUniformLocation((gX_U32)curProg, "mvp");
        if (mvpLoc >= 0)
        {
            gX_Matrix mvp = gX_RHI_Matrix_GetProjection();
            gX_RHI_UniformMatrix(mvpLoc, 1, 1, &mvp.m0);
        }
    }
    gX_RHI_VAO_Bind(vao);
    gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, vbo);
    gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, vertCount * 16, verts, gX_RHI_STREAM_DRAW);
    gX_RHI_DrawArrays(primMode, 0, (gX_U32)vertCount);
    gX_RHI_VAO_Bind(0);
}

gX_I0 gX_Drawing_FPS(gX_F32v2 pos) {
    //opencode: D10 — unified FPS counter via gX_Time_Get (replaces raylib DrawFPS)
    static gX_F64 lastFpsUpdate = 0.0;
    static gX_I32 frameCount = 0;
    static gX_I32 currentFps = 0;
    gX_F64 now = gX_Time_Get();
    frameCount++;
    if (now - lastFpsUpdate >= 1.0) {
        currentFps = frameCount;
        frameCount = 0;
        lastFpsUpdate = now;
    }
    gX_I8 buf[32];
    gX_I32 len = snprintf(buf, sizeof(buf), "%d FPS", currentFps);
    (void)len;
    gX_RawString_Draw_Lesser(buf, (gX_F32v2){pos.x, pos.y}, 10, (gX_Color){0, 255, 0, 200});
}

gX_I0 gX_Drawing_Begin3D(gX_Camera3D cam3D) {
    //opencode: unified 3D setup via RHI matrix stack — replaces raylib BeginMode3D + GLFW rlgl path.
    gX_Batch_Flush();
    gX_GL_State_Push();
    drawinfo.hasPrevState3D = true;
    {
        gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
        gX_F64 nearDist = 0.01;
        gX_F64 farDist = 1000.0;
        gX_F64 top = nearDist * (gX_F64)tan((gX_F64)cam3D.fovy * gX_DEG2RAD * 0.5);
        gX_F64 right = top * (gX_F64)aspect;
        gX_F32 halfH = cam3D.fovy * 0.5f;
        gX_F32 halfW = halfH * aspect;
        gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
        gX_RHI_Matrix_Push();
        gX_RHI_Matrix_LoadIdentity();
        //opencode: fix inverted projection check — gX camera enum mirrors raylib's
        //   (gX_CAMERA_PERSPECTIVE=0, gX_CAMERA_ORTHOGRAPHIC=1). The RHI migration
        //   had the branches swapped, so every perspective camera rendered ortho.
        if (cam3D.projection == CAMERA_ORTHOGRAPHIC) {
            gX_RHI_Matrix_Ortho(-halfW, halfW, -halfH, halfH, nearDist, farDist);
        } else {
            gX_RHI_Matrix_Frustum(-right, right, -top, top, nearDist, farDist);
        }
        gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
        gX_RHI_Matrix_LoadIdentity();
        gX_Matrix viewMat = gX_Matrix_LookAt(cam3D.position, cam3D.target, cam3D.up);
        gX_RHI_Matrix_Mult(&viewMat.m0);
        gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    }
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    //opencode: 3D passes must start with depth writes enabled after transparent draws.
    gX_RHI_Depth_Write(1);
}

gX_I0 gX_Drawing_BeginTexture(gX_RenderTexture rt) {
    if (!maindata.backendLoaded) { gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "DRAW: BeginTexture before backend init"); return; }
    //opencode: flush the backend-native queue before redirecting the FBO.
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    //opencode: register and scope each target around its render-to-texture pass.
    gX_RHI_BufferAudit_RegisterTarget(rt.id, rt.texture.id, rt.depth.id,
        rt.texture.width, rt.texture.height, NULL);
    gX_RHI_BufferAudit_TargetBegin(rt.id);
    gX_RHI_BindFBO(rt.id);
    gX_RHI_InvalidateFBO();
    gX_RHI_Viewport(0, 0, (gX_I32)rt.texture.width, (gX_I32)rt.texture.height);
    drawinfo.vpX = 0; drawinfo.vpY = 0;
    drawinfo.vpW = (gX_I32)rt.texture.width;
    drawinfo.vpH = (gX_I32)rt.texture.height;
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0, (gX_F64)rt.texture.width, (gX_F64)rt.texture.height, 0, 0.0, 1.0);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_LoadIdentity();
//opencode: PS2 stub — GS render-to-texture not yet implemented
    //opencode: mark canvas as drawn this frame (used by CaptureBackground).
#if gX_ENABLE_RENDERER == 1
    {
        extern gX_U32 gX_Renderer_GetCanvasTextureId(gX_I0);
        if (rt.id != 0 && rt.id == gX_Renderer_GetCanvasTextureId()) {
            extern gX_I0 gX_Renderer_SetCanvasDrawn(gX_I1);
            gX_Renderer_SetCanvasDrawn(1);
        }
    }
#endif
}

gX_I0 gX_Drawing_EndTexture(gX_I0) {
    //opencode: native raylib primitives must land in the RT before unbinding it;
    //   otherwise EndDrawing flushes them onto the default framebuffer later.
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    //opencode: close the active target scope before unbinding its framebuffer.
    gX_RHI_BufferAudit_TargetEnd();
    //opencode: gX's RHI batch is separate from raylib's native queue; both have
    //   now been drained before the framebuffer and viewport are restored.
    gX_RHI_UnbindFBO();
    gX_RHI_InvalidateFBO();
    gX_Drawing_SetViewport();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0, (gX_F64)maindata.screenWidth, (gX_F64)maindata.screenHeight, 0, 0.0, 1.0);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_LoadIdentity();
}

static gX_U32 g_prevShaderId = 0;

gX_I0 gX_Drawing_BeginShader(gX_Shader shader) {
    if (!maindata.backendLoaded) { gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "DRAW: BeginShader before backend init"); return; }
    //opencode: execute queued native primitives before the direct RHI pass so
    //   they cannot overwrite the shader output at EndDrawing.
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, (gX_I32*)&g_prevShaderId);
    gX_RHI_Shader_Use(shader.id);
}

gX_I0 gX_Drawing_BeginBlend(gX_I32 mode) {
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    gX_RHI_Blend_Mode(mode);
}

gX_I0 gX_Drawing_BeginBlendCustomSeparate(gX_I32 srcRGB, gX_I32 dstRGB, gX_I32 srcAlpha, gX_I32 dstAlpha) {
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    //opencode: enable blending explicitly — raylib's rlgl keeps GL_BLEND on by
    //   default so the water (and other alpha draws) blended there, but the
    //   GLFW RHI starts with it off → the world water rendered OPAQUE on GLFW.
    gX_RHI_Enable(gX_RHI_BLEND);
    gX_RHI_Blend_Func_Separate((gX_U32)srcRGB, (gX_U32)dstRGB, (gX_U32)srcAlpha, (gX_U32)dstAlpha);
}

//opencode: backface culling control for interior rendering (BSP rooms)
gX_I0 gX_Drawing_DisableCulling(gX_I0) {
    gX_RHI_Disable(gX_RHI_CULL_FACE);
}

gX_I0 gX_Drawing_EnableCulling(gX_I0) {
    gX_RHI_Enable(gX_RHI_CULL_FACE);
    gX_RHI_Cull_Mode(gX_RHI_BACK);
}

gX_I0 gX_Drawing_End3D() {
    gX_Batch_Flush();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_Pop();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_LoadIdentity();
    if (drawinfo.hasPrevState3D) {
        gX_GL_State_Pop();
        drawinfo.hasPrevState3D = false;
    } else {
        gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    }
}

gX_I0 gX_Drawing_EndShader(gX_I0) {
    gX_Batch_Flush();
    gX_U32 restoreId = (g_prevShaderId > 0) ? g_prevShaderId : gX_RHI_Shader_GetDefault();
    gX_RHI_Shader_Use(restoreId);
    g_prevShaderId = 0;
}

//opencode: scissor test wrapper
gX_I0 gX_Drawing_BeginScissor(gX_Rectangle rect) {
    if (!maindata.backendLoaded) return;
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    gX_RHI_Enable(gX_RHI_SCISSOR_TEST);
    gX_RHI_Scissor((gX_I32)rect.x, (gX_I32)rect.y, (gX_I32)rect.width, (gX_I32)rect.height);
}

gX_I0 gX_Drawing_EndScissor(gX_I0) {
    if (!maindata.backendLoaded) return;
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
}

gX_I0 gX_Drawing_EndBlend(gX_I0) {
    raylib_Drawing_Flush();
    gX_Batch_Flush();
    gX_RHI_Blend_Mode(gX_RHI_BLEND_ALPHA);
}

gX_I0 gX_Drawing_Begin2D(gX_Camera2D cam2D) {
    gX_RHI_Matrix_Translate((gX_F32)maindata.screenWidth * 0.5f, (gX_F32)maindata.screenHeight * 0.5f, 0.0f);
    gX_RHI_Matrix_Rotate(cam2D.rotation, 0, 0, 1);
    gX_RHI_Matrix_Scale(cam2D.zoom, cam2D.zoom, 1.0f);
    gX_RHI_Matrix_Translate(-cam2D.target.x, -cam2D.target.y, 0.0f);
    gX_RHI_Matrix_Translate(-cam2D.offset.x, -cam2D.offset.y, 0.0f);
}

gX_I0 gX_Drawing_End2D(gX_I0) {
    gX_RHI_Matrix_LoadIdentity();
}
