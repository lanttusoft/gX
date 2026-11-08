/**********************************************************************************************
*
*   [gX: src/wrappers/texture.c]
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

#include "wrappers_header.h"
#include "../common/enum.h"
#include "../common/path.h"
#include "texture.h"
#include "../common/config.h"
#include "../main.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../rhi/rhi_tex.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_bindless.h"
#include "../rhi/rhi_buffer_audit.h"

//opencode: bindless texture support flag — set once at GL init
gX_I1 gX_BindlessSupported = 0;

//opencode: PS1 texture tracker (TIM textures in VRAM)

//opencode: PS2 texture side-table (GS VRAM textures) — non-static for wrappers_ps2.c

//opencode: N64 sprite side-table

//opencode: fallback texture for missing files — 4×4 pink checkerboard
static const gX_U8 g_fallbackTexturePixels[64] = {
    255,0,255,255,  0,0,0,255,   255,0,255,255,  0,0,0,255,
    0,0,0,255,     255,0,255,255,  0,0,0,255,   255,0,255,255,
    255,0,255,255,  0,0,0,255,   255,0,255,255,  0,0,0,255,
    0,0,0,255,     255,0,255,255,  0,0,0,255,   255,0,255,255,
};

//ai: E6 — minimal DDS reader for BC1/BC3/BC4/BC5/BC7 payloads. The encoder
//   writes DX10 headers; legacy fourCC is accepted for BC1/BC3/BC5. The
//   loader returns a zero texture when the GPU lacks the required extension,
//   and the caller falls back to the source PNG.
#define GX_DDS_MAGIC 0x20534444u
#define GX_DDS_DX10  0x30315844u
#define GX_DDS_BC1   0x31545844u
#define GX_DDS_BC3   0x35545844u
#define GX_DDS_BC5   0x32495441u /* ATI2 */
#define GX_DDS_BC4   0x31495441u /* ATI1 */

static gX_I1 gX_Texture_PathEndsWith(const gX_I8 *path, const gX_I8 *suffix) {
    if (!path || !suffix) return 0;
    size_t pathLen = strlen(path), suffixLen = strlen(suffix);
    return pathLen >= suffixLen
        && strcmp(path + pathLen - suffixLen, suffix) == 0;
}

static gX_I1 gX_Texture_DDSFormat(gX_U32 fourCC, gX_U32 dxgi,
    gX_I32 *internalFormat, gX_I32 *blockBytes, gX_I1 *isDX10) {
    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    *isDX10 = 0;
    if (fourCC == GX_DDS_DX10) {
        *isDX10 = 1;
        switch (dxgi) {
            case 98: case 99:
                if (!caps->hasBPTC) return 0;
                *internalFormat = dxgi == 99 ? 0x8E8D : 0x8E8C;
                *blockBytes = 16;
                return 1;
            case 83: case 84:
                if (!caps->hasRGTC) return 0;
                *internalFormat = dxgi == 84 ? 0x8DBE : 0x8DBD;
                *blockBytes = 16;
                return 1;
            case 80: case 81:
                if (!caps->hasRGTC) return 0;
                *internalFormat = dxgi == 81 ? 0x8DBC : 0x8DBB;
                *blockBytes = 8;
                return 1;
            case 71: case 72:
                *internalFormat = dxgi == 72 ? 0x8C4D : 0x8C4C;
                *blockBytes = 8;
                return 1;
            case 77: case 78:
                *internalFormat = dxgi == 78 ? 0x8C4F : 0x8C4E;
                *blockBytes = 16;
                return 1;
            default:
                return 0;
        }
    }
    switch (fourCC) {
        case GX_DDS_BC1:
            *internalFormat = 0x8C4C;
            *blockBytes = 8;
            return 1;
        case GX_DDS_BC3:
            *internalFormat = 0x8C4E;
            *blockBytes = 16;
            return 1;
        case GX_DDS_BC5:
            if (!caps->hasRGTC) return 0;
            *internalFormat = 0x8DBD;
            *blockBytes = 16;
            return 1;
        case GX_DDS_BC4:
            if (!caps->hasRGTC) return 0;
            *internalFormat = 0x8DBB;
            *blockBytes = 8;
            return 1;
        default:
            return 0;
    }
}

