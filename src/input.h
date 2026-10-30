/**********************************************************************************************
*
*   [gX: src/input.h]
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


#ifndef INPUT_H
#define INPUT_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "common/types.h"
#include "common/text.h"
    
#define gX_BINDS_COMMON \
    X(MOVE_FORWARD) \
    X(MOVE_BACKWARD) \
    X(MOVE_LEFT) \
    X(MOVE_RIGHT) \
    X(MOVE_UP) \
    X(MOVE_DOWN) \
    X(ROTATE_CLOCKWISE) \
    X(ROTATE_COUNTERCLOCKWISE) \
    X(TILT_MORE) \
    X(TILT_LESS) \
    X(ZOOM_MORE) \
    X(ZOOM_LESS) \
    X(FOV_ZOOM) \
    X(FOV_WIDE) \
    X(FOV_RESET) \
    X(SPEED_FAST) \
    X(SPEED_SLOW) \
    X(CURSOR_TOGGLE) \
    X(CONFIRM) \
    X(CANCEL) \
    X(MENU) \

typedef enum gX_Binds_Common {
#define X(bind) gX_BIND_##bind,
    gX_BINDS_COMMON
#undef X
    gX_BIND_COMMON_AMOUNT
} gX_Binds_Common;
const gX_I8 *gX_Binds_Common_String(gX_I32 bind);
    
typedef struct gX_Input_State {
    gX_I1 DOWN;
    gX_I1 UP;
    gX_I1 PRESSED;
    gX_I1 RELEASED;
} gX_Input_State;
    
typedef struct gX_Input_Key {
    gX_Input_State state;
    gX_F32 counter;
    gX_F32 counterIntegral;
    gX_U32 pressCounter;
} gX_Input_Key;
 
//lol keylogger
typedef struct gX_Input_Keyboard {
    gX_Input_Key keys[512];
    gX_U32 keyHistoryIndexPtr;
    gX_I8 keyHistory[16];
    //opencode: text input — characters queued from key presses (textinput widget).
    //   gX_GetCharPressed() pops from this ring buffer (0 when empty).
    gX_I8  charQueue[64];
    gX_U32 charQueueCount;
    gX_U32 charQueueHead;
    gX_U32 charQueueTail;
} gX_Input_Keyboard;

typedef struct gX_Input_KeyMods {
    gX_I1 leftControl;
    gX_I1 rightControl;
    gX_I1 leftShift;
    gX_I1 rightShift;
    gX_I1 alt;
    gX_I1 altGr;
    gX_I1 super;
    gX_I1 fn;
} gX_Input_KeyMods;

typedef struct gX_Input_KeyCombo {
    gX_I32 key;
    gX_Input_KeyMods mods;
} gX_Input_KeyCombo;
    
typedef struct gX_Input_Mouse {
    gX_F32v2        coord;
    gX_F32v2        coordDelta;
    gX_F32v2        coordPrev;
    
    //mouse buttons
    gX_Input_State  leftButton;
    gX_Input_State  rightButton;
    gX_Input_State  middleButton;
    gX_Input_State  extraButton[8];
    gX_I32 buttonHistory[16];
    
    //scrollwheel
    gX_F32          scroll;
    gX_F32          scrollIntegral;
    
    gX_F32v2        offset;
    gX_F32v2        scale;
    gX_I32          cursor;
} gX_Input_Mouse;

typedef struct gX_Input_Gamepad {
    gX_I1 available;
    gX_I8 name[256];
    
    gX_Input_State buttons[64];
    gX_I32 buttonHistory[16];
    
    gX_I32 axisCount;
    gX_F32 axisMovement[8];
} gX_Input_Gamepad;

#define gX_INPUT_KEYBOARD_KEYS 512

//opencode: virtual input — programmatic mouse/key override for AI probe, scripts, tests
typedef struct gX_Input_Virtual {
    gX_I1   active;
    gX_I32  mouseX, mouseY;
    gX_I1   clickPending;
    gX_I1   keyPending;
    gX_I32  keyCode;
    gX_I1   keyShift;
    //opencode: persistent held-key bitmap — a key held via SetKeyHeld stays
    //   DOWN across frames until released (movement binds read state.DOWN).
    gX_I1   held[gX_INPUT_KEYBOARD_KEYS];
    gX_I1   heldShift[gX_INPUT_KEYBOARD_KEYS];
} gX_Input_Virtual;

#define gX_BIND_KEYS_AMOUNT 8
#define gX_BIND_AMOUNT 64
typedef struct gX_Input_Bind {
    gX_TextLine name;
    gX_U32 keysAmount;
    gX_I32 keys[gX_BIND_KEYS_AMOUNT];
    gX_Input_State state;
} gX_Input_Bind;
    
typedef struct gX_Input {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_I1 loaded;
    gX_U32 state;
    gX_I1 masterIndexSwitchResponse;
    
    gX_I32 exitKey;
    gX_I32 menuKey;
    gX_I32 screenshotKey;
    gX_I32 toolbarKey;
    gX_I32 consoleKey;
    gX_I32 helpKey;
    gX_I32 fullscreenKey;
    
    gX_I1 cursorSwitched;
    gX_I1 cursorEnabled;
    
    gX_Input_Keyboard keyboard;
    gX_Input_Mouse mouse;
    gX_Input_Gamepad gamepad[8];
    
    gX_U32 bindAmount;
    gX_Input_Bind binds[gX_BIND_AMOUNT];
    gX_I32 gamepadBindMap[gX_BIND_AMOUNT]; // maps bind index to gamepad button (-1=none)
    
    //opencode: virtual input — programmatic overrides for AI and tests
    gX_Input_Virtual virtInput;

    //opencode: settings keyboard-bind capture state
    gX_I1   bindCaptureActive;
    gX_U32  bindCaptureIndex;
    gX_U32  bindCaptureSlot;

    //opencode: let the active GFX editor free camera process movement binds.
    gX_I1   toolCameraInputEnabled;

} gX_Input;
    
extern gX_Input input;

gX_I0 gX_Input_Bind_ClearAll(gX_I0);
gX_I0 gX_Input_Bind_Clear(  gX_U32 index);
gX_I0 gX_Input_Bind_Set(    gX_U32 index, gX_I32 key, gX_U32 keyindex);
gX_I0 gX_Input_Bind_SetPersistent(gX_U32 index, gX_I32 key, gX_U32 keyindex);
gX_I1 gX_Input_Bind_CaptureKey(gX_I32 key);
gX_I0 gX_Input_Bind_SetName(gX_Input_Bind *bind, const gX_I8 *name);
gX_I0 gX_Input_Bind_Process(gX_Input_Bind *bind);

//opencode: gamepad constants — generic directional names so backend (N64/PS/Xbox)
//   maps physical buttons to these. Face buttons use compass directions to avoid
//   platform-specific labels (A/B/X/Y, Cross/Circle, etc.).
#define GX_GAMEPAD_FACE_SOUTH    0  // N64 A,  PS Cross,  Xbox A
#define GX_GAMEPAD_FACE_EAST     1  // N64 B,  PS Circle,  Xbox B
#define GX_GAMEPAD_FACE_WEST     2  // N64 none,PS Square,  Xbox X
#define GX_GAMEPAD_FACE_NORTH    3  // N64 none,PS Triangle,Xbox Y
#define GX_GAMEPAD_START         4
#define GX_GAMEPAD_SELECT        5
#define GX_GAMEPAD_SHOULDER_L    6  // N64 L,  PS L1,      Xbox LB
#define GX_GAMEPAD_SHOULDER_R    7  // N64 R,  PS R1,      Xbox RB
#define GX_GAMEPAD_TRIGGER_L     8  // N64 Z,  PS L2,      Xbox LT
#define GX_GAMEPAD_TRIGGER_R     9  // N64 none,PS R2,      Xbox RT
#define GX_GAMEPAD_DPAD_UP      10
#define GX_GAMEPAD_DPAD_DOWN    11
#define GX_GAMEPAD_DPAD_LEFT    12
#define GX_GAMEPAD_DPAD_RIGHT   13
#define GX_GAMEPAD_STICK_L      14  // left stick press (L3)
#define GX_GAMEPAD_STICK_R      15  // right stick press (R3)
// Gamepad axis mapping (values < 0 to distinguish from button indices)
#define GX_GAMEPAD_AXIS_LX      -16  // left stick X,  -1..1
#define GX_GAMEPAD_AXIS_LY      -17  // left stick Y,  -1..1
#define GX_GAMEPAD_AXIS_RX      -18  // right stick X, -1..1
#define GX_GAMEPAD_AXIS_RY      -19  // right stick Y, -1..1

//opencode: convenience — set all 6 free-camera movement binds (WASD+EQ)
//   and map gamepad analog stick axis 0/1 to movement binds.
gX_I0 gX_Input_Bind_SetFreeCam(gX_I0);

gX_Input_State gX_Input_Get_Bind_State(gX_U32 index);
gX_Input_State gX_Input_Get_Bind_State_Name(const gX_I8 *name);
gX_I0 gX_Input_Respond(gX_I0);

gX_I32 gX_Input_Load(gX_I0);
gX_I32 gX_Input_Unload(gX_I0);

gX_I0 gX_Input_Update(gX_I0);

//opencode: text input char queue — gX_GetCharPressed pops the next queued
//   printable character (0 when empty); gX_Input_ClearCharQueue empties it.
gX_I32 gX_GetCharPressed(gX_I0);
gX_I0  gX_Input_ClearCharQueue(gX_I0);

//opencode: virtual input API — for AI probe scripts, tests, and tooling
gX_I0 gX_Input_Virtual_SetMouse(gX_I32 x, gX_I32 y);
gX_I0 gX_Input_Virtual_Click(gX_I32 x, gX_I32 y);
gX_I0 gX_Input_Virtual_SetKey(gX_I32 keyCode, gX_I1 shift);
gX_I0 gX_Input_Virtual_SetKeyHeld(gX_I32 keyCode, gX_I1 shift, gX_I1 held);
gX_I0 gX_Input_Virtual_Deactivate(gX_I0);
//opencode: GX_AI_INPUT synth readout for the ImGui bridge — latest script
//   mouse (game coords; bridge maps back to window px), held button (0=left,
//   1=right, 2=middle), drag modifiers, and timed keyboard events; returns
//   nonzero while a script runs.
gX_I1 gX_AI_Input_Synth(gX_F32 *x, gX_F32 *y, gX_I1 *down, gX_I32 *button,
    gX_I32 *modifierKey, gX_I32 *modifierAxis, gX_I1 *modifierCtrl,
    gX_I1 *modifierDown, gX_I32 *keyCode, gX_I1 *keyDown,
    gX_I1 *keyShift, gX_I1 *keyCtrl);
    
#ifdef __cplusplus
}
#endif

#endif //MAIN_H
