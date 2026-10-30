/**********************************************************************************************
*
*   [gX: src/rhi/rhi_buffer_audit.c]
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


//opencode: opt-in render-target lifecycle audit for FBO ownership, clear coverage,
//   producer/consumer order, and RHI framebuffer-cache mismatches.
#include "rhi_buffer_audit.h"

#include "rhi_gl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_STENCIL_BITS
#define GL_STENCIL_BITS 0x0D57
#endif

#define GX_RHI_BUFFER_AUDIT_TARGET_MAX 256
#define GX_RHI_BUFFER_AUDIT_NAME_SIZE 64
#define GX_RHI_BUFFER_AUDIT_PASS_SIZE 48
#define GX_RHI_BUFFER_AUDIT_SCOPE_MAX 32

typedef struct {
    gX_U32 fbo;
    gX_U32 colorTexture;
    gX_U32 depthTexture;
    gX_U32 width;
    gX_U32 height;
    gX_U64 lastColorClearFrame;
    gX_U64 lastDepthClearFrame;
    gX_U32 passDraws;
    gX_U32 passTextureBinds;
    gX_U32 passTargetBegins;
    gX_U32 passClearMask;
    gX_U32 frameDraws;
    gX_U32 frameTextureBinds;
    gX_U32 frameTargetBegins;
    gX_U32 frameClearMask;
    gX_I1 colorInitialized;
    gX_I1 depthInitialized;
    gX_I1 colorWritten;
    gX_I1 depthWritten;
    gX_I8 name[GX_RHI_BUFFER_AUDIT_NAME_SIZE];
} gX_RHI_BufferAuditTarget;

static gX_RHI_BufferAuditTarget *s_targets = NULL;
static gX_U32 s_targetCount = 0;
static gX_U32 s_targetScopes[GX_RHI_BUFFER_AUDIT_SCOPE_MAX];
static gX_I32 s_targetScopeDepth = 0;
static FILE *s_auditStream = NULL;
static gX_I1 s_auditChecked = 0;
static gX_I1 s_auditEnabled = 0;
static gX_U64 s_frame = 0;
static gX_I8 s_pass[GX_RHI_BUFFER_AUDIT_PASS_SIZE] = "startup";

static gX_I0 BufferAudit_Init(gX_I0)
{
    if (s_auditChecked) return;
    s_auditChecked = 1;
    const gX_I8 *hook = getenv("GX_GFX_BUFFER_AUDIT");
    if (!hook || !hook[0] || hook[0] == '0') return;
    s_auditEnabled = 1;
    if (!strcmp((const char *)hook, "1") || !strcmp((const char *)hook, "stderr")) {
        s_auditStream = stderr;
    } else {
        s_auditStream = fopen((const char *)hook, "w");
        if (!s_auditStream) {
            s_auditEnabled = 0;
            fprintf(stderr, "GFX_BUFFER_AUDIT: cannot open %s\n", (const char *)hook);
            return;
        }
    }
    s_targets = (gX_RHI_BufferAuditTarget *)calloc(
        GX_RHI_BUFFER_AUDIT_TARGET_MAX, sizeof(*s_targets));
    if (!s_targets) {
        s_auditEnabled = 0;
        fprintf(stderr, "GFX_BUFFER_AUDIT: target registry allocation failed\n");
        if (s_auditStream != stderr) fclose(s_auditStream);
        s_auditStream = NULL;
        return;
    }
    fprintf(s_auditStream, "GFX_BUFFER_AUDIT enabled destination=%s\n",
        s_auditStream == stderr ? "stderr" : (const char *)hook);
    fflush(s_auditStream);
}

static gX_I0 BufferAudit_Emit(const gX_I8 *format, ...)
{
    BufferAudit_Init();
    if (!s_auditEnabled || !s_auditStream) return;
    fprintf(s_auditStream, "GFX_BUFFER_AUDIT frame=%llu pass=%s ",
        (unsigned long long)s_frame, (const char *)s_pass);
    va_list args;
    va_start(args, format);
    vfprintf(s_auditStream, (const char *)format, args);
    va_end(args);
    fputc('\n', s_auditStream);
}

static gX_I32 BufferAudit_FindTarget(gX_U32 fbo)
{
    if (fbo == 0) return -1;
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        if (s_targets[i].fbo == fbo) return (gX_I32)i;
    }
    return -1;
}

static const gX_I8 *BufferAudit_TargetName(gX_U32 fbo)
{
    gX_I32 index = BufferAudit_FindTarget(fbo);
    if (index < 0) return (const gX_I8 *)"unregistered";
    return s_targets[index].name;
}

static gX_I0 BufferAudit_MarkWrite(gX_U32 fbo, gX_U32 mask, gX_I1 fullWrite)
{
    gX_I32 index = BufferAudit_FindTarget(fbo);
    if (index < 0) return;
    gX_RHI_BufferAuditTarget *target = &s_targets[index];
    if ((mask & GL_COLOR_BUFFER_BIT) && target->colorTexture) {
        target->colorWritten = 1;
        if (fullWrite) target->colorInitialized = 1;
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && target->depthTexture) {
        target->depthWritten = 1;
        if (fullWrite) target->depthInitialized = 1;
    }
}

gX_I1 gX_RHI_BufferAudit_IsEnabled(gX_I0)
{
    BufferAudit_Init();
    return s_auditEnabled;
}

gX_I0 gX_RHI_BufferAudit_BeginFrame(gX_U64 frame)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    s_frame = frame;
    if (s_targetScopeDepth != 0) {
        BufferAudit_Emit("severity=ERROR op=frame_scope_leak depth=%d", s_targetScopeDepth);
        s_targetScopeDepth = 0;
    }
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        s_targets[i].frameDraws = 0;
        s_targets[i].frameTextureBinds = 0;
        s_targets[i].frameTargetBegins = 0;
        s_targets[i].frameClearMask = 0;
    }
    BufferAudit_Emit("op=frame_begin targets=%u", s_targetCount);
}

gX_I0 gX_RHI_BufferAudit_EndFrame(gX_I0)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        gX_RHI_BufferAuditTarget *target = &s_targets[i];
        if (target->frameDraws == 0 && target->frameTextureBinds == 0
            && target->frameTargetBegins == 0 && target->frameClearMask == 0) continue;
        BufferAudit_Emit("op=frame_target_summary target=%s fbo=%u color=%u depth=%u "
            "draws=%u textureBinds=%u begins=%u clearMask=0x%x colorInit=%u depthInit=%u "
            "colorWritten=%u depthWritten=%u colorClearFrame=%llu depthClearFrame=%llu",
            (const char *)target->name, target->fbo, target->colorTexture,
            target->depthTexture, target->frameDraws, target->frameTextureBinds,
            target->frameTargetBegins, target->frameClearMask,
            (gX_U32)target->colorInitialized, (gX_U32)target->depthInitialized,
            (gX_U32)target->colorWritten, (gX_U32)target->depthWritten,
            (unsigned long long)target->lastColorClearFrame,
            (unsigned long long)target->lastDepthClearFrame);
    }
    BufferAudit_Emit("op=frame_end");
    fflush(s_auditStream);
}

gX_I0 gX_RHI_BufferAudit_BeginPass(const gX_I8 *name)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    snprintf((char *)s_pass, sizeof(s_pass), "%s", name ? (const char *)name : "unknown");
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        s_targets[i].passDraws = 0;
        s_targets[i].passTextureBinds = 0;
        s_targets[i].passTargetBegins = 0;
        s_targets[i].passClearMask = 0;
    }
    BufferAudit_Emit("op=pass_begin");
}

gX_I0 gX_RHI_BufferAudit_EndPass(gX_I0)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        gX_RHI_BufferAuditTarget *target = &s_targets[i];
        if (target->passDraws == 0 && target->passTextureBinds == 0
            && target->passTargetBegins == 0 && target->passClearMask == 0) continue;
        BufferAudit_Emit("op=pass_target_summary target=%s fbo=%u color=%u depth=%u "
            "draws=%u textureBinds=%u begins=%u clearMask=0x%x",
            (const char *)target->name, target->fbo, target->colorTexture,
            target->depthTexture, target->passDraws, target->passTextureBinds,
            target->passTargetBegins, target->passClearMask);
    }
    BufferAudit_Emit("op=pass_end");
    snprintf((char *)s_pass, sizeof(s_pass), "%s", "outside");
}

gX_I0 gX_RHI_BufferAudit_RegisterTarget(gX_U32 fbo, gX_U32 colorTexture,
    gX_U32 depthTexture, gX_U32 width, gX_U32 height, const gX_I8 *name)
{
    BufferAudit_Init();
    if (!s_auditEnabled || fbo == 0) return;
    gX_I32 index = BufferAudit_FindTarget(fbo);
    gX_I1 created = 0;
    if (index < 0) {
        if (s_targetCount >= GX_RHI_BUFFER_AUDIT_TARGET_MAX) {
            BufferAudit_Emit("severity=ERROR op=target_registry_full fbo=%u", fbo);
            return;
        }
        index = (gX_I32)s_targetCount++;
        memset(&s_targets[index], 0, sizeof(s_targets[index]));
        s_targets[index].fbo = fbo;
        snprintf((char *)s_targets[index].name, sizeof(s_targets[index].name),
            "fbo_%u", fbo);
        created = 1;
    }
    gX_RHI_BufferAuditTarget *target = &s_targets[index];
    if (target->colorTexture != colorTexture || target->width != width
        || target->height != height) {
        target->colorInitialized = 0;
        target->colorWritten = 0;
    }
    if (target->depthTexture != depthTexture || target->width != width
        || target->height != height) {
        target->depthInitialized = 0;
        target->depthWritten = 0;
    }
    target->colorTexture = colorTexture;
    target->depthTexture = depthTexture;
    target->width = width;
    target->height = height;
    if (name && name[0])
        snprintf((char *)target->name, sizeof(target->name), "%s", (const char *)name);
    if (created) {
        BufferAudit_Emit("op=target_register target=%s fbo=%u color=%u depth=%u size=%ux%u",
            (const char *)target->name, fbo, colorTexture, depthTexture, width, height);
    }
}

gX_I0 gX_RHI_BufferAudit_UnregisterTarget(gX_U32 fbo)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    gX_I32 index = BufferAudit_FindTarget(fbo);
    if (index < 0) return;
    BufferAudit_Emit("op=target_unregister target=%s fbo=%u",
        (const char *)s_targets[index].name, fbo);
    for (gX_U32 i = (gX_U32)index; i + 1 < s_targetCount; i++)
        s_targets[i] = s_targets[i + 1];
    s_targetCount--;
}

gX_I0 gX_RHI_BufferAudit_TargetBegin(gX_U32 fbo)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    if (s_targetScopeDepth >= GX_RHI_BUFFER_AUDIT_SCOPE_MAX) {
        BufferAudit_Emit("severity=ERROR op=target_scope_overflow fbo=%u", fbo);
        return;
    }
    s_targetScopes[s_targetScopeDepth++] = fbo;
    gX_I32 index = BufferAudit_FindTarget(fbo);
    if (index >= 0) {
        s_targets[index].passTargetBegins++;
        s_targets[index].frameTargetBegins++;
    }
    BufferAudit_Emit("op=target_begin target=%s fbo=%u", BufferAudit_TargetName(fbo), fbo);
}

gX_I0 gX_RHI_BufferAudit_TargetEnd(gX_I0)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    if (s_targetScopeDepth <= 0) {
        BufferAudit_Emit("severity=ERROR op=target_scope_underflow");
        return;
    }
    gX_U32 fbo = s_targetScopes[--s_targetScopeDepth];
    GLint actualDraw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actualDraw);
    BufferAudit_Emit("op=target_end target=%s expectedFbo=%u actualFbo=%u match=%u",
        BufferAudit_TargetName(fbo), fbo, (gX_U32)actualDraw,
        ((gX_U32)actualDraw == fbo) ? 1u : 0u);
}

gX_I0 gX_RHI_BufferAudit_FboCheck(gX_RHI_BufferAuditBindMode mode,
    gX_U32 requested, gX_U32 cachedDraw, gX_U32 cachedRead, gX_I1 issuedBind)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    GLint actualDraw = 0, actualRead = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actualDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &actualRead);
    gX_U32 expectedDraw = mode == GX_RHI_BUFFER_AUDIT_BIND_READ
        ? cachedDraw : requested;
    gX_U32 expectedRead = mode == GX_RHI_BUFFER_AUDIT_BIND_DRAW
        ? cachedRead : requested;
    gX_I1 drawKnown = mode != GX_RHI_BUFFER_AUDIT_BIND_READ
        || cachedDraw != (gX_U32)-1;
    gX_I1 readKnown = mode != GX_RHI_BUFFER_AUDIT_BIND_DRAW
        || cachedRead != (gX_U32)-1;
    gX_I1 drawMismatch = drawKnown && (gX_U32)actualDraw != expectedDraw;
    gX_I1 readMismatch = readKnown && (gX_U32)actualRead != expectedRead;
    const gX_I8 *modeName = mode == GX_RHI_BUFFER_AUDIT_BIND_DRAW ? "draw"
        : (mode == GX_RHI_BUFFER_AUDIT_BIND_READ ? "read" : "both");
    if (drawMismatch || readMismatch) {
        BufferAudit_Emit("severity=ERROR op=%sbind_mismatch mode=%s requested=%u "
            "expectedDraw=%u expectedRead=%u actualDraw=%u actualRead=%u "
            "cachedDraw=%u cachedRead=%u issuedBind=%u",
            issuedBind ? "post_" : "cached_", (const char *)modeName, requested,
            expectedDraw, expectedRead, (gX_U32)actualDraw, (gX_U32)actualRead,
            cachedDraw, cachedRead, (gX_U32)issuedBind);
        return;
    }
    if (issuedBind || BufferAudit_FindTarget(requested) >= 0) {
        BufferAudit_Emit("op=fbo_bind mode=%s target=%s requested=%u actualDraw=%u "
            "actualRead=%u cachedDraw=%u cachedRead=%u issuedBind=%u",
            (const char *)modeName, BufferAudit_TargetName(requested), requested,
            (gX_U32)actualDraw, (gX_U32)actualRead, cachedDraw, cachedRead,
            (gX_U32)issuedBind);
    }
}

gX_I0 gX_RHI_BufferAudit_Clear(gX_U32 mask)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    GLint drawFbo = 0, scissor[4] = {0, 0, 0, 0};
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthMask = GL_TRUE;
    GLint stencilMask = -1;
    GLint stencilBits = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilMask);
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    gX_I1 scissorEnabled = glIsEnabled(GL_SCISSOR_TEST) ? 1 : 0;
    if (scissorEnabled) glGetIntegerv(GL_SCISSOR_BOX, scissor);
    gX_I32 index = BufferAudit_FindTarget((gX_U32)drawFbo);
    gX_RHI_BufferAuditTarget *target = index >= 0 ? &s_targets[index] : NULL;
    gX_I1 fullCoverage = !scissorEnabled
        || (target && scissor[0] <= 0 && scissor[1] <= 0
            && scissor[0] + scissor[2] >= (GLint)target->width
            && scissor[1] + scissor[3] >= (GLint)target->height);
    gX_I1 colorAllowed = colorMask[0] && colorMask[1] && colorMask[2] && colorMask[3];
    gX_I1 colorBlocked = (mask & GL_COLOR_BUFFER_BIT) && (!colorAllowed || !fullCoverage);
    gX_I1 depthBlocked = (mask & GL_DEPTH_BUFFER_BIT) && (!depthMask || !fullCoverage);
    gX_U32 fullStencilMask = stencilBits >= 32 ? 0xffffffffu
        : (stencilBits > 0 ? ((1u << (gX_U32)stencilBits) - 1u) : 0u);
    gX_I1 stencilBlocked = (mask & GL_STENCIL_BUFFER_BIT) && stencilBits > 0
        && ((gX_U32)stencilMask != fullStencilMask || !fullCoverage);
    if (target) {
        target->passClearMask |= mask;
        target->frameClearMask |= mask;
        if ((mask & GL_COLOR_BUFFER_BIT) && colorAllowed && fullCoverage) {
            target->colorInitialized = 1;
            target->lastColorClearFrame = s_frame;
        }
        if ((mask & GL_COLOR_BUFFER_BIT) && colorAllowed) target->colorWritten = 1;
        if ((mask & GL_DEPTH_BUFFER_BIT) && depthMask && fullCoverage && target->depthTexture) {
            target->depthInitialized = 1;
            target->lastDepthClearFrame = s_frame;
        }
        if ((mask & GL_DEPTH_BUFFER_BIT) && depthMask && target->depthTexture)
            target->depthWritten = 1;
    }
    BufferAudit_Emit("op=clear target=%s fbo=%u mask=0x%x size=%ux%u "
        "colorMask=%u%u%u%u depthMask=%u stencilMask=0x%x scissor=%u,%d,%d,%d,%d "
        "stencilBits=%d fullCoverage=%u colorBlocked=%u depthBlocked=%u stencilBlocked=%u",
        BufferAudit_TargetName((gX_U32)drawFbo), (gX_U32)drawFbo, mask,
        target ? target->width : 0, target ? target->height : 0,
        (gX_U32)colorMask[0], (gX_U32)colorMask[1], (gX_U32)colorMask[2],
        (gX_U32)colorMask[3], (gX_U32)depthMask, (gX_U32)stencilMask,
        (gX_U32)scissorEnabled, scissor[0], scissor[1], scissor[2], scissor[3],
        stencilBits, (gX_U32)fullCoverage, (gX_U32)colorBlocked,
        (gX_U32)depthBlocked, (gX_U32)stencilBlocked);
    if (target && (colorBlocked || depthBlocked || stencilBlocked)) {
        BufferAudit_Emit("severity=WARNING op=clear_not_full target=%s fbo=%u "
            "colorBlocked=%u depthBlocked=%u stencilBlocked=%u",
            BufferAudit_TargetName((gX_U32)drawFbo), (gX_U32)drawFbo,
            (gX_U32)colorBlocked, (gX_U32)depthBlocked, (gX_U32)stencilBlocked);
    }
}

gX_I0 gX_RHI_BufferAudit_Draw(const gX_I8 *kind, gX_U32 count)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    GLint drawFbo = 0;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthMask = GL_TRUE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    gX_I32 index = BufferAudit_FindTarget((gX_U32)drawFbo);
    if (index < 0) return;
    gX_RHI_BufferAuditTarget *target = &s_targets[index];
    target->passDraws++;
    target->frameDraws++;
    if (colorMask[0] || colorMask[1] || colorMask[2] || colorMask[3])
        target->colorWritten = 1;
    if (depthMask && glIsEnabled(GL_DEPTH_TEST) && target->depthTexture)
        target->depthWritten = 1;
    (void)kind;
    (void)count;
}

gX_I0 gX_RHI_BufferAudit_TextureBind(gX_U32 texture, gX_U32 unit,
    gX_U32 targetKind)
{
    BufferAudit_Init();
    if (!s_auditEnabled || texture == 0) return;
    for (gX_U32 i = 0; i < s_targetCount; i++) {
        gX_RHI_BufferAuditTarget *target = &s_targets[i];
        gX_I1 isColor = target->colorTexture == texture;
        gX_I1 isDepth = target->depthTexture == texture;
        if (!isColor && !isDepth) continue;
        GLint drawFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        gX_I1 feedback = (gX_U32)drawFbo == target->fbo;
        gX_I1 initialized = isColor ? target->colorInitialized : target->depthInitialized;
        gX_I1 written = isColor ? target->colorWritten : target->depthWritten;
        target->passTextureBinds++;
        target->frameTextureBinds++;
        BufferAudit_Emit("op=texture_bind target=%s attachment=%s texture=%u unit=%u "
            "targetKind=0x%x drawFbo=%u feedback=%u initialized=%u written=%u",
            (const char *)target->name, isColor ? "color" : "depth", texture,
            unit, targetKind, (gX_U32)drawFbo, (gX_U32)feedback,
            (gX_U32)initialized, (gX_U32)written);
        if (feedback) {
            BufferAudit_Emit("severity=ERROR op=read_write_feedback target=%s "
                "attachment=%s texture=%u drawFbo=%u",
                (const char *)target->name, isColor ? "color" : "depth", texture,
            (gX_U32)drawFbo);
        }
    }
}

//opencode: record explicitly verified full-target writes in attachment state.
gX_I0 gX_RHI_BufferAudit_MarkFullWrite(gX_U32 fbo, gX_U32 mask)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    BufferAudit_MarkWrite(fbo, mask, 1);
    BufferAudit_Emit("op=full_write target=%s fbo=%u mask=0x%x",
        BufferAudit_TargetName(fbo), fbo, mask);
}

gX_I0 gX_RHI_BufferAudit_Blit(gX_U32 source, gX_U32 destination,
    gX_U32 srcX, gX_U32 srcY, gX_U32 srcWidth, gX_U32 srcHeight,
    gX_U32 dstX, gX_U32 dstY, gX_U32 dstWidth, gX_U32 dstHeight,
    gX_U32 mask, gX_U32 filter)
{
    BufferAudit_Init();
    if (!s_auditEnabled) return;
    gX_I32 index = BufferAudit_FindTarget(destination);
    gX_I1 fullDestination = index >= 0 && dstX == 0 && dstY == 0
        && dstWidth >= s_targets[index].width
        && dstHeight >= s_targets[index].height;
    BufferAudit_Emit("op=blit source=%s sourceFbo=%u destination=%s destinationFbo=%u "
        "src=%u,%u,%u,%u dst=%u,%u,%u,%u mask=0x%x filter=0x%x fullDestination=%u",
        BufferAudit_TargetName(source), source, BufferAudit_TargetName(destination),
        destination, srcX, srcY, srcWidth, srcHeight, dstX, dstY, dstWidth, dstHeight,
        mask, filter, (gX_U32)fullDestination);
    BufferAudit_MarkWrite(destination, mask, fullDestination);
}
