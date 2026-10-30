/**********************************************************************************************
*
*   [gX: src/rhi/rhi_bindless.h]
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


//opencode: RHI Bindless — GL_ARB_bindless_texture wrappers.
//   Owns the extension's function pointers (loaded via a backend-provided
//   proc-address callback) so engine code never touches GL function
//   pointers directly. The material-map bindless draw path in
//   renderer/gfx/model.c stays compiled out (GX_BINDLESS_SUPPORTED is
//   undefined — the unit-based binding loop is the active path); this
//   module only relocates the machinery out of wrappers/texture.c.
#ifndef RHI_BINDLESS_H
#define RHI_BINDLESS_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Backend-provided OpenGL proc-address loader (raylib: rlGetProcAddress,
//   GLFW: glfwGetProcAddress). Must be registered before Bindless_Init.
typedef void *(*gX_RHI_ProcAddressFn)(const gX_I8 *name);

gX_I0 gX_RHI_SetProcAddressFn(gX_RHI_ProcAddressFn fn);
//ai: extension entry points share the backend-owned resolver.
gX_I0 *gX_RHI_GetProcAddress(const gX_I8 *name);

// Load GL_ARB_bindless_texture function pointers + detect support.
//   Returns 1 when bindless is available. Call after backend init.
gX_I1 gX_RHI_Bindless_Init(gX_I0);

// 1 when the extension is loaded and functional.
gX_I1 gX_RHI_Bindless_IsSupported(gX_I0);

// Generate a 64-bit handle for a texture (glGetTextureSamplerHandleARB).
//   sampler: 0 for a regular sampler, or a sampler object name.
gX_U64 gX_RHI_Bindless_GetHandle(gX_U32 texId, gX_U32 sampler);

// Make a handle resident for shader access (glMakeTextureHandleResidentARB).
gX_I1 gX_RHI_Bindless_MakeResident(gX_U64 handle);

// Release a resident handle (glReleaseTextureHandleResidentARB).
gX_I0 gX_RHI_Bindless_Release(gX_U64 handle);

// Upload a bindless handle to a uniform (glUniformHandleui64ARB).
//   The program must already be active (gX_RHI_Shader_Use).
gX_I0 gX_RHI_Bindless_UniformHandle64(gX_I32 location, gX_U64 handle);

#ifdef __cplusplus
}
#endif

#endif
