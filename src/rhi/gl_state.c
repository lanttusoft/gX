/**********************************************************************************************
*
*   [gX: src/rhi/gl_state.c]
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


/**********************************************************************************************
*
*   [src/wrappers/gl_state.c]
*   gX — a game engine
*
*   GL state save/restore stack for GFX→GUI transition safety.
*   Prevents GL state leaks across module boundaries.
*
**********************************************************************************************/

#include "gl_state.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "rhi_fbo.h"
#include "rhi_state.h"
#include "rhi_tex.h"
#include "rhi.h"
#include "../wrappers/wrappers_gl.h"
#include <string.h>

static gX_GL_State stateStack[GX_GL_STACK_DEPTH];
static gX_I32 stackPtr = -1;

gX_I0 gX_GL_State_Save(gX_GL_State *s) {
    s->blendEnabled   = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB,   &s->srcRGB);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB,   &s->dstRGB);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_ALPHA, &s->srcA);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_ALPHA, &s->dstA);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &s->blendEquationRGB);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_ALPHA, &s->blendEquationAlpha);
    s->cullFaceEnabled = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
    gX_RHI_GetIntegerv(gX_RHI_CULL_FACE_MODE, &s->cullFaceMode);
    gX_RHI_GetIntegerv(gX_RHI_FRONT_FACE, &s->frontFace);
    s->depthTestEnabled = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    gX_RHI_GetIntegerv(gX_RHI_DEPTH_WRITEMASK, (gX_I32*)&s->depthMask);
    gX_RHI_GetIntegerv(gX_RHI_DEPTH_FUNC, &s->depthFunc);
    s->scissorEnabled = gX_RHI_IsEnabled(gX_RHI_SCISSOR_TEST);
    gX_RHI_GetIntegerv(gX_RHI_ACTIVE_TEXTURE, &s->activeTexture);
    gX_RHI_GetIntegerv(gX_RHI_VIEWPORT, s->viewport);
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &s->currentProgram);
    gX_RHI_GetIntegerv(gX_RHI_COLOR_WRITEMASK, s->colorMask);
    s->drawFbo = (gX_I32)gX_RHI_GetDrawFBO();
    s->readFbo = (gX_I32)gX_RHI_GetReadFBO();
    //opencode: the RHI FBO tracker can be stale (-1 from InvalidateFBO), in
    //   which case GetDrawFBO() returns 0 as a safe fallback — but that would
    //   make this save record FBO 0 while a real FBO is bound. Restoring 0
    //   later (End3D) silently unbinds the canvas buffer, so subsequent 2D
    //   draws land in the default framebuffer. Always query the real GL state.
    gX_RHI_GetIntegerv(gX_RHI_DRAW_FRAMEBUFFER_BINDING, &s->drawFbo);
    gX_RHI_GetIntegerv(gX_RHI_READ_FRAMEBUFFER_BINDING, &s->readFbo);
    gX_RHI_GetIntegerv(gX_RHI_POLYGON_MODE, s->polygonMode);
    s->backendModelview = gX_GL_Matrix_GetModelView();
    s->backendProjection = gX_GL_Matrix_GetProjection();
}

gX_I0 gX_GL_State_Restore(gX_GL_State *s) {
    if (s->blendEnabled)      gX_RHI_Enable(gX_RHI_BLEND);        else gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_Blend_Equation_Separate((gX_U32)s->blendEquationRGB, (gX_U32)s->blendEquationAlpha);
    gX_RHI_Blend_Func_Separate((gX_U32)s->srcRGB, (gX_U32)s->dstRGB, (gX_U32)s->srcA, (gX_U32)s->dstA);
    if (s->cullFaceEnabled)   gX_RHI_Enable(gX_RHI_CULL_FACE);    else gX_RHI_Disable(gX_RHI_CULL_FACE);
    gX_RHI_Cull_Mode((gX_U32)s->cullFaceMode);
    gX_RHI_FrontFace((gX_U32)s->frontFace);
    if (s->depthTestEnabled)  gX_RHI_Enable(gX_RHI_DEPTH_TEST);   else gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Write(s->depthMask ? 1 : 0);
    gX_RHI_Depth_Func((gX_U32)s->depthFunc);
    if (s->scissorEnabled)    gX_RHI_Enable(gX_RHI_SCISSOR_TEST); else gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
    gX_RHI_ActiveTexture((gX_U32)s->activeTexture);
    gX_RHI_Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    gX_RHI_Shader_Use((gX_U32)s->currentProgram);
    gX_RHI_Color_Mask(s->colorMask[0], s->colorMask[1], s->colorMask[2], s->colorMask[3]);
    gX_RHI_BindDrawFBO((gX_RHI_FBO)s->drawFbo);
    gX_RHI_BindReadFBO((gX_RHI_FBO)s->readFbo);
    gX_RHI_PolygonMode(gX_RHI_FRONT_AND_BACK, (gX_U32)s->polygonMode[0]);
    gX_GL_Matrix_SetModelView(s->backendModelview);
    gX_GL_Matrix_SetProjection(s->backendProjection);
    //opencode: invalidate RHI state caches after restoring GL state via raw calls
    gX_RHI_InvalidateCaches();
}

