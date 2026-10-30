/**********************************************************************************************
*
*   [gX: src/rhi/rhi_fbo.c]
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


//opencode: RHI FBO — centralized framebuffer management.
//   Tracks draw/read FBO state internally so callers don't mix
//   raylib's rlgl tracker with raw GL calls.
#include "rhi_fbo.h"
#include "rhi.h"
#include "rhi_debug.h"
#include "rhi_gl.h"
#include "rhi_caps.h"
#include "rhi_buffer_audit.h"
#include "rhi_tex.h"
#include "../common/log.h"
#include "../common/macros.h"

static gX_RHI_FBO g_drawFbo = 0;
static gX_RHI_FBO g_readFbo = 0;

gX_RHI_FBO gX_RHI_FBO_Create(gX_U32 colorTex, gX_U32 depthTex, const gX_I8 *name) {
    gX_U32 fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (colorTex) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        //opencode: explicit draw/read buffers — a recycled FBO name may retain
        //   stale draw-buffer state (e.g. GL_NONE from a previous colorless
        //   FBO on this name), making later glClear raise
        //   GL_INVALID_FRAMEBUFFER_OPERATION despite a complete framebuffer.
        //   glDrawBuffer (singular) does not exist in GLES — ES has only
        //   glDrawBuffers, so use the count-1 form (glDrawBuffer is a NULL
        //   function pointer under the GLES loader and would crash).
        GLenum one = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &one);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        //opencode: colorless FBO — GL_NONE read/draw. glDrawBuffer singular is
        //   desktop-only; glReadBuffer(GL_NONE) is ES 3.2+ only (ES 3.0/3.1
        //   accept GL_BACK/GL_COLOR_ATTACHMENTn, so on older ES this is skipped
        //   rather than raising GL_INVALID_ENUM).
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        if (!gX_RHI_GetCaps()->isGLES) glReadBuffer(GL_NONE);
    }
    if (depthTex) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        gX_TraceLog(gX_LOG_ERROR, "RHI_FBO: FBO incomplete (status=0x%x, color=%u depth=%u) name='%s'", status, colorTex, depthTex, name ? name : "?");
        glDeleteFramebuffers(1, &fbo);
        g_drawFbo = 0;
        g_readFbo = 0;
        return 0;
    }
    gX_RHI_Debug_Label(fbo, GL_FRAMEBUFFER, name);
    g_drawFbo = fbo;
    g_readFbo = fbo;
    return fbo;
}
gX_I32 gX_RHI_FBO_Gen(gX_U32 count, gX_U32 *fbos) {
    if (count == 0 || !fbos) return -1;
    glGenFramebuffers((GLsizei)count, fbos);
    return 0;
}

gX_I1 gX_RHI_FBO_IsComplete(gX_I0) {
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    return (status == GL_FRAMEBUFFER_COMPLETE) ? 1 : 0;
}

//opencode: hunt - attachment forensics helper (see header note)
gX_U32 gX_RHI_FBO_ColorName(gX_RHI_FBO fbo) {
    //ai: BeginTexture invalidates the cache; preserve the real binding, never the sentinel.
    gX_RHI_FBO prev = gX_RHI_GetDrawFBOResolved();
    gX_RHI_BindDrawFBO(fbo);
    GLint pname = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &pname);
    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    gX_TraceLog(gX_LOG_NOTICE, "RHI_FBO: forensics fbo=%u color=%d complete=%d (0x%x)",
        (unsigned)fbo, (int)pname, (int)(status == GL_FRAMEBUFFER_COMPLETE), (unsigned)status);
    gX_RHI_BindDrawFBO(prev);
    return (gX_U32)pname;
}

//opencode: hunt - texture sample count (multisample textures are not
//   sampleable with sampler2D; reading one yields undefined values).
//   TEXTURE_SAMPLES is only queryable on multisample textures — asking a
//   single-sampled texture is INVALID_ENUM on strict drivers (NVIDIA
//   tolerates it), so answer 0 from the target registry without touching GL.
gX_U32 gX_RHI_Tex_Samples(gX_U32 tex) {
    if (gX_RHI_Tex_GetTarget(tex) != (gX_U32)gX_RHI_TEXTURE_2D_MULTISAMPLE) return 0;
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    GLint samples = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SAMPLES, &samples);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    return (gX_U32)samples;
}

gX_I0 gX_RHI_FBO_Delete(gX_RHI_FBO fbo) {
    if (fbo == 0) return;
    glDeleteFramebuffers(1, &fbo);
    if (g_drawFbo == fbo) g_drawFbo = 0;
    if (g_readFbo == fbo) g_readFbo = 0;
}

//opencode: validate cached and issued FBO binds when lifecycle tracing is enabled.
gX_I0 gX_RHI_BindFBO(gX_RHI_FBO fbo) {
    if (fbo == g_drawFbo && fbo == g_readFbo) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_BOTH, fbo,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    g_drawFbo = fbo;
    g_readFbo = fbo;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_BOTH, fbo,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_BindDrawFBO(gX_RHI_FBO fbo) {
    if (fbo == g_drawFbo) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_DRAW, fbo,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    g_drawFbo = fbo;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_DRAW, fbo,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_BindReadFBO(gX_RHI_FBO fbo) {
    if (fbo == g_readFbo) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_READ, fbo,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    g_readFbo = fbo;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_READ, fbo,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_UnbindFBO(gX_I0) {
    if (g_drawFbo == 0 && g_readFbo == 0) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_BOTH, 0,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_drawFbo = 0;
    g_readFbo = 0;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_BOTH, 0,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_UnbindDrawFBO(gX_I0) {
    if (g_drawFbo == 0) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_DRAW, 0,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g_drawFbo = 0;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_DRAW, 0,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_UnbindReadFBO(gX_I0) {
    if (g_readFbo == 0) {
        gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_READ, 0,
            g_drawFbo, g_readFbo, 0);
        return;
    }
    gX_RHI_IncFBOSwitch();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    g_readFbo = 0;
    gX_RHI_BufferAudit_FboCheck(GX_RHI_BUFFER_AUDIT_BIND_READ, 0,
        g_drawFbo, g_readFbo, 1);
}

gX_I0 gX_RHI_InvalidateFBO(gX_I0) {
    g_drawFbo = (gX_RHI_FBO)-1;
    g_readFbo = (gX_RHI_FBO)-1;
}

gX_RHI_FBO gX_RHI_GetDrawFBO(gX_I0) {
    //opencode: the cache holds -1 after gX_RHI_InvalidateFBO() — a stale
    //   sentinel is a GARBAGE GL name; return 0 (default framebuffer) instead
    //   so callers never bind an ungenerated name (e.g. the IBL restore path)
    return (g_drawFbo == (gX_RHI_FBO)-1) ? 0 : g_drawFbo;
}
gX_RHI_FBO gX_RHI_GetReadFBO(gX_I0) { return (g_readFbo == (gX_RHI_FBO)-1) ? 0 : g_readFbo; }

//opencode: resolved draw-FBO query — the cache holds the -1 sentinel after
//   InvalidateFBO, which GetDrawFBO reports as 0 (default). Callers that save
//   and restore the current target need the real binding, so read GL when the
//   cache is unknown.
gX_RHI_FBO gX_RHI_GetDrawFBOResolved(gX_I0) {
    if (g_drawFbo != (gX_RHI_FBO)-1) return g_drawFbo;
    GLint bound = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
    return (gX_RHI_FBO)bound;
}

#if defined(GAME_BACKEND_RAYLIB)
//opencode: after an RHI blit, re-assert the draw FBO binding. raylib 6.0's
//   rlEnableFramebuffer is literally glBindFramebuffer(GL_FRAMEBUFFER, id)
//   (no internal FBO state tracker exists in RLGL.State), so this is a plain
//   GL rebind that keeps subsequent raylib draw calls (DrawTexturePro in
//   gX_Texture_Draw) targeting the FBO the RHI left bound. Replaced the rlgl
//   call with the identical raw GL call so the RHI has no rlgl dependency.
static gX_I0 sync_rlgl_fbo(gX_I0) {
    GLint currentFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &currentFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)currentFbo);
    //opencode: Step-3-bis — GL_FRAMEBUFFER binds BOTH read+draw: record the
    //   clobbered read side too. Untracked, the second half of a split
    //   resolve skipped its binds on stale tracker state and silently
    //   self-blitted (dst->dst no-op): hdrScene.depth never resolved and the
    //   depth-driven SSR march starved on every driver. Caught via apitrace
    //   (missing READ bind before the depth blit).
    g_drawFbo = (gX_RHI_FBO)currentFbo;
    g_readFbo = (gX_RHI_FBO)currentFbo;
}
#endif

gX_I0 gX_RHI_Blit(gX_RHI_FBO src, gX_RHI_FBO dst,
                   gX_U32 srcX, gX_U32 srcY,
                   gX_U32 srcW, gX_U32 srcH,
                   gX_U32 dstX, gX_U32 dstY,
                   gX_U32 dstW, gX_U32 dstH,
                   gX_U32 mask, gX_U32 filter) {
    gX_RHI_BindReadFBO(src);
    gX_RHI_BindDrawFBO(dst);
    //opencode: track source/destination lifecycle across framebuffer blits.
    gX_RHI_BufferAudit_Blit(src, dst, srcX, srcY, srcW, srcH,
        dstX, dstY, dstW, dstH, mask, filter);
    glBlitFramebuffer((GLint)srcX, (GLint)srcY, (GLint)(srcX + srcW), (GLint)(srcY + srcH),
                      (GLint)dstX, (GLint)dstY, (GLint)(dstX + dstW), (GLint)(dstY + dstH),
                      (GLbitfield)mask, (GLenum)filter);
#if defined(GAME_BACKEND_RAYLIB)
    sync_rlgl_fbo();
#endif
}

gX_U32 gX_RHI_RBO_Create(gX_U32 w, gX_U32 h, gX_U32 internalFormat) {
    gX_U32 rbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, (GLenum)internalFormat, (GLsizei)w, (GLsizei)h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return rbo;
}

gX_I0 gX_RHI_RBO_Delete(gX_U32 rbo) {
    if (rbo == 0) return;
    glDeleteRenderbuffers(1, &rbo);
}

gX_I0 gX_RHI_FBO_AttachDepthRBO(gX_U32 rbo) {
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, (GLuint)rbo);
}

gX_I0 gX_RHI_FBO_AttachCubeFace(gX_U32 texId, gX_U32 face) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), (GLuint)texId, 0);
}

gX_I0 gX_RHI_FBO_AttachCubeFaceDepth(gX_U32 texId, gX_U32 face) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), (GLuint)texId, 0);
}

gX_I0 gX_RHI_FBO_AttachCubeFaceLevel(gX_U32 texId, gX_U32 face, gX_I32 level) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), (GLuint)texId, (GLint)level);
}

gX_I0 gX_RHI_FBO_AttachTexture(gX_U32 texId, gX_I32 attachment, gX_I32 target) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, (GLenum)attachment, (GLenum)target, (GLuint)texId, 0);
}

//opencode: attach a specific mip LEVEL of a 2D texture as a color attachment
gX_I0 gX_RHI_FBO_AttachTextureLevel(gX_U32 texId, gX_I32 attachment, gX_I32 target, gX_I32 level) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, (GLenum)attachment, (GLenum)target, (GLuint)texId, (GLint)level);
}

gX_I0 gX_RHI_DrawBuffer(gX_U32 buf) {
    //opencode: glDrawBuffer (singular) is desktop-only — ES has only
    //   glDrawBuffers. Emit the count-1 form so this wrapper is ES-safe
    //   (glDrawBuffer is a NULL pointer under the GLES loader).
    glDrawBuffers(1, (const GLenum *)&buf);
}

gX_I0 gX_RHI_ReadBuffer(gX_U32 buf) {
    //opencode: glReadBuffer(GL_NONE) is ES 3.2+ only — ES 3.0/3.1 accept
    //   GL_BACK/GL_COLOR_ATTACHMENTn. Guard so older ES doesn't error.
    if (buf == GL_NONE && gX_RHI_GetCaps()->isGLES) {
        if (gX_RHI_GetCaps()->glMinor >= 2) glReadBuffer((GLenum)buf);
        return;
    }
    glReadBuffer((GLenum)buf);
}

gX_I0 gX_RHI_DrawBuffers(gX_I32 count, const gX_U32 *bufs) {
    glDrawBuffers((GLsizei)count, (const GLenum *)bufs);
}

gX_I0 gX_RHI_Viewport(gX_I32 x, gX_I32 y, gX_I32 w, gX_I32 h) {
    glViewport((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h);
}

gX_I0 gX_RHI_Clear(gX_U32 mask) {
    //opencode: audit attachment and write-mask clear coverage before issuing GL.
    gX_RHI_BufferAudit_Clear(mask);
    glClear((GLbitfield)mask);
}

gX_I0 gX_RHI_ClearColor(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a) {
    glClearColor(r, g, b, a);
}

gX_I0 gX_RHI_FBO_AttachLayer(gX_U32 texId, gX_I32 attachment, gX_I32 level, gX_I32 layer) {
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, (GLenum)attachment,
        (GLuint)texId, (GLint)level, (GLint)layer);
}