static gX_Texture gX_Texture_Load_DDS(const gX_I8 *path) {
    gX_Texture result = {0};
    FILE *file = fopen(path, "rb");
    if (!file) return result;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return result; }
    long length = ftell(file);
    if (length < 128 || length > (long)(64 * 1024 * 1024)) { fclose(file); return result; }
    rewind(file);
    gX_U8 *data = (gX_U8 *)malloc((size_t)length);
    if (!data) { fclose(file); return result; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); free(data); return result;
    }
    fclose(file);
    if (*(gX_U32 *)data != GX_DDS_MAGIC) { free(data); return result; }

    gX_U32 height = *(gX_U32 *)(data + 12);
    gX_U32 width = *(gX_U32 *)(data + 16);
    gX_U32 mipmaps = *(gX_U32 *)(data + 28);
    gX_U32 fourCC = *(gX_U32 *)(data + 84);
    gX_I32 internalFormat = 0, blockBytes = 0;
    gX_I1 isDX10 = 0;
    gX_U32 dxgi = 0;
    if (fourCC == GX_DDS_DX10) {
        if (length < 148) { free(data); return result; }
        dxgi = *(gX_U32 *)(data + 128);
        if (*(gX_U32 *)(data + 140) > 1) { free(data); return result; }
    }
    if (!gX_Texture_DDSFormat(fourCC, dxgi, &internalFormat, &blockBytes, &isDX10)) {
        free(data);
        return result;
    }
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        free(data); return result;
    }
    if (mipmaps == 0) mipmaps = 1;
    if (mipmaps > 16) mipmaps = 16;
    gX_U32 caps2 = *(gX_U32 *)(data + 112);
    if (caps2 & 0x200u) { free(data); return result; } /* cubemap */
    gX_U32 offset = isDX10 ? 148u : 128u;
    gX_U32 id = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "dds");
    if (id == 0) { free(data); return result; }
    gX_I1 mipOK = 1;
    for (gX_U32 level = 0; level < mipmaps; level++) {
        gX_U32 mipW = width >> level; if (mipW == 0) mipW = 1;
        gX_U32 mipH = height >> level; if (mipH == 0) mipH = 1;
        gX_U32 blocksX = (mipW + 3) / 4;
        gX_U32 blocksY = (mipH + 3) / 4;
        gX_U32 imageSize = blocksX * blocksY * (gX_U32)blockBytes;
        if (offset + imageSize > (gX_U32)length) { mipOK = 0; break; }
        if (gX_RHI_Tex_Compressed2D(id, (gX_I32)level, internalFormat,
                (gX_I32)mipW, (gX_I32)mipH, (gX_I32)imageSize,
                data + offset) != 0) {
            mipOK = 0;
            break;
        }
        offset += imageSize;
    }
    free(data);
    if (!mipOK) {
        gX_RHI_Tex_Delete(id);
        return result;
    }
    //ai: one-level DDS payloads get a runtime mip chain; the PNG path also
    //   samples with mipmaps, so both sides minify consistently.
    if (mipmaps == 1 && (width > 1 || height > 1)) {
        gX_U32 levels = 1, mipW = width, mipH = height;
        while (mipW > 1 || mipH > 1) {
            mipW = (mipW + 1) / 2;
            mipH = (mipH + 1) / 2;
            levels++;
        }
        gX_RHI_Tex_GenMipmap(id, gX_RHI_TEXTURE_2D);
        mipmaps = levels;
    }
    gX_RHI_Tex_Param(id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER,
        mipmaps > 1 ? gX_RHI_LINEAR_MIPMAP_LINEAR : gX_RHI_LINEAR);
    gX_RHI_Tex_Param(id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER,
        gX_RHI_LINEAR);
    gX_RHI_Tex_Param(id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S,
        gX_RHI_REPEAT);
    gX_RHI_Tex_Param(id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T,
        gX_RHI_REPEAT);
    result.id = id;
    result.width = (gX_I32)width;
    result.height = (gX_I32)height;
    //ai: creation binds through raw GL; resync raylib's texture state so a
    //   later rlEnableTexture() cannot skip a rebind and sample this texture.
    rlActiveTextureSlot(0);
    rlEnableTexture(id);
    rlEnableTexture(0);
    //ai: and invalidate the RHI unit shadow for the same reason.
    gX_RHI_Tex_UnitInit();
    result.mipmaps = (gX_I32)mipmaps;
    result.format = gX_RHI_RGBA8;
    gX_TraceLog(gX_LOG_INFO, "DDS: loaded %s (%ux%u, %u mips, fmt 0x%x)",
        path, width, height, mipmaps, (unsigned)internalFormat);
    return result;
}

