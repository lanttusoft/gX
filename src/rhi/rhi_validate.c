/**********************************************************************************************
*
*   [gX: src/rhi/rhi_validate.c]
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


//opencode: RHI Validation — per-draw GL state validation.
#include "rhi_validate.h"
#include "rhi_debug.h"
#include "rhi_gl.h"
#include "rhi_fbo.h"
#include "rhi_tex.h"
#include "../common/log.h"
#include <math.h>

// ─── Global enable flag ───
gX_I1 g_rhiValidateEnabled = 0;

// ─── NaN check helpers ───
gX_I1 gX_RHI_Validate_IsNaN(gX_F32 value)
{
#ifdef _MSC_VER
    return _isnan(value) ? 1 : 0;
#else
    return isnan(value) ? 1 : 0;
#endif
}

gX_I1 gX_RHI_Validate_IsNaNv(const gX_F32 *values, gX_U32 count)
{
    if (!values) return 0;
    for (gX_U32 i = 0; i < count; i++) {
        if (gX_RHI_Validate_IsNaN(values[i])) return 1;
    }
    return 0;
}

// ─── Check program validity ───
static gX_I0 gX_Validate_Program(gX_U32 programId)
{
    if (programId == 0) {
        //opencode: error hints - validation warnings carry their recipe.
        const gX_I8 *hint = gX_RHI_Hint_For("program=0");
        if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: program=0 — draw will be silently skipped | %s", hint);
        else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: program=0 — draw will be silently skipped");
        return;
    }
    if (!glIsProgram((GLuint)programId)) {
        const gX_I8 *hint = gX_RHI_Hint_For("not a valid GL program");
        if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: program %u is not a valid GL program (deleted?) | %s", programId, hint);
        else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: program %u is not a valid GL program (deleted?)", programId);
    }
}

// ─── Check FBO completeness ───
static gX_I0 gX_Validate_FBO(gX_U32 drawFbo)
{
    if (drawFbo == 0) return; // default framebuffer — always complete
    if (gX_RHI_FBO_IsComplete()) return;
    // Query attachments for diagnostics
    GLint colorTex = 0, depthTex = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &colorTex);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
    const gX_I8 *fboHint = gX_RHI_Hint_For("INCOMPLETE");
    if (fboHint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: FBO %u INCOMPLETE (color=%d depth=%d) | %s", drawFbo, colorTex, depthTex, fboHint);
    else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: FBO %u INCOMPLETE (color=%d depth=%d)", drawFbo, colorTex, depthTex);
}

// ─── Check viewport vs FBO size ───
static gX_I0 gX_Validate_Viewport(gX_U32 drawFbo, const gX_I8 *label)
{
    gX_I32 vp[4] = {0};
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) {
        const gX_I8 *hint = gX_RHI_Hint_For("degenerate");
        if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: viewport %dx%d is degenerate | %s", vp[2], vp[3], hint);
        else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: viewport %dx%d is degenerate", vp[2], vp[3]);
        return;
    }
    if (drawFbo == 0) return; // default FB — no size comparison

    // Query color attachment texture ID, then its dimensions AT THE
    // ATTACHED LEVEL.
    //opencode: Phase 1b — level-aware + lossless error handling. The old
    //   code always queried level 0 as TEXTURE_2D, which false-alarmed on
    //   (a) multisample resolve targets (bind fails → stale 1x1 dims — the
    //   "viewport != 1x1" ghost hunted 2026-09-07) and (b) HiZ mip levels
    //   (viewport matches the attached level, not level 0). Probe errors
    //   (multisample/cubemap binds) are swallowed; a PRE-EXISTING error is
    //   re-reported here (the census already ran) so the probes never mask
    //   a real one.
    //opencode: Step-3-bis — target-aware: binding a cubemap face or a
    //   multisample texture as TEXTURE_2D queues GL_INVALID_OPERATION from
    //   the PROBE itself (self-sustaining chain: every PreDraw reported the
    //   previous probe's error — 56/64 trace hits, NVIDIA GL_DEBUG
    //   "Target doesn't match the texture's target"). Non-2D attachments
    //   skip silently (registry lookup, no GL call when unknown).
    GLenum preErr = glGetError();
    GLint colorTex = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &colorTex);
    if (colorTex > 0) {
        gX_U32 attTarget = gX_RHI_Tex_GetTarget((gX_U32)colorTex);
        if (attTarget != 0 && attTarget != (gX_U32)GL_TEXTURE_2D) {
            if (preErr != GL_NO_ERROR) {
                const gX_I8 *hint = gX_RHI_Hint_For("pending GL error");
            if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s' | %s", preErr, label ? label : "?", hint);
            else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s'", preErr, label ? label : "?");
            }
            return;
        }
        GLint texLevel = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &texLevel);
        glBindTexture(GL_TEXTURE_2D, (GLuint)colorTex);
        GLint fboW = 0, fboH = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, texLevel, GL_TEXTURE_WIDTH, &fboW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, texLevel, GL_TEXTURE_HEIGHT, &fboH);
        glBindTexture(GL_TEXTURE_2D, 0);
        GLenum probeErr = glGetError();
        if (preErr != GL_NO_ERROR) {
            const gX_I8 *hint = gX_RHI_Hint_For("pending GL error");
            if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s' | %s", preErr, label ? label : "?", hint);
            else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s'", preErr, label ? label : "?");
        } else if (probeErr == GL_NO_ERROR && (vp[2] != fboW || vp[3] != fboH)) {
            const gX_I8 *hint = gX_RHI_Hint_For("viewport");
            if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: viewport %dx%d != FBO %u color tex %dx%d | %s", vp[2], vp[3], drawFbo, fboW, fboH, hint);
            else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: viewport %dx%d != FBO %u color tex %dx%d", vp[2], vp[3], drawFbo, fboW, fboH);
        }
    } else if (preErr != GL_NO_ERROR) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s'", preErr, label ? label : "?");
    }
}

// ─── Check bound texture validity ───
static gX_I0 gX_Validate_Textures(gX_I0)
{
    //opencode: iterate all engine texture units (up to 31) — the engine binds
    //   shadow maps at 8, IBL at 16, CSM at 22, cookies at 23, etc. The old
    //   loop only checked 0-7, missing those.
    for (gX_U32 ui = 0; ui < GX_RHI_MAX_TEX_UNITS; ui++) {
        gX_U32 tid = gX_RHI_Tex_GetBoundOnUnit(ui);
        if (tid == 0) continue;
        if (!glIsTexture((GLuint)tid)) {
            const gX_I8 *hint = gX_RHI_Hint_For("deleted texture");
            if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: unit %u bound to invalid tex %u (deleted?) | %s", ui, tid, hint);
            else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: unit %u bound to invalid tex %u (deleted?)", ui, tid);
        }
    }
}

// ─── NaN injection test — deliberately set float uniforms to NaN ───
//   Used to verify that NaN detection works. Only runs when validation is enabled.
gX_I0 gX_RHI_Validate_InjectNaN(gX_U32 program)
{
    if (program == 0 || !g_rhiValidateEnabled) return;
    gX_I32 activeCount = 0;
    glGetProgramiv((GLuint)program, GL_ACTIVE_UNIFORMS, &activeCount);
    gX_U32 injected = 0;
    for (gX_I32 i = 0; i < activeCount; i++) {
        gX_I8 name[256] = {0};
        GLenum glType = 0;
        GLint glSize = 0;
        glGetActiveUniform((GLuint)program, (GLuint)i, (GLsizei)sizeof(name), NULL, &glSize, &glType, (GLchar *)name);
        gX_I32 loc = glGetUniformLocation((GLuint)program, name);
        if (loc < 0) continue;
        switch (glType) {
            case GL_FLOAT:
            case GL_FLOAT_VEC2:
            case GL_FLOAT_VEC3:
            case GL_FLOAT_VEC4:
            case GL_FLOAT_MAT4: {
                gX_F32 nanVal = NAN;
                gX_TraceLog(gX_LOG_WARNING, "RHI_NAN_INJECT: '%s' set to NaN (loc=%d)", name, loc);
                glUniform1f((GLint)loc, nanVal);
                injected++;
                break;
            }
            default: break;
        }
    }
    gX_TraceLog(gX_LOG_INFO, "RHI_NAN_INJECT: injected NaN into %u of %d active uniforms", injected, activeCount);
}

// ─── Per-draw validation entry point ───
gX_I0 gX_RHI_Validate_PreDraw(const gX_I8 *label)
{
    if (!g_rhiValidateEnabled) return;

    GLint curProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);

    gX_RHI_FBO drawFbo = gX_RHI_GetDrawFBO();

    //opencode: Phase 1b — census FIRST: the checks below may consume GL
    //   errors from their own probe queries (multisample/cubemap binds);
    //   report pre-existing errors before they can be masked.
    GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        const gX_I8 *hint = gX_RHI_Hint_For("pending GL error");
        if (hint) gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s' | %s", glErr, label ? label : "?", hint);
        else gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: pending GL error 0x%x before draw '%s'", glErr, label ? label : "?");
    }

    gX_Validate_Program((gX_U32)curProg);
    gX_Validate_FBO((gX_U32)drawFbo);
    gX_Validate_Viewport((gX_U32)drawFbo, label);
    gX_Validate_Textures();
}
