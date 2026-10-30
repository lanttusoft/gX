/**********************************************************************************************
*
*   [gX: src/common/utils/shader.h]
*
*   Copyright (c) 2025 lanttu
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

#ifndef UTILS_SHADER_H
#define UTILS_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include <stdarg.h>
    
#include "../types.h"
    
gX_I0 gX_Shader_GetDefaultLocations(gX_Shader *shader);

gX_I0 gX_Shader_SetValue_Int(       gX_Shader shader, gX_I32 loc, gX_I32 i);
gX_I0 gX_Shader_SetValue_Float(     gX_Shader shader, gX_I32 loc, gX_F32 f);
gX_I0 gX_Shader_SetValue_V2(        gX_Shader shader, gX_I32 loc, gX_F32v2 v2);
gX_I0 gX_Shader_SetValue_V3(        gX_Shader shader, gX_I32 loc, gX_F32v3 v3);
gX_I0 gX_Shader_SetValue_V4(        gX_Shader shader, gX_I32 loc, gX_F32v4 v4);
gX_I0 gX_Shader_SetValue_Rectangle( gX_Shader shader, gX_I32 loc, gX_Rectangle rect);
gX_I0 gX_Shader_SetValue_Color(     gX_Shader shader, gX_I32 loc, gX_Color color); //rgba
gX_I0 gX_Shader_SetValue_ColorRGB(  gX_Shader shader, gX_I32 loc, gX_Color color); //rgb

gX_I0 gX_Shader_SetValue_IntArray(      gX_Shader shader, gX_I32 loc, gX_I32 *i,          gX_U32 amount);
gX_I0 gX_Shader_SetValue_FloatArray(    gX_Shader shader, gX_I32 loc, gX_F32 *f,          gX_U32 amount);
gX_I0 gX_Shader_SetValue_V2Array(       gX_Shader shader, gX_I32 loc, gX_F32v2 *v2,       gX_U32 amount);
gX_I0 gX_Shader_SetValue_V3Array(       gX_Shader shader, gX_I32 loc, gX_F32v3 *v3,       gX_U32 amount);
gX_I0 gX_Shader_SetValue_V4Array(       gX_Shader shader, gX_I32 loc, gX_F32v4 *v4,       gX_U32 amount);

gX_I32 gX_Shader_GetLocation_Multi(gX_Shader shader, gX_U32 count, ...);

gX_I0 gX_Shader_Locations_Get(gX_Shader_Locations *locs, gX_Shader *shader);

//opencode: bindless texture handle upload — directly uploads a 64-bit handle
//   to a sampler uniform (bypasses texture unit binding). No-op when
//   gX_BindlessSupported is false or the function pointer is unavailable.
gX_I0 gX_Shader_SetValue_Handle64(gX_Shader shader, gX_I32 loc, gX_U64 handle);

//opencode: standard shader UBO blocks (Matrices + Vectors from standard.vert)
typedef struct gX_Shader_StandardUBO {
    gX_I32 locMVP;
    gX_I32 locView;
    gX_I32 locModel;
    gX_I32 locProjection;
    gX_I32 locNormal;
    gX_I32 locLightSpace;
    gX_I32 locLight;
    gX_I32 locViewPos;
    gX_I32 locFlags[3];
} gX_Shader_StandardUBO;

gX_I0 gX_Shader_Standard_GetLocations(gX_Shader_StandardUBO *ubo, gX_Shader shader);
gX_I0 gX_Shader_Standard_UploadMatrices(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_Matrix mvp, gX_Matrix view, gX_Matrix model, gX_Matrix projection,
    gX_Matrix normal, gX_Matrix lightSpace);
gX_I0 gX_Shader_Standard_UploadVectors(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_F32v3 lightPos, gX_F32v3 viewPos);
gX_I0 gX_Shader_Standard_UploadFlags(gX_Shader_StandardUBO *ubo, gX_Shader shader,
    gX_I32 ambient, gX_I32 diffuse, gX_I32 specular);
    
#ifdef __cplusplus
}
#endif

#endif //UTILS_SHADER_H
