/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers_texture.c]
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

#include "../../common/config.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "raylib_wrappers.h"

#include "rlgl.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/batch.h"
#include "../../gxmath/constants.h"
#include "../../gxmath/matrix.h"

#include "../../main.h"
#include "../../camera.h"
#include "../../rhi/rhi_fbo.h"
#include "../../wrappers/convert.h"
#include "../../wrappers/texture.h"

//opencode: texture draw via direct rlgl batch quad (replaces raylib DrawTexturePro).
//   Pipeline: src(texture rect) → dst(screen rect) → rotate → offset/origin → tint
//   src:     sub-rect or full texture (w==0||h==0 = full texture)
//   dst:     destination top-left + size
//   origin:  pivot OFFSET from CENTER of dst ({0,0} = center)
//   offset:  extra translation after rotation
//   scale:   size multiplier. Negative = flip around center.
//   tint:    color modulation
//   Auto-invert for RTs: swap UV rows instead of negating src.height,
//   since we control UVs directly (no DrawTexturePro src.height trick).
gX_I0 raylib_Texture_Draw(gX_Texture texture, gX_Texture_Draw_Params params) {
    Color col = gX_Color_ConvertToBackend(params.tint);

    // --- src rect: sub-rect or full texture ---
    float texW = (float)texture.width;
    float texH = (float)texture.height;
    float srcX = 0, srcY = 0, srcW = texW, srcH = texH;
    if (params.src.width > 0 && params.src.height > 0) {
        srcX = params.src.x;
        srcY = params.src.y;
        srcW = params.src.width;
        srcH = params.src.height;
    }

    // --- auto-invert for RT sources: swap UV row order ---
    //   RT: OpenGL row 0 = bottom of visual content. We swap v0↔v1 at UV time
    //   instead of negating src.height (which was the DrawTexturePro trick).
    //   Vulkan framebuffers are TOP-origin (row 0 = top of the image) — RTs are
    //   already oriented like file textures, so the flip must NOT apply (it
    //   rendered every RT composite upside down on the Vulkan backend).
    gX_I1 invertY = 0;
    invertY = gX_Texture_IsRT(texture.id) ? 1 : 0;

    // --- scale: size multiplier + flip axes ---
    //   Negative scale.x flips X (swap u0↔u1).
    //   Negative scale.y flips Y (swap v0↔v1, XOR with RT auto-invert).
    gX_F32 absScaleX = (params.scale.x >= 0) ? params.scale.x : -params.scale.x;
    gX_F32 absScaleY = (params.scale.y >= 0) ? params.scale.y : -params.scale.y;
    if (params.scale.x < 0) { srcW = -srcW; }
    if (params.scale.y < 0) { invertY = !invertY; }

    // --- dst rect with scale applied ---
    float destW = (params.dst.width > 0) ? (float)params.dst.width * absScaleX : texW * absScaleX;
    float destH = (params.dst.height > 0) ? (float)params.dst.height * absScaleY : texH * absScaleY;

    //opencode: pivot uses UNSCALED dst center so scale expands from center, not top-left.
    //   origin offsets the pivot from the original dst center ({0,0}=center).
    float unscaledCenterX = (params.dst.width > 0) ? (float)params.dst.width * 0.5f : 0.0f;
    float unscaledCenterY = (params.dst.height > 0) ? (float)params.dst.height * 0.5f : 0.0f;
    float pivotX = unscaledCenterX + params.origin.x;
    float pivotY = unscaledCenterY + params.origin.y;

    // --- UV coords from src rect ---
    float u0 = srcX / texW;
    float v0 = srcY / texH;
    float u1 = (srcX + srcW) / texW;
    float v1 = (srcY + srcH) / texH;
    if (invertY) { float tmp = v0; v0 = v1; v1 = tmp; }

    //opencode: draw textured quad via gX_Batch with CPU-side vertex pre-transform.
    //   Replaces rlgl matrix stack (translate+rotate) by computing final screen
    //   positions from the rotation angle and pivot, then submitting to the batch.
    gX_Batch_SetTexture(texture.id);
    {
        gX_F32 cosR = cosf(params.rotation * (gX_F32)gX_DEG2RAD);
        gX_F32 sinR = sinf(params.rotation * (gX_F32)gX_DEG2RAD);
        gX_F32 hw = destW * 0.5f, hh = destH * 0.5f;
        gX_F32 cx = params.dst.x + params.offset.x + pivotX;
        gX_F32 cy = params.dst.y + params.offset.y + pivotY;
        gX_F32 tlx = cx - hw * cosR + hh * sinR;
        gX_F32 tly = cy - hw * sinR - hh * cosR;
        gX_F32 blx = cx - hw * cosR - hh * sinR;
        gX_F32 bly = cy - hw * sinR + hh * cosR;
        gX_F32 brx = cx + hw * cosR - hh * sinR;
        gX_F32 bry = cy + hw * sinR + hh * cosR;
        gX_F32 trx = cx + hw * cosR + hh * sinR;
        gX_F32 trY = cy + hw * sinR - hh * cosR;

        gX_Batch_Begin(GX_BATCH_QUADS);
        gX_Batch_Color4ub(col.r, col.g, col.b, col.a);
        gX_Batch_TexCoord2f(u0, v0); gX_Batch_Vertex2f(tlx, tly);
        gX_Batch_TexCoord2f(u0, v1); gX_Batch_Vertex2f(blx, bly);
        gX_Batch_TexCoord2f(u1, v1); gX_Batch_Vertex2f(brx, bry);
        gX_Batch_TexCoord2f(u1, v0); gX_Batch_Vertex2f(trx, trY);
        gX_Batch_End();
    }
}

