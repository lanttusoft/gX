/**********************************************************************************************
* 
*   [wrappers/input.h]
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
//lanttu: original wrapper for raylib input (cursor/keyboard/mouse/gamepad) functions
#ifndef WRAPPERS_INPUT_H
#define WRAPPERS_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

gX_I0 gX_Cursor_Show(           gX_I0);
gX_I0 gX_Cursor_Hide(           gX_I0);
gX_I1 gX_Cursor_IsHidden(       gX_I0);
gX_I0 gX_Cursor_Enable(         gX_I0);
gX_I0 gX_Cursor_Disable(        gX_I0);

gX_I1 gX_Key_IsPressed(         gX_I32 key);
gX_I1 gX_Key_IsReleased(        gX_I32 key);
gX_I1 gX_Key_IsDown(            gX_I32 key);
gX_I1 gX_Key_IsUp(              gX_I32 key);
gX_I0 gX_ExitKey_Set(           gX_I32 key);

//opencode: next queued printable character (textinput widget); 0 when empty
gX_I32 gX_GetCharPressed(       gX_I0);

gX_I1  gX_Gamepad_IsAvailable(      gX_I32 gamepad);
gX_I8 *gX_Gamepad_GetName(          gX_I32 gamepad);
gX_I1  gX_Gamepad_Button_IsPressed( gX_I32 gamepad, gX_I32 button);
gX_I1  gX_Gamepad_Button_IsDown(    gX_I32 gamepad, gX_I32 button);
gX_I1  gX_Gamepad_Button_IsReleased(gX_I32 gamepad, gX_I32 button);
gX_I1  gX_Gamepad_Button_IsUp(      gX_I32 gamepad, gX_I32 button);
gX_I32 gX_Gamepad_Button_GetPressed(gX_I32 gamepad);
gX_I32 gX_Gamepad_Axis_GetCount(    gX_I32 gamepad);
gX_F32 gX_Gamepad_Axis_GetMovement( gX_I32 gamepad, gX_I32 axis);
gX_I32 gX_Gamepad_Mappings_Set(     const gX_I8 *mappings);
gX_I0  gX_Gamepad_Vibration_Set(    gX_I32 gamepad, gX_F32 leftMotor, gX_F32 rightMotor, gX_F32 duration);

gX_I1    gX_Mouse_IsPressed(      gX_I32 button);
gX_I1    gX_Mouse_IsReleased(     gX_I32 button);
gX_I1    gX_Mouse_IsDown(         gX_I32 button);
gX_I1    gX_Mouse_IsUp(           gX_I32 button);
gX_I32   gX_Mouse_GetX(           gX_I0);
gX_I32   gX_Mouse_GetY(           gX_I0);
gX_F32v2 gX_Mouse_GetPos(         gX_I0);
gX_F32v2 gX_Mouse_GetDelta(       gX_I0);
//opencode: pos is in logical game-screen pixels, not desktop window coordinates.
gX_Ray   gX_Mouse_GetRay(         gX_F32v2 pos, gX_Camera3D cam3D);
gX_I0    gX_Mouse_SetPos(         gX_F32v2 pos);
gX_I0    gX_Mouse_SetOffset(      gX_F32v2 offset);
gX_I0    gX_Mouse_SetScale(       gX_F32v2 scale);
gX_F32   gX_Mouse_GetWheelMove(   gX_I0);
gX_F32v2 gX_Mouse_GetWheelMoveV(  gX_I0);
gX_I0    gX_Mouse_SetCursor(      gX_I32 cursor);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_INPUT_H
