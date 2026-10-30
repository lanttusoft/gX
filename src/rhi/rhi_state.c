/**********************************************************************************************
*
*   [gX: src/rhi/rhi_state.c]
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


//opencode: RHI GL State — GL state management wrappers.
#include <stdio.h>
#include "rhi_state.h"
#include "rhi_gl.h"
#include "rhi_tex.h"
#include "rhi_caps.h"
#include "../common/log.h"

//opencode: RHI diagnostics — check cache consistency when enabled
static gX_I1 g_rhiDiagEnabled = 0;

//opencode: stage-dump gate — enables gX_GL_DumpState/gX_GL_CheckError from
//   renderer pipeline stages. Moved into RHI so the renderer no longer
//   depends on harness symbols (was defined in gfx_harness.c).
static gX_I1 g_rhiStageDumpEnabled = 0;

//opencode: per-frame GL error counter
static gX_U32 g_rhiFrameErrorCount = 0;

//opencode: state cache — skip redundant GL calls for common caps
static gX_U32 g_cacheBlendSrcRGB = (gX_U32)-1;
static gX_U32 g_cacheBlendDstRGB = (gX_U32)-1;
static gX_U32 g_cacheBlendSrcA   = (gX_U32)-1;
static gX_U32 g_cacheBlendDstA   = (gX_U32)-1;
//opencode: use a signed cache value so -1 remains an invalid sentinel instead
//   of converting to true through gX_I1 (bool) and suppressing depth-mask writes.
static gX_I32 g_cacheDepthWrite  = -1;
static gX_U32 g_cacheCullMode    = (gX_U32)-1;
static gX_U32 g_cacheDepthFunc   = (gX_U32)-1;
static gX_U32 g_cachePolyMode[2] = {(gX_U32)-1, (gX_U32)-1};

#define GX_RHI_CAP_CACHE_COUNT 6
static struct { gX_U32 cap; gX_I1 enabled; } g_capCache[GX_RHI_CAP_CACHE_COUNT];
static gX_I1 g_capCacheValid = 0;

static void gX_PrimeCapCache(gX_I0) {
    g_capCache[0].cap = GL_BLEND;           g_capCache[0].enabled = glIsEnabled(GL_BLEND);
    g_capCache[1].cap = GL_DEPTH_TEST;      g_capCache[1].enabled = glIsEnabled(GL_DEPTH_TEST);
    g_capCache[2].cap = GL_CULL_FACE;       g_capCache[2].enabled = glIsEnabled(GL_CULL_FACE);
    g_capCache[3].cap = GL_SCISSOR_TEST;    g_capCache[3].enabled = glIsEnabled(GL_SCISSOR_TEST);
    g_capCache[4].cap = GL_STENCIL_TEST;    g_capCache[4].enabled = glIsEnabled(GL_STENCIL_TEST);
    g_capCache[5].cap = GL_FRAMEBUFFER_SRGB; g_capCache[5].enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    g_capCacheValid = 1;
}

static gX_I1 *gX_FindCapCache(gX_U32 cap) {
    for (gX_U32 i = 0; i < GX_RHI_CAP_CACHE_COUNT; i++) {
        if (g_capCache[i].cap == cap) return &g_capCache[i].enabled;
    }
    return NULL;
}

gX_I0 gX_RHI_InvalidateCaches(gX_I0) {
    g_cacheBlendSrcRGB = (gX_U32)-1;
    g_cacheBlendDstRGB = (gX_U32)-1;
    g_cacheBlendSrcA   = (gX_U32)-1;
    g_cacheBlendDstA   = (gX_U32)-1;
    g_cacheDepthWrite  = -1;
    g_cacheCullMode    = (gX_U32)-1;
    g_cacheDepthFunc   = (gX_U32)-1;
    g_cachePolyMode[0] = (gX_U32)-1;
    g_cachePolyMode[1] = (gX_U32)-1;
    g_capCacheValid = 0;
}

gX_I0 gX_RHI_Enable(gX_U32 cap)
{
    if (!g_capCacheValid) gX_PrimeCapCache();
    gX_I1 *cached = gX_FindCapCache(cap);
    if (cached) {
        if (*cached) return; // already enabled — skip
        *cached = 1;
    }
    glEnable(cap);
}

gX_I0 gX_RHI_Disable(gX_U32 cap)
{
    if (!g_capCacheValid) gX_PrimeCapCache();
    gX_I1 *cached = gX_FindCapCache(cap);
    if (cached) {
        if (!*cached) return; // already disabled — skip
        *cached = 0;
    }
    glDisable(cap);
}

gX_I1 gX_RHI_IsEnabled(gX_U32 cap)
{
    gX_I1 val = (gX_I1)glIsEnabled(cap);
    if (!g_capCacheValid) gX_PrimeCapCache();
    gX_I1 *cached = gX_FindCapCache(cap);
    if (cached) *cached = val;
    return val;
}

gX_I0 gX_RHI_Scissor(gX_I32 x, gX_I32 y, gX_I32 w, gX_I32 h)
{
    glScissor(x, y, w, h);
}

gX_I0 gX_RHI_Blend_Mode(gX_I32 mode)
{
    //opencode: route through the cached wrappers so the cap + blend func caches
    //   stay consistent. Raw glEnable/glBlendFunc here previously desynced the
    //   caches, letting later cached calls skip needed state changes.
    switch (mode)
    {
        case gX_RHI_BLEND_ALPHA:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gX_RHI_Blend_Equation(GL_FUNC_ADD);
            break;
        case gX_RHI_BLEND_ADDITIVE:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_SRC_ALPHA, GL_ONE,
                GL_SRC_ALPHA, GL_ONE);
            gX_RHI_Blend_Equation(GL_FUNC_ADD);
            break;
        case gX_RHI_BLEND_MULTIPLIED:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA,
                GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            gX_RHI_Blend_Equation(GL_FUNC_ADD);
            break;
        case gX_RHI_BLEND_ADD_COLORS:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_ONE, GL_ONE,
                GL_ONE, GL_ONE);
            gX_RHI_Blend_Equation(GL_FUNC_ADD);
            break;
        case gX_RHI_BLEND_SUBTRACT_COLORS:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_ONE, GL_ONE,
                GL_ONE, GL_ONE);
            gX_RHI_Blend_Equation(GL_FUNC_SUBTRACT);
            break;
        case gX_RHI_BLEND_ALPHA_PREMULTIPLY:
            gX_RHI_Enable(GL_BLEND);
            gX_RHI_Blend_Func_Separate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            gX_RHI_Blend_Equation(GL_FUNC_ADD);
            break;
        case gX_RHI_BLEND_CUSTOM:
        case gX_RHI_BLEND_CUSTOM_SEPARATE:
            gX_RHI_Enable(GL_BLEND);
            break;
        default:
            gX_RHI_Disable(GL_BLEND);
            break;
    }
}

gX_I0 gX_RHI_Blend_Func_Separate(gX_U32 srcRGB, gX_U32 dstRGB,
    gX_U32 srcAlpha, gX_U32 dstAlpha)
{
    if (srcRGB == g_cacheBlendSrcRGB && dstRGB == g_cacheBlendDstRGB &&
        srcAlpha == g_cacheBlendSrcA && dstAlpha == g_cacheBlendDstA) return;
    g_cacheBlendSrcRGB = srcRGB; g_cacheBlendDstRGB = dstRGB;
    g_cacheBlendSrcA = srcAlpha; g_cacheBlendDstA = dstAlpha;
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
}

gX_I0 gX_RHI_Blend_Equation(gX_U32 mode)
{
    glBlendEquation(mode);
}

gX_I0 gX_RHI_Cull_Mode(gX_U32 mode)
{
    if (mode == g_cacheCullMode) return;
    g_cacheCullMode = mode;
    glCullFace(mode);
}

gX_I0 gX_RHI_FrontFace(gX_U32 mode)
{
    glFrontFace(mode);
}

gX_I0 gX_RHI_Blend_Equation_Separate(gX_U32 modeRGB, gX_U32 modeAlpha)
{
    glBlendEquationSeparate(modeRGB, modeAlpha);
}

gX_I0 gX_RHI_Color_Mask(gX_I1 r, gX_I1 g, gX_I1 b, gX_I1 a)
{
    glColorMask(r, g, b, a);
}

gX_I0 gX_RHI_Depth_Write(gX_I1 enable)
{
    if (enable == g_cacheDepthWrite) return;
    g_cacheDepthWrite = enable;
    glDepthMask(enable);
}

gX_I0 gX_RHI_Stencil_Mask(gX_U32 mask)
{
    //opencode: expose stencil write-mask control to complete render clears.
    glStencilMask((GLuint)mask);
}

gX_I0 gX_RHI_Depth_Func(gX_U32 func)
{
    if (func == g_cacheDepthFunc) return;
    g_cacheDepthFunc = func;
    glDepthFunc((GLenum)func);
}

gX_I0 gX_RHI_PolygonMode(gX_U32 face, gX_U32 mode)
{
    //opencode: glPolygonMode is desktop-only — GLES has no wireframe mode
    //   (always fill). No-op on ES so wireframe debug rendering degrades
    //   gracefully instead of leaving GL_INVALID_ENUM errors.
    if (gX_RHI_GetCaps()->isGLES) return;
    gX_U32 idx = (face == GL_FRONT) ? 0 : 1;
    if (mode == g_cachePolyMode[idx]) return;
    g_cachePolyMode[idx] = mode;
    glPolygonMode((GLenum)face, (GLenum)mode);
}

gX_I0 gX_RHI_ReadPixelsDoneFlag(gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_U32 format, gX_U32 type, void *pixels, gX_U8 *doneFlag)
{
    gX_RHI_ReadPixels(x, y, w, h, format, type, pixels);
    if (doneFlag) *doneFlag = 1;
}

gX_I0 gX_RHI_ReadPixels(gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_U32 format, gX_U32 type, void *pixels)
{
    if (!pixels || w == 0 || h == 0) return;
    glReadPixels((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h,
        (GLenum)format, (GLenum)type, pixels);
}

gX_I0 gX_RHI_GetIntegerv(gX_U32 pname, gX_I32 *data)
{
    if (!data) return;
    glGetIntegerv((GLenum)pname, data);
}

gX_I0 gX_RHI_GetBooleanv(gX_U32 pname, gX_I1 *data)
{
    if (!data) return;
    GLboolean val = GL_FALSE;
    glGetBooleanv((GLenum)pname, &val);
    *data = (val == GL_TRUE) ? 1 : 0;
}

gX_U32 gX_RHI_GetError(gX_I0)
{
    gX_U32 err = (gX_U32)glGetError();
    if (err != GL_NO_ERROR) g_rhiFrameErrorCount++;
    return err;
}

gX_U32 gX_RHI_GetAndResetErrorCount(gX_I0)
{
    gX_U32 count = g_rhiFrameErrorCount;
    g_rhiFrameErrorCount = 0;
    return count;
}

gX_I0 gX_RHI_ResetErrorCount(gX_I0)
{
    g_rhiFrameErrorCount = 0;
}

gX_I32 gX_RHI_DumpState(gX_I8 *buf, gX_U64 bufSize, const gX_I8 *label)
{
    gX_I32 curProg, drawFbo, readFbo, vp[4];
    gX_I32 blendSrcRGB, blendDstRGB, blendSrcA, blendDstA;
    gX_I32 cullFaceMode, frontFace, depthFunc, depthWr, colorMask[4];
    gX_I32 activeTexUnit, tex0binding, polyMode[2];
    gX_RHI_GetIntegerv(GL_CURRENT_PROGRAM, &curProg);
    gX_RHI_GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    gX_RHI_GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    gX_RHI_GetIntegerv(GL_VIEWPORT, vp);
    gX_RHI_GetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
    gX_RHI_GetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
    gX_RHI_GetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
    gX_RHI_GetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
    gX_RHI_GetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
    gX_RHI_GetIntegerv(GL_FRONT_FACE, &frontFace);
    gX_RHI_GetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    gX_RHI_GetIntegerv(GL_DEPTH_WRITEMASK, &depthWr);
    gX_RHI_GetIntegerv(GL_COLOR_WRITEMASK, colorMask);
    gX_RHI_GetIntegerv(GL_ACTIVE_TEXTURE, &activeTexUnit);
    gX_RHI_GetIntegerv(GL_TEXTURE_BINDING_2D, &tex0binding);
    gX_RHI_GetIntegerv(GL_POLYGON_MODE, polyMode);

    gX_I1 depthTest = gX_RHI_IsEnabled(GL_DEPTH_TEST);
    gX_I1 blend     = gX_RHI_IsEnabled(GL_BLEND);
    gX_I1 cullFace  = gX_RHI_IsEnabled(GL_CULL_FACE);
    gX_I1 scissor   = gX_RHI_IsEnabled(GL_SCISSOR_TEST);
    gX_I1 stencil   = gX_RHI_IsEnabled(GL_STENCIL_TEST);

    gX_U32 glErr = gX_RHI_GetError();

    gX_I32 written = 0;
    if (buf && bufSize > 0) {
        written = snprintf(buf, bufSize,
            "GL[%s]: prog=%d drawFbo=%d readFbo=%d vp=(%d,%d,%d,%d)\n"
            "  blend=(%d,%d,%d,%d) depth=%d blend=%d cull=%d\n"
            "  cullMode=%d frontFace=%d depthFunc=%d depthWr=%d\n"
            "  colMask=(%d,%d,%d,%d) scissor=%d stencil=%d polyMode=(%d,%d)\n"
            "  activeTexUnit=%d tex0=%d glErr=0x%x",
            label ? label : "RHI",
            curProg, drawFbo, readFbo, vp[0], vp[1], vp[2], vp[3],
            blendSrcRGB, blendDstRGB, blendSrcA, blendDstA,
            depthTest, blend, cullFace,
            cullFaceMode, frontFace, depthFunc, depthWr,
            colorMask[0], colorMask[1], colorMask[2], colorMask[3],
            scissor, stencil, polyMode[0], polyMode[1],
            activeTexUnit, tex0binding, glErr);
    } else {
        gX_TraceLog(gX_LOG_INFO,
            "RHI_DumpState[%s]: prog=%d fbo(d=%d,r=%d) vp=(%d,%d,%d,%d) "
            "blend=(%d,%d,%d,%d) depth=%d blend=%d cull=%d "
            "cullMode=%d frontFace=%d depthFunc=%d depthWr=%d "
            "colMask=(%d,%d,%d,%d) scissor=%d stencil=%d polyMode=(%d,%d) "
            "activeTex=%d tex0=%d glErr=0x%x",
            label ? label : "RHI",
            curProg, drawFbo, readFbo, vp[0], vp[1], vp[2], vp[3],
            blendSrcRGB, blendDstRGB, blendSrcA, blendDstA,
            depthTest, blend, cullFace,
            cullFaceMode, frontFace, depthFunc, depthWr,
            colorMask[0], colorMask[1], colorMask[2], colorMask[3],
            scissor, stencil, polyMode[0], polyMode[1],
            activeTexUnit, tex0binding, glErr);
    }
    return written;
}

gX_I0 gX_RHI_Diagnostics_SetEnabled(gX_I1 enabled) {
    g_rhiDiagEnabled = enabled;
}

gX_I1 gX_RHI_Diagnostics_IsEnabled(gX_I0) {
    return g_rhiDiagEnabled;
}

gX_I0 gX_RHI_Diagnostics_Check(const gX_I8 *when) {
    if (!g_rhiDiagEnabled) return;
    if (!g_capCacheValid) return;
    gX_I32 mismatches = 0;
    //opencode: compare cap cache with real GL state
    for (gX_U32 i = 0; i < GX_RHI_CAP_CACHE_COUNT; i++) {
        gX_I1 real = (gX_I1)glIsEnabled(g_capCache[i].cap);
        if (real != g_capCache[i].enabled) {
            gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: cap cache mismatch cap=0x%x cached=%d real=%d",
                when ? when : "?", g_capCache[i].cap, g_capCache[i].enabled, real);
            g_capCache[i].enabled = real; // fix cache
            mismatches++;
        }
    }
    //opencode: compare blend func cache with real GL state
    gX_I32 realSrc, realDst, realSrcA, realDstA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &realSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &realDst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &realSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &realDstA);
    if ((gX_U32)realSrc != g_cacheBlendSrcRGB || (gX_U32)realDst != g_cacheBlendDstRGB ||
        (gX_U32)realSrcA != g_cacheBlendSrcA || (gX_U32)realDstA != g_cacheBlendDstA) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: blend cache mismatch "
            "cached=(%u,%u,%u,%u) real=(%d,%d,%d,%d)",
            when ? when : "?",
            g_cacheBlendSrcRGB, g_cacheBlendDstRGB, g_cacheBlendSrcA, g_cacheBlendDstA,
            realSrc, realDst, realSrcA, realDstA);
        g_cacheBlendSrcRGB = (gX_U32)realSrc;
        g_cacheBlendDstRGB = (gX_U32)realDst;
        g_cacheBlendSrcA = (gX_U32)realSrcA;
        g_cacheBlendDstA = (gX_U32)realDstA;
        mismatches++;
    }
    //opencode: compare depth func cache
    gX_I32 realDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &realDepthFunc);
    if ((gX_U32)realDepthFunc != g_cacheDepthFunc) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: depthFunc cache mismatch cached=%u real=%d",
            when ? when : "?", g_cacheDepthFunc, realDepthFunc);
        g_cacheDepthFunc = (gX_U32)realDepthFunc;
        mismatches++;
    }
    //opencode: compare cull mode cache
    gX_I32 realCullMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &realCullMode);
    if ((gX_U32)realCullMode != g_cacheCullMode) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: cullMode cache mismatch cached=%u real=%d",
            when ? when : "?", g_cacheCullMode, realCullMode);
        g_cacheCullMode = (gX_U32)realCullMode;
        mismatches++;
    }
    //opencode: compare depth write cache
    gX_I32 realDepthWr;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &realDepthWr);
    if ((gX_I1)realDepthWr != g_cacheDepthWrite) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: depthWrite cache mismatch cached=%d real=%d",
            when ? when : "?", g_cacheDepthWrite, realDepthWr);
        g_cacheDepthWrite = (gX_I1)realDepthWr;
        mismatches++;
    }
    if (mismatches > 0) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_DIAG [%s]: %d cache mismatch(es) detected and corrected",
            when ? when : "?", mismatches);
    }
}

//opencode: enable/disable renderer pipeline-stage state dumps.
gX_I0 gX_RHI_StageDump_SetEnabled(gX_I1 enabled) {
    g_rhiStageDumpEnabled = enabled;
}

gX_I1 gX_RHI_StageDump_IsEnabled(gX_I0) {
    return g_rhiStageDumpEnabled;
}

//opencode: dump GL state at a named pipeline stage (moved from the gfx harness).
//   Gated by gX_RHI_StageDump_SetEnabled — called from gfx_pipeline.c stages
//   without depending on harness symbols.
gX_I0 gX_GL_DumpState(const gX_I8 *stage) {
    if (!g_rhiStageDumpEnabled) return;
    gX_I8 buf[512];
    gX_RHI_DumpState(buf, sizeof(buf), stage);
    gX_TraceLog(gX_LOG_INFO, "%s", buf);
    //opencode: extended texture-unit diagnostics via the shadow table — replaces
    //   the harness's hardcoded units 14/15/18 (stale unit numbers).
    gX_I8 tb[512];
    gX_I32 tlen = 0;
    for (gX_U32 ui = 0; ui < GX_RHI_MAX_TEX_UNITS; ui++) {
        gX_U32 tid = gX_RHI_Tex_GetBoundOnUnit(ui);
        if (tid == 0) continue;
        const gX_I8 *valid = glIsTexture((GLuint)tid) ? "ok" : "INVALID";
        tlen += snprintf(tb + tlen, sizeof(tb) - (gX_U64)tlen, " [%u]=%u(%s)", ui, tid, valid);
        if (tlen > (gX_I32)(sizeof(tb) - 128)) break;
    }
    if (tlen > 0) gX_TraceLog(gX_LOG_INFO, "GL_STATE_DUMP[%s] texUnits:%s", stage ? stage : "?", tb);
}

//opencode: quick GL error check at a named code location (moved from the gfx harness).
gX_I0 gX_GL_CheckError(const gX_I8 *location) {
    if (!g_rhiStageDumpEnabled) return;
    gX_U32 err = gX_RHI_GetError();
    if (err != GL_NO_ERROR) gX_TraceLog(gX_LOG_WARNING, "GL error 0x%x at %s", err, location ? location : "?");
}
