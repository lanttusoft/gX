/**********************************************************************************************
*
*   [gX: src/backend/raylib/raylib_wrappers_query.c]
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

//opencode: D13/D14 — backend query surface. All raylib query API used by the
//   engine (input, window, time, font measure, clipboard, random, URL) lives
//   here so engine code (wrappers, renderer, gui, scene) never calls raylib
//   directly. RAYLIB branch keeps raylib calls; GLFW branch delegates to the
//   gX_glfw_* natives so the GLFW build needs no raylib.
#include "raylib_wrappers_model.h"
#include "raylib_wrappers.h"
#include "../../wrappers/convert.h"
#include "../../common/log.h"
//opencode: ray construction uses the engine's logical game-screen dimensions.
#include "../../main.h"

//opencode: D14 — GLFW branch delegates to gX_glfw_* natives


// ---------------------------------------------------------------- keys

gX_I1 raylib_Key_IsPressed(gX_I32 key) {
    return (gX_I1)IsKeyPressed((int)key);
}
gX_I1 raylib_Key_IsReleased(gX_I32 key) {
    return (gX_I1)IsKeyReleased((int)key);
}
gX_I1 raylib_Key_IsDown(gX_I32 key) {
    return (gX_I1)IsKeyDown((int)key);
}
gX_I1 raylib_Key_IsUp(gX_I32 key) {
    return (gX_I1)IsKeyUp((int)key);
}

gX_I0 raylib_ExitKey_Set(gX_I32 key) {
    SetExitKey((int)key);
}

// ---------------------------------------------------------------- mouse

gX_I1 raylib_Mouse_IsPressed(gX_I32 button) {
    return (gX_I1)IsMouseButtonPressed((int)button);
}
gX_I1 raylib_Mouse_IsReleased(gX_I32 button) {
    return (gX_I1)IsMouseButtonReleased((int)button);
}
gX_I1 raylib_Mouse_IsDown(gX_I32 button) {
    return (gX_I1)IsMouseButtonDown((int)button);
}
gX_I1 raylib_Mouse_IsUp(gX_I32 button) {
    return (gX_I1)IsMouseButtonUp((int)button);
}

gX_I32 raylib_Mouse_GetX(gX_I0) {
    return (gX_I32)GetMouseX();
}
gX_I32 raylib_Mouse_GetY(gX_I0) {
    return (gX_I32)GetMouseY();
}

gX_F32v2 raylib_Mouse_GetPos(gX_I0) {
    Vector2 result = GetMousePosition();
    return (gX_F32v2){result.x, result.y};
}

gX_F32v2 raylib_Mouse_GetDelta(gX_I0) {
    Vector2 result = GetMouseDelta();
    return (gX_F32v2){result.x, result.y};
}

gX_Ray raylib_Mouse_GetRay(gX_F32v2 pos, gX_Camera3D cam) {
    Camera3D backend = gX_Camera3D_ConvertToBackend(cam);
    Ray result = GetScreenToWorldRayEx((Vector2){pos.x, pos.y}, backend,
        (int)maindata.screenWidth, (int)maindata.screenHeight);
    return gX_Ray_ConvertFromBackend(result);
}

gX_F32 raylib_Mouse_GetWheelMove(gX_I0) {
    return (gX_F32)GetMouseWheelMove();
}

gX_F32v2 raylib_Mouse_GetWheelMoveV(gX_I0) {
    Vector2 result = GetMouseWheelMoveV();
    return (gX_F32v2){result.x, result.y};
}

gX_I0 raylib_Mouse_SetPos(gX_F32v2 pos) {
    SetMousePosition((int)pos.x, (int)pos.y);
}
gX_I0 raylib_Mouse_SetOffset(gX_F32v2 offset) {
    SetMouseOffset((int)offset.x, (int)offset.y);
}
gX_I0 raylib_Mouse_SetScale(gX_F32v2 scale) {
    SetMouseScale(scale.x, scale.y);
}
gX_I0 raylib_Mouse_SetCursor(gX_I32 cursor) {
    SetMouseCursor((int)cursor);
}

// ---------------------------------------------------------------- cursor

gX_I0 raylib_Cursor_Show(gX_I0) {
    ShowCursor();
}
gX_I0 raylib_Cursor_Hide(gX_I0) {
    HideCursor();
}
gX_I1 raylib_Cursor_IsHidden(gX_I0) {
    return (gX_I1)IsCursorHidden();
}
gX_I0 raylib_Cursor_Enable(gX_I0) {
    EnableCursor();
}
gX_I0 raylib_Cursor_Disable(gX_I0) {
    DisableCursor();
}

// ---------------------------------------------------------------- gamepad

gX_I1 raylib_Gamepad_IsAvailable(gX_I32 gamepad) {
    return (gX_I1)IsGamepadAvailable((int)gamepad);
}
gX_I8 *raylib_Gamepad_GetName(gX_I32 gamepad) {
    return (gX_I8 *)GetGamepadName((int)gamepad);
}

gX_I1 raylib_Gamepad_Button_IsPressed(gX_I32 gamepad, gX_I32 button) {
    return (gX_I1)IsGamepadButtonPressed((int)gamepad, (int)button);
}
gX_I1 raylib_Gamepad_Button_IsDown(gX_I32 gamepad, gX_I32 button) {
    return (gX_I1)IsGamepadButtonDown((int)gamepad, (int)button);
}
gX_I1 raylib_Gamepad_Button_IsReleased(gX_I32 gamepad, gX_I32 button) {
    return (gX_I1)IsGamepadButtonReleased((int)gamepad, (int)button);
}
gX_I1 raylib_Gamepad_Button_IsUp(gX_I32 gamepad, gX_I32 button) {
    return (gX_I1)IsGamepadButtonUp((int)gamepad, (int)button);
}
gX_I32 raylib_Gamepad_Button_GetPressed(gX_I32 gamepad) {
    (void)gamepad;
    return (gX_I32)GetGamepadButtonPressed();
}
gX_I32 raylib_Gamepad_Axis_GetCount(gX_I32 gamepad) {
    return (gX_I32)GetGamepadAxisCount((int)gamepad);
}
gX_F32 raylib_Gamepad_Axis_GetMovement(gX_I32 gamepad, gX_I32 axis) {
    return (gX_F32)GetGamepadAxisMovement((int)gamepad, (int)axis);
}
gX_I32 raylib_Gamepad_Mappings_Set(const gX_I8 *mappings) {
    return (gX_I32)SetGamepadMappings((const char *)mappings);
}
gX_I0 raylib_Gamepad_Vibration_Set(gX_I32 gamepad, gX_F32 leftMotor, gX_F32 rightMotor, gX_F32 duration) {
    SetGamepadVibration((int)gamepad, leftMotor, rightMotor, duration);
}

// ---------------------------------------------------------------- window

gX_I32 raylib_Window_GetScreenWidth(gX_I0) {
    return (gX_I32)GetScreenWidth();
}
gX_I32 raylib_Window_GetScreenHeight(gX_I0) {
    return (gX_I32)GetScreenHeight();
}
gX_I32 raylib_Window_GetRenderWidth(gX_I0) {
    return (gX_I32)GetRenderWidth();
}
gX_I32 raylib_Window_GetRenderHeight(gX_I0) {
    return (gX_I32)GetRenderHeight();
}
gX_I1 raylib_Window_IsResized(gX_I0) {
    return (gX_I1)IsWindowResized();
}
gX_I0 raylib_Window_SetMinSize(gX_I32 width, gX_I32 height) {
    SetWindowMinSize((int)width, (int)height);
}

// ---------------------------------------------------------------- time

gX_F64 raylib_Time_Get(gX_I0) {
    return (gX_F64)GetTime();
}
gX_F64 raylib_Time_GetFrame(gX_I0) {
    return (gX_F64)GetFrameTime();
}

// ---------------------------------------------------------------- font

gX_Font raylib_Font_GetDefault(gX_I0) {
    Font font = GetFontDefault();
    return gX_Font_ConvertFromBackend(font);
}

gX_F32v2 raylib_Font_MeasureTextEx(gX_Font font, const gX_I8 *text, gX_F32 fontSize, gX_F32 spacing) {
    Font backendFont = gX_Font_ConvertToBackend(font);
    Vector2 result = MeasureTextEx(backendFont, (const char *)text, fontSize, spacing);
    return (gX_F32v2){result.x, result.y};
}

// ---------------------------------------------------------------- misc

const gX_I8 *raylib_Clipboard_Get(gX_I0) {
    return (const gX_I8 *)GetClipboardText();
}
gX_I0 raylib_Clipboard_Set(const gX_I8 *text) {
    SetClipboardText((const char *)text);
}
gX_I32 raylib_Random_GetValue(gX_I32 min, gX_I32 max) {
    return (gX_I32)GetRandomValue((int)min, (int)max);
}
gX_I0 raylib_Random_SetSeed(gX_U32 seed) {
    SetRandomSeed((unsigned int)seed);
}
gX_I0 raylib_URL_Open(const gX_I8 *url) {
    OpenURL((const char *)url);
}

// ---------------------------------------------------------------- backend setup

gX_I0 raylib_Backend_PreInit(gX_U32 configFlags) {
    SetTraceLogCallback(gX_TraceLog_Print);
    if (configFlags) SetConfigFlags((unsigned int)configFlags);
}

void *raylib_GetProcAddress(gX_I0) {
    return (void *)rlGetProcAddress;
}

// ---------------------------------------------------------------- misc setup

gX_I0 raylib_TargetFPS_Set(gX_F32 fps) {
    SetTargetFPS((int)fps);
}
gX_I0 raylib_MasterVolume_Set(gX_F32 volume) {
    SetMasterVolume(volume);
}
gX_I0 raylib_Image_Export(gX_Image image, const gX_I8 *fileName) {
    Image backendImg = gX_Image_ConvertToBackend(image);
    ExportImage(backendImg, (const char *)fileName);
}
