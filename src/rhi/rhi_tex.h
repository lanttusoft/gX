/**********************************************************************************************
*
*   [gX: src/rhi/rhi_tex.h]
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
//   Wraps glTexImage2D, glTexParameteri, glGenerateMipmap, glDeleteTextures.
//   Ensures consistent format/parameter setup across the engine.
#ifndef RHI_TEX_H
#define RHI_TEX_H

#include "../common/types.h"

// Create a 2D texture. Returns texture ID (0 = failure).
//   internalFormat, format, type match glTexImage2D parameters.
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Tex_Create2D(gX_U32 width, gX_U32 height,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const void *data,
    const gX_I8 *name);

// Generate one or more texture names (glGenTextures) without allocating storage.
//   Useful for 2D array textures where texStorage3D is used for allocation.
//   target: the texture's eventual target (gX_RHI_TEXTURE_2D_ARRAY, etc.) —
//   bound + labeled + registered so later storage calls and binds work.
gX_U32 gX_RHI_Tex_Gen(gX_U32 target, const gX_I8 *name);

// Create a 2D texture using immutable storage (glTexStorage2D + glTexSubImage2D).
//   Required on NVIDIA GPUs where mutable glTexImage2D produces zero-pixel data.
//   Unlike Create2D, this requires a sized internal format (gX_RHI_RGBA8 not gX_RHI_RGBA).
//   Without immutable storage (pre-4.2 desktop GL, no ARB_texture_storage) the
//   RHI allocates the same texture with mutable glTexImage2D instead.
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Tex_Create2D_Immutable(gX_U32 width, gX_U32 height,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const void *data,
    const gX_I8 *name);

// Create a cubemap texture (6 faces, each size×size). Returns texture ID (0 = failure).
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Tex_CreateCubemap(gX_U32 size,
    gX_I32 internalFormat, gX_I32 format, gX_I32 type, const gX_I8 *name);

// Set a single integer texture parameter (glTexParameteri).
//   texId=0 means "currently bound texture".
gX_I0 gX_RHI_Tex_Param(gX_U32 texId, gX_I32 target, gX_I32 pname, gX_I32 param);

// Set a single float texture parameter (glTexParameterf).
//   texId=0 means "currently bound texture".
gX_I0 gX_RHI_Tex_Paramf(gX_U32 texId, gX_I32 target, gX_I32 pname, gX_F32 param);

// Set float vector texture parameter (glTexParameterfv).
//   texId=0 means "currently bound texture".
gX_I0 gX_RHI_Tex_Paramfv(gX_U32 texId, gX_I32 target, gX_I32 pname, const gX_F32 *param);

// Generate mipmaps for a texture (glGenerateMipmap).
//   target: gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_CUBE_MAP, etc. — the texture's own target.
gX_I0 gX_RHI_Tex_GenMipmap(gX_U32 texId, gX_U32 target);

// Delete a texture (glDeleteTextures). texId=0 is a no-op.
gX_I0 gX_RHI_Tex_Delete(gX_U32 texId);

// Upload/replace pixel data (glTexSubImage2D).
gX_I0 gX_RHI_Tex_Upload(gX_U32 texId, gX_I32 target, gX_I32 level,
    gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_I32 format, gX_I32 type, const void *data);

// Create a multisampled 2D texture (glTexImage2DMultisample).
//   samples = MSAA sample count (2, 4, 8).
//   internalFormat = sized format (e.g. gX_RHI_RGBA16F, gX_RHI_DEPTH_COMPONENT24).
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
//   Returns texture ID (0 = failure).
gX_U32 gX_RHI_Tex_CreateMultisample(gX_U32 samples, gX_I32 internalFormat,
    gX_U32 w, gX_U32 h, const gX_I8 *name);

// Allocate immutable storage for a 3D texture array (glTexStorage3D).
//   Used for CSM shadow map arrays and other layered textures.
//   Without immutable storage the RHI allocates per-level glTexImage3D with
//   an explicit MAX_LEVEL so the texture stays complete.
//   target: gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_CUBE_MAP_ARRAY, etc.
//   levels: mipmap levels (1 for single-level array).
//   internalFormat: sized internal format (e.g. gX_RHI_DEPTH_COMPONENT24).
//   w, h: per-layer dimensions; depth: number of layers.
//   Returns 0 on success.
gX_I32 gX_RHI_Tex_Storage3D(gX_U32 texId, gX_I32 target, gX_I32 levels,
    gX_I32 internalFormat, gX_U32 w, gX_U32 h, gX_U32 depth);

// Allocate immutable 2D/cubemap storage (glTexStorage2D).
//   Defines ALL mip levels atomically — use for cubemaps rendered per-face
//   per-mip (mutable glTexImage2D mip allocation is unreliable on NVIDIA).
//   Without immutable storage the RHI allocates per-face-per-mip glTexImage2D
//   with an explicit MAX_LEVEL so the texture stays complete.
//   texId must be a name from gX_RHI_Tex_Gen (no prior storage).
//   Returns 0 on success.
gX_I32 gX_RHI_Tex_Storage2D(gX_U32 texId, gX_I32 target, gX_I32 levels,
    gX_I32 internalFormat, gX_U32 w, gX_U32 h);

// Bind a 2D/3D texture to a specific target — wraps glBindTexture.
//   Does NOT manage texture units (use gX_RHI_Tex_BindOnUnit for unit management).
gX_I0 gX_RHI_Tex_Bind(gX_U32 texId, gX_I32 target);

// Activate a specific texture unit — wraps glActiveTexture.
gX_I0   gX_RHI_ActiveTexture(gX_U32 unit);
gX_U32  gX_RHI_Tex_GetTarget(gX_U32 texId);

//opencode: texture unit shadow table — tracks bindings for debugging
#define GX_RHI_MAX_TEX_UNITS 32
gX_I0   gX_RHI_Tex_UnitInit(gX_I0);
gX_U32  gX_RHI_Tex_GetBoundOnUnit(gX_U32 slot);
gX_U32  gX_RHI_Tex_GetUnitTarget(gX_U32 slot);
gX_I0   gX_RHI_Tex_SetUnit(gX_U32 slot, gX_U32 texId, gX_U32 target);
gX_I0   gX_RHI_Tex_ClearUnit(gX_U32 slot);
gX_I0   gX_RHI_Tex_EvictUnits(gX_U32 texId);

#endif
