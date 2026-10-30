/**********************************************************************************************
* 
*   [common/types_shader.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025 lanttu
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

#ifndef TYPES_SHADER_H
#define TYPES_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "primary.h"
#include "state.h"
#include "text.h"

// [raylib] borrowed
// Shader
typedef struct gX_Shader {
    gX_U32 id;        // Shader program id
    gX_I32 *locs;     // Shader locations array (RL_MAX_SHADER_LOCATIONS)
} gX_Shader;

typedef struct gX_Shader_Locations {
    gX_I32 locTime;
    gX_I32 locRes;
    
    gX_I32 locUniform;
    gX_I32 locBox;
    gX_I32 locColor;
    
    gX_I32 locTexInput;
    gX_I32 locTexOutput;
    gX_I32 locTex0;
    gX_I32 locTex1;
    gX_I32 locTex2;
    gX_I32 locTex3;
    gX_I32 locTex4;
    gX_I32 locTex5;
    gX_I32 locTex6;
    gX_I32 locTex7;
    gX_I32 locTexSky;
    
    gX_I32 locCamPos;
    gX_I32 locCamTarget;
    gX_I32 locCamUp;
    gX_I32 locCamFov;
    
    gX_I32 locLightDir;
} gX_Shader_Locations;

typedef struct gX_Shader_Prototype_Stage {
    gX_I1        available;
    gX_TextLine  path;
    gX_I32       version;
    gX_U32       codeLength;
    gX_I8       *code;
} gX_Shader_Prototype_Stage;

typedef struct gX_Shader_Prototype {
    gX_U32 state;
    gX_Shader_Prototype_Stage vertex;
    gX_Shader_Prototype_Stage tesselation_control;
    gX_Shader_Prototype_Stage tesselation_evaluation;
    gX_Shader_Prototype_Stage geometry;
    gX_Shader_Prototype_Stage fragment;
    gX_Shader_Prototype_Stage compute;
} gX_Shader_Prototype;
    
#ifdef __cplusplus
}
#endif

#endif //TYPES_SHADER_H
