/**********************************************************************************************
*
*   [gX: src/rhi/rhi_enums.h]
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


//opencode: RHI enum constants — GL-value-matching names so engine code
//   stops naming OpenGL directly. Values match the GL_* constants 1:1
//   (from glad.h) — passing a gX_RHI_* to a gX_RHI_* wrapper is identical
//   to passing the corresponding GL_* on the GL backends. Non-GL backends
//   (null/headless, retro) interpret the same values without GL headers.
//   Generated from the constants used across src/ (rhi, wrappers, renderer,
//   gui, scene, main). Header guard style follows the rest of src/rhi/.
#ifndef RHI_ENUMS_H
#define RHI_ENUMS_H

#define gX_RHI_ACTIVE_TEXTURE                              0x84E0   // GL_ACTIVE_TEXTURE
#define gX_RHI_ACTIVE_UNIFORMS                             0x8B86   // GL_ACTIVE_UNIFORMS
#define gX_RHI_ACTIVE_UNIFORM_BLOCKS                       0x8A36   // GL_ACTIVE_UNIFORM_BLOCKS
#define gX_RHI_ANY_SAMPLES_PASSED                          0x8C2F   // GL_ANY_SAMPLES_PASSED
#define gX_RHI_ARRAY_BUFFER                                0x8892   // GL_ARRAY_BUFFER
//ai: binding query used to restore state after cached label-quad UV updates.
#define gX_RHI_ARRAY_BUFFER_BINDING                        0x8894   // GL_ARRAY_BUFFER_BINDING
#define gX_RHI_BACK                                        0x0405   // GL_BACK
#define gX_RHI_BLEND                                       0x0BE2   // GL_BLEND
#define gX_RHI_BLEND_DST_ALPHA                             0x80CA   // GL_BLEND_DST_ALPHA
#define gX_RHI_BLEND_DST_RGB                               0x80C8   // GL_BLEND_DST_RGB
#define gX_RHI_BLEND_EQUATION_ALPHA                        0x883D   // GL_BLEND_EQUATION_ALPHA
#define gX_RHI_BLEND_EQUATION_RGB                          0x8009   // GL_BLEND_EQUATION_RGB
#define gX_RHI_BLEND_SRC_ALPHA                             0x80CB   // GL_BLEND_SRC_ALPHA
#define gX_RHI_BLEND_SRC_RGB                               0x80C9   // GL_BLEND_SRC_RGB
#define gX_RHI_BUFFER                                      0x82E0   // GL_BUFFER
#define gX_RHI_CLAMP_TO_BORDER                             0x812D   // GL_CLAMP_TO_BORDER
#define gX_RHI_CLAMP_TO_EDGE                               0x812F   // GL_CLAMP_TO_EDGE
#define gX_RHI_COLOR_ATTACHMENT0                           0x8CE0   // GL_COLOR_ATTACHMENT0
#define gX_RHI_COLOR_BUFFER_BIT                            0x00004000   // GL_COLOR_BUFFER_BIT
#define gX_RHI_COLOR_WRITEMASK                             0x0C23   // GL_COLOR_WRITEMASK
//opencode: queryable stencil write mask used by complete clear coverage.
#define gX_RHI_STENCIL_WRITEMASK                           0x0B98   // GL_STENCIL_WRITEMASK
#define gX_RHI_COMPILE_STATUS                              0x8B81   // GL_COMPILE_STATUS
#define gX_RHI_COMPUTE_SHADER                              0x91B9   // GL_COMPUTE_SHADER
#define gX_RHI_CONTEXT_COMPATIBILITY_PROFILE_BIT           0x00000002   // GL_CONTEXT_COMPATIBILITY_PROFILE_BIT
#define gX_RHI_CONTEXT_CORE_PROFILE_BIT                    0x00000001   // GL_CONTEXT_CORE_PROFILE_BIT
#define gX_RHI_CONTEXT_PROFILE_MASK                        0x9126   // GL_CONTEXT_PROFILE_MASK
 #define gX_RHI_CULL_FACE                                   0x0B44   // GL_CULL_FACE
 #define gX_RHI_CULL_FACE_MODE                              0x0B45   // GL_CULL_FACE_MODE
 #define gX_RHI_CURRENT_PROGRAM                             0x8B8D   // GL_CURRENT_PROGRAM
 #define gX_RHI_CW                                          0x0900   // GL_CW
 #define gX_RHI_CCW                                         0x0901   // GL_CCW
 #define gX_RHI_DEBUG_OUTPUT                                0x92E0   // GL_DEBUG_OUTPUT
#define gX_RHI_DEBUG_OUTPUT_SYNCHRONOUS                    0x8242   // GL_DEBUG_OUTPUT_SYNCHRONOUS
#define gX_RHI_DEBUG_SEVERITY_HIGH                         0x9146   // GL_DEBUG_SEVERITY_HIGH
#define gX_RHI_DEBUG_SEVERITY_LOW                          0x9148   // GL_DEBUG_SEVERITY_LOW
#define gX_RHI_DEBUG_SEVERITY_MEDIUM                       0x9147   // GL_DEBUG_SEVERITY_MEDIUM
#define gX_RHI_DEBUG_SEVERITY_NOTIFICATION                 0x826B   // GL_DEBUG_SEVERITY_NOTIFICATION
#define gX_RHI_DEBUG_SOURCE_APPLICATION                    0x824A   // GL_DEBUG_SOURCE_APPLICATION
#define gX_RHI_DEBUG_TYPE_ERROR                            0x824C   // GL_DEBUG_TYPE_ERROR
#define gX_RHI_DEBUG_TYPE_UNDEFINED_BEHAVIOR               0x824E   // GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
#define gX_RHI_DEPTH_ATTACHMENT                            0x8D00   // GL_DEPTH_ATTACHMENT
#define gX_RHI_DEPTH_BUFFER_BIT                            0x00000100   // GL_DEPTH_BUFFER_BIT
#define gX_RHI_DEPTH_COMPONENT                             0x1902   // GL_DEPTH_COMPONENT
#define gX_RHI_DEPTH_COMPONENT24                           0x81A6   // GL_DEPTH_COMPONENT24
#define gX_RHI_DEPTH_FUNC                                  0x0B74   // GL_DEPTH_FUNC
#define gX_RHI_DEPTH_TEST                                  0x0B71   // GL_DEPTH_TEST
#define gX_RHI_DEPTH_WRITEMASK                             0x0B72   // GL_DEPTH_WRITEMASK
#define gX_RHI_DONT_CARE                                   0x1100   // GL_DONT_CARE
#define gX_RHI_DRAW_BUFFER0                                0x8825   // GL_DRAW_BUFFER0
#define gX_RHI_DRAW_BUFFER1                                0x8826   // GL_DRAW_BUFFER1
#define gX_RHI_DRAW_FRAMEBUFFER                            0x8CA9   // GL_DRAW_FRAMEBUFFER
#define gX_RHI_DRAW_FRAMEBUFFER_BINDING                    0x8CA6   // GL_DRAW_FRAMEBUFFER_BINDING
#define gX_RHI_DST_COLOR                                   0x0306   // GL_DST_COLOR
#define gX_RHI_DYNAMIC_DRAW                                0x88E8   // GL_DYNAMIC_DRAW
#define gX_RHI_ELEMENT_ARRAY_BUFFER                        0x8893   // GL_ELEMENT_ARRAY_BUFFER
#define gX_RHI_EXTENSIONS                                  0x1F03   // GL_EXTENSIONS
#define gX_RHI_FALSE                                       0   // GL_FALSE
#define gX_RHI_FILL                                        0x1B02   // GL_FILL
#define gX_RHI_FLOAT                                       0x1406   // GL_FLOAT
#define gX_RHI_FLOAT_MAT4                                  0x8B5C   // GL_FLOAT_MAT4
#define gX_RHI_FLOAT_VEC2                                  0x8B50   // GL_FLOAT_VEC2
#define gX_RHI_FLOAT_VEC3                                  0x8B51   // GL_FLOAT_VEC3
#define gX_RHI_FLOAT_VEC4                                  0x8B52   // GL_FLOAT_VEC4
#define gX_RHI_FRAGMENT_SHADER                             0x8B30   // GL_FRAGMENT_SHADER
#define gX_RHI_FRAMEBUFFER                                 0x8D40   // GL_FRAMEBUFFER
#define gX_RHI_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME          0x8CD1   // GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
#define gX_RHI_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE          0x8CD0   // GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
#define gX_RHI_FRAMEBUFFER_COMPLETE                        0x8CD5   // GL_FRAMEBUFFER_COMPLETE
#define gX_RHI_FRAMEBUFFER_INCOMPLETE_ATTACHMENT           0x8CD6   // GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define gX_RHI_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT   0x8CD7   // GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define gX_RHI_FRAMEBUFFER_SRGB                            0x8DB9   // GL_FRAMEBUFFER_SRGB
#define gX_RHI_FRONT                                       0x0404   // GL_FRONT
#define gX_RHI_FRONT_AND_BACK                              0x0408   // GL_FRONT_AND_BACK
#define gX_RHI_FRONT_FACE                                  0x0B46   // GL_FRONT_FACE
#define gX_RHI_FUNC_ADD                                    0x8006   // GL_FUNC_ADD
#define gX_RHI_FUNC_SUBTRACT                               0x800A   // GL_FUNC_SUBTRACT
#define gX_RHI_GEOMETRY_SHADER                             0x8DD9   // GL_GEOMETRY_SHADER
#define gX_RHI_HALF_FLOAT                                  0x140B   // GL_HALF_FLOAT
#define gX_RHI_INT                                         0x1404   // GL_INT
#define gX_RHI_INVALID_ENUM                                0x0500   // GL_INVALID_ENUM
#define gX_RHI_INVALID_FRAMEBUFFER_OPERATION               0x0506   // GL_INVALID_FRAMEBUFFER_OPERATION
#define gX_RHI_INVALID_INDEX                               0xFFFFFFFF   // GL_INVALID_INDEX
#define gX_RHI_INVALID_OPERATION                           0x0502   // GL_INVALID_OPERATION
#define gX_RHI_LEQUAL                                      0x0203   // GL_LEQUAL
#define gX_RHI_LESS                                        0x0201   // GL_LESS
#define gX_RHI_LINE                                        0x1B01   // GL_LINE
#define gX_RHI_LINEAR                                      0x2601   // GL_LINEAR
#define gX_RHI_LINEAR_MIPMAP_LINEAR                        0x2703   // GL_LINEAR_MIPMAP_LINEAR
#define gX_RHI_LINES                                       0x0001   // GL_LINES
#define gX_RHI_LINK_STATUS                                 0x8B82   // GL_LINK_STATUS
#define gX_RHI_MAX_COMBINED_TEXTURE_IMAGE_UNITS            0x8B4D   // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define gX_RHI_MAX_FRAGMENT_IMAGE_UNIFORMS                 0x90CE   // GL_MAX_FRAGMENT_IMAGE_UNIFORMS
#define gX_RHI_MAX_TEXTURE_IMAGE_UNITS                     0x8872   // GL_MAX_TEXTURE_IMAGE_UNITS
#define gX_RHI_NEAREST                                     0x2600   // GL_NEAREST
#define gX_RHI_NEAREST_MIPMAP_NEAREST                      0x2700   // GL_NEAREST_MIPMAP_NEAREST
#define gX_RHI_NONE                                        0   // GL_NONE
#define gX_RHI_NO_ERROR                                    0   // GL_NO_ERROR
#define gX_RHI_NUM_EXTENSIONS                              0x821D   // GL_NUM_EXTENSIONS
#define gX_RHI_ONE                                         1   // GL_ONE
#define gX_RHI_ZERO                                        0   // GL_ZERO
#define gX_RHI_ONE_MINUS_SRC_ALPHA                         0x0303   // GL_ONE_MINUS_SRC_ALPHA
#define gX_RHI_POINT                                       0x1B00   // GL_POINT
#define gX_RHI_POINTS                                      0x0000   // GL_POINTS
#define gX_RHI_POLYGON_MODE                                0x0B40   // GL_POLYGON_MODE
#define gX_RHI_PROGRAM                                     0x82E2   // GL_PROGRAM
#define gX_RHI_PROGRAM_BINARY_LENGTH                       0x8741   // GL_PROGRAM_BINARY_LENGTH
#define gX_RHI_PROGRAM_POINT_SIZE                            0x8642   // GL_PROGRAM_POINT_SIZE
#define gX_RHI_QUERY_RESULT                                0x8866   // GL_QUERY_RESULT
#define gX_RHI_QUERY_RESULT_AVAILABLE                      0x8867   // GL_QUERY_RESULT_AVAILABLE
#define gX_RHI_R32F                                        0x822E   // GL_R32F
#define gX_RHI_READ_FRAMEBUFFER                            0x8CA8   // GL_READ_FRAMEBUFFER
#define gX_RHI_READ_FRAMEBUFFER_BINDING                    0x8CAA   // GL_READ_FRAMEBUFFER_BINDING
#define gX_RHI_RED                                         0x1903   // GL_RED
#define gX_RHI_RENDERBUFFER                                0x8D41   // GL_RENDERBUFFER
#define gX_RHI_RENDERER                                    0x1F01   // GL_RENDERER
#define gX_RHI_REPEAT                                      0x2901   // GL_REPEAT
#define gX_RHI_RG                                          0x8227   // GL_RG
#define gX_RHI_RG16F                                       0x822F   // GL_RG16F
#define gX_RHI_RGB                                         0x1907   // GL_RGB
#define gX_RHI_RGB16F                                      0x881B   // GL_RGB16F
#define gX_RHI_RGB8                                        0x8051   // GL_RGB8
#define gX_RHI_RGBA                                        0x1908   // GL_RGBA
#define gX_RHI_RGBA16F                                     0x881A   // GL_RGBA16F
#define gX_RHI_READ_WRITE                                   0x88BA   // GL_READ_WRITE
#define gX_RHI_SHADER_IMAGE_ACCESS_BARRIER_BIT              0x00000020 // GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define gX_RHI_TEXTURE_FETCH_BARRIER_BIT                    0x00000008 // GL_TEXTURE_FETCH_BARRIER_BIT
#define gX_RHI_RGBA8                                       0x8058   // GL_RGBA8
#define gX_RHI_SAMPLER_2D                                  0x8B5E   // GL_SAMPLER_2D
#define gX_RHI_SAMPLER_CUBE                                0x8B60   // GL_SAMPLER_CUBE
#define gX_RHI_SAMPLES_PASSED                              0x8914   // GL_SAMPLES_PASSED
#define gX_RHI_SAMPLE_ALPHA_TO_COVERAGE                    0x809E   // GL_SAMPLE_ALPHA_TO_COVERAGE
#define gX_RHI_SCISSOR_BOX                                 0x0C10   // GL_SCISSOR_BOX
#define gX_RHI_SCISSOR_TEST                                0x0C11   // GL_SCISSOR_TEST
#define gX_RHI_SHADER_STORAGE_BARRIER_BIT                  0x00002000   // GL_SHADER_STORAGE_BARRIER_BIT
#define gX_RHI_SHADER_STORAGE_BUFFER                       0x90D2   // GL_SHADER_STORAGE_BUFFER
#define gX_RHI_SHADING_LANGUAGE_VERSION                    0x8B8C   // GL_SHADING_LANGUAGE_VERSION
#define gX_RHI_SRC_ALPHA                                   0x0302   // GL_SRC_ALPHA
#define gX_RHI_STACK_OVERFLOW                              0x0503   // GL_STACK_OVERFLOW
#define gX_RHI_STATIC_DRAW                                 0x88E4   // GL_STATIC_DRAW
#define gX_RHI_STENCIL_BUFFER_BIT                          0x00000400   // GL_STENCIL_BUFFER_BIT
#define gX_RHI_STENCIL_INDEX8                              0x8D48   // GL_STENCIL_INDEX8
#define gX_RHI_STENCIL_TEST                                0x0B90   // GL_STENCIL_TEST
#define gX_RHI_STREAM_DRAW                                 0x88E0   // GL_STREAM_DRAW
#define gX_RHI_TEXTURE                                     0x1702   // GL_TEXTURE
#define gX_RHI_TEXTURE0                                    0x84C0   // GL_TEXTURE0
#define gX_RHI_TEXTURE15                                   0x84CF   // GL_TEXTURE15
#define gX_RHI_TEXTURE16                                   0x84D0   // GL_TEXTURE16
#define gX_RHI_TEXTURE18                                   0x84D2   // GL_TEXTURE18
#define gX_RHI_TEXTURE2                                    0x84C2   // GL_TEXTURE2
#define gX_RHI_TEXTURE_2D                                  0x0DE1   // GL_TEXTURE_2D
#define gX_RHI_TEXTURE_2D_ARRAY                            0x8C1A   // GL_TEXTURE_2D_ARRAY
#define gX_RHI_TEXTURE_2D_MULTISAMPLE                      0x9100   // GL_TEXTURE_2D_MULTISAMPLE
#define gX_RHI_TEXTURE_BASE_LEVEL                          0x813C   // GL_TEXTURE_BASE_LEVEL
#define gX_RHI_TEXTURE_BINDING_2D                          0x8069   // GL_TEXTURE_BINDING_2D
#define gX_RHI_TEXTURE_BORDER_COLOR                        0x1004   // GL_TEXTURE_BORDER_COLOR
#define gX_RHI_TEXTURE_COMPARE_MODE                        0x884C   // GL_TEXTURE_COMPARE_MODE
#define gX_RHI_TEXTURE_CUBE_MAP                            0x8513   // GL_TEXTURE_CUBE_MAP
#define gX_RHI_TEXTURE_CUBE_MAP_ARRAY                      0x9009   // GL_TEXTURE_CUBE_MAP_ARRAY
//opencode: D14 — cube face targets (0x8515..0x851A)
#define gX_RHI_TEXTURE_CUBE_MAP_POSITIVE_X                 0x8515   // GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define gX_RHI_TEXTURE_CUBE_MAP_NEGATIVE_X                 0x8516   // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
#define gX_RHI_TEXTURE_CUBE_MAP_POSITIVE_Y                 0x8517   // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
#define gX_RHI_TEXTURE_CUBE_MAP_NEGATIVE_Y                 0x8518   // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
#define gX_RHI_TEXTURE_CUBE_MAP_POSITIVE_Z                 0x8519   // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
#define gX_RHI_TEXTURE_CUBE_MAP_NEGATIVE_Z                 0x851A   // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define gX_RHI_TEXTURE_CUBE_MAP_SEAMLESS                   0x884F   // GL_TEXTURE_CUBE_MAP_SEAMLESS
#define gX_RHI_TEXTURE_HEIGHT                              0x1001   // GL_TEXTURE_HEIGHT
#define gX_RHI_TEXTURE_INTERNAL_FORMAT                     0x1003   // GL_TEXTURE_INTERNAL_FORMAT
#define gX_RHI_TEXTURE_LOD_BIAS                            0x8501   // GL_TEXTURE_LOD_BIAS
#define gX_RHI_TEXTURE_MAG_FILTER                          0x2800   // GL_TEXTURE_MAG_FILTER
#define gX_RHI_TEXTURE_MAX_LEVEL                           0x813D   // GL_TEXTURE_MAX_LEVEL
#define gX_RHI_TEXTURE_MIN_FILTER                          0x2801   // GL_TEXTURE_MIN_FILTER
#define gX_RHI_TEXTURE_WIDTH                               0x1000   // GL_TEXTURE_WIDTH
#define gX_RHI_TEXTURE_WRAP_R                              0x8072   // GL_TEXTURE_WRAP_R
#define gX_RHI_TEXTURE_WRAP_S                              0x2802   // GL_TEXTURE_WRAP_S
#define gX_RHI_TEXTURE_WRAP_T                              0x2803   // GL_TEXTURE_WRAP_T
#define gX_RHI_TIMESTAMP                                   0x8E28   // GL_TIMESTAMP
#define gX_RHI_TIME_ELAPSED                                0x88BF   // GL_TIME_ELAPSED
#define gX_RHI_TRIANGLES                                   0x0004   // GL_TRIANGLES
#define gX_RHI_TRIANGLE_FAN                                0x0006   // GL_TRIANGLE_FAN
#define gX_RHI_TRUE                                        1   // GL_TRUE
#define gX_RHI_UNIFORM_BUFFER                              0x8A11   // GL_UNIFORM_BUFFER
#define gX_RHI_UNPACK_ALIGNMENT                            0x0CF5   // GL_UNPACK_ALIGNMENT
#define gX_RHI_UNSIGNED_BYTE                               0x1401   // GL_UNSIGNED_BYTE
#define gX_RHI_UNSIGNED_INT                                0x1405   // GL_UNSIGNED_INT
#define gX_RHI_UNSIGNED_SHORT                              0x1403   // GL_UNSIGNED_SHORT
#define gX_RHI_VENDOR                                      0x1F00   // GL_VENDOR
#define gX_RHI_VERSION                                     0x1F02   // GL_VERSION
#define gX_RHI_VERSION_4_3                                 1   // GL_VERSION_4_3
#define gX_RHI_VERTEX_ARRAY                                0x8074   // GL_VERTEX_ARRAY
#define gX_RHI_VERTEX_ARRAY_BINDING                        0x85B5   // GL_VERTEX_ARRAY_BINDING
#define gX_RHI_VERTEX_ATTRIB_ARRAY_BARRIER_BIT             0x00000001   // GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define gX_RHI_VERTEX_SHADER                               0x8B31   // GL_VERTEX_SHADER
#define gX_RHI_VIEWPORT                                    0x0BA2   // GL_VIEWPORT

//opencode: engine texture-unit slots — mirrors the authoritative table in
//   src/renderer/gfx/constants.h (gX_GFX_TEXUNIT_*). ALL units must be ≤ 31
//   (GL_MAX_TEXTURE_IMAGE_UNITS=32); compile-time asserts live in rhi_tests.c.
//   Shadow maps bind unit 8 (was 7 — collided with material map slot 7);
//   CSM cascades at 10, cookies at 11 (were 52/64 — invalid, silently
//   dropped). Material maps own 0-11; GFX main-shader units use 12-19.
#define gX_RHI_TEX_SLOT_BLOOM_BLUR                          6   // bloom blur buffer (bloomTex in composite)
#define gX_RHI_TEX_SLOT_SCENE                               2   // primary scene color
#define gX_RHI_TEX_SLOT_SHADOW                              8   // sun shadow map, CSM cascade 0, depth vis
#define gX_RHI_TEX_SLOT_DEPTH                               3   // depth for SSR/SSAO/DOF/motionblur/TAA
#define gX_RHI_TEX_SLOT_COLOR                               4   // color for SSR/motionblur/DOF blur/TAA
#define gX_RHI_TEX_SLOT_AUX                                 5   // aux: bgTex, SSAO blur input, DOF blur CoC
#define gX_RHI_TEX_SLOT_SSAO                                12  // ssaoMap for phong_ambient in main shader
#define gX_RHI_TEX_SLOT_GLOSS                               13  // gloss map
#define gX_RHI_TEX_SLOT_REFRACTION                          14  // bgTex for refractive materials
#define gX_RHI_TEX_SLOT_DETAIL                              15  // detail/dirt texture
#define gX_RHI_TEX_SLOT_IBL_IRRADIANCE                      16  // irradiance cubemap
#define gX_RHI_TEX_SLOT_IBL_PREFILTER                       17  // prefilter cubemap
#define gX_RHI_TEX_SLOT_IBL_BRDF                            18  // BRDF integration LUT
#define gX_RHI_TEX_SLOT_REFLECTION                          19  // planar reflection reflTex
#define gX_RHI_TEX_SLOT_POINT_SHADOW                        20  // shared point-light shadow 2D array (single bind, layers = light*6+face)
#define gX_RHI_TEX_SLOT_COOKIE                              11  // light cookie textures
#define gX_RHI_TEX_SLOT_HIZ                                22   // SSR min-reduced Hi-Z depth chain

//opencode: rlgl enum-ID mirrors — these are NOT GL constants. They mirror the
//   rlgl rlBlendMode/rlShaderLocationIndex/rlPixelFormat enum VALUES so the
//   RHI/backend surface no longer needs raylib's rlgl.h header. Indexed array
//   consumers (Shader.locs[]) depend on the exact numbers. D15 removed the
//   rlgl.h include from the GLFW build; the values are copied from rlgl.h.

// Blend modes (rlBlendMode order, rlgl.h) — gX_RHI_Blend_Mode switch input
#define gX_RHI_BLEND_ALPHA                                  0   // RL_BLEND_ALPHA
#define gX_RHI_BLEND_ADDITIVE                               1   // RL_BLEND_ADDITIVE
#define gX_RHI_BLEND_MULTIPLIED                             2   // RL_BLEND_MULTIPLIED
#define gX_RHI_BLEND_ADD_COLORS                             3   // RL_BLEND_ADD_COLORS
#define gX_RHI_BLEND_SUBTRACT_COLORS                        4   // RL_BLEND_SUBTRACT_COLORS
#define gX_RHI_BLEND_ALPHA_PREMULTIPLY                      5   // RL_BLEND_ALPHA_PREMULTIPLY
#define gX_RHI_BLEND_CUSTOM                                 6   // RL_BLEND_CUSTOM
#define gX_RHI_BLEND_CUSTOM_SEPARATE                        7   // RL_BLEND_CUSTOM_SEPARATE

// Shader location indices (rlShaderLocationIndex order, rlgl.h) — locs[] array indices
#define gX_RHI_SHADER_LOC_VERTEX_POSITION                   0   // vertex attribute: position
#define gX_RHI_SHADER_LOC_VERTEX_TEXCOORD01                 1   // vertex attribute: texcoord01
#define gX_RHI_SHADER_LOC_VERTEX_TEXCOORD02                 2   // vertex attribute: texcoord02
#define gX_RHI_SHADER_LOC_VERTEX_NORMAL                     3   // vertex attribute: normal
#define gX_RHI_SHADER_LOC_VERTEX_TANGENT                    4   // vertex attribute: tangent
#define gX_RHI_SHADER_LOC_VERTEX_COLOR                      5   // vertex attribute: color
#define gX_RHI_SHADER_LOC_MATRIX_MVP                        6   // uniform: model-view-projection
#define gX_RHI_SHADER_LOC_MATRIX_VIEW                       7   // uniform: view
#define gX_RHI_SHADER_LOC_MATRIX_PROJECTION                 8   // uniform: projection
#define gX_RHI_SHADER_LOC_MATRIX_MODEL                      9   // uniform: model
#define gX_RHI_SHADER_LOC_MATRIX_NORMAL                     10  // uniform: normal
#define gX_RHI_SHADER_LOC_VECTOR_VIEW                       11  // uniform: view vector
#define gX_RHI_SHADER_LOC_COLOR_DIFFUSE                     12  // uniform: diffuse color
#define gX_RHI_SHADER_LOC_COLOR_SPECULAR                    13  // uniform: specular color
#define gX_RHI_SHADER_LOC_COLOR_AMBIENT                     14  // uniform: ambient color
#define gX_RHI_SHADER_LOC_MAP_ALBEDO                        15  // sampler: albedo (== MAP_DIFFUSE)
#define gX_RHI_SHADER_LOC_MAP_METALNESS                     16  // sampler: metalness (== MAP_SPECULAR)
#define gX_RHI_SHADER_LOC_MAP_NORMAL                        17  // sampler: normal
#define gX_RHI_SHADER_LOC_MAP_ROUGHNESS                     18  // sampler: roughness
#define gX_RHI_SHADER_LOC_MAP_OCCLUSION                     19  // sampler: occlusion
#define gX_RHI_SHADER_LOC_MAP_EMISSION                      20  // sampler: emission
#define gX_RHI_SHADER_LOC_MAP_HEIGHT                        21  // sampler: height
#define gX_RHI_SHADER_LOC_MAP_CUBEMAP                       22  // sampler: cubemap
#define gX_RHI_SHADER_LOC_MAP_IRRADIANCE                    23  // sampler: irradiance
#define gX_RHI_SHADER_LOC_MAP_PREFILTER                     24  // sampler: prefilter
#define gX_RHI_SHADER_LOC_MAP_BRDF                          25  // sampler: brdf

// Shader location aliases (rlgl.h #defines) — same values as the above
#define gX_RHI_SHADER_LOC_MAP_DIFFUSE                       gX_RHI_SHADER_LOC_MAP_ALBEDO
#define gX_RHI_SHADER_LOC_MAP_SPECULAR                      gX_RHI_SHADER_LOC_MAP_METALNESS

// Pixel formats (rlPixelFormat order, rlgl.h) — only the ones gameX uses
#define gX_RHI_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8            7   // RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8

#endif //RHI_ENUMS_H
