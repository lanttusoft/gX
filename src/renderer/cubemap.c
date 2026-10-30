/**********************************************************************************************
*
*   [gX: src/renderer/cubemap.c]
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


//opencode: cubemap generation utility — RHI-ified.

#include "cubemap.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../gxmath/vector3.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_tex.h"
#include "../rhi/rhi_state.h"
#include "../rhi/gl_state.h"

//ai: opt-in raw HDR face readback after drawing; never replaces cubemap content or changes shading.
static gX_I0 gx_cubemap_hdr_dump(gX_U32 Size, gX_I32 Face)
{
    const gX_I8 *Prefix = getenv("GX_CUBEMAP_HDR_PREFIX");
    if (!Prefix || !Prefix[0]) return;
    if (strncmp(Prefix, "/tmp/", 5) != 0 || strlen(Prefix) > 450) return;
    static gX_U32 Capture = 0;
    static gX_U32 Written = 0;
    if (Face == 0) Capture++;
    const gX_I8 *TargetHook = getenv("GX_CUBEMAP_HDR_CAPTURE");
    gX_U32 Target = TargetHook ? (gX_U32)strtoul(TargetHook, NULL, 10) : 30;
    if (Target == 0) Target = 30;
    if (Face == 0 && Capture == 1)
        gX_TraceLog(gX_LOG_NOTICE, "CUBEMAP_HDR: first capture size=%u target=%u", Size, Target);
    if (Capture != Target || (Written & (1u << Face))) return;
    if (Size == 0 || Size > 4096) return;
    gX_U64 Count = (gX_U64)Size * Size * 4;
    gX_F32 *Pixels = malloc((size_t)Count * sizeof(*Pixels));
    if (!Pixels)
    {
        gX_TraceLog(gX_LOG_WARNING, "CUBEMAP_HDR: allocation failed face=%d", Face);
        return;
    }
    gX_RHI_ReadPixels(0, 0, (gX_I32)Size, (gX_I32)Size,
        gX_RHI_RGBA, gX_RHI_FLOAT, Pixels);
    gX_I8 Path[512];
    snprintf(Path, sizeof(Path), "%s.face%d.bin", Prefix, Face);
    FILE *File = fopen(Path, "wb");
    gX_U32 Header[3] = {Size, Size, 4};
    gX_I1 Complete = 0;
    if (File)
    {
        Complete = fwrite("GXHDR1\0\0", 1, 8, File) == 8
            && fwrite(Header, sizeof(*Header), 3, File) == 3
            && fwrite(Pixels, sizeof(*Pixels), (size_t)Count, File) == (size_t)Count;
        if (fclose(File) != 0) Complete = 0;
    }
    free(Pixels);
    if (Complete)
    {
        Written |= 1u << Face;
        gX_TraceLog(gX_LOG_NOTICE, "CUBEMAP_HDR: path=%s capture=%u face=%d", Path, Capture, Face);
    }
    else gX_TraceLog(gX_LOG_WARNING, "CUBEMAP_HDR: write failed path=%s", Path);
}

const gX_F32v3 gX_CUBEMAP_TARGETS[6] = {
    { 1,0,0}, {-1,0,0}, {0, 1,0}, {0,-1,0}, {0,0, 1}, {0,0,-1}
};
const gX_F32v3 gX_CUBEMAP_UPS[6] = {
    {0,-1,0}, {0,-1,0}, {0,0, 1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
};

gX_Texture gX_Cubemap_Create(gX_U32 size) {
    gX_Texture tex = {0};
    gX_U32 glTex = gX_RHI_Tex_CreateCubemap(size, gX_RHI_RGB, gX_RHI_RGB, gX_RHI_UNSIGNED_BYTE, NULL);
    gX_RHI_Tex_Param(glTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
    gX_RHI_Tex_Param(glTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
    gX_RHI_Tex_Param(glTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(glTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_Param(glTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);
    gX_RHI_Tex_GenMipmap(glTex, gX_RHI_TEXTURE_CUBE_MAP);
    tex.id = glTex;
    tex.width = (gX_I32)size;
    tex.height = (gX_I32)size;
    return tex;
}

gX_I0 gX_Cubemap_Destroy(gX_Texture cubemap) {
    gX_RHI_Tex_Delete(cubemap.id);
}

gX_I0 gX_Cubemap_Render(gX_Texture cubemap, gX_U32 size,
                         gX_F32v3 position, gX_F32v3 clearColor,
                         gX_Cubemap_DrawFace drawFn)
{
    if (cubemap.id == 0 || !drawFn || size == 0) return;

    gX_GL_State_Push();
    gX_U32 prevFbo = gX_RHI_GetDrawFBO();

    gX_U32 depthRbo = gX_RHI_RBO_Create(size, size, gX_RHI_DEPTH_COMPONENT24);
    gX_U32 fbo = gX_RHI_FBO_Create(0, 0, "cubemap_render");
    if (!fbo || !depthRbo) {
        gX_RHI_FBO_Delete(fbo);
        gX_RHI_RBO_Delete(depthRbo);
        goto cleanup;
    }
    gX_RHI_BindFBO(fbo);
    gX_RHI_FBO_AttachDepthRBO(depthRbo);
    gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);

    gX_RHI_Enable(gX_RHI_TEXTURE_CUBE_MAP_SEAMLESS);
    for (int face = 0; face < 6; face++) {
        gX_RHI_FBO_AttachCubeFace(cubemap.id, face);
        gX_RHI_Viewport(0, 0, (gX_I32)size, (gX_I32)size);
        gX_RHI_ClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT);

        gX_Camera3D faceCam = {0};
        faceCam.position = position;
        faceCam.target = gX_F32v3_Add(position, gX_CUBEMAP_TARGETS[face]);
        faceCam.up = gX_CUBEMAP_UPS[face];
        faceCam.fovy = 90.0f;
        faceCam.projection = 0;

        drawFn(&faceCam, face);
    }

    gX_RHI_Tex_GenMipmap(cubemap.id, gX_RHI_TEXTURE_CUBE_MAP);
    gX_RHI_FBO_Delete(fbo);
    gX_RHI_RBO_Delete(depthRbo);
    gX_RHI_BindFBO(prevFbo);
cleanup:
    //opencode: use one cleanup Pop so static scope accounting matches both
    //   the successful path and the partial-allocation failure path.
    gX_GL_State_Pop();
}

gX_I0 gX_Cubemap_RenderEx(gX_Texture cubemap, gX_U32 size,
                           gX_F32v3 position, gX_F32v3 clearColor,
                           gX_Cubemap_DrawFace drawFn,
                           gX_U32 fbo, gX_U32 depthRbo)
{
    if (cubemap.id == 0 || !drawFn || size == 0) return;

    gX_GL_State_Push();
    gX_U32 prevFbo = gX_RHI_GetDrawFBO();

    gX_U32 useFbo = fbo;
    gX_U32 useDepth = depthRbo;
    gX_I1 ownFbo = (fbo == 0);
    if (ownFbo) {
        useDepth = gX_RHI_RBO_Create(size, size, gX_RHI_DEPTH_COMPONENT24);
        useFbo = gX_RHI_FBO_Create(0, 0, "cubemap_faces");
        if (!useFbo || !useDepth) {
            goto cleanup;
        }
        gX_RHI_BindFBO(useFbo);
        gX_RHI_FBO_AttachDepthRBO(useDepth);
        gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    } else {
        gX_RHI_BindFBO(useFbo);
    }

    gX_RHI_Enable(gX_RHI_TEXTURE_CUBE_MAP_SEAMLESS);
    //opencode: deterministic face audit — replace the face content with a
    //   unique color and read it back, allowing the harness to verify all six
    //   attachments, target/up conventions, and repeated updates.
    const gX_I8 *faceDebug = getenv("GX_CUBEMAP_FACE_DEBUG");
    static const gX_F32v3 faceDebugColors[6] = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}
    };
    gX_I1 seamless = gX_RHI_IsEnabled(gX_RHI_TEXTURE_CUBE_MAP_SEAMLESS);
    for (int face = 0; face < 6; face++) {
        gX_RHI_FBO_AttachCubeFace(cubemap.id, face);
        gX_RHI_Viewport(0, 0, (gX_I32)size, (gX_I32)size);
        gX_F32v3 faceColor = faceDebug ? faceDebugColors[face] : clearColor;
        gX_RHI_ClearColor(faceColor.x, faceColor.y, faceColor.z, 1.0f);
        gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT);

        gX_Camera3D faceCam = {0};
        faceCam.position = position;
        faceCam.target = gX_F32v3_Add(position, gX_CUBEMAP_TARGETS[face]);
        faceCam.up = gX_CUBEMAP_UPS[face];
        faceCam.fovy = 90.0f;
        faceCam.projection = 0;

        if (faceDebug) {
            gX_U8 pixel[4] = {0, 0, 0, 0};
            gX_RHI_ReadPixels((gX_I32)(size / 2), (gX_I32)(size / 2), 1, 1,
                gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, pixel);
            gX_TraceLog(gX_LOG_NOTICE,
                "CUBEMAP_FACE_AUDIT face=%d target=(%.0f,%.0f,%.0f) up=(%.0f,%.0f,%.0f) seamless=%d rgba=(%u,%u,%u,%u)",
                face, faceCam.target.x - position.x, faceCam.target.y - position.y,
                faceCam.target.z - position.z, faceCam.up.x, faceCam.up.y, faceCam.up.z,
                (int)seamless,
                (unsigned)pixel[0], (unsigned)pixel[1], (unsigned)pixel[2], (unsigned)pixel[3]);
            continue;
        }
        drawFn(&faceCam, face);
        gx_cubemap_hdr_dump(size, face);
    }

    gX_RHI_Tex_GenMipmap(cubemap.id, gX_RHI_TEXTURE_CUBE_MAP);
cleanup:
    //opencode: converge owned-resource release and state restoration so the
    //   static GL scope audit sees one Pop for the one Push.
    if (ownFbo) {
        gX_RHI_FBO_Delete(useFbo);
        gX_RHI_RBO_Delete(useDepth);
    }
    gX_RHI_BindFBO(prevFbo);
    gX_GL_State_Pop();
}