//modified DrawTexturePro() & DrawTextCodepoint3D() from text_draw_3d.c from raylib examples
gX_I0 raylib_Texture_Draw3D(gX_Texture texture, gX_Texture_Draw_Params params, gX_F32v3 pos) {
    Rectangle source = (Rectangle){0.0f,0.0f,(float)texture.width,(float)texture.height};
    Rectangle dest = (Rectangle){params.dst.x,params.dst.y,params.dst.width,params.dst.height};
    Vector2 resolution = (Vector2){maindata.screenWidth,maindata.screenHeight};
    Vector3 position = (Vector3){pos.x,pos.y,pos.z};
    Color tint = (Color){params.tint.r,params.tint.g,params.tint.b,params.tint.a};
    //Vector3 position = params.pos;
    // Check if texture is valid
    if (texture.id > 0) {
        float width = (float)texture.width;
        float height = (float)texture.height;
        
        float aspectratio = resolution.y/resolution.x;

        bool flipX = false;

        if (source.width < 0) { flipX = true; source.width *= -1; }
        if (source.height < 0) source.y -= source.height;


        gX_Batch_SetTexture(texture.id);
        gX_RHI_Disable(GL_CULL_FACE);
        {
            gX_Matrix S = gX_Matrix_Scale(-4.0f / (gX_F32)aspectratio, -4.0f, -1.0f);
            gX_Matrix T1 = gX_Matrix_Translate(-0.5f, -0.5f, 0.0f);
            gX_Matrix T2 = gX_Matrix_Translate(position.x / resolution.x, position.y / resolution.y, position.z / resolution.y);
            gX_Matrix M = gX_Matrix_Multiply(S, gX_Matrix_Multiply(T1, T2));
            gX_F32 vx[4] = {0, dest.width / resolution.x, dest.width / resolution.x, 0};
            gX_F32 vy[4] = {0, 0, dest.height / resolution.y, dest.height / resolution.y};
            gX_F32 vz[4] = {0, 0, 0, 0};
            gX_F32 uvA, uvB, uvC, uvD;
            uvA = source.x / width; uvB = source.y / height;
            uvC = (source.x + source.width) / width; uvD = (source.y + source.height) / height;

            gX_Batch_Begin(GX_BATCH_QUADS);
            gX_Batch_Color4ub(tint.r, tint.g, tint.b, tint.a);

            gX_Batch_Normal3f(0.0f, 1.0f, 0.0f);
            for (gX_I32 fi = 0; fi < 4; fi++) {
                gX_F32 tx = M.m0*vx[fi]+M.m4*vy[fi]+M.m8*vz[fi]+M.m12;
                gX_F32 ty = M.m1*vx[fi]+M.m5*vy[fi]+M.m9*vz[fi]+M.m13;
                gX_F32 tz = M.m2*vx[fi]+M.m6*vy[fi]+M.m10*vz[fi]+M.m14;
                gX_Batch_TexCoord2f(flipX ? (fi<2?uvC:uvA) : (fi<2?uvA:uvC), fi==0||fi==3?uvB:uvD);
                gX_Batch_Vertex3f(tx, ty, tz);
            }

            gX_Batch_Normal3f(0.0f, -1.0f, 0.0f);
            for (gX_I32 bi = 0; bi < 4; bi++) {
                gX_F32 tx = M.m0*vx[bi]+M.m4*vy[bi]+M.m8*vz[bi]+M.m12;
                gX_F32 ty = M.m1*vx[bi]+M.m5*vy[bi]+M.m9*vz[bi]+M.m13;
                gX_F32 tz = M.m2*vx[bi]+M.m6*vy[bi]+M.m10*vz[bi]+M.m14;
                gX_Batch_TexCoord2f(flipX ? (bi<2?uvC:uvA) : (bi<2?uvA:uvC), bi==0||bi==3?uvB:uvD);
                gX_Batch_Vertex3f(tx, ty, tz);
            }

            gX_Batch_End();
        }
    }
}

