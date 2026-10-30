/**********************************************************************************************
*
*   [gX: src/rhi/rhi_caps.h]
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


//opencode: RHI capabilities — hardware capability report with GL-independent
//   names. The RHI owns GPU detection so engine code can query feature support
//   and limits without including GL headers or knowing GL_* constant names.
//   On non-GL backends gX_RHI_GetCaps() returns a zeroed struct (glMajor=0,
//   shaderModel=0) instead of fabricating GL names.
#ifndef RHI_CAPS_H
#define RHI_CAPS_H

#include "../common/types.h"

//opencode: feature-level ladder used to derive has* flags from the detected
//   GL version (educational levels, NOT GL constants — mirrors the renderer's
//   GX_GL_LEVEL_* ladder in gfx/gl_level.h).
#define GX_RHI_LEVEL_200  200
#define GX_RHI_LEVEL_300  300
#define GX_RHI_LEVEL_310  310
#define GX_RHI_LEVEL_320  320
#define GX_RHI_LEVEL_330  330
#define GX_RHI_LEVEL_400  400
#define GX_RHI_LEVEL_410  410
#define GX_RHI_LEVEL_420  420
#define GX_RHI_LEVEL_430  430

#ifdef __cplusplus
extern "C" {
#endif

//opencode: capability report — filled once by gX_RHI_GetCaps() after backend
//   init. has* flags are hardware support (ignores the educational user
//   level gating in gfx/gl_level.h). glMajor/glMinor = 0 on non-GL backends.
typedef struct gX_RHI_Caps {
    gX_I32 glMajor;                // GL major version (0 = non-GL backend)
    gX_I32 glMinor;                // GL minor version
    gX_I32 shaderModel;            // highest GLSL version as int (330, 400, 430; 0 = none)
    gX_I32 maxTextureUnits;        // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
    gX_I32 maxTextureSize;         // GL_MAX_TEXTURE_SIZE (largest square dimension)
    gX_I32 maxUniformComponents;   // GL_MAX_VERTEX_UNIFORM_COMPONENTS
    gX_I32 maxColorAttachments;    // GL_MAX_COLOR_ATTACHMENTS
    gX_I1 isGLES;                  // OpenGL ES context (vs desktop GL)
    gX_I1 hasFBO;                  // framebuffer objects (GL 3.0+ / ES 3.0+)
    gX_I1 hasTransformFeedback;    // transform feedback (GL 3.0+ / ES 3.0+)
    gX_I1 hasTessellation;         // GL 4.0+ / GL_ARB_tessellation_shader (never in ES)
    gX_I1 hasShaderSubroutine;     // GL 4.0+ / GL_ARB_shader_subroutine (never in ES)
    gX_I1 hasProgramBinary;        // GL 4.1+ / ES 3.0+ / GL_ARB_get_program_binary
    gX_I1 hasBindless;             // GL 4.2+ / GL_ARB_bindless_texture (never in ES)
    gX_I1 hasMultiDrawIndirect;    // GL 4.2+ / ES 3.1+ / GL_ARB_multi_draw_indirect
    gX_I1 hasCompute;              // GL 4.3+ / ES 3.1+ / GL_ARB_compute_shader
    gX_I1 hasSSBO;                 // GL 4.3+ / ES 3.1+ / GL_ARB_shader_storage_buffer_object
    gX_I1 hasCubeMapArray;         // GL 4.0+ / GL_ARB_texture_cube_map_array (never in ES)
    gX_I1 hasGeometryShaders;      // GL 3.2+ / ES 3.2+ / GL_ARB_geometry_shader4
    gX_I1 hasMSTexture;            // multisample textures (GL 3.2+ / ES 3.1+)
    gX_I1 hasInstancing;           // GL 3.1+ / ES 3.0+ / GL_ARB_draw_instanced
    gX_I1 hasImmutableStorage;     // glTexStorage2D/3D (GL 4.2+ / ES 3.0+ / GL_ARB_texture_storage)
} gX_RHI_Caps;

//opencode: query backend capabilities. Lazy — detected on first call and
//   cached. Returns a pointer to the static struct (stable address).
const gX_RHI_Caps *gX_RHI_GetCaps(gX_I0);

//opencode: clear the cached caps so the next gX_RHI_GetCaps() re-detects.
//   Used by tests; not needed in normal operation.
gX_I0 gX_RHI_Caps_Invalidate(gX_I0);

//opencode: GLSL #version header string for the current context. ES contexts
//   get "#version 300 es" (plus precision in fragment shaders — mandatory in
//   ES); a desktop GL 3.0 context gets "#version 130"; later desktop contexts
//   get "#version 330". Used by internal hardcoded shaders (batch, draw2d,
//   default shader) that bypass the preprocessor. GX_GLSL130=1 forces the
//   desktop-130 branch only for the GL3.3-hosted compatibility probe.
const gX_I8 *gX_RHI_Shader_VersionHeader(gX_I1 isFragment);

//opencode: nonzero when hardcoded shader sources must use a desktop legacy
// GLSL dialect (130/140/150). Native GL 3.x contexts report it directly;
// GX_GLSL130=1 forces 130 only for the GL3.3-hosted compatibility probe.
gX_I32 gX_RHI_Shader_LegacyGLSL(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //RHI_CAPS_H