static gX_I1 gX_Texture_DDSSidecar(const gX_I8 *path, gX_I8 *out, size_t outSize) {
    if (!gX_Texture_PathEndsWith(path, ".png")) return 0;
    size_t length = strlen(path);
    if (length + 5 >= outSize) return 0;
    memcpy(out, path, length - 4);
    memcpy(out + length - 4, ".dds", 5);
    struct stat info;
    return stat(out, &info) == 0;
}

gX_Texture gX_Texture_Load(const gX_I8 *path) {
    //ai: E6 — prefer a sibling .dds sidecar unless the audit hook disables
    //   it; a failed/unsupported compressed load falls back to the backend.
    gX_Texture textu = {0};
    gX_I8 ddsPath[1024] = {0};
    const gX_I8 *override = getenv("GX_NO_TEXTURE_DDS");
    gX_I1 allowDDS = !(override && override[0] == '1');
    if (allowDDS && path) {
        if (gX_Texture_PathEndsWith(path, ".dds")) {
            snprintf(ddsPath, sizeof(ddsPath), "%s", path);
        } else if (!gX_Texture_DDSSidecar(path, ddsPath, sizeof(ddsPath))) {
            ddsPath[0] = '\0';
        }
        if (ddsPath[0]) textu = gX_Texture_Load_DDS(ddsPath);
    }
    if (textu.id == 0) {
        //opencode: D12 — backend loader (raylib_Texture_Load handles the GLFW
        //   immutable-storage re-upload internally)
        textu = raylib_Texture_Load(path);
    }
    //opencode: v0.3.8 - guest fallback: registered guest roots may carry the
    //   texture when gX's own resources lack it (loaded from GX_ASSETS dirs)
    if (textu.id == 0) {
        const gX_I8 *guest = gX_Path_GuestFallback("texture", path);
        if (guest) {
            gX_TraceLog(gX_LOG_INFO, "Texture guest fallback '%s' -> '%s'", path ? path : "?", guest);
            textu = raylib_Texture_Load(guest);
        }
    }
    //opencode: fallback — if load failed (id==0), generate pink checkerboard
    if (textu.id == 0) {
        gX_TraceLog(gX_LOG_WARNING, "Texture load failed: %s — using fallback checkerboard", path ? path : "?");
        textu = gX_Texture_LoadFromData(4, 4, g_fallbackTexturePixels);
    }
    return textu;
}

gX_Texture gX_Texture_Load_Cubemap(const gX_I8 *path, gX_I32 layout) {
    //opencode: D12 — backend loader
    gX_Texture texture = raylib_Texture_LoadCubemap(path, layout);
    if (texture.id) {
        gX_RHI_Tex_RegisterTarget(texture.id, gX_RHI_TEXTURE_CUBE_MAP);
    }
    return texture;
}

gX_I0 gX_Texture_Unload(gX_Texture tex) {
    //ai: forget the GL target too; texture IDs are reused after deletion and a
    //   stale cube/array mapping would misroute later filter/wrap calls.
    if (tex.id) gX_RHI_Tex_UnregisterTarget(tex.id);
    //opencode: D12 — backend loader
    raylib_Texture_Unload(tex);
}