gX_I0 gX_GL_State_Push(gX_I0) {
    if (stackPtr >= GX_GL_STACK_DEPTH - 1) {
        gX_TraceLog(gX_LOG_WARNING, "GL state stack overflow");
        return;
    }
    stackPtr++;
    gX_GL_State_Save(&stateStack[stackPtr]);
}

gX_I0 gX_GL_State_Pop(gX_I0) {
    if (stackPtr < 0) {
        gX_TraceLog(gX_LOG_ERROR, "GL state stack UNDERFLOW — unbalanced Push/Pop! State may be corrupted.");
        return;
    }
    gX_GL_State_Restore(&stateStack[stackPtr]);
    //opencode: check RHI cache consistency after restore (--rhi-diagnostics)
    gX_RHI_Diagnostics_Check("GL_Pop");
    stackPtr--;
}

//opencode: expose the scoped GL-state depth to frame audits so an abandoned
//   Push cannot silently carry into the next frame.
gX_I32 gX_GL_State_GetDepth(gX_I0) {
    return stackPtr;
}

gX_I0 gX_GL_State_Diff(const gX_GL_State *before, const gX_GL_State *after, const gX_I8 *label) {
    if (!before || !after) return;
    gX_I1 any = 0;
    #define DIFF_FIELD(field,fmt) do { \
        if (before->field != after->field) { \
            if (!any) { gX_TraceLog(gX_LOG_WARNING, "GL_STATE_DIFF %s:", label); any = 1; } \
            gX_TraceLog(gX_LOG_WARNING, "  %s was=" fmt " now=" fmt, #field, (gX_U64)(gX_U32)before->field, (gX_U64)(gX_U32)after->field); \
        } \
    } while(0)
    #define DIFF_ARRAY(field,count,fmt) do { \
        for (gX_I32 _i = 0; _i < (count); _i++) { \
            if (before->field[_i] != after->field[_i]) { \
                if (!any) { gX_TraceLog(gX_LOG_WARNING, "GL_STATE_DIFF %s:", label); any = 1; } \
                gX_TraceLog(gX_LOG_WARNING, "  %s[%d] was=" fmt " now=" fmt, #field, _i, (gX_U64)(gX_U32)before->field[_i], (gX_U64)(gX_U32)after->field[_i]); \
            } \
        } \
    } while(0)

    DIFF_FIELD(blendEnabled, "%d");
    if (before->blendEnabled || after->blendEnabled) {
        DIFF_FIELD(srcRGB, "%d"); DIFF_FIELD(dstRGB, "%d");
        DIFF_FIELD(srcA, "%d");  DIFF_FIELD(dstA, "%d");
        DIFF_FIELD(blendEquationRGB, "%d"); DIFF_FIELD(blendEquationAlpha, "%d");
    }
    DIFF_FIELD(cullFaceEnabled, "%d");
    if (before->cullFaceEnabled || after->cullFaceEnabled) {
        DIFF_FIELD(cullFaceMode, "%d"); DIFF_FIELD(frontFace, "%d");
    }
    DIFF_FIELD(depthTestEnabled, "%d");
    if (before->depthTestEnabled || after->depthTestEnabled) {
        DIFF_FIELD(depthMask, "%d"); DIFF_FIELD(depthFunc, "%d");
    }
    DIFF_FIELD(scissorEnabled, "%d");
    DIFF_FIELD(activeTexture, "%d");
    DIFF_ARRAY(viewport, 4, "%d");
    DIFF_FIELD(currentProgram, "%d");
    DIFF_ARRAY(colorMask, 4, "%d");
    DIFF_FIELD(drawFbo, "%d");
    DIFF_FIELD(readFbo, "%d");
    DIFF_ARRAY(polygonMode, 2, "%d");
    if (memcmp(&before->backendModelview, &after->backendModelview, sizeof(gX_Matrix)) != 0) {
        if (!any) { gX_TraceLog(gX_LOG_WARNING, "GL_STATE_DIFF %s:", label); any = 1; }
        gX_TraceLog(gX_LOG_WARNING, "  backendModelview changed");
    }
    if (memcmp(&before->backendProjection, &after->backendProjection, sizeof(gX_Matrix)) != 0) {
        if (!any) { gX_TraceLog(gX_LOG_WARNING, "GL_STATE_DIFF %s:", label); any = 1; }
        gX_TraceLog(gX_LOG_WARNING, "  backendProjection changed");
    }

    #undef DIFF_FIELD
    #undef DIFF_ARRAY
}
