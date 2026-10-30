/**********************************************************************************************
*
*   [gX: src/main_tests.c]
*   Test runner
*
*   Copyright (c) 2026 lanttu
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "common/alloc.h"
#include "common/heap.h"
#include "common/log.h"
#include "common/types/3d.h"
#include "module/devcube_phase.h"
#include "common/marshal/json_impl.h"
#include "gui/element/textinput.h"
#include "common/shader_compiler.h"
#include "common/hardware.h"
#include "common/polisher.h"

#include "gui/css.h"
#include "gui/canvas.h"
#include "gui/element/struct.h"
#include "gui/element/element.h"
#include "gui/gui.h"
#include "gui/markup.h"
#include "gui/style.h"
#include "gui/binding.h"
#include "gui/builder.h"
#include "gui/layout.h"
#include "gui/enum.h"
#include "gxmath/constants.h"
#include "input.h"
#include "common/enum.h"
#include "settings.h"
#include "wrappers/text.h"

//opencode: release-lean guard — the unit-test suite is a development gate
//   (make check runs it on the Debug build); Release keeps only a stub so the
//   runner and its reachable helpers stay out of the payload. The other test
//   translation units become unreachable and are dropped by --gc-sections.
#ifdef GX_LEAN_RELEASE
gX_I32 gX_Tests(gX_I0) {
    gX_TraceLog(gX_LOG_NOTICE,
        "TESTS: not built into this release (use a Debug build)");
    return 0;
}
#else
//opencode: per-process temp path — parallel suite instances (concurrent audit
//   captures) must not clobber each other's round-trip files.
static gX_I0 gX_Tests_TempPath(const gX_I8 *name, gX_I8 *out, size_t size)
{
    snprintf((char *)out, size, "/tmp/gx_test_%ld_%s", (long)getpid(), name);
}

extern gX_I0 gX_GUI_Test_Dock_RTs_WindowSize(gX_I0);
extern gX_I32 gX_Tools_Editor_TestInputCapture(gX_I0);
extern gX_I32 gX_Tools_Editor_TestSnapshotRoundTrip(gX_I0);

//opencode: math tests
gX_I32 gX_Tests_Math(gX_I0);
//opencode: math-pass corpus dump (GX_MATH_DUMP=1) — verified by scripts/math_pass.py
gX_I32 gX_Tests_MathDump(gX_I0);
//ai: physics wrapper integration tests (src/physics/physics_tests.c)
gX_I32 gX_Tests_Physics(gX_I0);
//ai: toolgun framework suite (mode state, tool cycling) — headless.
gX_I32 gX_Tests_Toolgun(gX_I0);
//ai: synth engine offline-render tests (src/audio/synth_tests.c)
gX_I32 gX_Tests_Synth(gX_I0);
//ai: procedural SFX pool + mixer/reverb CPU tests (src/audio/sfx_tests.c)
gX_I32 gX_Tests_AudioSFX(gX_I0);
//ai: OPX 8-operator FM engine offline suite (src/audio/opx_tests.c)
gX_I32 gX_Tests_OPX(gX_I0);
//ai: PAD layered ensemble engine offline suite (src/audio/pad_tests.c)
gX_I32 gX_Tests_Pad(gX_I0);
//ai: shared filter core + master filter suite (src/audio/filter_tests.c)
gX_I32 gX_Tests_AudioFilter(gX_I0);

//opencode: background renderer CPU tests (param oscillators, transitions, imgpro)
//   Cluster-gated: renderer/background/tests.c compiles only in full builds.
#if gX_ENABLE_RENDERER == 1
gX_I32 gX_Tests_Background(gX_I0);
gX_I32 gX_Tests_Raymarch(gX_I0);
//opencode: textwriter parser tests (TXT loader, tags, capacity guards)
gX_I32 gX_Tests_TextWriter(gX_I0);
gX_I32 gX_Tests_Sky(gX_I0);
gX_I32 gX_Tests_PostProcess(gX_I0);
gX_I32 gX_Tests_Battle(gX_I0);
gX_I32 gX_Tests_ScenePhysics(gX_I0);
//ai: impact-audio palette mapping (pure; no world/renderer/audio device)
gX_I32 gX_Tests_SceneImpactSound(gX_I0);
gX_I32 gX_Tests_SceneSynthVis(gX_I0);
gX_I32 gX_Tests_Renderer_SceneTransition(gX_I0);
#include "renderer/gfx/light.h"
#include "renderer/gfx/temporal_history.h"
#endif

//opencode: temporal-history state-machine tests
#if gX_ENABLE_RENDERER == 1
//ai: compare fields rather than padding when checking the Lean temporal-history state contract.
static gX_I1 gX_Tests_TemporalHistory_StateEqual(
    gX_RendererGFX_TemporalHistory Left, gX_RendererGFX_TemporalHistory Right)
{
    return Left.frameIndex == Right.frameIndex && Left.historyValid == Right.historyValid
        && Left.clearPending == Right.clearPending && Left.passActive == Right.passActive
        && Left.parity == Right.parity;
}

//ai: expose the exact counter/flag/loading/pointer witness for a native contract mismatch.
static gX_I32 gX_Tests_TemporalHistory_ContractFailure(const gX_I8 *Stage,
    gX_I32 Frame, gX_U32 Flags, gX_U32 Loaded, gX_U32 Pointers)
{
    gX_TraceLog(gX_LOG_INFO,"TEMPORAL HISTORY contract FAIL: %s frame=%d flags=%u loaded=%u pointers=%u",
        Stage,Frame,Flags,Loaded,Pointers);
    return 1;
}

//ai: exhaust the Boolean guard/state space and signed-boundary witnesses without ever overflowing C.
static gX_I32 gX_Tests_TemporalHistory_ContractMatrix(gX_I0)
{
    const gX_I32 Frames[] = {0,7,2147483646,2147483647};
    gX_RendererGFX_RenderTexture TargetA = {0};
    gX_RendererGFX_RenderTexture TargetB = {0};
    gX_RendererGFX_TemporalHistory Initial = {.clearPending=true};
    gX_RendererGFX_TemporalHistory Initialized = {0};
    gX_RendererGFX_TemporalHistory_Init(&Initialized);
    if (!gX_Tests_TemporalHistory_StateEqual(Initialized,Initial))
        return gX_Tests_TemporalHistory_ContractFailure("init",0,0,0,0);
    gX_U32 BeginCases = 0;
    gX_U32 BackingCases = 0;
    for (gX_U32 FrameCase = 0; FrameCase < 4; FrameCase++)
    {
        for (gX_U32 Flags = 0; Flags < 16; Flags++)
        {
            gX_RendererGFX_TemporalHistory Input = {
                .frameIndex = Frames[FrameCase],.historyValid = (Flags & 1) != 0,
                .clearPending = (Flags & 2) != 0,.passActive = (Flags & 4) != 0,
                .parity = (Flags & 8) != 0};
            gX_RendererGFX_TemporalHistory Actual = Input;
            gX_RendererGFX_TemporalHistory Expected = Input;
            gX_RendererGFX_TemporalHistory_Reset(&Actual);
            if (!gX_Tests_TemporalHistory_StateEqual(Actual,Initial))
                return gX_Tests_TemporalHistory_ContractFailure("reset",Input.frameIndex,Flags,0,0);
            Actual = Input;
            Expected.clearPending = false;
            Expected.historyValid = false;
            gX_RendererGFX_TemporalHistory_MarkCleared(&Actual);
            if (!gX_Tests_TemporalHistory_StateEqual(Actual,Expected))
                return gX_Tests_TemporalHistory_ContractFailure("clear",Input.frameIndex,Flags,0,0);
            Actual = Input;
            Expected = Input;
            if (Input.passActive)
            {
                Expected.frameIndex = Input.frameIndex == 2147483647 ? 0 : Input.frameIndex + 1;
                Expected.historyValid = true;
                Expected.passActive = false;
                Expected.parity = !Input.parity;
            }
            gX_RendererGFX_TemporalHistory_CommitPass(&Actual);
            if (!gX_Tests_TemporalHistory_StateEqual(Actual,Expected)
                || gX_RendererGFX_TemporalHistory_CurrentTarget(&Actual,&TargetA,&TargetB)
                    != (Expected.parity ? &TargetB : &TargetA))
                return gX_Tests_TemporalHistory_ContractFailure("commit",Input.frameIndex,Flags,0,0);
            gX_RendererGFX_TemporalHistory_CommitPass(&Actual);
            if (!gX_Tests_TemporalHistory_StateEqual(Actual,Expected))
                return gX_Tests_TemporalHistory_ContractFailure("double commit",Input.frameIndex,Flags,0,0);
            Actual = Input;
            Expected = Input;
            Expected.passActive = false;
            gX_RendererGFX_TemporalHistory_AbortPass(&Actual);
            gX_RendererGFX_TemporalHistory_CommitPass(&Actual);
            if (!gX_Tests_TemporalHistory_StateEqual(Actual,Expected))
                return gX_Tests_TemporalHistory_ContractFailure("abort then commit",Input.frameIndex,Flags,0,0);
            TargetA.wrapper.rt.id = 1;
            TargetB.wrapper.rt.id = 2;
            TargetA.wrapper.rt.texture.id = 1;
            TargetB.wrapper.rt.texture.id = 2;
            for (gX_U32 Loaded = 0; Loaded < 4; Loaded++)
            {
                TargetA.wrapper.loadingState.loaded = (Loaded & 1) != 0;
                TargetB.wrapper.loadingState.loaded = (Loaded & 2) != 0;
                for (gX_U32 Pointers = 0; Pointers < 16; Pointers++)
                {
                    Actual = Input;
                    Expected = Input;
                    gX_RendererGFX_TemporalHistoryTargets Targets = {&TargetB,&TargetB};
                    gX_I1 Accept = !Input.clearPending && !Input.passActive && Loaded == 3 && Pointers == 15;
                    gX_I1 Accepted = gX_RendererGFX_TemporalHistory_BeginPass(
                        (Pointers & 1) ? &Actual : NULL,(Pointers & 2) ? &TargetA : NULL,
                        (Pointers & 4) ? &TargetB : NULL,(Pointers & 8) ? &Targets : NULL);
                    if (Accept) Expected.passActive = true;
                    if (Accepted != Accept || !gX_Tests_TemporalHistory_StateEqual(Actual,Expected)
                        || Targets.read != (Accept ? (Input.parity ? &TargetB : &TargetA) : &TargetB)
                        || Targets.write != (Accept ? (Input.parity ? &TargetA : &TargetB) : &TargetB))
                        return gX_Tests_TemporalHistory_ContractFailure("begin",Input.frameIndex,Flags,Loaded,Pointers);
                    BeginCases++;
                }
            }
            TargetA.wrapper.loadingState.loaded = true;
            TargetB.wrapper.loadingState.loaded = true;
            for (gX_U32 Backing = 0; Backing < 81; Backing++)
            {
                TargetA.wrapper.rt.id = Backing % 3;
                TargetB.wrapper.rt.id = (Backing / 3) % 3;
                TargetA.wrapper.rt.texture.id = (Backing / 9) % 3;
                TargetB.wrapper.rt.texture.id = (Backing / 27) % 3;
                Actual = Input;
                Expected = Input;
                gX_RendererGFX_TemporalHistoryTargets Targets = {&TargetB,&TargetB};
                gX_I1 Accept = !Input.clearPending && !Input.passActive
                    && TargetA.wrapper.rt.id && TargetB.wrapper.rt.id
                    && TargetA.wrapper.rt.texture.id && TargetB.wrapper.rt.texture.id
                    && TargetA.wrapper.rt.id != TargetB.wrapper.rt.id
                    && TargetA.wrapper.rt.texture.id != TargetB.wrapper.rt.texture.id;
                gX_I1 Accepted = gX_RendererGFX_TemporalHistory_BeginPass(&Actual,&TargetA,&TargetB,&Targets);
                if (Accept) Expected.passActive = true;
                if (Accepted != Accept || !gX_Tests_TemporalHistory_StateEqual(Actual,Expected)
                    || Targets.read != (Accept ? (Input.parity ? &TargetB : &TargetA) : &TargetB)
                    || Targets.write != (Accept ? (Input.parity ? &TargetA : &TargetB) : &TargetB))
                    return gX_Tests_TemporalHistory_ContractFailure("backing",Input.frameIndex,Flags,Backing,15);
                BackingCases++;
            }
        }
    }
    gX_RendererGFX_TemporalHistory History = Initial;
    History.frameIndex = 2147483647;
    gX_RendererGFX_TemporalHistory_CommitPass(&History);
    if (History.frameIndex != 2147483647)
        return gX_Tests_TemporalHistory_ContractFailure("idle signed maximum",History.frameIndex,0,0,0);
    gX_RendererGFX_TemporalHistory_Init(NULL);
    gX_RendererGFX_TemporalHistory_Reset(NULL);
    gX_RendererGFX_TemporalHistory_MarkCleared(NULL);
    gX_RendererGFX_TemporalHistory_AbortPass(NULL);
    gX_RendererGFX_TemporalHistory_CommitPass(NULL);
    if (gX_RendererGFX_TemporalHistory_NeedsClear(NULL)
        || gX_RendererGFX_TemporalHistory_FrameIndex(NULL) != 0
        || gX_RendererGFX_TemporalHistory_CurrentTarget(NULL,&TargetA,&TargetB) != &TargetA)
        return gX_Tests_TemporalHistory_ContractFailure("null queries",0,0,0,0);
    History = Initial;
    gX_RendererGFX_TemporalHistory_MarkCleared(&History);
    TargetA.wrapper.loadingState.loaded = true;
    TargetA.wrapper.rt.id = 1;
    TargetA.wrapper.rt.texture.id = 1;
    gX_RendererGFX_TemporalHistory Before = History;
    gX_RendererGFX_TemporalHistoryTargets Targets = {&TargetB,&TargetB};
    if (gX_RendererGFX_TemporalHistory_BeginPass(&History,&TargetA,&TargetA,&Targets)
        || !gX_Tests_TemporalHistory_StateEqual(History,Before)
        || Targets.read != &TargetB || Targets.write != &TargetB)
        return gX_Tests_TemporalHistory_ContractFailure("same wrapper rejection",0,0,0,0);
    gX_TraceLog(gX_LOG_INFO,"TEMPORAL HISTORY contract matrix: %u begin and %u backing cases passed",BeginCases,BackingCases);
    return 0;
}

static gX_I32 gX_Tests_TemporalHistory(gX_I0) {
    gX_I32 errors = 0;
    gX_RendererGFX_TemporalHistory history = {0};
    gX_RendererGFX_RenderTexture targetA = {0};
    gX_RendererGFX_RenderTexture targetB = {0};
    gX_RendererGFX_TemporalHistoryTargets targets = {0};
    targetA.wrapper.loadingState.loaded = true;
    targetB.wrapper.loadingState.loaded = true;
    targetA.wrapper.rt.id = 1;
    targetB.wrapper.rt.id = 2;
    targetA.wrapper.rt.texture.id = 1;
    targetB.wrapper.rt.texture.id = 2;

    gX_RendererGFX_TemporalHistory_Init(&history);
    if (!gX_RendererGFX_TemporalHistory_NeedsClear(&history)
        || gX_RendererGFX_TemporalHistory_FrameIndex(&history) != 0
        || gX_RendererGFX_TemporalHistory_BeginPass(
            &history, &targetA, &targetB, &targets)) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 1 FAIL: initial state");
        errors++;
    }

    gX_RendererGFX_TemporalHistory_MarkCleared(&history);
    if (gX_RendererGFX_TemporalHistory_NeedsClear(&history)
        || !gX_RendererGFX_TemporalHistory_BeginPass(
            &history, &targetA, &targetB, &targets)
        || targets.read != &targetA || targets.write != &targetB) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 2 FAIL: first target pair");
        errors++;
    }
    gX_RendererGFX_TemporalHistory_CommitPass(&history);
    if (gX_RendererGFX_TemporalHistory_FrameIndex(&history) != 1
        || !history.historyValid
        || gX_RendererGFX_TemporalHistory_CurrentTarget(
            &history, &targetA, &targetB) != &targetB) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 3 FAIL: commit state");
        errors++;
    }

    if (!gX_RendererGFX_TemporalHistory_BeginPass(
            &history, &targetA, &targetB, &targets)
        || targets.read != &targetB || targets.write != &targetA) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 4 FAIL: swapped target pair");
        errors++;
    }
    gX_RendererGFX_TemporalHistory_AbortPass(&history);
    if (gX_RendererGFX_TemporalHistory_FrameIndex(&history) != 1
        || gX_RendererGFX_TemporalHistory_CurrentTarget(
            &history, &targetA, &targetB) != &targetB) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 5 FAIL: abort changed state");
        errors++;
    }

    gX_RendererGFX_TemporalHistory_Reset(&history);
    if (!gX_RendererGFX_TemporalHistory_NeedsClear(&history)
        || history.historyValid
        || gX_RendererGFX_TemporalHistory_FrameIndex(&history) != 0
        || gX_RendererGFX_TemporalHistory_CurrentTarget(
            &history, &targetA, &targetB) != &targetA) {
        gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY 6 FAIL: reset state");
        errors++;
    }
    errors += gX_Tests_TemporalHistory_ContractMatrix();
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "TEMPORAL HISTORY tests: all passed");
    return errors;
}

//opencode: pure point-light path checks run headless without a GL context.
static gX_I32 gX_Tests_LightOrbit(gX_I0) {
    gX_I32 errors = 0;
    gX_RendererGFX_Light light = {0};
    gX_RendererGFX_Light_Init(&light);
    light.type = 1;
    light.orbitEnabled = 1;
    light.orbitPlane = 1;
    light.orbitCenter = (gX_F32v3){3.0f, 4.0f, 5.0f};
    light.orbitRadius = 2.0f;
    light.orbitSpeed = 360.0f;
    light.orbitLoop = 1;
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (fabsf(light.pos.x - 3.0f) > 0.001f
        || fabsf(light.pos.y - 4.0f) > 0.001f
        || fabsf(light.pos.z - 7.0f) > 0.001f
        || fabs(light.orbitElapsed - 0.25) > 0.000001) errors++;
    light.orbitPlane = 0;
    light.orbitElapsed = 0.0;
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (fabsf(light.pos.x - 3.0f) > 0.001f
        || fabsf(light.pos.y - 6.0f) > 0.001f
        || fabsf(light.pos.z - 5.0f) > 0.001f) errors++;
    gX_F32v3 pausedPosition = light.pos;
    light.orbitPaused = 1;
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (fabsf(light.pos.x - pausedPosition.x) > 0.001f
        || fabsf(light.pos.y - pausedPosition.y) > 0.001f
        || fabsf(light.pos.z - pausedPosition.z) > 0.001f
        || fabs(light.orbitElapsed - 0.25) > 0.000001) errors++;
    light.orbitEnabled = 0;
    light.pos = (gX_F32v3){9.0f, 8.0f, 7.0f};
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (fabsf(light.pos.x - 9.0f) > 0.001f
        || fabsf(light.pos.y - 8.0f) > 0.001f
        || fabsf(light.pos.z - 7.0f) > 0.001f) errors++;
    light.orbitEnabled = 1;
    light.orbitPaused = 0;
    light.orbitLoop = 0;
    light.orbitElapsed = 0.0;
    light.orbitSpeed = 720.0f;
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (light.orbitPaused || fabs(light.orbitElapsed - 0.25) > 0.000001) errors++;
    gX_RendererGFX_Light_Orbit_Update(&light, 0.25);
    if (!light.orbitPaused || fabs(light.orbitElapsed - 0.5) > 0.000001
        || fabsf(light.pos.x - light.orbitCenter.x - light.orbitRadius) > 0.001f)
        errors++;
    gX_TraceLog(errors == 0 ? gX_LOG_INFO : gX_LOG_ERROR,
        "GFX LIGHT ORBIT tests: %s", errors == 0 ? "all passed" : "failed");
    return errors;
}
#endif

//opencode: input tests — bind aliases, release edges, named lookup, and gamepad mappings.
static gX_I32 gX_Tests_Input(gX_I0) {
    gX_I32 errors = 0;
    gX_Input savedInput = input;
    input = (gX_Input){0};
    input.bindAmount = gX_BIND_COMMON_AMOUNT;
    gX_Input_Bind_ClearAll();

    gX_Input_Bind_Set(0, gX_KEY_W, 0);
    gX_Input_Bind_Set(0, gX_KEY_UP, 1);
    input.keyboard.keys[gX_KEY_W].state.DOWN = true;
    input.keyboard.keys[gX_KEY_W].state.PRESSED = true;
    input.keyboard.keys[gX_KEY_UP].state.UP = true;
    gX_Input_Bind_Process(&input.binds[0]);
    if (!input.binds[0].state.DOWN || !input.binds[0].state.PRESSED
        || input.binds[0].state.UP || input.binds[0].state.RELEASED) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 1 FAIL: alias state aggregation");
        errors++;
    }

    input.keyboard = (gX_Input_Keyboard){0};
    input.keyboard.keys[gX_KEY_UP].state.RELEASED = true;
    gX_Input_Bind_Process(&input.binds[0]);
    if (!input.binds[0].state.UP || !input.binds[0].state.RELEASED) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 2 FAIL: release state");
        errors++;
    }

    input.gamepad[0].available = true;
    input.gamepad[0].buttons[GX_GAMEPAD_FACE_SOUTH].DOWN = true;
    input.gamepad[0].buttons[GX_GAMEPAD_FACE_SOUTH].PRESSED = true;
    input.gamepadBindMap[1] = GX_GAMEPAD_FACE_SOUTH;
    gX_Input_Bind_Process(&input.binds[1]);
    if (!input.binds[1].state.DOWN || !input.binds[1].state.PRESSED
        || input.binds[1].state.UP) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 3 FAIL: gamepad button mapping");
        errors++;
    }

    input.gamepad[0] = (gX_Input_Gamepad){0};
    input.gamepad[0].available = true;
    input.gamepad[0].axisCount = 2;
    input.gamepad[0].axisMovement[0] = 0.8f;
    input.gamepadBindMap[gX_BIND_MOVE_RIGHT] = GX_GAMEPAD_AXIS_LX;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MOVE_RIGHT]);
    if (!input.binds[gX_BIND_MOVE_RIGHT].state.DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 4 FAIL: positive stick direction");
        errors++;
    }
    input.gamepad[0].axisMovement[0] = -0.8f;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MOVE_RIGHT]);
    if (input.binds[gX_BIND_MOVE_RIGHT].state.DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 5 FAIL: opposite stick direction");
        errors++;
    }

    gX_Input_Bind_Set(gX_BIND_MENU, gX_KEY_ESCAPE, 0);
    input.keyboard.keys[gX_KEY_ESCAPE].state.DOWN = true;
    input.keyboard.keys[gX_KEY_ESCAPE].state.PRESSED = true;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MENU]);
    if (!input.binds[gX_BIND_MENU].state.DOWN || !input.binds[gX_BIND_MENU].state.PRESSED) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 6 FAIL: terminal standard bind");
        errors++;
    }

    input.gamepad[0].axisMovement[1] = -0.8f;
    input.gamepadBindMap[gX_BIND_MOVE_FORWARD] = GX_GAMEPAD_AXIS_LY;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MOVE_FORWARD]);
    if (!input.binds[gX_BIND_MOVE_FORWARD].state.DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 7 FAIL: forward stick direction");
        errors++;
    }
    input.gamepadBindMap[gX_BIND_MOVE_BACKWARD] = GX_GAMEPAD_AXIS_LY;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MOVE_BACKWARD]);
    if (input.binds[gX_BIND_MOVE_BACKWARD].state.DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 8 FAIL: backward stick direction");
        errors++;
    }
    input.gamepad[0].axisMovement[1] = 0.8f;
    gX_Input_Bind_Process(&input.binds[gX_BIND_MOVE_BACKWARD]);
    if (!input.binds[gX_BIND_MOVE_BACKWARD].state.DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 9 FAIL: reverse stick direction");
        errors++;
    }

    gX_Input_Bind_SetName(&input.binds[gX_BIND_CONFIRM], "confirm");
    input.binds[gX_BIND_CONFIRM].state.PRESSED = true;
    gX_Input_State named = gX_Input_Get_Bind_State_Name("confirm");
    gX_Input_State missing = gX_Input_Get_Bind_State_Name("missing");
    if (!named.PRESSED || missing.DOWN || gX_Input_Get_Bind_State_Name(NULL).DOWN) {
        gX_TraceLog(gX_LOG_INFO, "INPUT 10 FAIL: named bind lookup");
        errors++;
    }

    input = savedInput;
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "INPUT tests: all passed");
    return errors;
}

//opencode: scene input tests — persisted remaps survive scene bind rebuilds.
static gX_I32 gX_Tests_Input_Transitions(gX_I0) {
    gX_I32 errors = 0;
    gX_Input savedInput = input;
    gX_Settings savedSettings = settings;
    gX_I32 savedScene = maindata.masterIndex;
    input = (gX_Input){0};
    input.bindAmount = gX_BIND_COMMON_AMOUNT;
    for (gX_I32 i = 0; i < gX_BIND_COMMON_AMOUNT; i++) {
        settings.bindKeys[i][0] = -1;
        settings.bindKeys[i][1] = -1;
    }
    gX_Input_Bind_SetPersistent(gX_BIND_MOVE_FORWARD, gX_KEY_I, 0);
    gX_Input_Bind_SetPersistent(gX_BIND_MOVE_FORWARD, -1, 1);
    if (!settings.dirtyFlag) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 0 FAIL: remap did not mark settings dirty");
        errors++;
    }
    input.bindCaptureActive = true;
    input.bindCaptureIndex = gX_BIND_MOVE_UP;
    input.bindCaptureSlot = 0;
    if (!gX_Input_Bind_CaptureKey(gX_KEY_J)
        || input.bindCaptureActive
        || settings.bindKeys[gX_BIND_MOVE_UP][0] != gX_KEY_J) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 0b FAIL: capture commit");
        errors++;
    }
    maindata.masterIndex = gX_SCENE_TITLE;
    gX_Input_Respond();
    if (input.binds[gX_BIND_MOVE_FORWARD].keysAmount != 1
        || input.binds[gX_BIND_MOVE_FORWARD].keys[0] != gX_KEY_I) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 1 FAIL: title remap");
        errors++;
    }
    maindata.masterIndex = gX_SCENE_RENDERER_GFX;
    gX_Input_Respond();
    if (input.binds[gX_BIND_MOVE_FORWARD].keysAmount != 1
        || input.binds[gX_BIND_MOVE_FORWARD].keys[0] != gX_KEY_I
        || input.binds[gX_BIND_MOVE_FORWARD].keys[1] != 0) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 2 FAIL: remap after scene switch");
        errors++;
    }
    maindata.masterIndex = gX_SCENE_RENDERER_WIREFRAME;
    gX_Input_Respond();
    if (input.gamepadBindMap[gX_BIND_MOVE_FORWARD] != GX_GAMEPAD_AXIS_LY
        || input.binds[gX_BIND_MOVE_FORWARD].keys[0] != gX_KEY_I) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 3 FAIL: free-camera remap");
        errors++;
    }
    maindata.masterIndex = gX_SCENE_TITLE;
    gX_Input_Respond();
    if (input.gamepadBindMap[gX_BIND_MOVE_FORWARD] != -1) {
        gX_TraceLog(gX_LOG_INFO, "INPUT TRANS 4 FAIL: stale gamepad mapping");
        errors++;
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "INPUT transition tests: all passed");
    input = savedInput;
    settings = savedSettings;
    maindata.masterIndex = savedScene;
    return errors;
}

//opencode: v0.6.0 — GUI word-wrap tests (headless: mock glyph measurement)
typedef gX_F32 (*gX_GUI_TextMeasureFn)(const gX_I8 *segment, gX_I0 *userData);
extern gX_I32 gX_GUI_Element_Text_Wrap(const gX_I8 *text, gX_F32 maxWidth,
                                       gX_GUI_TextMeasureFn measure, gX_I0 *userData,
                                       gX_I8 *out, gX_U32 outSize);
static gX_F32 gX_Test_Wrap_Measure(const gX_I8 *segment, gX_I0 *userData) {
    (void)userData;
    gX_F32 w = 0.0f;
    for (const gX_U8 *p = (const gX_U8*)segment; *p; ) {
        if ((*p & 0x80) == 0) { w += 8.0f; p++; }
        else { w += 16.0f; p += ((*p & 0xE0) == 0xE0) ? 3 : 2; }
    }
    return w;
}
static gX_I32 gX_Tests_Wrap(gX_I0) {
    gX_I32 errors = 0;
    gX_I8 out[512];
    const gX_I8 *in;
    const gX_I8 *want;

    in = "the quick brown fox jumps over";
    want = "the quick\nbrown fox\njumps over";
    gX_GUI_Element_Text_Wrap(in, 80.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 1 FAIL: got [%s] want [%s]", out, want); errors++; }

    in = "1234567890";
    want = "1234567890";
    gX_GUI_Element_Text_Wrap(in, 80.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 2 FAIL: got [%s]", out); errors++; }

    in = "abcdefghijklmnop";
    want = "abcdefghijklmnop";
    gX_GUI_Element_Text_Wrap(in, 80.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 3 FAIL: got [%s]", out); errors++; }

    in = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c" "abc"; // 你好世界abc
    want = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\n" "\xe7\x95\x8c" "abc"; // 你好世\n界abc
    gX_GUI_Element_Text_Wrap(in, 48.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 4 (CJK) FAIL: got [%s]", out); errors++; }

    in = "ab\ncd";
    want = "ab\ncd";
    gX_GUI_Element_Text_Wrap(in, 200.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 5 FAIL: got [%s]", out); errors++; }

    in = "aa   bb";
    want = "aa   bb";
    gX_GUI_Element_Text_Wrap(in, 200.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out));
    if (strcmp(out, want) != 0) { gX_TraceLog(gX_LOG_INFO, "WRAP 6 (spaces) FAIL: got [%s]", out); errors++; }

    in = "";
    if (gX_GUI_Element_Text_Wrap(in, 80.0f, gX_Test_Wrap_Measure, NULL, out, sizeof(out)) != 0 || out[0] != '\0')
        { gX_TraceLog(gX_LOG_INFO, "WRAP 7 (empty) FAIL"); errors++; }

    in = "abcdefghij";
    gX_GUI_Element_Text_Wrap(in, 200.0f, gX_Test_Wrap_Measure, NULL, out, 6);
    if (strlen(out) >= 6 || out[5] != '\0') { gX_TraceLog(gX_LOG_INFO, "WRAP 8 (cap) FAIL: got [%s]", out); errors++; }

    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI WRAP tests: all passed");
    return errors;
}

//opencode: draw-truth text layout — the shrink floor is pinned font-free,
//   and the full analyzer (wrap/truncate/overflow) runs when a font is
//   available (the GPU layout gate covers the windowed measurement path).
static gX_F32 gX_Test_F32_Delta(gX_F32 a, gX_F32 b);

static gX_I32 gX_Tests_GUI_TextLayout(gX_I0)
{
    gX_I32 errors = 0;

    // pure fit-scale math: exact fit, floor clamp, and no-op cases
    if (gX_Test_F32_Delta(gX_GUI_Element_Text_FitScale(100.0f, 120.0f), 1.0f) > 0.0001f
        || gX_Test_F32_Delta(gX_GUI_Element_Text_FitScale(150.0f, 120.0f), 0.8f) > 0.0001f
        || gX_Test_F32_Delta(gX_GUI_Element_Text_FitScale(240.0f, 120.0f),
            gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE) > 0.0001f
        || gX_Test_F32_Delta(gX_GUI_Element_Text_FitScale(0.0f, 120.0f), 1.0f) > 0.0001f
        || gX_Test_F32_Delta(gX_GUI_Element_Text_FitScale(150.0f, 0.0f), 1.0f) > 0.0001f) {
        gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT 1 FAIL: fit scale floor math");
        errors++;
    }

    //opencode: raylib's default font exists only after window init, so the
    //   measurement-dependent assertions are skipped in --test-only when the
    //   font probe returns zero.
    gX_F32v2 probe = gX_RawString_Measure("M", gX_Font_GetDefault(), 32.0f, 0.0f);
    if (probe.x <= 0.0f) {
        if (errors == 0)
            gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT tests: all passed (font probe skipped)");
        return errors;
    }

    gX_GUI_Canvas canvas = {0};
    if (gX_GUI_Canvas_Load(&canvas) != 0) return errors + 1;
    canvas.canvasBox = (gX_Rectangle){0, 0, 1024, 768};
    if (gX_GUI_Canvas_Add_Element(&canvas) != 0) {
        gX_GUI_Canvas_Unload(&canvas);
        return errors + 1;
    }
    gX_GUI_Element *element = &((gX_GUI_Element *)canvas.elements.data)[0];
    element->rect = (gX_Rectangle){0, 0, 120, 30};
    element->visible = true;
    if (gX_GUI_Element_Add_Text(element) != 0) errors++;
    gX_GUI_Element_Text *text = gX_GUI_Element_Text_Get(element, 0);
    if (!text) {
        gX_GUI_Canvas_Unload(&canvas);
        return errors + 1;
    }
    snprintf(text->text, sizeof(text->text), "%s", "WWWWWWWWWWWWWWWWWWWWWWWW");
    text->fontSize = 32.0f;

    // baseline: no shrink → the long label truncates with overflow
    gX_GUI_Element_Text_Layout layout = gX_GUI_Element_Text_Analyze(element, 0);
    if (!layout.hasText || !layout.truncated || layout.shrinkScale != 1.0f
        || !layout.overflowX || layout.natural.x <= layout.rectWidth) {
        gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT 2 FAIL: trunc=%d shrink=%.2f over=%d",
            (gX_I32)layout.truncated, layout.shrinkScale, (gX_I32)layout.overflowX);
        errors++;
    }

    // opt-in shrink: fits within the box above the configured floor
    text->autoShrink = true;
    layout = gX_GUI_Element_Text_Analyze(element, 0);
    if (!layout.hasText || layout.shrinkScale >= 1.0f
        || layout.shrinkScale < gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE - 0.001f
        || layout.truncated || layout.overflowX
        || layout.measure.x > layout.rectWidth + 0.5f) {
        gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT 3 FAIL: shrink=%.2f trunc=%d measure=%.0fx%.0f",
            layout.shrinkScale, (gX_I32)layout.truncated,
            layout.measure.x, layout.measure.y);
        errors++;
    }

    // floor: a string that cannot fit at the minimum scale stays truncated
    {
        gX_I8 longText[200];
        memset(longText, 'W', sizeof(longText) - 1);
        longText[sizeof(longText) - 1] = '\0';
        snprintf(text->text, sizeof(text->text), "%s", longText);
        layout = gX_GUI_Element_Text_Analyze(element, 0);
        if (!layout.truncated
            || layout.shrinkScale > gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE + 0.001f) {
            gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT 4 FAIL: shrink=%.2f trunc=%d",
                layout.shrinkScale, (gX_I32)layout.truncated);
            errors++;
        }
    }

    // wrap: a multi-word label wraps instead of truncating
    text->autoShrink = false;
    text->wrap = true;
    snprintf(text->text, sizeof(text->text), "%s", "wrap this sentence across lines");
    element->rect = (gX_Rectangle){0, 0, 120, 120};
    layout = gX_GUI_Element_Text_Analyze(element, 0);
    if (!layout.hasText || !layout.wraps || layout.truncated
        || layout.measure.x > layout.rectWidth + 0.5f) {
        gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT 5 FAIL: wraps=%d trunc=%d measure=%.0fx%.0f",
            (gX_I32)layout.wraps, (gX_I32)layout.truncated,
            layout.measure.x, layout.measure.y);
        errors++;
    }

    gX_GUI_Canvas_Unload(&canvas);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI TEXT LAYOUT tests: all passed");
    return errors;
}

//opencode: textinput widget tests — external-buffer capacity contract
//   (audit CRITICAL #1), replace-selection-at-maxLength (finding #9), and
//   UTF-8-safe truncation. The two editing helpers exercised here are
//   module-internal but exposed (not static) for unit tests.
extern gX_I0 gX_GUI_Element_TextInput_InsertChar(gX_GUI_Element_TextInput *ti, gX_I8 c);
extern gX_I0 gX_GUI_Element_TextInput_SyncExternalBuffer(gX_GUI_Element_TextInput *ti);

static gX_I0 gX_Test_TI_Init(gX_GUI_Element_TextInput *ti) {
    memset(ti, 0, sizeof(*ti));
    ti->maxLength = gX_GUI_ELEMENT_TEXT_MAX - 1;
}

//opencode: canary guard for the external-buffer sync tests — sentinel bytes on
//   both sides of the target buffer must survive every sync byte-for-byte.
//   Content checks alone cannot prove the old fixed-512-byte copy stayed inside
//   a short caller buffer (audit CRITICAL #1), so each capacity case below is
//   paired with guard verification.
#define GX_TEST_GUARD_BYTES 16
#define GX_TEST_GUARD_FILL  0xAB
typedef struct {
    gX_I8 before[GX_TEST_GUARD_BYTES];
    gX_I8 buffer[GX_TEST_GUARD_BYTES];
    gX_I8 after[GX_TEST_GUARD_BYTES];
} gX_Test_GuardedBuffer;

static gX_I1 gX_Test_GuardIntact(const gX_Test_GuardedBuffer *guard) {
    for (gX_U32 i = 0; i < GX_TEST_GUARD_BYTES; i++) {
        if ((gX_U8)guard->before[i] != GX_TEST_GUARD_FILL) return false;
        if ((gX_U8)guard->after[i] != GX_TEST_GUARD_FILL) return false;
    }
    return true;
}

//opencode: sentinel bytes from the end of the written text through the whole
//   16-byte test array must stay untouched — that range includes the bytes
//   past the declared capacity, which is exactly where an off-by-one or a
//   fixed-count copy would land.
static gX_I1 gX_Test_BufferTailIntact(const gX_Test_GuardedBuffer *guard,
    gX_U32 from) {
    for (gX_U32 i = from; i < GX_TEST_GUARD_BYTES; i++) {
        if ((gX_U8)guard->buffer[i] != GX_TEST_GUARD_FILL) return false;
    }
    return true;
}

static gX_I32 gX_Tests_TextInput(gX_I0) {
    gX_I32 errors = 0;

    // 1. canary-backed: capacity 8, longer text truncates at capacity-1 and
    //    the guard bytes on both sides stay untouched
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "hello world");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 8);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "hello w\0", 8) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1 FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 8)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1 FAIL: write past capacity 8"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1 FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1b. canary-backed: short text writes only text+NUL — the buffer tail is
    //     not zero-padded (the old strncpy wrote its full count)
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "hi");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 8);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "hi\0", 3) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1b FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 3)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1b FAIL: tail zero-padded past the text"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1b FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1c. canary-backed: capacity 4 truncates to three bytes plus NUL
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "abcdef");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 4);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "abc\0", 4) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1c FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 4)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1c FAIL: write past capacity 4"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1c FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1d. canary-backed: capacity 1 writes only the terminator
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "abc");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 1);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (guard.buffer[0] != '\0') {
            gX_TraceLog(gX_LOG_INFO, "TI 1d FAIL: capacity 1 not terminated"); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 1)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1d FAIL: write past capacity 1"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1d FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1e. canary-backed: text of capacity-1 bytes copies in full (no truncation)
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "0123456789abcde");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 16);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "0123456789abcde\0", 16) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1e FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1e FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1f. canary-backed: the template pattern — a caller buffer larger than
    //     the configured maxLength syncs at most maxLength+1 bytes, tail intact
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 4);
        gX_GUI_Element_TextInput_SetText(&ti, "abcdef");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 16);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "abcd\0", 5) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1f FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 5)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1f FAIL: tail written past maxLength"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1f FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 1g. canary-backed: a later shorter sync writes exactly its own text —
    //     no stale tail from the previous longer sync, guards untouched
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "abcdefghij");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 8);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        memset(guard.buffer, GX_TEST_GUARD_FILL, sizeof(guard.buffer));
        gX_GUI_Element_TextInput_SetText(&ti, "x");
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (memcmp(guard.buffer, "x\0", 2) != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 1g FAIL: caller buffer [%s]", guard.buffer); errors++;
        }
        if (!gX_Test_BufferTailIntact(&guard, 2)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1g FAIL: stale tail after a shorter sync"); errors++;
        }
        if (!gX_Test_GuardIntact(&guard)) {
            gX_TraceLog(gX_LOG_INFO, "TI 1g FAIL: guard bytes overwritten"); errors++;
        }
    }

    // 2. NULL pointer or zero capacity disables syncing entirely — the whole
    //    guarded block (buffer and both guard regions) stays sentinel
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_GuardedBuffer guard;
        gX_Test_TI_Init(&ti);
        memset(&guard, GX_TEST_GUARD_FILL, sizeof(guard));
        gX_GUI_Element_TextInput_SetText(&ti, "abc");
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, NULL, 0);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        gX_GUI_Element_TextInput_SetExternalBuffer(&ti, guard.buffer, 0);
        gX_GUI_Element_TextInput_SyncExternalBuffer(&ti);
        if (!gX_Test_GuardIntact(&guard)
            || !gX_Test_BufferTailIntact(&guard, 0)) {
            gX_TraceLog(gX_LOG_INFO, "TI 2 FAIL: disabled sync wrote the buffer"); errors++;
        }
    }

    // 3. inserting at maxLength with a selection replaces the selection
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 5);
        gX_GUI_Element_TextInput_SetText(&ti, "abcde");
        ti.selectionStart = 0;
        ti.cursorPos = 5;
        ti.hasSelection = true;
        gX_GUI_Element_TextInput_InsertChar(&ti, 'X');
        if (strcmp(ti.text, "X") != 0 || ti.length != 1) {
            gX_TraceLog(gX_LOG_INFO, "TI 3 FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
    }

    // 4. inserting at maxLength without a selection is still rejected
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 3);
        gX_GUI_Element_TextInput_SetText(&ti, "abc");
        gX_GUI_Element_TextInput_InsertChar(&ti, 'Z');
        if (strcmp(ti.text, "abc") != 0 || ti.length != 3) {
            gX_TraceLog(gX_LOG_INFO, "TI 4 FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
    }

    // 5. SetText truncates to maxLength (ASCII)
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 4);
        gX_GUI_Element_TextInput_SetText(&ti, "abcdef");
        if (strcmp(ti.text, "abcd") != 0 || ti.length != 4) {
            gX_TraceLog(gX_LOG_INFO, "TI 5 FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
    }

    // 6. SetText truncation keeps UTF-8 boundaries (never splits a codepoint)
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 4);
        gX_GUI_Element_TextInput_SetText(&ti, "a\xe4\xbd\xa0" "b"); // a你b → a你 kept, b dropped
        if (strcmp(ti.text, "a\xe4\xbd\xa0") != 0 || ti.length != 4) {
            gX_TraceLog(gX_LOG_INFO, "TI 6a FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 2);
        gX_GUI_Element_TextInput_SetText(&ti, "a\xe4\xbd\xa0"); // 4 bytes → 2 → 'a' only (你 dropped)
        if (strcmp(ti.text, "a") != 0 || ti.length != 1) {
            gX_TraceLog(gX_LOG_INFO, "TI 6b FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
    }

    // 7. SetMaxLength shrink keeps UTF-8 boundaries
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetText(&ti, "a\xe4\xbd\xa0" "bc"); // a你bc (6 bytes)
        gX_GUI_Element_TextInput_SetMaxLength(&ti, 3); // would cut inside 你 → back off to 'a'
        if (strcmp(ti.text, "a") != 0 || ti.length != 1) {
            gX_TraceLog(gX_LOG_INFO, "TI 7 FAIL: got [%s] len %u", ti.text, ti.length); errors++;
        }
    }

    // 8. word jumps land on word starts (Ctrl+Left/Right targets)
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetText(&ti, "foo bar  baz");
        if (gX_GUI_Element_TextInput_WordLeft(&ti, 12) != 9) {
            gX_TraceLog(gX_LOG_INFO, "TI 8a FAIL: wordleft end %u",
                gX_GUI_Element_TextInput_WordLeft(&ti, 12)); errors++;
        }
        if (gX_GUI_Element_TextInput_WordLeft(&ti, 9) != 4) {
            gX_TraceLog(gX_LOG_INFO, "TI 8b FAIL: wordleft mid %u",
                gX_GUI_Element_TextInput_WordLeft(&ti, 9)); errors++;
        }
        if (gX_GUI_Element_TextInput_WordRight(&ti, 0) != 4) {
            gX_TraceLog(gX_LOG_INFO, "TI 8c FAIL: wordright start %u",
                gX_GUI_Element_TextInput_WordRight(&ti, 0)); errors++;
        }
        if (gX_GUI_Element_TextInput_WordRight(&ti, 4) != 9) {
            gX_TraceLog(gX_LOG_INFO, "TI 8d FAIL: wordright mid %u",
                gX_GUI_Element_TextInput_WordRight(&ti, 4)); errors++;
        }
    }

    // 9. undo/redo round-trips a typed char; new edits clear the redo tail
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetText(&ti, "ab");
        gX_GUI_Element_TextInput_InsertChar(&ti, 'c');
        if (strcmp(ti.text, "abc") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 9a FAIL: got [%s]", ti.text); errors++;
        }
        gX_GUI_Element_TextInput_Undo(&ti);
        if (strcmp(ti.text, "ab") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 9b FAIL: undo got [%s]", ti.text); errors++;
        }
        gX_GUI_Element_TextInput_Redo(&ti);
        if (strcmp(ti.text, "abc") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 9c FAIL: redo got [%s]", ti.text); errors++;
        }
        gX_GUI_Element_TextInput_Undo(&ti);
        gX_GUI_Element_TextInput_InsertChar(&ti, 'd');
        gX_GUI_Element_TextInput_Redo(&ti);
        if (strcmp(ti.text, "abd") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 9d FAIL: redo after fork got [%s]", ti.text); errors++;
        }
    }

    // 10. Ctrl+Backspace removes the previous word
    {
        gX_GUI_Element_TextInput ti;
        gX_Test_TI_Init(&ti);
        gX_GUI_Element_TextInput_SetText(&ti, "foo bar");
        gX_GUI_Element_TextInput_DeleteWordLeft(&ti);
        if (strcmp(ti.text, "foo ") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 10 FAIL: got [%s]", ti.text); errors++;
        }
        gX_GUI_Element_TextInput_Undo(&ti);
        if (strcmp(ti.text, "foo bar") != 0) {
            gX_TraceLog(gX_LOG_INFO, "TI 10b FAIL: undo got [%s]", ti.text); errors++;
        }
    }

    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GX_TESTS: textinput done — 0 errors");
    return errors;
}

//opencode: scroll math tests — pure offset/selection helpers (no input heap).
static gX_I32 gX_Tests_ScrollMath(gX_I0) {
    gX_I32 errors = 0;
    if (gX_GUI_Scroll_Clamp(-3, 10, 4) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 1 FAIL: clamp low"); errors++;
    }
    if (gX_GUI_Scroll_Clamp(99, 10, 4) != 6) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 1 FAIL: clamp high"); errors++;
    }
    if (gX_GUI_Scroll_Clamp(2, 3, 4) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 1 FAIL: short list"); errors++;
    }
    if (gX_GUI_Scroll_WheelStep(3, 10, 4, 1) != 2) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 2 FAIL: wheel up"); errors++;
    }
    if (gX_GUI_Scroll_WheelStep(0, 10, 4, 1) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 2 FAIL: wheel top"); errors++;
    }
    if (gX_GUI_Scroll_WheelStep(6, 10, 4, -2) != 6) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 2 FAIL: wheel bottom"); errors++;
    }
    if (gX_GUI_Scroll_PageStep(0, 10, 4, 1) != 4) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 3 FAIL: page down"); errors++;
    }
    if (gX_GUI_Scroll_PageStep(5, 10, 4, 1) != 6) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 3 FAIL: page clamp"); errors++;
    }
    {
        gX_I32 sel = 3;
        gX_I32 scr = 0;
        gX_GUI_Scroll_MoveSelection(&sel, &scr, 10, 4, 1);
        if (sel != 4 || scr != 1) {
            gX_TraceLog(gX_LOG_INFO, "SCROLL 4 FAIL: move down autoscroll (%d,%d)", sel, scr); errors++;
        }
        gX_GUI_Scroll_MoveSelection(&sel, &scr, 10, 4, -4);
        if (sel != 0 || scr != 0) {
            gX_TraceLog(gX_LOG_INFO, "SCROLL 4 FAIL: move up autoscroll (%d,%d)", sel, scr); errors++;
        }
    }
    // 5. thumb drag maps pointer Y onto the range, clamped at both ends
    if (gX_GUI_Scroll_DragStep(100.0f, 200.0f, 50.0f, 100.0f, 10, 4) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: drag top"); errors++;
    }
    if (gX_GUI_Scroll_DragStep(100.0f, 200.0f, 50.0f, 250.0f, 10, 4) != 6) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: drag bottom"); errors++;
    }
    if (gX_GUI_Scroll_DragStep(100.0f, 200.0f, 50.0f, 175.0f, 10, 4) != 3) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: drag middle"); errors++;
    }
    if (gX_GUI_Scroll_DragStep(100.0f, 200.0f, 50.0f, -50.0f, 10, 4) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: drag above"); errors++;
    }
    if (gX_GUI_Scroll_DragStep(100.0f, 200.0f, 50.0f, 999.0f, 10, 4) != 6) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: drag below"); errors++;
    }
    if (gX_GUI_Scroll_DragStep(0.0f, 100.0f, 0.0f, 50.0f, 3, 4) != 0) {
        gX_TraceLog(gX_LOG_INFO, "SCROLL 5 FAIL: short list"); errors++;
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "SCROLL MATH tests: all passed");
    return errors;
}

//opencode: JSON round-trip tests (self-contained parser replacing json-c)
static gX_I32 gX_Tests_JSON(gX_I0) {
    gX_I32 errors = 0;
    gX_I8 pathBuf[160];
    gX_Tests_TempPath("json_test.json", pathBuf, sizeof(pathBuf));
    const gX_I8 *path = pathBuf;
    FILE *f;

    // 1. basic parse
    f = fopen(path, "wb");
    fputs("{\"a\":1,\"b\":2.5,\"c\":\"hi\",\"d\":true,\"e\":[1,2,3],\"f\":{\"g\":null}}", f);
    fclose(f);
    struct json_object *root = json_object_from_file(path);
    if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 1: parse failed"); errors++; }
    else {
        struct json_object *a = json_object_object_get(root, "a");
        struct json_object *b = json_object_object_get(root, "b");
        struct json_object *c = json_object_object_get(root, "c");
        struct json_object *d = json_object_object_get(root, "d");
        struct json_object *e = json_object_object_get(root, "e");
        struct json_object *fobj = json_object_object_get(root, "f");
        if (!a || json_object_get_int(a) != 1) errors++;
        if (!b || json_object_get_double(b) != 2.5) errors++;
        if (!c || strcmp(json_object_get_string(c), "hi") != 0) errors++;
        if (!d || json_object_get_boolean(d) != 1) errors++;
        if (!e || json_object_array_length(e) != 3) errors++;
        if (!e || json_object_get_int(json_object_array_get_idx(e, 1)) != 2) errors++;
        if (!fobj || json_object_object_get(fobj, "g") != NULL) errors++;
        json_object_put(root);
    }

    // 2. escapes + unicode
    f = fopen(path, "wb");
    fputs("{\"s\":\"a\\\"b\\\\c\\/d\\ne\\tf\\u0041\"}", f);
    fclose(f);
    root = json_object_from_file(path);
    if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 2: parse failed"); errors++; }
    else {
        struct json_object *s = json_object_object_get(root, "s");
        const gX_I8 *v = s ? json_object_get_string(s) : NULL;
        if (!v || strcmp(v, "a\"b\\c/d\ne\tfA") != 0) {
            gX_TraceLog(gX_LOG_INFO, "JSON test 2: escape mismatch: '%s'", v ? v : "(null)");
            errors++;
        }
        json_object_put(root);
    }

    // 3. surrogate pair → 4-byte UTF-8 (U+1F600)
    f = fopen(path, "wb");
    fputs("{\"s\":\"\\uD83D\\uDE00\"}", f);
    fclose(f);
    root = json_object_from_file(path);
    if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 3: parse failed"); errors++; }
    else {
        struct json_object *s = json_object_object_get(root, "s");
        const gX_I8 *v = s ? json_object_get_string(s) : NULL;
        if (!v || v[0] != (gX_I8)0xF0 || v[1] != (gX_I8)0x9F || v[2] != (gX_I8)0x98 || v[3] != (gX_I8)0x80 || v[4] != '\0') {
            gX_TraceLog(gX_LOG_INFO, "JSON test 3: surrogate mismatch");
            errors++;
        }
        json_object_put(root);
    }

    // 4. int vs double fidelity
    f = fopen(path, "wb");
    fputs("{\"i\":42,\"d\":42.0}", f);
    fclose(f);
    root = json_object_from_file(path);
    if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 4: parse failed"); errors++; }
    else {
        struct json_object *i = json_object_object_get(root, "i");
        struct json_object *d = json_object_object_get(root, "d");
        if (!i || json_object_get_type(i) != json_type_int) errors++;
        if (!d || json_object_get_type(d) != json_type_double) errors++;
        if (!d || json_object_get_double(d) != 42.0) errors++;
        json_object_put(root);
    }

    // 5. duplicate keys → last-wins
    f = fopen(path, "wb");
    fputs("{\"k\":1,\"k\":2}", f);
    fclose(f);
    root = json_object_from_file(path);
    if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 5: parse failed"); errors++; }
    else {
        struct json_object *k = json_object_object_get(root, "k");
        if (!k || json_object_get_int(k) != 2) errors++;
        json_object_put(root);
    }

    // 6. malformed → NULL
    f = fopen(path, "wb");
    fputs("{\"a\":", f);
    fclose(f);
    if (json_object_from_file(path) != NULL) { gX_TraceLog(gX_LOG_INFO, "JSON test 6a: malformed accepted"); errors++; }
    f = fopen(path, "wb");
    fputs("[1,2", f);
    fclose(f);
    if (json_object_from_file(path) != NULL) { gX_TraceLog(gX_LOG_INFO, "JSON test 6b: malformed accepted"); errors++; }
    f = fopen(path, "wb");
    fputs("{\"a\" 1}", f);
    fclose(f);
    if (json_object_from_file(path) != NULL) { gX_TraceLog(gX_LOG_INFO, "JSON test 6c: malformed accepted"); errors++; }

    // 7. double serialization keeps decimal point (round-trips as double)
    {
        struct json_object *o = json_object_new_object();
        json_object_object_add(o, "x", json_object_new_double(63.0));
        const gX_I8 *str = json_object_to_json_string(o);
        if (!str || strstr(str, "63.0") == NULL) {
            gX_TraceLog(gX_LOG_INFO, "JSON test 7: double serialization: '%s'", str ? str : "(null)");
            errors++;
        }
        json_object_put(o);
    }

    // 8. round-trip: build → serialize → file → parse → compare
    {
        struct json_object *o = json_object_new_object();
        json_object_object_add(o, "name", json_object_new_string("gX"));
        json_object_object_add(o, "count", json_object_new_int(7));
        json_object_object_add(o, "ratio", json_object_new_double(0.5));
        json_object_object_add(o, "ok", json_object_new_boolean(1));
        struct json_object *arr = json_object_new_array();
        json_object_array_add(arr, json_object_new_int(1));
        json_object_array_add(arr, json_object_new_int(2));
        json_object_object_add(o, "list", arr);
        int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB;
        if (json_object_to_file_ext(path, o, flags) != 0) { gX_TraceLog(gX_LOG_INFO, "JSON test 8: write failed"); errors++; }
        json_object_put(o);
        root = json_object_from_file(path);
        if (!root) { gX_TraceLog(gX_LOG_INFO, "JSON test 8: reparse failed"); errors++; }
        else {
            struct json_object *n = json_object_object_get(root, "name");
            struct json_object *c = json_object_object_get(root, "count");
            struct json_object *r = json_object_object_get(root, "ratio");
            struct json_object *ok = json_object_object_get(root, "ok");
            struct json_object *l = json_object_object_get(root, "list");
            if (!n || strcmp(json_object_get_string(n), "gX") != 0) errors++;
            if (!c || json_object_get_int(c) != 7) errors++;
            if (!r || json_object_get_double(r) != 0.5) errors++;
            if (!ok || json_object_get_boolean(ok) != 1) errors++;
            if (!l || json_object_array_length(l) != 2) errors++;
            if (!l || json_object_get_int(json_object_array_get_idx(l, 1)) != 2) errors++;
            json_object_put(root);
        }
    }

    remove(path);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GX_TESTS: JSON round-trip done — 0 errors");
    return errors;
}

//opencode: shader-compiler reload-observer tests (headless — the watch table
//   is pure C bookkeeping; CheckReload needs a GL context and is not exercised)
static gX_I0 gX_Test_Shader_Observer(gX_Shader *shader, gX_I0 *userData) {
    (void)shader;
    gX_I32 *count = (gX_I32 *)userData;
    if (count) (*count)++;
}
static gX_I32 gX_Tests_ShaderCompiler(gX_I0) {
    gX_I32 errors = 0;
    gX_I8 vpBuf[160];
    gX_I8 fpBuf[160];
    gX_Tests_TempPath("obs_test.vert", vpBuf, sizeof(vpBuf));
    gX_Tests_TempPath("obs_test.frag", fpBuf, sizeof(fpBuf));
    const gX_I8 *vp = vpBuf;
    const gX_I8 *fp = fpBuf;
    FILE *f = fopen(vp, "wb");
    if (f) { fputs("#version 330\nvoid main(){ gl_Position = vec4(0); }\n", f); fclose(f); }
    f = fopen(fp, "wb");
    if (f) { fputs("#version 330\nvoid main(){ gl_FragColor = vec4(1); }\n", f); fclose(f); }

    gX_Shader sh = {0};
    sh.id = 424242;   //opencode: fake program id — Watch/Subscribe do not touch GL
    gX_Shader other = {0};
    other.id = 424243;

    // 1. path-less Watch_Ex refused (the old GUI misuse — must not register)
    gX_Shader_Compiler_Watch_Ex(sh, NULL, NULL, NULL, NULL, NULL);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 0) errors++;

    // 2. owner watch with real paths creates an entry, zero observers
    gX_Shader_Compiler_Watch_Ex(sh, vp, fp, NULL, NULL, NULL);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 0) errors++;

    //opencode: dynamic watch-table regression — more than the old 16-entry limit
    {
        gX_Shader expanded[20] = {0};
        for (gX_I32 i = 0; i < 20; i++) {
            expanded[i].id = (gX_U32)(425000 + i);
            gX_Shader_Compiler_Watch_Ex(expanded[i], vp, fp, NULL, NULL, NULL);
            if (gX_Shader_Compiler_GetObserverCount(expanded[i]) != 0) errors++;
        }
        for (gX_I32 i = 0; i < 20; i++)
            gX_Shader_Compiler_Unwatch(expanded[i]);
    }

    // 3. subscribe to an unwatched program → 0 (must NOT create an entry)
    if (gX_Shader_Compiler_Subscribe(other, gX_Test_Shader_Observer, (gX_I0*)1) != 0) errors++;

    // 4. additive observers — two distinct subscribers survive
    gX_I32 hitsA = 0, hitsB = 0;
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsA) != 1) errors++;
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsB) != 1) errors++;
    if (gX_Shader_Compiler_GetObserverCount(sh) != 2) errors++;

    // 5. dedupe — same (callback,userData) again is a no-op
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsA) != 1) errors++;
    if (gX_Shader_Compiler_GetObserverCount(sh) != 2) errors++;

    // 6. NULL callback rejected
    if (gX_Shader_Compiler_Subscribe(sh, NULL, (gX_I0*)&hitsA) != 0) errors++;

    // 7. observer table cap (GX_SHADER_COMPILER_MAX_OBSERVERS = 4)
    gX_I32 hitsC = 0, hitsD = 0, hitsE = 0;
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsC) != 1) errors++;
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsD) != 1) errors++;
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsE) != 0) errors++;  // table full
    if (gX_Shader_Compiler_GetObserverCount(sh) != 4) errors++;

    // 8. unsubscribe removes exactly one observer
    gX_Shader_Compiler_Unsubscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsB);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 3) errors++;
    // 9. repeat unsubscribe → no-op
    gX_Shader_Compiler_Unsubscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsB);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 3) errors++;
    // 10. unsubscribe unknown userData → no-op
    gX_I32 hitsZ = 0;
    gX_Shader_Compiler_Unsubscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsZ);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 3) errors++;
    // 11. freed slot is reusable
    if (gX_Shader_Compiler_Subscribe(sh, gX_Test_Shader_Observer, (gX_I0*)&hitsB) != 1) errors++;
    if (gX_Shader_Compiler_GetObserverCount(sh) != 4) errors++;

    // 12. unwatch deactivates the entry (observers die with it)
    gX_Shader_Compiler_Unwatch(sh);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 0) errors++;
    // 13. re-watch of the same program reuses the slot — stale observers cleared
    gX_Shader_Compiler_Watch_Ex(sh, vp, fp, NULL, NULL, NULL);
    if (gX_Shader_Compiler_GetObserverCount(sh) != 0) errors++;

    gX_Shader_Compiler_Unwatch(sh);
    remove(vp);
    remove(fp);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "SHADER_COMPILER observer tests: all passed");
    return errors;
}

//opencode: gameCSS cascade tests (audit finding 8) — the key must compare
//   origin, importance, specificity, and source order lexicographically.
//   The old weighted key (origin*64 + important*2 + specificity*3) let one
//   specificity point outweigh !important and let a default-origin ID rule
//   beat a later-origin rule.
static gX_I32 gX_Tests_CSS_Cascade(gX_I0) {
    gX_I32 errors = 0;
    gX_CSS_Decl da = {0}, db = {0};
    gX_CSS_Rule ra = {0}, rb = {0};

    // 1. origin dominates importance + specificity
    ra.origin = 0; ra.selector.specificity = 100; da.important = true;
    rb.origin = 1; rb.selector.specificity = 0;   db.important = false;
    if (!gX_GUI_CSS_KeyLess(&da, &ra, &db, &rb)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 1 FAIL: later origin must win over default-origin ID + !important");
        errors++;
    }
    if (gX_GUI_CSS_KeyLess(&db, &rb, &da, &ra)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 1b FAIL: origin not antisymmetric");
        errors++;
    }

    // 2. !important beats specificity within the same origin
    ra.origin = 1; ra.selector.specificity = 10; da.important = false;
    rb.origin = 1; rb.selector.specificity = 0;  db.important = true;
    if (!gX_GUI_CSS_KeyLess(&da, &ra, &db, &rb)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 2 FAIL: !important must beat higher specificity");
        errors++;
    }

    // 3. specificity beats source order within the same origin + importance
    //   (higher specificity wins → sorts LATER, so KeyLess is false for it)
    ra.origin = 1; ra.selector.specificity = 100; da.important = false; ra.order = 99;
    rb.origin = 1; rb.selector.specificity = 10;  db.important = false; rb.order = 0;
    if (gX_GUI_CSS_KeyLess(&da, &ra, &db, &rb)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 3 FAIL: higher specificity must win over earlier source order");
        errors++;
    }
    if (!gX_GUI_CSS_KeyLess(&db, &rb, &da, &ra)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 3b FAIL: lower specificity must sort first");
        errors++;
    }

    // 4. source order breaks ties
    ra.origin = 1; ra.selector.specificity = 10; da.important = false; ra.order = 5;
    rb.origin = 1; rb.selector.specificity = 10; db.important = false; rb.order = 6;
    if (!gX_GUI_CSS_KeyLess(&da, &ra, &db, &rb)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 4 FAIL: lower source order must sort first");
        errors++;
    }
    if (gX_GUI_CSS_KeyLess(&db, &rb, &da, &ra)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 4b FAIL: higher source order must not sort first");
        errors++;
    }

    // 5. equal keys — not less in either direction (stable sort keeps order)
    ra.order = 3; rb.order = 3;
    if (gX_GUI_CSS_KeyLess(&da, &ra, &db, &rb) || gX_GUI_CSS_KeyLess(&db, &rb, &da, &ra)) {
        gX_TraceLog(gX_LOG_INFO, "CSS CASCADE 5 FAIL: equal keys must compare not-less both ways");
        errors++;
    }

    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "CSS CASCADE tests: all passed");
    return errors;
}

//opencode: gameCSS candidate collection must not silently truncate. Two
//   sheets × 64 universal rules = 128 matching declarations (> the old fixed
//   64-candidate cap). The winner by cascade order is the LAST rule of the
//   second sheet; the truncated collection dropped it and applied the first
//   sheet's last rule instead.
static gX_I32 gX_Tests_CSS_Overflow(gX_I0) {
    gX_I32 errors = 0;
    gX_I8 pathABuf[160];
    gX_I8 pathBBuf[160];
    gX_Tests_TempPath("css_overflow_a.css", pathABuf, sizeof(pathABuf));
    gX_Tests_TempPath("css_overflow_b.css", pathBBuf, sizeof(pathBBuf));
    const gX_I8 *pathA = pathABuf;
    const gX_I8 *pathB = pathBBuf;
    FILE *f;
    gX_I32 sheetA, sheetB;

    f = fopen(pathA, "wb");
    if (!f) { gX_TraceLog(gX_LOG_INFO, "CSS OVERFLOW: cannot write %s", pathA); return 1; }
    for (gX_I32 i = 0; i < 64; i++)
        fprintf(f, "* { background-color: rgb(%d, 0, 0); }\n", i);
    fclose(f);

    f = fopen(pathB, "wb");
    if (!f) { gX_TraceLog(gX_LOG_INFO, "CSS OVERFLOW: cannot write %s", pathB); return 1; }
    for (gX_I32 i = 0; i < 64; i++)
        fprintf(f, "* { background-color: rgb(0, %d, 0); }\n", i);
    fclose(f);

    sheetA = gX_GUI_CSS_LoadSheet(pathA);
    sheetB = gX_GUI_CSS_LoadSheet(pathB);
    if (sheetA < 0 || sheetB < 0) {
        gX_TraceLog(gX_LOG_INFO, "CSS OVERFLOW: sheet load failed (A=%d B=%d)", sheetA, sheetB);
        errors++;
        remove(pathA);
        remove(pathB);
        return errors;
    }

    gX_GUI_Element element;
    memset(&element, 0, sizeof(element));
    gX_Heap_Meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.USED = true;
    gX_Heap heap;
    memset(&heap, 0, sizeof(heap));
    heap.data = &element;
    heap.meta = &meta;
    heap.amount = 1;

    gX_GUI_Canvas canvas;
    memset(&canvas, 0, sizeof(canvas));
    canvas.elements = heap;
    canvas.cssSheetCount = 2;
    canvas.cssSheets[0] = sheetA;
    canvas.cssSheets[1] = sheetB;

    gX_GUI_CSS_ResolveCanvas(&canvas);

    //opencode: winner = sheet B rule 63 (green 63); a truncated collection
    //   would have applied sheet A rule 63 (red 63)
    if (element.color.r != 0 || element.color.g != 63 || element.color.b != 0) {
        gX_TraceLog(gX_LOG_INFO, "CSS OVERFLOW FAIL: got (%d,%d,%d) want (0,63,0)",
            (gX_I32)element.color.r, (gX_I32)element.color.g, (gX_I32)element.color.b);
        errors++;
    }

    remove(pathA);
    remove(pathB);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "CSS OVERFLOW tests: all passed");
    return errors;
}

//opencode: canvas composite transform round-trip (audit finding 7) — the
//   inverse hit-test transform must undo the forward composite transform
//   (offset + canvasScale + canvasAngle around the unscaled dst center, the
//   exact mapping gX_Texture_Draw applies when the canvas buffer is drawn).
static gX_F32 gX_Test_F32_Delta(gX_F32 a, gX_F32 b) {
    gX_F32 d = a - b;
    return (d < 0.0f) ? -d : d;
}

static gX_I32 gX_Tests_CanvasTransform(gX_I0) {
    gX_I32 errors = 0;
    gX_GUI_Canvas canvas;
    memset(&canvas, 0, sizeof(canvas));
    canvas.canvasBox = (gX_Rectangle){0, 0, 800, 600};
    canvas.buffer.width = 800;
    canvas.buffer.height = 600;
    canvas.buffer.loadingState.loaded = true;
    canvas.canvasOffset = (gX_F32v2){30.0f, -20.0f};
    canvas.canvasScale = (gX_F32v2){0.92f, 0.92f};
    canvas.canvasAngle = 12.0f;

    //opencode: forward composite (mirrors raylib_Texture_Draw: scale then
    //   rotate around the unscaled dst center)
    gX_F32 dstW = canvas.canvasBox.width, dstH = canvas.canvasBox.height;
    gX_F32 dstX = canvas.canvasBox.x + canvas.canvasOffset.x;
    gX_F32 dstY = canvas.canvasBox.y + canvas.canvasOffset.y;
    gX_F32 cx = dstX + dstW * 0.5f, cy = dstY + dstH * 0.5f;
    gX_F32 rad = canvas.canvasAngle * (gX_F32)gX_DEG2RAD;
    gX_F32 cosR = cosf(rad), sinR = sinf(rad);

    gX_F32v2 p = {140.0f, 220.0f};   // buffer-space point (inside an element)
    gX_F32 px = dstX + p.x * (dstW / (gX_F32)canvas.buffer.width);
    gX_F32 py = dstY + p.y * (dstH / (gX_F32)canvas.buffer.height);
    gX_F32 rx = (px - cx) * canvas.canvasScale.x;
    gX_F32 ry = (py - cy) * canvas.canvasScale.y;
    gX_F32v2 screen = { cx + rx * cosR - ry * sinR, cy + rx * sinR + ry * cosR };

    gX_F32v2 back = {0, 0};
    if (!gX_GUI_Canvas_ToBufferPoint(&canvas, screen, &back)) {
        gX_TraceLog(gX_LOG_INFO, "CANVAS TF 1 FAIL: inverse transform rejected");
        errors++;
    } else if (gX_Test_F32_Delta(back.x, p.x) > 0.5f || gX_Test_F32_Delta(back.y, p.y) > 0.5f) {
        gX_TraceLog(gX_LOG_INFO, "CANVAS TF 1 FAIL: round trip (%.1f,%.1f) -> (%.1f,%.1f)",
            p.x, p.y, back.x, back.y);
        errors++;
    }

    //opencode: identity transform (scale 1, no offset/angle) is exact
    canvas.canvasOffset = (gX_F32v2){0, 0};
    canvas.canvasScale = (gX_F32v2){1, 1};
    canvas.canvasAngle = 0.0f;
    p = (gX_F32v2){350.0f, 120.0f};
    if (!gX_GUI_Canvas_ToBufferPoint(&canvas, p, &back) ||
        gX_Test_F32_Delta(back.x, p.x) > 0.01f || gX_Test_F32_Delta(back.y, p.y) > 0.01f) {
        gX_TraceLog(gX_LOG_INFO, "CANVAS TF 2 FAIL: identity round trip (%.1f,%.1f) -> (%.1f,%.1f)",
            p.x, p.y, back.x, back.y);
        errors++;
    }

    //opencode: no buffer (direct screen-space draw) → no transform, reject
    canvas.buffer.loadingState.loaded = false;
    if (gX_GUI_Canvas_ToBufferPoint(&canvas, p, &back)) {
        gX_TraceLog(gX_LOG_INFO, "CANVAS TF 3 FAIL: no-buffer canvas must reject");
        errors++;
    }

    //opencode: degenerate scale → reject (collapsed canvas)
    canvas.buffer.loadingState.loaded = true;
    canvas.canvasScale = (gX_F32v2){0.0f, 0.0f};
    if (gX_GUI_Canvas_ToBufferPoint(&canvas, p, &back)) {
        gX_TraceLog(gX_LOG_INFO, "CANVAS TF 4 FAIL: zero-scale canvas must reject");
        errors++;
    }

    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "CANVAS TRANSFORM tests: all passed");
    return errors;
}

//opencode: theme hot reload restores C defaults and validates shipped presets.
static gX_I32 gX_Tests_StyleReload(gX_I0) {
    gX_I32 errors = 0;
    gX_I8 pathBuf[160];
    gX_Tests_TempPath("theme_test.json", pathBuf, sizeof(pathBuf));
    const gX_I8 *path = pathBuf;
    FILE *f = fopen(path, "wb");
    if (!f) { gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD: cannot write %s", path); return 1; }
    fputs("{\"core\":{\"panelBg\":[255,0,0,255]}}", f);
    fclose(f);

    gX_GUI_Style_SetThemePath(path);
    gX_GUI_Style_LoadDefault();
    if (gui.style.panelBg.r != 255 || gui.style.panelBg.g != 0 || gui.style.panelBg.b != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD 1 FAIL: theme key not applied (%d,%d,%d)",
            (gX_I32)gui.style.panelBg.r, (gX_I32)gui.style.panelBg.g, (gX_I32)gui.style.panelBg.b);
        errors++;
    }

    //opencode: rewrite the theme WITHOUT panelBg — reload must fall back to
    //   the C default {63,63,127,255}, not retain the stale red
    f = fopen(path, "wb");
    if (!f) { gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD: cannot rewrite %s", path); return 1; }
    fputs("{\"core\":{\"textColor\":[10,20,30,255]}}", f);
    fclose(f);
    gX_GUI_Style_LoadDefault();
    if (gui.style.panelBg.r != 63 || gui.style.panelBg.g != 63 || gui.style.panelBg.b != 127) {
        gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD 2 FAIL: removed key retained stale value (%d,%d,%d)",
            (gX_I32)gui.style.panelBg.r, (gX_I32)gui.style.panelBg.g, (gX_I32)gui.style.panelBg.b);
        errors++;
    }

    //opencode: a11y tokens fall back too — no a11y key in either theme, so
    //   the C default typeLineHeight (1.0) must be in effect
    if (gui.style.a11y.typeLineHeight != 1.0f || gui.style.a11y.motionReduced != false) {
        gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD 3 FAIL: a11y C defaults not restored");
        errors++;
    }

    remove(path);
    gX_GUI_Style_SetThemePath("resources/style/theme_high_contrast.json");
    if (gX_GUI_Style_LoadDefault() != 0
        || gui.style.panelBg.r != 0 || gui.style.panelBg.g != 0
        || gui.style.panelBg.b != 0 || gui.style.focusRing.r != 255
        || gui.style.focusRing.g != 235 || gui.style.focusRing.b != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE PRESET 1 FAIL: high-contrast palette not loaded");
        errors++;
    }
    gX_GUI_Style_SetThemePath("resources/style/theme_color_vision.json");
    if (gX_GUI_Style_LoadDefault() != 0
        || gui.style.shared.hpGreen.b <= gui.style.shared.hpGreen.r
        || gui.style.shared.alertRed.r <= gui.style.shared.alertRed.b
        || gui.style.shared.hpGreen.b == gui.style.shared.alertRed.r) {
        gX_TraceLog(gX_LOG_INFO, "STYLE PRESET 2 FAIL: color-vision palette not loaded");
        errors++;
    }
    gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
    if (gX_GUI_Style_LoadDefault() != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE PRESET 3 FAIL: default palette did not reload");
        errors++;
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "STYLE RELOAD tests: all passed");
    return errors;
}

//opencode: theme save/reload fixed point — the serialized token set must
//   round-trip byte-identically through the save path, and a load must
//   overwrite in-memory mutations with the saved values.
static gX_I32 gX_Tests_StyleRoundTrip(gX_I0)
{
    gX_I32 errors = 0;
    gX_I8 pathABuf[160];
    gX_I8 pathBBuf[160];
    gX_Tests_TempPath("style_roundtrip_a.json", pathABuf, sizeof(pathABuf));
    gX_Tests_TempPath("style_roundtrip_b.json", pathBBuf, sizeof(pathBBuf));
    const gX_I8 *pathA = pathABuf;
    const gX_I8 *pathB = pathBBuf;

    gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
    if (gX_GUI_Style_LoadDefault() != 0 || gX_GUI_Style_TokenCount() <= 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: default theme not loaded");
        return 1;
    }
    if (gX_GUI_Style_Save(pathA) != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: save '%s'", pathA);
        return 1;
    }

    //opencode: mutate a known token, then reload the saved file and require
    //   the saved value back (a load that is a silent no-op must fail).
    gX_Color expected = gui.style.panelBg;
    gui.style.panelBg = (gX_Color){255, 0, 0, 255};
    gX_GUI_Style_SetThemePath(pathA);
    if (gX_GUI_Style_LoadDefault() != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: saved theme did not load");
        errors++;
    } else if (gui.style.panelBg.r != expected.r || gui.style.panelBg.g != expected.g
        || gui.style.panelBg.b != expected.b || gui.style.panelBg.a != expected.a) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: panelBg (%d,%d,%d,%d) != (%d,%d,%d,%d)",
            (gX_I32)gui.style.panelBg.r, (gX_I32)gui.style.panelBg.g,
            (gX_I32)gui.style.panelBg.b, (gX_I32)gui.style.panelBg.a,
            (gX_I32)expected.r, (gX_I32)expected.g, (gX_I32)expected.b, (gX_I32)expected.a);
        errors++;
    }
    if (gX_GUI_Style_Save(pathB) != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: second save");
        errors++;
    }

    //opencode: byte-identical fixed point — load->save must not drift.
    FILE *fileA = fopen(pathA, "rb");
    FILE *fileB = fopen(pathB, "rb");
    if (!fileA || !fileB) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: compare open");
        errors++;
    } else {
        fseek(fileA, 0, SEEK_END); long sizeA = ftell(fileA); fseek(fileA, 0, SEEK_SET);
        fseek(fileB, 0, SEEK_END); long sizeB = ftell(fileB); fseek(fileB, 0, SEEK_SET);
        if (sizeA <= 0 || sizeA != sizeB) {
            gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: size %ld != %ld", sizeA, sizeB);
            errors++;
        } else {
            gX_I8 *bytesA = (gX_I8*)malloc((size_t)sizeA);
            gX_I8 *bytesB = (gX_I8*)malloc((size_t)sizeA);
            if (!bytesA || !bytesB
                || fread(bytesA, 1, (size_t)sizeA, fileA) != (size_t)sizeA
                || fread(bytesB, 1, (size_t)sizeA, fileB) != (size_t)sizeA
                || memcmp(bytesA, bytesB, (size_t)sizeA) != 0) {
                gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: save/load is not a fixed point");
                errors++;
            }
            free(bytesA);
            free(bytesB);
        }
    }
    if (fileA) fclose(fileA);
    if (fileB) fclose(fileB);
    remove(pathA);
    remove(pathB);

    gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
    if (gX_GUI_Style_LoadDefault() != 0) {
        gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP FAIL: default theme did not restore");
        errors++;
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "STYLE ROUNDTRIP tests: all passed");
    return errors;
}

//opencode: interaction foundation tests — modal scope, exclusive focus,
//   and drag capture on the live canvas heap (headless, restored on exit).
static gX_I32 gX_Tests_GUI_Interaction(gX_I0)
{
    gX_I32 errors = 0;
    //opencode: isolated heap — tests run before Load so the global heap is free.
    gX_Heap_Zero(&gui.canvases);
    gX_Heap_SetBlockSize(&gui.canvases, sizeof(gX_GUI_Canvas));
    gX_Heap_SetMax(&gui.canvases, 4);
    gX_Heap_SetAmount(&gui.canvases, 0);
    if (gX_Heap_Init(&gui.canvases) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT: canvas heap init failed");
        return 1;
    }
    gui.focusEnabled = true;
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gX_GUI_PointerCapture_Clear();
    if (gX_GUI_Add_Canvas() != 0 || gX_GUI_Add_Canvas() != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT: canvas add failed");
        errors++;
    }
    gX_I32 s0 = gX_GUI_Canvas_Add_Element(&((gX_GUI_Canvas*)gui.canvases.data)[0]);
    gX_I32 s1 = gX_GUI_Canvas_Add_Element(&((gX_GUI_Canvas*)gui.canvases.data)[1]);
    if (s0 != 0 || s1 != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT: element add FAIL (%d,%d)", s0, s1);
        errors++;
    }
    gX_GUI_Canvas *c0 = &((gX_GUI_Canvas*)gui.canvases.data)[0];
    gX_GUI_Canvas *c1 = &((gX_GUI_Canvas*)gui.canvases.data)[1];
    gX_GUI_Element *e0 = &((gX_GUI_Element*)c0->elements.data)[0];
    gX_GUI_Element *e1 = &((gX_GUI_Element*)c1->elements.data)[0];
    e0->visible = true;
    e1->visible = true;
    e0->state.FOCUSABLE = true;
    e1->state.FOCUSABLE = true;
    e1->state.DRAGGABLE_LEFT = true;
    //opencode: no modal — both canvases accept input.
    if (!gX_GUI_Canvas_InputAllowed(0) || !gX_GUI_Canvas_InputAllowed(1))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 1 FAIL: input blocked without modal");
        errors++;
    }
    //opencode: exclusive focus set/clear.
    if (!gX_GUI_Focus_Set(0, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 2 FAIL: Focus_Set(0,0) rejected");
        errors++;
    }
    if (gui.focusCanvasID != 0 || gui.focusElementID != 0 || !e0->state.FOCUSED)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 2 FAIL: focus IDs/bits diverged");
        errors++;
    }
    if (!gX_GUI_Focus_Set(1, 0) || e0->state.FOCUSED || !e1->state.FOCUSED)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 3 FAIL: exclusive transfer broken");
        errors++;
    }
    //opencode: invalid targets never take focus.
    if (gX_GUI_Focus_Set(-1, 0) || gX_GUI_Focus_Set(9, 0) || gX_GUI_Focus_Set(0, 9))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 4 FAIL: invalid focus accepted");
        errors++;
    }
    //opencode: modal scopes input and focus to its canvas.
    if (gX_GUI_Modal_Open(1, false) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 5 FAIL: Modal_Open rejected");
        errors++;
    }
    if (gX_GUI_Canvas_InputAllowed(0) || !gX_GUI_Canvas_InputAllowed(1))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 5 FAIL: modal scope wrong");
        errors++;
    }
    if (gX_GUI_Focus_Set(0, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 6 FAIL: background focus inside modal");
        errors++;
    }
    if (!gX_GUI_Focus_Set(1, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 6 FAIL: modal focus rejected");
        errors++;
    }
    //opencode: capture requires a draggable owner and resolves by pointer.
    if (!gX_GUI_PointerCapture_Begin(1, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 7 FAIL: capture begin rejected");
        errors++;
    }
    if (!gX_GUI_PointerCapture_IsOwner(c1, e1) || gX_GUI_PointerCapture_IsOwner(c0, e0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 7 FAIL: capture owner mismatch");
        errors++;
    }
    if (gX_GUI_PointerCapture_Begin(0, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 8 FAIL: non-draggable capture accepted");
        errors++;
    }
    //opencode: close restores the pre-modal focus (canvas 1 element 0 here).
    gX_GUI_Modal_Close();
    if (gui.modalCanvasID != -1 || !gX_GUI_Canvas_InputAllowed(0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 9 FAIL: modal close did not release scope");
        errors++;
    }
    if (gui.focusCanvasID != 1 || gui.focusElementID != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 9 FAIL: focus not restored (%d,%d)",
            gui.focusCanvasID, gui.focusElementID);
        errors++;
    }
    //opencode: removal drops focus and capture that pointed into the slot.
    gX_GUI_Focus_Set(1, 0);
    gX_GUI_PointerCapture_Begin(1, 0);
    gX_GUI_Canvas_Remove_Element(c1, 0);
    if (gui.focusCanvasID != -1 || gui.pointerCaptureActive)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI INTERACT 10 FAIL: removal kept stale focus/capture");
        errors++;
    }
    gX_GUI_Canvas_Unload(c0);
    gX_GUI_Canvas_Unload(c1);
    gX_Heap_DeInit(&gui.canvases);
    gX_Heap_Zero(&gui.canvases);
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gX_GUI_PointerCapture_Clear();
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI INTERACT tests: all passed");
    return errors;
}

//ai: title keyboard Back returns settings and Demos subviews to the menu.
static gX_I32 gX_Tests_GUI_TitleKeyboardBack(gX_I0)
{
    gX_I32 errors = 0;
    gX_I1 focusPending = gui.focusFirstPending;
    gX_GUI_Template_TitleGX_SetView(4);
    if (!gX_GUI_Template_TitleGX_Back()) {
        gX_TraceLog(gX_LOG_INFO, "GUI TITLE BACK 1 FAIL: settings subview was not handled");
        errors++;
    }
    if (gX_GUI_Template_TitleGX_Back()) {
        gX_TraceLog(gX_LOG_INFO, "GUI TITLE BACK 2 FAIL: root menu handled Back");
        errors++;
    }
    gX_GUI_Template_TitleGX_SetView(5);
    if (!gX_GUI_Template_TitleGX_Back()) {
        gX_TraceLog(gX_LOG_INFO, "GUI TITLE BACK 3 FAIL: demos subview was not handled");
        errors++;
    }
    if (gX_GUI_Template_TitleGX_Back()) {
        gX_TraceLog(gX_LOG_INFO, "GUI TITLE BACK 4 FAIL: demos Back did not return to root");
        errors++;
    }
    gX_GUI_Template_TitleGX_SetView(0);
    gui.focusFirstPending = focusPending;
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI TITLE BACK tests: all passed");
    return errors;
}

//opencode: dialog test spies — count confirmed/cancelled closes.
static gX_I32 gX_Test_DialogOkCount = 0;
static gX_I32 gX_Test_DialogCancelCount = 0;
static gX_I0 gX_Test_DialogOnOk(gX_I0 *arg) {
    (void)arg;
    gX_Test_DialogOkCount++;
}
static gX_I0 gX_Test_DialogOnCancel(gX_I0 *arg) {
    (void)arg;
    gX_Test_DialogCancelCount++;
}

//opencode: modal stack + dialog tests — nested open/pop with inner focus
//   restore, stack-full rejection, and dialog open/close callback routing.
static gX_I32 gX_Tests_GUI_Dialog(gX_I0)
{
    gX_I32 errors = 0;
    gX_U32 savedMakeAmount = gui.makeAmount;
    gX_I1 savedMake = gui.make;
    gX_Heap_Zero(&gui.canvases);
    gX_Heap_SetBlockSize(&gui.canvases, sizeof(gX_GUI_Canvas));
    gX_Heap_SetMax(&gui.canvases, 4);
    gX_Heap_SetAmount(&gui.canvases, 0);
    if (gX_Heap_Init(&gui.canvases) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG: canvas heap init failed");
        return 1;
    }
    gui.focusEnabled = true;
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gui.dialogOpen = false;
    gui.dialogCanvasIndex = 3;
    gX_GUI_PointerCapture_Clear();
    for (gX_U32 i = 0; i < 4; i++)
    {
        if (gX_GUI_Add_Canvas() != 0)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI DIALOG: canvas add %u failed", i);
            errors++;
        }
    }
    for (gX_U32 i = 0; i < 4; i++)
    {
        gX_GUI_Canvas *c = &((gX_GUI_Canvas*)gui.canvases.data)[i];
        if (gX_GUI_Canvas_Add_Element(c) != 0)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI DIALOG: element add %u failed", i);
            errors++;
        }
        else
        {
            gX_GUI_Element *e = &((gX_GUI_Element*)c->elements.data)[0];
            e->visible = true;
            e->state.FOCUSABLE = true;
        }
    }
    //opencode: nested modal — menu on 0, dialog on 1, pop restores inner focus.
    if (!gX_GUI_Focus_Set(0, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 1 FAIL: initial focus rejected");
        errors++;
    }
    if (gX_GUI_Modal_Open(0, false) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 1 FAIL: outer open rejected");
        errors++;
    }
    if (!gX_GUI_Focus_Set(0, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 1 FAIL: outer focus rejected");
        errors++;
    }
    if (gX_GUI_Modal_Open(1, false) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 2 FAIL: nested open rejected");
        errors++;
    }
    if (gui.modalDepth != 1 || gui.modalCanvasID != 1)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 2 FAIL: stack top wrong (%u/%d)",
            gui.modalDepth, gui.modalCanvasID);
        errors++;
    }
    if (gX_GUI_Canvas_InputAllowed(0) || !gX_GUI_Canvas_InputAllowed(1))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 2 FAIL: nested scope wrong");
        errors++;
    }
    if (!gX_GUI_Focus_Set(1, 0))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 2 FAIL: inner focus rejected");
        errors++;
    }
    gX_GUI_Modal_Close();
    if (gui.modalCanvasID != 0 || gui.modalDepth != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 3 FAIL: pop did not restore outer (%d/%u)",
            gui.modalCanvasID, gui.modalDepth);
        errors++;
    }
    if (gui.focusCanvasID != 0 || gui.focusElementID != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 3 FAIL: inner focus not restored (%d,%d)",
            gui.focusCanvasID, gui.focusElementID);
        errors++;
    }
    gX_GUI_Modal_Close();
    if (gui.modalCanvasID != -1 || gui.modalDepth != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 4 FAIL: bottom close wrong (%d/%u)",
            gui.modalCanvasID, gui.modalDepth);
        errors++;
    }
    //opencode: stack full — depth 3 reached, one more nested open rejected.
    gX_GUI_Modal_Open(0, false);
    gX_GUI_Modal_Open(1, false);
    gX_GUI_Modal_Open(2, false);
    gX_GUI_Modal_Open(3, false);
    if (gui.modalDepth != GX_GUI_MODAL_STACK)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 5 FAIL: depth %u, want %u",
            gui.modalDepth, (gX_U32)GX_GUI_MODAL_STACK);
        errors++;
    }
    if (gX_GUI_Modal_Open(0, false) == 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 5 FAIL: over-full open accepted");
        errors++;
    }
    while (gX_GUI_Modal_IsOpen()) gX_GUI_Modal_Close();
    //opencode: dialog open routes to the dialog canvas modal; double open fails.
    gX_Test_DialogOkCount = 0;
    gX_Test_DialogCancelCount = 0;
    if (gX_GUI_Dialog_Open("Quit", "Quit to desktop?", "Quit", "Cancel",
        gX_Test_DialogOnOk, NULL, gX_Test_DialogOnCancel, NULL) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 6 FAIL: Dialog_Open rejected");
        errors++;
    }
    if (!gui.dialogOpen || gui.modalCanvasID != 3 || !gX_GUI_Dialog_IsOpen())
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 6 FAIL: dialog state wrong");
        errors++;
    }
    if (gX_GUI_Dialog_Open("Quit", "again?", "Quit", "Cancel",
        gX_Test_DialogOnOk, NULL, gX_Test_DialogOnCancel, NULL) == 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 6 FAIL: double open accepted");
        errors++;
    }
    gX_GUI_Dialog_Close(true);
    if (gX_Test_DialogOkCount != 1 || gX_Test_DialogCancelCount != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 7 FAIL: confirm routed %d/%d",
            gX_Test_DialogOkCount, gX_Test_DialogCancelCount);
        errors++;
    }
    if (gui.dialogOpen || gX_GUI_Modal_IsOpen())
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 7 FAIL: confirm did not close");
        errors++;
    }
    if (gX_GUI_Dialog_Open("Quit", "Quit to desktop?", "Quit", "Cancel",
        gX_Test_DialogOnOk, NULL, gX_Test_DialogOnCancel, NULL) != 0)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 8 FAIL: reopen rejected");
        errors++;
    }
    gX_GUI_Dialog_Close(false);
    if (gX_Test_DialogOkCount != 1 || gX_Test_DialogCancelCount != 1)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI DIALOG 8 FAIL: cancel routed %d/%d",
            gX_Test_DialogOkCount, gX_Test_DialogCancelCount);
        errors++;
    }
    for (gX_U32 i = 0; i < 4; i++)
    {
        gX_GUI_Canvas_Unload(&((gX_GUI_Canvas*)gui.canvases.data)[i]);
    }
    gX_Heap_DeInit(&gui.canvases);
    gX_Heap_Zero(&gui.canvases);
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gui.dialogOpen = false;
    gui.dialogCanvasIndex = -1;
    gui.makeAmount = savedMakeAmount;
    gui.make = savedMake;
    gX_GUI_PointerCapture_Clear();
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI DIALOG tests: all passed");
    return errors;
}

//opencode: GUI core audit tests (findings #2/#3/#6) — headless invariants on
//   the element heap: render-order index stability (no physical array sort),
//   removal metadata inertness, slot reuse freshness, and the shared
//   interaction-eligibility predicate.
static gX_I32 gX_Tests_GUI_CoreAudit(gX_I0) {
    gX_I32 errors = 0;
    gX_GUI_Canvas canvas = {0};
    gX_U32 order[gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS];
    gX_U32 drawCount = 0;

    if (gX_GUI_Canvas_Load(&canvas) != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: Canvas_Load failed");
        return errors + 1;
    }

    gX_I32 sA = gX_GUI_Canvas_Add_Element(&canvas);
    gX_I32 sB = gX_GUI_Canvas_Add_Element(&canvas);
    gX_I32 sC = gX_GUI_Canvas_Add_Element(&canvas);
    if (sA != 0 || sB != 1 || sC != 2) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: slot assignment FAIL (%d,%d,%d)", sA, sB, sC);
        errors++;
    }
    gX_GUI_Element *elems = (gX_GUI_Element*)canvas.elements.data;
    elems[0].layer = 2;
    elems[1].layer = 0;
    elems[2].layer = 1;
    elems[0].state.FOCUSABLE = true;
    elems[1].state.FOCUSABLE = true;
    elems[2].state.FOCUSABLE = true;

    //opencode: BuildDrawOrder returns stable layer order (B:0, C:1, A:2)
    //   WITHOUT permuting the owning array.
    gX_GUI_Canvas_BuildDrawOrder(&canvas, order, gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS, &drawCount);
    if (drawCount != 3 || order[0] != 1 || order[1] != 2 || order[2] != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: draw order FAIL (%u: %u,%u,%u)", drawCount, order[0], order[1], order[2]);
        errors++;
    }
    //opencode: heap identity intact — id stays aligned with the slot, meta
    //   stays aligned with the element at that slot.
    if (elems[0].id != 0 || elems[1].id != 1 || elems[2].id != 2) errors++;
    if (!canvas.elements.meta[0].USED || !canvas.elements.meta[1].USED || !canvas.elements.meta[2].USED) errors++;

    //opencode: eligibility — live+visible+enabled is eligible; hidden and
    //   disabled are not.
    if (!gX_GUI_Canvas_Element_Eligible(&canvas, 0)) errors++;
    elems[0].visible = 0;
    if (gX_GUI_Canvas_Element_Eligible(&canvas, 0)) errors++;
    elems[0].visible = 1;
    elems[0].state.DISABLED = true;
    if (gX_GUI_Canvas_Element_Eligible(&canvas, 0)) errors++;
    elems[0].state.DISABLED = false;
    if (!gX_GUI_Canvas_Element_Eligible(&canvas, 0)) errors++;

    //opencode: removal clears the full operational metadata (heap clears only
    //   USED) and the slot leaves render order; the array is never permuted.
    gX_GUI_Canvas_Remove_Element(&canvas, 1);
    if (canvas.elements.meta[1].USED) errors++;
    if (canvas.elements.meta[1].UPDATE) errors++;
    if (canvas.elements.meta[1].DRAW) errors++;
    if (elems[1].visible != 0 || elems[1].rect.width != 0.0f) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: removed slot not zeroed (visible=%u w=%.1f)", elems[1].visible, elems[1].rect.width);
        errors++;
    }
    if (gX_GUI_Canvas_Element_Eligible(&canvas, 1)) errors++;
    gX_GUI_Canvas_BuildDrawOrder(&canvas, order, gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS, &drawCount);
    if (drawCount != 2 || order[0] != 2 || order[1] != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: order after remove FAIL (%u: %u,%u)", drawCount, order[0], order[1]);
        errors++;
    }

    //opencode: the freed slot is reused by first-empty and the replacement is
    //   fully fresh — no stale focus, defaults, or metadata from the prior
    //   occupant.
    gX_I32 sD = gX_GUI_Canvas_Add_Element(&canvas);
    if (sD != 1) {
        gX_TraceLog(gX_LOG_INFO, "GUI core audit: slot reuse FAIL (%d)", sD);
        errors++;
    }
    gX_GUI_Element *elems2 = (gX_GUI_Element*)canvas.elements.data;
    if (!canvas.elements.meta[1].USED) errors++;
    if (elems2[1].id != 1) errors++;
    if (elems2[1].state.FOCUSED) errors++;
    if (elems2[1].visible != 1) errors++;
    if (!gX_GUI_Canvas_Element_Eligible(&canvas, 1)) errors++;

    gX_GUI_Canvas_Unload(&canvas);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI core audit tests: all passed");
    return errors;
}

//opencode: transition stagger tests — per-canvas delay math
//   (headless, no frame clock needed). Callback-context coverage lives in
//   gX_Tests_GUI_CallbackContext below.
static gX_I32 gX_Tests_GUI_Transition(gX_I0)
{
    gX_I32 errors = 0;
    gX_GUI_Canvas_Transition tra = {0};
    tra.doOut = true;
    tra.doMiddle = true;
    tra.doIn = true;
    tra.out.Tseconds = 0.2f;
    tra.middle.Tseconds = 0.1f;
    tra.in.Tseconds = 0.2f;
    tra.start = (gX_F32v4){255.0f, 255.0f, 255.0f, 255.0f};
    tra.end = (gX_F32v4){0.0f, 0.0f, 0.0f, 255.0f};
    tra.easeOut = gX_EASE_LINEAR;
    tra.easeMiddle = gX_EASE_LINEAR;
    tra.easeIn = gX_EASE_LINEAR;
    tra.style = GX_GUI_TRANSITION_STYLE_FADE;
    tra.targetColor = true;
    //opencode: total active time without stagger.
    if (gX_Test_F32_Delta(gX_GUI_Transition_TotalSeconds(&tra), 0.5f) > 0.001f)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 1 FAIL: total %f", (double)gX_GUI_Transition_TotalSeconds(&tra));
        errors++;
    }
    gX_F32v4 c = {0, 0, 0, 0};
    gX_F32v2 p = {0, 0};
    gX_F32v2 s = {0, 0};
    gX_F32 a = 0.0f;
    //opencode: pre-delay clock holds the start state without finishing.
    if (gX_GUI_Transition_EvalLocal(&tra, -0.1f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 2 FAIL: pre-start reported done");
        errors++;
    }
    if (gX_Test_F32_Delta(c.x, 255.0f) > 1.0f)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 2 FAIL: pre-start color %f", (double)c.x);
        errors++;
    }
    //opencode: mid-out lerps halfway white to black.
    if (gX_GUI_Transition_EvalLocal(&tra, 0.1f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 3 FAIL: mid-out reported done");
        errors++;
    }
    if (gX_Test_F32_Delta(c.x, 127.5f) > 2.0f)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 3 FAIL: mid-out color %f", (double)c.x);
        errors++;
    }
    //opencode: middle holds the end color.
    if (gX_GUI_Transition_EvalLocal(&tra, 0.25f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 4 FAIL: middle reported done");
        errors++;
    }
    if (gX_Test_F32_Delta(c.x, 0.0f) > 1.0f)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 4 FAIL: middle color %f", (double)c.x);
        errors++;
    }
    //opencode: past the end lands steady white and done.
    if (!gX_GUI_Transition_EvalLocal(&tra, 0.6f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 5 FAIL: past-end not done");
        errors++;
    }
    if (gX_Test_F32_Delta(c.x, 255.0f) > 1.0f)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 5 FAIL: end color %f", (double)c.x);
        errors++;
    }
    //opencode: delay offsets the same curve (elapsed minus delay).
    if (gX_GUI_Transition_EvalLocal(&tra, 0.1f - 0.3f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 6 FAIL: delayed start reported done");
        errors++;
    }
    if (!gX_GUI_Transition_EvalLocal(&tra, 0.8f - 0.3f, &c, &p, &s, &a))
    {
        gX_TraceLog(gX_LOG_INFO, "GUI TRANS 6 FAIL: delayed end not done");
        errors++;
    }
    //opencode: stagger scenario — at elapsed 0.1 the undelayed canvas is
    //   mid-out while the 0.3s-delayed canvas still holds start; at 0.8
    //   both finished, so the shared done gate may close.
    {
        gX_I1 aDone = gX_GUI_Transition_EvalLocal(&tra, 0.1f, &c, &p, &s, &a);
        gX_F32 aColor = c.x;
        gX_I1 bDone = gX_GUI_Transition_EvalLocal(&tra, 0.1f - 0.3f, &c, &p, &s, &a);
        if (aDone || bDone)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI TRANS 8a FAIL: stagger mid-flight done");
            errors++;
        }
        if (gX_Test_F32_Delta(aColor, 255.0f) < 1.0f
            || gX_Test_F32_Delta(c.x, 255.0f) > 1.0f)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI TRANS 8a FAIL: stagger colors %f/%f",
                (double)aColor, (double)c.x);
            errors++;
        }
        aDone = gX_GUI_Transition_EvalLocal(&tra, 0.8f, &c, &p, &s, &a);
        bDone = gX_GUI_Transition_EvalLocal(&tra, 0.8f - 0.3f, &c, &p, &s, &a);
        if (!aDone || !bDone)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI TRANS 8b FAIL: stagger end not done");
            errors++;
        }
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI TRANS tests: all passed");
    return errors;
}

//opencode: owner-scoped callback contexts (GUI audit finding 4) — the widget
//   callback argument lives in the owning element's data module, so two
//   elements never alias and a partial canvas refresh cannot invalidate it.
//   Also asserts that plain gX_GUI_OnClick keeps arbitrary external userData.
static gX_I32 gX_Test_CtxProbe = -1;
static gX_I0 gX_Test_CtxCallback(gX_I0 *arg) {
    if (arg) gX_Test_CtxProbe = *(gX_I32*)arg;
}
static gX_I1 gX_Test_CtxOwnerDataPassed = false;
static gX_I0 gX_Test_CtxOwnerDataCallback(gX_I0 *arg) {
    gX_GUI_Element_Data *data = (gX_GUI_Element_Data*)arg;
    gX_Test_CtxOwnerDataPassed = data
        && strcmp(data->format, "owner-script.as") == 0
        && strcmp((gX_I8*)data->callbackCtx.bytes, "OwnerHandler") == 0;
}

static gX_I32 gX_Tests_GUI_CallbackContext(gX_I0)
{
    gX_I32 errors = 0;
    gX_GUI_Canvas canvas = {0};
    gX_I32 ctxA = 111;
    gX_I32 ctxB = 222;
    gX_I32 external = 333;

    if (gX_GUI_Canvas_Load(&canvas) != 0) return errors + 1;
    gX_GUI_Canvas_Add_Element(&canvas);
    gX_GUI_Canvas_Add_Element(&canvas);
    gX_GUI_Canvas_Add_Element(&canvas);
    gX_GUI_Canvas_Add_Element(&canvas);
    gX_GUI_Element *elems = (gX_GUI_Element*)canvas.elements.data;

    if (!gX_GUI_OnClick_Context(&elems[0], gX_Test_CtxCallback, &ctxA, sizeof(ctxA))) errors++;
    if (!gX_GUI_OnClick_Context(&elems[1], gX_Test_CtxCallback, &ctxB, sizeof(ctxB))) errors++;

    gX_GUI_Element_Data *dA = gX_GUI_Element_Data_Get(&elems[0], 0);
    gX_GUI_Element_Data *dB = gX_GUI_Element_Data_Get(&elems[1], 0);
    if (!dA || !dB)
    {
        gX_TraceLog(gX_LOG_INFO, "GUI CTX FAIL: data module missing");
        errors++;
    }
    else
    {
        if (!dA->usesCallbackContext || !dB->usesCallbackContext) errors++;
        if (dA->funcPtrArg != NULL || dB->funcPtrArg != NULL) errors++;
        if (*(gX_I32*)dA->callbackCtx.bytes != 111 || *(gX_I32*)dB->callbackCtx.bytes != 222)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI CTX FAIL: context bytes not copied");
            errors++;
        }
        // mutate A — B must be untouched (independent owner storage)
        *(gX_I32*)dA->callbackCtx.bytes = -7;
        if (*(gX_I32*)dB->callbackCtx.bytes != 222) errors++;
        // execute: the resolved inline context reaches the callback
        dA->executeAnyway = true;
        gX_Test_CtxProbe = -1;
        gX_GUI_Element_Update_Datas(&elems[0]);
        if (gX_Test_CtxProbe != -7)
        {
            gX_TraceLog(gX_LOG_INFO, "GUI CTX FAIL: A callback got %d", gX_Test_CtxProbe);
            errors++;
        }
        dB->executeAnyway = true;
        gX_Test_CtxProbe = -1;
        gX_GUI_Element_Update_Datas(&elems[1]);
        if (gX_Test_CtxProbe != 222) errors++;
    }

    // arbitrary external userData still routes through funcPtrArg
    if (!gX_GUI_OnClick_Context(&elems[2], gX_Test_CtxCallback, &ctxA, sizeof(ctxA))) errors++;
    gX_GUI_OnClick(&elems[2], gX_Test_CtxCallback, &external);
    gX_GUI_Element_Data *dC = gX_GUI_Element_Data_Get(&elems[2], 0);
    if (!dC)
    {
        errors++;
    }
    else
    {
        if (dC->usesCallbackContext) errors++;
        if (dC->funcPtrArg != &external) errors++;
        dC->executeAnyway = true;
        gX_Test_CtxProbe = -1;
        gX_GUI_Element_Update_Datas(&elems[2]);
        if (gX_Test_CtxProbe != 333) errors++;
    }

    //opencode: owner-data callbacks receive their live module at execution
    //   time, keeping references valid when the data heap reallocates.
    const gX_I8 ownerHandler[] = "OwnerHandler";
    gX_Test_CtxOwnerDataPassed = false;
    if (!gX_GUI_OnClick_Context(&elems[3], gX_Test_CtxOwnerDataCallback,
        ownerHandler, sizeof(ownerHandler))) errors++;
    gX_GUI_Element_Data *dD = gX_GUI_Element_Data_Get(&elems[3], 0);
    if (!dD) {
        errors++;
    } else {
        snprintf(dD->format, sizeof(dD->format), "%s", "owner-script.as");
        dD->callbackReceivesOwnerData = true;
        dD->executeAnyway = true;
        gX_GUI_Element_Update_Datas(&elems[3]);
        if (!gX_Test_CtxOwnerDataPassed) {
            gX_TraceLog(gX_LOG_INFO,
                "GUI CTX FAIL: owner callback did not receive its module");
            errors++;
        }
    }

    gX_GUI_Canvas_Unload(&canvas);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI callback context tests: all passed");
    return errors;
}

//opencode: partial-rebuild coverage (GUI audit coverage gap 1) — refreshing
//   one canvas must leave another canvas's owner-scoped callback contexts
//   untouched, and repeated refreshes must hand each rebuilt element its own
//   fresh context. The Clear + rebuild body mirrors the per-canvas branch of
//   gX_GUI_Update_Refresh.
static gX_I32 gX_Tests_GUI_CallbackRebuild(gX_I0)
{
    gX_I32 errors = 0;
    gX_GUI_Canvas canvasA = {0};
    gX_GUI_Canvas canvasB = {0};
    gX_I32 ctxA = 111;
    gX_I32 ctxB = 222;
    gX_I32 ctxA2 = 333;
    gX_U8 snapshotB[gX_GUI_ELEMENT_CALLBACK_CTX_MAX] = {0};
    gX_GUI_Element *elemsA = NULL;
    gX_GUI_Element *elemsB = NULL;
    gX_GUI_Element_Data *dataB = NULL;

    if (gX_GUI_Canvas_Load(&canvasA) != 0 || gX_GUI_Canvas_Load(&canvasB) != 0) {
        return errors + 1;
    }

    // baseline: each canvas carries its own context on its own element
    gX_GUI_Canvas_Add_Element(&canvasA);
    gX_GUI_Canvas_Add_Element(&canvasB);
    elemsA = (gX_GUI_Element *)canvasA.elements.data;
    elemsB = (gX_GUI_Element *)canvasB.elements.data;
    if (!gX_GUI_OnClick_Context(&elemsA[0], gX_Test_CtxCallback, &ctxA, sizeof(ctxA))
        || !gX_GUI_OnClick_Context(&elemsB[0], gX_Test_CtxCallback, &ctxB, sizeof(ctxB))) {
        gX_TraceLog(gX_LOG_INFO, "GUI CTX REBUILD FAIL: context attach rejected");
        errors++;
    }
    dataB = gX_GUI_Element_Data_Get(&elemsB[0], 0);
    if (!dataB) {
        gX_GUI_Canvas_Unload(&canvasA);
        gX_GUI_Canvas_Unload(&canvasB);
        return errors + 1;
    }
    memcpy(snapshotB, dataB->callbackCtx.bytes, sizeof(snapshotB));

    for (gX_I32 round = 0; round < 3; round++) {
        gX_I32 ctxNow = (round % 2 == 0) ? ctxA2 : ctxA;
        gX_GUI_Element_Data *dataA = NULL;

        // refresh canvas A only: clear it, rebuild one element with a fresh
        // context (mirrors gX_GUI_Update_Refresh for a single canvas)
        if (gX_GUI_Canvas_Clear(&canvasA) != 0) {
            errors++;
            break;
        }
        if (gX_GUI_Canvas_Add_Element(&canvasA) < 0) {
            gX_TraceLog(gX_LOG_INFO, "GUI CTX REBUILD %i FAIL: element add rejected", round);
            errors++;
            break;
        }
        elemsA = (gX_GUI_Element *)canvasA.elements.data;
        gX_GUI_OnClick_Context(&elemsA[0], gX_Test_CtxCallback, &ctxNow, sizeof(ctxNow));

        // canvas B was not refreshed: same module, same context bytes, still
        // fires its own callback with its own context
        dataB = gX_GUI_Element_Data_Get(&elemsB[0], 0);
        if (!dataB || !dataB->usesCallbackContext
            || dataB->funcPtr != gX_Test_CtxCallback
            || dataB->funcPtrArg != NULL
            || memcmp(dataB->callbackCtx.bytes, snapshotB, sizeof(snapshotB)) != 0) {
            gX_TraceLog(gX_LOG_INFO, "GUI CTX REBUILD %i FAIL: untouched canvas was reset", round);
            errors++;
        } else {
            dataB->executeAnyway = true;
            gX_Test_CtxProbe = -1;
            gX_GUI_Element_Update_Datas(&elemsB[0]);
            if (gX_Test_CtxProbe != ctxB) {
                gX_TraceLog(gX_LOG_INFO, "GUI CTX REBUILD %i FAIL: B fired %d",
                    round, gX_Test_CtxProbe);
                errors++;
            }
        }

        // A's rebuilt element fires the fresh context, not B's stale one
        dataA = gX_GUI_Element_Data_Get(&elemsA[0], 0);
        if (!dataA) {
            errors++;
        } else {
            dataA->executeAnyway = true;
            gX_Test_CtxProbe = -1;
            gX_GUI_Element_Update_Datas(&elemsA[0]);
            if (gX_Test_CtxProbe != ctxNow) {
                gX_TraceLog(gX_LOG_INFO, "GUI CTX REBUILD %i FAIL: A fired %d",
                    round, gX_Test_CtxProbe);
                errors++;
            }
        }
    }

    gX_GUI_Canvas_Unload(&canvasA);
    gX_GUI_Canvas_Unload(&canvasB);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI partial rebuild context tests: all passed");
    return errors;
}

//opencode: generation identity coverage — a reused element heap slot must not
//   satisfy a handle captured for its previous occupant.
static gX_I32 gX_Tests_GUI_ElementGeneration(gX_I0)
{
    gX_I32 errors = 0;
    gX_I32 firstIndex = -1;
    gX_I32 reusedIndex = -1;
    gX_U32 firstGeneration = 0;
    gX_GUI_Canvas canvas = {0};
    gX_GUI_Element *elements = NULL;

    if (gX_GUI_Canvas_Load(&canvas) != 0) return 1;
    firstIndex = gX_GUI_Canvas_Add_Element(&canvas);
    if (firstIndex < 0) {
        gX_GUI_Canvas_Unload(&canvas);
        return 1;
    }
    elements = (gX_GUI_Element *)canvas.elements.data;
    firstGeneration = elements[firstIndex].generation;
    if (firstGeneration == 0 || elements[firstIndex].id != (gX_U32)firstIndex) errors++;
    if (gX_GUI_Canvas_Remove_Element(&canvas, (gX_U32)firstIndex) != 0) errors++;
    reusedIndex = gX_GUI_Canvas_Add_Element(&canvas);
    elements = (gX_GUI_Element *)canvas.elements.data;
    if (reusedIndex != firstIndex) errors++;
    if (reusedIndex >= 0 && elements[reusedIndex].generation == firstGeneration) errors++;
    if (reusedIndex >= 0 && elements[reusedIndex].id != (gX_U32)reusedIndex) errors++;
    gX_GUI_Canvas_Unload(&canvas);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI element generation tests: all passed");
    return errors;
}

//opencode: editor identity coverage — the GUI editor's selection and slider
//   handles carry canvas + slot + generation, so an external target rebuild
//   that recycles slots must never let the editor alias a different element
//   (audit finding 11 / gate gap: editor selection after an external rebuild).
extern gX_I0 gX_GUI_Editor_Selection_Set(gX_I32 elementIndex);
extern gX_GUI_Element *gX_GUI_Editor_Selection_Resolve(gX_I0);
extern gX_I0 *gX_GUI_Editor_Field_Resolve(gX_I32 canvasIndex,
    gX_U32 elementIndex, gX_U32 generation, gX_U32 field);

static gX_I32 gX_Tests_GUI_EditorIdentity(gX_I0) {
    gX_I32 errors = 0;
    gX_I32 canvasIndex = 0;
    gX_GUI_Canvas *canvas = 0;
    gX_GUI_Element *elements = 0;
    gX_U32 firstGeneration = 0;
    gX_I8 savedTarget[32] = {0};
    const gX_I8 *savedTargetEnv = getenv("GX_GUI_EDITOR_TARGET");

    if (savedTargetEnv) strncpy(savedTarget, savedTargetEnv, sizeof(savedTarget) - 1);
    //opencode: isolated heap like the interaction tests — the global canvas
    //   heap is free between GUI test groups.
    gX_Heap_Zero(&gui.canvases);
    gX_Heap_SetBlockSize(&gui.canvases, sizeof(gX_GUI_Canvas));
    gX_Heap_SetMax(&gui.canvases, 4);
    gX_Heap_SetAmount(&gui.canvases, 0);
    if (gX_Heap_Init(&gui.canvases) != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: canvas heap init failed");
        return 1;
    }
    if (gX_GUI_Add_Canvas() != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: canvas add rejected");
        errors++;
        goto editorIdentityCleanup;
    }
    canvas = gX_GUI_Canvas_FromIndex(canvasIndex);
    setenv("GX_GUI_EDITOR_TARGET", "0", 1);

    for (gX_U32 i = 0; i < 3; i++) {
        if (gX_GUI_Canvas_Add_Element(canvas) < 0) {
            gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: element add rejected");
            errors++;
        }
    }
    elements = (gX_GUI_Element *)canvas->elements.data;
    firstGeneration = elements[1].generation;

    // 1. the selection resolves to the exact element while generations match
    gX_GUI_Editor_Selection_Set(1);
    if (gX_GUI_Editor_Selection_Resolve() != &elements[1]) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 1 FAIL: selection did not resolve");
        errors++;
    }

    // 2. external rebuild: every slot is recycled with a fresh generation
    for (gX_U32 i = 0; i < 3; i++) {
        if (gX_GUI_Canvas_Remove_Element(canvas, i) != 0) {
            gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: element remove %u rejected", i);
            errors++;
        }
    }
    for (gX_U32 i = 0; i < 3; i++) {
        if (gX_GUI_Canvas_Add_Element(canvas) < 0) {
            gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: element re-add %u rejected", i);
            errors++;
        }
    }
    elements = (gX_GUI_Element *)canvas->elements.data;
    if (elements[1].generation == firstGeneration) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 2 FAIL: recycled slot kept its generation");
        errors++;
    }
    if (gX_GUI_Editor_Selection_Resolve() != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 3 FAIL: stale selection aliased the recycled slot");
        errors++;
    }

    // 3. the slider field handle obeys the same generation contract
    if (gX_GUI_Editor_Field_Resolve(canvasIndex, 1, firstGeneration, 0) != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 4 FAIL: stale field handle survived");
        errors++;
    }
    if (gX_GUI_Editor_Field_Resolve(canvasIndex, 1, elements[1].generation, 0)
        != &elements[1].rect.x) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 5 FAIL: fresh field handle did not resolve");
        errors++;
    }
    if (gX_GUI_Editor_Field_Resolve(canvasIndex, 1, elements[1].generation, 99) != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 6 FAIL: unknown field id resolved");
        errors++;
    }

    // 4. a removed slot dies without reuse; out-of-range selections are rejected
    gX_GUI_Editor_Selection_Set(2);
    if (gX_GUI_Editor_Selection_Resolve() != &elements[2]) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 7 FAIL: fresh selection did not resolve");
        errors++;
    }
    if (gX_GUI_Canvas_Remove_Element(canvas, 2) != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 0 FAIL: slot 2 remove rejected");
        errors++;
    }
    if (gX_GUI_Editor_Selection_Resolve() != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 8 FAIL: removed slot resolved");
        errors++;
    }
    gX_GUI_Editor_Selection_Set(99);
    if (gX_GUI_Editor_Selection_Resolve() != 0) {
        gX_TraceLog(gX_LOG_INFO, "EDITOR 9 FAIL: out-of-range selection resolved");
        errors++;
    }

    gX_GUI_Canvas_Unload(canvas);
editorIdentityCleanup:
    if (savedTargetEnv) setenv("GX_GUI_EDITOR_TARGET", savedTarget, 1);
    else unsetenv("GX_GUI_EDITOR_TARGET");
    gX_Heap_DeInit(&gui.canvases);
    gX_Heap_Zero(&gui.canvases);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI editor identity tests: all passed");
    return errors;
}

//opencode: layout tests — flex grow distribution, min/max clamps, wrap
//   lines, and anchor/percent placement (pure math + headless builder).
static gX_I32 gX_Tests_Layout(gX_I0)
{
    gX_I32 errors = 0;
    gX_GUI_Layout_Style style = {0};
    style.display = gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW;
    style.justify = gX_GUI_ELEMENT_LAYOUT_JUSTIFY_START;
    style.align = gX_GUI_ELEMENT_LAYOUT_ALIGN_START;
    style.gap = 0.0f;
    //opencode: grow splits leftover space by weight (50/50 + 200 free 1:2).
    {
        gX_GUI_Layout_Item items[2] = {0};
        items[0].inW = 50.0f;
        items[0].inH = 20.0f;
        items[0].grow = 1.0f;
        items[1].inW = 50.0f;
        items[1].inH = 20.0f;
        items[1].grow = 2.0f;
        gX_GUI_Layout_Resolve(&style, 0.0f, 0.0f, 300.0f, 100.0f, items, 2);
        if (gX_Test_F32_Delta(items[0].outW, 116.67f) > 0.1f
            || gX_Test_F32_Delta(items[1].outW, 183.33f) > 0.1f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 1 FAIL: grow %f/%f",
                (double)items[0].outW, (double)items[1].outW);
            errors++;
        }
        if (gX_Test_F32_Delta(items[1].outX, 116.67f) > 0.1f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 1 FAIL: second x %f", (double)items[1].outX);
            errors++;
        }
    }
    //opencode: max clamps growth (second item pinned at 120).
    {
        gX_GUI_Layout_Item items[2] = {0};
        items[0].inW = 50.0f;
        items[0].inH = 20.0f;
        items[0].grow = 1.0f;
        items[1].inW = 50.0f;
        items[1].inH = 20.0f;
        items[1].grow = 2.0f;
        items[1].maxW = 120.0f;
        gX_GUI_Layout_Resolve(&style, 0.0f, 0.0f, 300.0f, 100.0f, items, 2);
        if (gX_Test_F32_Delta(items[1].outW, 120.0f) > 0.1f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 2 FAIL: max %f", (double)items[1].outW);
            errors++;
        }
    }
    //opencode: no-wrap overflow keeps one line (third item past the edge).
    {
        gX_GUI_Layout_Item items[3] = {0};
        for (gX_U32 i = 0; i < 3; i++)
        {
            items[i].inW = 100.0f;
            items[i].inH = 20.0f;
        }
        gX_GUI_Layout_Resolve(&style, 0.0f, 0.0f, 250.0f, 100.0f, items, 3);
        if (gX_Test_F32_Delta(items[2].outY, 0.0f) > 0.01f
            || gX_Test_F32_Delta(items[2].outX, 200.0f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 3 FAIL: flat %f/%f",
                (double)items[2].outX, (double)items[2].outY);
            errors++;
        }
    }
    //opencode: wrap breaks the third item onto a second line.
    {
        gX_GUI_Layout_Item items[3] = {0};
        gX_GUI_Layout_Style wstyle = style;
        wstyle.wrap = true;
        for (gX_U32 i = 0; i < 3; i++)
        {
            items[i].inW = 100.0f;
            items[i].inH = 20.0f;
        }
        gX_GUI_Layout_Resolve(&wstyle, 0.0f, 0.0f, 250.0f, 100.0f, items, 3);
        if (gX_Test_F32_Delta(items[0].outX, 0.0f) > 0.01f
            || gX_Test_F32_Delta(items[1].outX, 100.0f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 4 FAIL: line one %f/%f",
                (double)items[0].outX, (double)items[1].outX);
            errors++;
        }
        if (gX_Test_F32_Delta(items[2].outX, 0.0f) > 0.01f
            || gX_Test_F32_Delta(items[2].outY, 20.0f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 4 FAIL: line two %f/%f",
                (double)items[2].outX, (double)items[2].outY);
            errors++;
        }
    }
    //opencode: anchor/percent placement on a synthetic 800x600 grid.
    //   A bare builder is used (Builder_New needs the resource manager,
    //   which is not up this early in the test run).
    {
        gX_GUI_Builder b = {0};
        gX_GUI_Element el = {0};
        b.widthDiv = 100;
        b.heightDiv = 75;
        b.currentElement = &el;
        gX_GUI_Builder_RectAnchored(&b, 2.0f, 1.0f,
            GX_GUI_ANCHOR_END, GX_GUI_ANCHOR_END, 0.0f, 0.0f);
        if (gX_Test_F32_Delta(el.rect.x, 600.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.y, 525.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.width, 200.0f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 5 FAIL: anchor end %f/%f",
                (double)el.rect.x, (double)el.rect.y);
            errors++;
        }
        gX_GUI_Builder_RectAnchored(&b, 2.0f, 1.0f,
            GX_GUI_ANCHOR_CENTER, GX_GUI_ANCHOR_CENTER, 0.0f, 0.0f);
        if (gX_Test_F32_Delta(el.rect.x, 300.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.y, 262.5f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 5 FAIL: anchor center %f/%f",
                (double)el.rect.x, (double)el.rect.y);
            errors++;
        }
        gX_GUI_Builder_RectPercent(&b, 0.5f, 0.5f, 0.5f, 0.5f);
        if (gX_Test_F32_Delta(el.rect.x, 400.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.y, 300.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.width, 400.0f) > 0.01f
            || gX_Test_F32_Delta(el.rect.height, 300.0f) > 0.01f)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 5 FAIL: percent %f/%f",
                (double)el.rect.x, (double)el.rect.y);
            errors++;
        }
    }
    //opencode: nested capture — grandchildren owned by a closed inner
    //   container are skipped by the outer capture (no double-parenting).
    {
        gX_GUI_Canvas *c = NULL;
        gX_GUI_Element *elems = NULL;
        gX_GUI_Element_Layout *outer = NULL;
        gX_GUI_Element_Layout *inner = NULL;
        gX_Heap_Zero(&gui.canvases);
        gX_Heap_SetBlockSize(&gui.canvases, sizeof(gX_GUI_Canvas));
        gX_Heap_SetMax(&gui.canvases, 2);
        gX_Heap_SetAmount(&gui.canvases, 0);
        if (gX_Heap_Init(&gui.canvases) != 0)
        {
            gX_TraceLog(gX_LOG_INFO, "LAYOUT 6 FAIL: canvas heap init");
            errors++;
        }
        else
        {
            gX_U32 i = 0;
            gX_GUI_Add_Canvas();
            c = &((gX_GUI_Canvas*)gui.canvases.data)[0];
            for (i = 0; i < 6; i++)
            {
                if (gX_GUI_Canvas_Add_Element(c) < 0)
                {
                    gX_TraceLog(gX_LOG_INFO, "LAYOUT 6 FAIL: element add %u", i);
                    errors++;
                }
            }
            elems = (gX_GUI_Element*)c->elements.data;
            for (i = 0; i < 6; i++) elems[i].id = i;
            gX_GUI_Element_Add_Layout(&elems[0]);
            gX_GUI_Element_Add_Layout(&elems[2]);
            outer = gX_GUI_Element_Layout_Get(&elems[0], 0);
            inner = gX_GUI_Element_Layout_Get(&elems[2], 0);
            if (!outer || !inner)
            {
                gX_TraceLog(gX_LOG_INFO, "LAYOUT 6 FAIL: layout add/get");
                errors++;
            }
            else
            {
                //opencode: simulate the already-closed inner container
                //   owning slots 3..4; outer must capture 1, 2, 5 only.
                inner->childCount = 2;
                inner->children[0] = 3;
                inner->children[1] = 4;
                inner->memberFirst = 3;
                inner->memberLast = 4;
                gX_GUI_Canvas_Layout_Capture(c, 0, outer);
                if (outer->childCount != 3
                    || outer->children[0] != 1
                    || outer->children[1] != 2
                    || outer->children[2] != 5)
                {
                    gX_TraceLog(gX_LOG_INFO, "LAYOUT 6 FAIL: children %u [%d %d %d]",
                        outer->childCount,
                        outer->childCount > 0 ? outer->children[0] : -9,
                        outer->childCount > 1 ? outer->children[1] : -9,
                        outer->childCount > 2 ? outer->children[2] : -9);
                    errors++;
                }
                if (outer->memberFirst != 1 || outer->memberLast != 5)
                {
                    gX_TraceLog(gX_LOG_INFO, "LAYOUT 6 FAIL: range %d..%d",
                        outer->memberFirst, outer->memberLast);
                    errors++;
                }
            }
            gX_GUI_Canvas_Unload(c);
            gX_Heap_DeInit(&gui.canvases);
            gX_Heap_Zero(&gui.canvases);
        }
    }
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "LAYOUT tests: all passed");
    return errors;
}

//opencode: build deterministic nested markup heaps for the lifecycle tests.
static gX_I1 gX_Test_MarkupHeap_Init(gX_Heap *heap, gX_U32 blockSize,
    gX_U32 amount)
{
    if (!heap || blockSize == 0 || amount == 0) return false;
    gX_Heap_Zero(heap);
    gX_Heap_SetBlockSize(heap, blockSize);
    gX_Heap_SetMax(heap, amount);
    gX_Heap_SetAmount(heap, amount);
    if (gX_Heap_Init(heap) != 0 || !heap->data || !heap->meta) return false;
    for (gX_U32 i = 0; i < amount; i++) heap->meta[i].USED = true;
    return true;
}

//opencode: initialize a root markup heap with all requested slots live.
static gX_I1 gX_Test_Markup_Init(gX_Markup *markup, gX_U32 amount)
{
    if (!markup || !gX_Test_MarkupHeap_Init(&markup->elements,
        sizeof(gX_MarkupElement), amount)) return false;
    return true;
}

//opencode: dock RT resize must pool old targets and mark new-size targets
//   unloaded/pending so the allocator replaces their backing storage.
static gX_I32 gX_Tests_GUI_DockResize(gX_I0)
{
    gX_I32 errors = 0;
    gX_U32 savedWindowWidth = maindata.windowWidth;
    gX_U32 savedWindowHeight = maindata.windowHeight;
    gX_RenderTexture_Wrapper savedPing = gui.ping;
    gX_RenderTexture_Wrapper savedPong = gui.pong;
    gX_RenderTexture_Wrapper savedBackground = gui.composite.background;
    gX_I0 *savedPool = gX_Malloc(sizeof(gui.bufferPool));
    if (!savedPool) return 1;
    memcpy(savedPool, gui.bufferPool, sizeof(gui.bufferPool));
    memset(gui.bufferPool, 0, sizeof(gui.bufferPool));

    gui.ping = (gX_RenderTexture_Wrapper){0};
    gui.pong = (gX_RenderTexture_Wrapper){0};
    gui.composite.background = (gX_RenderTexture_Wrapper){0};
    gui.ping.width = 800;
    gui.ping.height = 600;
    gui.ping.rt.id = 101;
    gui.ping.loadingState.loaded = true;
    gui.pong.width = 800;
    gui.pong.height = 600;
    gui.pong.rt.id = 102;
    gui.pong.loadingState.loaded = true;
    gui.composite.background.width = 800;
    gui.composite.background.height = 600;
    gui.composite.background.rt.id = 103;
    gui.composite.background.loadingState.loaded = true;
    maindata.windowWidth = 1280;
    maindata.windowHeight = 720;

    gX_GUI_Test_Dock_RTs_WindowSize();
    if (gui.ping.width != 1280 || gui.ping.height != 720
        || gui.pong.width != 1280 || gui.pong.height != 720
        || gui.composite.background.width != 1280
        || gui.composite.background.height != 720) errors++;
    if (!gui.ping.loadingState.load || gui.ping.loadingState.loaded
        || !gui.pong.loadingState.load || gui.pong.loadingState.loaded
        || !gui.composite.background.loadingState.load
        || gui.composite.background.loadingState.loaded) {
        gX_TraceLog(gX_LOG_INFO, "GUI DOCK RESIZE FAIL: targets not pending allocation");
        errors++;
    }
    if (!gui.bufferPool[0].used || !gui.bufferPool[1].used
        || !gui.bufferPool[2].used
        || gui.bufferPool[0].rt.rt.id != 101
        || gui.bufferPool[1].rt.rt.id != 102
        || gui.bufferPool[2].rt.rt.id != 103
        || gui.bufferPool[0].width != 800 || gui.bufferPool[0].height != 600
        || gui.bufferPool[1].width != 800 || gui.bufferPool[1].height != 600
        || gui.bufferPool[2].width != 800 || gui.bufferPool[2].height != 600) {
        gX_TraceLog(gX_LOG_INFO, "GUI DOCK RESIZE FAIL: old targets not pooled");
        errors++;
    }

    maindata.windowWidth = savedWindowWidth;
    maindata.windowHeight = savedWindowHeight;
    gui.ping = savedPing;
    gui.pong = savedPong;
    gui.composite.background = savedBackground;
    memcpy(gui.bufferPool, savedPool, sizeof(gui.bufferPool));
    gX_Free(savedPool);
    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "GUI DOCK RESIZE tests: all passed");
    return errors;
}

//opencode: removing an active modal canvas must close its scope and restore
//   the background canvas focus instead of leaving stale IDs behind.
static gX_I32 gX_Tests_GUI_ModalCanvasRemoval(gX_I0)
{
    gX_I32 errors = 0;
    gX_Heap_Zero(&gui.canvases);
    gX_Heap_SetBlockSize(&gui.canvases, sizeof(gX_GUI_Canvas));
    gX_Heap_SetMax(&gui.canvases, 2);
    gX_Heap_SetAmount(&gui.canvases, 0);
    if (gX_Heap_Init(&gui.canvases) != 0) return 1;
    gui.focusEnabled = true;
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gX_GUI_PointerCapture_Clear();

    for (gX_U32 i = 0; i < 2; i++) {
        if (gX_GUI_Add_Canvas() != 0) errors++;
    }
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas *)gui.canvases.data;
    for (gX_U32 i = 0; i < 2; i++) {
        if (gX_GUI_Canvas_Add_Element(&canvases[i]) != 0) errors++;
        else {
            gX_GUI_Element *element = &((gX_GUI_Element *)canvases[i].elements.data)[0];
            element->visible = true;
            element->state.FOCUSABLE = true;
        }
    }
    if (!gX_GUI_Focus_Set(0, 0) || gX_GUI_Modal_Open(1, false) != 0
        || !gX_GUI_Focus_Set(1, 0)) errors++;
    if (gX_GUI_Remove_Canvas(1) != 0) errors++;
    if (gX_GUI_Modal_IsOpen() || !gX_GUI_Canvas_InputAllowed(0)
        || gui.focusCanvasID != 0 || gui.focusElementID != 0) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MODAL REMOVE FAIL: removed modal retained scope/focus (%d,%d,%d)",
            gui.modalCanvasID, gui.focusCanvasID, gui.focusElementID);
        errors++;
    }

    if (gX_GUI_Modal_IsOpen()) gX_GUI_Modal_Close();
    if (gui.canvases.data && gui.canvases.meta[0].USED)
        gX_GUI_Canvas_Unload(&((gX_GUI_Canvas *)gui.canvases.data)[0]);
    gX_Heap_DeInit(&gui.canvases);
    gX_Heap_Zero(&gui.canvases);
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gX_GUI_PointerCapture_Clear();
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI MODAL REMOVE tests: all passed");
    return errors;
}

//opencode: repeated markup builds must not alias callback payloads, and
//   explicit visibility must survive tree-to-canvas construction.
static gX_I32 gX_Tests_GUI_MarkupRegressions(gX_I0)
{
    gX_I32 errors = 0;
    gX_Markup markupA = {0};
    gX_Markup markupB = {0};
    gX_Markup visibleMarkup = {0};
    gX_GUI_Canvas canvasA = {0};
    gX_GUI_Canvas canvasB = {0};
    gX_GUI_Canvas visibleCanvas = {0};
    gX_I1 canvasALoaded = false;
    gX_I1 canvasBLoaded = false;
    gX_I1 visibleCanvasLoaded = false;

    if (!gX_Test_Markup_Init(&markupA, 1)
        || !gX_Test_Markup_Init(&markupB, 1)
        || !gX_Test_Markup_Init(&visibleMarkup, 3)) {
        errors++;
        goto markup_cleanup;
    }
    snprintf(markupA.script, sizeof(markupA.script), "%s", "script-a.as");
    snprintf(markupB.script, sizeof(markupB.script), "%s", "script-b.as");
    snprintf(((gX_MarkupElement *)markupA.elements.data)[0].type,
        sizeof(((gX_MarkupElement *)markupA.elements.data)[0].type), "%s", "button");
    snprintf(((gX_MarkupElement *)markupA.elements.data)[0].onclick,
        sizeof(((gX_MarkupElement *)markupA.elements.data)[0].onclick), "%s", "HandlerA()");
    snprintf(((gX_MarkupElement *)markupB.elements.data)[0].type,
        sizeof(((gX_MarkupElement *)markupB.elements.data)[0].type), "%s", "button");
    snprintf(((gX_MarkupElement *)markupB.elements.data)[0].onclick,
        sizeof(((gX_MarkupElement *)markupB.elements.data)[0].onclick), "%s", "HandlerB()");

    gX_MarkupElement *visibilityElements = visibleMarkup.elements.data;
    snprintf(visibilityElements[0].type, sizeof(visibilityElements[0].type), "%s", "rect");
    visibilityElements[0].visible = false;
    snprintf(visibilityElements[1].type, sizeof(visibilityElements[1].type), "%s", "rect");
    visibilityElements[1].visible = true;
    visibilityElements[2].visible = false;
    if (!gX_Test_MarkupHeap_Init(&visibilityElements[2].children,
        sizeof(gX_MarkupElement), 1)) {
        errors++;
        goto markup_cleanup;
    }
    gX_MarkupElement *hiddenChild = visibilityElements[2].children.data;
    snprintf(hiddenChild[0].type, sizeof(hiddenChild[0].type), "%s", "rect");
    hiddenChild[0].visible = false;

    canvasALoaded = gX_GUI_Canvas_Load(&canvasA) == 0;
    canvasBLoaded = gX_GUI_Canvas_Load(&canvasB) == 0;
    visibleCanvasLoaded = gX_GUI_Canvas_Load(&visibleCanvas) == 0;
    if (!canvasALoaded || !canvasBLoaded || !visibleCanvasLoaded) {
        errors++;
        goto markup_cleanup;
    }
    canvasA.canvasBox = (gX_Rectangle){0, 0, 800, 600};
    canvasB.canvasBox = (gX_Rectangle){0, 0, 800, 600};
    visibleCanvas.canvasBox = (gX_Rectangle){0, 0, 800, 600};

    gX_GUI_Markup_Build(&markupA, &canvasA);
    gX_GUI_Element *elementsA = canvasA.elements.data;
    gX_GUI_Element_Data *dataA = gX_GUI_Element_Data_Get(&elementsA[0], 0);
    gX_U8 callbackSnapshot[gX_GUI_ELEMENT_CALLBACK_CTX_MAX] = {0};
    if (dataA && dataA->usesCallbackContext)
        memcpy(callbackSnapshot, dataA->callbackCtx.bytes, sizeof(callbackSnapshot));

    gX_GUI_Markup_Build(&markupB, &canvasB);
    gX_GUI_Element *elementsB = canvasB.elements.data;
    gX_GUI_Element_Data *dataB = gX_GUI_Element_Data_Get(&elementsB[0], 0);
    if (!dataA || !dataB || !dataA->usesCallbackContext
        || !dataB->usesCallbackContext
        || !dataA->callbackReceivesOwnerData
        || !dataB->callbackReceivesOwnerData
        || strcmp(dataA->format, "script-a.as") != 0
        || strcmp(dataB->format, "script-b.as") != 0
        || strcmp((gX_I8*)dataA->callbackCtx.bytes, "HandlerA") != 0
        || strcmp((gX_I8*)dataB->callbackCtx.bytes, "HandlerB") != 0
        || memcmp(callbackSnapshot, dataA->callbackCtx.bytes,
            sizeof(callbackSnapshot)) != 0
        || dataA == dataB) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP CALLBACK FAIL: later build changed an earlier handler");
        errors++;
    }

    gX_GUI_Markup_Build(&visibleMarkup, &visibleCanvas);
    gX_GUI_Element *visibleElements = visibleCanvas.elements.data;
    if (visibleCanvas.elements.amount < 4
        || visibleElements[0].visible || !visibleElements[1].visible
        || visibleElements[2].visible || visibleElements[3].visible) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP VISIBLE FAIL: explicit visibility was not preserved");
        errors++;
    }

markup_cleanup:
    if (canvasALoaded) gX_GUI_Canvas_Unload(&canvasA);
    if (canvasBLoaded) gX_GUI_Canvas_Unload(&canvasB);
    if (visibleCanvasLoaded) gX_GUI_Canvas_Unload(&visibleCanvas);
    gX_GUI_Markup_Clear(&markupA);
    gX_GUI_Markup_Clear(&markupB);
    gX_GUI_Markup_Clear(&visibleMarkup);
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI MARKUP regressions tests: all passed");
    return errors;
}

//opencode: allocator tracker assertions make nested cleanup leaks observable.
static gX_U32 gX_Test_HeapTracker_Count(gX_I0)
{
    gX_U32 count = 0;
    for (gX_U32 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++)
        if (heaptracker.instances[i].used) count++;
    return count;
}

//opencode: check whether a specific allocation is still tracked as live.
static gX_I1 gX_Test_HeapTracker_Has(gX_I0 *address)
{
    for (gX_U32 i = 0; i < GX_HEAPINSTANCES_AMOUNT; i++) {
        if (heaptracker.instances[i].used
            && heaptracker.instances[i].address == (gX_I64)address) return true;
    }
    return false;
}

//opencode: cleanup must recursively release arbitrarily nested child heaps.
static gX_I32 gX_Tests_GUI_MarkupRecursiveCleanup(gX_I0)
{
    gX_I32 errors = 0;
    gX_U32 before = gX_Test_HeapTracker_Count();
    gX_Markup markup = {0};
    gX_I0 *deepData = NULL;
    gX_I0 *deepMeta = NULL;
    if (!gX_Test_Markup_Init(&markup, 1)) return 1;

    gX_MarkupElement *level0 = markup.elements.data;
    if (!gX_Test_MarkupHeap_Init(&level0[0].children,
        sizeof(gX_MarkupElement), 1)) errors++;
    if (errors == 0) {
        gX_MarkupElement *level1 = level0[0].children.data;
        if (!gX_Test_MarkupHeap_Init(&level1[0].children,
            sizeof(gX_MarkupElement), 1)) errors++;
    }
    if (errors == 0) {
        gX_MarkupElement *level1 = level0[0].children.data;
        gX_MarkupElement *level2 = level1[0].children.data;
        if (!gX_Test_MarkupHeap_Init(&level2[0].children,
            sizeof(gX_MarkupElement), 1)) errors++;
        else {
            deepData = level2[0].children.data;
            deepMeta = (gX_I0 *)level2[0].children.meta;
        }
    }

    gX_GUI_Markup_Clear(&markup);
    if (deepData && gX_Test_HeapTracker_Has(deepData)) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP CLEAR FAIL: deeply nested element data remains allocated");
        errors++;
        gX_Free(deepData);
    }
    if (deepMeta && gX_Test_HeapTracker_Has(deepMeta)) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP CLEAR FAIL: deeply nested metadata remains allocated");
        errors++;
        gX_Free(deepMeta);
    }
    if (gX_Test_HeapTracker_Count() != before) {
        gX_TraceLog(gX_LOG_INFO, "GUI MARKUP CLEAR FAIL: allocation count changed (%u -> %u)",
            before, gX_Test_HeapTracker_Count());
        errors++;
    }
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI MARKUP CLEAR tests: all passed");
    return errors;
}

//opencode: nested containers — children inherit the parent's visibility and
//   every callback keeps its own owning element's context, including across
//   a rebuild of the same markup into a fresh canvas.
static gX_I32 gX_Tests_GUI_MarkupOwnership(gX_I0)
{
    gX_I32 errors = 0;
    gX_Markup nested = {0};
    gX_GUI_Canvas canvasA = {0};
    gX_GUI_Canvas canvasB = {0};
    gX_I1 canvasALoaded = false;
    gX_I1 canvasBLoaded = false;

    if (!gX_Test_Markup_Init(&nested, 1)) return 1;
    snprintf(nested.script, sizeof(nested.script), "%s", "nested.as");

    gX_MarkupElement *root = nested.elements.data;
    snprintf(root[0].type, sizeof(root[0].type), "%s", "container");
    root[0].visible = false;
    if (!gX_Test_MarkupHeap_Init(&root[0].children,
        sizeof(gX_MarkupElement), 2)) {
        gX_GUI_Markup_Clear(&nested);
        return 1;
    }
    gX_MarkupElement *kids = root[0].children.data;
    snprintf(kids[0].type, sizeof(kids[0].type), "%s", "button");
    snprintf(kids[0].onclick, sizeof(kids[0].onclick), "%s", "HandlerChildA()");
    kids[0].visible = true;
    snprintf(kids[1].type, sizeof(kids[1].type), "%s", "button");
    snprintf(kids[1].onclick, sizeof(kids[1].onclick), "%s", "HandlerChildB()");
    kids[1].visible = true;

    canvasALoaded = gX_GUI_Canvas_Load(&canvasA) == 0;
    canvasBLoaded = gX_GUI_Canvas_Load(&canvasB) == 0;
    if (!canvasALoaded || !canvasBLoaded) {
        errors++;
        goto ownership_cleanup;
    }
    canvasA.canvasBox = (gX_Rectangle){0, 0, 800, 600};
    canvasB.canvasBox = (gX_Rectangle){0, 0, 800, 600};

    gX_GUI_Markup_Build(&nested, &canvasA);
    gX_GUI_Element *elements = canvasA.elements.data;
    if (canvasA.elements.amount != 3
        || elements[0].type != gX_GUI_ELEMENT_TYPE_CONTAINER
        || elements[1].type != gX_GUI_ELEMENT_TYPE_BUTTON
        || elements[2].type != gX_GUI_ELEMENT_TYPE_BUTTON
        || elements[0].visible || elements[1].visible || elements[2].visible) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP OWNERSHIP FAIL: nested shape or inherited visibility");
        errors++;
    }

    gX_GUI_Element_Data *dataA = gX_GUI_Element_Data_Get(&elements[1], 0);
    gX_GUI_Element_Data *dataB = gX_GUI_Element_Data_Get(&elements[2], 0);
    if (!dataA || !dataB || !dataA->usesCallbackContext
        || !dataB->usesCallbackContext
        || !dataA->callbackReceivesOwnerData
        || !dataB->callbackReceivesOwnerData
        || strcmp(dataA->format, "nested.as") != 0
        || strcmp(dataB->format, "nested.as") != 0
        || strcmp((gX_I8 *)dataA->callbackCtx.bytes, "HandlerChildA") != 0
        || strcmp((gX_I8 *)dataB->callbackCtx.bytes, "HandlerChildB") != 0
        || memcmp(dataA->callbackCtx.bytes, dataB->callbackCtx.bytes,
            sizeof(dataA->callbackCtx.bytes)) == 0) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP OWNERSHIP FAIL: child handlers not isolated");
        errors++;
    }

    gX_GUI_Markup_Build(&nested, &canvasB);
    gX_GUI_Element *elementsB = canvasB.elements.data;
    gX_GUI_Element_Data *dataAB = canvasB.elements.amount > 1
        ? gX_GUI_Element_Data_Get(&elementsB[1], 0) : NULL;
    gX_GUI_Element_Data *dataBB = canvasB.elements.amount > 2
        ? gX_GUI_Element_Data_Get(&elementsB[2], 0) : NULL;
    if (canvasB.elements.amount != 3 || !dataAB || !dataBB
        || strcmp((gX_I8 *)dataAB->callbackCtx.bytes, "HandlerChildA") != 0
        || strcmp((gX_I8 *)dataBB->callbackCtx.bytes, "HandlerChildB") != 0
        || (dataA && memcmp(dataAB->callbackCtx.bytes, dataA->callbackCtx.bytes,
            sizeof(dataAB->callbackCtx.bytes)) != 0)
        || (dataB && memcmp(dataBB->callbackCtx.bytes, dataB->callbackCtx.bytes,
            sizeof(dataBB->callbackCtx.bytes)) != 0)) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI MARKUP OWNERSHIP FAIL: rebuild changed a child handler");
        errors++;
    }

ownership_cleanup:
    if (canvasALoaded) gX_GUI_Canvas_Unload(&canvasA);
    if (canvasBLoaded) gX_GUI_Canvas_Unload(&canvasB);
    gX_GUI_Markup_Clear(&nested);
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI MARKUP OWNERSHIP tests: all passed");
    return errors;
}

//opencode: WM resize sync — same-size and cramped sizes are no-ops; a real
//   resize syncs maindata, re-fits the viewport aspect box and re-targets the
//   window-sized RTs (olds pooled); the cramped dock falls back to overlay.
static gX_I32 gX_Tests_GUI_WMResize(gX_I0)
{
    gX_I32 errors = 0;
    gX_U32 savedWindowWidth = maindata.windowWidth;
    gX_U32 savedWindowHeight = maindata.windowHeight;
    gX_U32 savedScreenWidth = maindata.screenWidth;
    gX_U32 savedScreenHeight = maindata.screenHeight;
    gX_Rectangle savedViewportBox = gui.viewportBox;
    gX_I1 savedDocked = gui.viewportDocked;
    gX_F32 savedInset = gui.viewportRightInset;
    gX_RenderTexture_Wrapper savedPing = gui.ping;
    gX_RenderTexture_Wrapper savedPong = gui.pong;
    gX_RenderTexture_Wrapper savedBackground = gui.composite.background;
    gX_I0 *savedPool = gX_Malloc(sizeof(gui.bufferPool));
    if (!savedPool) return 1;
    memcpy(savedPool, gui.bufferPool, sizeof(gui.bufferPool));
    memset(gui.bufferPool, 0, sizeof(gui.bufferPool));

    //opencode: the test-only path boots without a window, so pin a known
    //   render size for the aspect fit and the dock-width math.
    maindata.screenWidth = 1024;
    maindata.screenHeight = 768;

    gui.ping = (gX_RenderTexture_Wrapper){0};
    gui.pong = (gX_RenderTexture_Wrapper){0};
    gui.composite.background = (gX_RenderTexture_Wrapper){0};
    gui.ping.width = 800;
    gui.ping.height = 600;
    gui.ping.rt.id = 101;
    gui.ping.loadingState.loaded = true;
    gui.pong.width = 800;
    gui.pong.height = 600;
    gui.pong.rt.id = 102;
    gui.pong.loadingState.loaded = true;
    gui.composite.background.width = 800;
    gui.composite.background.height = 600;
    gui.composite.background.rt.id = 103;
    gui.composite.background.loadingState.loaded = true;
    maindata.windowWidth = 800;
    maindata.windowHeight = 600;
    gui.viewportDocked = 0;

    //opencode: duplicate and cramped live sizes are ignored.
    if (gX_GUI_WindowResize_Sync(800, 600)
        || gX_GUI_WindowResize_Sync(300, 200)
        || maindata.windowWidth != 800 || maindata.windowHeight != 600
        || gui.ping.loadingState.load) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: ignored sizes still synced");
        errors++;
    }

    //opencode: a real resize syncs the window state, re-fits the viewport
    //   with the screen aspect and re-targets the window-sized RTs.
    if (!gX_GUI_WindowResize_Sync(1280, 720)) {
        gX_TraceLog(gX_LOG_INFO, "GUI WM RESIZE FAIL: live size not applied");
        errors++;
    }
    if (maindata.windowWidth != 1280 || maindata.windowHeight != 720) {
        gX_TraceLog(gX_LOG_INFO, "GUI WM RESIZE FAIL: maindata not synced");
        errors++;
    }
    gX_F32 screenAspect = (gX_F32)maindata.screenWidth
        / (gX_F32)maindata.screenHeight;
    gX_F32 windowAspect = 1280.0f / 720.0f;
    gX_Rectangle expected;
    if (windowAspect > screenAspect + 0.001f) {
        gX_F32 viewportWidth = 720.0f * screenAspect;
        expected = (gX_Rectangle){(1280.0f - viewportWidth) * 0.5f, 0.0f,
            viewportWidth, 720.0f};
    } else if (screenAspect > windowAspect + 0.001f) {
        gX_F32 viewportHeight = 1280.0f / screenAspect;
        expected = (gX_Rectangle){0.0f, (720.0f - viewportHeight) * 0.5f,
            1280.0f, viewportHeight};
    } else {
        expected = (gX_Rectangle){0.0f, 0.0f, 1280.0f, 720.0f};
    }
    if (fabsf(gui.viewportBox.x - expected.x) > 0.5f
        || fabsf(gui.viewportBox.y - expected.y) > 0.5f
        || fabsf(gui.viewportBox.width - expected.width) > 0.5f
        || fabsf(gui.viewportBox.height - expected.height) > 0.5f) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: viewport box not re-fit (%.1fx%.1f at %.1f,%.1f)",
            gui.viewportBox.width, gui.viewportBox.height,
            gui.viewportBox.x, gui.viewportBox.y);
        errors++;
    }
    if (gui.ping.width != 1280 || gui.ping.height != 720
        || gui.pong.width != 1280 || gui.pong.height != 720
        || gui.composite.background.width != 1280
        || gui.composite.background.height != 720
        || !gui.ping.loadingState.load || gui.ping.loadingState.loaded
        || !gui.pong.loadingState.load || gui.pong.loadingState.loaded
        || !gui.composite.background.loadingState.load
        || gui.composite.background.loadingState.loaded) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: RTs not re-targeted at the new size");
        errors++;
    }
    if (!gui.bufferPool[0].used || !gui.bufferPool[1].used
        || !gui.bufferPool[2].used
        || gui.bufferPool[0].width != 800 || gui.bufferPool[0].height != 600
        || gui.bufferPool[1].width != 800 || gui.bufferPool[1].height != 600
        || gui.bufferPool[2].width != 800 || gui.bufferPool[2].height != 600) {
        gX_TraceLog(gX_LOG_INFO, "GUI WM RESIZE FAIL: old targets not pooled");
        errors++;
    }

    //opencode: a duplicate call does not churn the RT state again.
    gX_U32 pooledBefore = 0;
    for (gX_U32 i = 0; i < GX_GUI_BUFFER_POOL_MAX; i++)
        if (gui.bufferPool[i].used) pooledBefore++;
    if (gX_GUI_WindowResize_Sync(1280, 720)) {
        gX_TraceLog(gX_LOG_INFO, "GUI WM RESIZE FAIL: duplicate call resynced");
        errors++;
    }
    gX_U32 pooledAfter = 0;
    for (gX_U32 i = 0; i < GX_GUI_BUFFER_POOL_MAX; i++)
        if (gui.bufferPool[i].used) pooledAfter++;
    if (pooledBefore != pooledAfter) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: duplicate call churned the pool (%u -> %u)",
            pooledBefore, pooledAfter);
        errors++;
    }

    //opencode: a docked window too cramped for the minimum viewport width
    //   falls back to overlay; a wide one stays docked.
    gui.viewportDocked = 1;
    gui.viewportRightInset = 0.0f;
    maindata.windowWidth = (gX_U32)(maindata.screenWidth / 2);
    maindata.windowHeight = maindata.screenHeight;
    if (!gX_GUI_Dock_Cramped_Check()) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: cramped dock stayed docked");
        errors++;
    }
    if (gui.viewportDocked) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: cramped fallback did not flip the flag");
        errors++;
    }
    gui.viewportDocked = 1;
    maindata.windowWidth = maindata.screenWidth * 3;
    if (gX_GUI_Dock_Cramped_Check() || !gui.viewportDocked) {
        gX_TraceLog(gX_LOG_INFO,
            "GUI WM RESIZE FAIL: wide window left the dock");
        errors++;
    }

    maindata.windowWidth = savedWindowWidth;
    maindata.windowHeight = savedWindowHeight;
    maindata.screenWidth = savedScreenWidth;
    maindata.screenHeight = savedScreenHeight;
    gui.viewportBox = savedViewportBox;
    gui.viewportDocked = savedDocked;
    gui.viewportRightInset = savedInset;
    gui.ping = savedPing;
    gui.pong = savedPong;
    gui.composite.background = savedBackground;
    memcpy(gui.bufferPool, savedPool, sizeof(gui.bufferPool));
    gX_Free(savedPool);
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI WM RESIZE tests: all passed");
    return errors;
}

//ai: polisher — register/find/report/reset/env semantics, headless.
static gX_I32 gX_Tests_Polisher(gX_I0)
{
    gX_I32 errors = 0;
    gX_F32 fval = 1.0f;
    gX_I32 ival = 2;
    gX_Polisher_RegisterFloat("TEST_FLOAT_DEFVAL", "Test", &fval, 0.0f,
        10.0f, 0.1f);
    gX_Polisher_RegisterInt("TEST_INT_DEFVAL", "Test", &ival, 0, 100, 1);
    gX_U32 count = polisher.count;
    // idempotent re-registration keeps one entry per name
    gX_Polisher_RegisterFloat("TEST_FLOAT_DEFVAL", "Test", &fval, 0.0f,
        10.0f, 0.1f);
    if (polisher.count != count || count < 2) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: registration count");
        errors++;
    }
    gX_Polisher_Entry *entry = gX_Polisher_Find("TEST_FLOAT_DEFVAL");
    if (!entry || entry->f32Original != 1.0f || entry->type
            != GX_POLISHER_TYPE_F32) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: find/original");
        errors++;
    }
    if (gX_Polisher_Find("NO_SUCH_DEFVAL") != NULL) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: unknown find");
        errors++;
    }
    // modify -> report -> reset
    fval = 3.5f;
    if (entry) entry->modified = 1;
    gX_I8 report[512];
    gX_Polisher_GenerateReport(report, sizeof(report));
    if (!strstr((const char *)report, "TEST_FLOAT_DEFVAL")
        || !strstr((const char *)report, "3.5")) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: report");
        errors++;
    }
    gX_Polisher_ResetAll();
    if (fval != 1.0f || ival != 2) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: reset");
        errors++;
    }
    // env parsing applies known names and ignores unknown ones (restore the
    //   previous value — a harness may have set GX_POLISHER for its own run)
    const char *previousEnv = getenv("GX_POLISHER");
    gX_I8 savedEnv[256] = {0};
    if (previousEnv)
        snprintf((char *)savedEnv, sizeof(savedEnv), "%s", previousEnv);
    setenv("GX_POLISHER", "TEST_INT_DEFVAL=7,NO_SUCH_DEFVAL=1", 1);
    gX_U32 countBefore = polisher.count;
    gX_Polisher_ApplyEnv();
    if (savedEnv[0]) setenv("GX_POLISHER", (const char *)savedEnv, 1);
    else unsetenv("GX_POLISHER");
    if (ival != 7 || polisher.count != countBefore) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: env apply");
        errors++;
    }
    if (gX_Polisher_Find("TEST_INT_DEFVAL")
        && gX_Polisher_Find("TEST_INT_DEFVAL")->modified != 1) {
        gX_TraceLog(gX_LOG_ERROR, "POLISHER TEST FAIL: env modified flag");
        errors++;
    }
    // restore the test entries for later suites
    gX_Polisher_ResetAll();
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "POLISHER_TESTS: all passed");
    return errors;
}

//ai: hardware tier classifier — synthetic caps only, no GL context needed.
static gX_I32 gX_Tests_Hardware(gX_I0)
{
    gX_I32 errors = 0;
    gX_I8 reason[192];
    gX_RHI_Caps caps;

    // below minimum: no GLSL 3.30
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 2; caps.glMinor = 1; caps.shaderModel = 120; caps.hasFBO = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_BELOW_MINIMUM) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: GLSL 120 not below minimum");
        errors++;
    }
    // below minimum: no framebuffer objects
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 3; caps.glMinor = 3; caps.shaderModel = 330; caps.hasFBO = 0;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_BELOW_MINIMUM) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: no-FBO caps not below minimum");
        errors++;
    }
    // minimum: desktop GL 3.3
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 3; caps.glMinor = 3; caps.shaderModel = 330; caps.hasFBO = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_MINIMUM) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: GL 3.3 not minimum");
        errors++;
    }
    // standard: compute + SSBO without the bindless/cube-map set
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 4; caps.glMinor = 3; caps.shaderModel = 430;
    caps.hasFBO = 1; caps.hasCompute = 1; caps.hasSSBO = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_STANDARD) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: GL 4.3 compute not standard");
        errors++;
    }
    // standard: GL 4.2 bindless-era raster path without compute
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 4; caps.glMinor = 2; caps.shaderModel = 420;
    caps.hasFBO = 1; caps.hasBindless = 1; caps.hasImmutableStorage = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_STANDARD) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: GL 4.2 bindless not standard");
        errors++;
    }
    // recommended: the full desktop feature set (capability-driven — the
    //   engine's default compatibility context advertises 3.3)
    caps = (gX_RHI_Caps){0};
    caps.glMajor = 3; caps.glMinor = 3; caps.shaderModel = 330;
    caps.hasFBO = 1; caps.hasCompute = 1; caps.hasSSBO = 1;
    caps.hasBindless = 1; caps.hasCubeMapArray = 1; caps.hasTessellation = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_RECOMMENDED) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: full feature caps not recommended");
        errors++;
    }
    // recommended requires the whole set: missing tessellation stays standard
    caps.hasTessellation = 0;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_STANDARD) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: partial feature set not standard");
        errors++;
    }
    // ES: 3.1 compute+SSBO is standard, 3.0 is minimum, 2.0 is below
    caps = (gX_RHI_Caps){0};
    caps.isGLES = 1; caps.glMajor = 3; caps.glMinor = 1; caps.shaderModel = 310;
    caps.hasFBO = 1; caps.hasCompute = 1; caps.hasSSBO = 1;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_STANDARD) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: ES 3.1 compute not standard");
        errors++;
    }
    caps.hasCompute = 0; caps.hasSSBO = 0;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_MINIMUM) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: ES 3.0 not minimum");
        errors++;
    }
    caps.shaderModel = 100;
    if (gX_Hardware_Tier_Classify(&caps, reason, sizeof(reason))
            != gX_HW_TIER_BELOW_MINIMUM) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: ES 2.0 not below minimum");
        errors++;
    }
    // tier strings stay stable (the gate greps them)
    if (strcmp(gX_Hardware_Tier_String(gX_HW_TIER_BELOW_MINIMUM),
            "below-minimum") != 0
        || strcmp(gX_Hardware_Tier_String(gX_HW_TIER_MINIMUM), "minimum") != 0
        || strcmp(gX_Hardware_Tier_String(gX_HW_TIER_STANDARD), "standard") != 0
        || strcmp(gX_Hardware_Tier_String(gX_HW_TIER_RECOMMENDED),
            "recommended") != 0) {
        gX_TraceLog(gX_LOG_ERROR, "HW TEST FAIL: tier strings changed");
        errors++;
    }
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "HW TESTS: all passed");
    return errors;
}

//ai: per-draw skin upload clamp — >128-bone skeletons must never upload past
//    the shader's boneMatrices[gX_SKIN_MAX_BONES] array.
static gX_I32 gX_Tests_SkinUploadClamp(gX_I0)
{
    gX_I32 errors = 0;
    const gX_I32 cases[][2] = {
        {0, 0}, {-1, 0}, {1, 1},
        {gX_SKIN_MAX_BONES - 1, gX_SKIN_MAX_BONES - 1},
        {gX_SKIN_MAX_BONES, gX_SKIN_MAX_BONES},
        {gX_SKIN_MAX_BONES + 1, gX_SKIN_MAX_BONES},
        {256, gX_SKIN_MAX_BONES},
    };
    for (gX_U32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        if (gX_Skin_UploadBoneCount(cases[i][0]) != cases[i][1])
        {
            gX_TraceLog(gX_LOG_ERROR,
                "SKIN TEST FAIL: clamp(%d) != %d", cases[i][0], cases[i][1]);
            errors++;
        }
    }
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "SKIN TESTS: upload clamp passed");
    return errors;
}

//ai: devcube 60 Hz phase — low render rates must not change the wall-clock
//    phase (20 fps = 3 ticks/frame, 30 fps = 2, 60 fps = 1).
static gX_I32 gX_Tests_DevCubePhase(gX_I0)
{
    gX_I32 errors = 0;
    const gX_I32 rates[3] = {20, 30, 60};
    for (gX_U32 r = 0; r < 3; r++)
    {
        gX_I32 fps = rates[r];
        gX_I64 previousTicks = 0;
        for (gX_I32 i = 0; i <= fps * 5; i++)
        {
            gX_F64 seconds = (gX_F64)i / (gX_F64)fps;
            gX_I64 elapsedNs = (gX_I64)(seconds * 1000000000.0 + 0.5);
            gX_I64 expected = (gX_I64)(seconds * 60.0 + 0.5);
            gX_I64 ticks = gX_DevCube_PhaseTicks(elapsedNs);
            if (ticks != expected || ticks < previousTicks)
            {
                gX_TraceLog(gX_LOG_ERROR,
                    "DEVCUBE TEST FAIL: %d fps frame %d ticks=%d expected=%d",
                    fps, i, (gX_I32)ticks, (gX_I32)expected);
                errors++;
                break;
            }
            previousTicks = ticks;
        }
        gX_I64 frameNs = (gX_I64)(1000000000.0 / (gX_F64)fps + 0.5);
        gX_I64 step = gX_DevCube_PhaseTicks(frameNs);
        gX_I64 expectedStep = 60 / fps;
        if (step != expectedStep)
        {
            gX_TraceLog(gX_LOG_ERROR,
                "DEVCUBE TEST FAIL: %d fps step=%d expected=%d",
                fps, (gX_I32)step, (gX_I32)expectedStep);
            errors++;
        }
    }
    if (gX_DevCube_PhaseTicks(1000000000LL) != 60
        || gX_DevCube_PhaseTicks(5000000000LL) != 300
        || gX_DevCube_PhaseTicks(-1) != 0
        || gX_DevCube_PhaseSeconds(1000000000LL) != 1.0)
        errors++;
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "DEVCUBE TESTS: 60Hz phase passed");
    return errors;
}

gX_I32 gX_Tests(gX_I0);
//ai: allocator regression failures participate in the test-only admission verdict.
gX_I32 gX_Tests(gX_I0) {
    gX_I32 errors = 0;
    
    errors += gX_Alloc_Tests();
    errors += gX_Tests_Hardware();
    errors += gX_Tests_Polisher();
    errors += gX_Tests_Math();
    errors += gX_Tests_Input();
    errors += gX_Tests_Input_Transitions();
    errors += gX_Tests_JSON();
    errors += gX_Tests_ShaderCompiler();
    errors += gX_Tests_SkinUploadClamp();
    errors += gX_Tests_DevCubePhase();
    //ai: physics wrapper integration (skips when ODE is unavailable)
    errors += gX_Tests_Physics();
    //ai: toolgun mode state (the shared editor crosshair/tool framework)
    errors += gX_Tests_Toolgun();
    //ai: synth offline-render suite (no audio device needed)
    errors += gX_Tests_Synth();
    //ai: SFX wave synthesis / mixer buses / reverb core (no audio device)
    errors += gX_Tests_AudioSFX();
    //ai: OPX 8-operator FM engine (offline render core, no audio device)
    errors += gX_Tests_OPX();
    //ai: PAD layered ensemble engine (offline render core, no audio device)
    errors += gX_Tests_Pad();
    //ai: shared filter core + master filter (no audio device)
    errors += gX_Tests_AudioFilter();
#if gX_ENABLE_RENDERER == 1
    errors += gX_Tests_Background();
    errors += gX_Tests_Sky();
    errors += gX_Tests_Raymarch();
    errors += gX_Tests_TextWriter();
    errors += gX_Tests_PostProcess();
    errors += gX_Tests_Battle();
    errors += gX_Tests_ScenePhysics();
    errors += gX_Tests_SceneImpactSound();
    errors += gX_Tests_SceneSynthVis();
    errors += gX_Tests_Renderer_SceneTransition();
    errors += gX_Tests_TemporalHistory();
    errors += gX_Tests_LightOrbit();
#endif
    errors += gX_Tests_Wrap();
    errors += gX_Tests_GUI_TextLayout();
    errors += gX_Tests_TextInput();
    errors += gX_Tests_CSS_Cascade();
    errors += gX_Tests_CSS_Overflow();
    errors += gX_Tests_CanvasTransform();
    errors += gX_Tests_StyleReload();
    errors += gX_Tests_StyleRoundTrip();
    errors += gX_Tests_GUI_CoreAudit();
    errors += gX_Tests_GUI_Interaction();
    errors += gX_Tests_GUI_TitleKeyboardBack();
    errors += gX_Tests_GUI_Dialog();
    errors += gX_Tests_GUI_Transition();
    errors += gX_Tests_GUI_CallbackContext();
    errors += gX_Tests_GUI_CallbackRebuild();
    errors += gX_Tests_GUI_ElementGeneration();
    errors += gX_Tests_GUI_EditorIdentity();
    errors += gX_Tests_ScrollMath();
    errors += gX_Tests_Layout();
    //opencode: targeted GUI audit regressions stay in the headless test gate.
    errors += gX_Tests_GUI_DockResize();
    errors += gX_Tests_GUI_ModalCanvasRemoval();
    errors += gX_Tests_GUI_MarkupRegressions();
    errors += gX_Tests_GUI_MarkupRecursiveCleanup();
    errors += gX_Tests_GUI_MarkupOwnership();
    errors += gX_Tests_GUI_WMResize();
    gX_I32 imguiInputErrors = gX_Tools_Editor_TestInputCapture();
    if (imguiInputErrors == 0)
        gX_TraceLog(gX_LOG_INFO, "GUI IMGUI input tests: all passed");
    else
        gX_TraceLog(gX_LOG_INFO, "GUI IMGUI input tests: %d errors", imguiInputErrors);
    errors += imguiInputErrors;
    //opencode: editor snapshot round-trip (canvas -> markup -> rebuild).
    gX_I32 snapshotErrors = gX_Tools_Editor_TestSnapshotRoundTrip();
    if (snapshotErrors != 0)
        gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip tests: %d errors", snapshotErrors);
    errors += snapshotErrors;
    
    /*gX_Heap testHeap = { 0 };
    gX_Heap_Tests(&testHeap);*/
    
    return errors;
}
#endif //GX_LEAN_RELEASE