gX_I0 gX_Texture_SetFilter(gX_Texture texture, gX_I32 filter) {
    //ai: raylib's SetTextureFilter always binds GL_TEXTURE_2D. Route known
    //   cubemap/array targets through the target-aware RHI instead; the old
    //   path made raylib cubemap filtering emit GL_INVALID_OPERATION
    //   "target doesn't match".
    gX_U32 target = gX_RHI_Tex_GetTarget(texture.id);
    if (target == gX_RHI_TEXTURE_2D_MULTISAMPLE) return;
    if (target == 0 || target == gX_RHI_TEXTURE_2D) {
        raylib_Texture_SetFilter(texture, filter);
        return;
    }
    gX_I32 minFilter = 0;
    gX_I32 magFilter = 0;
    gX_I32 anisotropy = 0;
    switch (filter) {
        case gX_TEXTURE_FILTER_POINT:
            minFilter = texture.mipmaps > 1
                ? gX_RHI_NEAREST_MIPMAP_NEAREST : gX_RHI_NEAREST;
            magFilter = gX_RHI_NEAREST;
            break;
        case gX_TEXTURE_FILTER_BILINEAR:
            minFilter = texture.mipmaps > 1
                ? gX_RHI_LINEAR_MIPMAP_NEAREST : gX_RHI_LINEAR;
            magFilter = gX_RHI_LINEAR;
            break;
        case gX_TEXTURE_FILTER_TRILINEAR:
            minFilter = texture.mipmaps > 1
                ? gX_RHI_LINEAR_MIPMAP_LINEAR : gX_RHI_LINEAR;
            magFilter = gX_RHI_LINEAR;
            break;
        case gX_TEXTURE_FILTER_ANISOTROPIC_4X: anisotropy = 4; break;
        case gX_TEXTURE_FILTER_ANISOTROPIC_8X: anisotropy = 8; break;
        case gX_TEXTURE_FILTER_ANISOTROPIC_16X: anisotropy = 16; break;
        default: return;
    }
    if (minFilter) {
        gX_RHI_Tex_Param(texture.id, (gX_I32)target,
            gX_RHI_TEXTURE_MIN_FILTER, minFilter);
        gX_RHI_Tex_Param(texture.id, (gX_I32)target,
            gX_RHI_TEXTURE_MAG_FILTER, magFilter);
    }
    if (anisotropy) {
        gX_RHI_Tex_Paramf(texture.id, (gX_I32)target,
            gX_RHI_TEXTURE_MAX_ANISOTROPY, (gX_F32)anisotropy);
    }
}

gX_I0 gX_Texture_SetWrap(gX_Texture texture, gX_I32 wrap) {
    //ai: same target rule as filtering; multisample render targets do not
    //   support sampler wrap state.
    gX_U32 target = gX_RHI_Tex_GetTarget(texture.id);
    if (target == gX_RHI_TEXTURE_2D_MULTISAMPLE) return;
    if (target == 0 || target == gX_RHI_TEXTURE_2D) {
        raylib_Texture_SetWrap(texture, wrap);
        return;
    }
    gX_I32 value = 0;
    switch (wrap) {
        case gX_TEXTURE_WRAP_REPEAT: value = gX_RHI_REPEAT; break;
        case gX_TEXTURE_WRAP_CLAMP: value = gX_RHI_CLAMP_TO_EDGE; break;
        case gX_TEXTURE_WRAP_MIRROR_REPEAT: value = gX_RHI_MIRRORED_REPEAT; break;
        case gX_TEXTURE_WRAP_MIRROR_CLAMP: value = gX_RHI_MIRROR_CLAMP_TO_EDGE; break;
        default: return;
    }
    gX_RHI_Tex_Param(texture.id, (gX_I32)target,
        gX_RHI_TEXTURE_WRAP_S, value);
    gX_RHI_Tex_Param(texture.id, (gX_I32)target,
        gX_RHI_TEXTURE_WRAP_T, value);
}

//opencode: gX_Texture_Draw, Draw_At, Draw_Fullscreen, Draw_Sprite
//   implemented after the RT tracker below.

