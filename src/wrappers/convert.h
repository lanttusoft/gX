/**********************************************************************************************
* 
*   [wrappers/convert.h]
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
//opencode: backend type conversion functions (gX ↔ backend)
#ifndef WRAPPERS_CONVERT_H
#define WRAPPERS_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"

//internal to wrappers

#include "../common/types.h"

#include "../backend/raylib/raylib_wrappers.h"

gX_F32v2            gX_F32v2_ConvertFromBackend(        Vector2 v2);
gX_F32v3            gX_F32v3_ConvertFromBackend(        Vector3 v3);
gX_F32v4            gX_F32v4_ConvertFromBackend(        Vector4 v4);
gX_Matrix           gX_Matrix_ConvertFromBackend(       Matrix mat);
gX_Color            gX_Color_ConvertFromBackend(        Color color);
gX_Rectangle        gX_Rectangle_ConvertFromBackend(    Rectangle rectangle);
gX_Texture          gX_Texture_ConvertFromBackend(      Texture texture);
gX_RenderTexture    gX_RenderTexture_ConvertFromBackend(RenderTexture rt);
gX_Shader           gX_Shader_ConvertFromBackend(       Shader shader);
gX_MaterialMap      gX_MaterialMap_ConvertFromBackend(  MaterialMap materialmap);
gX_Material         gX_Material_ConvertFromBackend(     Material material);
gX_Mesh             gX_Mesh_ConvertFromBackend(         Mesh mesh);
gX_Mesh             gX_MeshPtr_ConvertFromBackend(      Mesh *mesh);
gX_ModelSkeleton    gX_ModelSkeleton_ConvertFromBackend(ModelSkeleton skeleton);
gX_Model            gX_Model_ConvertFromBackend(        Model model);
gX_Font             gX_Font_ConvertFromBackend(         Font font);
gX_Wave             gX_Wave_ConvertFromBackend(         Wave wave);
gX_AudioStream      gX_AudioStream_ConvertFromBackend(  AudioStream stream);
gX_Sound            gX_Sound_ConvertFromBackend(        Sound sound);
gX_Music            gX_Music_ConvertFromBackend(        Music music);
gX_Camera2D         gX_Camera2D_ConvertFromBackend(     Camera2D camera);
gX_Camera3D         gX_Camera3D_ConvertFromBackend(     Camera3D camera);
gX_Ray              gX_Ray_ConvertFromBackend(          Ray ray);
gX_RayCollision     gX_RayCollision_ConvertFromBackend( RayCollision collision);
gX_Image            gX_Image_ConvertFromBackend(        Image image); //opencode

Vector2             gX_F32v2_ConvertToBackend(          gX_F32v2 v2);
Vector3             gX_F32v3_ConvertToBackend(          gX_F32v3 v3);
Vector4             gX_F32v4_ConvertToBackend(          gX_F32v4 v4);
Matrix              gX_Matrix_ConvertToBackend(         gX_Matrix mat);
Color               gX_Color_ConvertToBackend(          gX_Color color);
Rectangle           gX_Rectangle_ConvertToBackend(      gX_Rectangle rectangle);
Texture             gX_Texture_ConvertToBackend(        gX_Texture texture);
RenderTexture       gX_RenderTexture_ConvertToBackend(  gX_RenderTexture rt);
Shader              gX_Shader_ConvertToBackend(         gX_Shader shader);
MaterialMap         gX_MaterialMap_ConvertToBackend(    gX_MaterialMap materialmap);
Material            gX_Material_ConvertToBackend(       gX_Material material);
Mesh                gX_Mesh_ConvertToBackend(           gX_Mesh mesh);
Mesh                gX_MeshPtr_ConvertToBackend(        gX_Mesh *mesh);
ModelSkeleton       gX_ModelSkeleton_ConvertToBackend(  gX_ModelSkeleton skeleton);
Model               gX_Model_ConvertToBackend(          gX_Model model);
Font                gX_Font_ConvertToBackend(           gX_Font font);
Wave                gX_Wave_ConvertToBackend(           gX_Wave wave);
AudioStream         gX_AudioStream_ConvertToBackend(    gX_AudioStream stream);
Sound               gX_Sound_ConvertToBackend(          gX_Sound sound);
Music               gX_Music_ConvertToBackend(          gX_Music music);
Camera2D            gX_Camera2D_ConvertToBackend(       gX_Camera2D camera);
Camera3D            gX_Camera3D_ConvertToBackend(       gX_Camera3D camera);
Ray                 gX_Ray_ConvertToBackend(            gX_Ray ray);
RayCollision        gX_RayCollision_ConvertToBackend(   gX_RayCollision collision);
Image               gX_Image_ConvertToBackend(          gX_Image image); //opencode


#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_CONVERT_H
