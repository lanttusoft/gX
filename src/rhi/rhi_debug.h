/**********************************************************************************************
*
*   [gX: src/rhi/rhi_debug.h]
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


//opencode: RHI Debug — GL_KHR_debug message callback, object labeling, push/pop groups.
//   Provides centralized debug annotation and error capture for RenderDoc/NSight.
//   All functions gracefully no-op when GL_KHR_debug is unavailable.
#ifndef RHI_DEBUG_H
#define RHI_DEBUG_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Initialization ───
// Registers a GL debug message callback. Call once after GL context creation.
// Messages are routed through gX_TraceLog at appropriate severity levels.
gX_I0 gX_RHI_Debug_Init(gX_I0);

// Returns 1 if GL_KHR_debug is available (GL 4.3+ or extension).
gX_I1 gX_RHI_Debug_HasKHR(gX_I0);

// ─── Debug groups (scoped annotations for RenderDoc) ───
gX_I0 gX_RHI_Debug_PushGroup(const gX_I8 *name);
gX_I0 gX_RHI_Debug_PopGroup(gX_I0);

// ─── Object labeling (names visible in RenderDoc/NSight) ───
// identifier: the GL object name (e.g., texture ID, program ID, FBO ID)
// glEnum: the object type (gX_RHI_TEXTURE, gX_RHI_PROGRAM, gX_RHI_FRAMEBUFFER, gX_RHI_BUFFER, etc.)
// label: a human-readable name (up to 256 chars)
gX_I0 gX_RHI_Debug_Label(gX_U32 identifier, gX_U32 glEnum, const gX_I8 *label);

// ─── Error hints ───
//opencode: error-hint table - maps an error/GLSL message to one actionable
//   hint string (or NULL). Shared by the GL callback, the shader compile
//   logs and the validate warnings so every error carries its recipe.
//   First substring match wins; matching is case-sensitive (GL/NVIDIA and
//   GLSL messages have stable case). Extend in rhi_debug.c, document in
//   docs/error_hints.md.
const gX_I8 *gX_RHI_Hint_For(const gX_I8 *message);

#ifdef __cplusplus
}
#endif

#endif