//opencode: create texture from raw RGBA8888 pixel data
gX_Texture gX_Texture_LoadFromData(gX_U32 width, gX_U32 height, const gX_U8 *data) {
    Image img = { 0 };
    img.data = (void*)data;
    img.width = (int)width;
    img.height = (int)height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    //opencode: D12 — backend loader
    gX_Image gimg = gX_Image_ConvertFromBackend(img);
    return raylib_Texture_LoadFromImage(gimg);
//opencode: PS2 — upload RGBA8888 data to GS VRAM
}

//opencode: texture update wrapper for analog CRT threaded processing
gX_I0 gX_Texture_Update(gX_Texture texture, const gX_I0 *pixels) {
    raylib_Texture_Update(texture, pixels);
}

//opencode: RT texture ID tracker — auto-invert for render textures
static gX_U32 g_rtTextureIds[gX_TEXTURE_RT_TRACKER_MAX];
static gX_U32 g_rtTextureCount = 0;

gX_I0 gX_Texture_RT_Track(gX_U32 texId) {
    if (texId == 0) return;
    if (g_rtTextureCount >= gX_TEXTURE_RT_TRACKER_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "RT texture tracker full (%d max)", gX_TEXTURE_RT_TRACKER_MAX);
        return;
    }
    g_rtTextureIds[g_rtTextureCount++] = texId;
}

gX_I0 gX_Texture_RT_Untrack(gX_U32 texId) {
    for (gX_U32 i = 0; i < g_rtTextureCount; i++) {
        if (g_rtTextureIds[i] == texId) {
            g_rtTextureIds[i] = g_rtTextureIds[--g_rtTextureCount];
            return;
        }
    }
}

gX_I1 gX_Texture_IsRT(gX_U32 texId) {
    for (gX_U32 i = 0; i < g_rtTextureCount; i++) {
        if (g_rtTextureIds[i] == texId) return 1;
    }
    return 0;
}

//opencode: new texture draw API — center-origin convention
gX_I0 gX_Texture_Draw(gX_Texture texture, gX_Texture_Draw_Params params) {
    //opencode: raylib's queued texture quad bypasses the direct RHI bind hooks.
    gX_RHI_BufferAudit_TextureBind(texture.id, 0, gX_RHI_TEXTURE_2D);
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"TextureDraw", 1);
    raylib_Texture_Draw(texture, params);
}

//opencode: draw texture filling the screen (center-origin, fullscreen size)
gX_I0 gX_Texture_Draw_Fullscreen(gX_Texture texture) {
    gX_Texture_Draw_Params p = {
        .dst = {0, 0, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f}
    };
    gX_Texture_Draw(texture, p);
}

//opencode: draw texture at position at native size
gX_I0 gX_Texture_Draw_At(gX_Texture texture, gX_F32v2 pos) {
    gX_Texture_Draw_Params p = {
        .dst = {pos.x, pos.y, (gX_F32)texture.width, (gX_F32)texture.height},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f}
    };
    gX_Texture_Draw(texture, p);
}

//opencode: sprite draw — center at pos, explicit size, rotation
gX_I0 gX_Texture_Draw_Sprite(gX_Texture texture, gX_F32v2 pos, gX_F32v2 size, gX_F32 rotation) {
    gX_Texture_Draw_Params p = {
        .dst = {pos.x, pos.y, size.x, size.y},
        .tint = {255, 255, 255, 255},
        .scale = {1.0f, 1.0f},
        .rotation = rotation
    };
    gX_Texture_Draw(texture, p);
}

gX_I0 gX_Texture_Draw3D_Params(gX_Texture texture, gX_Texture_Draw_Params params, gX_F32v3 pos) {
    raylib_Texture_Draw3D(texture, params, pos);
}

gX_RenderTexture gX_RenderTexture_Load(gX_U32 width, gX_U32 height) {
    RenderTexture rt = raylib_RenderTexture_Load((gX_I32)width,(gX_I32)height,true,false);
    gX_RenderTexture gxrt = gX_RenderTexture_ConvertFromBackend(rt);
    gX_Texture_RT_Track(gxrt.texture.id);
    //opencode: register backend-created targets for later lifecycle attribution.
    gX_RHI_BufferAudit_RegisterTarget(gxrt.id, gxrt.texture.id, gxrt.depth.id,
        gxrt.texture.width, gxrt.texture.height, NULL);
    return gxrt;
}

