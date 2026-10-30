/**********************************************************************************************
* 
*   [wrappers/wrappers_gl.h]
*   Uses borrowed code from: raylib v5.x, rlgl v4.5
*   All instances are marked with respective labels
*
 *   Copyright (c) 2025-2026 lanttu, opencode
*
*   [raylib -> rlgl]
*   rlgl v4.5 - A multi-OpenGL abstraction layer with an immediate-mode style API
*
*   DESCRIPTION:
*       An abstraction layer for multiple OpenGL versions (1.1, 2.1, 3.3 Core, 4.3 Core, ES 2.0)
*       that provides a pseudo-OpenGL 1.1 immediate-mode style API (rlVertex, rlTranslate, rlRotate...)
*
*   ADDITIONAL NOTES:
*       When choosing an OpenGL backend different than OpenGL 1.1, some internal buffer are
*       initialized on rlglInit() to accumulate vertex data.
*
*       When an internal state change is required all the stored vertex data is renderer in batch,
*       additionally, rlDrawRenderBatchActive() could be called to force flushing of the batch.
*
*       Some resources are also loaded for convenience, here the complete list:
*          - Default batch (RLGL.defaultBatch): RenderBatch system to accumulate vertex data
*          - Default texture (RLGL.defaultTextureId): 1x1 white pixel R8G8B8A8
*          - Default shader (RLGL.State.defaultShaderId, RLGL.State.defaultShaderLocs)
*
*       Internal buffer (and resources) must be manually unloaded calling rlglClose().
*
*   CONFIGURATION:
*       #define GRAPHICS_API_OPENGL_11
*       #define GRAPHICS_API_OPENGL_21
*       #define GRAPHICS_API_OPENGL_33
*       #define GRAPHICS_API_OPENGL_43
*       #define GRAPHICS_API_OPENGL_ES2
*       #define GRAPHICS_API_OPENGL_ES3
*           Use selected OpenGL graphics backend, should be supported by platform
*           Those preprocessor defines are only used on rlgl module, if OpenGL version is
*           required by any other module, use rlGetVersion() to check it
*
*       #define RLGL_IMPLEMENTATION
*           Generates the implementation of the library into the included file.
*           If not defined, the library is in header only mode and can be included in other headers
*           or source files without problems. But only ONE file should hold the implementation.
*
*       #define RLGL_RENDER_TEXTURES_HINT
*           Enable framebuffer objects (fbo) support (enabled by default)
*           Some GPUs could not support them despite the OpenGL version
*
*       #define RLGL_SHOW_GL_DETAILS_INFO
*           Show OpenGL extensions and capabilities detailed logs on init
*
*       #define RLGL_ENABLE_OPENGL_DEBUG_CONTEXT
*           Enable debug context (only available on OpenGL 4.3)
*
*       rlgl capabilities could be customized just defining some internal
*       values before library inclusion (default values listed):
*
*       #define RL_DEFAULT_BATCH_BUFFER_ELEMENTS   8192    // Default internal render batch elements limits
*       #define RL_DEFAULT_BATCH_BUFFERS              1    // Default number of batch buffers (multi-buffering)
*       #define RL_DEFAULT_BATCH_DRAWCALLS          256    // Default number of batch draw calls (by state changes: mode, texture)
*       #define RL_DEFAULT_BATCH_MAX_TEXTURE_UNITS    4    // Maximum number of textures units that can be activated on batch drawing (SetShaderValueTexture())
*
*       #define RL_MAX_MATRIX_STACK_SIZE             32    // Maximum size of internal Matrix stack
*       #define RL_MAX_SHADER_LOCATIONS              32    // Maximum number of shader locations supported
*       #define RL_CULL_DISTANCE_NEAR              0.01    // Default projection matrix near cull distance
*       #define RL_CULL_DISTANCE_FAR             1000.0    // Default projection matrix far cull distance
*
*       When loading a shader, the following vertex attributes and uniform
*       location names are tried to be set automatically:
*
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_POSITION     "vertexPosition"    // Bound by default to shader location: 0
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD     "vertexTexCoord"    // Bound by default to shader location: 1
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_NORMAL       "vertexNormal"      // Bound by default to shader location: 2
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_COLOR        "vertexColor"       // Bound by default to shader location: 3
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_TANGENT      "vertexTangent"     // Bound by default to shader location: 4
*       #define RL_DEFAULT_SHADER_ATTRIB_NAME_TEXCOORD2    "vertexTexCoord2"   // Bound by default to shader location: 5
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_MVP         "mvp"               // model-view-projection matrix
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_VIEW        "matView"           // view matrix
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_PROJECTION  "matProjection"     // projection matrix
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_MODEL       "matModel"          // model matrix
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_NORMAL      "matNormal"         // normal matrix (transpose(inverse(matModelView))
*       #define RL_DEFAULT_SHADER_UNIFORM_NAME_COLOR       "colDiffuse"        // color diffuse (base tint color, multiplied by texture color)
*       #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE0  "texture0"          // texture0 (texture slot active 0)
*       #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE1  "texture1"          // texture1 (texture slot active 1)
*       #define RL_DEFAULT_SHADER_SAMPLER2D_NAME_TEXTURE2  "texture2"          // texture2 (texture slot active 2)
*
*   DEPENDENCIES:
*      - OpenGL libraries (depending on platform and OpenGL version selected)
*      - GLAD OpenGL extensions loading library (only for OpenGL 3.3 Core, 4.3 Core)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2014-2023 Ramon Santamaria (@raysan5)
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

#ifndef WRAPPERS_GL_H
#define WRAPPERS_GL_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

gX_I0 gX_GL_Matrix_Mode(        gX_I32 mode);
gX_I0 gX_GL_Matrix_Push(        gX_I0);
gX_I0 gX_GL_Matrix_Pop(         gX_I0);
gX_I0 gX_GL_Matrix_LoadIdentity(gX_I0);
gX_I0 gX_GL_Matrix_Translate(   gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0 gX_GL_Matrix_Rotate(      gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 angle);
gX_I0 gX_GL_Matrix_Scale(       gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0 gX_GL_Matrix_Mul(         gX_F32 *mat);
gX_I0 gX_GL_Matrix_Frustum(     gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far);
gX_I0 gX_GL_Matrix_Ortho(       gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far);
gX_I0 gX_GL_Matrix_Viewport(    gX_I32 x, gX_I32 y, gX_I32 width, gX_I32 height);
gX_Matrix gX_GL_Matrix_GetModelView(gX_I0);
gX_Matrix gX_GL_Matrix_GetProjection(gX_I0);
gX_Matrix gX_GL_Matrix_GetTransform(gX_I0);
gX_I0     gX_GL_Matrix_SetModelView( gX_Matrix matrix);
gX_I0     gX_GL_Matrix_SetProjection(gX_Matrix matrix);

gX_I0 gX_GL_BackFaceCull_Enable(    gX_I0);
gX_I0 gX_GL_BackFaceCull_Disable(   gX_I0);
gX_I0 gX_GL_ColorBlend_Enable(      gX_I0);
gX_I0 gX_GL_ColorBlend_Disable(     gX_I0);
gX_I0 gX_GL_CullFace_Set(           gX_I32 mode);
gX_I0 gX_GL_DepthClamp_Enable(      gX_I0);
gX_I0 gX_GL_DepthClamp_Disable(     gX_I0);
gX_I0 gX_GL_DepthMask_Enable(       gX_I0);
gX_I0 gX_GL_DepthMask_Disable(      gX_I0);
gX_I0 gX_GL_DepthTest_Enable(       gX_I0);
gX_I0 gX_GL_DepthTest_Disable(      gX_I0);
gX_I0 gX_GL_Dither_Enable(          gX_I0);
gX_I0 gX_GL_Dither_Disable(         gX_I0);
gX_F32 gX_GL_LineWidth_Get(         gX_I0);
gX_I0  gX_GL_LineWidth_Set(         gX_F32 width);
gX_I0 gX_GL_Multisample_Enable(     gX_I0);
gX_I0 gX_GL_Multisample_Disable(    gX_I0);
gX_I0 gX_GL_PointMode_Enable(       gX_I0);
gX_I0 gX_GL_PointMode_Disable(      gX_I0);
gX_I0 gX_GL_ScissorTest_Enable(     gX_I0);
gX_I0 gX_GL_ScissorTest_Disable(    gX_I0);
gX_I0 gX_GL_Scissor(                gX_Rectangle rect);
gX_I0 gX_GL_SmoothLines_Enable(     gX_I0);
gX_I0 gX_GL_SmoothLines_Disable(    gX_I0);
gX_I0 gX_GL_StencilTest_Enable(     gX_I0);
gX_I0 gX_GL_StencilTest_Disable(    gX_I0);
gX_I0 gX_GL_WireMode_Enable(        gX_I0);
gX_I0 gX_GL_WireMode_Disable(       gX_I0);

gX_I0 gX_GL_Enable(gX_I32 cap);
gX_I0 gX_GL_Disable(gX_I32 cap);

gX_I0 gX_GL_Texture_Enable(         gX_U32 id);
gX_I0 gX_GL_Texture_Disable(        gX_I0);
gX_I0 gX_GL_Texture_Parameters(     gX_U32 id, gX_I32 param, gX_I32 value);

gX_I0 gX_GL_ClearScreenBuffers(     gX_I0);
gX_I0 gX_GL_CheckErrors(            gX_I0);

gX_I0 gX_GL_Shader_Compile(     gX_I0);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_GL_H
