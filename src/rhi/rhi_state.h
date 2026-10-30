/**********************************************************************************************
*
*   [gX: src/rhi/rhi_state.h]
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


//opencode: RHI GL State — centralized wrappers for GL state management.
//   Replaces direct rlgl state calls (rlEnableScissorTest, rlSetBlendMode,
//   rlDisableBackfaceCulling, etc.) with backend-agnostic RHI wrappers.
#ifndef RHI_STATE_H
#define RHI_STATE_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enable/disable/query a capability (gX_RHI_BLEND, gX_RHI_DEPTH_TEST, gX_RHI_SCISSOR_TEST,
//   gX_RHI_CULL_FACE, etc.). Thin wrapper around glEnable/glDisable/glIsEnabled.
gX_I0 gX_RHI_Enable(gX_U32 cap);
gX_I0 gX_RHI_Disable(gX_U32 cap);
gX_I1 gX_RHI_IsEnabled(gX_U32 cap);

// Set scissor rectangle — wraps glScissor.
gX_I0 gX_RHI_Scissor(gX_I32 x, gX_I32 y, gX_I32 w, gX_I32 h);

// Blend mode — maps rlgl blend-mode ids (gX_RHI_BLEND_ALPHA,
//   gX_RHI_BLEND_ADDITIVE, gX_RHI_BLEND_MULTIPLIED, etc.) to GL blend state.
//   Handles glEnable/glDisable(gX_RHI_BLEND) + glBlendFunc + glBlendEquation.
gX_I0 gX_RHI_Blend_Mode(gX_I32 mode);

// Separate blend function — wraps glBlendFuncSeparate + glBlendEquationSeparate.
//   factor values match OpenGL GLenum constants (gX_BLEND_* in drawing.h).
gX_I0 gX_RHI_Blend_Func_Separate(gX_U32 srcRGB, gX_U32 dstRGB,
    gX_U32 srcAlpha, gX_U32 dstAlpha);

// Set blend equation — wraps glBlendEquation.
gX_I0 gX_RHI_Blend_Equation(gX_U32 mode);

// Set cull face mode — wraps glCullFace (gX_RHI_FRONT, gX_RHI_BACK, gX_RHI_FRONT_AND_BACK).
gX_I0 gX_RHI_Cull_Mode(gX_U32 mode);

// Set front-face winding — wraps glFrontFace (gX_RHI_CW, gX_RHI_CCW).
gX_I0 gX_RHI_FrontFace(gX_U32 mode);

// Set blend equation for RGB and alpha separately — wraps glBlendEquationSeparate.
gX_I0 gX_RHI_Blend_Equation_Separate(gX_U32 modeRGB, gX_U32 modeAlpha);

// Set color mask — wraps glColorMask.
gX_I0 gX_RHI_Color_Mask(gX_I1 r, gX_I1 g, gX_I1 b, gX_I1 a);

// Set depth write mask — wraps glDepthMask.
gX_I0 gX_RHI_Depth_Write(gX_I1 enable);

// Set stencil write mask — wraps glStencilMask.
gX_I0 gX_RHI_Stencil_Mask(gX_U32 mask);

// Set depth comparison function — wraps glDepthFunc (gX_RHI_LESS, gX_RHI_LEQUAL, etc.).
gX_I0 gX_RHI_Depth_Func(gX_U32 func);

// Set polygon rasterization mode — wraps glPolygonMode (gX_RHI_FILL, gX_RHI_LINE, gX_RHI_POINT).
gX_I0 gX_RHI_PolygonMode(gX_U32 face, gX_U32 mode);

// State queries — wraps glGetIntegerv, glGetBooleanv, glGetError
gX_I0 gX_RHI_GetIntegerv(gX_U32 pname, gX_I32 *data);
gX_I0 gX_RHI_GetBooleanv(gX_U32 pname, gX_I1 *data);
gX_U32 gX_RHI_GetError(gX_I0);

// Per-frame error counter — incremented by every glGetError hit, reset per frame
gX_U32 gX_RHI_GetAndResetErrorCount(gX_I0);
gX_I0  gX_RHI_ResetErrorCount(gX_I0);

// Invalidate all RHI state caches — call after raw GL calls bypassing RHI wrappers
// (e.g., gX_GL_State_Restore which uses glEnable/glDisable directly).
gX_I0 gX_RHI_InvalidateCaches(gX_I0);

// Snapshot current GL state into a string buffer (always available, unlike harness-only gX_GL_DumpState).
//   buf may be NULL (logs via gX_TraceLog instead). Returns number of chars written.
gX_I32 gX_RHI_DumpState(gX_I8 *buf, gX_U64 bufSize, const gX_I8 *label);

// Enable/disable RHI diagnostics (cache consistency checking).
// When enabled, gX_RHI_Diagnostics_Check() compares the RHI cap cache against
// real GL state and logs mismatches. Call after gX_GL_State_Restore to detect
// silent desync caused by raw GL calls that bypass RHI wrappers.
gX_I0 gX_RHI_Diagnostics_SetEnabled(gX_I1 enabled);
gX_I1 gX_RHI_Diagnostics_IsEnabled(gX_I0);
gX_I0 gX_RHI_Diagnostics_Check(const gX_I8 *when);

// Renderer pipeline-stage state dumps (formerly defined in the gfx harness).
//   Enabled via gX_RHI_StageDump_SetEnabled — see gX_GL_DumpState below.
gX_I0 gX_RHI_StageDump_SetEnabled(gX_I1 enabled);
gX_I1 gX_RHI_StageDump_IsEnabled(gX_I0);

// Legacy harness entry points, now provided by RHI:
//   dump GL state at a named pipeline stage (logs via gX_TraceLog).
gX_I0 gX_GL_DumpState(const gX_I8 *stage);
//   quick GL error check at a named code location.
gX_I0 gX_GL_CheckError(const gX_I8 *location);

// Read pixels from the currently bound FBO — wraps glReadPixels.
//   x, y: pixel coordinates (bottom-left origin for FBOs, top-left for screen with flip).
//   w, h: size of region to read; format: gX_RHI_RGBA, gX_RHI_DEPTH_COMPONENT, etc.
//   type: gX_RHI_UNSIGNED_BYTE, gX_RHI_FLOAT, etc.
//   pixels: output buffer (must be large enough for w*h*channels*sizeof(type)).
gX_I0 gX_RHI_ReadPixels(gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_U32 format, gX_U32 type, void *pixels);

//opencode: Stage 1S — ReadPixels with a per-request completion flag. The
//   Vulkan backend's deferred (fbo-0 full-screen) path sets *doneFlag = 1
//   when the copy landed in `pixels` (at the next present's fence wait); the
//   synchronous backends set it immediately. Callers must not consume
//   `pixels` until the flag reads 1 (glfw screenshot queue).
gX_I0 gX_RHI_ReadPixelsDoneFlag(gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_U32 format, gX_U32 type, void *pixels, gX_U8 *doneFlag);

//opencode: Stage 1S — ReadPixels with a per-request completion flag. The
//   Vulkan backend's deferred (fbo-0 full-screen) path sets *doneFlag = 1
//   when the copy landed in `pixels` (at the next present's fence wait); the
//   synchronous backends set it immediately. Callers must not consume
//   `pixels` until the flag reads 1 (glfw screenshot queue).
gX_I0 gX_RHI_ReadPixelsDoneFlag(gX_I32 x, gX_I32 y, gX_U32 w, gX_U32 h,
    gX_U32 format, gX_U32 type, void *pixels, gX_U8 *doneFlag);

//opencode: debug GL error check macro (replaces the removed gl_utils' GX_GL_CHECK).
//   Drains the GL error queue at the call site in debug builds.
#if defined(GX_DEBUG) || !defined(NDEBUG)
  #define GX_GL_CHECK() do { while (gX_RHI_GetError() != 0) {} } while(0)
#else
  #define GX_GL_CHECK() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