//opencode: HDR float16 render texture for bloom/tonemap pipeline
gX_RenderTexture gX_RenderTexture_Load_HDR(gX_U32 width, gX_U32 height) {
    RenderTexture rt = raylib_RenderTexture_Load((gX_I32)width,(gX_I32)height,true,true);
    gX_RenderTexture gxrt = gX_RenderTexture_ConvertFromBackend(rt);
    gX_Texture_RT_Track(gxrt.texture.id);
    //opencode: register HDR backend targets for lifecycle attribution.
    gX_RHI_BufferAudit_RegisterTarget(gxrt.id, gxrt.texture.id, gxrt.depth.id,
        gxrt.texture.width, gxrt.texture.height, NULL);
    return gxrt;
}

//ai: format-aware render-target creation. Used by the compression lab's
//   auxiliary-buffer diet: immutable sized storage, an optional depth
//   attachment (most fullscreen targets never read one), and an empty
//   struct on an incomplete FBO so the caller can fall back to the legacy
//   loader instead of rendering into an unusable target.
gX_RenderTexture gX_RenderTexture_Load_Format(gX_U32 width, gX_U32 height,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, gX_I1 depth,
    const gX_I8 *name) {
    gX_RenderTexture rt = {0};
    gX_U32 color = gX_RHI_Tex_Create2D_Immutable(width, height,
        internalFormat, format, type, NULL, name);
    if (!color) return rt;
    gX_U32 depthTex = 0;
    if (depth) {
        depthTex = gX_RHI_Tex_Create2D_Immutable(width, height,
            gX_RHI_DEPTH_COMPONENT24, gX_RHI_DEPTH_COMPONENT,
            gX_RHI_UNSIGNED_INT, NULL, name);
        if (!depthTex) {
            gX_RHI_Tex_Delete(color);
            return rt;
        }
    }
    gX_RHI_FBO fbo = gX_RHI_FBO_Create(color, depthTex, name);
    if (!fbo) {
        gX_RHI_Tex_Delete(color);
        if (depthTex) gX_RHI_Tex_Delete(depthTex);
        return rt;
    }
    //ai: match the legacy loader's opaque-black initialization. Some targets
    //   (TAA history) are sampled before their first authored clear, so
    //   undefined storage would leak into temporal accumulation.
    gX_RHI_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);
    gX_RHI_UnbindFBO();
    rt.id = fbo;
    rt.texture.id = color;
    rt.texture.width = (gX_I32)width;
    rt.texture.height = (gX_I32)height;
    rt.texture.mipmaps = 1;
    rt.texture.format = (internalFormat == gX_RHI_RGBA8 || internalFormat == gX_RHI_R8)
        ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        : PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    if (depthTex) {
        rt.depth.id = depthTex;
        rt.depth.width = (gX_I32)width;
        rt.depth.height = (gX_I32)height;
        rt.depth.mipmaps = 1;
        rt.depth.format = 19;
    }
    gX_Texture_RT_Track(color);
    gX_RHI_BufferAudit_RegisterTarget(rt.id, color, depthTex, width, height, name);
    return rt;
}

gX_I0 gX_RenderTexture_Unload(gX_RenderTexture rt) {
    //opencode: release audit metadata with the underlying render target.
    gX_RHI_BufferAudit_UnregisterTarget(rt.id);
    gX_Texture_RT_Untrack(rt.texture.id);
    //opencode: D12 — backend loader
    raylib_RenderTexture_Unload(rt);
}

gX_I0 gX_RenderTexture_Draw(gX_RenderTexture rt) {
    //opencode: D10 — draw via the RHI texture path (replaces raylib DrawTexture)
    gX_Texture_Draw_Params p = {0};
    p.dst = (gX_Rectangle){0, 0, (gX_F32)rt.texture.width, (gX_F32)rt.texture.height};
    p.scale = (gX_F32v2){1.0f, 1.0f};
    p.tint = (gX_Color){255, 255, 255, 255};
    gX_Texture_Draw(rt.texture, p);
}

