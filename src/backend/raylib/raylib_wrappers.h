/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers.h]
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

#ifndef RAYLIB_WRAPPERS_H
#define RAYLIB_WRAPPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../common/types.h"

//opencode: raylib headers only available on desktop backends
#include "raylib.h"
//opencode: suppress raymath warnings (third-party code)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wundef"
#include "raymath.h"
#pragma GCC diagnostic pop
#include "rcamera.h"
#include "rlgl.h"

RenderTexture2D raylib_RenderTexture_Load(gX_I32 width, gX_I32 height, gX_I1 depth, gX_I1 floatingpoint);
gX_I0 raylib_Texture_Draw(  gX_Texture texture, gX_Texture_Draw_Params  params);
gX_I0 raylib_Texture_Draw3D(gX_Texture texture, gX_Texture_Draw_Params  params, gX_F32v3 pos);

gX_I0 raylib_Rectangle_Draw(gX_Rectangle rect,  gX_Rectangle_Parameters params);
gX_I0 raylib_Circle_Draw_Matrix(gX_F32v3 center, gX_F32 radius, Matrix matrix, gX_Color color);

gX_I0 raylib_Drawing_Begin3D(gX_Camera3D cam);

Vector2 raylib_Camera_GetWorldToScreen(Vector3 pos, Camera3D cam, Vector2 res);

gX_I0 raylib_Shader_Compile(const gX_I8 *code, gX_I32 type);
gX_I32 raylib_Shader_GetLocationAttrib(gX_Shader shader, const gX_I8 *attribName);
gX_I0 raylib_Shader_SetValueCubemap(gX_Shader shader, gX_I32 location, gX_Texture texture);
gX_I0 raylib_Shader_SetValueTextureSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot);
gX_I0 raylib_Shader_SetValueCubemapSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot);

const gX_I8 *raylib_Text_Format(const gX_I8 *text, va_list args);

//opencode: D13 — backend query surface. All raylib query API (input, window,
//   time, font measure, clipboard, random, URL, backend setup) lives here so
//   engine code never calls raylib directly. Implemented in
//   raylib_wrappers_query.c. Served to both desktop backends.

// keys
gX_I1 raylib_Key_IsPressed(gX_I32 key);
gX_I1 raylib_Key_IsReleased(gX_I32 key);
gX_I1 raylib_Key_IsDown(gX_I32 key);
gX_I1 raylib_Key_IsUp(gX_I32 key);
gX_I0 raylib_ExitKey_Set(gX_I32 key);

// mouse
gX_I1 raylib_Mouse_IsPressed(gX_I32 button);
gX_I1 raylib_Mouse_IsReleased(gX_I32 button);
gX_I1 raylib_Mouse_IsDown(gX_I32 button);
gX_I1 raylib_Mouse_IsUp(gX_I32 button);
gX_I32 raylib_Mouse_GetX(gX_I0);
gX_I32 raylib_Mouse_GetY(gX_I0);
gX_F32v2 raylib_Mouse_GetPos(gX_I0);
gX_F32v2 raylib_Mouse_GetDelta(gX_I0);
gX_Ray raylib_Mouse_GetRay(gX_F32v2 pos, gX_Camera3D cam);
gX_F32 raylib_Mouse_GetWheelMove(gX_I0);
gX_F32v2 raylib_Mouse_GetWheelMoveV(gX_I0);
gX_I0 raylib_Mouse_SetPos(gX_F32v2 pos);
gX_I0 raylib_Mouse_SetOffset(gX_F32v2 offset);
gX_I0 raylib_Mouse_SetScale(gX_F32v2 scale);
gX_I0 raylib_Mouse_SetCursor(gX_I32 cursor);

// cursor
gX_I0 raylib_Cursor_Show(gX_I0);
gX_I0 raylib_Cursor_Hide(gX_I0);
gX_I1 raylib_Cursor_IsHidden(gX_I0);
gX_I0 raylib_Cursor_Enable(gX_I0);
gX_I0 raylib_Cursor_Disable(gX_I0);

// gamepad
gX_I1 raylib_Gamepad_IsAvailable(gX_I32 gamepad);
gX_I8 *raylib_Gamepad_GetName(gX_I32 gamepad);
gX_I1 raylib_Gamepad_Button_IsPressed(gX_I32 gamepad, gX_I32 button);
gX_I1 raylib_Gamepad_Button_IsDown(gX_I32 gamepad, gX_I32 button);
gX_I1 raylib_Gamepad_Button_IsReleased(gX_I32 gamepad, gX_I32 button);
gX_I1 raylib_Gamepad_Button_IsUp(gX_I32 gamepad, gX_I32 button);
gX_I32 raylib_Gamepad_Button_GetPressed(gX_I32 gamepad);
gX_I32 raylib_Gamepad_Axis_GetCount(gX_I32 gamepad);
gX_F32 raylib_Gamepad_Axis_GetMovement(gX_I32 gamepad, gX_I32 axis);
gX_I32 raylib_Gamepad_Mappings_Set(const gX_I8 *mappings);
gX_I0 raylib_Gamepad_Vibration_Set(gX_I32 gamepad, gX_F32 leftMotor, gX_F32 rightMotor, gX_F32 duration);

// window
gX_I32 raylib_Window_GetScreenWidth(gX_I0);
gX_I32 raylib_Window_GetScreenHeight(gX_I0);
gX_I32 raylib_Window_GetRenderWidth(gX_I0);
gX_I32 raylib_Window_GetRenderHeight(gX_I0);
gX_I1 raylib_Window_IsResized(gX_I0);
gX_I0 raylib_Window_SetMinSize(gX_I32 width, gX_I32 height);

