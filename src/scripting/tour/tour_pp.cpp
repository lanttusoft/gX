/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_pp.cpp]
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


//ai: tour postprocess API — the embedded chain instance (renderer.pp): enable
//   toggle, built-in presets (incl. the battle transitions), the shader-chain
//   editing surface (push/pop/clear/remove/move/reload with PP_<TYPE>
//   constants), composite flags (feedback/inject/injectCanvas), feedback
//   decay/blend, inject slots (amount + texture paths) and the gameX
//   StartBattleTransition/StopPostProcess equivalents. Per-slot inject
//   transforms/colours are not bound — presets carry the layout.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <string>
#include <string.h>

#include "../../common/log.h"
#include "../../common/enum.h"
#include "../../main.h"
#include "../../renderer/renderer.h"
#include "../../renderer/postprocess.h"

// ===========================================================================
// Control
// ===========================================================================

static gX_RendererPostProcess *TourPostProcess_Instance(void) {
    return &renderer.pp;
}

static void TourPostProcess_SetEnabled(gX_I1 enable) {
    renderer.state.usePP = enable ? 1 : 0;
}

static gX_I1 TourPostProcess_IsEnabled(void) {
    return renderer.state.usePP;
}

static void TourPostProcess_LoadPreset(gX_I32 index) {
    gX_RendererPostProcess_Preset_BuiltIn(TourPostProcess_Instance(), index);
}

//ai: gameX StopPostProcess — disable the chain and clear its shaders.
static void TourPostProcess_Stop(void) {
    renderer.state.usePP = 0;
    gX_RendererPostProcess_Shaders_Clear(TourPostProcess_Instance());
}

// ===========================================================================
// Shader chain
// ===========================================================================

static void TourPostProcess_PushShader(gX_I32 type) {
    gX_RendererPostProcess_Shader_Push(TourPostProcess_Instance(), type);
}

static void TourPostProcess_PopShader(void) {
    gX_RendererPostProcess_Shader_Pop(TourPostProcess_Instance());
}

static void TourPostProcess_ClearShaders(void) {
    gX_RendererPostProcess_Shaders_Clear(TourPostProcess_Instance());
}

static void TourPostProcess_RemoveShader(gX_I32 index) {
    if (index >= 0) gX_RendererPostProcess_Shader_Remove(TourPostProcess_Instance(), (gX_U32)index);
}

static void TourPostProcess_MoveShader(gX_I32 index, gX_I32 delta) {
    if (index >= 0) gX_RendererPostProcess_Shader_Move(TourPostProcess_Instance(), (gX_U32)index, delta);
}

static void TourPostProcess_ReloadShader(gX_I32 index) {
    if (index >= 0) gX_RendererPostProcess_Shader_Reload(TourPostProcess_Instance(), (gX_U32)index);
}

static gX_I32 TourPostProcess_GetShaderCount(void) {
    return (gX_I32)renderer.pp.shadersAmount;
}

// ===========================================================================
// Composite flags + feedback
// ===========================================================================

static void TourPostProcess_SetFeedback(gX_I1 enable) {
    renderer.pp.state.feedback = enable ? 1 : 0;
}

static void TourPostProcess_SetInject(gX_I1 enable) {
    renderer.pp.state.inject = enable ? 1 : 0;
}

static void TourPostProcess_SetInjectCanvas(gX_I1 enable) {
    renderer.pp.state.injectCanvas = enable ? 1 : 0;
}

static void TourPostProcess_SetFeedbackDecay(gX_F32 decay) {
    renderer.pp.state.feedbackDecay = decay;
}

static void TourPostProcess_SetFeedbackBlend(gX_I32 mode) {
    renderer.pp.state.feedbackBlendMode = mode;
}

static void TourPostProcess_SetInjectCanvasBlend(gX_I32 mode) {
    renderer.pp.state.injectCanvasBlendMode = mode;
}