//opencode: Texture_Wrapper implementation (moved from common/utils.c)

gX_I0 gX_Texture_Wrapper_Load(gX_Texture_Wrapper *tw) {
    gX_TraceLog(gX_LOG_TRACE3,"Texture_Wrapper_Load");
    if (tw->isSkybox) {
        tw->texture = gX_Texture_Load_Cubemap(tw->path,tw->skyboxLayout);
    }
    else {
        tw->texture = gX_Texture_Load(tw->path);
    }
    tw->loadingState.loaded = true;
}

gX_I0 gX_Texture_Wrapper_Unload(gX_Texture_Wrapper *tw) {
    gX_TraceLog(gX_LOG_TRACE3,"Texture_Wrapper_Unload");
    //opencode: release bindless handle before unloading the GL texture
    if (tw->handleResident && tw->bindlessHandle != 0) {
        gX_Texture_ReleaseHandle(tw->bindlessHandle);
    }
    tw->bindlessHandle = 0;
    tw->handleResident = false;
    gX_Texture_Unload(tw->texture);
    tw->loadingState.loaded = false;
}

gX_I0 gX_Texture_Wrapper_Update(gX_Texture_Wrapper *tw) {
    gX_I1 loadasapstate = (maindata.backendLoaded && tw->loadingState.loadAsap);
    gX_I1 loadbeforestate = false;
    gX_I1 unloadafterstate = false;
    if ((tw->loadingState.load || loadasapstate || loadbeforestate) && !(tw->loadingState.loaded)) {
        gX_Texture_Wrapper_Load(tw);
    }
    if ((tw->loadingState.unload || unloadafterstate) && tw->loadingState.loaded) {
        gX_Texture_Wrapper_Unload(tw);
    }
    if (tw->loadingState.reload && tw->loadingState.loaded) {
        gX_Texture_Wrapper_Unload(tw);
        gX_Texture_Wrapper_Load(tw);
    }
    if (tw->filterChanged && tw->loadingState.loaded) {
        gX_Texture_SetFilter(tw->texture,tw->filter);
        tw->filterChanged = false;
    }
    if (tw->wrapChanged && tw->loadingState.loaded) {
        gX_Texture_SetWrap(tw->texture,tw->wrap);
        tw->wrapChanged = false;
    }

    //opencode: generate bindless handle after texture is first loaded
    if (tw->loadingState.loaded && tw->bindlessHandle == 0 && gX_BindlessSupported && tw->texture.id > 0) {
        tw->bindlessHandle = gX_Texture_GetHandle(tw->texture);
        if (tw->bindlessHandle != 0) {
            gX_Texture_MakeResident(tw->bindlessHandle);
            tw->handleResident = true;
        }
    }
}

//opencode: bindless texture — machinery lives in RHI (rhi_bindless.c);
//   the extension's function pointers are loaded via the backend
//   proc-address callback. These wrappers only delegate.

gX_I0 gX_Bindless_CheckSupport(gX_I0)
{
    gX_BindlessSupported = gX_RHI_Bindless_Init();
}

gX_U64 gX_Texture_GetHandle(gX_Texture texture)
{
    if (!gX_BindlessSupported || texture.id == 0) return 0;
    return gX_RHI_Bindless_GetHandle(texture.id, 0);
}

gX_I1 gX_Texture_MakeResident(gX_U64 handle)
{
    if (!gX_BindlessSupported) return 0;
    return gX_RHI_Bindless_MakeResident(handle);
}

gX_I0 gX_Texture_ReleaseHandle(gX_U64 handle)
{
    if (!gX_BindlessSupported) return;
    gX_RHI_Bindless_Release(handle);
}

gX_I0 gX_Shader_SetValue_Handle64(gX_Shader shader, gX_I32 loc, gX_U64 handle)
{
    if (loc < 0 || shader.id == 0) return;
    gX_RHI_Shader_Use(shader.id);
    gX_RHI_Bindless_UniformHandle64(loc, handle);
}
