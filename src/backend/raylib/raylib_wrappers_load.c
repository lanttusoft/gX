/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers_load.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

//opencode: D12/D14 — backend asset-load surface. All raylib Load*/Unload*/Init*/Close*
//   API used by the engine lives here (declared in raylib_wrappers_model.h), so
//   engine code (wrappers, renderer, gui, scene) never calls raylib directly.
//   RAYLIB branch keeps raylib calls; GLFW branch uses stb_image (stbi_*),
//   cgltf, miniaudio (ma_*) and RHI texture/mesh upload so the GLFW build
//   needs no raylib.
#include <stdlib.h>
#include "raylib_wrappers_model.h"
#include "raylib_wrappers.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi_mesh.h"
#include "../../wrappers/convert.h"
#include "../../common/log.h"

//opencode: D14 — GLFW branch needs stb_image, cgltf and miniaudio symbols.
//   During the transition the raylib source files compiled for GLFW export
//   stbi_*/cgltf_*/ma_*; Phase 3 replaces them with glfw_thirdparty.c.


// ---------------------------------------------------------------- image

gX_Image raylib_Image_Load(const gX_I8 *fileName) {
    Image img = LoadImage(fileName);
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadRaw(const gX_I8 *fileName, gX_I32 width, gX_I32 height, gX_I32 format, gX_I32 headerSize) {
    Image img = LoadImageRaw(fileName, width, height, format, headerSize);
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadAnim(const gX_I8 *fileName, gX_I32 *frames) {
    Image img = LoadImageAnim(fileName, frames);
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadAnimFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize, gX_I32 *frames) {
    Image img = LoadImageAnimFromMemory(fileType, (const unsigned char *)fileData, dataSize, frames);
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize) {
    Image img = LoadImageFromMemory(fileType, (const unsigned char *)fileData, dataSize);
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadFromScreen(gX_I0) {
    Image img = LoadImageFromScreen();
    return gX_Image_ConvertFromBackend(img);
}

gX_Image raylib_Image_LoadFromTexture(gX_Texture texture) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    Image img = LoadImageFromTexture(tex);
    return gX_Image_ConvertFromBackend(img);
}

gX_I0 raylib_Image_Unload(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    UnloadImage(img);
}

gX_Color *raylib_Image_LoadColors(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    Color *colors = LoadImageColors(img);
    return (gX_Color *)colors;
}

gX_Color *raylib_Image_LoadPalette(gX_Image image, gX_I32 maxPaletteSize, gX_I32 *colorCount) {
    Image img = gX_Image_ConvertToBackend(image);
    Color *palette = LoadImagePalette(img, maxPaletteSize, colorCount);
    return (gX_Color *)palette;
}

gX_I0 raylib_Image_UnloadColors(gX_Color *colors) {
    if (colors) free(colors);
}

gX_I0 raylib_Image_UnloadPalette(gX_Color *colors) {
    if (colors) free(colors);
}

// ---------------------------------------------------------------- texture

gX_Texture raylib_Texture_Load(const gX_I8 *fileName) {
    Texture tex = LoadTexture(fileName);
    gX_Texture textu = gX_Texture_ConvertFromBackend(tex);
    return textu;
}

gX_Texture raylib_Texture_LoadCubemap(const gX_I8 *fileName, gX_I32 layout) {
    Image img = LoadImage(fileName);
    //ai: raylib may allocate an empty cubemap for an invalid image; reject before GPU creation.
    if (!img.data || img.width <= 0 || img.height <= 0)
    {
        TraceLog(LOG_WARNING, "GX_CUBEMAP: source image unavailable: %s", fileName);
        UnloadImage(img);
        return (gX_Texture){0};
    }
    Texture tex = LoadTextureCubemap(img, layout);
    gX_Texture textu = gX_Texture_ConvertFromBackend(tex);
    UnloadImage(img);
    return textu;
}

gX_Texture raylib_Texture_LoadFromImage(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    Texture tex = LoadTextureFromImage(img);
    return gX_Texture_ConvertFromBackend(tex);
}

gX_I0 raylib_Texture_Unload(gX_Texture texture) {
    if (texture.id == 0) return;
    Texture textu = gX_Texture_ConvertToBackend(texture);
    UnloadTexture(textu);
}

gX_I0 raylib_RenderTexture_Unload(gX_RenderTexture rt) {
    RenderTexture rentex = gX_RenderTexture_ConvertToBackend(rt);
    UnloadRenderTexture(rentex);
}

// ---------------------------------------------------------------- shader

gX_Shader raylib_Shader_Load(const gX_I8 *vertFileName, const gX_I8 *fragFileName) {
    Shader sha = LoadShader(vertFileName, fragFileName);
    return gX_Shader_ConvertFromBackend(sha);
}

gX_Shader raylib_Shader_LoadFromMemory(const gX_I8 *vertCode, const gX_I8 *fragCode) {
    Shader sha = LoadShaderFromMemory(vertCode, fragCode);
    return gX_Shader_ConvertFromBackend(sha);
}

gX_I0 raylib_Shader_Unload(gX_Shader shader) {
    if (shader.id == 0) return;
    Shader sha = gX_Shader_ConvertToBackend(shader);
    UnloadShader(sha);
}

// ---------------------------------------------------------------- font

gX_Font raylib_Font_Load(const gX_I8 *fileName) {
    Font font = LoadFont(fileName);
    return gX_Font_ConvertFromBackend(font);
}

gX_Font raylib_Font_LoadEx(const gX_I8 *fileName, gX_I32 fontSize, gX_I32 *codepoints, gX_I32 codepointCount) {
    Font fnt = LoadFontEx(fileName, fontSize, codepoints, codepointCount);
    return gX_Font_ConvertFromBackend(fnt);
}

gX_I0 raylib_Font_Unload(gX_Font font) {
    Font fnt = gX_Font_ConvertToBackend(font);
    UnloadFont(fnt);
}

// ---------------------------------------------------------------- model / mesh

gX_Model raylib_Model_Load(const gX_I8 *fileName) {
    Model modell = LoadModel(fileName);
    return gX_Model_ConvertFromBackend(modell);
}

gX_Model raylib_Model_LoadFromMesh(gX_Mesh mesh) {
    Mesh mes = gX_Mesh_ConvertToBackend(mesh);
    Model modell = LoadModelFromMesh(mes);
    return gX_Model_ConvertFromBackend(modell);
}

gX_I0 raylib_Model_Unload(gX_Model model) {
    Model modell = gX_Model_ConvertToBackend(model);
    UnloadModel(modell);
}

gX_I0 raylib_Mesh_Unload(gX_Mesh mesh) {
    Mesh mes = gX_Mesh_ConvertToBackend(mesh);
    UnloadMesh(mes);
}

// ---------------------------------------------------------------- audio


gX_AudioStream raylib_AudioStream_Load(gX_U32 sampleRate, gX_U32 sampleSize, gX_U32 channels) {
    AudioStream as = LoadAudioStream(sampleRate, sampleSize, channels);
    return gX_AudioStream_ConvertFromBackend(as);
}

gX_I0 raylib_AudioStream_Unload(gX_AudioStream stream) {
    AudioStream as = gX_AudioStream_ConvertToBackend(stream);
    UnloadAudioStream(as);
}

gX_Wave raylib_Wave_Load(const gX_I8 *fileName) {
    Wave wave = LoadWave(fileName);
    return gX_Wave_ConvertFromBackend(wave);
}

gX_I0 raylib_Wave_Unload(gX_Wave wave) {
    Wave wav = gX_Wave_ConvertToBackend(wave);
    UnloadWave(wav);
}

gX_Sound raylib_Sound_Load(const gX_I8 *fileName) {
    Sound sound = LoadSound(fileName);
    return gX_Sound_ConvertFromBackend(sound);
}

gX_Sound raylib_Sound_LoadWave(gX_Wave wave) {
    Wave wav = gX_Wave_ConvertToBackend(wave);
    Sound sound = LoadSoundFromWave(wav);
    return gX_Sound_ConvertFromBackend(sound);
}

gX_I0 raylib_Sound_Unload(gX_Sound sound) {
    if (sound.frameCount == 0) return;
    Sound snd = gX_Sound_ConvertToBackend(sound);
    UnloadSound(snd);
}

gX_Music raylib_Music_Load(const gX_I8 *fileName) {
    Music music = LoadMusicStream(fileName);
    return gX_Music_ConvertFromBackend(music);
}

gX_I0 raylib_Music_Unload(gX_Music music) {
    if (!music.ctxData && music.stream.buffer == 0) return;
    Music mus = gX_Music_ConvertToBackend(music);
    UnloadMusicStream(mus);
}

// ---------------------------------------------------------------- file data

gX_U8 *raylib_FileData_Load(const gX_I8 *fileName, gX_I32 *dataSize) {
    return LoadFileData(fileName, dataSize);
}

gX_I0 raylib_FileData_Unload(gX_U8 *data) {
    if (data) free(data);
}

// ---------------------------------------------------------------- window / audio device

gX_I1 raylib_Window_Init(gX_U32 width, gX_U32 height, const gX_I8 *name) {
    InitWindow((gX_I32)width, (gX_I32)height, (const char *)name);
    return (gX_I1)IsWindowReady();
}

gX_I0 raylib_Window_Close(gX_I0) {
    CloseWindow();
}

gX_I0 raylib_AudioDevice_Init(gX_I0) {
    //opencode: mute before InitAudioDevice to suppress startup pop/hiss
    SetMasterVolume(0.0f);
    InitAudioDevice();
}

gX_I0 raylib_AudioDevice_Close(gX_I0) {
    CloseAudioDevice();
}