// time
gX_F64 raylib_Time_Get(gX_I0);
gX_F64 raylib_Time_GetFrame(gX_I0);

// font
gX_Font raylib_Font_GetDefault(gX_I0);
gX_F32v2 raylib_Font_MeasureTextEx(gX_Font font, const gX_I8 *text, gX_F32 fontSize, gX_F32 spacing);

// misc
const gX_I8 *raylib_Clipboard_Get(gX_I0);
gX_I0 raylib_Clipboard_Set(const gX_I8 *text);
gX_I32 raylib_Random_GetValue(gX_I32 min, gX_I32 max);
gX_I0 raylib_Random_SetSeed(gX_U32 seed);
gX_I0 raylib_URL_Open(const gX_I8 *url);

// backend setup
gX_I0 raylib_Backend_PreInit(gX_U32 configFlags);
void *raylib_GetProcAddress(gX_I0);

// misc setup
gX_I0 raylib_TargetFPS_Set(gX_F32 fps);
gX_I0 raylib_MasterVolume_Set(gX_F32 volume);
gX_I0 raylib_Image_Export(gX_Image image, const gX_I8 *fileName);

//opencode: D14 — backend control surface. All raylib control/update API
//   (window control, frame begin/end, audio playback, texture updates, glyph
//   lookup, pixel-size, file save, working dir) lives here so engine code
//   never calls raylib directly. Implemented in raylib_wrappers_control.c.

// window control
gX_I1 raylib_Window_ShouldClose(gX_I0);
gX_I1 raylib_Window_IsReady(gX_I0);
gX_I1 raylib_Window_IsFullscreen(gX_I0);
gX_I1 raylib_Window_IsHidden(gX_I0);
gX_I1 raylib_Window_IsMinimized(gX_I0);
gX_I1 raylib_Window_IsMaximized(gX_I0);
gX_I1 raylib_Window_IsFocused(gX_I0);
gX_I1 raylib_Window_IsState(gX_U32 flag);
gX_I0 raylib_Window_SetState(gX_U32 flags);
gX_I0 raylib_Window_ClearState(gX_U32 flags);
gX_I0 raylib_Window_ToggleFullscreen(gX_I0);
gX_I0 raylib_Window_ToggleBorderless(gX_I0);
gX_I0 raylib_Window_Minimize(gX_I0);
gX_I0 raylib_Window_Maximize(gX_I0);
gX_I0 raylib_Window_Restore(gX_I0);
gX_I0 raylib_Window_SetTitle(const gX_I8 *title);
gX_I0 raylib_Window_SetPosition(gX_I32 x, gX_I32 y);
gX_I0 raylib_Window_SetMonitor(gX_I32 monitor);

// frame begin/end
gX_I0 raylib_Drawing_Begin(gX_I0);
gX_I0 raylib_Drawing_End(gX_I0);
//opencode: flush raylib's native batch before RHI-only passes or FBO changes
gX_I0 raylib_Drawing_Flush(gX_I0);

// audio control
gX_I0 raylib_AudioStream_Play(gX_AudioStream stream);
gX_I0 raylib_AudioStream_Update(gX_AudioStream stream, const gX_I0 *data, gX_I32 frameCount);
gX_I0 raylib_Sound_Update(gX_Sound sound, gX_I0 *data, gX_I32 sampleCount);
gX_I0 raylib_Sound_Play(gX_Sound sound);
gX_I0 raylib_Sound_Stop(gX_Sound sound);
gX_I1 raylib_Sound_IsPlaying(gX_Sound sound);
gX_I0 raylib_Sound_SetPan(gX_Sound sound, gX_F32 pan);
gX_I0 raylib_Sound_SetVolume(gX_Sound sound, gX_F32 volume);
gX_I0 raylib_Sound_SetPitch(gX_Sound sound, gX_F32 pitch);
gX_I0 raylib_Music_Update(gX_Music music);
gX_I0 raylib_Music_Play(gX_Music music);
gX_I0 raylib_Music_Pause(gX_Music music);
gX_I0 raylib_Music_Stop(gX_Music music);
gX_I0 raylib_Music_Resume(gX_Music music);
gX_I0 raylib_Music_Seek(gX_Music music, gX_F32 position);
gX_I0 raylib_Music_SetVolume(gX_Music music, gX_F32 volume);
gX_I0 raylib_Music_SetPitch(gX_Music music, gX_F32 pitch);
gX_I0 raylib_Music_SetPan(gX_Music music, gX_F32 pan);
gX_I1 raylib_Music_IsPlaying(gX_Music music);
gX_F32 raylib_Music_GetLength(gX_Music music);
gX_F32 raylib_Music_GetTimePlayed(gX_Music music);

// texture control
gX_I0 raylib_Texture_SetFilter(gX_Texture texture, gX_I32 filter);
gX_I0 raylib_Texture_SetWrap(gX_Texture texture, gX_I32 wrap);
gX_I0 raylib_Texture_Update(gX_Texture texture, const gX_I0 *pixels);
gX_I0 raylib_Texture_UpdateRec(gX_Texture texture, gX_Rectangle rec, const gX_I0 *pixels);

// text control
gX_I32 raylib_Font_GetGlyphIndex(gX_Font font, gX_I32 codepoint);

// color/misc
gX_I32 raylib_Color_GetPixelDataSize(gX_I32 width, gX_I32 height, gX_I32 format);

// file control
gX_I1 raylib_FileData_Save(const gX_I8 *fileName, gX_I0 *data, gX_I32 dataSize);
const gX_I8 *raylib_Directory_Working_Get(gX_I0);

#include "raylib_wrappers_model.h"

#ifdef __cplusplus
}
#endif

#endif //RAYLIB_WRAPPERS_H
