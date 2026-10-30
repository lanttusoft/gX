/**********************************************************************************************
*
*   [gX: src/scripting/api_core.cpp]
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


//ai: ported from gameX src/scripting/api_core.cpp — distilled to gX's shell
//   surface: math/easing, Print, Yield/Sleep, key state, time, render scale,
//   scene switch + constants, screenshot, Vec3, hooks, and the shell
//   assert/quit harness (game-content, GUI-test, audio, synth, AI-probe and
//   demoscript bindings stayed out).
#include "api_core.h"

#include <stdio.h>
#include <math.h>
#include <string>

#include <angelscript.h>

#include "../common/log.h"
#include "../common/utils.h"
#include "../common/enum.h"
#include "../gxmath/vector3.h"
#include "../gxmath/easing.h"
#include "../scripting/hooks.h"
#include "../main.h"
#include "../input.h"
#include "../gui/gui.h"
#include "../gui/canvas.h"
#include "../gui/element/struct.h"

//ai: gX_TakeScreenShot lives in wrappers/wrappers.h, whose include chain
//   reaches raylib's raymath through an extern "C" block — a C++ translation
//   unit must not include it (see src/tools/gfx/gfx_editor.cpp's note). Only
//   the one symbol is needed here.
extern "C" gX_I0 gX_TakeScreenShot(const gX_I8 *path);

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS registration failed (code %d)", r); \
} while(0)

// ===========================================================================
// Frame yield / sleep — cooperative multi-frame suspension
// ===========================================================================

static gX_Script *gX_ApiCore_FindScriptByContext(asIScriptContext *ctx) {
    gX_Script *scripts = (gX_Script*)g_scripting.scripts.data;
    for (gX_U32 i = 0; i < g_scripting.scripts.amount; i++) {
        if (g_scripting.scripts.meta[i].USED && scripts[i].context == ctx)
            return &scripts[i];
    }
    return NULL;
}

static void Script_Yield(void) {
    asIScriptContext *ctx = asGetActiveContext();
    if (ctx) ctx->Suspend();
}

static void Script_Sleep(float ms) {
    asIScriptContext *ctx = asGetActiveContext();
    if (!ctx) return;
    gX_Script *s = gX_ApiCore_FindScriptByContext(ctx);
    if (s) {
        s->resumeTime = maindata.timeElapsed + (gX_F64)ms / 1000.0;
    }
    ctx->Suspend();
}

// ===========================================================================
// Keyboard state
// ===========================================================================

static gX_I1 Script_IsKeyPressed(gX_I32 key) {
    if (key < 0 || key >= gX_INPUT_KEYBOARD_KEYS) return 0;
    return input.keyboard.keys[key].state.PRESSED;
}

static gX_I1 Script_IsKeyDown(gX_I32 key) {
    if (key < 0 || key >= gX_INPUT_KEYBOARD_KEYS) return 0;
    return input.keyboard.keys[key].state.DOWN;
}

static gX_I1 Script_IsKeyReleased(gX_I32 key) {
    if (key < 0 || key >= gX_INPUT_KEYBOARD_KEYS) return 0;
    return input.keyboard.keys[key].state.RELEASED;
}

// ===========================================================================
// Mouse state
// ===========================================================================

static const gX_Input_State *ApiCore_MouseButton(gX_I32 btn) {
    switch (btn) {
        case 0: return &input.mouse.leftButton;
        case 1: return &input.mouse.rightButton;
        case 2: return &input.mouse.middleButton;
        default: {
            gX_I32 idx = btn - 3;
            if (idx >= 0 && idx < 8) return &input.mouse.extraButton[idx];
            return NULL;
        }
    }
}

static gX_I32 Script_MouseGetX(void) { return (gX_I32)input.mouse.coord.x; }
static gX_I32 Script_MouseGetY(void) { return (gX_I32)input.mouse.coord.y; }
static gX_F32 Script_MouseGetScroll(void) { return input.mouse.scroll; }
static gX_F32 Script_MouseGetDeltaX(void) { return input.mouse.coordDelta.x; }
static gX_F32 Script_MouseGetDeltaY(void) { return input.mouse.coordDelta.y; }

static gX_I1 Script_MouseIsPressed(gX_I32 btn) {
    const gX_Input_State *s = ApiCore_MouseButton(btn);
    return s ? s->PRESSED : 0;
}

static gX_I1 Script_MouseIsDown(gX_I32 btn) {
    const gX_Input_State *s = ApiCore_MouseButton(btn);
    return s ? s->DOWN : 0;
}

// ===========================================================================
// Virtual input — programmatic mouse/keys through the per-script slot
// ===========================================================================

static void Script_GUIClick(gX_I32 x, gX_I32 y) {
    gX_Input_Virtual *virt = gX_Scripting_GetActiveVirt();
    virt->mouseX = x;
    virt->mouseY = y;
    virt->clickPending = 1;
    virt->active = 1;
}

static void Script_GUIKey(gX_I32 keyCode) {
    gX_Input_Virtual *virt = gX_Scripting_GetActiveVirt();
    virt->keyCode = keyCode;
    virt->keyShift = 0;
    virt->keyPending = 1;
    virt->active = 1;
}

//release programmatic input for the CURRENT script only — concurrent scripts
//   keep their own pending clicks/keys (per-script slots).
static void Script_GUIDeactivateVirtualInput(void) {
    gX_Input_Virtual *virt = gX_Scripting_GetActiveVirt();
    virt->active = 0;
    virt->clickPending = 0;
    virt->keyPending = 0;
}

// ===========================================================================
// GUI canvas/element queries (script-side DOM)
// ===========================================================================

static gX_GUI_Element *ApiCore_Dom_Element(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(canvasID);
    if (!c) return NULL;
    if (elementIndex < 0 || (gX_U32)elementIndex >= c->elements.amount) return NULL;
    if (!c->elements.meta[elementIndex].USED) return NULL;
    return &((gX_GUI_Element*)c->elements.data)[elementIndex];
}

static gX_I32 Script_GUIGetCanvasCount(void) {
    return (gX_I32)gui.canvases.amount;
}

static gX_I32 Script_GUIGetCanvasElementCount(gX_I32 canvasID) {
    gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(canvasID);
    return c ? (gX_I32)c->elements.amount : -1;
}

static gX_I32 Script_GUIFindElement(gX_I32 canvasID, const std::string &name) {
    gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(canvasID);
    if (!c || name.empty()) return -1;
    gX_GUI_Element *elements = (gX_GUI_Element*)c->elements.data;
    for (gX_U32 i = 0; i < c->elements.amount; i++) {
        if (!c->elements.meta[i].USED) continue;
        if (strcmp(elements[i].name, name.c_str()) == 0) return (gX_I32)i;
    }
    return -1;
}

static std::string Script_GUIGetElementName(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? std::string(e->name) : std::string();
}

static std::string Script_GUIGetElementText(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    if (!e || e->texts.amount == 0 || !e->texts.data) return std::string();
    gX_GUI_Element_Text *t = &((gX_GUI_Element_Text*)e->texts.data)[0];
    return std::string(t->text);
}

static gX_I1 Script_GUIIsHovered(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? (e->state.HOVERED != 0) : 0;
}

static gX_I1 Script_GUIIsClicked(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? (e->state.CLICKED_LEFT != 0) : 0;
}

static gX_I1 Script_GUIIsFocused(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? (e->state.FOCUSED != 0) : 0;
}

static gX_I1 Script_GUIIsDisabled(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? (e->state.DISABLED != 0) : 0;
}

static gX_F32 Script_GUIGetElementX(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? e->rect.x : -1.0f;
}
static gX_F32 Script_GUIGetElementY(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? e->rect.y : -1.0f;
}
static gX_F32 Script_GUIGetElementW(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? e->rect.width : -1.0f;
}
static gX_F32 Script_GUIGetElementH(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    return e ? e->rect.height : -1.0f;
}

//click an element's rect center — element rects live in canvas-buffer space,
//   so the forward transform (offset/scale/angle) converts to screen pixels
//   before handing the point to the virtual mouse.
static void Script_GUIClickElement(gX_I32 canvasID, gX_I32 elementIndex) {
    gX_GUI_Element *e = ApiCore_Dom_Element(canvasID, elementIndex);
    gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(canvasID);
    if (!e || !c) return;
    gX_F32v2 buffer = { e->rect.x + e->rect.width * 0.5f, e->rect.y + e->rect.height * 0.5f };
    gX_F32v2 screen = buffer;
    gX_GUI_Canvas_ToScreenPoint(c, buffer, &screen);
    Script_GUIClick((gX_I32)screen.x, (gX_I32)screen.y);
}

static void Script_GUIFocusElement(gX_I32 canvasID, gX_I32 elementIndex) {
    if (!ApiCore_Dom_Element(canvasID, elementIndex)) return;
    gX_GUI_Focus_Set(canvasID, elementIndex);
}

static gX_I1 Script_GUIAssertTextContains(gX_I32 canvasID, gX_I32 elementIndex,
                                          const std::string &substr, const std::string &msg) {
    std::string text = Script_GUIGetElementText(canvasID, elementIndex);
    gX_I1 passed = !substr.empty() && text.find(substr) != std::string::npos;
    if (passed) {
        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[SHELL] PASS: %s (text contains \"%s\")", msg.c_str(), substr.c_str());
        return 1;
    }
    gX_Scripting_Shell_Fail();
    gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "[SHELL] FAIL: %s (text \"%s\" lacks \"%s\")",
        msg.c_str(), text.c_str(), substr.c_str());
    return 0;
}

// ===========================================================================
// Time
// ===========================================================================

static gX_F64 Script_GetTotalTime(void) {
    return maindata.timeElapsed;
}

static gX_I32 Script_GetFrameCount(void) {
    return (gX_I32)maindata.frameCount;
}

// ===========================================================================
// Render resolution control
// ===========================================================================

static void Script_SetRenderScale(float scale) {
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 4.0f) scale = 4.0f;
    maindata.renderScale = scale;
    maindata.refresh = true;
}

// ===========================================================================
// Screenshot
// ===========================================================================

static void Script_TakeScreenshot(void) {
    gX_TextLine text;
    gX_Text_Screenshot(text);
    gX_TakeScreenShot(text);
    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "screenshot -> screenshot/%s", text);
}

// ===========================================================================
// Scene switching + scene constants
// ===========================================================================

static void Script_SwitchScene(gX_I32 sceneIndex) {
    gX_MainData_Switch(sceneIndex);
}

#define X(scene) static const gX_I32 SCENE_CONST_##scene = gX_SCENE_##scene;
gX_SCENES
#undef X

// ===========================================================================
// Scalar math + easing
// ===========================================================================

static float Script_Sin(float x)  { return sinf(x); }
static float Script_Cos(float x)  { return cosf(x); }
static float Script_Tan(float x)  { return tanf(x); }
static float Script_Sqrt(float x) { return sqrtf(x); }
static float Script_Abs(float x)  { return fabsf(x); }
static float Script_Min(float a, float b) { return a < b ? a : b; }
static float Script_Max(float a, float b) { return a > b ? a : b; }
static float Script_Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Script_EaseBezier(float start, float startCtrl, float endCtrl, float end, float t) {
    return gX_F32_EasingBezier(start, startCtrl, endCtrl, end, t);
}

static float Script_EasePow(float start, float end, float t, float power) {
    return gX_F32_EasingPow(start, end, t, power);
}

static void Script_Print(const std::string &msg) {
    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[Script] %s", msg.c_str());
}

// ===========================================================================
// Shell harness — asserts, failure count and clean quit for gate scripts
// ===========================================================================

static gX_I1 Shell_AssertResult(gX_I1 passed, const std::string &msg) {
    if (passed) {
        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[SHELL] PASS: %s", msg.c_str());
    } else {
        gX_Scripting_Shell_Fail();
        gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "[SHELL] FAIL: %s", msg.c_str());
    }
    return passed;
}

static gX_I1 Shell_AssertTrue(gX_I1 condition, const std::string &msg) {
    return Shell_AssertResult(condition != 0, msg);
}

static gX_I1 Shell_AssertEqual(gX_I32 actual, gX_I32 expected, const std::string &msg) {
    gX_I1 passed = actual == expected;
    if (passed) {
        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[SHELL] PASS: %s (%d == %d)", msg.c_str(), actual, expected);
        return 1;
    }
    gX_Scripting_Shell_Fail();
    gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "[SHELL] FAIL: %s (%d != %d)", msg.c_str(), actual, expected);
    return 0;
}

static gX_I1 Shell_AssertFloat(gX_F32 actual, gX_F32 expected, gX_F32 epsilon, const std::string &msg) {
    gX_F32 diff = actual - expected;
    if (diff < 0.0f) diff = -diff;
    gX_I1 passed = diff <= epsilon;
    if (passed) {
        gX_TraceLog_SCRIPTING(gX_LOG_INFO, "[SHELL] PASS: %s (|%g - %g| <= %g)", msg.c_str(), actual, expected, epsilon);
        return 1;
    }
    gX_Scripting_Shell_Fail();
    gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "[SHELL] FAIL: %s (|%g - %g| > %g)", msg.c_str(), actual, expected, epsilon);
    return 0;
}

static void Shell_Fail(const std::string &msg) {
    gX_Scripting_Shell_Fail();
    gX_TraceLog_SCRIPTING(gX_LOG_ERROR, "[SHELL] FAIL: %s", msg.c_str());
}

static gX_I32 Shell_FailCount(void) {
    return gX_Scripting_Shell_FailCount();
}

static void Shell_Quit(gX_I32 code) {
    gX_Scripting_Shell_SetExitCode(code);
    maindata.loop = false;
}

// ===========================================================================
// Script list + debugger bridge (breakpoints/stepping; the panel and gate
//   drivers use these — the host owns the pause in the line callback)
// ===========================================================================

static std::string Script_GetPath(gX_I32 scriptIndex) {
    return std::string(gX_Scripting_ScriptPath((gX_U32)scriptIndex));
}

static std::string Script_GetName(gX_I32 scriptIndex) {
    return std::string(gX_Scripting_ScriptName((gX_U32)scriptIndex));
}

// ===========================================================================
// Vec3 — native gX_F32v3 POD, registered as an AngelScript value type
// ===========================================================================

static void Vec3_DefaultConstruct(gX_F32v3 *self) {
    self->x = 0.0f; self->y = 0.0f; self->z = 0.0f;
}
static void Vec3_Construct(gX_F32v3 *self, float x, float y, float z) {
    self->x = x; self->y = y; self->z = z;
}
static void Vec3_CopyConstruct(gX_F32v3 *self, const gX_F32v3 &other) {
    *self = other;
}

static gX_F32v3 Vec3_OpAdd(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Add(*self, other);
}
static gX_F32v3 Vec3_OpSub(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Sub(*self, other);
}
static gX_F32v3 Vec3_OpMul(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Mul(*self, other);
}
static gX_F32v3 Vec3_OpDiv(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Div(*self, other);
}
static gX_F32v3 Vec3_OpMulFloat(const gX_F32v3 *self, float s) {
    return gX_F32v3_MulScalar(*self, s);
}
static gX_F32v3 Vec3_OpDivFloat(const gX_F32v3 *self, float s) {
    return gX_F32v3_DivScalar(*self, s);
}
static gX_F32v3& Vec3_OpAddAssign(gX_F32v3 *self, const gX_F32v3 &other) {
    *self = gX_F32v3_Add(*self, other); return *self;
}
static gX_F32v3& Vec3_OpSubAssign(gX_F32v3 *self, const gX_F32v3 &other) {
    *self = gX_F32v3_Sub(*self, other); return *self;
}
static gX_F32v3& Vec3_OpMulAssign(gX_F32v3 *self, const gX_F32v3 &other) {
    *self = gX_F32v3_Mul(*self, other); return *self;
}
static gX_F32v3& Vec3_OpDivAssign(gX_F32v3 *self, const gX_F32v3 &other) {
    *self = gX_F32v3_Div(*self, other); return *self;
}
static bool Vec3_OpEquals(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Equals(*self, other) != 0;
}
static gX_F32v3 Vec3_OpNeg(const gX_F32v3 *self) {
    return gX_F32v3_Negate(*self);
}

static float Vec3_Length(const gX_F32v3 *self) {
    return gX_F32v3_Length(*self);
}
static float Vec3_LengthSqr(const gX_F32v3 *self) {
    return gX_F32v3_LengthSqr(*self);
}
static gX_F32v3 Vec3_Normalize(const gX_F32v3 *self) {
    return gX_F32v3_Normalize(*self);
}
static float Vec3_Dot(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Dot(*self, other);
}
static gX_F32v3 Vec3_Cross(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Cross(*self, other);
}
static float Vec3_Distance(const gX_F32v3 *self, const gX_F32v3 &other) {
    return gX_F32v3_Distance(*self, other);
}
static gX_F32v3 Vec3_Lerp(const gX_F32v3 *self, const gX_F32v3 &target, float t) {
    return gX_F32v3_Lerp(*self, target, t);
}
static gX_F32v3 Vec3_Reflect(const gX_F32v3 *self, const gX_F32v3 &normal) {
    return gX_F32v3_Reflect(*self, normal);
}
static gX_F32v3 Vec3_Abs(const gX_F32v3 *self) {
    return gX_F32v3_Abs(*self);
}
static gX_F32v3 Vec3_Floor(const gX_F32v3 *self) {
    return gX_F32v3_Floor(*self);
}
static gX_F32v3 Vec3_Ceil(const gX_F32v3 *self) {
    return gX_F32v3_Ceil(*self);
}
static gX_F32v3 Vec3_Clamp(const gX_F32v3 *self, const gX_F32v3 &min, const gX_F32v3 &max) {
    return gX_F32v3_Clamp(*self, min, max);
}

static gX_F32v3 Vec3_Create(float x, float y, float z) {
    return gX_F32v3_Make(x, y, z);
}

// ===========================================================================
// Hook — register script callbacks from .as files
// ===========================================================================

//funcdef-based hook registration; scripts pass the function directly:
//     Hook("scene_enter", OnSceneEnter);
//   The HOOKCALLBACK @+ parameter is an auto-handle — AngelScript releases
//   the reference after the call returns, so this must NOT release it here;
//   gX_Scripting_Hook_Register AddRefs its own stored copy.
static void Script_Hook(const std::string &name, asIScriptFunction *func) {
    if (!func) return;
    int r = gX_Scripting_Hook_Register(name.c_str(), func);
    if (r < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING,
            "Hook('%s') registration failed (%d)", name.c_str(), r);
    }
}

//script-facing trigger — wraps gX_Scripting_TriggerHook so the registered
//   function takes const string& (AngelScript passes the string object, not a
//   raw char* — a raw asFUNCTION(char*) ABI mismatch corrupts the arg).
static void Script_TriggerHook(const std::string &name) {
    gX_Scripting_TriggerHook(name.c_str());
}

// ===========================================================================
// Registration
// ===========================================================================

gX_I32 gX_Scripting_RegisterCoreAPI(asIScriptEngine *engine) {
    int r = 0;

    r = engine->RegisterGlobalFunction("float Sin(float)",
        asFUNCTION(Script_Sin), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Cos(float)",
        asFUNCTION(Script_Cos), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Tan(float)",
        asFUNCTION(Script_Tan), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Sqrt(float)",
        asFUNCTION(Script_Sqrt), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Abs(float)",
        asFUNCTION(Script_Abs), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Min(float, float)",
        asFUNCTION(Script_Min), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Max(float, float)",
        asFUNCTION(Script_Max), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Clamp(float, float, float)",
        asFUNCTION(Script_Clamp), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float EaseBezier(float, float, float, float, float)",
        asFUNCTION(Script_EaseBezier), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float EasePow(float, float, float, float)",
        asFUNCTION(Script_EasePow), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Print(const string &in)",
        asFUNCTION(Script_Print), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("void Yield()",
        asFUNCTION(Script_Yield), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Sleep(float)",
        asFUNCTION(Script_Sleep), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("void SetRenderScale(float)",
        asFUNCTION(Script_SetRenderScale), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TakeScreenshot()",
        asFUNCTION(Script_TakeScreenshot), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("double GetTotalTime()",
        asFUNCTION(Script_GetTotalTime), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int GetFrameCount()",
        asFUNCTION(Script_GetFrameCount), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("bool IsKeyPressed(int)",
        asFUNCTION(Script_IsKeyPressed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool IsKeyDown(int)",
        asFUNCTION(Script_IsKeyDown), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool IsKeyReleased(int)",
        asFUNCTION(Script_IsKeyReleased), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("int Mouse_GetX()",
        asFUNCTION(Script_MouseGetX), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int Mouse_GetY()",
        asFUNCTION(Script_MouseGetY), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Mouse_GetScroll()",
        asFUNCTION(Script_MouseGetScroll), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Mouse_GetDeltaX()",
        asFUNCTION(Script_MouseGetDeltaX), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float Mouse_GetDeltaY()",
        asFUNCTION(Script_MouseGetDeltaY), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Mouse_IsPressed(int)",
        asFUNCTION(Script_MouseIsPressed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool Mouse_IsDown(int)",
        asFUNCTION(Script_MouseIsDown), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("void GUIClick(int, int)",
        asFUNCTION(Script_GUIClick), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void GUIKey(int)",
        asFUNCTION(Script_GUIKey), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void GUIDeactivateVirtualInput()",
        asFUNCTION(Script_GUIDeactivateVirtualInput), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void GUIClickElement(int, int)",
        asFUNCTION(Script_GUIClickElement), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void GUIFocusElement(int, int)",
        asFUNCTION(Script_GUIFocusElement), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("int GUIGetCanvasCount()",
        asFUNCTION(Script_GUIGetCanvasCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int GUIGetCanvasElementCount(int)",
        asFUNCTION(Script_GUIGetCanvasElementCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int GUIFindElement(int, const string &in)",
        asFUNCTION(Script_GUIFindElement), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool GUIIsHovered(int, int)",
        asFUNCTION(Script_GUIIsHovered), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool GUIIsClicked(int, int)",
        asFUNCTION(Script_GUIIsClicked), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool GUIIsFocused(int, int)",
        asFUNCTION(Script_GUIIsFocused), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool GUIIsDisabled(int, int)",
        asFUNCTION(Script_GUIIsDisabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string GUIGetElementName(int, int)",
        asFUNCTION(Script_GUIGetElementName), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string GUIGetElementText(int, int)",
        asFUNCTION(Script_GUIGetElementText), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float GUIGetElementX(int, int)",
        asFUNCTION(Script_GUIGetElementX), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float GUIGetElementY(int, int)",
        asFUNCTION(Script_GUIGetElementY), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float GUIGetElementW(int, int)",
        asFUNCTION(Script_GUIGetElementW), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float GUIGetElementH(int, int)",
        asFUNCTION(Script_GUIGetElementH), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool GUIAssertTextContains(int, int, const string &in, const string &in)",
        asFUNCTION(Script_GUIAssertTextContains), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("bool AssertTrue(bool, const string &in)",
        asFUNCTION(Shell_AssertTrue), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool AssertEqual(int, int, const string &in)",
        asFUNCTION(Shell_AssertEqual), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool AssertFloat(float, float, float, const string &in)",
        asFUNCTION(Shell_AssertFloat), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Fail(const string &in)",
        asFUNCTION(Shell_Fail), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int FailCount()",
        asFUNCTION(Shell_FailCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Quit(int)",
        asFUNCTION(Shell_Quit), asCALL_CDECL); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("int ScriptCount()",
        asFUNCTION(gX_Scripting_ScriptCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string ScriptGetPath(int)",
        asFUNCTION(Script_GetPath), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string ScriptGetName(int)",
        asFUNCTION(Script_GetName), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugToggleBreakpoint(int, int)",
        asFUNCTION(gX_Scripting_Debug_ToggleBreakpoint), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugClearBreakpoints(int)",
        asFUNCTION(gX_Scripting_Debug_ClearBreakpoints), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool DebugHasBreakpoint(int, int)",
        asFUNCTION(gX_Scripting_Debug_HasBreakpoint), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugContinue(int)",
        asFUNCTION(gX_Scripting_Debug_Continue), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugStep(int)",
        asFUNCTION(gX_Scripting_Debug_Step), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugStepOver(int)",
        asFUNCTION(gX_Scripting_Debug_StepOver), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void DebugStepOut(int)",
        asFUNCTION(gX_Scripting_Debug_StepOut), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool DebugIsPaused(int)",
        asFUNCTION(gX_Scripting_Debug_IsPaused), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int DebugGetLine(int)",
        asFUNCTION(gX_Scripting_Debug_GetLine), asCALL_CDECL); AS_CHECK(r);

    #define X(scene) \
        r = engine->RegisterGlobalProperty("const int SCENE_" #scene, (void*)&SCENE_CONST_##scene); \
        AS_CHECK(r);
    gX_SCENES
    #undef X

    {
        static gX_I32 KEY_CONST_TAB = gX_KEY_TAB;
        static gX_I32 KEY_CONST_ENTER = gX_KEY_ENTER;
        static gX_I32 KEY_CONST_SPACE = gX_KEY_SPACE;
        static gX_I32 KEY_CONST_ESCAPE = gX_KEY_ESCAPE;
        static gX_I32 KEY_CONST_F4 = gX_KEY_F4;
        static gX_I32 KEY_CONST_UP = gX_KEY_UP;
        static gX_I32 KEY_CONST_DOWN = gX_KEY_DOWN;
        static gX_I32 KEY_CONST_LEFT = gX_KEY_LEFT;
        static gX_I32 KEY_CONST_RIGHT = gX_KEY_RIGHT;
        static gX_I32 KEY_CONST_HOME = gX_KEY_HOME;
        static gX_I32 KEY_CONST_END = gX_KEY_END;
        static gX_I32 KEY_CONST_BACKSPACE = gX_KEY_BACKSPACE;
        static gX_I32 KEY_CONST_DELETE = gX_KEY_DELETE;
        static gX_I32 KEY_CONST_A = gX_KEY_A;
        static gX_I32 KEY_CONST_B = gX_KEY_B;
        static gX_I32 KEY_CONST_C = gX_KEY_C;
        static gX_I32 KEY_CONST_D = gX_KEY_D;
        static gX_I32 KEY_CONST_M = gX_KEY_M;
        static gX_I32 KEY_CONST_R = gX_KEY_R;
        static gX_I32 KEY_CONST_S = gX_KEY_S;
        static gX_I32 KEY_CONST_V = gX_KEY_V;
        static gX_I32 KEY_CONST_W = gX_KEY_W;
        static gX_I32 KEY_CONST_X = gX_KEY_X;
        static gX_I32 KEY_CONST_Z = gX_KEY_Z;
        r = engine->RegisterGlobalProperty("const int KEY_TAB", (void*)&KEY_CONST_TAB); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_ENTER", (void*)&KEY_CONST_ENTER); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_SPACE", (void*)&KEY_CONST_SPACE); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_ESCAPE", (void*)&KEY_CONST_ESCAPE); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_F4", (void*)&KEY_CONST_F4); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_UP", (void*)&KEY_CONST_UP); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_DOWN", (void*)&KEY_CONST_DOWN); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_LEFT", (void*)&KEY_CONST_LEFT); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_RIGHT", (void*)&KEY_CONST_RIGHT); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_HOME", (void*)&KEY_CONST_HOME); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_END", (void*)&KEY_CONST_END); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_BACKSPACE", (void*)&KEY_CONST_BACKSPACE); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_DELETE", (void*)&KEY_CONST_DELETE); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_A", (void*)&KEY_CONST_A); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_B", (void*)&KEY_CONST_B); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_C", (void*)&KEY_CONST_C); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_D", (void*)&KEY_CONST_D); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_M", (void*)&KEY_CONST_M); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_R", (void*)&KEY_CONST_R); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_S", (void*)&KEY_CONST_S); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_V", (void*)&KEY_CONST_V); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_W", (void*)&KEY_CONST_W); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_X", (void*)&KEY_CONST_X); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int KEY_Z", (void*)&KEY_CONST_Z); AS_CHECK(r);

        //ai: full letter/digit/punctuation coverage — playable surfaces (the
        //   synth demo keyboard) need more than the curated specials above.
#define X(name) do { \
            static gX_I32 k_##name = gX_KEY_##name; \
            r = engine->RegisterGlobalProperty("const int KEY_" #name, (void*)&k_##name); AS_CHECK(r); \
        } while(0);
        X(E) X(F) X(G) X(H) X(I) X(J) X(K) X(L) X(N) X(O) X(P) X(Q) X(T) X(U) X(Y)
        X(ZERO) X(ONE) X(TWO) X(THREE) X(FOUR) X(FIVE) X(SIX) X(SEVEN) X(EIGHT) X(NINE)
        X(COMMA) X(PERIOD) X(MINUS) X(EQUAL) X(SLASH) X(SEMICOLON) X(APOSTROPHE)
#undef X
    }

    r = engine->RegisterGlobalFunction("void SwitchScene(int)",
        asFUNCTION(Script_SwitchScene), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered core API (math, Print, Yield, Sleep, keys, mouse, time, render scale, screenshot, scene, virtual input, GUI/DOM, shell harness)");
    return 0;
}

gX_I32 gX_Scripting_RegisterVec3Type(asIScriptEngine *engine) {
    int r = 0;

    //value type with APP_CLASS + ALLFLOATS so the engine knows the calling
    //   convention for returning by value (x86-64 SPLIT_OBJS_BY_MEMBER_TYPES
    //   requires ALLFLOATS/ALLINTS; three float members, no ctor/dtor).
    r = engine->RegisterObjectType("Vec3", sizeof(gX_F32v3),
        asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS | asOBJ_APP_CLASS_ALLFLOATS);
    if (r < 0) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING,
            "Vec3 type registration failed (code %d) — skipping", r);
        return r;
    }

    r = engine->RegisterObjectProperty("Vec3", "float x",
        offsetof(gX_F32v3, x)); AS_CHECK(r);
    r = engine->RegisterObjectProperty("Vec3", "float y",
        offsetof(gX_F32v3, y)); AS_CHECK(r);
    r = engine->RegisterObjectProperty("Vec3", "float z",
        offsetof(gX_F32v3, z)); AS_CHECK(r);

    r = engine->RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT,
        "void f()", asFUNCTION(Vec3_DefaultConstruct), asCALL_CDECL_OBJFIRST);
    AS_CHECK(r);
    r = engine->RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT,
        "void f(float, float, float)",
        asFUNCTION(Vec3_Construct), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT,
        "void f(const Vec3 &in)",
        asFUNCTION(Vec3_CopyConstruct), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterObjectMethod("Vec3", "Vec3 opAdd(const Vec3 &in) const",
        asFUNCTION(Vec3_OpAdd), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 opSub(const Vec3 &in) const",
        asFUNCTION(Vec3_OpSub), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 opMul(const Vec3 &in) const",
        asFUNCTION(Vec3_OpMul), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 opMul(float) const",
        asFUNCTION(Vec3_OpMulFloat), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 opDiv(const Vec3 &in) const",
        asFUNCTION(Vec3_OpDiv), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 opDiv(float) const",
        asFUNCTION(Vec3_OpDivFloat), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterObjectMethod("Vec3", "Vec3& opAddAssign(const Vec3 &in)",
        asFUNCTION(Vec3_OpAddAssign), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3& opSubAssign(const Vec3 &in)",
        asFUNCTION(Vec3_OpSubAssign), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3& opMulAssign(const Vec3 &in)",
        asFUNCTION(Vec3_OpMulAssign), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3& opDivAssign(const Vec3 &in)",
        asFUNCTION(Vec3_OpDivAssign), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterObjectMethod("Vec3", "bool opEquals(const Vec3 &in) const",
        asFUNCTION(Vec3_OpEquals), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterObjectMethod("Vec3", "Vec3 opNeg() const",
        asFUNCTION(Vec3_OpNeg), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterObjectMethod("Vec3", "float Length() const",
        asFUNCTION(Vec3_Length), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "float LengthSqr() const",
        asFUNCTION(Vec3_LengthSqr), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Normalize() const",
        asFUNCTION(Vec3_Normalize), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "float Dot(const Vec3 &in) const",
        asFUNCTION(Vec3_Dot), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Cross(const Vec3 &in) const",
        asFUNCTION(Vec3_Cross), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "float Distance(const Vec3 &in) const",
        asFUNCTION(Vec3_Distance), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Lerp(const Vec3 &in, float) const",
        asFUNCTION(Vec3_Lerp), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Reflect(const Vec3 &in) const",
        asFUNCTION(Vec3_Reflect), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Abs() const",
        asFUNCTION(Vec3_Abs), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Floor() const",
        asFUNCTION(Vec3_Floor), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Ceil() const",
        asFUNCTION(Vec3_Ceil), asCALL_CDECL_OBJFIRST); AS_CHECK(r);
    r = engine->RegisterObjectMethod("Vec3", "Vec3 Clamp(const Vec3 &in, const Vec3 &in) const",
        asFUNCTION(Vec3_Clamp), asCALL_CDECL_OBJFIRST); AS_CHECK(r);

    r = engine->RegisterGlobalFunction("Vec3 Vec3_Create(float, float, float)",
        asFUNCTION(Vec3_Create), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered Vec3 type (value type with operators)");
    return 0;
}

gX_I32 gX_Scripting_RegisterHookAPI(asIScriptEngine *engine) {
    int r = 0;

    //funcdef-based hook registration (the old "function" keyword type is not
    //   supported by AngelScript 2.37/2.38; a funcdef parameter arrives in the
    //   C++ function as asIScriptFunction*, which hooks.cpp stores + fires).
    r = engine->RegisterFuncdef("void HOOKCALLBACK()"); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void Hook(const string &in, HOOKCALLBACK @+)",
        asFUNCTION(Script_Hook), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TriggerHook(const string &in)",
        asFUNCTION(Script_TriggerHook), asCALL_CDECL); AS_CHECK(r);
    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered Hook API");
    return 0;
}
