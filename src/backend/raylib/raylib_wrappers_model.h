/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers_model.h]
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

#ifndef RAYLIB_WRAPPERS_MODEL_H
#define RAYLIB_WRAPPERS_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include <stdio.h>
#include <string.h>
#include <math.h>
    
#include "../../common/types.h"

//opencode: raylib headers only available on desktop backends
#include "raylib.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wundef"
#include "raymath.h"
#pragma GCC diagnostic pop
#include "rcamera.h"
#include "rlgl.h"

//opencode: D12 — backend asset-load surface. All raylib Load*/Unload* API
//   lives here so engine code (wrappers, renderer, gui, scene) never calls
//   raylib directly. Implemented in raylib_wrappers_load.c.

// image
gX_Image raylib_Image_Load(const gX_I8 *fileName);
gX_Image raylib_Image_LoadRaw(const gX_I8 *fileName, gX_I32 width, gX_I32 height, gX_I32 format, gX_I32 headerSize);
gX_Image raylib_Image_LoadAnim(const gX_I8 *fileName, gX_I32 *frames);
gX_Image raylib_Image_LoadAnimFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize, gX_I32 *frames);
gX_Image raylib_Image_LoadFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize);
gX_Image raylib_Image_LoadFromScreen(gX_I0);
gX_Image raylib_Image_LoadFromTexture(gX_Texture texture);
gX_I0 raylib_Image_Unload(gX_Image image);
gX_Color *raylib_Image_LoadColors(gX_Image image);
gX_Color *raylib_Image_LoadPalette(gX_Image image, gX_I32 maxPaletteSize, gX_I32 *colorCount);
gX_I0 raylib_Image_UnloadColors(gX_Color *colors);
gX_I0 raylib_Image_UnloadPalette(gX_Color *colors);

// texture
gX_Texture raylib_Texture_Load(const gX_I8 *fileName);
gX_Texture raylib_Texture_LoadCubemap(const gX_I8 *fileName, gX_I32 layout);
gX_Texture raylib_Texture_LoadFromImage(gX_Image image);
gX_I0 raylib_Texture_Unload(gX_Texture texture);
gX_I0 raylib_RenderTexture_Unload(gX_RenderTexture rt);

// shader
gX_Shader raylib_Shader_Load(const gX_I8 *vertFileName, const gX_I8 *fragFileName);
gX_Shader raylib_Shader_LoadFromMemory(const gX_I8 *vertCode, const gX_I8 *fragCode);
gX_I0 raylib_Shader_Unload(gX_Shader shader);

// font
gX_Font raylib_Font_Load(const gX_I8 *fileName);
gX_Font raylib_Font_LoadEx(const gX_I8 *fileName, gX_I32 fontSize, gX_I32 *codepoints, gX_I32 codepointCount);
gX_I0 raylib_Font_Unload(gX_Font font);

// model / mesh
gX_Model raylib_Model_Load(const gX_I8 *fileName);
gX_Model raylib_Model_LoadFromMesh(gX_Mesh mesh);
gX_Model raylib_Model_LoadMesh(gX_Mesh mesh);
gX_I0 raylib_Model_Unload(gX_Model model);
gX_I0 raylib_Mesh_Unload(gX_Mesh mesh);

// audio
gX_AudioStream raylib_AudioStream_Load(gX_U32 sampleRate, gX_U32 sampleSize, gX_U32 channels);
gX_I0 raylib_AudioStream_Unload(gX_AudioStream stream);
gX_Wave raylib_Wave_Load(const gX_I8 *fileName);
gX_I0 raylib_Wave_Unload(gX_Wave wave);
gX_Sound raylib_Sound_Load(const gX_I8 *fileName);
gX_Sound raylib_Sound_LoadWave(gX_Wave wave);
gX_I0 raylib_Sound_Unload(gX_Sound sound);
gX_Music raylib_Music_Load(const gX_I8 *fileName);
gX_I0 raylib_Music_Unload(gX_Music music);

// file data
gX_U8 *raylib_FileData_Load(const gX_I8 *fileName, gX_I32 *dataSize);
gX_I0 raylib_FileData_Unload(gX_U8 *data);

// window / audio device
gX_I1 raylib_Window_Init(gX_U32 width, gX_U32 height, const gX_I8 *name);
gX_I0 raylib_Window_Close(gX_I0);
gX_I0 raylib_AudioDevice_Init(gX_I0);
gX_I0 raylib_AudioDevice_Close(gX_I0);

gX_Material raylib_Material_LoadDefault(gX_I0);
gX_Mesh raylib_Mesh_GenSphere(gX_F32 radius, gX_I32 rings, gX_I32 slices);
gX_I0 raylib_Mesh_Upload(gX_Mesh *mesh, gX_I1 dynamic);
gX_I0 raylib_Mesh_GenTangents(gX_Mesh *mesh);
gX_I0 raylib_Mesh_GenBins(gX_Mesh *mesh);
gX_I0 raylib_Mesh_ExportObj(gX_Mesh *mesh, const gX_I8 *fileName);
gX_I0 raylib_Model_SetMaterial(gX_Model *model, gX_I32 materialIndex);
//opencode: Model_Draw uses the separate-args version (not gX_Model_Parameters)
gX_I0 raylib_Model_Draw(gX_Model model, gX_F32v3 position, gX_F32 scale, gX_Color tint);
gX_I0 raylib_UpdateModelAnimation(gX_Model *model, gX_I32 anim, gX_I32 frame);

gX_I0 raylib_Mesh_Draw(gX_Mesh mesh, gX_Material material, gX_Matrix transform);
gX_I0 raylib_Mesh_Simple(gX_Mesh mesh, gX_Material material, gX_Matrix transform);
gX_I0 raylib_Model_DrawEx(gX_Model model, gX_F32v3 position, gX_F32v3 rotationAxis, gX_F32 rotationAngle, gX_F32v3 scale, gX_Color tint);
    
#ifdef __cplusplus
}
#endif

#endif //RAYLIB_WRAPPERS_MODEL_H
