/**********************************************************************************************
*
*   [gX: src/renderer/gfx/gl_level.h]
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


#ifndef GFX_GL_LEVEL_H
#define GFX_GL_LEVEL_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: GL version levels — educational tool to see what breaks at each level.
//   Like selecting DirectX 8/9/10/11 in games. Default = detected max, but the
//   user can lower it in ImGui to test forward compatibility / see what breaks.
//   500+ is reserved for Vulkan (spiritual) — not a real GL version.
#define GX_GL_LEVEL_200  200   // OpenGL 2.0 / GLSL 120
#define GX_GL_LEVEL_300  300   // OpenGL 3.0 / GLSL 130
#define GX_GL_LEVEL_310  310   // OpenGL 3.1 / GLSL 140
#define GX_GL_LEVEL_320  320   // OpenGL 3.2 / GLSL 150
#define GX_GL_LEVEL_330  330   // Base: OpenGL 3.3 / GLSL 330
#define GX_GL_LEVEL_400  400   // Tessellation shaders, OIT (ARB)
#define GX_GL_LEVEL_410  410   // Shader subroutines, improved robustness
#define GX_GL_LEVEL_420  420   // glFragDepth layout qualifiers, bindless (ARB)
#define GX_GL_LEVEL_430  430   // Compute shaders, SSBO, multi-view
#define GX_GL_LEVEL_500  500   // Vulkan (spiritual) — reserved for future

//opencode: feature flags derived from the USER-SELECTED GL level.
//   Mirrors the #define GX_GL_VERSION N injected into shaders.
//   The user picks a level (<= detected max) and the engine only uses
//   features from that level and below. This is the educational part.
typedef struct gX_GL_Features {
    gX_I32 level;               // detected max hardware level (read-only)
    gX_I32 useLevel;            // user-selectable level (default = level, can be lowered)
    gX_I1 hasTessellation;      // GL 4.0+: GL_ARB_tessellation_shader
    gX_I1 hasCompute;           // GL 4.3+ / ES 3.1+: GL_ARB_compute_shader
    gX_I1 hasBindless;          // GL 4.2+: GL_ARB_bindless_texture
    gX_I1 hasShaderSubroutine;  // GL 4.0+: GL_ARB_shader_subroutine
    gX_I1 hasSSBO;              // GL 4.3+ / ES 3.1+: GL_ARB_shader_storage_buffer_object
    gX_I1 hasMultiDrawIndirect; // GL 4.2+ / ES 3.1+: GL_ARB_multi_draw_indirect
    gX_I1 hasProgramBinary;     // GL 4.1+ / ES 3.0+: GL_ARB_get_program_binary
    gX_I1 hasCubeMapArray;      // GL 4.0+: GL_ARB_texture_cube_map_array
    gX_I1 hasGeometryShaders;   // GL 3.2+ / ES 3.2+ — .geom programs (never in ES 3.0/3.1)
    gX_I1 hasMSTexture;         // GL 3.2+ / ES 3.1+ — multisampled texture attachments
    gX_I1 hasInstancing;        // GL 3.1+ / ES 3.0+ — hardware model instancing
} gX_GL_Features;

//opencode: global GL capabilities — set once at init
extern gX_GL_Features gX_GL;

//opencode: detect GL version and populate feature flags. Called after backend init.
gX_I0 gX_GL_Detect(gX_I0);

//opencode: set user GL level (for educational experimentation).
//   Clamped to [GX_GL_LEVEL_200, gX_GL.level]. Returns the new useLevel.
//   Updates all has* flags to match the new level. Set gX_GL_ShaderDefines()
//   to refresh defines, then reload shaders for the change to take effect.
gX_I32 gX_GL_SetLevel(gX_I32 newLevel);

//opencode: get the GLSL #version directive string for the current level.
//   Returns e.g. "330", "400", "430".
char *gX_GL_VersionString(gX_I0);

//opencode: get a #define string for shader injection. Injects:
//   #define GX_GL_VERSION 330
//   #define GX_GL_HAS_TESSELLATION 0
//   etc.
//   Returns a static buffer — valid until next call.
char *gX_GL_ShaderDefines(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GFX_GL_LEVEL_H