static void TourPostProcess_SetInjectCanvasTint(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    renderer.pp.state.injectCanvasTint = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

// ===========================================================================
// Inject slots
// ===========================================================================

static void TourPostProcess_SetInjectAmount(gX_I32 amount) {
    gX_RendererPostProcess *pp = TourPostProcess_Instance();
    if (amount < 0) amount = 0;
    if (amount > GX_PP_INJECT_SLOTS) amount = GX_PP_INJECT_SLOTS;
    pp->inject.amount = (gX_U32)amount;
}

//ai: inject-slot texture paths — the module stores subpaths relative to the
//   texture heap; the binding also accepts repo-relative paths carrying the
//   texture prefix (for consistency with the rest of the tour surface) and
//   normalizes them to the subpath the loader expects.
static const char *TourPostProcess_InjectSubpath(const std::string &path, std::string &storage) {
    const char *prefix = "resources/texture/";
    size_t prefixLen = strlen(prefix);
    if (path.compare(0, prefixLen, prefix) == 0) {
        storage = path.substr(prefixLen);
        return storage.c_str();
    }
    return path.c_str();
}

static void TourPostProcess_SetInjectTexture(gX_I32 slot, const std::string &path) {
    std::string subpath;
    gX_RendererPostProcess_Inject_SetTexturePath(TourPostProcess_Instance(), slot,
        TourPostProcess_InjectSubpath(path, subpath));
}

static void TourPostProcess_ClearInjectTexture(gX_I32 slot) {
    gX_RendererPostProcess_Inject_SetTexturePath(TourPostProcess_Instance(), slot, "");
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-pp registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourPostProcess_Register(asIScriptEngine *engine) {
    int r = 0;

    // --- Control ---
    r = engine->RegisterGlobalFunction("void PostProcess_SetEnabled(bool)",
        asFUNCTION(TourPostProcess_SetEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool PostProcess_IsEnabled()",
        asFUNCTION(TourPostProcess_IsEnabled), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_LoadPreset(int)",
        asFUNCTION(TourPostProcess_LoadPreset), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_Stop()",
        asFUNCTION(TourPostProcess_Stop), asCALL_CDECL); AS_CHECK(r);

    // --- Shader chain ---
    r = engine->RegisterGlobalFunction("void PostProcess_PushShader(int)",
        asFUNCTION(TourPostProcess_PushShader), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_PopShader()",
        asFUNCTION(TourPostProcess_PopShader), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_ClearShaders()",
        asFUNCTION(TourPostProcess_ClearShaders), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_RemoveShader(int)",
        asFUNCTION(TourPostProcess_RemoveShader), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_MoveShader(int, int)",
        asFUNCTION(TourPostProcess_MoveShader), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_ReloadShader(int)",
        asFUNCTION(TourPostProcess_ReloadShader), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int PostProcess_GetShaderCount()",
        asFUNCTION(TourPostProcess_GetShaderCount), asCALL_CDECL); AS_CHECK(r);

    // --- Composite flags + feedback ---
    r = engine->RegisterGlobalFunction("void PostProcess_SetFeedback(bool)",
        asFUNCTION(TourPostProcess_SetFeedback), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetInject(bool)",
        asFUNCTION(TourPostProcess_SetInject), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetInjectCanvas(bool)",
        asFUNCTION(TourPostProcess_SetInjectCanvas), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetFeedbackDecay(float)",
        asFUNCTION(TourPostProcess_SetFeedbackDecay), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetFeedbackBlend(int)",
        asFUNCTION(TourPostProcess_SetFeedbackBlend), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetInjectCanvasBlend(int)",
        asFUNCTION(TourPostProcess_SetInjectCanvasBlend), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetInjectCanvasTint(int, int, int, int)",
        asFUNCTION(TourPostProcess_SetInjectCanvasTint), asCALL_CDECL); AS_CHECK(r);

    // --- Inject slots ---
    r = engine->RegisterGlobalFunction("void PostProcess_SetInjectAmount(int)",
        asFUNCTION(TourPostProcess_SetInjectAmount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_SetInjectTexture(int, const string &in)",
        asFUNCTION(TourPostProcess_SetInjectTexture), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void PostProcess_ClearInjectTexture(int)",
        asFUNCTION(TourPostProcess_ClearInjectTexture), asCALL_CDECL); AS_CHECK(r);

    // --- Constants ---
    {
        static gX_I32 cPresetTest = gX_PP_PRESET_TEST;
        static gX_I32 cPresetBlur = gX_PP_PRESET_BLUR;
        static gX_I32 cPresetStrongBlur = gX_PP_PRESET_STRONGBLUR;
        static gX_I32 cPresetBattle = gX_PP_PRESET_BATTLE;
        static gX_I32 cPresetBattle2 = gX_PP_PRESET_BATTLE2;
        static gX_I32 cPresetVignette = gX_PP_PRESET_VIGNETTE;
        static gX_I32 cPresetPixelation = gX_PP_PRESET_PIXELATION;
        r = engine->RegisterGlobalProperty("const int PP_PRESET_TEST", (void *)&cPresetTest); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_BLUR", (void *)&cPresetBlur); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_STRONGBLUR", (void *)&cPresetStrongBlur); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_BATTLE", (void *)&cPresetBattle); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_BATTLE2", (void *)&cPresetBattle2); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_VIGNETTE", (void *)&cPresetVignette); AS_CHECK(r);
        r = engine->RegisterGlobalProperty("const int PP_PRESET_PIXELATION", (void *)&cPresetPixelation); AS_CHECK(r);

#define X(blend) do { \
            static gX_I32 c_blend_##blend = gX_BLEND_##blend; \
            r = engine->RegisterGlobalProperty("const int PP_BLEND_" #blend, (void *)&c_blend_##blend); AS_CHECK(r); \
        } while(0);
        gX_BLENDMODES
#undef X

#define X(type) do { \
            static gX_I32 c_type_##type = gX_PP_##type; \
            r = engine->RegisterGlobalProperty("const int PP_" #type, (void *)&c_type_##type); AS_CHECK(r); \
        } while(0);
        gX_RENDERERPP_SHADERTYPES
#undef X
    }

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour postprocess API (presets, chain, feedback/inject)");
    return 0;
}
