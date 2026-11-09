/**********************************************************************************************
*
*   [gX: src/input.c]
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


#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "input.h"
#include "gui/gui.h"
#include "gui/binding.h"
#include "gui/console_playback.h"
#include "tools/tools.h"

#include "main.h"
#include "common/enum.h"
#include "common/log.h"

#include "common/profiler.h"

#include "wrappers/input.h"
#include "wrappers/window.h"
#include "audio.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "settings.h"

//opencode: PS2 — DualShock pad input

//ai: common bind names (declared in input.h; the enum coverage gate scans the
//   whole tree now).
const gX_I8 *gX_Binds_Common_String(gX_I32 bind) {
    switch (bind) {
#define X(binding) case gX_BIND_##binding: return #binding;
        gX_BINDS_COMMON
#undef X
        case gX_BIND_COMMON_AMOUNT: return "AMOUNT";
        default: return "undefined";
    }
}

gX_Input input;

gX_I0 gX_Input_Bind_ClearAll(gX_I0) {
    for (gX_U32 i = 0; i < gX_BIND_AMOUNT; i++) {
        gX_Input_Bind_Clear(i);
    }
}

gX_I0 gX_Input_Bind_Clear(gX_U32 index) {
    if (index >= gX_BIND_AMOUNT) return; //opencode: guard OOB
    for (gX_U32 i = 0; i < gX_BIND_KEYS_AMOUNT; i++) {
        input.binds[index].keys[i] = 0;
    }
    input.binds[index].keysAmount = 0;
    input.gamepadBindMap[index] = -1;
}

gX_I0 gX_Input_Bind_Set(gX_U32 index, gX_I32 key, gX_U32 keyindex) {
    if (index >= gX_BIND_AMOUNT || keyindex >= gX_BIND_KEYS_AMOUNT) return; //opencode: guard OOB
    input.binds[index].keys[keyindex] = key;
    if (input.binds[index].keysAmount < gX_BIND_KEYS_AMOUNT) input.binds[index].keysAmount++;
}

//opencode: apply persisted keyboard slots after scene bind setup.
static gX_I0 gX_Input_Bind_ApplyPersistent(gX_U32 index) {
    if (index >= gX_BIND_COMMON_AMOUNT || input.binds[index].keysAmount == 0) return;
    gX_U32 amount = 0;
    for (gX_U32 i = 0; i < 2; i++) {
        gX_I32 key = settings.bindKeys[index][i];
        if (key < 0 || key >= gX_INPUT_KEYBOARD_KEYS) continue;
        input.binds[index].keys[amount] = key;
        amount++;
    }
    for (gX_U32 i = amount; i < gX_BIND_KEYS_AMOUNT; i++) input.binds[index].keys[i] = 0;
    input.binds[index].keysAmount = amount;
}

//opencode: public remap API updates settings and the active bind immediately.
gX_I0 gX_Input_Bind_SetPersistent(gX_U32 index, gX_I32 key, gX_U32 keyindex) {
    if (index >= gX_BIND_COMMON_AMOUNT || keyindex >= 2 || key < -1 || key >= gX_INPUT_KEYBOARD_KEYS) return;
    settings.bindKeys[index][keyindex] = key;
    settings.dirtyFlag = true;
    gX_Input_Bind_ApplyPersistent(index);
}

//opencode: commit one key from the settings capture state.
gX_I1 gX_Input_Bind_CaptureKey(gX_I32 key) {
    if (!input.bindCaptureActive || input.bindCaptureIndex >= gX_BIND_COMMON_AMOUNT
        || input.bindCaptureSlot >= 2 || key < 0 || key >= gX_INPUT_KEYBOARD_KEYS) return false;
    gX_Input_Bind_SetPersistent(input.bindCaptureIndex, key, input.bindCaptureSlot);
    input.bindCaptureActive = false;
    gX_GUI_Settings_Refresh();
    return true;
}

//ai: digital-only pads (e.g. a PS1 controller without analog sticks) still
//   need free-camera movement; mirror the D-pad onto the movement axes.
static gX_I32 gX_Input_MovementDpad(gX_I32 bindIndex) {
    switch (bindIndex) {
        case gX_BIND_MOVE_FORWARD:  return gX_GAMEPAD_BUTTON_LEFT_FACE_UP;
        case gX_BIND_MOVE_BACKWARD: return gX_GAMEPAD_BUTTON_LEFT_FACE_DOWN;
        case gX_BIND_MOVE_LEFT:     return gX_GAMEPAD_BUTTON_LEFT_FACE_LEFT;
        case gX_BIND_MOVE_RIGHT:    return gX_GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
        default: return -1;
    }
}

//ai: digital pads such as a PS1 USB adapter expose the D-pad as the left-stick
//   axes. Synthesize the four raylib D-pad button states with hysteresis so
//   menu focus, pause navigation and the free-camera fallback see a D-pad.
//   Pads that rest off-centre (wheels) never enable it; the env kill switch
//   covers centred sticks that must stay off the menu.
#define GX_GAMEPAD_DPAD_AXIS_PRESS   0.75f
#define GX_GAMEPAD_DPAD_AXIS_RELEASE 0.50f

static gX_I1 gX_Input_Gamepad_DpadAxesEnabled(gX_I0) {
    const gX_I8 *hook = getenv("GX_GAMEPAD_DPAD_AXES");
    return !(hook && (strcmp((const char *)hook, "0") == 0
        || strcmp((const char *)hook, "off") == 0));
}

gX_I0 gX_Input_Gamepad_SynthesizeDpad(gX_Input_Gamepad *pad) {
    if (!pad) return;
    gX_I1 axisDown[4] = {0};
    if (gX_Input_Gamepad_DpadAxesEnabled() && pad->axisCount >= 2) {
        gX_F32 lx = pad->axisMovement[0];
        gX_F32 ly = pad->axisMovement[1];
        if (!pad->dpadAxisReady
            && fabsf(lx) <= 0.25f && fabsf(ly) <= 0.25f)
            pad->dpadAxisReady = 1;
        if (pad->dpadAxisReady) {
            axisDown[0] = pad->dpadAxisDown[0]
                ? ly < -GX_GAMEPAD_DPAD_AXIS_RELEASE
                : ly < -GX_GAMEPAD_DPAD_AXIS_PRESS;
            axisDown[1] = pad->dpadAxisDown[1]
                ? lx > GX_GAMEPAD_DPAD_AXIS_RELEASE
                : lx > GX_GAMEPAD_DPAD_AXIS_PRESS;
            axisDown[2] = pad->dpadAxisDown[2]
                ? ly > GX_GAMEPAD_DPAD_AXIS_RELEASE
                : ly > GX_GAMEPAD_DPAD_AXIS_PRESS;
            axisDown[3] = pad->dpadAxisDown[3]
                ? lx < -GX_GAMEPAD_DPAD_AXIS_RELEASE
                : lx < -GX_GAMEPAD_DPAD_AXIS_PRESS;
        }
    }
    for (gX_I32 d = 0; d < 4; d++) {
        gX_Input_State *btn =
            &pad->buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_UP + d];
        gX_I1 down = btn->DOWN || axisDown[d];
        btn->PRESSED = down && !pad->dpadPrevDown[d];
        btn->RELEASED = !down && pad->dpadPrevDown[d];
        btn->DOWN = down;
        btn->UP = !down;
        pad->dpadPrevDown[d] = down;
        pad->dpadAxisDown[d] = axisDown[d];
    }
}

gX_I0 gX_Input_Bind_Process(gX_Input_Bind *bind) {
    if (!bind) return;
    bind->state = (gX_Input_State){0};
    for (gX_U32 i = 0; i < bind->keysAmount; i++) {
        gX_I32 keyindex = bind->keys[i];
        //opencode: guard against out-of-range key index (negative or >511)
        if (keyindex < 0 || keyindex >= 512) continue;
        bind->state.DOWN |= input.keyboard.keys[keyindex].state.DOWN;
        bind->state.PRESSED |= input.keyboard.keys[keyindex].state.PRESSED;
        bind->state.RELEASED |= input.keyboard.keys[keyindex].state.RELEASED;
    }
    gX_I32 bindIndex = -1;
    for (gX_I32 i = 0; i < gX_BIND_AMOUNT; i++) {
        if (&input.binds[i] == bind) {
            bindIndex = i;
            break;
        }
    }
    if (bindIndex >= 0) {
        gX_I32 mapping = input.gamepadBindMap[bindIndex];
        for (gX_I32 padIndex = 0; padIndex < 8; padIndex++) {
            gX_Input_Gamepad *pad = &input.gamepad[padIndex];
            if (!pad->available) continue;
            if (mapping >= 0 && mapping < 64) {
                bind->state.DOWN |= pad->buttons[mapping].DOWN;
                bind->state.PRESSED |= pad->buttons[mapping].PRESSED;
                bind->state.RELEASED |= pad->buttons[mapping].RELEASED;
            } else if (mapping <= GX_GAMEPAD_AXIS_LX && mapping >= GX_GAMEPAD_AXIS_RY) {
                gX_I32 axis = -(mapping + 16);
                if (axis < pad->axisCount && axis < 8) {
                    gX_F32 value = pad->axisMovement[axis];
                    gX_I1 axisDown = value < -0.25f || value > 0.25f;
                    if (mapping == GX_GAMEPAD_AXIS_LX && bindIndex == gX_BIND_MOVE_LEFT) axisDown = value < -0.25f;
                    if (mapping == GX_GAMEPAD_AXIS_LX && bindIndex == gX_BIND_MOVE_RIGHT) axisDown = value > 0.25f;
                    if (mapping == GX_GAMEPAD_AXIS_LY && bindIndex == gX_BIND_MOVE_FORWARD) axisDown = value < -0.25f;
                    if (mapping == GX_GAMEPAD_AXIS_LY && bindIndex == gX_BIND_MOVE_BACKWARD) axisDown = value > 0.25f;
                    bind->state.DOWN |= axisDown;
                }
            }
            gX_I32 dpad = gX_Input_MovementDpad(bindIndex);
            if (dpad >= 0) {
                bind->state.DOWN |= pad->buttons[dpad].DOWN;
                bind->state.PRESSED |= pad->buttons[dpad].PRESSED;
                bind->state.RELEASED |= pad->buttons[dpad].RELEASED;
            }
        }
    }
    bind->state.UP = !bind->state.DOWN;
}

//opencode: implemented stubs for declared-but-missing functions (BUG 10/11)
gX_Input_State gX_Input_Get_Bind_State(gX_U32 index) {
    if (index >= gX_BIND_AMOUNT) return (gX_Input_State){0};
    return input.binds[index].state;
}
gX_Input_State gX_Input_Get_Bind_State_Name(const gX_I8 *name) {
    if (!name) return (gX_Input_State){0};
    for (gX_U32 i = 0; i < gX_BIND_AMOUNT; i++) {
        if (strcmp(input.binds[i].name, name) == 0) return input.binds[i].state;
    }
    return (gX_Input_State){0};
}
gX_I0 gX_Input_Bind_SetName(gX_Input_Bind *bind, const gX_I8 *name) {
    if (!bind || !name) return;
    strncpy(bind->name, name, sizeof(bind->name) - 1);
    bind->name[sizeof(bind->name) - 1] = '\0';
}

//opencode: set free-camera binds (WASD+EQ) with gamepad mirror
gX_I0 gX_Input_Bind_SetFreeCam(gX_I0) {
    gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,  0);
    gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,  0);
    gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,  0);
    gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,  0);
    gX_Input_Bind_Set(gX_BIND_MOVE_UP,      gX_KEY_E,  0);
    gX_Input_Bind_Set(gX_BIND_MOVE_DOWN,    gX_KEY_Q,  0);
    // Gamepad mirror: left stick → movement, shoulders → up/down
    input.gamepadBindMap[gX_BIND_MOVE_FORWARD]  = GX_GAMEPAD_AXIS_LY;
    input.gamepadBindMap[gX_BIND_MOVE_BACKWARD] = GX_GAMEPAD_AXIS_LY;
    input.gamepadBindMap[gX_BIND_MOVE_LEFT]     = GX_GAMEPAD_AXIS_LX;
    input.gamepadBindMap[gX_BIND_MOVE_RIGHT]    = GX_GAMEPAD_AXIS_LX;
    input.gamepadBindMap[gX_BIND_MOVE_UP]       = GX_GAMEPAD_SHOULDER_L;
    input.gamepadBindMap[gX_BIND_MOVE_DOWN]     = GX_GAMEPAD_SHOULDER_R;
}

gX_I32 gX_Input_Load(gX_I0) {
    gX_TRACELOG_PROLOGUE("Input_Load","INPUT: Loading input")
    
    input = (gX_Input){0};
    
    strcpy(input.name,"input");
    strcpy(input.description,"gets inputs");
    gX_TraceLog_INPUT(gX_LOG_GREEN,"name: %s",input.name);
    gX_TraceLog_INPUT(gX_LOG_GREEN,"desc: %s",input.description);
    
    input.masterIndexSwitchResponse = false;
    
    input.exitKey = gX_KEY_F4;
    input.menuKey = gX_KEY_ESCAPE;
    input.screenshotKey = gX_KEY_F12;
    input.toolbarKey = gX_KEY_F9;
    input.consoleKey = gX_KEY_F10;
    input.helpKey = gX_KEY_F1;
    input.fullscreenKey = gX_KEY_F11;
    
    input.cursorSwitched = true;
    input.cursorEnabled = true;
    
    input.bindAmount = gX_BIND_COMMON_AMOUNT;
    gX_Input_Bind_ClearAll();
    
    input.loaded = true;
    
    gX_TRACELOG_EPILOGUE("INPUT: Loading done")
    return 0;
}

gX_I32 gX_Input_Unload(gX_I0) {
    gX_TRACELOG_PROLOGUE("Input_Unload","INPUT: Unloading input")

    //opencode: restore the OS cursor before the backend window is destroyed.
    if (input.loaded && maindata.backendLoaded) gX_Cursor_Enable();
    
    input.loaded = false;
    
    gX_TRACELOG_EPILOGUE("INPUT: Unloading done")
    return 0;
}

//opencode: automated captures and virtual-input probes must not lock the user's cursor.
#define GX_CURSOR_STACK_MAX 8
#define GX_CURSOR_OWNER_MAX 32

typedef struct gX_CursorEntry {
    gX_CursorState state;
    gX_I8 owner[GX_CURSOR_OWNER_MAX];
    gX_I32 priority;
} gX_CursorEntry;

static gX_CursorEntry s_cursorStack[GX_CURSOR_STACK_MAX];
static gX_I32 s_cursorDepth = 0;
static gX_CursorState s_cursorDefault = GX_CURSOR_STATE_UI;
static gX_CursorState s_cursorLastState = GX_CURSOR_STATE_UI;
static gX_I8 s_cursorLastOwner[GX_CURSOR_OWNER_MAX] = "scene";
static gX_I1 s_cursorHasLast = 0;

static const gX_I8 *cursor_state_label(gX_CursorState state) {
    return state == GX_CURSOR_STATE_CAPTURED ? "CAPTURED" : "UI";
}

static gX_I32 cursor_best_index(gX_I0) {
    gX_I32 best = -1;
    gX_I32 bestPriority = -1;
    for (gX_I32 i = 0; i < s_cursorDepth; i++) {
        if (s_cursorStack[i].priority >= bestPriority) {
            best = i;
            bestPriority = s_cursorStack[i].priority;
        }
    }
    return best;
}

static gX_I0 gX_Cursor_Apply(gX_I0) {
    gX_I32 best = cursor_best_index();
    gX_CursorState state = best >= 0
        ? s_cursorStack[best].state : s_cursorDefault;
    const gX_I8 *owner = best >= 0
        ? s_cursorStack[best].owner : "scene";
    gX_I1 changed = !s_cursorHasLast || state != s_cursorLastState
        || strcmp((const char *)owner, (const char *)s_cursorLastOwner) != 0;
    s_cursorHasLast = 1;
    s_cursorLastState = state;
    snprintf((char *)s_cursorLastOwner, sizeof(s_cursorLastOwner), "%s",
        (const char *)owner);
    input.cursorEnabled = state == GX_CURSOR_STATE_UI ? 1 : 0;
    input.cursorSwitched = 1;
    if (!changed) return;
    const gX_I8 *trace = getenv("GX_CURSOR_TRACE");
    if (trace && trace[0] && trace[0] != '0') {
        gX_TraceLog(gX_LOG_INFO, "CURSOR: state=%s owner=%s depth=%d",
            (const char *)cursor_state_label(state), (const char *)owner,
            s_cursorDepth);
    }
}

gX_I0 gX_Cursor_SetDefault(gX_CursorState state) {
    s_cursorDefault = state == GX_CURSOR_STATE_CAPTURED
        ? GX_CURSOR_STATE_CAPTURED : GX_CURSOR_STATE_UI;
    gX_Cursor_Apply();
}

gX_I0 gX_Cursor_Reset(gX_CursorState state) {
    s_cursorDepth = 0;
    gX_Cursor_SetDefault(state);
}

gX_I0 gX_Cursor_Push(gX_CursorState state, const gX_I8 *owner, gX_I32 priority) {
    if (!owner || !owner[0]) owner = "?";
    state = state == GX_CURSOR_STATE_CAPTURED
        ? GX_CURSOR_STATE_CAPTURED : GX_CURSOR_STATE_UI;
    for (gX_I32 i = s_cursorDepth - 1; i >= 0; i--) {
        if (strcmp((const char *)s_cursorStack[i].owner,
                (const char *)owner) != 0) continue;
        if (s_cursorStack[i].state == state
            && s_cursorStack[i].priority == priority)
            return;
        for (gX_I32 j = i; j < s_cursorDepth - 1; j++)
            s_cursorStack[j] = s_cursorStack[j + 1];
        s_cursorDepth--;
        break;
    }
    if (s_cursorDepth >= GX_CURSOR_STACK_MAX) s_cursorDepth = GX_CURSOR_STACK_MAX - 1;
    s_cursorStack[s_cursorDepth].state = state;
    s_cursorStack[s_cursorDepth].priority = priority;
    snprintf((char *)s_cursorStack[s_cursorDepth].owner,
        sizeof(s_cursorStack[s_cursorDepth].owner), "%s", (const char *)owner);
    s_cursorDepth++;
    gX_Cursor_Apply();
}

gX_I0 gX_Cursor_Pop(const gX_I8 *owner) {
    if (!owner || !owner[0]) return;
    for (gX_I32 i = 0; i < s_cursorDepth; i++) {
        if (strcmp((const char *)s_cursorStack[i].owner,
                (const char *)owner) != 0) continue;
        for (gX_I32 j = i; j < s_cursorDepth - 1; j++)
            s_cursorStack[j] = s_cursorStack[j + 1];
        s_cursorDepth--;
        gX_Cursor_Apply();
        return;
    }
}

gX_CursorState gX_Cursor_Get(gX_I0) {
    gX_I32 best = cursor_best_index();
    return best >= 0 ? s_cursorStack[best].state : s_cursorDefault;
}

const gX_I8 *gX_Cursor_Owner(gX_I0) {
    gX_I32 best = cursor_best_index();
    return best >= 0 ? s_cursorStack[best].owner : "scene";
}

gX_I0 gX_Cursor_Toggle(gX_I0) {
    if (gX_Cursor_Get() == GX_CURSOR_STATE_UI)
        gX_Cursor_Push(GX_CURSOR_STATE_CAPTURED, "manual", 40);
    else
        gX_Cursor_Push(GX_CURSOR_STATE_UI, "manual", 40);
}
static gX_I1 gX_Input_IsAutomated(gX_I0) {
    const gX_I8 *script = getenv("GX_AI_INPUT");
    const gX_I8 *frames = getenv("GX_FRAMES");
    return arguments.aiScreenshotFinal || arguments.hasAiScreenshotAfter
        || (script && script[0]) || (frames && frames[0]);
}

gX_I0 gX_Input_Respond(gX_I0) {
    gX_TraceLog_INPUT(gX_LOG_DEBUG,"Responding");
    input.masterIndexSwitchResponse = false;
    gX_Input_Bind_ClearAll();
    switch (maindata.masterIndex) {
        case gX_SCENE_TITLE: {
            input.cursorEnabled = true;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,     0);
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_UP,    1);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,     0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_DOWN,  1);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,     0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_LEFT,  1);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,     0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_RIGHT, 1);
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_SYNTHVIS: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_BATTLE: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_UP,      gX_KEY_E,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_DOWN,    gX_KEY_Q,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_MAP: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD,             gX_KEY_W,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,            gX_KEY_S,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,                gX_KEY_A,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,               gX_KEY_D,  0);
            gX_Input_Bind_Set(gX_BIND_ROTATE_CLOCKWISE,         gX_KEY_E,  0);
            gX_Input_Bind_Set(gX_BIND_ROTATE_COUNTERCLOCKWISE,  gX_KEY_Q,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_BACKGROUND: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_RAYMARCH: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_UP,      gX_KEY_E,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_DOWN,    gX_KEY_Q,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_GFXDEBUG:
        case gX_SCENE_RENDERER_GFX: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_UP,      gX_KEY_E,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_DOWN,    gX_KEY_Q,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_GFX_DEMO: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_RENDERER_PP: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            input.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: wireframe scene with free camera
        case gX_SCENE_RENDERER_WIREFRAME: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_SetFreeCam();
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_WORLD: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD,  gX_KEY_W, 0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD, gX_KEY_S, 0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,     gX_KEY_A, 0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,    gX_KEY_D, 0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        //ai: module-owned scene — free-camera movement plus arrow-key turn;
        //   the module reads the bind states, not raw backend input.
        case gX_SCENE_MODULE: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_SetFreeCam();
            gX_Input_Bind_Set(gX_BIND_ROTATE_CLOCKWISE,        gX_KEY_RIGHT, 0);
            gX_Input_Bind_Set(gX_BIND_ROTATE_COUNTERCLOCKWISE, gX_KEY_LEFT,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: allow renderer-only scenes to respond
        case gX_SCENE_RENDERER_TEXTWRITER:
        case gX_SCENE_RENDERER_FRACTAL:
        case gX_SCENE_RENDERER_GLOBE:
        case gX_SCENE_RENDERER_ANALOG:
        case gX_SCENE_RENDERER_CLOCK:
        case gX_SCENE_RENDERER_GENERATE:
        case gX_SCENE_RENDERER_SKY:
        case gX_SCENE_RENDERER_TELETEXT:
        case gX_SCENE_RENDERER_VIDEO:
        case gX_SCENE_RENDERER_DEMOSCRIPT: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            input.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: water standalone — free camera via helper, also mirrors to gamepad
        case gX_SCENE_RENDERER_WATER: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_SetFreeCam();
            input.masterIndexSwitchResponse = true;
            break;
        }
        case gX_SCENE_ROOMS: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_Set(gX_BIND_MOVE_FORWARD, gX_KEY_W,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_BACKWARD,gX_KEY_S,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_LEFT,    gX_KEY_A,  0);
            gX_Input_Bind_Set(gX_BIND_MOVE_RIGHT,   gX_KEY_D,  0);
            input.masterIndexSwitchResponse = true;
            break;
        }
        //opencode: physics scene with free camera + reset
        case gX_SCENE_PHYSICS: {
            input.cursorEnabled = false;
            input.cursorSwitched = true;
            gX_Input_Bind_SetFreeCam();
            input.masterIndexSwitchResponse = true;
            break;
        }
        default: {
            input.masterIndexSwitchResponse = true; //opencode: respond even to unknown scenes
            break;
        }
    }
    for (gX_U32 i = 0; i < input.bindAmount && i < gX_BIND_COMMON_AMOUNT; i++) {
        gX_Input_Bind_ApplyPersistent(i);
    }
    //opencode: scene default drives the cursor state machine; scene switches
    //   drop stale tool/freecam owners. Automated runs still keep the OS
    //   cursor visible through the audit-window guard below.
    gX_Cursor_Reset(input.cursorEnabled ? GX_CURSOR_STATE_UI
        : GX_CURSOR_STATE_CAPTURED);
    if (gX_Input_IsAutomated()) {
        input.cursorEnabled = true;
        input.cursorSwitched = true;
    }
}

static gX_I0 gX_Input_Key_Process(gX_Input_Key *key, gX_I32 index) {
    if (gX_Key_IsDown(index)) {
        input.keyboard.keys[index].state.DOWN = true;
        gX_F32 incrementation = 1.0f/(gX_F32)maindata.targetFPS;
        input.keyboard.keys[index].counter += incrementation;
        input.keyboard.keys[index].counterIntegral += incrementation;
    }
    else {
        input.keyboard.keys[index].state.DOWN = false;
        input.keyboard.keys[index].counter = 0.0f;
    }
    if (gX_Key_IsUp(index))   input.keyboard.keys[index].state.UP = true;
    else                      input.keyboard.keys[index].state.UP = false;
    if (gX_Key_IsPressed(index))  {
        input.keyboard.keys[index].state.PRESSED = true;
        ++input.keyboard.keys[index].pressCounter;
        input.keyboard.keyHistory[input.keyboard.keyHistoryIndexPtr] = (gX_I8)index;
        input.keyboard.keyHistoryIndexPtr++;
        input.keyboard.keyHistoryIndexPtr %= 16;
    }
    else {
        input.keyboard.keys[index].state.PRESSED = false;
    }
    if (gX_Key_IsReleased(index)) input.keyboard.keys[index].state.RELEASED = true;
    else                          input.keyboard.keys[index].state.RELEASED = false;
}

static gX_I0 gX_Input_Update_Keyboard(gX_I0) {
    for (gX_I32 i = 32; i <= 96; i++) {
        gX_Input_Key_Process(&input.keyboard.keys[i], i);
    }
    for (gX_I32 i = 256; i <= 348; i++) {
        gX_Input_Key_Process(&input.keyboard.keys[i], i);
    }
}

//opencode: map a printable keycode (32..96) to its character, applying shift.
//   US-layout symbols. Used by gX_GetCharPressed for the textinput widget.
static gX_I8 gX_Input_Key_ToChar(gX_I32 key, gX_I1 shift) {
    // letters: a-z / A-Z
    if (key >= 'A' && key <= 'Z') return shift ? (gX_I8)key : (gX_I8)(key + 32);
    // digits: 0-9 → symbols with shift
    static const gX_I8 shiftDigits[] = ")!@#$%^&*(";
    if (key >= '0' && key <= '9') {
        if (shift) return shiftDigits[key - '0'];
        return (gX_I8)key;
    }
    // punctuation (US layout)
    switch (key) {
        case ' ': return ' ';
        case 39: return shift ? '"'  : '\'';
        case 44: return shift ? '<'  : ',';
        case 45: return shift ? '_'  : '-';
        case 46: return shift ? '>'  : '.';
        case 47: return shift ? '?'  : '/';
        case 59: return shift ? ':'  : ';';
        case 61: return shift ? '+'  : '=';
        case 91: return shift ? '{'  : '[';
        case 92: return shift ? '|'  : '\\';
        case 93: return shift ? '}'  : ']';
        case 96: return shift ? '~'  : '`';
        default: return (gX_I8)key;
    }
}

//opencode: queue characters for the textinput widget — scans printable keys
//   (32..96) that were pressed this frame and enqueues their char with shift
//   applied. Runs after key processing AND after the virtual input override so
//   AI/script key presses (which set keys[kc].state.PRESSED) are captured too.
static gX_I0 gX_Input_Update_CharQueue(gX_I0) {
    gX_I1 shift = input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN
               || input.keyboard.keys[gX_KEY_RIGHT_SHIFT].state.DOWN;
    for (gX_I32 i = 32; i <= 96; i++) {
        if (input.keyboard.keys[i].state.PRESSED) {
            if (input.keyboard.charQueueCount >= 64) break;
            input.keyboard.charQueue[input.keyboard.charQueueTail] = gX_Input_Key_ToChar(i, shift);
            input.keyboard.charQueueTail = (input.keyboard.charQueueTail + 1) % 64;
            input.keyboard.charQueueCount++;
        }
    }
}

//opencode: consume the first newly pressed keyboard key while the settings UI
//   is waiting for a replacement bind.
static gX_I0 gX_Input_CaptureBind(gX_I0) {
    if (!input.bindCaptureActive) return;
    for (gX_I32 i = 32; i <= 96; i++) {
        if (input.keyboard.keys[i].state.PRESSED) {
            gX_Input_Bind_CaptureKey(i);
            input.keyboard.keys[i].state.PRESSED = false;
            input.keyboard.keys[i].state.DOWN = false;
            return;
        }
    }
    for (gX_I32 i = 256; i <= 348; i++) {
        if (input.keyboard.keys[i].state.PRESSED) {
            gX_Input_Bind_CaptureKey(i);
            input.keyboard.keys[i].state.PRESSED = false;
            input.keyboard.keys[i].state.DOWN = false;
            return;
        }
    }
}

gX_I32 gX_GetCharPressed(gX_I0) {
    if (input.keyboard.charQueueCount == 0) return 0;
    gX_I8 c = input.keyboard.charQueue[input.keyboard.charQueueHead];
    input.keyboard.charQueueHead = (input.keyboard.charQueueHead + 1) % 64;
    input.keyboard.charQueueCount--;
    return (gX_I32)c;
}

gX_I0 gX_Input_ClearCharQueue(gX_I0) {
    input.keyboard.charQueueCount = 0;
    input.keyboard.charQueueHead = 0;
    input.keyboard.charQueueTail = 0;
}

static gX_I0 gX_AI_Input_Apply(gX_I0);

static gX_I0 gX_Input_Update_Mouse(gX_I0) {
    input.mouse.coordPrev = input.mouse.coord;
    //opencode: engine mouse owns game coords — raw window px mapped once via
    //   the live viewport (replaces the window-ratio scale, which could not
    //   represent pillarbox offsets and double-mapped against ScreenToGame).
    {
        gX_F32v2 rawPos = gX_Mouse_GetPos();
        input.mouse.coord = gX_GUI_ScreenToGame(rawPos.x, rawPos.y);
    }
    //opencode: the FIRST GetMouseDelta returns the cursor's absolute position
    //   (the backend's previous position starts at 0), so the first frame
    //   can report a multi-hundred-pixel jump. A free camera pitched ~85°
    //   down at world spawn from this. Discard the first delta.
    {
        static gX_I1 firstMouse = 1;
        input.mouse.coordDelta = firstMouse ? (gX_F32v2){0, 0} : gX_Mouse_GetDelta();
        firstMouse = 0;
    }
    
    input.mouse.leftButton.DOWN = gX_Mouse_IsDown(gX_MOUSE_BUTTON_LEFT);
    input.mouse.leftButton.PRESSED = gX_Mouse_IsPressed(gX_MOUSE_BUTTON_LEFT);
    
    input.mouse.rightButton.DOWN = gX_Mouse_IsDown(gX_MOUSE_BUTTON_RIGHT);
    input.mouse.rightButton.PRESSED = gX_Mouse_IsPressed(gX_MOUSE_BUTTON_RIGHT);
    
    input.mouse.middleButton.DOWN = gX_Mouse_IsDown(gX_MOUSE_BUTTON_MIDDLE);
    input.mouse.middleButton.PRESSED = gX_Mouse_IsPressed(gX_MOUSE_BUTTON_MIDDLE);
    
    input.mouse.scroll = gX_Mouse_GetWheelMove();
    input.mouse.scrollIntegral += gX_Mouse_GetWheelMove();
    
    //opencode: handle stuck PRESSED from window focus loss — PRESSED should only be one frame.
    //If it persists for >1 frame when DOWN is also true, force it off. The release event was missed.
    static gX_I1 prevPressed = false;
    if (input.mouse.leftButton.PRESSED && prevPressed && input.mouse.leftButton.DOWN) {
        input.mouse.leftButton.PRESSED = false;
    }
    prevPressed = input.mouse.leftButton.PRESSED;

    //opencode: virtual input override LAST so it beats the real cursor
    gX_AI_Input_Apply();
}


//opencode: v0.2.0-pre9 — GX_AI_INPUT: timed virtual input for headless
//   testing, layered on the virtInput override below. Never touches the
//   real cursor; synthesizes mouse state after the OS poll.
//ai: timed virtual gamepad actions support deterministic gX-240 controller gates.
//   "1.0 click 180 354; 3.0 drag 500 980 640 980 300 CTRL+RX; 5.0 move 110 194 2000; 6.0 key 74; 7.0 pad 0 start"
//   click/drag hold the button; move parks button-up (pre-position). Drag
//   chords are posted through the existing ImGui bridge for editor gestures.
typedef struct {
    gX_F32 t0;
    gX_I32 isDrag;
    gX_I1 isKey;
    gX_I32 keyCode;
    gX_I1 keyCtrl;
    gX_I32 modifierKey;
    gX_I32 modifierAxis;
    //opencode: Ctrl remains a distinct gesture modifier for transform snapping.
    gX_I1 modifierCtrl;
    gX_I1 keyShift;
    gX_I1 isPad;
    gX_I32 padIndex;
    gX_I32 padButton;
    //opencode: synthesized button for camera gestures: 0=left, 1=right, 2=middle.
    gX_I32 button;
    gX_F32 x1, y1, x2, y2;
    gX_F32 dur;
} gX_AI_Action;

static struct {
    gX_I1 parsed;
    gX_F32 clock;
    gX_AI_Action actions[16];
    gX_I32 count;
    gX_I32 activeIdx;
    gX_F64 t0Wall;
    //opencode: ImGui bridge readout — latest synth pos/button (game coords).
    gX_F32 lastX, lastY;
    gX_I1 lastDown;
    gX_I32 lastButton;
    gX_I32 lastKeyCode;
    gX_I1 lastKeyDown;
    gX_I1 lastKeyShift;
    gX_I1 lastKeyCtrl;
    //opencode: park — while a script owns the cursor, idle frames hold the
    //   last script pos (else hover/focus flap back to the real cursor
    //   between actions and multi-press gestures never compose).
    gX_F32 parkX, parkY;
    gX_I1 hasPark;
} g_aiInput = {.activeIdx = -1, .t0Wall = -1.0};

//ai: the script uses semantic face and D-pad names while the runtime stores raylib button slots.
static gX_I32 gX_AI_Input_GamepadButton(const gX_I8 *name)
{
    if (strcmp((const char *)name, "south") == 0)
        return gX_GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
    if (strcmp((const char *)name, "east") == 0)
        return gX_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
    if (strcmp((const char *)name, "west") == 0)
        return gX_GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
    if (strcmp((const char *)name, "north") == 0)
        return gX_GAMEPAD_BUTTON_RIGHT_FACE_UP;
    if (strcmp((const char *)name, "start") == 0)
        return gX_GAMEPAD_BUTTON_MIDDLE_RIGHT;
    if (strcmp((const char *)name, "select") == 0)
        return gX_GAMEPAD_BUTTON_MIDDLE_LEFT;
    if (strcmp((const char *)name, "dpad-up") == 0)
        return gX_GAMEPAD_BUTTON_LEFT_FACE_UP;
    if (strcmp((const char *)name, "dpad-right") == 0)
        return gX_GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    if (strcmp((const char *)name, "dpad-down") == 0)
        return gX_GAMEPAD_BUTTON_LEFT_FACE_DOWN;
    if (strcmp((const char *)name, "dpad-left") == 0)
        return gX_GAMEPAD_BUTTON_LEFT_FACE_LEFT;
    return -1;
}

static gX_I0 gX_AI_Input_Parse(const gX_I8 *src) {
    gX_I8 buf[512];
    strncpy(buf, src, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    gX_I8 *save = NULL;
    for (gX_I8 *tok = strtok_r(buf, ";", &save);
         tok && g_aiInput.count < 16;
         tok = strtok_r(NULL, ";", &save)) {
        gX_AI_Action *a = &g_aiInput.actions[g_aiInput.count];
        memset(a, 0, sizeof(*a));
        a->dur = 0.15f;
        a->modifierKey = 0;
        a->modifierAxis = 0;
        a->modifierCtrl = 0;
        a->button = 0;
        float t;
        char kind[16];
        if (sscanf(tok, "%f %15s", &t, kind) != 2) continue;
        a->t0 = t;
        if (strcmp(kind, "click") == 0) {
            char tail[16] = {0};
            int fields = sscanf(tok, "%*f %*s %f %f %15s", &a->x1, &a->y1, tail);
            if (fields < 2) continue;
            if (fields >= 3) {
                if (strcmp(tail, "RMB") == 0) a->button = 1;
                else if (strcmp(tail, "MMB") == 0) a->button = 2;
                else if (strcmp(tail, "LMB") != 0) continue;
            }
        } else if (strcmp(kind, "key") == 0) {
            int shift = 0;
            int ctrl = 0;
            a->isKey = true;
            if (sscanf(tok, "%*f %*s %d %d %d", &a->keyCode, &shift, &ctrl) < 1) continue;
            a->keyShift = shift != 0;
            a->keyCtrl = ctrl != 0;
        } else if (strcmp(kind, "pad") == 0) {
            gX_I8 buttonName[16] = {0};
            gX_F32 durationMs = 150.0f;
            gX_I32 fields = sscanf(tok, "%*f %*s %d %15s %f",
                &a->padIndex, buttonName, &durationMs);
            if (fields < 2 || a->padIndex < 0 || a->padIndex >= 8) continue;
            a->padButton = gX_AI_Input_GamepadButton(buttonName);
            if (a->padButton < 0 || a->padButton > gX_GAMEPAD_BUTTON_RIGHT_THUMB)
                continue;
            if (fields >= 3) {
                if (durationMs <= 0.0f || durationMs > 60000.0f) continue;
                a->dur = durationMs / 1000.0f;
            }
            a->isPad = true;
        } else if (strcmp(kind, "move") == 0) {
            //opencode: park the cursor button-up (pre-position before a
            //   press; clicks route by last-frame hover, so a teleport +
            //   same-frame press would route to the old spot).
            a->isDrag = 2;
            float ms = 1000.0f;
            if (sscanf(tok, "%*f %*s %f %f %f", &a->x1, &a->y1, &ms) < 2) continue;
            a->x2 = a->x1;
            a->y2 = a->y1;
            a->dur = ms / 1000.0f;
        } else if (strcmp(kind, "drag") == 0) {
            a->isDrag = 1;
            float ms = 250.0f;
            char tail[16] = {0};
            if (sscanf(tok, "%*f %*s %f %f %f %f %f %15s",
                       &a->x1, &a->y1, &a->x2, &a->y2, &ms, tail) < 4) continue;
            a->dur = ms / 1000.0f;
            //opencode: a trailing LMB/RMB/MMB selects the synthesized button;
            //   otherwise the token is a transform chord.
            if (strcmp(tail, "RMB") == 0) {
                a->button = 1;
            } else if (strcmp(tail, "MMB") == 0) {
                a->button = 2;
            } else if (strcmp(tail, "LMB") == 0) {
                a->button = 0;
            } else {
                gX_I8 *chordKeys = tail;
                if (strncmp(tail, "CTRL+", 5) == 0) {
                    a->modifierCtrl = 1;
                    chordKeys += 5;
                }
                if (chordKeys[0] == 'R' || chordKeys[0] == 'S' || chordKeys[0] == 'V') {
                    a->modifierKey = chordKeys[0];
                    if ((chordKeys[1] == 'X' || chordKeys[1] == 'Y' || chordKeys[1] == 'Z')
                        && !chordKeys[2])
                        a->modifierAxis = chordKeys[1];
                    else if (chordKeys[1]) continue;
                } else if (chordKeys[0] == 'X' || chordKeys[0] == 'Y' || chordKeys[0] == 'Z') {
                    if (chordKeys[1]) continue;
                    a->modifierKey = chordKeys[0];
                } else if (chordKeys[0]) continue;
                else if (a->modifierCtrl) continue;
            }
        } else continue;
        g_aiInput.count++;
    }
    g_aiInput.parsed = 1;
}

static gX_I0 gX_AI_Input_Apply(gX_I0) {
    const gX_I8 *script = getenv("GX_AI_INPUT");
    if (!script || !script[0]) return;
    if (!g_aiInput.parsed) gX_AI_Input_Parse(script);

    //opencode: GetFrameTime() reads 0 during the input poll (pre-timing
    //   mark), so the script clock advances explicitly: with GX_FIXED_DT it
    //   steps one fixed delta per frame (click frames become independent of
    //   render speed — hidden audit windows used to shift wall-clock
    //   schedules); without the hook the original monotonic wall clock runs.
    {
        gX_F64 step = 0.0;
        const gX_I8 *fixed = getenv("GX_FIXED_DT");
        if (fixed && fixed[0]) {
            char *end = NULL;
            gX_F64 dt = strtod((const char *)fixed, &end);
            if (end != fixed && dt > 0.0) step = dt;
        }
        if (step > 0.0) {
            g_aiInput.clock += (gX_F32)step;
        } else {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (g_aiInput.t0Wall < 0.0) g_aiInput.t0Wall = (gX_F64)now.tv_sec + (gX_F64)now.tv_nsec / 1e9;
            g_aiInput.clock = (gX_F32)((gX_F64)now.tv_sec + (gX_F64)now.tv_nsec / 1e9 - g_aiInput.t0Wall);
        }
    }
    const float t = g_aiInput.clock;

    int idx = -1;
    float localT = 0.0f;
    for (int i = 0; i < g_aiInput.count; i++) {
        const gX_AI_Action *a = &g_aiInput.actions[i];
        if (t >= a->t0 && t < a->t0 + a->dur) { idx = i; localT = t - a->t0; break; }
    }

    static int prevDown = 0;
    static int prevButton = 0;
    static int prevKeyAction = -1;
    int down = 0;
    int button = prevButton;
    g_aiInput.lastKeyCode = 0;
    g_aiInput.lastKeyDown = 0;
    g_aiInput.lastKeyShift = 0;
    g_aiInput.lastKeyCtrl = 0;

    if (idx >= 0) {
        const gX_AI_Action *a = &g_aiInput.actions[idx];
        //opencode: move parks button-up; click/drag hold the button.
        down = (a->isKey || a->isPad) ? 0 : ((a->isDrag == 2) ? 0 : 1);
        button = a->button;
        g_aiInput.activeIdx = idx;
        if (a->isKey) {
            g_aiInput.lastKeyCode = a->keyCode;
            g_aiInput.lastKeyDown = 1;
            g_aiInput.lastKeyShift = a->keyShift;
            g_aiInput.lastKeyCtrl = a->keyCtrl;
            if (idx != prevKeyAction) gX_Input_Virtual_SetKey(a->keyCode, a->keyShift);
            prevKeyAction = idx;
        } else if (a->isPad) {
            prevKeyAction = -1;
        } else {
            //opencode: scripts author in window px; engine owns game coords.
            gX_F32 wx, wy;
            if (a->isDrag) {
                float k = (a->dur > 0.0f) ? (localT / a->dur) : 1.0f;
                if (k > 1.0f) k = 1.0f;
                wx = a->x1 + (a->x2 - a->x1) * k;
                wy = a->y1 + (a->y2 - a->y1) * k;
            } else {
                wx = a->x1;
                wy = a->y1;
            }
            input.mouse.coord = gX_GUI_ScreenToGame(wx, wy);
            g_aiInput.parkX = input.mouse.coord.x;
            g_aiInput.parkY = input.mouse.coord.y;
            g_aiInput.hasPark = 1;
            prevKeyAction = -1;
        }
    } else if (prevDown && g_aiInput.activeIdx >= 0) {
        const gX_AI_Action *a = &g_aiInput.actions[g_aiInput.activeIdx];
        button = a->button;
        gX_F32 wx = a->isDrag ? a->x2 : a->x1;
        gX_F32 wy = a->isDrag ? a->y2 : a->y1;
        input.mouse.coord = gX_GUI_ScreenToGame(wx, wy);
        g_aiInput.parkX = input.mouse.coord.x;
        g_aiInput.parkY = input.mouse.coord.y;
        g_aiInput.hasPark = 1;
        g_aiInput.activeIdx = -1;
        prevKeyAction = -1;
    } else if (g_aiInput.hasPark) {
        input.mouse.coord.x = g_aiInput.parkX;
        input.mouse.coord.y = g_aiInput.parkY;
        prevKeyAction = -1;
    } else {
        prevKeyAction = -1;
    }

    //opencode: drive only the synthesized button; release the others.
    gX_Input_State *states[3] = { &input.mouse.leftButton,
        &input.mouse.rightButton, &input.mouse.middleButton };
    for (int b = 0; b < 3; b++) {
        int isDown = down && button == b;
        int wasDown = (b == prevButton) ? prevDown : 0;
        states[b]->DOWN = isDown;
        states[b]->PRESSED = (isDown && !wasDown);
        states[b]->RELEASED = (!isDown && wasDown);
        states[b]->UP = !isDown;
    }
    prevDown = down;
    prevButton = button;
    //opencode: ImGui bridge readout (posted post-rlImGuiBegin in Draw_ImGui).
    g_aiInput.lastX = input.mouse.coord.x;
    g_aiInput.lastY = input.mouse.coord.y;
    g_aiInput.lastDown = down ? 1 : 0;
    g_aiInput.lastButton = button;
}

//ai: apply scripted pad state after physical polling so the same GUI path handles test and hardware input.
static gX_I0 gX_AI_Input_Apply_Gamepad(gX_I0)
{
    const gX_I8 *script = getenv("GX_AI_INPUT");
    if (!script || !script[0] || !g_aiInput.parsed) return;
    gX_I32 activeAction = -1;
    for (gX_I32 i = 0; i < g_aiInput.count; i++) {
        const gX_AI_Action *action = &g_aiInput.actions[i];
        if (g_aiInput.clock >= action->t0
            && g_aiInput.clock < action->t0 + action->dur) {
            activeAction = i;
            break;
        }
    }
    static gX_I32 previousAction = -1;
    static gX_I32 previousPad = -1;
    static gX_I32 previousButton = -1;
    if (previousAction >= 0 && previousAction != activeAction) {
        gX_Input_State *state = &input.gamepad[previousPad].buttons[previousButton];
        state->DOWN = false;
        state->PRESSED = false;
        state->RELEASED = true;
        state->UP = true;
        previousAction = -1;
    }
    if (activeAction < 0 || !g_aiInput.actions[activeAction].isPad) return;
    gX_AI_Action *action = &g_aiInput.actions[activeAction];
    gX_Input_Gamepad *pad = &input.gamepad[action->padIndex];
    gX_Input_State *state = &pad->buttons[action->padButton];
    pad->available = true;
    state->DOWN = true;
    state->PRESSED = previousAction != activeAction;
    state->RELEASED = false;
    state->UP = false;
    previousAction = activeAction;
    previousPad = action->padIndex;
    previousButton = action->padButton;
}

//opencode: predicate for the rlImGui poll guard — while a script owns the
//   cursor the backend must not re-post the real cursor (its poll runs after
//   our bridge posts, so it would win every frame).
int gX_AI_Input_ScriptActive(void) {
    const gX_I8 *script = getenv("GX_AI_INPUT");
    if (!script || !script[0]) return 0;
    if (!g_aiInput.parsed || (g_aiInput.count <= 0)) return 0;
    return 1;
}

//opencode: GX_AI_INPUT synth readout for the ImGui bridge. Valid from the
//   first parsed script frame on (script owns the cursor for the whole run).
gX_I1 gX_AI_Input_Synth(gX_F32 *x, gX_F32 *y, gX_I1 *down, gX_I32 *button,
    gX_I32 *modifierKey, gX_I32 *modifierAxis, gX_I1 *modifierCtrl,
    gX_I1 *modifierDown, gX_I32 *keyCode, gX_I1 *keyDown,
    gX_I1 *keyShift, gX_I1 *keyCtrl) {
    const gX_I8 *script = getenv("GX_AI_INPUT");
    if ((!script || !script[0]) || !g_aiInput.parsed || (g_aiInput.count <= 0)) return 0;
    if (x) *x = g_aiInput.lastX;
    if (y) *y = g_aiInput.lastY;
    if (down) *down = g_aiInput.lastDown;
    if (button) *button = g_aiInput.lastButton;
    if (modifierKey) *modifierKey = 0;
    if (modifierAxis) *modifierAxis = 0;
    if (modifierCtrl) *modifierCtrl = 0;
    if (modifierDown) *modifierDown = 0;
    if (keyCode) *keyCode = g_aiInput.lastKeyCode;
    if (keyDown) *keyDown = g_aiInput.lastKeyDown;
    if (keyShift) *keyShift = g_aiInput.lastKeyShift;
    if (keyCtrl) *keyCtrl = g_aiInput.lastKeyCtrl;
    if (g_aiInput.lastDown && g_aiInput.activeIdx >= 0) {
        const gX_AI_Action *a = &g_aiInput.actions[g_aiInput.activeIdx];
        if (modifierKey) *modifierKey = a->modifierKey;
        if (modifierAxis) *modifierAxis = a->modifierAxis;
        if (modifierCtrl) *modifierCtrl = a->modifierCtrl;
        if (modifierDown) *modifierDown = (a->modifierKey || a->modifierCtrl) ? 1 : 0;
    }
    return 1;
}

static gX_I0 gX_Input_Update_Binds(gX_I0) {
    for (gX_U32 i = 0; i < input.bindAmount; i++) {
        gX_Input_Bind_Process(&input.binds[i]);
    }
}

//ai: Start opens the cartridge pause menu; cover every console profile and
//   require its own scene so a stale boot flag cannot affect other demos.
static gX_I1 gX_Input_ConsoleController_Enabled(gX_I0)
{
    if (arguments.cartridgeY2K
        && maindata.masterIndex == gX_SCENE_RENDERER_GFX) return 1;
    if (arguments.cartridgeRay1
        && maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH) return 1;
    if (arguments.cartridgeParticle
        && maindata.masterIndex == gX_SCENE_PHYSICS) return 1;
    return 0;
}

//ai: physical-controller diagnostic — GX_GAMEPAD_TRACE=1 logs button edges
//   and significant axis changes so an adapter's mapping can be verified.
static gX_F32 s_gamepadTraceAxis[8][8];

//opencode: poll all connected gamepads and update button/axis state.
//   Iterates up to 8 gamepad slots. For each connected gamepad, polls
//   up to 64 buttons and up to 8 axes via the wrapper API.
static gX_I0 gX_Input_Update_Gamepad(gX_I0) {
    if (!maindata.backendLoaded) return;  //opencode: raylib gamepad calls crash before init
    gX_I1 trace = getenv("GX_GAMEPAD_TRACE") != NULL;
    for (gX_I32 gp = 0; gp < 8; gp++) {
        gX_Input_Gamepad *pad = &input.gamepad[gp];
        gX_I1 wasAvailable = pad->available;
        pad->available = gX_Gamepad_IsAvailable(gp);
        if (pad->available) {
            if (!wasAvailable) {
                // Gamepad just connected — grab name
                const gX_I8 *n = gX_Gamepad_GetName(gp);
                strncpy(pad->name, (n && n[0]) ? n : "unknown", sizeof(pad->name) - 1);
                pad->name[sizeof(pad->name) - 1] = '\0';
                pad->axisCount = gX_Gamepad_Axis_GetCount(gp);
                for (gX_I32 a = 0; a < 8; a++) s_gamepadTraceAxis[gp][a] = 0.0f;
                gX_TraceLog(gX_LOG_INFO,
                    "INPUT: gamepad %d connected name=\"%s\" axes=%d",
                    gp, pad->name, pad->axisCount);
            }
            // Poll buttons (0..GX_GAMEPAD_BUTTON_RIGHT_THUMB, max 17).
            //   Raylib only defines 18 buttons — querying indices >17 may
            //   access out-of-bounds internal arrays and segfault.
            for (gX_I32 b = 0; b <= gX_GAMEPAD_BUTTON_RIGHT_THUMB && b < 64; b++) {
                gX_Input_State *btn = &pad->buttons[b];
                btn->DOWN    = gX_Gamepad_Button_IsDown(gp, b);
                btn->PRESSED = gX_Gamepad_Button_IsPressed(gp, b);
                btn->RELEASED= gX_Gamepad_Button_IsReleased(gp, b);
                btn->UP      = gX_Gamepad_Button_IsUp(gp, b);
                if (btn->PRESSED) {
                    pad->buttonHistory[input.keyboard.keyHistoryIndexPtr % 16] = (gX_I8)b;
                    if (trace)
                        gX_TraceLog(gX_LOG_INFO,
                            "GAMEPAD_TRACE: pad=%d button=%d", gp, b);
                }
            }
            // Poll axes (0..pad->axisCount, max 8)
            for (gX_I32 a = 0; a < pad->axisCount && a < 8; a++) {
                pad->axisMovement[a] = gX_Gamepad_Axis_GetMovement(gp, a);
                if (trace
                    && fabsf(pad->axisMovement[a] - s_gamepadTraceAxis[gp][a]) > 0.25f) {
                    gX_TraceLog(gX_LOG_INFO,
                        "GAMEPAD_TRACE: pad=%d axis=%d value=%.2f",
                        gp, a, pad->axisMovement[a]);
                    s_gamepadTraceAxis[gp][a] = pad->axisMovement[a];
                }
            }
            //ai: digital pads report the D-pad on axes 0/1; synthesize the
            //   button states after the raw poll so menus and binds agree.
            gX_Input_Gamepad_SynthesizeDpad(pad);
            if (trace) {
                static const gX_I8 *dpadNames[4] = {"up", "right", "down", "left"};
                for (gX_I32 d = 0; d < 4; d++) {
                    if (pad->buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_UP + d].PRESSED
                        && pad->dpadAxisDown[d])
                        gX_TraceLog(gX_LOG_INFO,
                            "GAMEPAD_TRACE: pad=%d dpad=%s (axis)", gp, dpadNames[d]);
                }
            }
        } else {
            // Gamepad disconnected — clear state
            if (wasAvailable) {
                gX_TraceLog(gX_LOG_INFO, "INPUT: gamepad %d disconnected", gp);
                for (gX_I32 a = 0; a < 8; a++) s_gamepadTraceAxis[gp][a] = 0.0f;
                *pad = (gX_Input_Gamepad){0};
            }
        }
    }
}

//opencode: active runtime tools own keyboard navigation and gameplay shortcuts.
gX_I0 gX_Input_Update(gX_I0) {


    if (maindata.masterIndexSwitched) {
        GX_PROFILE_ZONE_BEGIN("input:respond");
        gX_Input_Respond();
        GX_PROFILE_ZONE_END("input:respond");
    }

    //opencode: runtime tools suppress gameplay binds except for an explicitly
    //   activated editor camera, which uses the existing scene camera binds.
    gX_I1 updateBinds = !gX_Tools_RuntimeActive() || input.toolCameraInputEnabled;

    GX_PROFILE_ZONE_BEGIN("input:devices");
    gX_Input_Update_Keyboard();
    gX_Input_Update_Mouse();
    //opencode: discard cursor recentering so mode changes do not become camera look.
    if (input.cursorSwitched) input.mouse.coordDelta = (gX_F32v2){0.0f, 0.0f};
    gX_Input_Update_Gamepad();
    gX_AI_Input_Apply_Gamepad();
    GX_PROFILE_ZONE_END("input:devices");

    //opencode: virtual input override — applies AFTER normal mouse/key update
    //   so programmatic inputs (AI probe, scripts, tests) take priority.
    //   Mouse position persists (overrides raylib) while virtInput is active.
    //   Clicks and keys fire once per set, then clear their pending flags.
    //   Held keys (SetKeyHeld) stay DOWN every frame until released. Held keys
    //   apply regardless of the transient `active` flag — scripting's merge
    //   (scripting.cpp) resets active=0 every frame even when no scripts run,
    //   but the held bitmap is persistent state that must still take effect.
    if (input.virtInput.active) {
        input.mouse.coord.x = (gX_F32)input.virtInput.mouseX;
        input.mouse.coord.y = (gX_F32)input.virtInput.mouseY;
        if (input.virtInput.clickPending) {
            input.mouse.leftButton.DOWN = true;
            input.mouse.leftButton.PRESSED = true;
            input.virtInput.clickPending = false;
        }
        if (input.virtInput.keyPending) {
            gX_I32 kc = input.virtInput.keyCode;
            if (kc >= 0 && kc < gX_INPUT_KEYBOARD_KEYS) {
                input.keyboard.keys[kc].state.PRESSED = true;
                input.keyboard.keys[kc].state.DOWN = true;
                if (input.virtInput.keyShift && gX_KEY_LEFT_SHIFT < gX_INPUT_KEYBOARD_KEYS) {
                    input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN = true;
                }
            }
            input.virtInput.keyPending = false;
        }
    }
    //opencode: held keys — persistent DOWN, independent of the active flag
    for (gX_I32 kc = 0; kc < gX_INPUT_KEYBOARD_KEYS; kc++) {
        if (input.virtInput.held[kc]) {
            input.keyboard.keys[kc].state.DOWN = true;
            if (input.virtInput.heldShift[kc] && gX_KEY_LEFT_SHIFT < gX_INPUT_KEYBOARD_KEYS) {
                input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN = true;
            }
        }
    }
    //opencode: re-process binds after the held-key state so movement sees it
    //   this frame; the console idle wake consumes input first, before binds,
    //   character queueing or any global hotkey.
    gX_GUI_ConsoleIdle_Update();
    gX_Input_CaptureBind();
    if (updateBinds) gX_Input_Update_Binds();
    //opencode: enqueue characters for the textinput widget (after virtual
    //   input override so AI/script key presses synthesize chars too)
    if (!gX_Tools_RuntimeActive()) gX_Input_Update_CharQueue();
    
    //opencode: use input system's PRESSED state (not raylib gX_Key_IsPressed)
    //so AI virtual key presses work correctly
    if (input.keyboard.keys[gX_KEY_INSERT].state.PRESSED) {
        gX_Cursor_Toggle();
    }
    gX_I1 consoleStartPressed = gX_Input_ConsoleController_Enabled()
        && input.gamepad[0].available
        && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_MIDDLE_RIGHT].PRESSED;
    if ((input.keyboard.keys[input.menuKey].state.PRESSED || consoleStartPressed)
        && !gX_Tools_RuntimeActive()) {
        //opencode: ignore ESC during scene transition (fade in progress, canvases may not exist)
        if (gui.directdraw.fadeActive || maindata.masterIndexSwitch) return;
        //opencode: Esc cancels the dialog first (never toggles the menu behind it)
        if (gX_GUI_Dialog_IsOpen()) {
            gX_GUI_Dialog_Close(false);
            return;
        }
        //opencode: if exit is pending, cancel it
        if (maindata.exitRequested) {
            maindata.exitRequested = false;
            gui.state.SHOW_MSG = false;
            gui.state.toastMessage[0] = '\0';
        } else if (!gX_GUI_Handle_Back()) {
            if (gui.state.SHOW_MENU) {
                //opencode: close menu and clear focus
                gui.state.SHOW_MENU = false;
                gX_GUI_Focus_Clear();
            } else {
                gui.state.SHOW_MENU = true;
            }
        }
    }
    if (consoleStartPressed && !gX_Tools_RuntimeActive()) {
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_INPUT: start menu=%d",
            gui.state.SHOW_MENU ? 1 : 0);
    }
    if (input.keyboard.keys[input.screenshotKey].state.PRESSED) {
        maindata.screenshot = true;
        gX_Audio_SoundPlay("notify");
    }
    if (input.keyboard.keys[input.fullscreenKey].state.PRESSED) {
        gX_Window_ToggleFullScreen();
        settings.fullScreen = gX_Window_IsFullScreen();
        maindata.refresh = true;
    }
    //opencode: F7 toggles element inspector
    if (input.keyboard.keys[gX_KEY_F7].state.PRESSED
        && !gX_Tools_RuntimeActive()
        && !gX_ConsolePlayback_Paused()) {
        gui.inspectorEnabled = !gui.inspectorEnabled;
        if (!gui.inspectorEnabled) {
            gui.inspectorCanvasID = -1;
            gui.inspectorElementID = -1;
        }
    }
    
    //opencode: element inspector picks element on left click
    if (gui.inspectorEnabled && input.mouse.leftButton.PRESSED) {
        gui.inspectorCanvasID = gui.occlusion.canvasID;
        gui.inspectorElementID = gui.occlusion.elementID;
    }
    
    if (input.cursorSwitched) {
        //opencode: audit windows never lock the pointer — a GLFW cursor grab
        //   is global on X11 and would take the mouse away from the desktop
        //   while the audit runs (GX_AUDIT_WINDOW=hidden/1, set by the audit
        //   scripts and the Makefile gate default).
        const gX_I8 *auditWindow = getenv("GX_AUDIT_WINDOW");
        gX_I1 auditCursor = auditWindow
            && (auditWindow[0] == '1'
                || strcmp((const char *)auditWindow, "hidden") == 0);
        gX_I1 automated = gX_Input_IsAutomated();
        if (input.cursorEnabled || auditCursor || automated) {
            if (!input.cursorEnabled && auditCursor) {
                static gX_I1 auditCursorLogged = 0;
                if (!auditCursorLogged) {
                    auditCursorLogged = 1;
                    gX_TraceLog(gX_LOG_INFO,
                        "INPUT: audit window keeps the cursor (GX_AUDIT_WINDOW)");
                }
            }
            gX_Cursor_Enable();
        } else {
            static gX_I1 cursorLockLogged = 0;
            if (!cursorLockLogged) {
                cursorLockLogged = 1;
                gX_TraceLog(gX_LOG_INFO,
                    "INPUT: scene locked the cursor (free-look path)");
            }
            gX_Cursor_Disable();
        }
        {
            const gX_I8 *trace = getenv("GX_CURSOR_TRACE");
            if (trace && trace[0] && trace[0] != '0') {
                gX_TraceLog(gX_LOG_INFO, "CURSOR: applied state=%s hidden=%d",
                    (const char *)cursor_state_label(gX_Cursor_Get()),
                    (gX_I32)gX_Cursor_IsHidden());
            }
        }
        input.cursorSwitched = false;
    }
    
}

//opencode: virtual input API — programmatic mouse/key override
//   Once active, virtual mouse position persists across frames until
//   the next SetMouse call or a real raylib mouse event. Clicks and
//   keys fire once per set, then clear their pending flags.
gX_I0 gX_Input_Virtual_SetMouse(gX_I32 x, gX_I32 y) {
    input.virtInput.mouseX = x;
    input.virtInput.mouseY = y;
    input.virtInput.active = true;
}

gX_I0 gX_Input_Virtual_Click(gX_I32 x, gX_I32 y) {
    input.virtInput.mouseX = x;
    input.virtInput.mouseY = y;
    input.virtInput.clickPending = true;
    input.virtInput.active = true;
}

gX_I0 gX_Input_Virtual_SetKey(gX_I32 keyCode, gX_I1 shift) {
    input.virtInput.keyCode = keyCode;
    input.virtInput.keyShift = shift;
    input.virtInput.keyPending = true;
    input.virtInput.active = true;
}

//opencode: persistent key hold — keeps the key DOWN across frames until
//   released. Movement binds (state.DOWN) read this every frame.
gX_I0 gX_Input_Virtual_SetKeyHeld(gX_I32 keyCode, gX_I1 shift, gX_I1 held) {
    if (keyCode < 0 || keyCode >= gX_INPUT_KEYBOARD_KEYS) return;
    input.virtInput.held[keyCode] = held;
    input.virtInput.heldShift[keyCode] = shift;
    input.virtInput.active = true;
}

//opencode: deactivate virtual input — returns control to real raylib mouse
gX_I0 gX_Input_Virtual_Deactivate(gX_I0) {
    input.virtInput.active = false;
}