RenderTexture2D raylib_RenderTexture_Load(gX_I32 width, gX_I32 height, gX_I1 depth, gX_I1 floatingpoint) {
    TraceLog(LOG_TRACE,"RAYLIB: Loading Rendertexture");
    if (depth) TraceLog(LOG_TRACE,"RAYLIB: using depth");
    if (floatingpoint) TraceLog(LOG_TRACE,"RAYLIB: using floating point");
    RenderTexture2D target = { 0 };

    target.id = rlLoadFramebuffer();   // Load an empty framebuffer

    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);

        //opencode: allocate texture with opaque-black data (alpha=255) instead
        //   of NULL. rlLoadTexture with NULL data leaves the texture storage
        //   uninitialized, so the first frame's render to this RT can sample
        //   garbage alpha=0 (transparent) and the canvas composite shows
        //   the default framebuffer through it. Initializing to (0,0,0,255)
        //   = opaque black gives a known starting state. calloc zero-fills
        //   RGB to 0; we set alpha=255 explicitly below.
        //opencode: the R16G16B16A16 format uploads with glType=GL_HALF_FLOAT
        //   (rlgl.h:3642), so the data must be unsigned short half-floats.
        //   Previously a gX_F32* buffer was passed — every 32-bit float was
        //   reinterpreted as TWO half-floats, so the "opaque black" init was
        //   actually garbage (the roadmap's flagged DOF root-cause). 1.0f in
        //   half-float is 0x3C00.
        if (floatingpoint) {
            gX_U16 *blackData = (gX_U16 *)RL_CALLOC(width * height * 4, sizeof(gX_U16));
            //opencode: set alpha=1.0 (half-float) for all pixels (calloc zeroes RGB).
            for (gX_I32 i = 0; i < width * height; i++) blackData[i*4 + 3] = 0x3C00;
            target.texture.id = rlLoadTexture(blackData, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
            target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
            RL_FREE(blackData);
        }
        else {
            gX_U8 *blackData = (gX_U8 *)RL_CALLOC(width * height * 4, sizeof(gX_U8));
            //opencode: set alpha=255 for all pixels (calloc only zeroes).
            for (gX_I32 i = 0; i < width * height; i++) blackData[i*4 + 3] = 255;
            target.texture.id = rlLoadTexture(blackData, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
            target.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            RL_FREE(blackData);
        }
        target.texture.width = width;
        target.texture.height = height;
        target.texture.mipmaps = 1;

        // Create depth texture buffer (instead of raylib default renderbuffer)
        if (depth) {
            target.depth.id = rlLoadTextureDepth(width, height, false);
            target.depth.width = width;
            target.depth.height = height;
            target.depth.format = 19;       //DEPTH_COMPONENT_24BIT?
            target.depth.mipmaps = 1;
        }

        // Attach color texture and depth texture to FBO
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        if (depth) {
            rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        }

        // Check if fbo is complete with attachments (valid)
        if (rlFramebufferComplete(target.id)) TRACELOG(LOG_TRACE, "FBO: [ID %i] Framebuffer object created successfully", target.id);

        rlDisableFramebuffer();
    }
    else TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

    return target;
}
