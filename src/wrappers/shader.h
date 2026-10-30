/**********************************************************************************************
* 
*   [wrappers/shader.h]
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
//lanttu: original wrapper for raylib shader functions
#ifndef WRAPPERS_SHADER_H
#define WRAPPERS_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//opencode: Shader Feature Level System — flags, types, level API
// Each backend caps at a max level; the shader compiler injects #define GX_FEATURE_*.

#define GX_SHADER_FEATURE_TEXTURE       (1u << 0)   // L1
#define GX_SHADER_FEATURE_LIGHTING      (1u << 1)   // L2
#define GX_SHADER_FEATURE_NORMAL_MAP    (1u << 2)   // L2
#define GX_SHADER_FEATURE_SPECULAR      (1u << 3)   // L2
#define GX_SHADER_FEATURE_SHADOW        (1u << 4)   // L3
#define GX_SHADER_FEATURE_PARALLAX      (1u << 5)   // L3
#define GX_SHADER_FEATURE_FOG           (1u << 6)   // L3
#define GX_SHADER_FEATURE_SKINNING      (1u << 7)   // L3
#define GX_SHADER_FEATURE_INSTANCING    (1u << 8)   // L3
#define GX_SHADER_FEATURE_DISPLACEMENT  (1u << 9)   // L4
#define GX_SHADER_FEATURE_CURVATURE     (1u << 10)  // L4
#define GX_SHADER_FEATURE_WIND          (1u << 11)  // L4
#define GX_SHADER_FEATURE_PBR           (1u << 12)  // L4
#define GX_SHADER_FEATURE_HDR           (1u << 13)  // L4
#define GX_SHADER_FEATURE_IBL           (1u << 14)  // L4
#define GX_SHADER_FEATURE_MORPH         (1u << 15)  // L5
#define GX_SHADER_FEATURE_LOD_CROSSFADE (1u << 16)  // L5
#define GX_SHADER_FEATURE_BLOOM         (1u << 17)  // L5
#define GX_SHADER_FEATURE_IBL_PREFILTER (1u << 18)  // L5
#define GX_SHADER_FEATURE_SSR           (1u << 19)  // L5
#define GX_SHADER_FEATURE_SSAO          (1u << 20)  // L5
#define GX_SHADER_FEATURE_GODRAYS       (1u << 21)  // L5

#define GX_SHADER_LEVEL_MAX 5
#define GX_SHADER_FEATURE_COUNT 22

typedef enum {
    GX_SHADER_TYPE_STANDARD,
    GX_SHADER_TYPE_DEPTH,
    GX_SHADER_TYPE_DEPTH_VISUALIZER,
    GX_SHADER_TYPE_SKY,
    GX_SHADER_TYPE_WATER,
    GX_SHADER_TYPE_MAP,
    GX_SHADER_TYPE_WIREFRAME,
    GX_SHADER_TYPE_PARTICLE,
    GX_SHADER_TYPE_BILLBOARD,
    GX_SHADER_TYPE_POST_BLUR,
    GX_SHADER_TYPE_POST_TONEMAP,
    GX_SHADER_TYPE_POST_BLOOM,
    GX_SHADER_TYPE_POST_SSAO,
    GX_SHADER_TYPE_POST_SSR,
    GX_SHADER_TYPE_POST_COMPOSITE,
    GX_SHADER_TYPE_COUNT
} gX_ShaderType;

//opencode: backend shader capabilities — max feature level per type
typedef struct {
    gX_U32 maxLevel[GX_SHADER_TYPE_COUNT];
    gX_I1  supportsCompute;
    gX_I1  supportsTessellation;
} gX_BackendShaderCaps;

//opencode: resolve level → feature bitmask
gX_U32 gX_Shader_FlagsFromLevel(gX_F32 level, gX_U32 extraFlags);

//opencode: convert feature bitmask to "#define GX_FEATURE_X\n" string (caller must free)
gX_I8 *gX_Shader_FlagsToDefines(gX_U32 flags);

//opencode: compile a shader type at a given feature level
//   level=3.0 → Phong+ with shadows/fog. extraFlags adds individual bits.
gX_Shader gX_Shader_Compile_Level(gX_ShaderType type, gX_F32 level, gX_U32 extraFlags, const gX_I8 *customDefines);

//opencode: query backend caps
const gX_BackendShaderCaps *gX_Shader_GetBackendCaps(gX_I0);

//opencode: debug override — force all level-capable shaders to this level, -1 disables
gX_I0  gX_Shader_ForceLevel(gX_I32 level);
gX_I32 gX_Shader_GetForcedLevel(gX_I0);

//opencode: Shader_Wrapper — deferred-loading shader with path + defines
typedef struct gX_Shader_Wrapper {
    gX_LoadingState loadingState;
    
    gX_I1 useDefaultVert;
    gX_I1 useDefaultFrag;
    gX_TextLine pathVert;
    gX_TextLine pathFrag;
    gX_I32      version;
    gX_I8       defines[2048];
    
    gX_Shader   shader;
    gX_Shader_Locations locs;
} gX_Shader_Wrapper;

gX_I0 gX_Shader_Wrapper_Load(gX_Shader_Wrapper *sw);
gX_I0 gX_Shader_Wrapper_Unload(gX_Shader_Wrapper *sw);
gX_I0 gX_Shader_Wrapper_Update(gX_Shader_Wrapper *sw);
    
gX_Shader gX_Shader_Load(   const gX_I8 *vertpath, const gX_I8 *fragpath);
gX_Shader gX_Shader_Load_Ex(const gX_I8 *vertpath, const gX_I8 *fragpath, const gX_I8 *defines); //opencode: preprocesses via shader compiler
gX_Shader gX_Shader_Load_Geo(const gX_I8 *vertpath, const gX_I8 *geopath, const gX_I8 *fragpath);
gX_Shader gX_Shader_Load_Prototype(gX_Shader_Prototype proto);
gX_I0     gX_Shader_Unload( gX_Shader shader);
gX_I32    gX_Shader_GetLocation( gX_Shader shader, const gX_I8 *locName);
gX_I32    gX_Shader_GetLocationAttrib(gX_Shader shader, const gX_I8 *attribName); //opencode: backend-agnostic attribute location query
gX_I0     gX_Shader_SetValue(         gX_Shader shader, gX_I32 location, gX_I0 *ptr, gX_I32 format);
gX_I0     gX_Shader_SetValue_Array(   gX_Shader shader, gX_I32 location, gX_I0 *ptr, gX_I32 format, gX_U32 amount);
gX_I0     gX_Shader_SetValue_Texture( gX_Shader shader, gX_I32 location, gX_Texture texture);
gX_I0     gX_Shader_SetValue_TextureSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot);
gX_I0     gX_Shader_SetValue_CubemapSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot);
gX_I0     gX_Shader_SetValue_Cubemap( gX_Shader shader, gX_I32 location, gX_Texture cubemap);
gX_I0     gX_Shader_SetValue_Matrix(  gX_Shader shader, gX_I32 location, gX_Matrix matrix);    
    
#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_SHADER_H
