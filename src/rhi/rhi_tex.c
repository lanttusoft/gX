/**********************************************************************************************
*
*   [gX: src/rhi/rhi_tex.c]
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


//opencode: RHI Texture — centralized texture lifecycle management.
#include "rhi_tex.h"
#include "rhi_debug.h"
#include "rhi_caps.h"
#include "rhi_buffer_audit.h"

#include <stdlib.h>
#include <string.h>
#include "rhi_gl.h"
#include "../common/log.h"

//opencode: texture unit shadow table — tracks which texture is bound to each unit
#define GX_RHI_MAX_TEX_UNITS 32
static gX_U32 g_texUnits[GX_RHI_MAX_TEX_UNITS];
static gX_U32 g_texUnitTargets[GX_RHI_MAX_TEX_UNITS];

//opencode: per-texture target registry — records the GL target each texture
//   was created with. BindOnUnit uses it to avoid binding e.g. a 2D ARRAY
//   texture through GL_TEXTURE_2D (GL_INVALID_OPERATION "target doesn't
//   match" — was flooded per-frame by the CSM array bind).
#define GX_RHI_TEX_TARGET_REG_MAX 1024
static gX_U32 g_texTargetReg[GX_RHI_TEX_TARGET_REG_MAX];
static gX_U32 g_texTargetVal[GX_RHI_TEX_TARGET_REG_MAX];
static gX_U32 g_texTargetCount = 0;

static gX_I0 gX_TexTarget_Register(gX_U32 texId, gX_U32 target) {
    if (texId == 0) return;
    for (gX_U32 i = 0; i < g_texTargetCount; i++) {
        if (g_texTargetReg[i] == texId) { g_texTargetVal[i] = target; return; }
    }
    if (g_texTargetCount < GX_RHI_TEX_TARGET_REG_MAX) {
        g_texTargetReg[g_texTargetCount] = texId;
        g_texTargetVal[g_texTargetCount] = target;
        g_texTargetCount++;
    }
}

static gX_U32 gX_TexTarget_Lookup(gX_U32 texId) {
    for (gX_U32 i = 0; i < g_texTargetCount; i++) {
        if (g_texTargetReg[i] == texId) return g_texTargetVal[i];
    }
    return 0;
}

gX_U32 gX_RHI_Tex_GetTarget(gX_U32 texId) {
    return gX_TexTarget_Lookup(texId);
}

gX_I0 gX_RHI_Tex_UnitInit(gX_I0) {
    memset(g_texUnits, 0, sizeof(g_texUnits));
    memset(g_texUnitTargets, 0, sizeof(g_texUnitTargets));
}

gX_U32 gX_RHI_Tex_GetBoundOnUnit(gX_U32 slot) {
    if (slot >= GX_RHI_MAX_TEX_UNITS) return 0;
    return g_texUnits[slot];
}

gX_U32 gX_RHI_Tex_GetUnitTarget(gX_U32 slot) {
    if (slot >= GX_RHI_MAX_TEX_UNITS) return 0;
    return g_texUnitTargets[slot];
}

gX_I0 gX_RHI_Tex_SetUnit(gX_U32 slot, gX_U32 texId, gX_U32 target) {
    if (slot >= GX_RHI_MAX_TEX_UNITS) return;
    //opencode: detect target attachment reads and framebuffer feedback loops.
    gX_RHI_BufferAudit_TextureBind(texId, slot, target);
    //opencode: collision detection — log warning if overwriting an occupied unit
    if (g_texUnits[slot] != 0 && g_texUnits[slot] != texId) {
        //opencode: deduplicate repeated same-triplet warnings — only log once per unique collision
        static gX_U32 warned[64], warnedCount = 0;
        //opencode: full 32-bit key — the old (slot<<24)|(tex<<12)|texId truncated
        //   texture IDs to 12 bits, aliasing distinct collisions.
        gX_U32 key = (slot << 24) ^ (g_texUnits[slot] * 31u) ^ texId;
        gX_I1 seen = 0;
        for (gX_U32 i = 0; i < warnedCount; i++) {
            if (warned[i] == key) { seen = 1; break; }
        }
        if (!seen && warnedCount < 64) {
            warned[warnedCount++] = key;
            gX_TraceLog(gX_LOG_WARNING, "RHI_TEX: unit %u overwritten: tex %u \xe2\x86\x92 %u", slot, g_texUnits[slot], texId);
        }
    }
    g_texUnits[slot] = texId;
    g_texUnitTargets[slot] = target;
}

gX_I0 gX_RHI_Tex_ClearUnit(gX_U32 slot) {
    if (slot >= GX_RHI_MAX_TEX_UNITS) return;
    g_texUnits[slot] = 0;
    g_texUnitTargets[slot] = 0;
}

//opencode: on texture delete, clear any units that reference this texture
gX_I0 gX_RHI_Tex_EvictUnits(gX_U32 texId) {
    for (gX_U32 i = 0; i < GX_RHI_MAX_TEX_UNITS; i++) {
        if (g_texUnits[i] == texId) {
            g_texUnits[i] = 0;
            g_texUnitTargets[i] = 0;
        }
    }
}

gX_U32 gX_RHI_Tex_Create2D(gX_U32 width, gX_U32 height,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const void *data,
    const gX_I8 *name)
{
    gX_U32 tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    //opencode: tight packing during upload — matches Create2D_Immutable so
    //   tightly-packed RGBA sources (row stride = width*4) upload correctly
    //   regardless of the caller's GL_UNPACK_ALIGNMENT.
    GLint prevAlign;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, (gX_I32)width, (gX_I32)height, 0, format, type, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gX_RHI_Debug_Label(tex, GL_TEXTURE, name);
    gX_TexTarget_Register(tex, GL_TEXTURE_2D);
    return tex;
}

gX_U32 gX_RHI_Tex_Gen(gX_U32 target, const gX_I8 *name) {
    gX_U32 tex = 0;
    glGenTextures(1, &tex);
    //opencode: bind before labeling — NVIDIA's glObjectLabel rejects names
    //   without storage ("unknown texture object"); binding creates the object.
    glBindTexture((GLenum)target, tex);
    gX_RHI_Debug_Label(tex, GL_TEXTURE, name);
    gX_TexTarget_Register(tex, target);
    return tex;
}

//opencode: immutable-storage gate — glTexStorage2D/3D needs GL 4.2+,
//   ES 3.0+, or GL_ARB_texture_storage. GX_NO_IMMUTABLE_STORAGE=1 forces the
//   mutable path on a capable host so the fallback is testable without a
//   native GL 3.0 context.
static gX_I1 gX_TexHasImmutableStorage(gX_I0)
{
    const gX_I8 *force = getenv("GX_NO_IMMUTABLE_STORAGE");
    if (force && force[0] && force[0] != '0') return 0;
    {
        const gX_RHI_Caps *caps = gX_RHI_GetCaps();
        if (!caps) return 0;
        return caps->hasImmutableStorage;
    }
}

//opencode: log the mutable fallback once per process — per-texture logging
//   would flood loads that allocate several depth arrays.
static gX_I0 gX_TexFallback_Note(const gX_I8 *what)
{
    static gX_I1 noted = 0;
    if (noted) return;
    noted = 1;
    gX_TraceLog(gX_LOG_WARNING, "RHI_TEX: immutable storage unavailable — mutable fallback for %s",
        what ? what : "?");
}

//opencode: derive TexImage format/type from a sized internal format for the
//   mutable fallback. Covers every internal format the engine passes to the
//   Storage entry points (RGBA8, R32F, RGB16F, DEPTH_COMPONENT24). Returns 1
//   on success, 0 when the format is unknown (caller must fail loudly).
static gX_I1 gX_TexFallbackFormatType(gX_I32 internalFormat, gX_I32 *format, gX_I32 *type)
{
    switch (internalFormat) {
        case gX_RHI_RGBA8:
            *format = gX_RHI_RGBA; *type = gX_RHI_UNSIGNED_BYTE; return 1;
        case gX_RHI_RGB8:
            *format = gX_RHI_RGB; *type = gX_RHI_UNSIGNED_BYTE; return 1;
        case gX_RHI_R32F:
            *format = gX_RHI_RED; *type = gX_RHI_FLOAT; return 1;
        case gX_RHI_RGB16F:
            *format = gX_RHI_RGB; *type = gX_RHI_HALF_FLOAT; return 1;
        case gX_RHI_RGBA16F:
            *format = gX_RHI_RGBA; *type = gX_RHI_HALF_FLOAT; return 1;
        case gX_RHI_DEPTH_COMPONENT24:
        case gX_RHI_DEPTH_COMPONENT:
            *format = gX_RHI_DEPTH_COMPONENT; *type = gX_RHI_UNSIGNED_INT; return 1;
        default:
            break;
    }
    return 0;
}

gX_U32 gX_RHI_Tex_Create2D_Immutable(gX_U32 width, gX_U32 height,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const void *data,
    const gX_I8 *name)
{
    gX_U32 tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint prevAlign;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (gX_TexHasImmutableStorage()) {
        glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, (gX_I32)width, (gX_I32)height);
        if (data) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (gX_I32)width, (gX_I32)height, format, type, data);
    } else {
        //opencode: native GL 3.0 without ARB_texture_storage has no
        //   glTexStorage2D — mutable allocation is the only option. The
        //   caller supplies format/type, so no inference is needed.
        gX_TexFallback_Note("Create2D_Immutable");
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, (gX_I32)width, (gX_I32)height, 0, format, type, data);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gX_RHI_Debug_Label(tex, GL_TEXTURE, name);
    gX_TexTarget_Register(tex, GL_TEXTURE_2D);
    return tex;
}

gX_U32 gX_RHI_Tex_CreateCubemap(gX_U32 size,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const gX_I8 *name)
{
    gX_U32 tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (gX_I32 i = 0; i < 6; i++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
            (gX_I32)size, (gX_I32)size, 0, format, type, NULL);
    gX_RHI_Debug_Label(tex, GL_TEXTURE, name);
    gX_TexTarget_Register(tex, GL_TEXTURE_CUBE_MAP);
    return tex;
}

gX_I0 gX_RHI_Tex_Param(gX_U32 texId, gX_I32 target, gX_I32 pname, gX_I32 param) {
    if (texId) glBindTexture((gX_U32)target, texId);
    glTexParameteri(target, pname, param);
}

gX_I0 gX_RHI_Tex_GenMipmap(gX_U32 texId, gX_U32 target) {
    if (texId) glBindTexture((GLenum)target, texId);
    glGenerateMipmap((GLenum)target);
}

gX_I0 gX_RHI_Tex_Delete(gX_U32 texId) {
    if (texId == 0) return;
    gX_RHI_Tex_EvictUnits(texId);
    glDeleteTextures(1, &texId);
}

gX_U32 gX_RHI_Tex_CreateMultisample(gX_U32 samples, gX_I32 internalFormat,
    gX_U32 w, gX_U32 h, const gX_I8 *name)
{
    //opencode: multisample textures require desktop GL 3.2+ or ES 3.1+.
    //   On ES 3.0 (renderbuffer MSAA only) return 0 so callers fall back to
    //   a single-sampled path instead of calling glTexImage2DMultisample
    //   (which does not exist in the ES 3.0 API).
    if (!gX_RHI_GetCaps()->hasMSTexture) return 0;
    gX_U32 tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, (gX_I32)samples,
        (GLenum)internalFormat, (gX_I32)w, (gX_I32)h, GL_TRUE);
    //opencode: NO tex params for MSAA — GL_TEXTURE_MIN_FILTER/MAG_FILTER
    //   are invalid for GL_TEXTURE_2D_MULTISAMPLE (generates GL_INVALID_ENUM).
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    gX_RHI_Debug_Label(tex, GL_TEXTURE, name);
    gX_TexTarget_Register(tex, GL_TEXTURE_2D_MULTISAMPLE);
    return tex;
}

gX_I0 gX_RHI_Tex_Upload(gX_U32 texId, gX_I32 target, gX_I32 level,
    gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_I32 format, gX_I32 type, const void *data)
{
    if (texId) {
        //opencode: glBindTexture accepts only top-level targets — a cube FACE
        //   target (POSITIVE_X..NEGATIVE_Z) is GL_INVALID_ENUM. Bind the cubemap
        //   instead; glTexSubImage2D accepts the face target for uploads.
        gX_U32 bindTarget = (gX_U32)target;
        if (bindTarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && bindTarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
            bindTarget = GL_TEXTURE_CUBE_MAP;
        glBindTexture((GLenum)bindTarget, texId);
    }
    glTexSubImage2D(target, level, x, y, (gX_I32)w, (gX_I32)h, format, type, data);
}

gX_I0 gX_RHI_Tex_Paramf(gX_U32 texId, gX_I32 target, gX_I32 pname, gX_F32 param) {
    if (texId) glBindTexture((gX_U32)target, texId);
    glTexParameterf(target, pname, param);
}

gX_I0 gX_RHI_Tex_Paramfv(gX_U32 texId, gX_I32 target, gX_I32 pname, const gX_F32 *param) {
    if (texId) glBindTexture((gX_U32)target, texId);
    glTexParameterfv(target, pname, param);
}

gX_I32 gX_RHI_Tex_Storage3D(gX_U32 texId, gX_I32 target, gX_I32 levels,
    gX_I32 internalFormat, gX_U32 w, gX_U32 h, gX_U32 depth)
{
    if (texId) glBindTexture((GLenum)target, texId);
    if (gX_TexHasImmutableStorage()) {
        glTexStorage3D((GLenum)target, (GLint)levels, (GLenum)internalFormat,
            (GLsizei)w, (GLsizei)h, (GLsizei)depth);
        gX_TexTarget_Register(texId, (gX_U32)target);
        return 0;
    }
    //opencode: mutable fallback for pre-4.2 desktop GL — one glTexImage3D
    //   per mip level (all live callers use levels=1). Mutable textures need
    //   an explicit MAX_LEVEL, or a mipmap-minified sample reads past the
    //   defined levels and the texture goes incomplete.
    {
        gX_I32 format = 0, type = 0;
        if (!gX_TexFallbackFormatType(internalFormat, &format, &type)) {
            gX_TraceLog(gX_LOG_ERROR, "RHI_TEX: Storage3D fallback has no format mapping for internal 0x%x",
                (unsigned)internalFormat);
            return -1;
        }
        gX_TexFallback_Note("Storage3D");
        for (gX_I32 level = 0; level < levels; level++) {
            gX_U32 lw = w >> level; if (lw == 0) lw = 1;
            gX_U32 lh = h >> level; if (lh == 0) lh = 1;
            glTexImage3D((GLenum)target, level, internalFormat,
                (GLsizei)lw, (GLsizei)lh, (GLsizei)depth, 0,
                (GLenum)format, (GLenum)type, NULL);
        }
        glTexParameteri((GLenum)target, GL_TEXTURE_MAX_LEVEL, levels - 1);
        gX_TexTarget_Register(texId, (gX_U32)target);
        return 0;
    }
}

//opencode: immutable 2D/cubemap storage (glTexStorage2D) — defines ALL mip
//   levels atomically. Required for cubemaps rendered per-face-per-mip:
//   mutable glTexImage2D mip allocation was unreliable on NVIDIA (levels
//   stayed undefined despite no GL error). Without immutable storage
//   (pre-4.2 desktop GL) the RHI falls back to per-mip glTexImage2D with an
//   explicit MAX_LEVEL so the texture stays complete.
gX_I32 gX_RHI_Tex_Storage2D(gX_U32 texId, gX_I32 target, gX_I32 levels,
    gX_I32 internalFormat, gX_U32 w, gX_U32 h)
{
    if (texId) glBindTexture((GLenum)target, texId);
    if (gX_TexHasImmutableStorage()) {
        glTexStorage2D((GLenum)target, (GLint)levels, (GLenum)internalFormat,
            (GLsizei)w, (GLsizei)h);
        gX_TexTarget_Register(texId, (gX_U32)target);
        return 0;
    }
    {
        gX_I32 format = 0, type = 0;
        if (!gX_TexFallbackFormatType(internalFormat, &format, &type)) {
            gX_TraceLog(gX_LOG_ERROR, "RHI_TEX: Storage2D fallback has no format mapping for internal 0x%x",
                (unsigned)internalFormat);
            return -1;
        }
        gX_TexFallback_Note("Storage2D");
        if ((gX_U32)target == (gX_U32)gX_RHI_TEXTURE_CUBE_MAP) {
            for (gX_I32 face = 0; face < 6; face++) {
                for (gX_I32 level = 0; level < levels; level++) {
                    gX_U32 lw = w >> level; if (lw == 0) lw = 1;
                    gX_U32 lh = h >> level; if (lh == 0) lh = 1;
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level,
                        internalFormat, (GLsizei)lw, (GLsizei)lh, 0,
                        (GLenum)format, (GLenum)type, NULL);
                }
            }
        } else {
            for (gX_I32 level = 0; level < levels; level++) {
                gX_U32 lw = w >> level; if (lw == 0) lw = 1;
                gX_U32 lh = h >> level; if (lh == 0) lh = 1;
                glTexImage2D((GLenum)target, level, internalFormat,
                    (GLsizei)lw, (GLsizei)lh, 0,
                    (GLenum)format, (GLenum)type, NULL);
            }
        }
        glTexParameteri((GLenum)target, GL_TEXTURE_MAX_LEVEL, levels - 1);
        gX_TexTarget_Register(texId, (gX_U32)target);
        return 0;
    }
}

gX_I0 gX_RHI_Tex_Bind(gX_U32 texId, gX_I32 target) {
    glBindTexture((GLenum)target, texId);
}

gX_I0 gX_RHI_ActiveTexture(gX_U32 unit) {
    glActiveTexture((GLenum)unit);
}
