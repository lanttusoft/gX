/**********************************************************************************************
* 
*   [common/enum/shader.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
#ifndef ENUM_SHADER_H
#define ENUM_SHADER_H

#include "../types/primary.h"

//raylib
// Shader location index
#define gX_SHADERLOCATIONINDEX \
    X(VERTEX_POSITION)      /* // Shader location: vertex attribute: position */ \
    X(VERTEX_TEXCOORD01)    /* // Shader location: vertex attribute: texcoord01 */ \
    X(VERTEX_TEXCOORD02)    /* // Shader location: vertex attribute: texcoord02 */ \
    X(VERTEX_NORMAL)        /* // Shader location: vertex attribute: normal */ \
    X(VERTEX_TANGENT)       /* // Shader location: vertex attribute: tangent */ \
    X(VERTEX_COLOR)         /* // Shader location: vertex attribute: color */ \
    X(MATRIX_MVP)           /* // Shader location: matrix uniform: model-view-projection */ \
    X(MATRIX_VIEW)          /* // Shader location: matrix uniform: view (camera transform) */ \
    X(MATRIX_PROJECTION)    /* // Shader location: matrix uniform: projection */ \
    X(MATRIX_MODEL)         /* // Shader location: matrix uniform: model (transform) */ \
    X(MATRIX_NORMAL)        /* // Shader location: matrix uniform: normal */ \
    X(VECTOR_VIEW)          /* // Shader location: vector uniform: view */ \
    X(COLOR_DIFFUSE)        /* // Shader location: vector uniform: diffuse color */ \
    X(COLOR_SPECULAR)       /* // Shader location: vector uniform: specular color */ \
    X(COLOR_AMBIENT)        /* // Shader location: vector uniform: ambient color */ \
    X(MAP_ALBEDO)           /* // Shader location: sampler2d texture: albedo (same as: SHADER_LOC_MAP_DIFFUSE) */ \
    X(MAP_METALNESS)        /* // Shader location: sampler2d texture: metalness (same as: SHADER_LOC_MAP_SPECULAR) */ \
    X(MAP_NORMAL)           /* // Shader location: sampler2d texture: normal */ \
    X(MAP_ROUGHNESS)        /* // Shader location: sampler2d texture: roughness */ \
    X(MAP_OCCLUSION)        /* // Shader location: sampler2d texture: occlusion */ \
    X(MAP_EMISSION)         /* // Shader location: sampler2d texture: emission */ \
    X(MAP_HEIGHT)           /* // Shader location: sampler2d texture: height */ \
    X(MAP_CUBEMAP)          /* // Shader location: samplerCube texture: cubemap */ \
    X(MAP_IRRADIANCE)       /* // Shader location: samplerCube texture: irradiance */ \
    X(MAP_PREFILTER)        /* // Shader location: samplerCube texture: prefilter */ \
    X(MAP_BRDF)             /* // Shader location: sampler2d texture: brdf */ \
    
typedef enum {
#define X(location) gX_SHADER_LOC_##location,
    gX_SHADERLOCATIONINDEX
#undef X
} gX_ShaderLocationIndex;
const gX_I8 *gX_ShaderLocationIndex_String(gX_I32 locindex);

//raylib
// Shader uniform data type
//opencode: added gX_SHADERUNIFORMDATATYPE X macro, typedef, and string decl
#define gX_SHADERUNIFORMDATATYPE \
    X(FLOAT)      /* // Shader uniform type: float */ \
    X(VEC2)       /* // Shader uniform type: vec2 (2 float) */ \
    X(VEC3)       /* // Shader uniform type: vec3 (3 float) */ \
    X(VEC4)       /* // Shader uniform type: vec4 (4 float) */ \
    X(INT)        /* // Shader uniform type: int */ \
    X(IVEC2)      /* // Shader uniform type: ivec2 (2 int) */ \
    X(IVEC3)      /* // Shader uniform type: ivec3 (3 int) */ \
    X(IVEC4)      /* // Shader uniform type: ivec4 (4 int) */ \
    X(SAMPLER2D)  /* // Shader uniform type: sampler2d */ \

//opencode: typedef using gX_SHADERUNIFORMDATATYPE
typedef enum {
#define X(type) gX_SHADER_UNIFORM_##type,
    gX_SHADERUNIFORMDATATYPE
#undef X
} gX_ShaderUniformDataType;
//opencode: string function declaration
const gX_I8 *gX_ShaderUniformDataType_String(gX_I32 type);

//raylib
// Shader attribute data types
//opencode: added gX_SHADERATTRIBUTEDATATYPE X macro, typedef, and string decl
#define gX_SHADERATTRIBUTEDATATYPE \
    X(FLOAT) /* // Shader attribute type: float */ \
    X(VEC2)  /* // Shader attribute type: vec2 (2 float) */ \
    X(VEC3)  /* // Shader attribute type: vec3 (3 float) */ \
    X(VEC4)  /* // Shader attribute type: vec4 (4 float) */ \

//opencode: typedef using gX_SHADERATTRIBUTEDATATYPE
typedef enum {
#define X(type) gX_SHADER_ATTRIB_##type,
    gX_SHADERATTRIBUTEDATATYPE
#undef X
} gX_ShaderAttributeDataType;
//opencode: string function declaration
const gX_I8 *gX_ShaderAttributeDataType_String(gX_I32 type);

#endif //ENUM_SHADER_H
