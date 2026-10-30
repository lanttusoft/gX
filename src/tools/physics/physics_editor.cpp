/**********************************************************************************************
*
*   [gX: src/tools/physics/physics_editor.cpp]
*   Physics editor — sandbox/fluid/thermal tuning with JSON presets
*
*   Copyright (c) 2026 lanttu, opencode
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


//opencode: engine-native surface over the sandbox's parameter struct
//   (gX_ScenePhysics_Tuning): world/content toggles and the shared EM/
//   thermal/fluid settings apply live to the running sandbox, and presets
//   round-trip under resources/tools/physics (shipped paths are refused
//   under GX_NO_SETTINGS_SAVE). Opened by --physics-editor,
//   GX_TOOLS_OPEN=physics, or the title EXTRAS menu; the opener routes to
//   the sandbox scene (the tuning target).
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through renderer.h would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

//ai: renderer access comes before the engine headers — gfx.h opens its own
//   extern "C" chain, and pulling the wrappers C++-first would clash with it
//   (the GFX editor's ordering rule).
#if gX_ENABLE_RENDERER == 1
    #include "../../renderer/gfx.h"
    #include "../../renderer/renderer.h"
#endif

#include "../imgui/imgui.h"

#include "../../scene/scene.h"
#include "../../scene/physics.h"
#include "../../physics/physics.h"
#include "../../physics/relativity.h"
#include "../../physics/fluid.h"
#include "../../common/log.h"
#include "../../common/enum/keys.h"
#include "../../wrappers/file.h"
#include "../../input.h"
#include "../../wrappers/input.h"
#include "../../wrappers/physics.h"
#include "../../camera.h"
#include "../../main.h"
//ai: vector ops (gX_F32v3_Add/MulScalar...) come from gxmath; in the
//   kernel profile the renderer headers are gone, so include it explicitly
//   instead of relying on the renderer's transitive chain.
#include "../../gxmath/gxmath.h"
#include "../toolgun.h"
#include "../../audio/sfx.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimePhysicsFlag(gX_I0);

//ai: the panel edits the sandbox scene, which the kernel profile compiles
//   out — the whole implementation is guarded and the three entry points
//   become no-ops there (previously the panel's renderer/scene references
//   broke `make check-kernel`).
#if gX_ENABLE_RENDERER == 1

//ai: free camera — the engine freezes the scene camera binds while any tool
//   is open (input.toolCameraInputEnabled), so the panel offers the GFX
//   editor's modal pattern: WASD/EQ + mouse look take the keyboard and the
//   cursor while active, and Esc hands both back to the panel.
static gX_I1 s_peFreeCamera = 0;
static gX_I1 s_peCursorBefore = 1;
//ai: a forced (capture-hook) camera ignores the cursor-exit: automated runs
//   force the cursor on every frame, which would end the mode instantly.
static gX_I1 s_peFreeCamForced = 0;
//ai: toolgun — the shared crosshair/tool mode riding the free camera; the
//   panel's Toolgun button (or GX_TOOLS_TOOLGUN=1) enters both together.
static gX_I1 s_peToolgun = 0;
static gX_I0 PeEditor_Toolgun(gX_I1 active);

static gX_I0 PeEditor_FreeCamera(gX_I1 active) {
    if ((active ? 1 : 0) == s_peFreeCamera) return;
    if (active) {
        s_peCursorBefore = input.cursorEnabled ? 1 : 0;
        s_peFreeCamera = 1;
        input.toolCameraInputEnabled = 1;
        input.cursorEnabled = 0;
        input.cursorSwitched = 1;
    } else {
        PeEditor_Toolgun(0);
        s_peFreeCamera = 0;
        input.toolCameraInputEnabled = 0;
        input.cursorEnabled = s_peCursorBefore;
        input.cursorSwitched = 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_EDITOR: free camera %s",
        s_peFreeCamera ? "on" : "off");
}

//ai: the toolgun's tools — each maps to a sandbox action on the crosshair
//   aim (grab/freeze/heat/impulse/pin/remove); the scene-side effects are the
//   gX_ScenePhysics_Tool* surface, the trivial ones call the wrapper.
#define GX_PE_TOOL_COUNT 6
static const gX_Tools_ToolgunTool s_peTools[GX_PE_TOOL_COUNT] = {
    {"GRAB", "LMB hold: spring-pull the aimed prop or rig part",
        {133, 224, 238, 255}},
    {"FREEZE", "LMB: toggle dynamic / kinematic",
        {150, 196, 255, 255}},
    {"HEAT", "LMB: dump heat into the aimed part",
        {255, 150, 82, 255}},
    {"IMPULSE", "LMB: shove the aimed part along the crosshair",
        {226, 138, 255, 255}},
    {"PIN", "LMB: pin in place, RMB: release the aimed part's pins",
        {255, 220, 127, 255}},
    {"REMOVE", "LMB: delete the aimed prop or ragdoll (rig links stay)",
        {255, 112, 112, 255}}
};

//ai: kernel-safe renderer access — the return type is opaque when the
//   renderer cluster is compiled out (a typed gX_RendererGFX* cannot be
//   declared then; the previous form broke `make check-kernel`).
static gX_I0 *PeEditor_Renderer(gX_I0) {
#if gX_ENABLE_RENDERER == 1
    if (renderer.gfx.data) return renderer.gfx.data;
#endif
    return 0;
}

//ai: the gun's aim — the screen-centre ray plus the model under the
//   crosshair, resolved to a sandbox object by the scene.
static gX_ScenePhysics_Aim PeEditor_Aim(gX_I0) {
    gX_F32v2 center = {
        (gX_F32)GetScreenWidth() * 0.5f,
        (gX_F32)GetScreenHeight() * 0.5f
    };
    gX_I0 *gfx = PeEditor_Renderer();
    gX_I32 picked = -1;
#if gX_ENABLE_RENDERER == 1
    if (gfx) picked = gX_RendererGFX_Model_Pick((gX_RendererGFX *)gfx, center, NULL);
#endif
    return gX_ScenePhysics_AimAtModel(&scene.physicsScene, picked);
}

//ai: the aim point — where the crosshair ray passes the body's centre; used
//   by the pin and by tools that need a point rather than a direction.
static gX_F32v3 PeEditor_AimPoint(gX_ScenePhysics_Aim aim, gX_Ray ray) {
    gX_F32v3 relative = gX_F32v3_Sub(aim.position, ray.position);
    gX_F32 distance = gX_F32v3_Dot(relative, ray.direction);
    if (distance < 0.5f) distance = 0.5f;
    return gX_F32v3_Add(ray.position,
        gX_F32v3_MulScalar(ray.direction, distance));
}

//ai: the toolgun's per-frame slice — input poll, aim resolution and the
//   current tool's action; runs from the panel draw while the mode is on.
static gX_I0 PeEditor_ToolgunFrame(gX_I0) {
    gX_Tools_Toolgun_Step();
    gX_Ray ray = gX_Mouse_GetRay(
        (gX_F32v2){(gX_F32)GetScreenWidth() * 0.5f,
            (gX_F32)GetScreenHeight() * 0.5f}, camera.camera3D);
    gX_ScenePhysics_Aim aim = PeEditor_Aim();
    gX_Tools_Toolgun_SetAim(aim.kind != gX_SCENE_PHYSICS_AIM_NONE);
    gX_I1 primaryDown = gX_Tools_Toolgun_Fire(0);
    gX_I1 primaryPressed = gX_Tools_Toolgun_FirePressed(0);
    gX_I1 secondaryPressed = gX_Tools_Toolgun_FirePressed(1);

    //ai: toolgun feedback — a short procedural tick on every trigger pull,
    //   pitched by the tool slot (each tool reads slightly differently) and
    //   positioned at the aim point (a spot just in front of the camera when
    //   the crosshair hits nothing).
    if (primaryPressed || secondaryPressed) {
        gX_F32v3 at = (aim.kind != gX_SCENE_PHYSICS_AIM_NONE)
            ? PeEditor_AimPoint(aim, ray)
            : gX_F32v3_Add(ray.position,
                gX_F32v3_MulScalar(ray.direction, 2.0f));
        gX_F32 pitch = 1.0f + 0.06f * (gX_F32)(gX_Tools_Toolgun_Tool() % 6);
        gX_AudioSFX_Play(GX_AUDIO_SFX_CLICK, at, 0.45f, pitch, 9.0f);
    }

    switch (gX_Tools_Toolgun_Tool()) {
        case 0: {
            gX_ScenePhysics_ToolGrab(&scene.physicsScene, aim, ray.position,
                ray.direction, primaryPressed, primaryDown);
            break;
        }
        case 1: {
            if (primaryPressed && aim.body.internal != 0) {
                gX_PhysicsBodyType type = gX_PhysicsBody_GetType(aim.body);
                gX_PhysicsBody_SetType(aim.body,
                    type == gX_PHYSICS_BODY_DYNAMIC
                        ? gX_PHYSICS_BODY_KINEMATIC
                        : gX_PHYSICS_BODY_DYNAMIC);
                gX_PhysicsBody_Enable(aim.body);
            }
            break;
        }
        case 2: {
            if (primaryPressed && aim.body.internal != 0) {
                gX_PhysicsThermal_HeatBody(aim.body,
                    scene.physicsScene.heatEnergy);
            }
            break;
        }
        case 3: {
            if (primaryPressed && aim.body.internal != 0
                && gX_PhysicsBody_GetType(aim.body)
                    == gX_PHYSICS_BODY_DYNAMIC) {
                gX_F32 mass = gX_PhysicsBody_GetMass(aim.body);
                if (mass > 0.0001f) {
                    gX_F32 delta = 45.0f / mass;
                    if (delta < 0.6f) delta = 0.6f;
                    if (delta > 12.0f) delta = 12.0f;
                    gX_F32v3 velocity =
                        gX_PhysicsBody_GetLinearVel(aim.body);
                    velocity = gX_F32v3_Add(velocity,
                        gX_F32v3_MulScalar(ray.direction, delta));
                    gX_PhysicsBody_SetLinearVel(aim.body, velocity);
                    gX_PhysicsBody_Enable(aim.body);
                }
            }
            break;
        }
        case 4: {
            if (primaryPressed && aim.body.internal != 0) {
                gX_ScenePhysics_ToolPin(&scene.physicsScene, aim,
                    PeEditor_AimPoint(aim, ray));
            }
            if (secondaryPressed && aim.body.internal != 0) {
                gX_ScenePhysics_ToolUnpin(&scene.physicsScene, aim);
            }
            break;
        }
        case 5: {
            if (primaryPressed) {
                gX_ScenePhysics_ToolRemove(&scene.physicsScene, aim);
            }
            break;
        }
        default: break;
    }
}

//ai: enter/leave the toolgun — it always rides the free camera, and the
//   scene's own mouse interaction stands down while it owns the buttons.
static gX_I0 PeEditor_Toolgun(gX_I1 active) {
    if ((active ? 1 : 0) == s_peToolgun) return;
    s_peToolgun = active ? 1 : 0;
    if (s_peToolgun) {
        if (!s_peFreeCamera) PeEditor_FreeCamera(1);
        gX_ScenePhysics_SetToolMode(&scene.physicsScene, 1);
        gX_Tools_Toolgun_Open(s_peTools, GX_PE_TOOL_COUNT);
    } else {
        gX_Tools_Toolgun_Close();
        gX_ScenePhysics_SetToolMode(&scene.physicsScene, 0);
    }
    gX_TraceLog(gX_LOG_NOTICE, "PHYSICS_EDITOR: toolgun %s",
        s_peToolgun ? "on" : "off");
}

//ai: closing the tool while the camera owns the input must hand it back.
extern "C" gX_I0 gX_Tools_Physics_Shutdown(gX_I0);
extern "C" gX_I0 gX_Tools_Physics_Shutdown(gX_I0) {
    PeEditor_FreeCamera(0);
}

#define GX_PE_PATH_MAX 256
#define GX_PE_FILE_MAX 16
#define GX_PE_DIR "resources/tools/physics"

static gX_ScenePhysics_Tuning s_peTuning;
static gX_I1 s_peValid = 0;
static gX_U32 s_peEpoch = 0;
static gX_I8 s_pePath[GX_PE_PATH_MAX] = GX_PE_DIR "/tuning_edit.json";
static gX_I8 s_peStatus[224] = "ready";
static gX_I8 s_peFiles[GX_PE_FILE_MAX][128];
static gX_I32 s_peFileCount = 0;

static gX_I0 PeEditor_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_peStatus, sizeof(s_peStatus), fmt, args);
    va_end(args);
}

static gX_I0 PeEditor_RefreshFiles(void) {
    s_peFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_PE_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_peFileCount < GX_PE_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".json") != 0) continue;
            strncpy((char *)s_peFiles[s_peFileCount], (const char *)name, 127);
            s_peFiles[s_peFileCount][127] = '\0';
            s_peFileCount++;
        }
    }
}

//ai: pull the live scene/applier state into the editor copy (the panel then
//   edits the current sandbox; pool geometry stays scene content).
static gX_I0 PeEditor_Pull(gX_I0) {
    gX_ScenePhysics_Tuning *t = &s_peTuning;
    gX_ScenePhysics *sc = &scene.physicsScene;
    *t = gX_ScenePhysics_Tuning_Defaults();
    t->tuneLevel = physics.tuningPin;
    if (sc->spawnTotal > 0) t->props = sc->spawnTotal;
    t->ragdolls = sc->ragdollCount;
    t->materials = sc->materialsEnabled;
    t->joints = sc->jointsEnabled;
    t->water = sc->waterEnabled;
    t->magnetsOn = sc->magnetsOn;
    //ai: the relativity layer rides the applier settings (the scene's gate
    //   mirrors them).
    t->relativity = gX_PhysicsRelativity_Enabled();
    t->lightSpeed = gX_PhysicsRelativity_GetSettings().lightSpeed;
    t->sparks = sc->sparksEnabled;
    t->decals = sc->decalsEnabled;
    t->sfx = sc->sfxEnabled;
    t->em = gX_PhysicsEM_GetSettings();
    t->em.dipoleScale = sc->magnetScale;
    t->thermal = gX_PhysicsThermal_GetSettings();
    gX_PhysicsFluid_Settings fluid = gX_PhysicsFluid_GetSettings();
    t->fluidDensity = fluid.density;
    t->fluidDrag = fluid.dragCoefficient;
    t->fluidLinearDrag = fluid.linearDrag;
    t->fluidAngularDrag = fluid.angularDrag;
}

static gX_I0 PeEditor_Apply(const gX_ScenePhysics_Tuning *tuning) {
    if (!scene.physicsScene.loaded) {
        PeEditor_Status("sandbox scene not active — open --physics");
        return;
    }
    gX_ScenePhysics_Tuning_Apply(&scene.physicsScene, tuning);
    PeEditor_Status("applied to the live sandbox");
}

extern "C" gX_I0 gX_Tools_Physics_Reset(gX_I0);
extern "C" gX_I0 gX_Tools_Physics_Reset(gX_I0) {
    //ai: the opener routes to the sandbox asynchronously and its content plan
    //   runs after Load; Draw pulls once the plan is populated (spawnTotal).
    s_peValid = 0;
    PeEditor_RefreshFiles();
    PeEditor_Status("ready — edits apply to the live physics sandbox");
    //ai: headless capture hook — open straight into the free camera.
    const gX_I8 *envFreeCam = (const gX_I8 *)getenv("GX_PHYSICS_FREECAM");
    if (envFreeCam && envFreeCam[0] == '1') {
        s_peFreeCamForced = 1;
        PeEditor_FreeCamera(1);
    }
    //ai: headless capture hook — free camera plus the toolgun (crosshair,
    //   viewmodel, tool readout) for the editor gate.
    const gX_I8 *envToolgun = (const gX_I8 *)getenv("GX_TOOLS_TOOLGUN");
    if (envToolgun && envToolgun[0] == '1') {
        s_peFreeCamForced = 1;
        PeEditor_Toolgun(1);
    }
}

//ai: a gX_I1 checkbox without aliasing the byte as bool.
static gX_I1 PeEditor_Checkbox(const char *label, gX_I1 *value) {
    bool on = *value != 0;
    if (ImGui::Checkbox(label, &on)) {
        *value = on ? 1 : 0;
        return 1;
    }
    return 0;
}

extern "C" gX_I0 gX_Tools_Physics_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Physics_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimePhysicsFlag();
    if (!open) return;

    //ai: free-camera mode — Esc (or the cursor returning) restores the
    //   panel; while active the panel is replaced by a small hint window.
    if (s_peFreeCamera
        && (input.keyboard.keys[gX_KEY_ESCAPE].state.PRESSED
            || (!s_peFreeCamForced && input.cursorEnabled))) {
        s_peFreeCamForced = 0;
        PeEditor_FreeCamera(0);
    }
    if (s_peFreeCamera && s_peToolgun) {
        PeEditor_ToolgunFrame();
    }
    if (s_peFreeCamera) {
        if (ImGui::Begin("gX Tools - Physics Editor (free camera)", 0,
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoSavedSettings)) {
            if (s_peToolgun) {
                ImGui::Text("Toolgun — %s",
                    (const char *)gX_Tools_Toolgun_ToolName(
                        gX_Tools_Toolgun_Tool()));
                ImGui::TextDisabled("LMB fire, RMB alt, wheel cycles tools; "
                    "Esc returns to the panel.");
            } else {
                ImGui::TextUnformatted("Free camera");
                ImGui::TextDisabled("WASD move, E/Q rise and fall, mouse to "
                    "look; Esc returns to the panel.");
            }
        }
        ImGui::End();
        if (s_peToolgun) {
            gX_Tools_Toolgun_Draw(camera.camera3D);
        }
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(700, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Physics Editor", &open)) {
        gX_ScenePhysics *sc = &scene.physicsScene;
        if (!sc->loaded) {
            s_peValid = 0;
            ImGui::TextUnformatted("sandbox scene not active — open --physics");
        } else if (!s_peValid || s_peEpoch != sc->contentEpoch) {
            //ai: pull only once the entry's content plan has run; a rebuild
            //   (epoch bump) invalidates the copy so env defaults survive.
            s_peValid = 0;
            if (sc->spawnTotal > 0) {
                PeEditor_Pull();
                s_peValid = 1;
                s_peEpoch = sc->contentEpoch;
            } else {
                ImGui::TextUnformatted("waiting for the sandbox content plan...");
            }
        }
        if (s_peValid) {
            gX_ScenePhysics_Tuning *t = &s_peTuning;
            gX_I1 changed = 0;

            ImGui::Text("live: %d props  %d ragdolls  %d joints  world %s  em %s",
                        sc->liveCount, sc->ragdollCount, sc->jointCount,
                        physics.enabled ? "live" : "stub",
                        gX_PhysicsEM_Enabled() ? "on" : "off");
            if (ImGui::SmallButton("Pull live")) PeEditor_Pull();
            ImGui::SameLine();
            if (ImGui::SmallButton("Defaults")) {
                *t = gX_ScenePhysics_Tuning_Defaults();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Free camera")) PeEditor_FreeCamera(1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Toolgun")) PeEditor_Toolgun(1);
            ImGui::SameLine();
            ImGui::TextDisabled("(WASD + mouse look, Esc returns)");

            //opencode: tuning is taller than the window — keep it in one
            //   scrolling region and fold the parameter-heavy groups, so the
            //   world/content sections and the presets + status footer stay on
            //   screen at the default size (the background editor's pattern).
            ImGui::BeginChild("PeTuning", ImVec2(0, -170.0f), true);
            ImGui::SeparatorText("world");
            changed |= ImGui::SliderFloat("gravity (m/s^2)", &t->gravity,
                0.0f, 30.0f, "%.2f");
            const char *levels[] = {"adaptive", "0 max quality", "1",
                "2", "3 min quality"};
            gX_I32 levelIndex = t->tuneLevel + 1;
            if (ImGui::Combo("quality", &levelIndex, levels, 5)) {
                t->tuneLevel = levelIndex - 1;
                changed = 1;
            }

            ImGui::SeparatorText("content (fills up; nothing despawns)");
            changed |= ImGui::SliderInt("props", &t->props, 1,
                GX_SCENE_PHYSICS_MAX_PROPS);
            changed |= ImGui::SliderInt("ragdolls", &t->ragdolls, 0,
                GX_SCENE_PHYSICS_MAX_RAGDOLLS);
            changed |= PeEditor_Checkbox("materials (future spawns)",
                &t->materials);
            ImGui::SameLine();
            changed |= PeEditor_Checkbox("joints (rebuilds)", &t->joints);
            ImGui::SameLine();
            changed |= PeEditor_Checkbox("water", &t->water);
            changed |= PeEditor_Checkbox("magnets", &t->magnetsOn);
            ImGui::SameLine();
            changed |= PeEditor_Checkbox("sparks", &t->sparks);
            ImGui::SameLine();
            changed |= PeEditor_Checkbox("decals", &t->decals);
            //ai: impact audio layer (procedural SFX on contact events)
            changed |= PeEditor_Checkbox("impact sfx", &t->sfx);
            //ai: relativity layer — the light-speed clamp + gamma mass
            changed |= PeEditor_Checkbox("relativity (light speed)",
                &t->relativity);
            ImGui::SameLine();
            if (ImGui::SliderFloat("c (m/s)", &t->lightSpeed, 2.0f, 60.0f,
                    "%.1f")) {
                changed = 1;
            }

            if (ImGui::CollapsingHeader("em / magnets")) {
                changed |= ImGui::SliderFloat("coulombScale", &t->em.coulombScale,
                    0.0f, 300.0f, "%.1f");
                changed |= ImGui::SliderFloat("softening", &t->em.softening,
                    0.0f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("maxForce", &t->em.maxForce,
                    0.0f, 2000.0f, "%.0f");
                gX_F32 b[3] = {t->em.uniformB.x, t->em.uniformB.y,
                    t->em.uniformB.z};
                if (ImGui::InputFloat3("uniform B", b)) {
                    t->em.uniformB = (gX_F32v3){b[0], b[1], b[2]};
                    changed = 1;
                }
                changed |= ImGui::SliderFloat("magnet strength (dipoleScale)",
                    &t->em.dipoleScale, 0.0f, 0.2f, "%.4f");
                changed |= ImGui::SliderFloat("susceptibilityScale",
                    &t->em.susceptibilityScale, 0.0f, 10.0f, "%.2f");
            }

            if (ImGui::CollapsingHeader("thermal")) {
                changed |= PeEditor_Checkbox("enabled", &t->thermal.enabled);
                changed |= ImGui::SliderFloat("ambient (K)",
                    &t->thermal.ambientTemperature, 100.0f, 400.0f, "%.0f");
                changed |= ImGui::SliderFloat("heatingScale",
                    &t->thermal.heatingScale, 0.0f, 10000.0f, "%.0f");
                changed |= ImGui::SliderFloat("coolingScale",
                    &t->thermal.coolingScale, 0.0f, 1.0e-5f, "%.2e");
                changed |= ImGui::SliderFloat("contactConductance",
                    &t->thermal.contactConductance, 0.0f, 2000.0f, "%.0f");
                changed |= ImGui::SliderFloat("maxTemperature (K)",
                    &t->thermal.maxTemperature, 500.0f, 10000.0f, "%.0f");
                changed |= ImGui::SliderFloat("minImpactSpeed",
                    &t->thermal.minImpactSpeed, 0.0f, 10.0f, "%.2f");
            }

            if (ImGui::CollapsingHeader("fluid (pool geometry is scene content)")) {
                changed |= ImGui::SliderFloat("density", &t->fluidDensity,
                    0.0f, 3.0f, "%.2f");
                changed |= ImGui::SliderFloat("drag", &t->fluidDrag,
                    0.0f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("linearDrag", &t->fluidLinearDrag,
                    0.0f, 10.0f, "%.2f");
                changed |= ImGui::SliderFloat("angularDrag", &t->fluidAngularDrag,
                    0.0f, 10.0f, "%.2f");
            }
            ImGui::EndChild();

            if (changed) PeEditor_Apply(t);

            ImGui::SeparatorText("presets (" GX_PE_DIR ")");
            ImGui::SetNextItemWidth(-215.0f);
            ImGui::InputText("##pepath", (char *)s_pePath, sizeof(s_pePath));
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                if (gX_ScenePhysics_Tuning_Save(s_pePath, t) == 0) {
                    PeEditor_Status("saved %s", s_pePath);
                    PeEditor_RefreshFiles();
                } else {
                    PeEditor_Status("SAVE FAILED (guard or io) -> %s", s_pePath);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                gX_ScenePhysics_Tuning loaded;
                if (gX_ScenePhysics_Tuning_Load(s_pePath, &loaded) == 0) {
                    s_peTuning = loaded;
                    PeEditor_Apply(&s_peTuning);
                    PeEditor_Status("loaded %s", s_pePath);
                } else {
                    PeEditor_Status("LOAD FAILED <- %s", s_pePath);
                }
            }
            ImGui::BeginChild("PeEditorFiles", ImVec2(0, 72), true);
            if (s_peFileCount == 0) ImGui::TextUnformatted("(no .json presets found)");
            for (gX_I32 i = 0; i < s_peFileCount; i++) {
                if (ImGui::Selectable((const char *)s_peFiles[i])) {
                    snprintf((char *)s_pePath, sizeof(s_pePath), GX_PE_DIR "/%s",
                             (const char *)s_peFiles[i]);
                    gX_ScenePhysics_Tuning loaded;
                    if (gX_ScenePhysics_Tuning_Load(s_pePath, &loaded) == 0) {
                        s_peTuning = loaded;
                        PeEditor_Apply(&s_peTuning);
                        PeEditor_Status("loaded %s", s_pePath);
                    } else {
                        PeEditor_Status("LOAD FAILED <- %s", s_pePath);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) PeEditor_RefreshFiles();
            ImGui::SameLine();
            if (ImGui::Button("Pull live")) PeEditor_Pull();
            ImGui::TextUnformatted((const char *)s_peStatus);
        }
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimePhysicsFlag() = 0;
}

#else //!gX_ENABLE_RENDERER — no sandbox scene to edit (kernel profile)

extern "C" gX_I0 gX_Tools_Physics_Draw(gX_I0) { }
extern "C" gX_I0 gX_Tools_Physics_Reset(gX_I0) { }
extern "C" gX_I0 gX_Tools_Physics_Shutdown(gX_I0) { }

#endif
