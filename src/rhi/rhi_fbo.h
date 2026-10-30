/**********************************************************************************************
*
*   [gX: src/rhi/rhi_fbo.h]
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


#ifndef RHI_FBO_H
#define RHI_FBO_H

#include "../common/types.h"

// RHI FBO — wraps GL framebuffer object lifecycle + binding.
//   Tracks the currently bound FBO internally to eliminate the
//   raylib/raw GL state mismatch and centralize FBO error checking.
//
//   Pattern:
//     gX_RHI_FBO fbo = gX_RHI_FBO_Create(colorTex, depthTex);
//     gX_RHI_BindFBO(fbo);
//     // ... draw commands ...
//     gX_RHI_UnbindFBO();
//     gX_RHI_FBO_Delete(fbo);

typedef gX_U32 gX_RHI_FBO;  // GL framebuffer name (0 = default framebuffer)

// Lifecycle
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_RHI_FBO gX_RHI_FBO_Create(gX_U32 colorTex, gX_U32 depthTex, const gX_I8 *name);
gX_I0      gX_RHI_FBO_Delete(gX_RHI_FBO fbo);

// Generate FBO names (glGenFramebuffers) — for custom FBO setup.
//   Returns 0 on success, -1 on failure.
gX_I32 gX_RHI_FBO_Gen(gX_U32 count, gX_U32 *fbos);

// Check FBO completeness (glCheckFramebufferStatus). Returns 1 = complete.
gX_I1 gX_RHI_FBO_IsComplete(gX_I0);

// Binding — wraps glBindFramebuffer with internal state tracking
gX_I0 gX_RHI_BindFBO(gX_RHI_FBO fbo);          // binds gX_RHI_FRAMEBUFFER (draw + read)
gX_I0 gX_RHI_BindDrawFBO(gX_RHI_FBO fbo);      // binds gX_RHI_DRAW_FRAMEBUFFER only
gX_I0 gX_RHI_BindReadFBO(gX_RHI_FBO fbo);      // binds gX_RHI_READ_FRAMEBUFFER only

// Unbind — resets to default framebuffer (FBO 0)
gX_I0 gX_RHI_UnbindFBO(gX_I0);
gX_I0 gX_RHI_UnbindDrawFBO(gX_I0);
gX_I0 gX_RHI_UnbindReadFBO(gX_I0);

// State invalidation — resets tracker so next Bind always calls glBindFramebuffer.
//   Call when an external path (raylib's BeginTextureMode, etc.) binds an FBO
//   without going through this API.
gX_I0 gX_RHI_InvalidateFBO(gX_I0);

// State queries — returns currently bound FBO for each target
gX_RHI_FBO gX_RHI_GetDrawFBO(gX_I0);
gX_RHI_FBO gX_RHI_GetReadFBO(gX_I0);
gX_RHI_FBO gX_RHI_GetDrawFBOResolved(gX_I0);

// Attachment query — name of the texture attached to COLOR_ATTACHMENT0
// of the given draw FBO (0 = none). For orphan-attachment forensics
// (wrapper recreates color texture, FBO keeps pointing at the old one).
gX_U32 gX_RHI_FBO_ColorName(gX_RHI_FBO fbo);

// Texture sample count (0/1 = single-sample sampleable; >1 = multisample,
// not sampleable with sampler2D).
gX_U32 gX_RHI_Tex_Samples(gX_U32 tex);

// Blit — convenience wrapper for glBlitFramebuffer
gX_I0 gX_RHI_Blit(gX_RHI_FBO src, gX_RHI_FBO dst,
                   gX_U32 srcX, gX_U32 srcY,
                   gX_U32 srcW, gX_U32 srcH,
                   gX_U32 dstX, gX_U32 dstY,
                   gX_U32 dstW, gX_U32 dstH,
                   gX_U32 mask, gX_U32 filter);

// Renderbuffer — wraps glGenRenderbuffers + glRenderbufferStorage
gX_U32 gX_RHI_RBO_Create(gX_U32 w, gX_U32 h, gX_U32 internalFormat);
gX_I0  gX_RHI_RBO_Delete(gX_U32 rbo);

// Attach a renderbuffer as depth attachment to the currently bound FBO
gX_I0 gX_RHI_FBO_AttachDepthRBO(gX_U32 rbo);

// Attach a cubemap face as color attachment to the currently bound FBO.
//   face = 0..5 for gX_RHI_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z
gX_I0 gX_RHI_FBO_AttachCubeFace(gX_U32 texId, gX_U32 face);

// Attach a cubemap face as DEPTH attachment (point light shadow cubemaps).
//   face = 0..5. The color-attachment variant is wrong for depth cubemaps —
//   attaching a depth texture to a color slot makes the FBO incomplete.
gX_I0 gX_RHI_FBO_AttachCubeFaceDepth(gX_U32 texId, gX_U32 face);

// Attach a cubemap face at a specific mip level.
//   face = 0..5, level = mip level (0 = base).
gX_I0 gX_RHI_FBO_AttachCubeFaceLevel(gX_U32 texId, gX_U32 face, gX_I32 level);

// Attach a single layer of a 2D texture array as attachment — wraps glFramebufferTextureLayer
//   Used for CSM shadow map arrays where each cascade maps to one array layer.
//   attachment: gX_RHI_DEPTH_ATTACHMENT, gX_RHI_COLOR_ATTACHMENT0, etc.
//   texId: the texture array ID; layer: which array layer (0-based).
//   level: mipmap level (0 for base).
gX_I0 gX_RHI_FBO_AttachLayer(gX_U32 texId, gX_I32 attachment, gX_I32 level, gX_I32 layer);

// Attach a 2D texture to a color attachment slot of the currently bound FBO.
//   attachment = gX_RHI_COLOR_ATTACHMENT0..gX_RHI_COLOR_ATTACHMENTn, or gX_RHI_DEPTH_ATTACHMENT.
gX_I0 gX_RHI_FBO_AttachTexture(gX_U32 texId, gX_I32 attachment, gX_I32 target);

//opencode: attach a specific mip LEVEL of a 2D texture as a color attachment
gX_I0 gX_RHI_FBO_AttachTextureLevel(gX_U32 texId, gX_I32 attachment, gX_I32 target, gX_I32 level);

// Set draw/read buffers for the currently bound FBO
gX_I0 gX_RHI_DrawBuffer(gX_U32 buf);
gX_I0 gX_RHI_ReadBuffer(gX_U32 buf);

// Set multiple draw buffers (MRT) — wraps glDrawBuffers
gX_I0 gX_RHI_DrawBuffers(gX_I32 count, const gX_U32 *bufs);

// Viewport and clear — wraps glViewport, glClear, glClearColor
gX_I0 gX_RHI_Viewport(gX_I32 x, gX_I32 y, gX_I32 w, gX_I32 h);
gX_I0 gX_RHI_Clear(gX_U32 mask);
gX_I0 gX_RHI_ClearColor(gX_F32 r, gX_F32 g, gX_F32 b, gX_F32 a);

#endif
