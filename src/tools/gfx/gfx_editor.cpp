/**********************************************************************************************
*
*   [gX: src/tools/gfx/gfx_editor.cpp]
*   gX — a game engine
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

//opencode: gfx editor — third tool in the suite. Immediate-mode ImGui panels
//   over the live scene, editing the gX_RendererGFX state directly
//   (write-through like the theme editor's token writes): sun + heap lights
//   (add/remove via Light_Add/Remove + 3-point studio preset), shadow
//   biases + toggles, CSM cascades/distances, Sky Lab (time-of-day sun +
//   procedural-sky atmosphere promoted from hardcoded uniforms), fog +
//   fog volume, bloom, godrays, lensflare, water, exposure/ambient,
//   quality preset (ApplyQualityPreset is runtime-safe —
//   showcase.c calls it live), TAA + compositor toggles, and JSON preset
//   save/load through the renderer schema. Headless save/load hooks mirror
//   the theme editor's verification path.
//   Kernel-only builds compile the panels out (cluster symbols absent) and
//   show a hint instead. (No extern "C" include block: the gX headers
//   self-guard for C++; only the two entry points need C linkage.)
//opencode: raylib headers FIRST with C++ linkage — renderer/gfx.h opens
//   extern "C" before its include chain reaches them, which would give
//   raymath's operator overloads C linkage (raylib operator clash). The
//   include guards make the later includes no-ops. (raylib.h before
//   raymath.h: raymath standalone fallbacks clash with raylib types.)
#include "raylib.h"
#include "raymath.h"
#include "../../common/alloc.h"
#include "../../common/types.h"
#include "../../main.h"
#include "../../common/log.h"
#include "../../common/error.h"
#include "../../common/profiler.h"
#include "../../common/debug.h"
#if gX_ENABLE_RENDERER == 1
    #include "../../renderer/gfx.h"
    //ai: private surface panels borrow editor-owned services.
    #include "surface_panels.h"
    #include "../../renderer/gfx/load.h"
    #include "../../renderer/gfx/schema.h"
    #include "../../renderer/gfx/scene.h"
    #include "../../renderer/postprocess/schema2.h"
    #include "../../common/polisher.h"
    #include "../../renderer/gfx/reflection_plane.h"
    #include "../../renderer/gfx/glb_loader.h"
    #include "../../renderer/gfx/glb_writer.h"
    #include "../../renderer/gfx/animation.h"
    #include "../../renderer/gfx/gl_level.h"
    #include "../../settings.h"
    #include "../../renderer/renderer.h"
    //opencode: raymarch preset schema declarations (save/load entry points).
    #include "../../renderer/raymarch/schema2.h"
    //opencode: reusable material assets use gX's compact JSON implementation.
    #include "../../common/marshal/json_impl.h"
#endif
#include "../../gui/gui.h"
#include "../../input.h"
#include "../../wrappers/input.h"
#include "../../wrappers/camera.h"

#include "../imgui/imgui.h"
#include "../imgui/imgui_gx_widgets.h"
#include "../background/bg_panel.h"
#include "../imgui/rlImGui.h"

extern "C" gX_I0 gX_Tools_ImGui_AccessibilityMenu(gX_I0);
extern "C" gX_I1 gX_Tools_ImGui_Aero_Panel(gX_I0);
extern "C" gX_I0 gX_Tools_ImGui_Aero_MenuBar(gX_I0);
#include "../tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include "../../common/data.h"

//opencode: the editor is a cold UI translation unit; size-optimize it so
//   interactive authoring features stay inside the release lean budget.
#if defined(__GNUC__)
#pragma GCC optimize("Os")
#endif

//ai: editor-local plum/lavender/amber palette; accessible themes stay untouched.
static gX_I32 gfx_editor_palette_push(gX_I0)
{
    if (settings.uiThemePreset != GX_SETTINGS_UI_THEME_DEFAULT) return 0;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.74f, 0.70f, 0.86f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.040f, 0.090f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.055f, 0.115f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.070f, 0.050f, 0.110f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.30f, 0.52f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.09f, 0.18f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.13f, 0.27f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.17f, 0.34f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.06f, 0.13f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.11f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.07f, 0.05f, 0.11f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.98f, 0.72f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.72f, 0.55f, 0.92f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.98f, 0.72f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.14f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.20f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.27f, 0.52f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.15f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.21f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.36f, 0.26f, 0.50f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.36f, 0.62f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.14f, 0.10f, 0.21f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.24f, 0.17f, 0.34f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.30f, 0.21f, 0.44f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.98f, 0.72f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(0.20f, 0.15f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.06f, 0.045f, 0.09f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.32f, 0.24f, 0.44f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.42f, 0.32f, 0.56f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.52f, 0.40f, 0.68f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(1.00f, 0.78f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.38f, 0.28f, 0.54f, 0.60f));
    return 33;
}

static gX_I0 gfx_editor_palette_pop(gX_I32 count)
{
    if (count > 0) ImGui::PopStyleColor(count);
}

static gX_I32 s_selLight = -1;
//opencode: scene-content selections for editable decals and particle emitters.
static gX_I32 s_selDecal = -1;
static gX_I32 s_selEmitter = -1;
static gX_I32 s_selectedMapSlot = 0;
static gX_I32 s_selectedWave = 0;
static gX_F32 s_decalSize = 1.0f;
//opencode: v0.6.11 — live scene inspector selection and add-model path.
static gX_I32 s_selModel = -1;
//opencode: per-selection checkpoint for safe live experimentation; this is
//   intentionally not part of the JSON preset state.
static gX_I32 s_modelRevertModel = -1;
static gX_I1 s_modelRevertValid = 0;
static gX_I1 s_modelRevertUseTextures = 1;
#if gX_ENABLE_RENDERER == 1
static gX_RendererGFX_Model_Parameters s_modelRevertParams;
#endif
static gX_I8 s_modelPath[256] = "resources/model/primitive/cube.glb";
//ai: new normal-map selections start with the shipped project texture, not a quarantined file.
static gX_I8 s_normalTexturePath[256] = "resources/texture/texture_normal.png";
//opencode: Sky Lab time-of-day — derived from the live sun.dir on first
//   open (s_sunInit), then drives sun.dir every drag. Elevation 5-85°,
//   azimuth 0-360°. sun.dir points along light travel (-toSun).
static float s_sunElev = 45.0f, s_sunAzim = 135.0f;
static gX_I1 s_sunInit = 0;
//opencode: v0.6.10 — editor preset path/status. The default stays inside the
//   existing tools resource directory so a first Save works without setup.
static gX_I8 s_presetPath[256] = "resources/tools/gfx_showcase.json";
static gX_I8 s_presetStatus[160] = { 0 };
static gX_I1 s_presetStatusOK = 1;
//opencode: transient search, selection, and filesystem index for the preset browser.
static gX_I8 s_presetSearch[128] = { 0 };
static gX_I8 s_presetSelectedPath[256] = { 0 };
static FilePathList s_presetFiles = { 0 };
//opencode: cached shipped font list for the label font picker (loaded once).
static FilePathList s_fontFiles = { 0 };
static gX_I1 s_fontFilesLoaded = 0;
//opencode: search query for the model-local material preset browser.
static gX_I8 s_materialSearch[128] = { 0 };
//opencode: defer nested browser load confirmation to the inspector popup scope.
static gX_I1 s_presetConfirmPending = 0;
//opencode: existing preset files require an explicit overwrite confirmation.
static gX_I1 s_presetOverwritePending = 0;
static gX_I1 s_headlessPresetSaveDone = 0;
//ai: audit hook — open the real save dialog without depending on panel geometry.
static gX_I1 s_headlessPresetSaveUiDone = 0;
static gX_I1 s_headlessPresetLoadDone = 0;
static gX_I1 s_headlessSnapshotSmokeDone = 0;
static gX_I1 s_scenePanelOpen = 1;
static gX_I1 s_inspectorPanelOpen = 1;
//ai: one-action viewport focus; remembers which panels were open.
static gX_I1 s_focusViewport = 0;
static gX_I1 s_focusSavedScene = 1;
static gX_I1 s_focusSavedInspector = 1;
static gX_I1 s_pickEnabled = 1;
//ai: resolved panel widths for the shortcut footer's safe center.
static gX_F32 s_footerSceneW = 0.0f;
static gX_F32 s_footerInspectorW = 0.0f;

//ai: focus viewport hides both side panels and restores the prior panel state.
static gX_I0 gfx_focus_viewport_set(gX_I1 on)
{
    if (on == s_focusViewport) return;
    if (on) {
        s_focusSavedScene = s_scenePanelOpen;
        s_focusSavedInspector = s_inspectorPanelOpen;
        s_scenePanelOpen = 0;
        s_inspectorPanelOpen = 0;
    } else {
        s_scenePanelOpen = s_focusSavedScene;
        s_inspectorPanelOpen = s_focusSavedInspector;
    }
    s_focusViewport = on;
}
//opencode: the editor free camera temporarily gives mouse and keyboard to gX camera input.
static gX_I1 s_freeCameraActive = 0;
static gX_I32 s_moveAxis = 0;
static gX_F32 s_moveStep = 0.1f;
//opencode: configurable snap increments for viewport translation, rotation, and scale.
static gX_F32 s_translateSnapStep = 0.25f;
static gX_F32 s_rotationSnapStep = 15.0f;
static gX_F32 s_scaleSnapStep = 0.1f;
static gX_I1 s_draggingModel = 0;
static gX_I1 s_draggingLight = 0;
static gX_I1 s_dragMoved = 0;
//opencode: viewport transform mode and immutable drag baselines.
enum {
    gX_GFX_EDITOR_DRAG_PLANE,
    gX_GFX_EDITOR_DRAG_AXIS,
    gX_GFX_EDITOR_DRAG_ROTATE,
    gX_GFX_EDITOR_DRAG_SCALE
};
static gX_I32 s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
static gX_I32 s_dragAxis = -1;
static gX_F32v3 s_dragStartPosition;
static gX_F32v3 s_dragStartRotation;
static gX_F32v3 s_dragStartScale;
static gX_F32v3 s_dragStartHit;
static gX_F32v3 s_dragPlanePoint;
static gX_F32v3 s_dragPlaneNormal;
//opencode: held-V temporary view-plane drag, independent of the stored plane choice.
static gX_I1 s_dragViewPlane = 0;
static gX_F32 s_dragStartAxisParameter = 0.0f;
static gX_F32 s_dragStartMouseX = 0.0f;
static gX_F32 s_dragStartMouseY = 0.0f;
static gX_I32 s_dragModel = -1;
//opencode: viewport Bezier handle drag state — which handle was grabbed (waypoint
//   index + side: 0 = out, 1 = in), the owning object kind, and the grab point.
static gX_I1 s_draggingHandle = 0;
//opencode: authored-camera marker drag — kind 1 = position, 2 = target.
static gX_I32 s_draggingCamera = 0;
static gX_I32 s_dragCameraKind = 0;
static gX_I32 s_dragHandleIndex = -1;
static gX_I32 s_dragHandleSide = 0;
static gX_I1 s_dragHandleIsLight = 0;
static gX_F32v3 s_dragHandleStart;
//opencode: viewport light selection and position-drag state.
static gX_I32 s_dragLight = -1;
//opencode: camera gesture trace state for deterministic pan/orbit tests.
static gX_I32 s_cameraGestureMode = 0;
static gX_F32v3 s_cameraGestureStartPos;
static gX_F32v3 s_cameraGestureStartTarget;
//opencode: last mesh-fit flatness diagnostic shown for generated mirror planes.
static gX_I32 s_planarFitModel = -1;
static gX_F32 s_planarFitRatio = -1.0f;

#if gX_ENABLE_RENDERER == 1
static gX_RendererGFX *gfx_state(gX_I0) {
    if (!renderer.gfx.data) return 0;
    return (gX_RendererGFX *)renderer.gfx.data;
}
#include "bug_report.h"

//opencode: the browser is intentionally scoped to audited resource roots.
#define GX_GFX_ASSET_ROOT_CAP 8
#define GX_GFX_ASSET_ROOT_SOURCE_CAP (GX_GFX_ASSET_ROOT_CAP + 1)
//ai: raymarch module accessor (defined with the panel below; used earlier by
//   the asset-browser texture targets).
static gX_RendererRaymarch *gfx_raymarch_active(gX_I0);

#define GX_GFX_ASSET_SOURCE_CAP (GX_GFX_ASSET_ROOT_SOURCE_CAP * 2)
#define GX_GFX_ASSET_THUMBNAIL_CAP 64
enum {
    gX_GFX_ASSET_MODELS,
    gX_GFX_ASSET_TEXTURES
};
enum {
    gX_GFX_ASSET_TARGET_NONE,
    gX_GFX_ASSET_TARGET_MODEL_SLOT,
    gX_GFX_ASSET_TARGET_MODEL_GLOSS,
    gX_GFX_ASSET_TARGET_MODEL_LIGHTMAP,
    gX_GFX_ASSET_TARGET_DECAL,
    gX_GFX_ASSET_TARGET_WATER_NORMAL,
    gX_GFX_ASSET_TARGET_MATERIAL_MAP,
    //opencode: UX pass — path-based targets that previously required typing.
    gX_GFX_ASSET_TARGET_SKYBOX,
    gX_GFX_ASSET_TARGET_LIGHT_COOKIE,
    gX_GFX_ASSET_TARGET_DETAIL,
    //ai: raymarch texturing — the skybox cubemap and tex0..2 (slot in
    //   s_assetTargetSlot); paths are copied into the raymarch wrappers.
    gX_GFX_ASSET_TARGET_RAYMARCH_SKYBOX,
    gX_GFX_ASSET_TARGET_RAYMARCH_TEX
};
typedef struct gX_GFX_Asset_Thumbnail {
    Texture2D texture;
    gX_I32 sourceWidth;
    gX_I32 sourceHeight;
    gX_I8 path[1024];
} gX_GFX_Asset_Thumbnail;
static FilePathList s_modelAssetSources[GX_GFX_ASSET_SOURCE_CAP] = {};
static FilePathList s_textureAssetSources[GX_GFX_ASSET_SOURCE_CAP] = {};
static gX_I8 s_assetGuestRoots[GX_GFX_ASSET_ROOT_CAP][256] = {};
static gX_U32 s_assetSourceCount = 0;
static gX_I1 s_assetsReady = 0;
static gX_I1 s_assetBrowserOpen = 0;
static gX_I1 s_assetBrowserFocus = 0;
static gX_I32 s_assetBrowserKind = gX_GFX_ASSET_MODELS;
static gX_I32 s_assetTextureTarget = gX_GFX_ASSET_TARGET_NONE;
static gX_I32 s_assetTargetIndex = -1;
static gX_I32 s_assetTargetSlot = -1;
static gX_I32 s_assetTargetPurpose = -1;
//opencode: per-material map target — the override entry inside the model.
static gX_I32 s_assetTargetOverride = -1;
static gX_I8 s_assetSearch[128] = {};
static gX_I8 s_assetSelectedPath[1024] = {};
static gX_I8 s_assetBrowserStatus[160] = {};
static gX_GFX_Asset_Thumbnail *s_assetThumbnailCache = NULL;
static gX_U32 s_assetThumbnailNext = 0;
static gX_I1 s_assetThumbnailClearPending = 0;

//opencode: automatic model edit history plus four live A/B model, camera, and
//   renderer snapshot slots.
#define GX_GFX_HISTORY_CAP 16
#define GX_GFX_SNAPSHOT_CAP 4
typedef struct gX_GFX_Model_Edit_State {
    gX_RendererGFX_Model_Parameters params;
    gX_I1 useTextures;
} gX_GFX_Model_Edit_State;
typedef struct gX_GFX_Renderer_Edit_State {
    gX_RendererGFX_State gfxState;
    gX_I1 usePP;
    gX_I1 useCompositor;
    gX_I1 normalBufferEnabled;
    gX_I32 ssrMaxSteps;
    gX_F32 ssrStepSize;
    gX_F32 ssrMaxDistance;
    gX_F32 ssrDepthBias;
    gX_F32 ssrOpacity;
    gX_F32 ssrRoughness;
    gX_F32 ssrMaxLod;
    gX_F32 ssrEdgeFade;
    gX_I32 ssrMaxBounces;
    gX_F32 ssrBounceAtten;
    gX_F32 ssrMaxDepthDelta;
    gX_F32 ssrWorldGapThreshold;
    gX_F32 ssrFinalDepthThreshold;
    gX_F32 ssrHitFacingThreshold;
    gX_F32 reflectionBias;
} gX_GFX_Renderer_Edit_State;
typedef struct gX_GFX_Model_Snapshot {
    gX_GFX_Model_Edit_State model;
    gX_GFX_Renderer_Edit_State renderer;
    gX_Camera3D camera;
    gX_I32 modelIndex;
    gX_I1 valid;
    gX_I8 name[64];
} gX_GFX_Model_Snapshot;
//opencode: authored heap-light fields only. Cookie textures, shadow GPU
//   handles/matrices, and derived camera state stay out of the history.
typedef struct gX_GFX_Light_Edit_State {
    gX_I32 type;
    gX_F32v3 pos;
    gX_F32v3 dir;
    gX_F32 fov;
    gX_F32 brightness;
    gX_Color diffuseTint;
    gX_Color specularTint;
    gX_F32 diffuseFactor;
    gX_F32 specularFactor;
    gX_Color color;
    gX_F32 attenConst;
    gX_F32 attenLinear;
    gX_F32 attenQuad;
    gX_I1 castShadows;
    gX_U32 shadowSize;
    gX_F32 shadowFov;
    gX_F32 shadowFar;
    gX_F32 shadowRange;
    gX_I1 orbitEnabled;
    gX_I1 orbitPaused;
    gX_I1 orbitLoop;
    gX_I32 orbitPlane;
    gX_F32v3 orbitCenter;
    gX_F32 orbitRadius;
    gX_F32 orbitSpeed;
    gX_F32 orbitPhase;
    //opencode: authored waypoint path + handles (viewport handle drags and the
    //   path panel edits are undoable like the rest of the light).
    gX_I32 pathMode;
    gX_I32 pathWaypointCount;
    gX_F32 pathWaypoints[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_I1 pathPingPong;
    gX_I1 pathSmooth;
    gX_I1 pathBezier;
    gX_F32 pathTangents[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_F32 pathTangentsIn[gX_GFX_LIGHT_WAYPOINTS * 3];
    gX_I1 pathHandlesIndependent;
} gX_GFX_Light_Edit_State;
//opencode: the history and snapshot slots each hold a full params copy; they
//   are heap-allocated on first use so the editor's BSS footprint does not
//   grow with the params struct (lean-gate hard ceiling). All accessors go
//   through gfx_model_history_ensure().
static gX_GFX_Model_Edit_State *s_modelUndo = NULL;
static gX_GFX_Model_Edit_State *s_modelRedo = NULL;
static gX_GFX_Model_Snapshot *s_modelSnapshots = NULL;
static gX_GFX_Model_Edit_State s_modelHistoryCurrent;
static gX_I32 s_modelUndoCount = 0;
static gX_I32 s_modelRedoCount = 0;
static gX_I32 s_modelHistoryModel = -1;
static gX_I32 s_modelSnapshotSlot = 0;
static gX_I8 s_modelSnapshotName[64] = "reflection A";
static gX_I1 s_modelHistoryValid = 0;
//opencode: pre-drag model state so one undo reverts the whole viewport gesture.
static gX_GFX_Model_Edit_State *s_dragStartEdit = NULL;
static gX_I1 s_dragStartEditValid = 0;
//opencode: an active motion path drives pos; re-baseline when it stops so
//   runtime motion is never recorded as an authored edit.
static gX_I1 s_modelOrbitDriving = 0;
//opencode: selected-light history uses the same coalesced-drag contract.
static gX_GFX_Light_Edit_State *s_lightUndo = NULL;
static gX_GFX_Light_Edit_State *s_lightRedo = NULL;
static gX_GFX_Light_Edit_State *s_dragStartLightEdit = NULL;
static gX_GFX_Light_Edit_State s_lightHistoryCurrent;
static gX_I32 s_lightUndoCount = 0;
static gX_I32 s_lightRedoCount = 0;
static gX_I32 s_lightHistoryLight = -1;
static gX_I1 s_lightHistoryValid = 0;
static gX_I1 s_dragStartLightEditValid = 0;
//opencode: orbit playback drives pos; re-baseline when it stops so runtime
//   motion is never recorded as an authored edit.
static gX_I1 s_lightOrbitDriving = 0;
static gX_I0 gfx_model_history_reset(gX_I0);
static gX_I0 gfx_model_snapshots_clear(gX_I0);
//opencode: forward declarations for the prefab helpers defined earlier in the
//   file than the model/light accessors themselves.
static gX_RendererGFX_Model *gfx_model_at(gX_RendererGFX *gfx, gX_I32 index);
static gX_RendererGFX_Decal *gfx_decal_at(gX_RendererGFX *gfx, gX_I32 index);
static gX_RendererGFX_Emitter *gfx_emitter_at(gX_RendererGFX *gfx, gX_I32 index);
static gX_I0 gfx_prefab_json_add_vec4(struct json_object *parent,
    const gX_I8 *key, gX_F32v4 value);
static gX_I0 gfx_prefab_read_vec4(struct json_object *parent,
    const gX_I8 *key, gX_F32v4 *out);
static gX_RendererGFX_Light *gfx_light_at(gX_RendererGFX *gfx, gX_I32 index);
static gX_I0 gfx_ssr_normal_buffer_set(gX_RendererGFX *gfx, gX_I1 enabled);

//opencode: reusable surface assets store authoring values only; material target
//   names and resolved GPU texture IDs remain local to each model override.
typedef struct gX_GFX_Material_Surface_Asset {
    gX_Color emissiveColor;
    gX_F32 emissive;
    gX_Color albedo;
    gX_F32 roughness;
    gX_F32 metalness;
    gX_I8 albedoMap[128];
    gX_I8 emissiveMap[128];
} gX_GFX_Material_Surface_Asset;
typedef struct gX_GFX_Material_Asset {
    gX_I8 name[64];
    gX_I8 description[128];
    gX_GFX_Material_Surface_Asset surface;
} gX_GFX_Material_Asset;
static gX_I8 s_materialAssetName[64] = "New material";
static gX_I8 s_materialAssetDescription[128] = {0};
static gX_I8 s_materialAssetPath[256] = "new.material.json";
static gX_I8 s_materialAssetStatus[160] = {0};
static gX_I8 s_materialAssetSelectedPath[256] = {0};
static FilePathList s_materialAssetFiles = {0};
static gX_I1 s_materialAssetStatusOK = 1;
static gX_I1 s_headlessMaterialAssetCheckDone = 0;
//opencode: headless per-material map assignment check (gate hook).
static gX_I1 s_headlessMaterialMapCheckDone = 0;
//opencode: headless asset-root registration check (gate hook).
static gX_I1 s_headlessAssetRootCheckDone = 0;
//opencode: headless prefab check (gate hook): capture the fixture's selected
//   model + nearby lights, place a copy 2m along +X, and report the counts.
static gX_I1 s_headlessPrefabCheckDone = 0;

//opencode: headless UI-theme selection (gate hook); sets the preset without
//   marking settings dirty, so harness runs never persist it.
static gX_I1 s_headlessUiThemeDone = 0;

//opencode: blockout primitive authoring (tooling scope): type/size/detail
//   plus the headless gate flag, no asset files involved.
static gX_I32 s_primitiveType = 0;
static gX_F32 s_primitiveSize = 1.0f;
static gX_I32 s_primitiveDetail = 1;
static gX_I1 s_headlessPrimitiveCheckDone = 0;

//opencode: texture utilities (tooling scope): generator settings, normal
//   and ORM inputs, plus the headless gate flag. Outputs land in the
//   generated-textures folder unless GX_GFX_TEXTURE_DIR redirects them.
static gX_I32 s_texKind = 1;
static gX_I32 s_texWidth = 512;
static gX_I32 s_texHeight = 512;
static gX_F32 s_texParam = 8.0f;
static gX_Color s_texColorA = { 255, 255, 255, 255 };
static gX_Color s_texColorB = { 51, 51, 51, 255 };
static gX_I8 s_texName[128] = {0};
static gX_I8 s_texSource[512] = {0};
static gX_F32 s_texStrength = 2.0f;
static gX_I8 s_ormAo[512] = {0};
static gX_I8 s_ormRough[512] = {0};
static gX_I8 s_ormMetal[512] = {0};
static gX_I8 s_texStatus[640] = {0};
static gX_I1 s_texStatusOK = 0;
static gX_I1 s_headlessTextureCheckDone = 0;
//opencode: preview thumbnail — the last generated/baked texture, shown in the
//   Texture tools panel and reloaded only when the path changes.
static Texture2D s_texPreview = {0};
static gX_I8 s_texPreviewPath[640] = {0};

//opencode: scene export (tooling scope): OBJ prefix + status and the headless
//   gate flag. The output folder is overridable through GX_GFX_EXPORT_DIR.
static gX_I8 s_exportName[128] = "scene";
static gX_I8 s_exportStatus[640] = {0};
static gX_I1 s_exportStatusOK = 0;
static gX_I1 s_headlessExportCheckDone = 0;

//opencode: raymarch editor panel (revival authoring surface): preset path,
//   status and the one-shot UI trace for the gate.
static gX_I8 s_rmPresetPath[512] = {0};
static gX_I8 s_rmStatus[320] = {0};
static gX_I1 s_rmStatusOK = 0;
static gX_I1 s_rmUiTraced = 0;
static gX_I1 s_ppUiTraced = 0;
static gX_I1 s_raymarchComposite = 0;
//opencode: raymarch preset gallery (folder list + built-in picker).
static FilePathList s_rmPresetFiles = {0};
static gX_I1 s_rmPresetListLoaded = 0;
static gX_I8 s_rmPresetSelected[512] = {0};
static gX_I32 s_rmBuiltinIndex = 0;

//opencode: decal bake (tooling scope, slice 4) — top-down decal projection.
static gX_I32 s_decalBakeSize = 512;
static gX_F32 s_decalBakeRough = 0.9f;
static gX_F32 s_decalBakeMetal = 0.0f;
static gX_Color s_decalBakeColor = { 220, 90, 60, 255 };
static gX_I8 s_decalBakeName[128] = "decals";
static gX_I8 s_decalBakeStatus[320] = {0};
static gX_I1 s_decalBakeStatusOK = 0;
static gX_I1 s_headlessDecalBakeDone = 0;

//opencode: prefab library — reusable scene bundles. v1 captures the selected
//   model (anchor) plus every used light within a radius, positions relative
//   to the anchor, and places copies at a position.
static FilePathList s_prefabFiles = {0};
static gX_I1 s_prefabListLoaded = 0;
static gX_I8 s_prefabSelectedPath[256] = {0};
static gX_I8 s_prefabName[128] = "New prefab";
static gX_F32 s_prefabRadius = 8.0f;
static gX_I1 s_prefabPlaceArm = 0;
//opencode: prefab group tagging — per-kind bitmasks (32 slots each) so a
//   bundle can capture exactly the tagged set instead of a radius.
static gX_U32 s_taggedModels = 0;
static gX_U32 s_taggedLights = 0;
static gX_U32 s_taggedDecals = 0;
static gX_U32 s_taggedEmitters = 0;
static gX_I1 s_prefabUseTags = 0;
static gX_F32v3 s_prefabPlaceAt = {0.0f, 0.0f, 0.0f};
static gX_I1 s_prefabPlaceInit = 0;
static gX_I8 s_prefabStatus[192] = {0};
static gX_I1 s_prefabStatusOK = 1;

//opencode: authored scene cameras (Blender/Bryce-style render views). The
//   editor owns them and appends a top-level "cameras" section to presets;
//   v1 edits numerically and copies to/from the live view.
#define GX_GFX_EDITOR_CAMERAS 8
typedef struct gX_GFX_Editor_Camera {
    gX_I8 name[128];
    gX_I8 description[256];
    gX_Camera3D view;
} gX_GFX_Editor_Camera;

static gX_GFX_Editor_Camera s_cameras[GX_GFX_EDITOR_CAMERAS];
static gX_I32 s_cameraCount = 0;
static gX_I32 s_selectedCamera = -1;
static gX_I8 s_renderOutput[512] = "screenshot/render.png";
//opencode: render output size — 0 keeps the docked viewport crop size, a
//   positive pair rescales the crop (delivery/thumbnail formats).
static gX_I32 s_renderWidth = 0;
static gX_I32 s_renderHeight = 0;
static gX_I8 s_renderStatus[192] = {0};
static gX_I1 s_renderStatusOK = 1;
//opencode: two-phase render — apply the selected camera for one frame, then
//   capture the presented frame, then restore the authoring view.
static gX_I1 s_renderPending = 0;
static gX_I1 s_renderApplied = 0;
static gX_Camera3D s_renderSavedView = {0};
static gX_I1 s_renderSavedMarkers = 0;
static gX_I1 s_headlessRenderDone = 0;
static gX_I1 s_headlessCameraDragDone = 0;
static gX_I32 s_selectedMaterialOverride = 0;

//opencode: asset files are JSON objects with a version tag, display metadata,
//   and the subset of surface values supported by per-material overrides.
static const gX_I8 *s_materialAssetFormat = "gx-material-v1";

//opencode: idempotent heap allocation for the history/snapshot slots.
static gX_I0 gfx_model_history_ensure(gX_I0) {
    if (!s_modelUndo)
        s_modelUndo = (gX_GFX_Model_Edit_State*)calloc(
            GX_GFX_HISTORY_CAP, sizeof(gX_GFX_Model_Edit_State));
    if (!s_modelRedo)
        s_modelRedo = (gX_GFX_Model_Edit_State*)calloc(
            GX_GFX_HISTORY_CAP, sizeof(gX_GFX_Model_Edit_State));
    if (!s_modelSnapshots)
        s_modelSnapshots = (gX_GFX_Model_Snapshot*)calloc(
            GX_GFX_SNAPSHOT_CAP, sizeof(gX_GFX_Model_Snapshot));
    if (!s_dragStartEdit)
        s_dragStartEdit = (gX_GFX_Model_Edit_State*)calloc(
            1, sizeof(gX_GFX_Model_Edit_State));
    if (!s_lightUndo)
        s_lightUndo = (gX_GFX_Light_Edit_State*)calloc(
            GX_GFX_HISTORY_CAP, sizeof(gX_GFX_Light_Edit_State));
    if (!s_lightRedo)
        s_lightRedo = (gX_GFX_Light_Edit_State*)calloc(
            GX_GFX_HISTORY_CAP, sizeof(gX_GFX_Light_Edit_State));
    if (!s_dragStartLightEdit)
        s_dragStartLightEdit = (gX_GFX_Light_Edit_State*)calloc(
            1, sizeof(gX_GFX_Light_Edit_State));
}

//opencode: gX_Color is U8 RGBA; ImGui edits floats. Round-trip helper.
static gX_I1 gfx_color_edit(const gX_I8 *label, gX_Color *c) {
    float f[3];
    f[0] = c->r / 255.0f; f[1] = c->g / 255.0f; f[2] = c->b / 255.0f;
    if (ImGui::ColorEdit3(label, f)) {
        c->r = (gX_U8)(f[0] * 255.0f + 0.5f);
        c->g = (gX_U8)(f[1] * 255.0f + 0.5f);
        c->b = (gX_U8)(f[2] * 255.0f + 0.5f);
        return 1;
    }
    return 0;
}

//opencode: keep disabled inspector guidance readable inside narrow docks.
static gX_I0 gfx_text_disabled_wrapped(const gX_I8 *text) {
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static const gX_I8 *gfx_light_type_name(gX_I32 t) {
    //opencode: glb_loader.c:318-320 — 0=directional, 1=point, 2=spot.
    if (t == 0) return "directional";
    if (t == 2) return "spot";
    return "point";
}

//opencode: v0.6.10 — preset feedback stays in the editor instead of forcing
//   users to inspect the log after a Save/Load action.
static gX_I0 gfx_preset_set_status(const gX_I8 *text, gX_I1 ok) {
    strncpy(s_presetStatus, text ? text : "", sizeof(s_presetStatus) - 1);
    s_presetStatus[sizeof(s_presetStatus) - 1] = 0;
    s_presetStatusOK = ok;
    //opencode: the status is UI-only otherwise — headless runs (and the error
    //   gate) need preset failures reported in the log.
    gX_TraceLog(ok ? gX_LOG_INFO : gX_LOG_ERROR, "GFX_PRESET: %s",
        (const char *)s_presetStatus);
}

//opencode: the schema loader clears the live scene before parsing, so reject
//   missing paths before handing control to it.
static gX_I1 gfx_preset_file_exists(const gX_I8 *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

//opencode: reusable material files use an explicit extension so the scene
//   preset browser never mistakes them for complete GFX presets.
static gX_I1 gfx_material_asset_path_valid(const gX_I8 *path) {
    if (!path || !path[0]) return 0;
    size_t length = strlen(path);
    const gX_I8 *suffix = ".material.json";
    size_t suffixLength = strlen(suffix);
    return length > suffixLength
        && strcmp(path + length - suffixLength, suffix) == 0;
}

//opencode: bare filenames resolve inside the editor material library, while
//   explicit resource and absolute paths remain available to advanced users.
static gX_I1 gfx_material_asset_resolve_path(const gX_I8 *path,
    gX_I8 *resolved, gX_U32 capacity) {
    if (!path || !path[0] || !resolved || capacity == 0) return 0;
    if (path[0] == '/' || strncmp(path, "resources/", 10) == 0) {
        if (strlen(path) >= capacity) return 0;
        snprintf(resolved, capacity, "%s", path);
        return 1;
    }
    const gX_I8 *libraryPath = "resources/tools/materials/";
    size_t libraryLength = strlen(libraryPath);
    size_t assetLength = strlen(path);
    if (libraryLength + assetLength >= capacity) return 0;
    memcpy(resolved, libraryPath, libraryLength);
    memcpy(resolved + libraryLength, path, assetLength + 1);
    return 1;
}

//opencode: reusable asset status stays beside the authoring controls.
static gX_I0 gfx_material_asset_set_status(const gX_I8 *text, gX_I1 ok) {
    snprintf(s_materialAssetStatus, sizeof(s_materialAssetStatus), "%s", text ? text : "");
    s_materialAssetStatusOK = ok;
}

//opencode: material asset defaults match the identity tint and unset scalar
//   semantics used by model-side material overrides.
static gX_I0 gfx_material_asset_init(gX_GFX_Material_Asset *asset) {
    memset(asset, 0, sizeof(*asset));
    asset->surface.emissiveColor = (gX_Color){255, 255, 255, 255};
    asset->surface.albedo = (gX_Color){255, 255, 255, 255};
    asset->surface.roughness = -1.0f;
    asset->surface.metalness = -1.0f;
}

//opencode: append an RGBA8 value as a compact four-number JSON array.
static gX_I0 gfx_material_json_add_color(struct json_object *parent,
    const gX_I8 *key, gX_Color color) {
    struct json_object *value = json_object_new_array();
    json_object_array_add(value, json_object_new_int(color.r));
    json_object_array_add(value, json_object_new_int(color.g));
    json_object_array_add(value, json_object_new_int(color.b));
    json_object_array_add(value, json_object_new_int(color.a));
    json_object_object_add(parent, key, value);
}

//opencode: the engine's compact JSON API exposes type inspection through one
//   function rather than json-c's convenience predicate.
static gX_I1 gfx_material_json_is_type(struct json_object *value, json_type type) {
    return value && json_object_get_type(value) == type;
}

//opencode: write the transferable surface only; model target and GPU IDs are
//   intentionally omitted so one asset can be applied to another model.
static gX_I1 gfx_material_asset_save(const gX_RendererGFX_MaterialOverride *source,
    const gX_I8 *name, const gX_I8 *description, const gX_I8 *path) {
    if (!source || !name || !name[0] || !gfx_material_asset_path_valid(path)) return 0;
    gX_I8 resolvedPath[1024];
    if (!gfx_material_asset_resolve_path(path, resolvedPath, sizeof(resolvedPath))) return 0;
    gX_I8 parentPath[1024];
    if (strlen(resolvedPath) >= sizeof(parentPath)) return 0;
    snprintf(parentPath, sizeof(parentPath), "%s", resolvedPath);
    gX_I8 *lastSlash = strrchr(parentPath, '/');
    if (lastSlash) {
        *lastSlash = 0;
        if (parentPath[0] && !DirectoryExists(parentPath)
            && MakeDirectory(parentPath) != 0) return 0;
    }
    struct json_object *root = json_object_new_object();
    struct json_object *surface = json_object_new_object();
    if (!root || !surface) {
        if (root) json_object_put(root);
        if (surface) json_object_put(surface);
        return 0;
    }
    json_object_object_add(root, "format", json_object_new_string(s_materialAssetFormat));
    json_object_object_add(root, "name", json_object_new_string(name));
    json_object_object_add(root, "description",
        json_object_new_string(description ? description : ""));
    gfx_material_json_add_color(surface, "emissiveColor", source->emissiveColor);
    json_object_object_add(surface, "emissive", json_object_new_double(source->emissive));
    gfx_material_json_add_color(surface, "albedo", source->albedo);
    json_object_object_add(surface, "roughness", json_object_new_double(source->roughness));
    json_object_object_add(surface, "metalness", json_object_new_double(source->metalness));
    json_object_object_add(surface, "albedoMap", json_object_new_string(source->albedoMap));
    json_object_object_add(surface, "emissiveMap", json_object_new_string(source->emissiveMap));
    json_object_object_add(root, "surface", surface);
    gX_I1 saved = json_object_to_file_ext(resolvedPath, root, JSON_C_TO_STRING_PRETTY) == 0;
    json_object_put(root);
    return saved;
}

//opencode: validate numeric JSON values before copying them into renderer
//   tuning fields, rejecting NaN and infinity in hand-edited assets.
static gX_I1 gfx_material_json_read_float(struct json_object *parent,
    const gX_I8 *key, gX_F32 *value) {
    struct json_object *member = NULL;
    if (!json_object_object_get_ex(parent, key, &member)
        || (json_object_get_type(member) != json_type_int
            && json_object_get_type(member) != json_type_double)) return 0;
    gX_F64 parsed = json_object_get_double(member);
    if (!isfinite(parsed)) return 0;
    *value = (gX_F32)parsed;
    return isfinite(*value);
}

//opencode: read an RGBA8 tuple without accepting malformed lengths or values.
static gX_I1 gfx_material_json_read_color(struct json_object *parent,
    const gX_I8 *key, gX_Color *color) {
    struct json_object *member = NULL;
    if (!json_object_object_get_ex(parent, key, &member)
        || !gfx_material_json_is_type(member, json_type_array)
        || json_object_array_length(member) != 4) return 0;
    gX_U8 *channels[] = {&color->r, &color->g, &color->b, &color->a};
    for (gX_I32 ci = 0; ci < 4; ci++) {
        struct json_object *component = json_object_array_get_idx(member, ci);
        if (!component || (json_object_get_type(component) != json_type_int
                && json_object_get_type(component) != json_type_double)) return 0;
        gX_F64 parsed = json_object_get_double(component);
        if (!isfinite(parsed) || parsed < 0.0 || parsed > 255.0
            || floor(parsed) != parsed) return 0;
        *channels[ci] = (gX_U8)parsed;
    }
    return 1;
}

//opencode: required asset strings are length-checked instead of silently
//   truncating texture resource paths or display metadata.
static gX_I1 gfx_material_json_read_string(struct json_object *parent,
    const gX_I8 *key, gX_I8 *destination, gX_U32 capacity, gX_I1 required) {
    struct json_object *member = NULL;
    if (!json_object_object_get_ex(parent, key, &member)) {
        destination[0] = 0;
        return !required;
    }
    if (!gfx_material_json_is_type(member, json_type_string)) return 0;
    const gX_I8 *value = json_object_get_string(member);
    size_t length = strlen(value);
    if (length >= capacity) return 0;
    memcpy(destination, value, length + 1);
    return !required || destination[0] != 0;
}

//opencode: parse a complete, versioned material asset into a temporary value;
//   callers apply nothing if any present field is malformed.
static gX_I1 gfx_material_asset_load(const gX_I8 *path,
    gX_GFX_Material_Asset *asset) {
    gX_I8 resolvedPath[1024];
    if (!gfx_material_asset_path_valid(path)
        || !gfx_material_asset_resolve_path(path, resolvedPath, sizeof(resolvedPath))
        || !gfx_preset_file_exists(resolvedPath)) return 0;
    struct json_object *root = json_object_from_file(resolvedPath);
    if (!root) return 0;
    struct json_object *format = NULL;
    struct json_object *surface = NULL;
    gX_I1 valid = gfx_material_json_is_type(root, json_type_object)
        && json_object_object_get_ex(root, "format", &format)
        && gfx_material_json_is_type(format, json_type_string)
        && strcmp(json_object_get_string(format), s_materialAssetFormat) == 0
        && json_object_object_get_ex(root, "surface", &surface)
        && gfx_material_json_is_type(surface, json_type_object);
    gX_GFX_Material_Asset loaded;
    gfx_material_asset_init(&loaded);
    if (valid) {
        valid = gfx_material_json_read_string(root, "name", loaded.name,
                sizeof(loaded.name), 1)
            && gfx_material_json_read_string(root, "description", loaded.description,
                sizeof(loaded.description), 0);
    }
    if (valid) {
        valid = gfx_material_json_read_color(surface, "emissiveColor",
                &loaded.surface.emissiveColor)
            && gfx_material_json_read_float(surface, "emissive", &loaded.surface.emissive)
            && gfx_material_json_read_color(surface, "albedo", &loaded.surface.albedo)
            && gfx_material_json_read_float(surface, "roughness", &loaded.surface.roughness)
            && gfx_material_json_read_float(surface, "metalness", &loaded.surface.metalness)
            && gfx_material_json_read_string(surface, "albedoMap",
                loaded.surface.albedoMap, sizeof(loaded.surface.albedoMap), 0)
            && gfx_material_json_read_string(surface, "emissiveMap",
                loaded.surface.emissiveMap, sizeof(loaded.surface.emissiveMap), 0);
    }
    json_object_put(root);
    if (!valid) return 0;
    *asset = loaded;
    return 1;
}

//opencode: applying an asset replaces surface values while preserving the
//   model-specific material match and forcing texture paths to resolve again.
static gX_I0 gfx_material_asset_apply(const gX_GFX_Material_Asset *asset,
    gX_RendererGFX_MaterialOverride *target) {
    target->emissiveColor = asset->surface.emissiveColor;
    target->emissive = asset->surface.emissive;
    target->albedo = asset->surface.albedo;
    target->roughness = asset->surface.roughness;
    target->metalness = asset->surface.metalness;
    memcpy(target->albedoMap, asset->surface.albedoMap, sizeof(target->albedoMap));
    memcpy(target->emissiveMap, asset->surface.emissiveMap, sizeof(target->emissiveMap));
    target->albedoMapID = -1;
    target->emissiveMapID = -1;
}

//opencode: load/apply is atomic from the editor's point of view and refreshes
//   the asset metadata controls only after a valid file has been parsed.
static gX_I1 gfx_material_asset_load_apply(const gX_I8 *path,
    gX_RendererGFX_MaterialOverride *target) {
    if (!target) return 0;
    gX_GFX_Material_Asset asset;
    if (!gfx_material_asset_load(path, &asset)) return 0;
    gfx_material_asset_apply(&asset, target);
    snprintf(s_materialAssetName, sizeof(s_materialAssetName), "%s", asset.name);
    snprintf(s_materialAssetDescription, sizeof(s_materialAssetDescription), "%s",
        asset.description);
    snprintf(s_materialAssetPath, sizeof(s_materialAssetPath), "%s", path);
    return 1;
}

//opencode: compare authored color channels without depending on struct padding.
static gX_I1 gfx_material_asset_color_equal(gX_Color left, gX_Color right) {
    return left.r == right.r && left.g == right.g
        && left.b == right.b && left.a == right.a;
}

//opencode: opt-in persistence check covers the same save/read/apply helpers as
//   the editor and confirms model-local targets and runtime IDs stay local.
static gX_I1 gfx_material_asset_headless_check(const gX_I8 *path) {
    gX_RendererGFX_MaterialOverride source = {0};
    snprintf(source.material, sizeof(source.material), "source_only_material");
    source.emissiveColor = (gX_Color){17, 34, 51, 255};
    source.emissive = 2.75f;
    source.albedo = (gX_Color){91, 123, 177, 255};
    source.roughness = 0.27f;
    source.metalness = 0.81f;
    snprintf(source.albedoMap, sizeof(source.albedoMap), "fixture/albedo.png");
    snprintf(source.emissiveMap, sizeof(source.emissiveMap), "fixture/emission.png");
    source.albedoMapID = 43;
    source.emissiveMapID = 47;
    gX_I1 saved = gfx_material_asset_save(&source, "material_asset_probe",
        "temporary serialization fixture", path);
    gX_GFX_Material_Asset loaded;
    gX_I1 read = saved && gfx_material_asset_load(path, &loaded);
    gX_RendererGFX_MaterialOverride target = {0};
    snprintf(target.material, sizeof(target.material), "destination_material");
    target.albedoMapID = 101;
    target.emissiveMapID = 103;
    if (read) gfx_material_asset_apply(&loaded, &target);
    gX_I1 valuesEqual = read
        && strcmp(loaded.name, "material_asset_probe") == 0
        && strcmp(loaded.description, "temporary serialization fixture") == 0
        && gfx_material_asset_color_equal(source.emissiveColor, target.emissiveColor)
        && fabsf(source.emissive - target.emissive) < 0.00001f
        && gfx_material_asset_color_equal(source.albedo, target.albedo)
        && fabsf(source.roughness - target.roughness) < 0.00001f
        && fabsf(source.metalness - target.metalness) < 0.00001f
        && strcmp(source.albedoMap, target.albedoMap) == 0
        && strcmp(source.emissiveMap, target.emissiveMap) == 0
        && strcmp(target.material, "destination_material") == 0
        && target.albedoMapID == -1 && target.emissiveMapID == -1;
    gX_TraceLog(valuesEqual ? gX_LOG_NOTICE : gX_LOG_ERROR,
        "GFX_MATERIAL_ASSET_CHECK: %s", valuesEqual ? "PASS" : "FAIL");
    return valuesEqual;
}

//opencode: common case-insensitive substring matching for authoring browsers.
static gX_I1 gfx_text_search_matches(const gX_I8 *text, const gX_I8 *query) {
    if (!query || !query[0]) return 1;
    if (!text) return 0;
    for (const gX_I8 *candidate = text; *candidate; candidate++) {
        const gX_I8 *scan = candidate;
        const gX_I8 *search = query;
        while (*scan && *search
            && tolower((unsigned char)*scan) == tolower((unsigned char)*search)) {
            scan++;
            search++;
        }
        if (!*search) return 1;
    }
    return 0;
}

//opencode: preset browsing accepts only JSON files with the GFX scene shape.
static gX_I1 gfx_preset_asset_is_valid(const gX_I8 *path) {
    if (!path) return 0;
    const gX_I8 *extension = strrchr(path, '.');
    if (!extension || strlen(extension) != 5
        || tolower((unsigned char)extension[0]) != '.'
        || tolower((unsigned char)extension[1]) != 'j'
        || tolower((unsigned char)extension[2]) != 's'
        || tolower((unsigned char)extension[3]) != 'o'
        || tolower((unsigned char)extension[4]) != 'n'
        || extension[5] != 0) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    gX_I8 contents[16384];
    size_t length = fread(contents, 1, sizeof(contents) - 1, file);
    fclose(file);
    contents[length] = 0;
    return strstr(contents, "\"state\"") && strstr(contents, "\"models\"");
}

//opencode: deterministic preset ordering keeps the browser stable between runs.
static int gfx_preset_asset_compare(const void *left, const void *right) {
    const gX_I8 *leftPath = *(const gX_I8 *const *)left;
    const gX_I8 *rightPath = *(const gX_I8 *const *)right;
    return strcmp(leftPath, rightPath);
}

//opencode: refresh and release the lightweight list of authoring presets.
static gX_I0 gfx_preset_assets_scan(gX_I0) {
    if (s_presetFiles.paths) UnloadDirectoryFiles(s_presetFiles);
    s_presetFiles = LoadDirectoryFilesEx("resources/tools", "FILES*", false);
    if (s_presetFiles.count > 1)
        qsort(s_presetFiles.paths, s_presetFiles.count,
            sizeof(s_presetFiles.paths[0]), gfx_preset_asset_compare);
    gX_U32 validCount = 0;
    s_presetSelectedPath[0] = 0;
    for (gX_U32 pi = 0; pi < s_presetFiles.count; pi++) {
        const gX_I8 *path = s_presetFiles.paths[pi];
        if (!gfx_preset_asset_is_valid(path)) continue;
        s_presetFiles.paths[validCount++] = s_presetFiles.paths[pi];
        if (strcmp(path, s_presetPath) == 0)
            snprintf(s_presetSelectedPath, sizeof(s_presetSelectedPath), "%s", path);
    }
    s_presetFiles.count = validCount;
}

//opencode: close the transient raylib directory index when the editor restarts.
static gX_I0 gfx_preset_assets_release(gX_I0) {
    if (s_presetFiles.paths) UnloadDirectoryFiles(s_presetFiles);
    s_presetFiles = (FilePathList){0};
    s_presetSelectedPath[0] = 0;
}

//opencode: the preset browser uses the same explicit replace confirmation as typed paths.
static gX_I0 gfx_preset_browser_popup(gX_I0) {
    if (ImGui::Button("Browse")) {
        gfx_preset_assets_scan();
        s_presetSearch[0] = 0;
        ImGui::OpenPopup("GFX Preset Browser");
    }
    if (!ImGui::BeginPopupModal("GFX Preset Browser", NULL,
            ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::InputTextWithHint("##gfxPresetSearch", "Search GFX presets...",
        s_presetSearch, sizeof(s_presetSearch));
    ImGui::BeginChild("GfxPresetFiles", ImVec2(560.0f, 300.0f), true);
    gX_U32 matching = 0;
    for (gX_U32 pi = 0; pi < s_presetFiles.count; pi++) {
        const gX_I8 *path = s_presetFiles.paths[pi];
        if (!gfx_text_search_matches(path, s_presetSearch)) continue;
        matching++;
        gX_I1 selected = strcmp(s_presetSelectedPath, path) == 0;
        if (ImGui::Selectable(path, selected))
            snprintf(s_presetSelectedPath, sizeof(s_presetSelectedPath), "%s", path);
    }
    if (matching == 0) ImGui::TextDisabled("No GFX presets match this search.");
    ImGui::EndChild();
    ImGui::TextWrapped("Selected: %s", s_presetSelectedPath[0]
        ? s_presetSelectedPath : "none");
    gX_I1 pathFits = s_presetSelectedPath[0]
        && strlen(s_presetSelectedPath) < sizeof(s_presetPath);
    ImGui::BeginDisabled(!pathFits);
    if (ImGui::Button("Use path")) {
        snprintf(s_presetPath, sizeof(s_presetPath), "%s", s_presetSelectedPath);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load and replace...")) {
        snprintf(s_presetPath, sizeof(s_presetPath), "%s", s_presetSelectedPath);
        ImGui::CloseCurrentPopup();
        s_presetConfirmPending = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) gfx_preset_assets_scan();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

//opencode: v0.6.10 — JSON preset persistence for live GFX authoring.
//opencode: commit an explicitly authorized preset write.
//opencode: camera/render status line for the scene panel.
static gX_I0 gfx_camera_status(const gX_I8 *text, gX_I1 ok) {
    snprintf(s_renderStatus, sizeof(s_renderStatus), "%s", text ? text : "");
    s_renderStatusOK = ok;
}

static struct json_object *gfx_camera_json_vec3(struct json_object *parent,
    const gX_I8 *key, gX_F32v3 value) {
    struct json_object *array = json_object_new_array();
    json_object_array_add(array, json_object_new_double((gX_F64)value.x));
    json_object_array_add(array, json_object_new_double((gX_F64)value.y));
    json_object_array_add(array, json_object_new_double((gX_F64)value.z));
    json_object_object_add(parent, key, array);
    return array;
}

static gX_I1 gfx_camera_json_read_vec3(struct json_object *parent,
    const gX_I8 *key, gX_F32v3 *out) {
    struct json_object *member = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &member)
        || !gfx_material_json_is_type(member, json_type_array)
        || json_object_array_length(member) != 3) return 0;
    gX_F32 values[3];
    for (gX_I32 i = 0; i < 3; i++) {
        struct json_object *item = json_object_array_get_idx(member, (gX_U32)i);
        if (!item) return 0;
        gX_F64 parsed = json_object_get_double(item);
        if (!isfinite(parsed)) return 0;
        values[i] = (gX_F32)parsed;
    }
    out->x = values[0];
    out->y = values[1];
    out->z = values[2];
    return 1;
}

//opencode: presets carry the camera list as a top-level section the engine
//   schema does not know about; the editor appends it after a save and reads
//   it back after a load.
static gX_I0 gfx_cameras_preset_write(const gX_I8 *path) {
    struct json_object *root = json_object_from_file((char *)path);
    if (!root) root = json_object_new_object();
    struct json_object *array = json_object_new_array();
    for (gX_I32 ci = 0; ci < s_cameraCount; ci++) {
        const gX_GFX_Editor_Camera *cameraEntry = &s_cameras[ci];
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "name",
            json_object_new_string((const char *)cameraEntry->name));
        json_object_object_add(entry, "description",
            json_object_new_string((const char *)cameraEntry->description));
        struct json_object *view = gX_RendererGFX_Preset_CameraToObj(&cameraEntry->view);
        json_object_object_add(entry, "view", view);
        json_object_array_add(array, entry);
    }
    json_object_object_add(root, "cameras", array);
    gX_I1 ok = json_object_to_file_ext((char *)path, root,
        JSON_C_TO_STRING_PRETTY) == 0;
    json_object_put(root);
    if (!ok) gfx_camera_status("Camera section write failed", 0);
}

static gX_I0 gfx_cameras_preset_read(const gX_I8 *path) {
    struct json_object *root = json_object_from_file((char *)path);
    if (!root) return;
    struct json_object *array = NULL;
    if (json_object_object_get_ex(root, "cameras", &array)
        && gfx_material_json_is_type(array, json_type_array)) {
        s_cameraCount = 0;
        gX_U32 entries = json_object_array_length(array);
        for (gX_U32 ci = 0; ci < entries && s_cameraCount < GX_GFX_EDITOR_CAMERAS; ci++) {
            struct json_object *entry = json_object_array_get_idx(array, ci);
            if (!entry || !gfx_material_json_is_type(entry, json_type_object)) continue;
            gX_GFX_Editor_Camera *cameraEntry = &s_cameras[s_cameraCount];
            memset(cameraEntry, 0, sizeof(*cameraEntry));
            cameraEntry->view = camera.camera3D;
            struct json_object *member = NULL;
            if (json_object_object_get_ex(entry, "name", &member)
                && gfx_material_json_is_type(member, json_type_string))
                snprintf((char *)cameraEntry->name, sizeof(cameraEntry->name),
                    "%s", json_object_get_string(member));
            if (json_object_object_get_ex(entry, "description", &member)
                && gfx_material_json_is_type(member, json_type_string))
                snprintf((char *)cameraEntry->description,
                    sizeof(cameraEntry->description), "%s",
                    json_object_get_string(member));
            struct json_object *view = NULL;
            if (json_object_object_get_ex(entry, "view", &view)
                && gfx_material_json_is_type(view, json_type_object)) {
                gfx_camera_json_read_vec3(view, "position", &cameraEntry->view.position);
                gfx_camera_json_read_vec3(view, "target", &cameraEntry->view.target);
                gfx_camera_json_read_vec3(view, "up", &cameraEntry->view.up);
                if (json_object_object_get_ex(view, "fovy", &member)) {
                    gX_F64 parsed = json_object_get_double(member);
                    if (isfinite(parsed) && parsed > 1.0) cameraEntry->view.fovy = (gX_F32)parsed;
                }
                if (json_object_object_get_ex(view, "projection", &member))
                    cameraEntry->view.projection = json_object_get_int(member);
            }
            s_cameraCount++;
        }
        if (s_selectedCamera >= s_cameraCount) s_selectedCamera = s_cameraCount - 1;
    }
    json_object_put(root);
}

//opencode: crop the presented frame to the docked scene viewport and export.
static gX_I1 gfx_camera_capture(const gX_I8 *path) {
    gX_Image frame = gX_Image_LoadFromScreen();
    if (!frame.data) return 0;
    gX_Rectangle box = gX_GUI_ViewportBox_Get();
    gX_I32 x = (gX_I32)box.x;
    gX_I32 y = (gX_I32)box.y;
    gX_I32 w = (gX_I32)box.width;
    gX_I32 h = (gX_I32)box.height;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > frame.width) w = frame.width - x;
    if (y + h > frame.height) h = frame.height - y;
    gX_I1 ok = 0;
    if (w > 0 && h > 0) {
        gX_Image cropped = gX_Image_FromImage(frame,
            (gX_Rectangle){(gX_F32)x, (gX_F32)y, (gX_F32)w, (gX_F32)h});
        if (cropped.data) {
            if (s_renderWidth > 0 && s_renderHeight > 0
                && (cropped.width != s_renderWidth
                    || cropped.height != s_renderHeight)) {
                gX_Image_Resize(&cropped, s_renderWidth, s_renderHeight);
            }
            ok = gX_Image_Export(cropped, path);
            gX_Image_Unload(cropped);
        }
    }
    gX_Image_Unload(frame);
    return ok;
}

//opencode: render tick — one frame applies the selected camera, the next
//   captures the presented frame and restores the authoring view.
static gX_I0 gfx_camera_render_tick(gX_RendererGFX *gfx) {
    if (!gfx) return;
    if (s_renderApplied) {
        gX_I1 ok = gfx_camera_capture(s_renderOutput);
        camera.camera3D = s_renderSavedView;
        gfx->state.SHOW_LIGHT_MARKERS = s_renderSavedMarkers;
        s_renderApplied = 0;
        gfx_camera_status(ok ? "Render saved" : "Render failed", ok);
        gX_TraceLog(ok ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_RENDER_CAMERA: %s path=%s", ok ? "PASS" : "FAIL",
            s_renderOutput);
        return;
    }
    if (!s_renderPending) return;
    s_renderPending = 0;
    if (s_selectedCamera < 0 || s_selectedCamera >= s_cameraCount) {
        gfx_camera_status("Render failed: no camera selected", 0);
        return;
    }
    s_renderSavedView = camera.camera3D;
    s_renderSavedMarkers = gfx->state.SHOW_LIGHT_MARKERS;
    camera.camera3D = s_cameras[s_selectedCamera].view;
    gfx->state.SHOW_LIGHT_MARKERS = 0;
    s_renderApplied = 1;
}

//opencode: primitive type names shared by the UI, the trace and the hook.
static const gX_I8 *gfx_primitive_names(gX_I32 type) {
    static const gX_I8 *names[] = {
        "cube", "plane", "sphere", "cylinder", "cone", "torus" };
    return (type >= 0 && type < 6) ? names[type] : "?";
}

//opencode: generate a primitive mesh, upload it, and add it as a model.
//   gX_Mesh mirrors raylib's Mesh layout, so the generated buffers transfer
//   field by field and gX_Mesh_Upload owns the VAO/VBO ids the model frees.
static gX_I32 gfx_primitive_add(gX_RendererGFX *gfx, gX_I32 type, gX_F32 size,
    gX_I32 detail)
{
    if (size <= 0.01f) size = 1.0f;
    if (detail < 0) detail = 0;
    if (detail > 12) detail = 12;
    gX_I32 rings = 4 + detail * 2;
    gX_I32 slices = 8 + detail * 4;
    Mesh generated = {0};
    switch (type) {
        case 0: generated = GenMeshCube(size, size, size); break;
        case 1: generated = GenMeshPlane(size, size, 1 + detail, 1 + detail); break;
        case 2: generated = GenMeshSphere(size * 0.5f, rings, slices); break;
        case 3: generated = GenMeshCylinder(size * 0.5f, size, slices); break;
        case 4: generated = GenMeshCone(size * 0.5f, size, slices); break;
        case 5: generated = GenMeshTorus(size * 0.35f, size * 0.5f, rings, slices);
            break;
        default: return -1;
    }
    if (generated.vertexCount <= 0 || generated.triangleCount <= 0) return -1;
    gX_Mesh mesh = {0};
    mesh.vertexCount = generated.vertexCount;
    mesh.triangleCount = generated.triangleCount;
    mesh.vertices = (gX_F32 *)generated.vertices;
    mesh.texcoords = (gX_F32 *)generated.texcoords;
    mesh.texcoords2 = (gX_F32 *)generated.texcoords2;
    mesh.normals = (gX_F32 *)generated.normals;
    mesh.tangents = (gX_F32 *)generated.tangents;
    mesh.colors = (gX_U8 *)generated.colors;
    mesh.indices = (gX_U16 *)generated.indices;
    //opencode: gX_Mesh shares raylib's Mesh field prefix (up to vboId), which
    //   is all UploadMesh touches; the ids transfer into the model's copy.
    UploadMesh((Mesh *)&mesh, 0);
    gX_I32 index = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
    if (index < 0) return -1;
    gX_RendererGFX_Model *model = gfx_model_at(gfx, index);
    if (model) {
        model->params.usePBR = 1;
        model->params.metalness = 0.1f;
        model->params.roughness = 0.45f;
        model->params.albedoTint = (gX_Color){196, 226, 240, 255};
    }
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: primitive added type=%s size=%.2f detail=%d index=%d verts=%d",
        (const char *)gfx_primitive_names(type), (gX_F64)size, detail, index,
        generated.vertexCount);
    return index;
}

//opencode: texture-utility helpers (tooling scope): procedural generates, a
//   normal-from-height bake and ORM channel packing. Outputs are PNG files in
//   the generated-textures folder; GX_GFX_TEXTURE_DIR overrides it headlessly.
static const gX_I8 *gfx_texture_out_dir(gX_I0) {
    const char *override = getenv("GX_GFX_TEXTURE_DIR");
    return (override && override[0]) ? (const gX_I8 *)override
        : (const gX_I8 *)"resources/tools/textures";
}

static const gX_I8 *gfx_texture_kind_name(gX_I32 kind) {
    static const gX_I8 *names[] = {
        "solid", "checker", "gradient", "radial", "noise", "perlin",
        "cellular", "value" };
    return (kind >= 0 && kind < 8) ? names[kind] : "?";
}

static Color gfx_texture_color(gX_Color color) {
    Color out = { color.r, color.g, color.b, color.a };
    return out;
}

static gX_I0 gfx_texture_status(const char *label, const gX_I8 *path,
    gX_I1 ok)
{
    if (path && path[0])
        snprintf((char *)s_texStatus, sizeof(s_texStatus), "%s %s", label,
            (const char *)path);
    else
        snprintf((char *)s_texStatus, sizeof(s_texStatus), "%s", label);
    s_texStatusOK = ok;
}

static gX_I1 gfx_texture_export(Image image, const gX_I8 *name,
    gX_I8 *outPath, gX_I32 outSize)
{
    const gX_I8 *folder = gfx_texture_out_dir();
    if (!DirectoryExists((const char *)folder))
        MakeDirectory((const char *)folder);
    gX_I8 path[640];
    snprintf((char *)path, sizeof(path), "%s/%s.png",
        (const char *)folder, (const char *)name);
    gX_I1 ok = (gX_I1)ExportImage(image, (const char *)path);
    if (ok && outPath && outSize > 0)
        snprintf((char *)outPath, outSize, "%s", (const char *)path);
    return ok;
}

//opencode: value noise — integer-hash lattice with smoothstep bilinear
//   interpolation (deterministic across runs and compilers).
static gX_F32 gfx_texture_value_hash(gX_I32 x, gX_I32 y) {
    gX_U32 h = (gX_U32)(x * 374761393) ^ (gX_U32)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (gX_F32)(h & 0xFFFFFFu) / (gX_F32)0xFFFFFF;
}

static gX_F32 gfx_texture_value_noise(gX_F32 x, gX_F32 y) {
    gX_I32 xi = (gX_I32)floorf(x);
    gX_I32 yi = (gX_I32)floorf(y);
    gX_F32 fx = x - (gX_F32)xi;
    gX_F32 fy = y - (gX_F32)yi;
    gX_F32 sx = fx * fx * (3.0f - 2.0f * fx);
    gX_F32 sy = fy * fy * (3.0f - 2.0f * fy);
    gX_F32 n00 = gfx_texture_value_hash(xi, yi);
    gX_F32 n10 = gfx_texture_value_hash(xi + 1, yi);
    gX_F32 n01 = gfx_texture_value_hash(xi, yi + 1);
    gX_F32 n11 = gfx_texture_value_hash(xi + 1, yi + 1);
    gX_F32 nx0 = n00 + (n10 - n00) * sx;
    gX_F32 nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

//opencode: preview loader for the Texture tools panel (and the headless
//   texture gate). Reloads only when the path changes.
static gX_I0 gfx_texture_preview_load(const gX_I8 *path) {
    if (!path || !path[0]) return;
    if (s_texPreview.id > 0 && s_texPreviewPath[0]
        && strcmp((const char *)s_texPreviewPath, (const char *)path) == 0)
        return;
    if (s_texPreview.id > 0) {
        UnloadTexture(s_texPreview);
        s_texPreview = (Texture2D){0};
    }
    s_texPreviewPath[0] = '\0';
    Texture2D texture = LoadTexture((const char *)path);
    if (texture.id == 0) {
        gX_TraceLog(gX_LOG_WARNING, "GfxEd: texture preview failed file=%s",
            (const char *)path);
        return;
    }
    s_texPreview = texture;
    snprintf((char *)s_texPreviewPath, sizeof(s_texPreviewPath), "%s",
        (const char *)path);
    gX_TraceLog(gX_LOG_INFO, "GfxEd: texture preview file=%s",
        (const char *)path);
}

static gX_I1 gfx_texture_generate(gX_I32 kind, gX_I32 width, gX_I32 height,
    gX_F32 param, gX_Color colorA, gX_Color colorB, const gX_I8 *name,
    gX_I8 *outPath, gX_I32 outSize)
{
    if (width < 8) width = 8;
    if (height < 8) height = 8;
    if (width > 2048) width = 2048;
    if (height > 2048) height = 2048;
    Color cA = gfx_texture_color(colorA);
    Color cB = gfx_texture_color(colorB);
    Image image = {0};
    if (kind == 1) {
        gX_I32 cells = (gX_I32)param;
        if (cells < 2) cells = 2;
        if (cells > 64) cells = 64;
        image = GenImageChecked(width, height, cells, cells, cA, cB);
    } else if (kind == 2) {
        gX_I32 direction = (gX_I32)param;
        if (direction < 0) direction = 0;
        if (direction > 360) direction = 360;
        image = GenImageGradientLinear(width, height, direction, cA, cB);
    } else if (kind == 3) {
        gX_F32 density = param < 0.0f ? 0.0f : (param > 1.0f ? 1.0f : param);
        image = GenImageGradientRadial(width, height, density, cA, cB);
    } else if (kind == 4) {
        gX_F32 factor = param < 0.0f ? 0.0f : (param > 1.0f ? 1.0f : param);
        image = GenImageWhiteNoise(width, height, factor);
    } else if (kind == 5) {
        gX_F32 scale = param < 1.0f ? 1.0f : (param > 64.0f ? 64.0f : param);
        image = GenImagePerlinNoise(width, height, 0, 0, scale);
    } else if (kind == 6) {
        gX_I32 tile = (gX_I32)param;
        if (tile < 2) tile = 2;
        if (tile > 64) tile = 64;
        image = GenImageCellular(width, height, tile);
    } else if (kind == 7) {
        gX_I32 cells = (gX_I32)param;
        if (cells < 1) cells = 1;
        if (cells > 64) cells = 64;
        image = GenImageColor(width, height, BLACK);
        gX_U8 *out = (gX_U8 *)image.data;
        for (gX_I32 y = 0; y < height; y++) {
            for (gX_I32 x = 0; x < width; x++) {
                gX_F32 fx = (gX_F32)x / (gX_F32)width * (gX_F32)cells;
                gX_F32 fy = (gX_F32)y / (gX_F32)height * (gX_F32)cells;
                gX_U8 value = (gX_U8)(gfx_texture_value_noise(fx, fy) * 255.0f);
                gX_I32 pi = (y * width + x) * 4;
                out[pi + 0] = value;
                out[pi + 1] = value;
                out[pi + 2] = value;
                out[pi + 3] = 255;
            }
        }
    } else {
        image = GenImageColor(width, height, cA);
    }
    if (!image.data || image.width <= 0 || image.height <= 0) {
        gX_TraceLog(gX_LOG_ERROR, "GfxEd: texture generate failed kind=%d",
            kind);
        return 0;
    }
    gX_I1 ok = gfx_texture_export(image, name, outPath, outSize);
    gX_TraceLog(ok ? gX_LOG_INFO : gX_LOG_ERROR,
        "GfxEd: texture generated kind=%s size=%dx%d file=%s",
        (const char *)gfx_texture_kind_name(kind), image.width, image.height,
        (ok && outPath) ? (const char *)outPath : "?");
    if (ok && outPath) gfx_texture_preview_load(outPath);
    UnloadImage(image);
    return ok;
}

static gX_F32 gfx_texture_luminance(const gX_U8 *pixels, gX_I32 x, gX_I32 y,
    gX_I32 width, gX_I32 height)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width) x = width - 1;
    if (y >= height) y = height - 1;
    const gX_U8 *p = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4;
    return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
}

static gX_I1 gfx_texture_normal_from_height(const gX_I8 *source,
    gX_F32 strength, const gX_I8 *name, gX_I8 *outPath, gX_I32 outSize)
{
    Image input = LoadImage((const char *)source);
    if (!input.data || input.width < 2 || input.height < 2) {
        if (input.data) UnloadImage(input);
        gX_TraceLog(gX_LOG_ERROR, "GfxEd: texture normal failed source=%s",
            (const char *)source);
        return 0;
    }
    ImageFormat(&input, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    gX_I32 width = input.width;
    gX_I32 height = input.height;
    const gX_U8 *pixels = (const gX_U8 *)input.data;
    Image output = GenImageColor(width, height, BLANK);
    gX_U8 *out = (gX_U8 *)output.data;
    for (gX_I32 y = 0; y < height; y++) {
        for (gX_I32 x = 0; x < width; x++) {
            gX_F32 tl = gfx_texture_luminance(pixels, x - 1, y - 1, width,
                height);
            gX_F32 tc = gfx_texture_luminance(pixels, x, y - 1, width, height);
            gX_F32 tr = gfx_texture_luminance(pixels, x + 1, y - 1, width,
                height);
            gX_F32 ml = gfx_texture_luminance(pixels, x - 1, y, width, height);
            gX_F32 mr = gfx_texture_luminance(pixels, x + 1, y, width, height);
            gX_F32 bl = gfx_texture_luminance(pixels, x - 1, y + 1, width,
                height);
            gX_F32 bc = gfx_texture_luminance(pixels, x, y + 1, width, height);
            gX_F32 br = gfx_texture_luminance(pixels, x + 1, y + 1, width,
                height);
            gX_F32 dx = (tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl);
            gX_F32 dy = (bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr);
            gX_F32 nx = -dx * strength;
            gX_F32 ny = -dy * strength;
            gX_F32 inv = 1.0f / sqrtf(nx * nx + ny * ny + 1.0f);
            gX_I32 pi = (y * width + x) * 4;
            out[pi + 0] = (gX_U8)(255.0f * (nx * inv * 0.5f + 0.5f));
            out[pi + 1] = (gX_U8)(255.0f * (ny * inv * 0.5f + 0.5f));
            out[pi + 2] = (gX_U8)(255.0f * (inv * 0.5f + 0.5f));
            out[pi + 3] = 255;
        }
    }
    gX_I1 ok = gfx_texture_export(output, name, outPath, outSize);
    gX_TraceLog(ok ? gX_LOG_INFO : gX_LOG_ERROR,
        "GfxEd: texture normal strength=%.2f size=%dx%d file=%s",
        (gX_F64)strength, width, height,
        (ok && outPath) ? (const char *)outPath : "?");
    if (ok && outPath) gfx_texture_preview_load(outPath);
    UnloadImage(output);
    UnloadImage(input);
    return ok;
}

static gX_U8 gfx_texture_channel(const gX_U8 *pixels, gX_I32 srcWidth,
    gX_I32 srcHeight, gX_I32 width, gX_I32 height, gX_I32 x, gX_I32 y)
{
    gX_I32 sx = (gX_I32)(((gX_I64)x * srcWidth) / width);
    gX_I32 sy = (gX_I32)(((gX_I64)y * srcHeight) / height);
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= srcWidth) sx = srcWidth - 1;
    if (sy >= srcHeight) sy = srcHeight - 1;
    return pixels[((size_t)sy * (size_t)srcWidth + (size_t)sx) * 4];
}

static gX_I1 gfx_texture_pack_orm(const gX_I8 *aoPath, const gX_I8 *roughPath,
    const gX_I8 *metalPath, const gX_I8 *name, gX_I8 *outPath, gX_I32 outSize)
{
    Image ao = LoadImage((const char *)aoPath);
    Image rough = LoadImage((const char *)roughPath);
    Image metal = LoadImage((const char *)metalPath);
    if (!ao.data || !rough.data || !metal.data) {
        if (ao.data) UnloadImage(ao);
        if (rough.data) UnloadImage(rough);
        if (metal.data) UnloadImage(metal);
        gX_TraceLog(gX_LOG_ERROR,
            "GfxEd: texture orm failed ao=%s roughness=%s metallic=%s",
            (const char *)aoPath, (const char *)roughPath,
            (const char *)metalPath);
        return 0;
    }
    ImageFormat(&ao, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageFormat(&rough, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageFormat(&metal, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    gX_I32 width = ao.width;
    gX_I32 height = ao.height;
    const gX_U8 *aoPixels = (const gX_U8 *)ao.data;
    const gX_U8 *roughPixels = (const gX_U8 *)rough.data;
    const gX_U8 *metalPixels = (const gX_U8 *)metal.data;
    Image output = GenImageColor(width, height, BLANK);
    gX_U8 *out = (gX_U8 *)output.data;
    for (gX_I32 y = 0; y < height; y++) {
        for (gX_I32 x = 0; x < width; x++) {
            gX_I32 pi = (y * width + x) * 4;
            out[pi + 0] = gfx_texture_channel(aoPixels, ao.width, ao.height,
                width, height, x, y);
            out[pi + 1] = gfx_texture_channel(roughPixels, rough.width,
                rough.height, width, height, x, y);
            out[pi + 2] = gfx_texture_channel(metalPixels, metal.width,
                metal.height, width, height, x, y);
            out[pi + 3] = 255;
        }
    }
    gX_I1 ok = gfx_texture_export(output, name, outPath, outSize);
    gX_TraceLog(ok ? gX_LOG_INFO : gX_LOG_ERROR,
        "GfxEd: texture orm size=%dx%d file=%s", width, height,
        (ok && outPath) ? (const char *)outPath : "?");
    if (ok && outPath) gfx_texture_preview_load(outPath);
    UnloadImage(output);
    UnloadImage(ao);
    UnloadImage(rough);
    UnloadImage(metal);
    return ok;
}

//opencode: scene-export helpers (tooling scope): bake each model's world
//   transform into a mesh copy and write OBJ through raylib's ExportMesh. The
//   transform uses the gxmath column-vector convention (row = m0,m4,m8,m12).
static const gX_I8 *gfx_export_out_dir(gX_I0) {
    const char *override = getenv("GX_GFX_EXPORT_DIR");
    return (override && override[0]) ? (const gX_I8 *)override
        : (const gX_I8 *)"resources/tools/exports";
}

static gX_I0 gfx_export_status(const char *label, const gX_I8 *path, gX_I1 ok)
{
    if (path && path[0])
        snprintf((char *)s_exportStatus, sizeof(s_exportStatus), "%s %s",
            label, (const char *)path);
    else
        snprintf((char *)s_exportStatus, sizeof(s_exportStatus), "%s", label);
    s_exportStatusOK = ok;
}

static gX_I0 gfx_export_slug(const gX_I8 *path, gX_I8 *out, gX_I32 outSize) {
    const gX_I8 *slash = strrchr(path, '/');
    const gX_I8 *base = slash ? slash + 1 : path;
    gX_I32 si = 0;
    for (const gX_I8 *cursor = base; *cursor && si < outSize - 1; cursor++) {
        gX_I8 c = *cursor;
        if (c == '.') break;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[si++] = c;
        else if (si > 0 && out[si - 1] != '-')
            out[si++] = '-';
    }
    if (si == 0)
        snprintf((char *)out, outSize, "model");
    else
        out[si] = 0;
}

//opencode: export v2 slice 1 — MTL sidecars. The values mirror the render-side
//   material resolution (same base color + tint through AlbedoTint_Apply, the
//   effective per-mesh arrays when a sidecar overrides them), and override map
//   paths are copied next to the OBJ so the export stays self-contained for
//   Blender/Krita. GLB export remains a later slice.
static gX_I0 gfx_export_basename(const gX_I8 *path, gX_I8 *out, gX_I32 outSize) {
    const gX_I8 *slash = strrchr((const char *)path, '/');
    const gX_I8 *base = slash ? slash + 1 : path;
    snprintf((char *)out, outSize, "%s", (const char *)base);
    gX_I8 *dot = strrchr((char *)out, '.');
    if (dot) *dot = '\0';
}

static gX_I0 gfx_export_mtl_path(const gX_I8 *objPath, gX_I8 *out, gX_I32 outSize) {
    snprintf((char *)out, outSize, "%s", (const char *)objPath);
    gX_I8 *dot = strrchr((char *)out, '.');
    if (dot) snprintf((char *)dot, (size_t)(outSize - (dot - out)), ".mtl");
}

static gX_F32 gfx_export_clamp01(gX_F32 value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

//opencode: copy an override map (resource subpath under resources/texture/)
//   next to the export and return the local basename the MTL references.
static gX_I0 gfx_export_copy_map(const gX_I8 *subpath, const gX_I8 *folder,
    gX_I8 *outName, gX_I32 outSize)
{
    outName[0] = '\0';
    if (!subpath || !subpath[0]) return;
    const gX_I8 *slash = strrchr((const char *)subpath, '/');
    const gX_I8 *base = slash ? slash + 1 : subpath;
    if (!base[0]) return;
    gX_I8 src[512];
    snprintf((char *)src, sizeof(src), "resources/texture/%s",
        (const char *)subpath);
    if (!FileExists((const char *)src)) return;
    gX_I8 dst[640];
    snprintf((char *)dst, sizeof(dst), "%s/%s", (const char *)folder,
        (const char *)base);
    gX_I32 size = 0;
    gX_U8 *data = gX_FileData_Load(src, &size);
    if (!data || size <= 0) {
        gX_FileData_Unload(data);
        return;
    }
    if (gX_FileData_Save(dst, (gX_I0 *)data, size))
        snprintf((char *)outName, outSize, "%s", (const char *)base);
    gX_FileData_Unload(data);
}

static gX_I1 gfx_export_mtl_write(const gX_RendererGFX_Model *model,
    gX_I32 meshIndex, const gX_I8 *mtlPath, const gX_I8 *materialName,
    const gX_I8 *folder)
{
    gX_RendererGFX_MeshMaterialValues values;
    gX_RendererGFX_Model_MeshMaterialValues(model, meshIndex, &values);
    gX_F32 keR = 0.0f, keG = 0.0f, keB = 0.0f;
    if (values.emissiveGlow > 0.0f) {
        keR = gfx_export_clamp01((gX_F32)values.emissive.r / 255.0f
            * values.emissiveGlow);
        keG = gfx_export_clamp01((gX_F32)values.emissive.g / 255.0f
            * values.emissiveGlow);
        keB = gfx_export_clamp01((gX_F32)values.emissive.b / 255.0f
            * values.emissiveGlow);
    }
    gX_I8 albedoMap[160] = {0};
    gX_I8 emissiveMap[160] = {0};
    gfx_export_copy_map(values.albedoMap, folder, albedoMap, sizeof(albedoMap));
    gfx_export_copy_map(values.emissiveMap, folder, emissiveMap,
        sizeof(emissiveMap));
    FILE *file = fopen((const char *)mtlPath, "wb");
    if (!file) return 0;
    fprintf(file, "# gX scene export\nnewmtl %s\n", (const char *)materialName);
    fprintf(file, "Kd %.6f %.6f %.6f\n", (double)values.albedo.r / 255.0,
        (double)values.albedo.g / 255.0, (double)values.albedo.b / 255.0);
    fprintf(file, "Ke %.6f %.6f %.6f\n", (double)keR, (double)keG, (double)keB);
    if (!model->params.usePBR)
        fprintf(file, "Ns %.3f\n", (double)model->params.shininess);
    else
        fprintf(file, "Pr %.6f\nPm %.6f\n",
            (double)gfx_export_clamp01(values.roughness),
            (double)gfx_export_clamp01(values.metalness));
    if (model->params.enableBlending && model->params.blendOpacity < 0.999f)
        fprintf(file, "d %.6f\n",
            (double)gfx_export_clamp01(model->params.blendOpacity));
    if (albedoMap[0]) fprintf(file, "map_Kd %s\n", (const char *)albedoMap);
    if (emissiveMap[0]) fprintf(file, "map_Ke %s\n", (const char *)emissiveMap);
    fclose(file);
    return 1;
}

//opencode: raylib's ExportMesh writes geometry only; prepend the mtllib/usemtl
//   linkage so the OBJ resolves the sidecar material.
static gX_I1 gfx_export_obj_link(const gX_I8 *objPath, const gX_I8 *mtlName,
    const gX_I8 *materialName)
{
    gX_I32 size = 0;
    gX_U8 *data = gX_FileData_Load(objPath, &size);
    if (!data || size <= 0) {
        gX_FileData_Unload(data);
        return 0;
    }
    FILE *file = fopen((const char *)objPath, "wb");
    if (!file) {
        gX_FileData_Unload(data);
        return 0;
    }
    fprintf(file, "mtllib %s\nusemtl %s\n", (const char *)mtlName,
        (const char *)materialName);
    size_t written = fwrite(data, 1, (size_t)size, file);
    fclose(file);
    gX_FileData_Unload(data);
    return written == (size_t)size;
}

static gX_I1 gfx_export_mesh_baked(const gX_Mesh *mesh, gX_Matrix world,
    const gX_I8 *path)
{
    if (!mesh->vertices || mesh->vertexCount <= 0 || mesh->triangleCount <= 0)
        return 0;
    gX_I32 vertexCount = mesh->vertexCount;
    Mesh out = {0};
    out.vertexCount = vertexCount;
    out.triangleCount = mesh->triangleCount;
    out.vertices = (float *)malloc((size_t)vertexCount * 3 * sizeof(float));
    if (!out.vertices) return 0;
    for (gX_I32 vi = 0; vi < vertexCount; vi++) {
        gX_F32 x = mesh->vertices[vi * 3 + 0];
        gX_F32 y = mesh->vertices[vi * 3 + 1];
        gX_F32 z = mesh->vertices[vi * 3 + 2];
        out.vertices[vi * 3 + 0] = world.m0 * x + world.m4 * y + world.m8 * z
            + world.m12;
        out.vertices[vi * 3 + 1] = world.m1 * x + world.m5 * y + world.m9 * z
            + world.m13;
        out.vertices[vi * 3 + 2] = world.m2 * x + world.m6 * y + world.m10 * z
            + world.m14;
    }
    //opencode: ExportMesh writes vt/vn unconditionally, so the bake always
    //   supplies both buffers; a mesh without UVs gets a zeroed vt array.
    out.texcoords = (float *)calloc((size_t)vertexCount * 2, sizeof(float));
    if (!out.texcoords) {
        free(out.vertices);
        return 0;
    }
    if (mesh->texcoords)
        memcpy(out.texcoords, mesh->texcoords,
            (size_t)vertexCount * 2 * sizeof(float));
    out.normals = (float *)calloc((size_t)vertexCount * 3, sizeof(float));
    if (!out.normals) {
        free(out.vertices);
        free(out.texcoords);
        return 0;
    }
    if (mesh->normals) {
        for (gX_I32 vi = 0; vi < vertexCount; vi++) {
            gX_F32 nx = mesh->normals[vi * 3 + 0];
            gX_F32 ny = mesh->normals[vi * 3 + 1];
            gX_F32 nz = mesh->normals[vi * 3 + 2];
            gX_F32 tx = world.m0 * nx + world.m4 * ny + world.m8 * nz;
            gX_F32 ty = world.m1 * nx + world.m5 * ny + world.m9 * nz;
            gX_F32 tz = world.m2 * nx + world.m6 * ny + world.m10 * nz;
            gX_F32 length = sqrtf(tx * tx + ty * ty + tz * tz);
            gX_F32 inv = (length > 1e-8f) ? 1.0f / length : 0.0f;
            out.normals[vi * 3 + 0] = tx * inv;
            out.normals[vi * 3 + 1] = ty * inv;
            out.normals[vi * 3 + 2] = tz * inv;
        }
    }
    if (mesh->indices) {
        out.indices = (unsigned short *)malloc((size_t)mesh->triangleCount * 3
            * sizeof(unsigned short));
        if (!out.indices) {
            free(out.vertices);
            free(out.texcoords);
            free(out.normals);
            return 0;
        }
        memcpy(out.indices, mesh->indices,
            (size_t)mesh->triangleCount * 3 * sizeof(unsigned short));
    }
    gX_I1 ok = (gX_I1)ExportMesh(out, (const char *)path);
    free(out.vertices);
    free(out.texcoords);
    free(out.normals);
    free(out.indices);
    return ok;
}

static gX_I32 gfx_export_model(gX_RendererGFX *gfx, gX_I32 index,
    const gX_I8 *prefix, gX_I32 *outMtls, gX_I32 *outGlbs)
{
    gX_RendererGFX_Model *model = gfx_model_at(gfx, index);
    if (!model || !model->loaded || model->model.meshCount <= 0) return 0;
    const gX_I8 *folder = gfx_export_out_dir();
    if (!DirectoryExists((const char *)folder))
        MakeDirectory((const char *)folder);
    gX_I8 slug[160];
    gfx_export_slug(model->path, slug, sizeof(slug));
    gX_Matrix world = gX_RendererGFX_Model_WorldMatrix(model);
    gX_I32 written = 0;
    gX_I32 mtls = 0;
    for (gX_I32 mi = 0; mi < model->model.meshCount; mi++) {
        gX_I8 path[640];
        if (model->model.meshCount == 1)
            snprintf((char *)path, sizeof(path), "%s/%s_%02d_%s.obj",
                (const char *)folder, (const char *)prefix, index,
                (const char *)slug);
        else
            snprintf((char *)path, sizeof(path), "%s/%s_%02d_%s_%d.obj",
                (const char *)folder, (const char *)prefix, index,
                (const char *)slug, mi);
        const gX_Mesh *mesh = &model->model.meshes[mi];
        if (!gfx_export_mesh_baked(mesh, world, path)) continue;
        //opencode: MTL sidecar + OBJ linkage (export v2 slice 1)
        gX_I8 base[200];
        gX_I8 mtlName[208];
        gX_I8 mtlPath[640];
        gfx_export_basename(path, base, sizeof(base));
        snprintf((char *)mtlName, sizeof(mtlName), "%s.mtl", (const char *)base);
        gfx_export_mtl_path(path, mtlPath, sizeof(mtlPath));
        if (gfx_export_mtl_write(model, mi, mtlPath, base, folder)
            && gfx_export_obj_link(path, mtlName, base)) {
            mtls++;
            gX_TraceLog(gX_LOG_INFO,
                "GfxEd: export mtl model=%d mesh=%d file=%s",
                index, mi, (const char *)mtlPath);
        }
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: export mesh model=%d mesh=%d verts=%d tris=%d file=%s",
            index, mi, mesh->vertexCount, mesh->triangleCount,
            (const char *)path);
        written++;
    }
    //opencode: one GLB per model (all meshes as primitives, maps embedded)
    gX_I8 glbPath[640];
    snprintf((char *)glbPath, sizeof(glbPath), "%s/%s_%02d_%s.glb",
        (const char *)folder, (const char *)prefix, index, (const char *)slug);
    gX_I32 glbs = 0;
    if (gX_RendererGFX_GLB_WriteModel(model, glbPath)) {
        glbs = 1;
        gX_TraceLog(gX_LOG_INFO, "GfxEd: export glb model=%d file=%s",
            index, (const char *)glbPath);
    }
    if (outMtls) *outMtls += mtls;
    if (outGlbs) *outGlbs += glbs;
    return written;
}

static gX_I32 gfx_export_all(gX_RendererGFX *gfx, const gX_I8 *prefix,
    gX_I32 *outMtls, gX_I32 *outGlbs) {
    gX_I32 files = 0;
    gX_I32 mtls = 0;
    gX_I32 glbs = 0;
    gX_I32 models = 0;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, (gX_I32)mi);
        if (!model || !model->loaded) continue;
        gX_I32 modelMtls = 0;
        gX_I32 modelGlbs = 0;
        gX_I32 written = gfx_export_model(gfx, (gX_I32)mi, prefix,
            &modelMtls, &modelGlbs);
        if (written > 0) {
            files += written;
            models++;
        }
        mtls += modelMtls;
        glbs += modelGlbs;
    }
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: export all models=%d files=%d mtls=%d glbs=%d",
        models, files, mtls, glbs);
    if (outMtls) *outMtls = mtls;
    if (outGlbs) *outGlbs = glbs;
    return files;
}

//opencode: decal bake (tooling scope, slice 4): project the scene's decals
//   top-down onto the XZ plane into an albedo PNG (bake color + coverage
//   alpha) and an ORM PNG (AO 1, bake roughness/metalness inside coverage,
//   full rough/no metal outside). Texture-mapped decals bake their footprint
//   solid (their map alpha is not sampled in v1).
static gX_I1 gfx_decal_bake(gX_RendererGFX *gfx, gX_I32 size, gX_F32 rough,
    gX_F32 metal, gX_Color color, const gX_I8 *dir, const gX_I8 *name,
    gX_I8 *outAlbedo, gX_I32 outSize)
{
    if (!gfx || size < 16) return 0;
    if (size > 2048) size = 2048;
    gX_F32 minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    gX_I32 count = 0;
    for (gX_U32 di = 0; di < gfx->decals.amount; di++) {
        gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, (gX_I32)di);
        if (!decal) continue;
        gX_F32 half = decal->size * 0.5f;
        if (decal->pos.x - half < minX) minX = decal->pos.x - half;
        if (decal->pos.x + half > maxX) maxX = decal->pos.x + half;
        if (decal->pos.z - half < minZ) minZ = decal->pos.z - half;
        if (decal->pos.z + half > maxZ) maxZ = decal->pos.z + half;
        count++;
    }
    if (count == 0 || minX > maxX || minZ > maxZ) return 0;
    gX_F32 pad = 0.1f * (maxX - minX + maxZ - minZ) * 0.5f + 0.01f;
    minX -= pad; maxX += pad; minZ -= pad; maxZ += pad;
    gX_U8 *albedo = (gX_U8 *)calloc((size_t)size * (size_t)size * 4, 1);
    gX_U8 *orm = (gX_U8 *)malloc((size_t)size * (size_t)size * 4);
    if (!albedo || !orm) {
        free(albedo);
        free(orm);
        return 0;
    }
    gX_U8 roughByte = (gX_U8)(rough * 255.0f + 0.5f);
    gX_U8 metalByte = (gX_U8)(metal * 255.0f + 0.5f);
    for (gX_I32 py = 0; py < size; py++) {
        for (gX_I32 px = 0; px < size; px++) {
            gX_F32 wx = minX + (maxX - minX) * ((gX_F32)px + 0.5f)
                / (gX_F32)size;
            gX_F32 wz = minZ + (maxZ - minZ) * ((gX_F32)py + 0.5f)
                / (gX_F32)size;
            gX_F32 coverage = 0.0f;
            for (gX_U32 di = 0; di < gfx->decals.amount; di++) {
                gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, (gX_I32)di);
                if (!decal) continue;
                gX_F32 dx = wx - decal->pos.x;
                gX_F32 dz = wz - decal->pos.z;
                gX_F32 cs = cosf(-decal->rotation);
                gX_F32 sn = sinf(-decal->rotation);
                gX_F32 lx = dx * cs - dz * sn;
                gX_F32 lz = dx * sn + dz * cs;
                gX_F32 half = decal->size * 0.5f;
                if (fabsf(lx) > half || fabsf(lz) > half) continue;
                if (decal->alpha > coverage) coverage = decal->alpha;
            }
            gX_I32 pi = (py * size + px) * 4;
            if (coverage > 0.0f) {
                albedo[pi + 0] = color.r;
                albedo[pi + 1] = color.g;
                albedo[pi + 2] = color.b;
                albedo[pi + 3] = (gX_U8)(coverage * 255.0f + 0.5f);
                orm[pi + 0] = 255;
                orm[pi + 1] = roughByte;
                orm[pi + 2] = metalByte;
            } else {
                orm[pi + 0] = 255;
                orm[pi + 1] = 255;
                orm[pi + 2] = 0;
            }
            orm[pi + 3] = 255;
        }
    }
    gX_I1 ok = 0;
    const gX_I8 *folder = dir && dir[0] ? dir
        : (const gX_I8 *)"resources/tools/bakes";
    if (!DirectoryExists((const char *)folder))
        MakeDirectory((const char *)folder);
    Image albedoImage = {0};
    albedoImage.data = albedo;
    albedoImage.width = size;
    albedoImage.height = size;
    albedoImage.mipmaps = 1;
    albedoImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Image ormImage = {0};
    ormImage.data = orm;
    ormImage.width = size;
    ormImage.height = size;
    ormImage.mipmaps = 1;
    ormImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    gX_I8 albedoPath[640], ormPath[640];
    snprintf((char *)albedoPath, sizeof(albedoPath), "%s/%s_albedo.png",
        (const char *)folder, (const char *)name);
    snprintf((char *)ormPath, sizeof(ormPath), "%s/%s_orm.png",
        (const char *)folder, (const char *)name);
    gX_I1 albedoOK = (gX_I1)ExportImage(albedoImage, (const char *)albedoPath);
    gX_I1 ormOK = (gX_I1)ExportImage(ormImage, (const char *)ormPath);
    ok = (albedoOK && ormOK) ? 1 : 0;
    if (outAlbedo && outSize > 0)
        snprintf((char *)outAlbedo, outSize, "%s", (const char *)albedoPath);
    gX_TraceLog(ok ? gX_LOG_INFO : gX_LOG_ERROR,
        "GfxEd: decal bake %s size=%d decals=%d albedo=%s orm=%s",
        ok ? "written" : "failed", size, count, (const char *)albedoPath,
        (const char *)ormPath);
    free(albedo);
    free(orm);
    return ok;
}

//opencode: prefab status + list helpers.
static gX_I0 gfx_prefab_status(const gX_I8 *text, gX_I1 ok) {
    snprintf(s_prefabStatus, sizeof(s_prefabStatus), "%s", text ? text : "");
    s_prefabStatusOK = ok;
}

static gX_I0 gfx_prefab_list_release(gX_I0) {
    if (s_prefabFiles.paths) UnloadDirectoryFiles(s_prefabFiles);
    s_prefabFiles = (FilePathList){0};
    s_prefabListLoaded = 0;
}

static gX_I0 gfx_prefab_list_refresh(gX_I0) {
    gfx_prefab_list_release();
    s_prefabFiles = LoadDirectoryFilesEx("resources/tools/prefabs",
        "FILES*", false);
    s_prefabListLoaded = 1;
}

//opencode: capture the selected model + nearby lights as a prefab file.
static gX_I1 gfx_prefab_capture(gX_RendererGFX *gfx, const gX_I8 *name,
    const gX_I8 *path, gX_F32 radius, gX_I1 useTags)
{
    gX_RendererGFX_Model *anchor = NULL;
    if (useTags) {
        for (gX_U32 mi = 0; mi < gfx->models.amount && mi < 32; mi++)
            if (s_taggedModels & (1u << mi)) {
                anchor = gfx_model_at(gfx, (gX_I32)mi);
                if (anchor) break;
            }
    } else {
        anchor = gfx_model_at(gfx, s_selModel);
    }
    if (!anchor) {
        gfx_prefab_status(useTags
            ? "Tag a model to anchor the prefab" 
            : "Select a model to anchor the prefab", 0);
        return 0;
    }
    if (radius <= 0.0f) radius = 8.0f;
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "schema", json_object_new_int(1));
    json_object_object_add(root, "name", json_object_new_string((const char *)name));
    struct json_object *entries = json_object_new_array();
    gX_I32 modelCount = 0;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, (gX_I32)mi);
        if (!model) continue;
        if (useTags) {
            if (mi >= 32 || !(s_taggedModels & (1u << mi))) continue;
        } else if (model != anchor) {
            continue;
        }
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "kind", json_object_new_string("model"));
        json_object_object_add(entry, "path",
            json_object_new_string((const char *)model->path));
        gfx_camera_json_vec3(entry, "pos", (gX_F32v3){
            model->params.pos.x - anchor->params.pos.x,
            model->params.pos.y - anchor->params.pos.y,
            model->params.pos.z - anchor->params.pos.z});
        gfx_camera_json_vec3(entry, "scale", model->params.scale);
        gfx_camera_json_vec3(entry, "rotation", model->params.rotation);
        json_object_object_add(entry, "usePBR",
            json_object_new_boolean(model->params.usePBR ? 1 : 0));
        json_object_object_add(entry, "metalness",
            json_object_new_double((gX_F64)model->params.metalness));
        json_object_object_add(entry, "roughness",
            json_object_new_double((gX_F64)model->params.roughness));
        json_object_object_add(entry, "enableReflections",
            json_object_new_boolean(model->params.enableReflections ? 1 : 0));
        json_object_object_add(entry, "receiveSSR",
            json_object_new_boolean(model->params.receiveSSR ? 1 : 0));
        json_object_object_add(entry, "planarReflection",
            json_object_new_boolean(model->params.planarReflection ? 1 : 0));
        gfx_material_json_add_color(entry, "albedoTint",
            model->params.albedoTint);
        json_object_array_add(entries, entry);
        modelCount++;
    }
    gX_I32 lightCount = 0;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, (gX_I32)li);
        if (!light) continue;
        if (useTags && (li >= 32 || !(s_taggedLights & (1u << li))))
            continue;
        gX_F32 dx = light->pos.x - anchor->params.pos.x;
        gX_F32 dy = light->pos.y - anchor->params.pos.y;
        gX_F32 dz = light->pos.z - anchor->params.pos.z;
        if (!useTags && dx * dx + dy * dy + dz * dz > radius * radius)
            continue;
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "kind", json_object_new_string("light"));
        json_object_object_add(entry, "type", json_object_new_int(light->type));
        gfx_camera_json_vec3(entry, "pos", (gX_F32v3){dx, dy, dz});
        gfx_camera_json_vec3(entry, "dir", light->dir);
        gfx_material_json_add_color(entry, "color", light->color);
        json_object_object_add(entry, "brightness",
            json_object_new_double((gX_F64)light->brightness));
        json_object_object_add(entry, "attenConst",
            json_object_new_double((gX_F64)light->attenConst));
        json_object_object_add(entry, "attenLinear",
            json_object_new_double((gX_F64)light->attenLinear));
        json_object_object_add(entry, "attenQuad",
            json_object_new_double((gX_F64)light->attenQuad));
        json_object_object_add(entry, "castShadows",
            json_object_new_boolean(light->castShadows ? 1 : 0));
        json_object_object_add(entry, "shadowSize",
            json_object_new_int((gX_I32)light->shadowSize));
        json_object_object_add(entry, "shadowFov",
            json_object_new_double((gX_F64)light->shadowFov));
        json_object_object_add(entry, "shadowFar",
            json_object_new_double((gX_F64)light->shadowFar));
        json_object_array_add(entries, entry);
        lightCount++;
    }
    json_object_object_add(root, "entries", entries);
    gX_I32 decalCount = 0;
    for (gX_U32 di = 0; di < gfx->decals.amount; di++) {
        gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, (gX_I32)di);
        if (!decal) continue;
        if (useTags && (di >= 32 || !(s_taggedDecals & (1u << di))))
            continue;
        gX_F32 dx = decal->pos.x - anchor->params.pos.x;
        gX_F32 dy = decal->pos.y - anchor->params.pos.y;
        gX_F32 dz = decal->pos.z - anchor->params.pos.z;
        if (!useTags && dx * dx + dy * dy + dz * dz > radius * radius)
            continue;
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "kind", json_object_new_string("decal"));
        gfx_camera_json_vec3(entry, "pos", (gX_F32v3){dx, dy, dz});
        gfx_camera_json_vec3(entry, "normal", decal->normal);
        json_object_object_add(entry, "size",
            json_object_new_double((gX_F64)decal->size));
        json_object_object_add(entry, "rotation",
            json_object_new_double((gX_F64)decal->rotation));
        json_object_object_add(entry, "alpha",
            json_object_new_double((gX_F64)decal->alpha));
        json_object_object_add(entry, "snowLayer",
            json_object_new_boolean(decal->snowLayer ? 1 : 0));
        json_object_array_add(entries, entry);
        decalCount++;
    }
    gX_I32 emitterCount = 0;
    for (gX_U32 ei = 0; ei < gfx->particle.emitters.amount; ei++) {
        gX_RendererGFX_Emitter *emitter = gfx_emitter_at(gfx, (gX_I32)ei);
        if (!emitter) continue;
        if (useTags && (ei >= 32 || !(s_taggedEmitters & (1u << ei))))
            continue;
        gX_F32 dx = emitter->pos.x - anchor->params.pos.x;
        gX_F32 dy = emitter->pos.y - anchor->params.pos.y;
        gX_F32 dz = emitter->pos.z - anchor->params.pos.z;
        if (!useTags && dx * dx + dy * dy + dz * dz > radius * radius)
            continue;
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "kind",
            json_object_new_string("emitter"));
        gfx_camera_json_vec3(entry, "pos", (gX_F32v3){dx, dy, dz});
        json_object_object_add(entry, "emitRate",
            json_object_new_double((gX_F64)emitter->emitRate));
        gfx_camera_json_vec3(entry, "baseVel", emitter->baseVel);
        json_object_object_add(entry, "spread",
            json_object_new_double((gX_F64)emitter->spread));
        gfx_prefab_json_add_vec4(entry, "color", emitter->color);
        json_object_object_add(entry, "life",
            json_object_new_double((gX_F64)emitter->life));
        json_object_object_add(entry, "speed",
            json_object_new_double((gX_F64)emitter->speed));
        json_object_object_add(entry, "active",
            json_object_new_boolean(emitter->active ? 1 : 0));
        json_object_array_add(entries, entry);
        emitterCount++;
    }
    gX_I1 ok = json_object_to_file_ext((char *)path, root,
        JSON_C_TO_STRING_PRETTY) == 0;
    json_object_put(root);
    if (!ok) {
        gfx_prefab_status("Prefab write failed", 0);
        return 0;
    }
    gfx_prefab_status("Prefab saved", 1);
    gX_TraceLog(gX_LOG_NOTICE,
        "GfxEd: prefab captured models=%d lights=%d decals=%d emitters=%d "
        "radius=%.2f path=%s",
        modelCount, lightCount, decalCount, emitterCount, (gX_F64)radius,
        path);
    return 1;
}

static gX_F32 gfx_prefab_read_f32(struct json_object *parent, const gX_I8 *key,
    gX_F32 fallback)
{
    struct json_object *member = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &member)) return fallback;
    gX_F64 parsed = json_object_get_double(member);
    return isfinite(parsed) ? (gX_F32)parsed : fallback;
}

static gX_I0 gfx_prefab_json_add_vec4(struct json_object *parent,
    const gX_I8 *key, gX_F32v4 value)
{
    struct json_object *array = json_object_new_array();
    json_object_array_add(array, json_object_new_double((gX_F64)value.x));
    json_object_array_add(array, json_object_new_double((gX_F64)value.y));
    json_object_array_add(array, json_object_new_double((gX_F64)value.z));
    json_object_array_add(array, json_object_new_double((gX_F64)value.w));
    json_object_object_add(parent, key, array);
}

static gX_I0 gfx_prefab_read_vec4(struct json_object *parent,
    const gX_I8 *key, gX_F32v4 *out)
{
    struct json_object *array = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &array)
        || !gfx_material_json_is_type(array, json_type_array)
        || json_object_array_length(array) < 4) return;
    out->x = (gX_F32)json_object_get_double(json_object_array_get_idx(array, 0));
    out->y = (gX_F32)json_object_get_double(json_object_array_get_idx(array, 1));
    out->z = (gX_F32)json_object_get_double(json_object_array_get_idx(array, 2));
    out->w = (gX_F32)json_object_get_double(json_object_array_get_idx(array, 3));
}

static gX_I32 gfx_prefab_read_i32(struct json_object *parent, const gX_I8 *key,
    gX_I32 fallback)
{
    struct json_object *member = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &member)) return fallback;
    return json_object_get_int(member);
}

static gX_I1 gfx_prefab_read_bool(struct json_object *parent, const gX_I8 *key,
    gX_I1 fallback)
{
    struct json_object *member = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &member)) return fallback;
    return json_object_get_boolean(member) ? 1 : 0;
}

static gX_I0 gfx_prefab_read_color(struct json_object *parent, const gX_I8 *key,
    gX_Color *color)
{
    struct json_object *member = NULL;
    if (!parent || !json_object_object_get_ex(parent, key, &member)
        || !gfx_material_json_is_type(member, json_type_array)
        || json_object_array_length(member) != 4) return;
    gX_I32 values[4] = {255, 255, 255, 255};
    for (gX_I32 i = 0; i < 4; i++) {
        struct json_object *item = json_object_array_get_idx(member, (gX_U32)i);
        if (item) values[i] = json_object_get_int(item);
    }
    *color = (gX_Color){(gX_U8)values[0], (gX_U8)values[1],
        (gX_U8)values[2], (gX_U8)values[3]};
}

//opencode: place a prefab at a world position. v1 instantiates models and
//   lights; positions are anchor-relative in the file.
static gX_I32 gfx_prefab_place(gX_RendererGFX *gfx, const gX_I8 *path,
    gX_F32v3 at)
{
    struct json_object *root = json_object_from_file((char *)path);
    if (!root) {
        gfx_prefab_status("Prefab load failed", 0);
        return -1;
    }
    struct json_object *entries = NULL;
    if (!json_object_object_get_ex(root, "entries", &entries)
        || !gfx_material_json_is_type(entries, json_type_array)) {
        json_object_put(root);
        gfx_prefab_status("Prefab has no entries", 0);
        return -1;
    }
    gX_I32 placedModels = 0;
    gX_I32 placedLights = 0;
    gX_I32 placedDecals = 0;
    gX_I32 placedEmitters = 0;
    gX_U32 count = json_object_array_length(entries);
    for (gX_U32 ei = 0; ei < count; ei++) {
        struct json_object *entry = json_object_array_get_idx(entries, ei);
        if (!entry || !gfx_material_json_is_type(entry, json_type_object)) continue;
        struct json_object *kindMember = NULL;
        const gX_I8 *kind = "";
        if (json_object_object_get_ex(entry, "kind", &kindMember))
            kind = (const gX_I8 *)json_object_get_string(kindMember);
        gX_F32v3 offset = {0.0f, 0.0f, 0.0f};
        gfx_camera_json_read_vec3(entry, "pos", &offset);
        if (strcmp((const char *)kind, "model") == 0) {
            struct json_object *pathMember = NULL;
            if (!json_object_object_get_ex(entry, "path", &pathMember)) continue;
            gX_I32 index = gX_RendererGFX_Model_Add(gfx,
                (const gX_I8 *)json_object_get_string(pathMember));
            gX_RendererGFX_Model *model = gfx_model_at(gfx, index);
            if (!model) continue;
            model->params.pos = (gX_F32v3){at.x + offset.x, at.y + offset.y,
                at.z + offset.z};
            gfx_camera_json_read_vec3(entry, "scale", &model->params.scale);
            gfx_camera_json_read_vec3(entry, "rotation", &model->params.rotation);
            model->params.usePBR = gfx_prefab_read_bool(entry, "usePBR", 1);
            model->params.metalness = gfx_prefab_read_f32(entry, "metalness", 0.0f);
            model->params.roughness = gfx_prefab_read_f32(entry, "roughness", 1.0f);
            model->params.enableReflections =
                gfx_prefab_read_bool(entry, "enableReflections", 0);
            model->params.receiveSSR = gfx_prefab_read_bool(entry, "receiveSSR", 0);
            model->params.planarReflection =
                gfx_prefab_read_bool(entry, "planarReflection", 0);
            gfx_prefab_read_color(entry, "albedoTint", &model->params.albedoTint);
            if (placedModels == 0)
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: prefab model placed at=%.2f,%.2f,%.2f path=%s",
                    (gX_F64)model->params.pos.x, (gX_F64)model->params.pos.y,
                    (gX_F64)model->params.pos.z, model->path);
            placedModels++;
        } else if (strcmp((const char *)kind, "light") == 0) {
            gX_I32 index = gX_RendererGFX_Light_Add(gfx);
            gX_RendererGFX_Light *light = gfx_light_at(gfx, index);
            if (!light) continue;
            light->type = gfx_prefab_read_i32(entry, "type", light->type);
            light->pos = (gX_F32v3){at.x + offset.x, at.y + offset.y,
                at.z + offset.z};
            gfx_camera_json_read_vec3(entry, "dir", &light->dir);
            gfx_prefab_read_color(entry, "color", &light->color);
            light->brightness = gfx_prefab_read_f32(entry, "brightness",
                light->brightness);
            light->attenConst = gfx_prefab_read_f32(entry, "attenConst",
                light->attenConst);
            light->attenLinear = gfx_prefab_read_f32(entry, "attenLinear",
                light->attenLinear);
            light->attenQuad = gfx_prefab_read_f32(entry, "attenQuad",
                light->attenQuad);
            light->castShadows = gfx_prefab_read_bool(entry, "castShadows",
                light->castShadows);
            light->shadowSize = (gX_U32)gfx_prefab_read_i32(entry, "shadowSize",
                (gX_I32)light->shadowSize);
            light->shadowFov = gfx_prefab_read_f32(entry, "shadowFov",
                light->shadowFov);
            light->shadowFar = gfx_prefab_read_f32(entry, "shadowFar",
                light->shadowFar);
            if (placedLights == 0)
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: prefab light placed at=%.2f,%.2f,%.2f type=%d",
                    (gX_F64)light->pos.x, (gX_F64)light->pos.y,
                    (gX_F64)light->pos.z, light->type);
            placedLights++;
        } else if (strcmp((const char *)kind, "decal") == 0) {
            gX_I32 index = gX_RendererGFX_Decal_Add(gfx,
                (gX_F32v3){at.x + offset.x, at.y + offset.y,
                    at.z + offset.z},
                (gX_F32v3){0.0f, 1.0f, 0.0f}, 1.0f, -1, -1.0);
            gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, index);
            if (!decal) continue;
            gfx_camera_json_read_vec3(entry, "normal", &decal->normal);
            decal->size = gfx_prefab_read_f32(entry, "size", 1.0f);
            decal->rotation = gfx_prefab_read_f32(entry, "rotation", 0.0f);
            decal->alpha = gfx_prefab_read_f32(entry, "alpha", 1.0f);
            decal->snowLayer = gfx_prefab_read_bool(entry, "snowLayer", 0);
            if (placedDecals == 0)
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: prefab decal placed at=%.2f,%.2f,%.2f size=%.2f",
                    (gX_F64)decal->pos.x, (gX_F64)decal->pos.y,
                    (gX_F64)decal->pos.z, (gX_F64)decal->size);
            placedDecals++;
        } else if (strcmp((const char *)kind, "emitter") == 0) {
            gX_I32 index = gX_RendererGFX_Emitter_Add(gfx);
            gX_RendererGFX_Emitter *emitter = gfx_emitter_at(gfx, index);
            if (!emitter) continue;
            emitter->pos = (gX_F32v3){at.x + offset.x, at.y + offset.y,
                at.z + offset.z};
            emitter->emitRate = gfx_prefab_read_f32(entry, "emitRate",
                emitter->emitRate);
            gfx_camera_json_read_vec3(entry, "baseVel", &emitter->baseVel);
            emitter->spread = gfx_prefab_read_f32(entry, "spread",
                emitter->spread);
            gfx_prefab_read_vec4(entry, "color", &emitter->color);
            emitter->life = gfx_prefab_read_f32(entry, "life", emitter->life);
            emitter->speed = gfx_prefab_read_f32(entry, "speed",
                emitter->speed);
            emitter->active = gfx_prefab_read_bool(entry, "active",
                emitter->active);
            if (placedEmitters == 0)
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: prefab emitter placed at=%.2f,%.2f,%.2f rate=%.1f",
                    (gX_F64)emitter->pos.x, (gX_F64)emitter->pos.y,
                    (gX_F64)emitter->pos.z, (gX_F64)emitter->emitRate);
            placedEmitters++;
        }
    }
    json_object_put(root);
    gfx_prefab_status("Prefab placed", 1);
    gX_TraceLog(gX_LOG_NOTICE,
        "GfxEd: prefab placed models=%d lights=%d decals=%d emitters=%d "
        "at=%.2f,%.2f,%.2f path=%s",
        placedModels, placedLights, placedDecals, placedEmitters,
        (gX_F64)at.x, (gX_F64)at.y, (gX_F64)at.z, path);
    return placedModels + placedLights + placedDecals + placedEmitters;
}

static gX_I0 gfx_preset_save_commit(gX_RendererGFX *gfx) {
    if (!s_presetPath[0]) {
        gfx_preset_set_status("Save failed: enter a preset path", 0);
        GX_ERROR_SET(GX_ERR_INVALID, "preset save without a path");
        return;
    }
    if (gX_RendererGFX_Preset_Save_New(gfx, s_presetPath) == 0) {
        gfx_cameras_preset_write(s_presetPath);
        gfx_preset_set_status("Preset saved", 1);
    } else {
        gfx_preset_set_status("Preset save failed", 0);
        GX_ERROR_SET(GX_ERR_IO, "preset save failed: %s", s_presetPath);
    }
}

//opencode: protect existing scene files from an accidental Ctrl+S or button click.
static gX_I0 gfx_preset_save(gX_RendererGFX *gfx) {
    if (!s_presetPath[0]) {
        gfx_preset_set_status("Save failed: enter a preset path", 0);
        return;
    }
    if (gfx_preset_file_exists(s_presetPath)) {
        s_presetOverwritePending = 1;
        gfx_preset_set_status("Confirm overwrite before saving this preset", 0);
        return;
    }
    gfx_preset_save_commit(gfx);
}

//opencode: reset transient editor state after a preset replaces the live
//   model/light heaps, including the Sky Lab's derived sun sliders.
static gX_I0 gfx_preset_load(gX_RendererGFX *gfx) {
    if (!s_presetPath[0]) {
        gfx_preset_set_status("Load failed: enter a preset path", 0);
        return;
    }
    if (!gfx_preset_file_exists(s_presetPath)) {
        gfx_preset_set_status("Load failed: file not found", 0);
        GX_ERROR_SET(GX_ERR_NOT_FOUND, "preset not found: %s", s_presetPath);
        return;
    }
    if (gX_RendererGFX_Preset_Load_New(gfx, s_presetPath) == 0) {
        gfx_cameras_preset_read(s_presetPath);
        s_selLight = -1;
        s_selModel = -1;
        s_draggingLight = 0;
        s_draggingModel = 0;
        s_dragLight = -1;
        s_dragModel = -1;
        s_modelRevertModel = -1;
        s_modelRevertValid = 0;
        gfx_model_history_reset();
        gfx_model_snapshots_clear();
        s_sunInit = 0;
        gfx_preset_set_status("Preset loaded", 1);
    } else {
        gfx_preset_set_status("Preset load failed", 0);
        GX_ERROR_SET(GX_ERR_PARSE, "preset load failed: %s", s_presetPath);
    }
}

//opencode: v0.6.10 — shared preset bar; load is confirmed because it replaces
//   all live models, lights, and textures.
static gX_I0 gfx_preset_panel(gX_RendererGFX *gfx) {
    ImGui::SeparatorText("Preset");
    if (s_presetOverwritePending) {
        ImGui::OpenPopup("Confirm GFX preset overwrite");
        s_presetOverwritePending = 0;
    }
    //ai: leave real room for the scaled Browse button instead of clipping it.
    gX_F32 browseWidth = ImGui::CalcTextSize("Browse").x
        + ImGui::GetStyle().FramePadding.x * 2.0f
        + ImGui::GetStyle().ItemSpacing.x + 6.0f;
    ImGui::SetNextItemWidth(-browseWidth);
    ImGui::InputTextWithHint("##presetPath", "preset path",
        s_presetPath, sizeof(s_presetPath));
    ImGui::SameLine();
    gfx_preset_browser_popup();
    if (s_presetConfirmPending) {
        ImGui::OpenPopup("Confirm GFX preset load");
        s_presetConfirmPending = 0;
    }
    //opencode: one-click shipped presets through the same confirmed-load
    //   flow as a typed path (load replaces all live content).
    if (ImGui::Button("Default")) {
        snprintf(s_presetPath, sizeof(s_presetPath), "%s", "resources/tools/gfx_editor.json");
        ImGui::OpenPopup("Confirm GFX preset load");
    }
    ImGui::SameLine();
    if (ImGui::Button("Y2K")) {
        snprintf(s_presetPath, sizeof(s_presetPath), "%s", "resources/tools/gfx_y2k.json");
        ImGui::OpenPopup("Confirm GFX preset load");
    }
    ImGui::SameLine();
    if (gX_Tools_ImGui_GradientButton("Save preset", ImVec2(0.0f, 0.0f),
            IM_COL32(178, 120, 54, 255), IM_COL32(96, 58, 88, 255),
            IM_COL32(255, 244, 224, 255)))
        gfx_preset_save(gfx);
    if (ImGui::Button("Load preset")) ImGui::OpenPopup("Confirm GFX preset load");
    if (s_presetStatus[0]) {
        ImVec4 color = s_presetStatusOK
            ? ImVec4(0.45f, 0.90f, 0.60f, 1.0f)
            : ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        ImGui::TextColored(color, "%s", s_presetStatus);
    }
    if (ImGui::BeginPopupModal("Confirm GFX preset load", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("This replaces the live GFX scene and renderer settings.");
        ImGui::Text("File: %s", s_presetPath);
        if (ImGui::Button("Load and replace")) {
            gfx_preset_load(gfx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Confirm GFX preset overwrite", NULL,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("This replaces the existing preset file with the live scene.");
        ImGui::Text("File: %s", s_presetPath);
        if (ImGui::Button("Overwrite preset")) {
            gfx_preset_save_commit(gfx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

//opencode: v0.6.11 — safe heap lookup for the model inspector. Preset loads
//   and model removals can invalidate the previous selection between frames.
static gX_RendererGFX_Model *gfx_model_at(gX_RendererGFX *gfx, gX_I32 index) {
    if (!gfx || !gfx->models.data || !gfx->models.meta
        || index < 0 || (gX_U32)index >= gfx->models.amount) return 0;
    if (!gfx->models.meta[index].USED) return 0;
    return &((gX_RendererGFX_Model *)gfx->models.data)[index];
}

//opencode: checked heap access for editor-managed texture assets.
static gX_RendererGFX_Texture *gfx_texture_at(gX_RendererGFX *gfx, gX_I32 index) {
    if (!gfx || !gfx->textures.data || !gfx->textures.meta
        || index < 0 || (gX_U32)index >= gfx->textures.amount) return 0;
    if (!gfx->textures.meta[index].USED) return 0;
    return &((gX_RendererGFX_Texture *)gfx->textures.data)[index];
}

//opencode: shared basename helper is defined with the scene model browser.
static const gX_I8 *gfx_model_basename(const gX_I8 *path);
static gX_I0 gfx_asset_browser_open(gX_RendererGFX *gfx, gX_I32 kind,
    gX_I32 targetType, gX_I32 targetIndex, gX_I32 targetSlot,
    gX_I32 targetPurpose, gX_I32 currentTexture);
static gX_RendererGFX_Decal *gfx_decal_at(gX_RendererGFX *gfx, gX_I32 index);

//opencode: reuse a loaded preset texture when the asset path already exists.
static gX_I32 gfx_texture_find_or_add(gX_RendererGFX *gfx, const gX_I8 *path) {
    if (!gfx || !path || !path[0]
        || strlen(path) >= sizeof(((gX_RendererGFX_Texture *)0)->wrapper.path)
        || !gfx_preset_file_exists(path)) return -1;
    for (gX_U32 ti = 0; ti < gfx->textures.amount; ti++) {
        gX_RendererGFX_Texture *texture = gfx_texture_at(gfx, (gX_I32)ti);
        if (texture && strcmp(texture->wrapper.path, path) == 0) return (gX_I32)ti;
    }
    return gX_RendererGFX_Texture_Add(gfx, path);
}

//opencode: pick a texture heap entry for model maps, decals, and lightmaps.
static gX_I0 gfx_texture_combo(const gX_I8 *label, gX_RendererGFX *gfx,
    gX_I32 *selected, gX_I32 targetType, gX_I32 targetIndex,
    gX_I32 targetSlot, gX_I32 targetPurpose) {
    gX_RendererGFX_Texture *current = gfx_texture_at(gfx, *selected);
    const gX_I8 *preview = current ? gfx_model_basename(current->wrapper.path) : "None";
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("None", *selected < 0)) *selected = -1;
        for (gX_U32 ti = 0; ti < gfx->textures.amount; ti++) {
            gX_RendererGFX_Texture *texture = gfx_texture_at(gfx, (gX_I32)ti);
            if (!texture) continue;
            gX_I8 name[192];
            snprintf(name, sizeof(name), "%u: %s", ti, gfx_model_basename(texture->wrapper.path));
            if (ImGui::Selectable(name, *selected == (gX_I32)ti)) *selected = (gX_I32)ti;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse files..."))
        gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES, targetType,
            targetIndex, targetSlot, targetPurpose, *selected);
}

//opencode: material-map injection uses the fixed raylib map slots 0-7.
static const gX_I8 *s_materialMapNames[] = {
    "Albedo", "Metalness", "Normal", "Roughness", "Occlusion", "Emission", "Height", "Cubemap"
};

static gX_I0 gfx_model_map_mark_present(gX_RendererGFX_Model *model, gX_I32 purpose) {
    switch (purpose) {
        case gX_MATERIAL_MAP_ALBEDO: model->params.hasAlbedoMap = 1; break;
        case gX_MATERIAL_MAP_METALNESS: model->params.hasMetalnessMap = 1; break;
        case gX_MATERIAL_MAP_NORMAL: model->params.hasNormalMap = 1; break;
        case gX_MATERIAL_MAP_ROUGHNESS: model->params.hasRoughnessMap = 1; break;
        case gX_MATERIAL_MAP_OCCLUSION: model->params.hasAoMap = 1; break;
        case gX_MATERIAL_MAP_EMISSION: model->params.hasEmissiveMap = 1; break;
        case gX_MATERIAL_MAP_HEIGHT: model->params.hasHeightMap = 1; break;
        default: break;
    }
}

//opencode: model reload reapplies per-material overrides after their edits.
static gX_I0 gfx_model_reload(gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    gX_RendererGFX_Model_Unload(gfx, model);
    model->loadAsap = 1;
}

static gX_I0 gfx_model_edit_capture(gX_GFX_Model_Edit_State *state,
    gX_RendererGFX_Model *model) {
    //opencode: memcpy the params so struct padding is copied verbatim and the
    //   history memcmp stays stable; a field-wise assignment can leave stack
    //   padding indeterminate and make every frame look like an edit.
    memset(state, 0, sizeof(*state));
    memcpy(&state->params, &model->params, sizeof(state->params));
    state->useTextures = model->useTextures;
}

static gX_I0 gfx_model_edit_apply(gX_RendererGFX_Model *model,
    const gX_GFX_Model_Edit_State *state) {
    model->params = state->params;
    model->useTextures = state->useTextures;
}

static gX_I0 gfx_renderer_edit_capture(gX_GFX_Renderer_Edit_State *state,
    gX_RendererGFX *gfx) {
    state->gfxState = gfx->state;
    state->usePP = renderer.state.usePP;
    state->useCompositor = renderer.state.useCompositor;
    state->normalBufferEnabled = gfx->ssr.normalBufferEnabled;
    state->ssrMaxSteps = gfx->ssr.maxSteps;
    state->ssrStepSize = gfx->ssr.stepSize;
    state->ssrMaxDistance = gfx->ssr.maxDistance;
    state->ssrDepthBias = gfx->ssr.depthBias;
    state->ssrOpacity = gfx->ssr.opacity;
    state->ssrRoughness = gfx->ssr.roughness;
    state->ssrMaxLod = gfx->ssr.maxLod;
    state->ssrEdgeFade = gfx->ssr.edgeFade;
    state->ssrMaxBounces = gfx->ssr.maxBounces;
    state->ssrBounceAtten = gfx->ssr.bounceAtten;
    state->ssrMaxDepthDelta = gfx->ssr.maxDepthDelta;
    state->ssrWorldGapThreshold = gfx->ssr.worldGapThreshold;
    state->ssrFinalDepthThreshold = gfx->ssr.finalDepthThreshold;
    state->ssrHitFacingThreshold = gfx->ssr.hitFacingThreshold;
    state->reflectionBias = gfx->reflection.bias;
}

static gX_I0 gfx_renderer_edit_apply(gX_RendererGFX *gfx,
    const gX_GFX_Renderer_Edit_State *state) {
    gfx->state = state->gfxState;
    renderer.state.usePP = state->usePP;
    renderer.state.useCompositor = state->useCompositor;
    gfx->ssr.maxSteps = state->ssrMaxSteps;
    gfx->ssr.stepSize = state->ssrStepSize;
    gfx->ssr.maxDistance = state->ssrMaxDistance;
    gfx->ssr.depthBias = state->ssrDepthBias;
    gfx->ssr.opacity = state->ssrOpacity;
    gfx->ssr.roughness = state->ssrRoughness;
    gfx->ssr.maxLod = state->ssrMaxLod;
    gfx->ssr.edgeFade = state->ssrEdgeFade;
    gfx->ssr.maxBounces = state->ssrMaxBounces;
    gfx->ssr.bounceAtten = state->ssrBounceAtten;
    gfx->ssr.maxDepthDelta = state->ssrMaxDepthDelta;
    gfx->ssr.worldGapThreshold = state->ssrWorldGapThreshold;
    gfx->ssr.finalDepthThreshold = state->ssrFinalDepthThreshold;
    gfx->ssr.hitFacingThreshold = state->ssrHitFacingThreshold;
    gfx->reflection.bias = state->reflectionBias;
    gfx_ssr_normal_buffer_set(gfx, state->normalBufferEnabled);
}

static gX_I0 gfx_model_history_push(gX_GFX_Model_Edit_State *stack,
    gX_I32 *count, const gX_GFX_Model_Edit_State *state) {
    gfx_model_history_ensure();
    if (*count == GX_GFX_HISTORY_CAP) {
        memmove(stack, stack + 1, sizeof(stack[0]) * (GX_GFX_HISTORY_CAP - 1));
        *count = GX_GFX_HISTORY_CAP - 1;
    }
    memcpy(&stack[*count], state, sizeof(stack[0]));
    (*count)++;
}

static gX_I0 gfx_model_history_observe(gX_I32 index, gX_RendererGFX_Model *model) {
    gfx_model_history_ensure();
    if (!model) {
        gfx_model_history_reset();
        return;
    }
    //opencode: a viewport drag coalesces into one entry pushed at release.
    if (s_draggingModel || s_draggingHandle) return;
    //opencode: an active motion path drives pos every frame; keep runtime
    //   motion out of the history and re-baseline when it stops.
    gX_I1 driving = model->params.orbitEnabled && !model->params.orbitPaused;
    if (driving) {
        s_modelOrbitDriving = 1;
        return;
    }
    gX_GFX_Model_Edit_State current;
    gfx_model_edit_capture(&current, model);
    if (s_modelOrbitDriving) {
        s_modelOrbitDriving = 0;
        memcpy(&s_modelHistoryCurrent, &current, sizeof(current));
        s_modelHistoryModel = index;
        s_modelHistoryValid = 1;
        return;
    }
    if (!s_modelHistoryValid || s_modelHistoryModel != index) {
        s_modelUndoCount = 0;
        s_modelRedoCount = 0;
        memcpy(&s_modelHistoryCurrent, &current, sizeof(current));
        s_modelHistoryModel = index;
        s_modelHistoryValid = 1;
        return;
    }
    if (memcmp(&current, &s_modelHistoryCurrent, sizeof(current)) != 0) {
        gfx_model_history_push(s_modelUndo, &s_modelUndoCount, &s_modelHistoryCurrent);
        memcpy(&s_modelHistoryCurrent, &current, sizeof(current));
        s_modelRedoCount = 0;
    }
}

static gX_I0 gfx_model_history_apply(gX_RendererGFX_Model *model,
    const gX_GFX_Model_Edit_State *state) {
    if (s_modelHistoryValid) gfx_model_history_push(s_modelUndo,
        &s_modelUndoCount, &s_modelHistoryCurrent);
    gfx_model_edit_apply(model, state);
    memcpy(&s_modelHistoryCurrent, state, sizeof(*state));
    s_modelRedoCount = 0;
    s_modelHistoryValid = 1;
}

//opencode: one directional mover keeps a single copy of the apply path in the
//   Release binary; separate undo/redo wrappers duplicated it at -O2.
static gX_I0 gfx_model_history_move(gX_RendererGFX_Model *model, gX_I1 redo) {
    gfx_model_history_ensure();
    gX_GFX_Model_Edit_State *from = redo ? s_modelRedo : s_modelUndo;
    gX_GFX_Model_Edit_State *to = redo ? s_modelUndo : s_modelRedo;
    gX_I32 *fromCount = redo ? &s_modelRedoCount : &s_modelUndoCount;
    gX_I32 *toCount = redo ? &s_modelUndoCount : &s_modelRedoCount;
    if (!model || *fromCount <= 0) return;
    gfx_model_history_push(to, toCount, &s_modelHistoryCurrent);
    (*fromCount)--;
    gfx_model_edit_apply(model, &from[*fromCount]);
    memcpy(&s_modelHistoryCurrent, &from[*fromCount],
        sizeof(s_modelHistoryCurrent));
}

//opencode: shared undo/redo entry point for the History panel and Ctrl+Z/Y.
static gX_I0 gfx_model_history_step(gX_I32 index, gX_RendererGFX_Model *model,
    gX_I1 redo) {
    if (!model) return;
    gX_I32 before = redo ? s_modelRedoCount : s_modelUndoCount;
    gfx_model_history_move(model, redo);
    if ((redo ? s_modelRedoCount : s_modelUndoCount) == before) return;
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: history %s model=%d pos=(%.3f,%.3f,%.3f) undo=%d redo=%d",
        redo ? "redo" : "undo", index,
        model->params.pos.x, model->params.pos.y, model->params.pos.z,
        s_modelUndoCount, s_modelRedoCount);
}

static gX_I0 gfx_model_history_reset(gX_I0) {
    s_modelUndoCount = 0;
    s_modelRedoCount = 0;
    s_modelHistoryModel = -1;
    s_modelHistoryValid = 0;
    s_modelOrbitDriving = 0;
}

//opencode: authored-light capture/apply. Field-wise copies keep struct padding
//   zeroed so the history memcmp stays stable.
static gX_I0 gfx_light_edit_capture(gX_GFX_Light_Edit_State *state,
    gX_RendererGFX_Light *light) {
    memset(state, 0, sizeof(*state));
    state->type = light->type;
    state->pos = light->pos;
    state->dir = light->dir;
    state->fov = light->fov;
    state->brightness = light->brightness;
    state->diffuseTint = light->diffuseTint;
    state->specularTint = light->specularTint;
    state->diffuseFactor = light->diffuseFactor;
    state->specularFactor = light->specularFactor;
    state->color = light->color;
    state->attenConst = light->attenConst;
    state->attenLinear = light->attenLinear;
    state->attenQuad = light->attenQuad;
    state->castShadows = light->castShadows;
    state->shadowSize = light->shadowSize;
    state->shadowFov = light->shadowFov;
    state->shadowFar = light->shadowFar;
    state->shadowRange = light->shadowRange;
    state->orbitEnabled = light->orbitEnabled;
    state->orbitPaused = light->orbitPaused;
    state->orbitLoop = light->orbitLoop;
    state->orbitPlane = light->orbitPlane;
    state->orbitCenter = light->orbitCenter;
    state->orbitRadius = light->orbitRadius;
    state->orbitSpeed = light->orbitSpeed;
    state->orbitPhase = light->orbitPhase;
    state->pathMode = light->pathMode;
    state->pathWaypointCount = light->pathWaypointCount;
    memcpy(state->pathWaypoints, light->pathWaypoints,
        sizeof(state->pathWaypoints));
    state->pathPingPong = light->pathPingPong;
    state->pathSmooth = light->pathSmooth;
    state->pathBezier = light->pathBezier;
    memcpy(state->pathTangents, light->pathTangents,
        sizeof(state->pathTangents));
    memcpy(state->pathTangentsIn, light->pathTangentsIn,
        sizeof(state->pathTangentsIn));
    state->pathHandlesIndependent = light->pathHandlesIndependent;
}

static gX_I0 gfx_light_edit_apply(gX_RendererGFX_Light *light,
    const gX_GFX_Light_Edit_State *state) {
    light->type = state->type;
    light->pos = state->pos;
    light->dir = state->dir;
    light->fov = state->fov;
    light->brightness = state->brightness;
    light->diffuseTint = state->diffuseTint;
    light->specularTint = state->specularTint;
    light->diffuseFactor = state->diffuseFactor;
    light->specularFactor = state->specularFactor;
    light->color = state->color;
    light->attenConst = state->attenConst;
    light->attenLinear = state->attenLinear;
    light->attenQuad = state->attenQuad;
    light->castShadows = state->castShadows;
    light->shadowSize = state->shadowSize;
    light->shadowFov = state->shadowFov;
    light->shadowFar = state->shadowFar;
    light->shadowRange = state->shadowRange;
    light->orbitEnabled = state->orbitEnabled;
    light->orbitPaused = state->orbitPaused;
    light->orbitLoop = state->orbitLoop;
    light->orbitPlane = state->orbitPlane;
    light->orbitCenter = state->orbitCenter;
    light->orbitRadius = state->orbitRadius;
    light->orbitSpeed = state->orbitSpeed;
    light->orbitPhase = state->orbitPhase;
    light->pathMode = state->pathMode;
    light->pathWaypointCount = state->pathWaypointCount;
    memcpy(light->pathWaypoints, state->pathWaypoints,
        sizeof(light->pathWaypoints));
    light->pathPingPong = state->pathPingPong;
    light->pathSmooth = state->pathSmooth;
    light->pathBezier = state->pathBezier;
    memcpy(light->pathTangents, state->pathTangents,
        sizeof(light->pathTangents));
    memcpy(light->pathTangentsIn, state->pathTangentsIn,
        sizeof(light->pathTangentsIn));
    light->pathHandlesIndependent = state->pathHandlesIndependent;
}

static gX_I0 gfx_light_history_push(gX_GFX_Light_Edit_State *stack,
    gX_I32 *count, const gX_GFX_Light_Edit_State *state) {
    gfx_model_history_ensure();
    if (*count == GX_GFX_HISTORY_CAP) {
        memmove(stack, stack + 1, sizeof(stack[0]) * (GX_GFX_HISTORY_CAP - 1));
        *count = GX_GFX_HISTORY_CAP - 1;
    }
    memcpy(&stack[*count], state, sizeof(stack[0]));
    (*count)++;
}

static gX_I0 gfx_light_history_reset(gX_I0) {
    s_lightUndoCount = 0;
    s_lightRedoCount = 0;
    s_lightHistoryLight = -1;
    s_lightHistoryValid = 0;
    s_lightOrbitDriving = 0;
}

static gX_I0 gfx_light_history_observe(gX_I32 index, gX_RendererGFX_Light *light) {
    gfx_model_history_ensure();
    if (!light) {
        gfx_light_history_reset();
        return;
    }
    //opencode: a drag coalesces into one entry pushed at release.
    if (s_draggingLight || s_draggingHandle) return;
    //opencode: an active orbit path drives pos every frame; keep runtime motion
    //   out of the history (authored orbit values are still observed).
    gX_I1 driving = light->orbitEnabled && !light->orbitPaused && light->type == 1;
    if (driving) {
        s_lightOrbitDriving = 1;
        return;
    }
    gX_GFX_Light_Edit_State current;
    gfx_light_edit_capture(&current, light);
    if (s_lightOrbitDriving) {
        s_lightOrbitDriving = 0;
        memcpy(&s_lightHistoryCurrent, &current, sizeof(current));
        s_lightHistoryLight = index;
        s_lightHistoryValid = 1;
        return;
    }
    if (!s_lightHistoryValid || s_lightHistoryLight != index) {
        s_lightUndoCount = 0;
        s_lightRedoCount = 0;
        memcpy(&s_lightHistoryCurrent, &current, sizeof(current));
        s_lightHistoryLight = index;
        s_lightHistoryValid = 1;
        return;
    }
    if (memcmp(&current, &s_lightHistoryCurrent, sizeof(current)) != 0) {
        gfx_light_history_push(s_lightUndo, &s_lightUndoCount, &s_lightHistoryCurrent);
        memcpy(&s_lightHistoryCurrent, &current, sizeof(current));
        s_lightRedoCount = 0;
    }
}

//opencode: one directional mover keeps a single copy of the apply path.
static gX_I0 gfx_light_history_move(gX_RendererGFX_Light *light, gX_I1 redo) {
    gfx_model_history_ensure();
    gX_GFX_Light_Edit_State *from = redo ? s_lightRedo : s_lightUndo;
    gX_GFX_Light_Edit_State *to = redo ? s_lightUndo : s_lightRedo;
    gX_I32 *fromCount = redo ? &s_lightRedoCount : &s_lightUndoCount;
    gX_I32 *toCount = redo ? &s_lightUndoCount : &s_lightRedoCount;
    if (!light || *fromCount <= 0) return;
    gfx_light_history_push(to, toCount, &s_lightHistoryCurrent);
    (*fromCount)--;
    gfx_light_edit_apply(light, &from[*fromCount]);
    memcpy(&s_lightHistoryCurrent, &from[*fromCount],
        sizeof(s_lightHistoryCurrent));
}

//opencode: shared light undo/redo entry point for the panel and Ctrl+Z/Y.
static gX_I0 gfx_light_history_step(gX_I32 index, gX_RendererGFX_Light *light,
    gX_I1 redo) {
    if (!light) return;
    gX_I32 before = redo ? s_lightRedoCount : s_lightUndoCount;
    gfx_light_history_move(light, redo);
    if ((redo ? s_lightRedoCount : s_lightUndoCount) == before) return;
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: light history %s light=%d pos=(%.3f,%.3f,%.3f) undo=%d redo=%d",
        redo ? "redo" : "undo", index,
        light->pos.x, light->pos.y, light->pos.z,
        s_lightUndoCount, s_lightRedoCount);
}

static gX_I0 gfx_model_snapshots_clear(gX_I0) {
    gfx_model_history_ensure();
    for (gX_I32 i = 0; i < GX_GFX_SNAPSHOT_CAP; i++)
        s_modelSnapshots[i].valid = 0;
}

static gX_I0 gfx_model_snapshot_save(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_I32 index) {
    gfx_model_history_ensure();
    gX_GFX_Model_Snapshot *snapshot = &s_modelSnapshots[s_modelSnapshotSlot];
    gfx_model_edit_capture(&snapshot->model, model);
    gfx_renderer_edit_capture(&snapshot->renderer, gfx);
    snapshot->camera = camera.camera3D;
    snapshot->modelIndex = index;
    snapshot->valid = 1;
    if (!s_modelSnapshotName[0]) strcpy(s_modelSnapshotName, "snapshot");
    strncpy(snapshot->name, s_modelSnapshotName, sizeof(snapshot->name) - 1);
    snapshot->name[sizeof(snapshot->name) - 1] = 0;
}

static gX_I0 gfx_model_snapshot_restore(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_I32 index) {
    gfx_model_history_ensure();
    gX_GFX_Model_Snapshot *snapshot = &s_modelSnapshots[s_modelSnapshotSlot];
    if (!snapshot->valid || snapshot->modelIndex != index) return;
    gfx_model_history_apply(model, &snapshot->model);
    gfx_renderer_edit_apply(gfx, &snapshot->renderer);
    camera.camera3D = snapshot->camera;
}

static gX_I0 gfx_model_history_panel(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_I32 index) {
    if (!ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::Button("Undo")) gfx_model_history_step(index, model, 0);
    ImGui::SameLine();
    if (ImGui::Button("Redo")) gfx_model_history_step(index, model, 1);
    ImGui::TextDisabled("undo=%d redo=%d · Ctrl+Z / Ctrl+Y", s_modelUndoCount, s_modelRedoCount);
    gX_GFX_Model_Snapshot *snapshot = &s_modelSnapshots[s_modelSnapshotSlot];
    if (ImGui::CollapsingHeader("Snapshots")) {
        ImGui::SliderInt("snapshot slot", &s_modelSnapshotSlot, 0, GX_GFX_SNAPSHOT_CAP - 1);
        ImGui::InputText("snapshot name", s_modelSnapshotName, sizeof(s_modelSnapshotName));
        if (ImGui::Button("Save snapshot")) gfx_model_snapshot_save(gfx, model, index);
        ImGui::SameLine();
        if (ImGui::Button("Restore snapshot") && snapshot->valid
            && snapshot->modelIndex == index) gfx_model_snapshot_restore(gfx, model, index);
        if (snapshot->valid) ImGui::TextDisabled("slot: %s | model + camera + renderer", snapshot->name);
    }
}

//opencode: capture the selected model's editable state before authoring edits.
static gX_I0 gfx_model_revert_capture(gX_RendererGFX *gfx, gX_I32 index) {
    gX_RendererGFX_Model *model = gfx_model_at(gfx, index);
    if (!model) {
        s_modelRevertModel = -1;
        s_modelRevertValid = 0;
        return;
    }
    s_modelRevertModel = index;
    s_modelRevertParams = model->params;
    s_modelRevertUseTextures = model->useTextures;
    s_modelRevertValid = 1;
}

//opencode: restore renderer defaults without unloading or reloading the asset.
static gX_I0 gfx_model_reset_defaults(gX_RendererGFX_Model *model) {
    gX_RendererGFX_Model defaults = {};
    gX_RendererGFX_Model_Init(&defaults, model->path);
    model->params = defaults.params;
    model->useTextures = defaults.useTextures;
}

static gX_RendererGFX_Light *gfx_light_at(gX_RendererGFX *gfx, gX_I32 index) {
    if (!gfx || !gfx->lights.data || !gfx->lights.meta
        || index < 0 || (gX_U32)index >= gfx->lights.amount) return 0;
    if (!gfx->lights.meta[index].USED) return 0;
    return &((gX_RendererGFX_Light *)gfx->lights.data)[index];
}

//opencode: keep the model list useful at narrow dock widths by displaying the
//   final path component while the full path remains in the inspector.
static const gX_I8 *gfx_model_basename(const gX_I8 *path) {
    const gX_I8 *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

//opencode: file lists are owned by raylib and released when the index refreshes.
static gX_I0 gfx_assets_sources_release(gX_I0) {
    for (gX_U32 si = 0; si < GX_GFX_ASSET_SOURCE_CAP; si++) {
        if (s_modelAssetSources[si].paths) UnloadDirectoryFiles(s_modelAssetSources[si]);
        if (s_textureAssetSources[si].paths) UnloadDirectoryFiles(s_textureAssetSources[si]);
        s_modelAssetSources[si] = (FilePathList){0};
        s_textureAssetSources[si] = (FilePathList){0};
    }
    s_assetSourceCount = 0;
}

//opencode: external source roots are deduplicated and bounded to the runtime cap.
static gX_I1 gfx_asset_root_add(const gX_I8 *path) {
    if (!path || !path[0]) return 0;
    gX_I8 root[256];
    gX_I32 length = snprintf(root, sizeof(root), "%s", path);
    if (length <= 0 || (gX_U32)length >= sizeof(root)) return 0;
    while (length > 1 && root[length - 1] == '/') root[--length] = 0;
    for (gX_U32 ri = 0; ri + 1 < s_assetSourceCount; ri++)
        if (strcmp(root, s_assetGuestRoots[ri]) == 0) return 1;
    if (s_assetSourceCount >= GX_GFX_ASSET_ROOT_SOURCE_CAP) return 0;
    snprintf(s_assetGuestRoots[s_assetSourceCount - 1],
        sizeof(s_assetGuestRoots[0]), "%s", root);
    s_assetSourceCount++;
    return 1;
}

//opencode: settings-backed browse roots (colon separated). Roots are used in
//   place for browsing/assignment only — nothing is copied into the project,
//   so external guest trees stay outside the repo and its copyright audit.
static gX_I1 gfx_asset_settings_root_add(const gX_I8 *path) {
    if (!path || !path[0]) {
        GX_ERROR_SET(GX_ERR_INVALID, "asset root without a path");
        return 0;
    }
    gX_I8 root[256];
    gX_I32 length = snprintf(root, sizeof(root), "%s", path);
    if (length <= 0 || (gX_U32)length >= sizeof(root)) {
        GX_ERROR_SET(GX_ERR_INVALID, "asset root too long: %s", path);
        return 0;
    }
    while (length > 1 && root[length - 1] == '/') root[--length] = 0;
    if (strstr((const char *)settings.toolAssetRoots, (const char *)root))
        return 1;
    size_t used = strlen((const char *)settings.toolAssetRoots);
    size_t needed = strlen((const char *)root) + (used ? 1 : 0);
    if (used + needed >= sizeof(settings.toolAssetRoots)) return 0;
    if (used) {
        settings.toolAssetRoots[used] = ':';
        settings.toolAssetRoots[used + 1] = 0;
    }
    strncat((char *)settings.toolAssetRoots, (const char *)root,
        sizeof(settings.toolAssetRoots) - strlen((const char *)settings.toolAssetRoots) - 1);
    return 1;
}

static gX_I0 gfx_asset_settings_root_remove(const gX_I8 *path) {
    if (!path || !path[0]) return;
    gX_I8 rebuilt[sizeof(settings.toolAssetRoots)];
    rebuilt[0] = 0;
    gX_I8 buffer[sizeof(settings.toolAssetRoots)];
    snprintf(buffer, sizeof(buffer), "%s", settings.toolAssetRoots);
    gX_I8 *save = NULL;
    for (gX_I8 *token = strtok_r(buffer, ":", &save); token;
         token = strtok_r(NULL, ":", &save)) {
        if (strcmp((const char *)token, (const char *)path) == 0) continue;
        if (rebuilt[0]) {
            size_t rebuiltUsed = strlen((const char *)rebuilt);
            rebuilt[rebuiltUsed] = ':';
            rebuilt[rebuiltUsed + 1] = 0;
        }
        strncat(rebuilt, (const char *)token,
            sizeof(rebuilt) - strlen((const char *)rebuilt) - 1);
    }
    snprintf((char *)settings.toolAssetRoots, sizeof(settings.toolAssetRoots),
        "%s", (const char *)rebuilt);
}

//opencode: string ordering helper for the deterministic per-root asset index.
static int gfx_asset_path_compare(const void *left, const void *right) {
    const gX_I8 *leftPath = *(const gX_I8 *const *)left;
    const gX_I8 *rightPath = *(const gX_I8 *const *)right;
    return strcmp(leftPath, rightPath);
}

//opencode: sort each source independently, preserving local-before-guest priority.
static gX_I0 gfx_asset_sort_sources(gX_I0) {
    for (gX_U32 si = 0; si < s_assetSourceCount * 2; si++) {
        if (s_modelAssetSources[si].count > 1)
            qsort(s_modelAssetSources[si].paths, s_modelAssetSources[si].count,
                sizeof(s_modelAssetSources[si].paths[0]), gfx_asset_path_compare);
        if (s_textureAssetSources[si].count > 1)
            qsort(s_textureAssetSources[si].paths, s_textureAssetSources[si].count,
                sizeof(s_textureAssetSources[si].paths[0]), gfx_asset_path_compare);
    }
}

//opencode: index local assets first and registered guest roots as browse-only sources.
static gX_I0 gfx_assets_scan(gX_I0) {
    if (s_assetsReady) return;
    gfx_assets_sources_release();
    s_assetSourceCount = 1;
    gX_U32 generatedTextures = 0;
    const gX_I8 *environmentRoots = getenv("GX_ASSETS");
    if (environmentRoots && environmentRoots[0]) {
        gX_I8 rootBuffer[2048];
        gX_I32 copied = snprintf(rootBuffer, sizeof(rootBuffer), "%s", environmentRoots);
        if (copied > 0 && (gX_U32)copied < sizeof(rootBuffer)) {
            gX_I8 *save = NULL;
            for (gX_I8 *token = strtok_r(rootBuffer, ":", &save);
             token && s_assetSourceCount < GX_GFX_ASSET_ROOT_SOURCE_CAP;
                 token = strtok_r(NULL, ":", &save))
                gfx_asset_root_add(token);
        }
    }
    if (settings.toolAssetRoots[0]) {
        gX_I8 rootBuffer[sizeof(settings.toolAssetRoots)];
        gX_I32 copied = snprintf(rootBuffer, sizeof(rootBuffer), "%s",
            settings.toolAssetRoots);
        if (copied > 0 && (gX_U32)copied < sizeof(rootBuffer)) {
            gX_I8 *save = NULL;
            for (gX_I8 *token = strtok_r(rootBuffer, ":", &save);
                 token && s_assetSourceCount < GX_GFX_ASSET_ROOT_SOURCE_CAP;
                 token = strtok_r(NULL, ":", &save))
                gfx_asset_root_add(token);
        }
    }
    gX_I32 guestCount = arguments.guestAssetCount;
    if (guestCount < 0) guestCount = 0;
    if (guestCount > GX_GFX_ASSET_ROOT_CAP) guestCount = GX_GFX_ASSET_ROOT_CAP;
    for (gX_I32 ri = 0; ri < guestCount
        && s_assetSourceCount < GX_GFX_ASSET_ROOT_SOURCE_CAP; ri++)
        gfx_asset_root_add(arguments.guestAssets[ri]);

    static const gX_I8 *modelDirectories[] = { "model", "models" };
    static const gX_I8 *textureDirectories[] = { "texture", "textures" };
    for (gX_U32 di = 0; di < 2; di++) {
        gX_I8 modelDirectory[320];
        gX_I8 textureDirectory[320];
        snprintf(modelDirectory, sizeof(modelDirectory), "%s/%s",
            "resources", modelDirectories[di]);
        snprintf(textureDirectory, sizeof(textureDirectory), "%s/%s",
            "resources", textureDirectories[di]);
        if (DirectoryExists(modelDirectory))
            s_modelAssetSources[di] = LoadDirectoryFilesEx(modelDirectory, "FILES*", true);
        if (DirectoryExists(textureDirectory))
            s_textureAssetSources[di] = LoadDirectoryFilesEx(textureDirectory, "FILES*", true);
    }
    //opencode: generated textures join the local grid — the Texture tools
    //   write them under resources/tools/textures; authors pick them in the
    //   browser instead of typing paths.
    {
        const gX_I8 *generatedDir = gfx_texture_out_dir();
        if (DirectoryExists((const char *)generatedDir)) {
            FilePathList generated = LoadDirectoryFilesEx(
                (const char *)generatedDir, "FILES*", false);
            FilePathList *target = &s_textureAssetSources[0];
            gX_U32 beforeCount = target->count;
            if (generated.count > 0 && target->paths) {
                gX_U32 total = target->count + generated.count;
                gX_I8 **grown = (gX_I8 **)realloc(target->paths,
                    (size_t)total * sizeof(gX_I8 *));
                if (grown) {
                    target->paths = grown;
                    for (gX_U32 gi = 0; gi < generated.count; gi++) {
                        gX_I8 *copy = strdup(generated.paths[gi]);
                        if (!copy) continue;
                        target->paths[target->count++] = copy;
                    }
                }
            }
            generatedTextures = target->count - beforeCount;
            if (generated.paths) UnloadDirectoryFiles(generated);
        }
    }
    for (gX_U32 ri = 0; ri + 1 < s_assetSourceCount; ri++) {
        gX_I8 modelDirectory[320];
        gX_I8 textureDirectory[320];
        for (gX_U32 di = 0; di < 2; di++) {
            gX_U32 sourceIndex = (ri + 1) * 2 + di;
            snprintf(modelDirectory, sizeof(modelDirectory), "%.*s/%s", 255,
                s_assetGuestRoots[ri], modelDirectories[di]);
            snprintf(textureDirectory, sizeof(textureDirectory), "%.*s/%s", 255,
                s_assetGuestRoots[ri], textureDirectories[di]);
            if (DirectoryExists(modelDirectory))
                s_modelAssetSources[sourceIndex] = LoadDirectoryFilesEx(
                    modelDirectory, "FILES*", true);
            if (DirectoryExists(textureDirectory))
                s_textureAssetSources[sourceIndex] = LoadDirectoryFilesEx(
                    textureDirectory, "FILES*", true);
        }
    }
    gfx_asset_sort_sources();
    s_assetsReady = 1;
    if (getenv("GX_ASSET_TRACE")) {
        gX_U32 models = 0, textures = 0, guestModels = 0, guestTextures = 0;
        for (gX_U32 si = 0; si < s_assetSourceCount * 2; si++) {
            models += s_modelAssetSources[si].count;
            textures += s_textureAssetSources[si].count;
            if (si >= 2) {
                guestModels += s_modelAssetSources[si].count;
                guestTextures += s_textureAssetSources[si].count;
            }
        }
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: assets scan roots=%u models=%u textures=%u "
            "guestModels=%u guestTextures=%u generated=%u",
            s_assetSourceCount - 1, models, textures, guestModels,
            guestTextures, generatedTextures);
    }
}

//opencode: thumbnail cache clear is defined with the browser grid below.
static gX_I0 gfx_asset_thumbnails_clear(gX_I0);

//opencode: full asset index refresh — thumbnails, file lists, and scan.
static gX_I0 gfx_assets_refresh(gX_I0) {
    gfx_asset_thumbnails_clear();
    gfx_assets_sources_release();
    s_assetsReady = 0;
    gfx_assets_scan();
}

//opencode: deterministic filtering keeps repeated searches stable across refreshes.
static gX_I1 gfx_asset_extension_is(const gX_I8 *path, const gX_I8 *extension) {
    const gX_I8 *actual = strrchr(path, '.');
    if (!actual) return 0;
    while (*actual && *extension) {
        if (tolower((unsigned char)*actual) != tolower((unsigned char)*extension)) return 0;
        actual++;
        extension++;
    }
    return !*actual && !*extension;
}

static gX_I1 gfx_asset_path_supported(const gX_I8 *path, gX_I32 kind) {
    static const gX_I8 *modelExtensions[] = {
        ".glb", ".gltf", ".obj", ".iqm", ".vox", ".m3d"
    };
    static const gX_I8 *textureExtensions[] = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr",
        ".qoi", ".psd", ".pic", ".dds", ".ktx"
    };
    const gX_I8 **extensions = kind == gX_GFX_ASSET_MODELS
        ? modelExtensions : textureExtensions;
    gX_U32 count = kind == gX_GFX_ASSET_MODELS
        ? sizeof(modelExtensions) / sizeof(modelExtensions[0])
        : sizeof(textureExtensions) / sizeof(textureExtensions[0]);
    for (gX_U32 ei = 0; ei < count; ei++)
        if (gfx_asset_extension_is(path, extensions[ei])) return 1;
    return 0;
}

//opencode: case-insensitive substring filtering spans filenames and folders.
static gX_I1 gfx_asset_search_matches(const gX_I8 *path) {
    return gfx_text_search_matches(path, s_assetSearch);
}

//opencode: count with the same extension and search rules used by the grid.
static gX_U32 gfx_asset_filtered_count(gX_I32 kind) {
    FilePathList *sources = kind == gX_GFX_ASSET_MODELS
        ? s_modelAssetSources : s_textureAssetSources;
    gX_U32 count = 0;
    for (gX_U32 si = 0; si < s_assetSourceCount * 2; si++) {
        for (gX_U32 fi = 0; fi < sources[si].count; fi++) {
            const gX_I8 *path = sources[si].paths[fi];
            if (gfx_asset_path_supported(path, kind) && gfx_asset_search_matches(path)) count++;
        }
    }
    return count;
}

//opencode: selected paths are copied because raylib owns the scanned path list.
static gX_I0 gfx_asset_select(const gX_I8 *path) {
    gX_I32 copied = snprintf(s_assetSelectedPath, sizeof(s_assetSelectedPath), "%s", path);
    if (copied <= 0 || (gX_U32)copied >= sizeof(s_assetSelectedPath)) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "Asset path is longer than the editor can retain.");
        s_assetSelectedPath[0] = 0;
        return;
    }
    s_assetBrowserStatus[0] = 0;
}

//opencode: visible texture cards share a small, heap-backed thumbnail cache.
static Texture2D *gfx_asset_thumbnail(const gX_I8 *path) {
    if (!s_assetThumbnailCache) {
        s_assetThumbnailCache = (gX_GFX_Asset_Thumbnail *)gX_Calloc(
            sizeof(*s_assetThumbnailCache) * GX_GFX_ASSET_THUMBNAIL_CAP);
        if (!s_assetThumbnailCache) return NULL;
    }
    for (gX_U32 ti = 0; ti < GX_GFX_ASSET_THUMBNAIL_CAP; ti++) {
        if (s_assetThumbnailCache[ti].path[0]
            && strcmp(s_assetThumbnailCache[ti].path, path) == 0)
            return s_assetThumbnailCache[ti].texture.id > 0
                ? &s_assetThumbnailCache[ti].texture : NULL;
    }
    gX_GFX_Asset_Thumbnail *entry = &s_assetThumbnailCache[
        s_assetThumbnailNext++ % GX_GFX_ASSET_THUMBNAIL_CAP];
    if (entry->texture.id > 0) UnloadTexture(entry->texture);
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    Image image = LoadImage(path);
    if (!image.data || image.width <= 0 || image.height <= 0) {
        if (image.data) UnloadImage(image);
        return NULL;
    }
    entry->sourceWidth = image.width;
    entry->sourceHeight = image.height;
    gX_F32 scale = fminf(80.0f / (gX_F32)image.width,
        80.0f / (gX_F32)image.height);
    gX_I32 width = (gX_I32)fmaxf(1.0f, roundf(image.width * scale));
    gX_I32 height = (gX_I32)fmaxf(1.0f, roundf(image.height * scale));
    if (width != image.width || height != image.height) ImageResize(&image, width, height);
    entry->texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (entry->texture.id == 0) return NULL;
    SetTextureFilter(entry->texture, TEXTURE_FILTER_BILINEAR);
    return &entry->texture;
}

//opencode: unload GPU thumbnails before refreshing or closing the browser.
static gX_I0 gfx_asset_thumbnails_clear(gX_I0) {
    if (!s_assetThumbnailCache) return;
    for (gX_U32 ti = 0; ti < GX_GFX_ASSET_THUMBNAIL_CAP; ti++)
        if (s_assetThumbnailCache[ti].texture.id > 0)
            UnloadTexture(s_assetThumbnailCache[ti].texture);
    gX_Free(s_assetThumbnailCache);
    s_assetThumbnailCache = NULL;
    s_assetThumbnailNext = 0;
}

//opencode: browser targets capture the selected renderer object/slot when opened.
static gX_I0 gfx_asset_browser_open(gX_RendererGFX *gfx, gX_I32 kind,
    gX_I32 targetType, gX_I32 targetIndex, gX_I32 targetSlot,
    gX_I32 targetPurpose, gX_I32 currentTexture) {
    if (!gfx) return;
    gfx_assets_scan();
    s_assetBrowserKind = kind;
    s_assetTextureTarget = targetType;
    s_assetTargetIndex = targetIndex;
    s_assetTargetSlot = targetSlot;
    s_assetTargetPurpose = targetPurpose;
    s_assetSearch[0] = 0;
    s_assetSelectedPath[0] = 0;
    s_assetBrowserStatus[0] = 0;
    if (kind == gX_GFX_ASSET_MODELS) {
        gfx_asset_select(s_modelPath);
    } else if (targetType == gX_GFX_ASSET_TARGET_WATER_NORMAL) {
        gfx_asset_select(gfx->water.normalMapPath);
    } else if (currentTexture >= 0) {
        gX_RendererGFX_Texture *texture = gfx_texture_at(gfx, currentTexture);
        if (texture) gfx_asset_select(texture->wrapper.path);
    }
    s_assetBrowserOpen = 1;
    s_assetBrowserFocus = 1;
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: asset browser open kind=%d target=%d index=%d slot=%d",
        kind, targetType, targetIndex, targetSlot);
}

//opencode: scene-panel shortcut to the model asset browser.
static gX_I0 gfx_asset_browser_button(gX_RendererGFX *gfx) {
    if (ImGui::Button("Browse models..."))
        gfx_asset_browser_open(gfx, gX_GFX_ASSET_MODELS,
            gX_GFX_ASSET_TARGET_NONE, -1, -1, -1, -1);
}

//opencode: assign directly into the original selector target after adding/reusing
//   its texture-heap entry, so the usual model/decal load path handles the asset.
static gX_I1 gfx_asset_texture_assign(gX_RendererGFX *gfx, gX_I32 textureIndex) {
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_NONE) return 1;
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_DECAL) {
        gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, s_assetTargetIndex);
        if (!decal) return 0;
        decal->textureIndex = textureIndex;
        return 1;
    }
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_assetTargetIndex);
    if (!model) return 0;
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_SLOT) {
        if (s_assetTargetSlot < 0 || s_assetTargetSlot >= gX_GFX_MODEL_TEXTURE_SLOTS) return 0;
        model->textureID[s_assetTargetSlot] = textureIndex;
        model->texturePurpose[s_assetTargetSlot] = s_assetTargetPurpose;
        gfx_model_map_mark_present(model, s_assetTargetPurpose);
    } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_GLOSS) {
        model->params.glossMapID = textureIndex;
        model->params.hasGlossMap = 1;
    } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_LIGHTMAP) {
        model->params.lightmapID = textureIndex;
        model->params.hasLightmap = 1;
    }
    return 1;
}

//opencode: write a browsed texture path into a per-material override map and
//   force the runtime IDs to re-resolve. Shared by the browser and the
//   headless material-map check.
static gX_I1 gfx_material_override_map_assign(gX_RendererGFX_Model *model,
    gX_I32 overrideIndex, gX_I32 mapKind, const gX_I8 *path) {
    if (!model || !path || !path[0]) return 0;
    if (overrideIndex < 0
        || overrideIndex >= model->params.materialOverrideCount) return 0;
    gX_RendererGFX_MaterialOverride *entry =
        &model->params.materialOverrides[overrideIndex];
    gX_I8 *destination = (mapKind == 1) ? entry->emissiveMap : entry->albedoMap;
    size_t capacity = (mapKind == 1) ? sizeof(entry->emissiveMap)
                                     : sizeof(entry->albedoMap);
    size_t length = strlen(path);
    if (length >= capacity) return 0;
    memcpy(destination, path, length + 1);
    entry->albedoMapID = -1;
    entry->emissiveMapID = -1;
    gX_RendererGFX_Model_MaterialOverridesResolve(model);
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: material map assigned override=%d kind=%s path=%s",
        overrideIndex, mapKind == 1 ? "emissive" : "albedo", destination);
    return 1;
}

//opencode: per-material map browser — captures the model, override entry, and
//   map kind (0 = albedo, 1 = emissive) for the assign step.
static gX_I0 gfx_asset_browser_open_material_map(gX_RendererGFX *gfx,
    gX_I32 modelIndex, gX_I32 overrideIndex, gX_I32 mapKind) {
    gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
        gX_GFX_ASSET_TARGET_MATERIAL_MAP, modelIndex, mapKind, -1, -1);
    s_assetTargetOverride = overrideIndex;
}

//opencode: apply a selected browser asset to the captured scene destination.
static gX_I1 gfx_asset_browser_apply(gX_RendererGFX *gfx) {
    if (!s_assetSelectedPath[0]) return 0;
    if (!gfx_asset_path_supported(s_assetSelectedPath, s_assetBrowserKind)) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "Select a supported asset for the active browser tab.");
        return 0;
    }
    if (!gfx_preset_file_exists(s_assetSelectedPath)) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "Asset file no longer exists; refresh the browser and choose another.");
        return 0;
    }
    if (strlen(s_assetSelectedPath) >= sizeof(gX_TextLine)) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "This asset path exceeds the renderer's 127-character path limit.");
        return 0;
    }
    if (s_assetBrowserKind == gX_GFX_ASSET_MODELS) {
        memcpy(s_modelPath, s_assetSelectedPath, strlen(s_assetSelectedPath) + 1);
        gX_I32 modelIndex = gX_RendererGFX_Model_Add(gfx, s_assetSelectedPath);
        if (modelIndex < 0) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "The scene model list is full.");
            return 0;
        }
        s_selModel = modelIndex;
        gfx_model_revert_capture(gfx, modelIndex);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: asset browser added model %d: %s",
            modelIndex, s_modelPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_WATER_NORMAL) {
        size_t pathLength = strlen(s_assetSelectedPath);
        if (pathLength >= sizeof(gfx->water.normalMapPath)) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "Water normal map path exceeds the preset path limit.");
            return 0;
        }
        gX_Texture texture = gX_Texture_Load(s_assetSelectedPath);
        if (texture.id <= 0) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "Water normal map could not be loaded.");
            return 0;
        }
        if (gfx->water.normalMapTex.id > 0)
            gX_Texture_Unload(gfx->water.normalMapTex);
        gfx->water.normalMapTex = texture;
        memcpy(gfx->water.normalMapPath, s_assetSelectedPath, pathLength + 1);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: water normal map assigned: %s",
            gfx->water.normalMapPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_RAYMARCH_SKYBOX
        || s_assetTextureTarget == gX_GFX_ASSET_TARGET_RAYMARCH_TEX) {
        gX_RendererRaymarch *rm = gfx_raymarch_active();
        if (!rm) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "The raymarch module is not active; open it in the panel.");
            return 0;
        }
        gX_I32 slot = (s_assetTextureTarget
            == gX_GFX_ASSET_TARGET_RAYMARCH_SKYBOX) ? -1 : s_assetTargetSlot;
        gX_RendererRaymarch_Texture_SetPath(rm, slot, s_assetSelectedPath);
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: raymarch texture slot %d assigned: %s", slot,
            s_assetSelectedPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_SKYBOX) {
        size_t pathLength = strlen(s_assetSelectedPath);
        if (pathLength >= sizeof(gfx->sky.skyboxPath)) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "Cubemap path exceeds the preset path limit.");
            return 0;
        }
        if (gfx->sky.skybox.wrapper.loadingState.loaded)
            gX_RendererGFX_Texture_Unload(&gfx->sky.skybox);
        memcpy(gfx->sky.skyboxPath, s_assetSelectedPath, pathLength + 1);
        memcpy(gfx->sky.skybox.wrapper.path, s_assetSelectedPath, pathLength + 1);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: skybox assigned: %s",
            gfx->sky.skyboxPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_LIGHT_COOKIE) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, s_assetTargetIndex);
        if (!light) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "The light no longer exists; reopen the browser from its panel.");
            return 0;
        }
        gX_RendererGFX_Light_SetCookie(gfx, light, s_assetSelectedPath);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: cookie assigned light=%d path=%s",
            s_assetTargetIndex, s_assetSelectedPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_DETAIL) {
        size_t pathLength = strlen(s_assetSelectedPath);
        if (pathLength >= sizeof(gfx->detail.mapPath)) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "Detail texture path exceeds the preset path limit.");
            return 0;
        }
        if (gfx->detail.map.id > 0) gX_Texture_Unload(gfx->detail.map);
        gfx->detail.map = gX_Texture_Load(s_assetSelectedPath);
        memcpy(gfx->detail.mapPath, s_assetSelectedPath, pathLength + 1);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: detail texture assigned: %s",
            gfx->detail.mapPath);
        return 1;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MATERIAL_MAP) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, s_assetTargetIndex);
        if (!gfx_material_override_map_assign(model, s_assetTargetOverride,
                s_assetTargetSlot, s_assetSelectedPath)) {
            snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
                "The material override no longer exists; reopen the browser "
                "from its panel.");
            return 0;
        }
        return 1;
    }
    gX_I32 textureIndex = gfx_texture_find_or_add(gfx, s_assetSelectedPath);
    if (textureIndex < 0) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "Texture could not be added; check the file and scene texture capacity.");
        return 0;
    }
    if (!gfx_asset_texture_assign(gfx, textureIndex)) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "The texture target no longer exists; reopen the browser from its selector.");
        return 0;
    }
    if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_NONE) {
        snprintf(s_assetBrowserStatus, sizeof(s_assetBrowserStatus),
            "Added to the scene texture pool as slot %d.", textureIndex);
        return 0;
    }
    gX_TraceLog(gX_LOG_INFO, "GfxEd: asset browser assigned texture %d: %s",
        textureIndex, s_assetSelectedPath);
    return 1;
}

//opencode: searchable model cards and a thumbnail grid for texture assignment.
static gX_I0 gfx_asset_browser_window(gX_RendererGFX *gfx) {
    if (!s_assetBrowserOpen || !gfx) return;
    gX_F32 scrW = (gX_F32)maindata.windowWidth;
    gX_F32 scrH = (gX_F32)maindata.windowHeight;
    if (scrW <= 0.0f) scrW = ImGui::GetIO().DisplaySize.x;
    if (scrH <= 0.0f) scrH = ImGui::GetIO().DisplaySize.y;
    gX_F32 width = fminf(1100.0f, scrW - 32.0f);
    gX_F32 height = fminf(760.0f, scrH - ImGui::GetFrameHeight() - 32.0f);
    if (width < 480.0f) width = 480.0f;
    if (height < 360.0f) height = 360.0f;
    if (s_assetBrowserFocus) {
        ImGui::SetNextWindowPos(ImVec2(scrW * 0.5f, scrH * 0.5f), ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
        ImGui::SetNextWindowFocus();
        s_assetBrowserFocus = 0;
    }
    bool open = true;
    if (ImGui::Begin("GFX Asset Browser", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginDisabled(s_assetTextureTarget != gX_GFX_ASSET_TARGET_NONE);
        if (ImGui::Button("Models")
            && s_assetBrowserKind != gX_GFX_ASSET_MODELS) {
            s_assetBrowserKind = gX_GFX_ASSET_MODELS;
            s_assetSelectedPath[0] = 0;
            s_assetBrowserStatus[0] = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Textures")
            && s_assetBrowserKind != gX_GFX_ASSET_TEXTURES) {
            s_assetBrowserKind = gX_GFX_ASSET_TEXTURES;
            s_assetSelectedPath[0] = 0;
            s_assetBrowserStatus[0] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            gfx_assets_refresh();
        //opencode: browse roots — local resources plus guest roots from
        //   GX_ASSETS, --guest-asset, and this settings-backed list. Roots are
        //   read in place; nothing is copied into the project.
        {
            static gX_I8 rootInput[256] = {0};
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
            ImGui::InputTextWithHint("##gfxAssetRoot",
                "Add asset root (folder with model/ or texture/)",
                rootInput, sizeof(rootInput));
            ImGui::SameLine();
            if (ImGui::Button("Add root")) {
                if (rootInput[0] && gfx_asset_settings_root_add(rootInput))
                    gfx_assets_refresh();
                rootInput[0] = 0;
            }
        }
        //opencode: settings-backed roots get one-click removal; env GX_ASSETS
        //   and --guest-asset roots stay for the session.
        if (settings.toolAssetRoots[0]) {
            gX_I8 rootList[sizeof(settings.toolAssetRoots)];
            snprintf(rootList, sizeof(rootList), "%s", settings.toolAssetRoots);
            gX_I8 *rootSave = NULL;
            for (gX_I8 *rootToken = strtok_r(rootList, ":", &rootSave); rootToken;
                 rootToken = strtok_r(NULL, ":", &rootSave)) {
                ImGui::PushID(rootToken);
                ImGui::TextDisabled("root: %s", rootToken);
                ImGui::SameLine();
                if (ImGui::SmallButton("remove")) {
                    gfx_asset_settings_root_remove(rootToken);
                    gfx_assets_refresh();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##gfxAssetSearch", "Search names or paths...",
            s_assetSearch, sizeof(s_assetSearch));
        gX_U32 assetCount = gfx_asset_filtered_count(s_assetBrowserKind);
        ImGui::TextDisabled("%u matching assets | local resources, generated textures and guest roots",
            assetCount);
        ImVec2 available = ImGui::GetContentRegionAvail();
        gX_F32 listWidth = available.x * 0.67f;
        ImGui::BeginChild("GfxAssetGrid", ImVec2(listWidth, 0.0f), true);
        gX_U32 columns = s_assetBrowserKind == gX_GFX_ASSET_TEXTURES ? 4u : 3u;
        if (ImGui::BeginTable("GfxAssetItems", (int)columns,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
            FilePathList *sources = s_assetBrowserKind == gX_GFX_ASSET_MODELS
                ? s_modelAssetSources : s_textureAssetSources;
            for (gX_U32 si = 0; si < s_assetSourceCount * 2; si++) {
                for (gX_U32 fi = 0; fi < sources[si].count; fi++) {
                    const gX_I8 *path = sources[si].paths[fi];
                    if (!gfx_asset_path_supported(path, s_assetBrowserKind)
                        || !gfx_asset_search_matches(path)) continue;
                    ImGui::TableNextColumn();
                    ImGui::PushID(path);
                    gX_I1 selected = strcmp(s_assetSelectedPath, path) == 0;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.18f, 0.42f, 0.68f, 1.0f));
                    if (s_assetBrowserKind == gX_GFX_ASSET_MODELS) {
                        if (ImGui::Button(gfx_model_basename(path), ImVec2(-FLT_MIN, 76.0f)))
                            gfx_asset_select(path);
                        ImGui::TextDisabled("%s", path);
                    } else {
                        ImVec2 itemSize = ImVec2(84.0f, 84.0f);
                        gX_I1 visible = ImGui::IsRectVisible(itemSize);
                        Texture2D *thumbnail = visible ? gfx_asset_thumbnail(path) : NULL;
                        if (thumbnail) {
                            itemSize = ImVec2((gX_F32)thumbnail->width,
                                (gX_F32)thumbnail->height);
                            if (ImGui::ImageButton("##assetThumbnail",
                                    (ImTextureID)thumbnail->id, itemSize))
                                gfx_asset_select(path);
                        } else if (visible) {
                            if (ImGui::Button("Preview unavailable", itemSize))
                                gfx_asset_select(path);
                        } else {
                            ImGui::Dummy(itemSize);
                        }
                        ImGui::TextWrapped("%s", gfx_model_basename(path));
                    }
                    if (selected) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (assetCount == 0) ImGui::TextDisabled("No supported assets match this search.");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("GfxAssetDetails", ImVec2(0.0f, 0.0f), true);
        ImGui::SeparatorText("Selected asset");
        if (s_assetBrowserKind == gX_GFX_ASSET_TEXTURES
            && s_assetTextureTarget != gX_GFX_ASSET_TARGET_NONE) {
            if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_SLOT) {
                ImGui::TextDisabled("Target: model %d · slot %d · %s",
                    s_assetTargetIndex, s_assetTargetSlot,
                    s_assetTargetPurpose >= 0
                        && s_assetTargetPurpose < gX_GFX_MAX_MATERIAL_MAPS
                        ? s_materialMapNames[s_assetTargetPurpose] : "map");
            } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_GLOSS) {
                ImGui::TextDisabled("Target: model %d · gloss map", s_assetTargetIndex);
            } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_LIGHTMAP) {
                ImGui::TextDisabled("Target: model %d · lightmap", s_assetTargetIndex);
            } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_DECAL) {
                ImGui::TextDisabled("Target: decal %d", s_assetTargetIndex);
            } else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_WATER_NORMAL) {
                ImGui::TextDisabled("Target: water normal map");
            }
        }
        if (s_assetSelectedPath[0]) {
            ImGui::TextWrapped("%s", s_assetSelectedPath);
            gX_I1 pathFits = strlen(s_assetSelectedPath) < sizeof(gX_TextLine);
            if (!pathFits) ImGui::TextWrapped(
                "Path is longer than the renderer's 127-character asset-path limit.");
            if (s_assetBrowserKind == gX_GFX_ASSET_TEXTURES) {
                Texture2D *thumbnail = gfx_asset_thumbnail(s_assetSelectedPath);
                if (thumbnail) {
                    ImGui::Image((ImTextureID)thumbnail->id,
                        ImVec2((gX_F32)thumbnail->width, (gX_F32)thumbnail->height));
                    for (gX_U32 ti = 0; ti < GX_GFX_ASSET_THUMBNAIL_CAP; ti++) {
                        gX_GFX_Asset_Thumbnail *entry = &s_assetThumbnailCache[ti];
                        if (strcmp(entry->path, s_assetSelectedPath) == 0) {
                            ImGui::TextDisabled("%d × %d", entry->sourceWidth,
                                entry->sourceHeight);
                            break;
                        }
                    }
                }
            }
            if (s_assetBrowserKind == gX_GFX_ASSET_MODELS) {
                ImGui::TextDisabled("Model asset");
                ImGui::BeginDisabled(!pathFits);
                if (ImGui::Button("Add model to scene", ImVec2(-FLT_MIN, 0.0f)))
                    if (gfx_asset_browser_apply(gfx)) open = false;
                ImGui::EndDisabled();
            } else {
                const gX_I8 *action = "Add texture to scene";
                if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_SLOT)
                    action = "Assign to material slot";
                else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_GLOSS)
                    action = "Assign gloss map";
                else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MODEL_LIGHTMAP)
                    action = "Assign lightmap";
                else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_DECAL)
                    action = "Assign decal texture";
                else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_WATER_NORMAL)
                    action = "Set water normal map";
                else if (s_assetTextureTarget == gX_GFX_ASSET_TARGET_MATERIAL_MAP)
                    action = (s_assetTargetSlot == 1) ? "Assign emissive map"
                                                      : "Assign albedo map";
                ImGui::BeginDisabled(!pathFits);
                if (ImGui::Button(action, ImVec2(-FLT_MIN, 0.0f)))
                    if (gfx_asset_browser_apply(gfx)) open = false;
                ImGui::EndDisabled();
            }
        } else {
            ImGui::TextDisabled("Choose an asset from the grid.");
        }
        if (s_assetBrowserStatus[0]) ImGui::TextWrapped("%s", s_assetBrowserStatus);
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f))) open = false;
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) {
        s_assetBrowserOpen = 0;
        s_assetTextureTarget = gX_GFX_ASSET_TARGET_NONE;
        s_assetTargetIndex = -1;
        s_assetTargetSlot = -1;
        s_assetTargetPurpose = -1;
        s_assetTargetOverride = -1;
        s_assetThumbnailClearPending = 1;
    }
}

static gX_F32v3 gfx_vec3_cross(gX_F32v3 a, gX_F32v3 b) {
    return (gX_F32v3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

static gX_F32 gfx_vec3_dot(gX_F32v3 a, gX_F32v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static gX_F32v3 gfx_vec3_normalize(gX_F32v3 v) {
    gX_F32 length = sqrtf(gfx_vec3_dot(v, v));
    if (length <= 0.0001f) return (gX_F32v3){0.0f, 0.0f, 0.0f};
    return (gX_F32v3){v.x / length, v.y / length, v.z / length};
}

static gX_I0 gfx_camera_inspection_view(gX_RendererGFX_Model *model,
    gX_F32v3 direction, gX_F32v3 up) {
    gX_F32v3 target = model ? model->params.pos : (gX_F32v3){0.0f, 0.0f, 0.0f};
    gX_F32 distance = 6.0f;
    if (model && model->aabbValid) {
        target.x += (model->aabbMin[0] + model->aabbMax[0]) * 0.5f * model->params.scale.x;
        target.y += (model->aabbMin[1] + model->aabbMax[1]) * 0.5f * model->params.scale.y;
        target.z += (model->aabbMin[2] + model->aabbMax[2]) * 0.5f * model->params.scale.z;
        gX_F32 extentX = fabsf(model->aabbMax[0] - model->aabbMin[0]) * fabsf(model->params.scale.x);
        gX_F32 extentY = fabsf(model->aabbMax[1] - model->aabbMin[1]) * fabsf(model->params.scale.y);
        gX_F32 extentZ = fabsf(model->aabbMax[2] - model->aabbMin[2]) * fabsf(model->params.scale.z);
        gX_F32 extent = fmaxf(extentX, fmaxf(extentY, extentZ));
        gX_F32 fovy = camera.camera3D.fovy;
        if (fovy < 5.0f || fovy > 175.0f) fovy = 45.0f;
        distance = extent * 0.5f / tanf(fovy * 0.5f * gX_DEG2RAD) * 1.6f;
        if (distance < 3.0f) distance = 3.0f;
        if (distance > 5000.0f) distance = 5000.0f;
    }
    gX_F32v3 viewDirection = gfx_vec3_normalize(direction);
    if (gfx_vec3_dot(viewDirection, viewDirection) <= 0.0f)
        viewDirection = (gX_F32v3){1.0f, 0.35f, 1.0f};
    camera.camera3D.target = target;
    camera.camera3D.position = (gX_F32v3){
        target.x + viewDirection.x * distance,
        target.y + viewDirection.y * distance,
        target.z + viewDirection.z * distance};
    camera.camera3D.up = gfx_vec3_normalize(up);
}

static gX_I0 gfx_camera_frame_model(gX_RendererGFX_Model *model) {
    gX_F32v3 direction = gfx_vec3_normalize((gX_F32v3){
        camera.camera3D.position.x - camera.camera3D.target.x,
        camera.camera3D.position.y - camera.camera3D.target.y,
        camera.camera3D.position.z - camera.camera3D.target.z});
    if (gfx_vec3_dot(direction, direction) <= 0.0f)
        direction = (gX_F32v3){1.0f, 0.35f, 1.0f};
    gfx_camera_inspection_view(model, direction, (gX_F32v3){0.0f, 1.0f, 0.0f});
}

//opencode: frame a selected light with a close, stable camera target.
static gX_I0 gfx_camera_frame_light(gX_RendererGFX_Light *light) {
    if (!light) return;
    gX_F32v3 direction = gfx_vec3_normalize((gX_F32v3){
        camera.camera3D.position.x - camera.camera3D.target.x,
        camera.camera3D.position.y - camera.camera3D.target.y,
        camera.camera3D.position.z - camera.camera3D.target.z});
    if (gfx_vec3_dot(direction, direction) <= 0.0f)
        direction = (gX_F32v3){1.0f, 0.35f, 1.0f};
    gX_F32 fovy = camera.camera3D.fovy;
    if (fovy < 5.0f || fovy > 175.0f) fovy = 45.0f;
    gX_F32 distance = 0.75f / tanf(fovy * 0.5f * gX_DEG2RAD);
    if (distance < 1.5f) distance = 1.5f;
    if (distance > 100.0f) distance = 100.0f;
    camera.camera3D.target = light->pos;
    camera.camera3D.position = (gX_F32v3){
        light->pos.x + direction.x * distance,
        light->pos.y + direction.y * distance,
        light->pos.z + direction.z * distance};
    camera.camera3D.up = (gX_F32v3){0.0f, 1.0f, 0.0f};
}

//opencode: match the visible brightness-scaled editor marker for ray picking.
static gX_F32 gfx_light_marker_scale(gX_RendererGFX_Light *light) {
    gX_F32 brightness = light->brightness;
    if (brightness < 0.0f) brightness = 0.0f;
    gX_F32 sizeFactor = logf(1.0f + brightness)
        / logf(1.0f + gX_GFX_MARKER_BRIGHTNESS_MAX);
    if (sizeFactor > 1.0f) sizeFactor = 1.0f;
    return gX_GFX_MARKER_SIZE_MIN
        + (gX_GFX_MARKER_SIZE - gX_GFX_MARKER_SIZE_MIN) * sizeFactor;
}

//opencode: ray/sphere intersection returns the nearest positive marker hit.
static gX_I1 gfx_ray_sphere(gX_Ray ray, gX_F32v3 center,
    gX_F32 radius, gX_F32 *distance) {
    gX_F32v3 offset = {ray.position.x - center.x,
        ray.position.y - center.y, ray.position.z - center.z};
    gX_F32 a = gfx_vec3_dot(ray.direction, ray.direction);
    gX_F32 b = gfx_vec3_dot(offset, ray.direction);
    gX_F32 c = gfx_vec3_dot(offset, offset) - radius * radius;
    gX_F32 discriminant = b * b - a * c;
    if (a <= 0.000001f || discriminant < 0.0f) return 0;
    gX_F32 root = sqrtf(discriminant);
    gX_F32 nearHit = (-b - root) / a;
    gX_F32 farHit = (-b + root) / a;
    *distance = nearHit >= 0.0f ? nearHit : farHit;
    return *distance >= 0.0f;
}

//opencode: select only the nearest visible light marker along the viewport ray.
static gX_I32 gfx_light_pick(gX_RendererGFX *gfx, gX_F32v2 gameMouse,
    gX_F32 *outDistance) {
    if (!gfx || !gfx->state.SHOW_LIGHT_MARKERS
        || !gfx->common.sphereMarkerLoaded) return -1;
    gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
    gX_F32 nearest = INFINITY;
    gX_I32 selected = -1;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, (gX_I32)li);
        if (!light) continue;
        gX_F32 distance = 0.0f;
        gX_F32 radius = gfx_light_marker_scale(light) * 1.3f;
        if (gfx_ray_sphere(ray, light->pos, radius, &distance)
            && distance < nearest) {
            nearest = distance;
            selected = (gX_I32)li;
        }
    }
    if (selected >= 0 && outDistance) *outDistance = nearest;
    return selected;
}

static gX_I1 gfx_ray_plane(gX_Ray ray, gX_F32v3 point, gX_F32v3 normal, gX_F32v3 *hit) {
    gX_F32 denominator = gfx_vec3_dot(ray.direction, normal);
    if (fabsf(denominator) <= 0.0001f) return 0;
    gX_F32v3 offset = {point.x - ray.position.x, point.y - ray.position.y, point.z - ray.position.z};
    gX_F32 distance = gfx_vec3_dot(offset, normal) / denominator;
    if (distance < 0.0f) return 0;
    *hit = (gX_F32v3){
        ray.position.x + ray.direction.x * distance,
        ray.position.y + ray.direction.y * distance,
        ray.position.z + ray.direction.z * distance};
    return 1;
}

//opencode: closest parameter between a mouse ray and a world-axis line.
static gX_I1 gfx_ray_axis_parameter(gX_Ray ray, gX_F32v3 point,
    gX_F32v3 axis, gX_F32 *parameter) {
    gX_F32v3 offset = {
        ray.position.x - point.x,
        ray.position.y - point.y,
        ray.position.z - point.z};
    gX_F32 rayLengthSq = gfx_vec3_dot(ray.direction, ray.direction);
    gX_F32 axisLengthSq = gfx_vec3_dot(axis, axis);
    gX_F32 rayAxisDot = gfx_vec3_dot(ray.direction, axis);
    gX_F32 denominator = rayLengthSq * axisLengthSq - rayAxisDot * rayAxisDot;
    if (denominator <= 0.0001f) return 0;
    *parameter = (gfx_vec3_dot(axis, offset) * rayLengthSq
        - rayAxisDot * gfx_vec3_dot(ray.direction, offset)) / denominator;
    return 1;
}

//opencode: read the optional world-axis chord from the active ImGui frame.
static gX_I32 gfx_editor_modifier_axis(gX_I0) {
    if (ImGui::IsKeyDown(ImGuiKey_X)) return 0;
    if (ImGui::IsKeyDown(ImGuiKey_Y)) return 1;
    if (ImGui::IsKeyDown(ImGuiKey_Z)) return 2;
    return -1;
}

//opencode: map X/Y/Z transform constraints to their world unit vectors.
static gX_F32v3 gfx_editor_axis_vector(gX_I32 axis) {
    if (axis == 0) return (gX_F32v3){1.0f, 0.0f, 0.0f};
    if (axis == 1) return (gX_F32v3){0.0f, 1.0f, 0.0f};
    return (gX_F32v3){0.0f, 0.0f, 1.0f};
}

//opencode: snap a transform component to the editor's configured increment.
static gX_F32 gfx_editor_snap_value(gX_F32 value, gX_F32 step) {
    if (step <= 0.0f) return value;
    return roundf(value / step) * step;
}

//opencode: opt-in gesture readout for deterministic editor interaction tests.
static gX_I0 gfx_editor_gesture_trace(gX_RendererGFX_Model *model) {
    const char *trace = getenv("GX_GFX_GESTURE_TRACE");
    if (!trace || !trace[0] || trace[0] == '0' || !model) return;
    const char *mode = s_dragMode == gX_GFX_EDITOR_DRAG_ROTATE ? "rotate"
        : (s_dragMode == gX_GFX_EDITOR_DRAG_SCALE ? "scale"
            : (s_dragMode == gX_GFX_EDITOR_DRAG_AXIS ? "axis" : "plane"));
    gX_TraceLog(gX_LOG_INFO,
        "GfxEd: gesture end mode=%s axis=%d model=%d startPos=(%.3f,%.3f,%.3f) pos=(%.3f,%.3f,%.3f) startRot=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f) startScale=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f) view=%d",
        mode, s_dragAxis, s_dragModel,
        s_dragStartPosition.x, s_dragStartPosition.y, s_dragStartPosition.z,
        model->params.pos.x, model->params.pos.y, model->params.pos.z,
        s_dragStartRotation.x, s_dragStartRotation.y, s_dragStartRotation.z,
        model->params.rotation.x, model->params.rotation.y, model->params.rotation.z,
        s_dragStartScale.x, s_dragStartScale.y, s_dragStartScale.z,
        model->params.scale.x, model->params.scale.y, model->params.scale.z,
        s_dragViewPlane);
}

//opencode: preserve nonzero model extents while applying a positive drag-scale factor.
static gX_F32 gfx_editor_scale_component(gX_F32 value, gX_F32 factor) {
    gX_F32 scaled = value * factor;
    if (fabsf(scaled) < 0.001f) return scaled < 0.0f ? -0.001f : 0.001f;
    return scaled;
}

static gX_F32v3 gfx_camera_right(gX_Camera3D *cam) {
    gX_F32v3 forward = gfx_vec3_normalize((gX_F32v3){
        cam->target.x - cam->position.x,
        cam->target.y - cam->position.y,
        cam->target.z - cam->position.z});
    return gfx_vec3_normalize(gfx_vec3_cross(forward, cam->up));
}

//opencode: camera-facing normal for screenspace (view-plane) dragging.
static gX_F32v3 gfx_camera_forward(gX_Camera3D *cam) {
    return gfx_vec3_normalize((gX_F32v3){
        cam->target.x - cam->position.x,
        cam->target.y - cam->position.y,
        cam->target.z - cam->position.z});
}

//opencode: log completed pan/orbit gestures for deterministic editor tests.
static gX_I0 gfx_editor_camera_gesture_end(gX_I0) {
    if (!s_cameraGestureMode) return;
    const gX_I8 *trace = getenv("GX_GFX_GESTURE_TRACE");
    if (trace && trace[0] && trace[0] != '0') {
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: camera gesture mode=%s startPos=(%.3f,%.3f,%.3f) "
            "endPos=(%.3f,%.3f,%.3f) startTarget=(%.3f,%.3f,%.3f) "
            "endTarget=(%.3f,%.3f,%.3f)",
            s_cameraGestureMode == 1 ? "pan" : "orbit",
            s_cameraGestureStartPos.x, s_cameraGestureStartPos.y,
            s_cameraGestureStartPos.z,
            camera.camera3D.position.x, camera.camera3D.position.y,
            camera.camera3D.position.z,
            s_cameraGestureStartTarget.x, s_cameraGestureStartTarget.y,
            s_cameraGestureStartTarget.z,
            camera.camera3D.target.x, camera.camera3D.target.y,
            camera.camera3D.target.z);
    }
    s_cameraGestureMode = 0;
}

//opencode: viewport camera navigation: middle drag orbits, right drag pans,
//   Shift+middle keeps the legacy pan, and the wheel dollies or adjusts
//   orthographic scale around the target.
static gX_I0 gfx_editor_camera_input(ImGuiIO &io, ImVec2 mouse,
    gX_Rectangle viewport, gX_I1 inViewport) {
    if (inViewport && !io.WantCaptureMouse && io.MouseWheel != 0.0f) {
        gX_F32 zoom = expf(-io.MouseWheel * 0.12f);
        if (camera.camera3D.projection == CAMERA_ORTHOGRAPHIC) {
            camera.camera3D.fovy *= zoom;
            if (camera.camera3D.fovy < 1.0f) camera.camera3D.fovy = 1.0f;
            if (camera.camera3D.fovy > 175.0f) camera.camera3D.fovy = 175.0f;
        } else {
            gX_F32v3 offset = {
                camera.camera3D.position.x - camera.camera3D.target.x,
                camera.camera3D.position.y - camera.camera3D.target.y,
                camera.camera3D.position.z - camera.camera3D.target.z};
            gX_F32 distance = sqrtf(gfx_vec3_dot(offset, offset));
            gX_F32 nextDistance = distance * zoom;
            if (nextDistance < 0.15f) nextDistance = 0.15f;
            if (nextDistance > 10000.0f) nextDistance = 10000.0f;
            if (distance > 0.0001f) {
                gX_F32 factor = nextDistance / distance;
                camera.camera3D.position = (gX_F32v3){
                    camera.camera3D.target.x + offset.x * factor,
                    camera.camera3D.target.y + offset.y * factor,
                    camera.camera3D.target.z + offset.z * factor};
            }
        }
    }
    gX_I1 middleDrag = ImGui::IsMouseDown(ImGuiMouseButton_Middle)
        && ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
    gX_I1 rightDrag = ImGui::IsMouseDown(ImGuiMouseButton_Right)
        && ImGui::IsMouseDragging(ImGuiMouseButton_Right);
    if ((!middleDrag && !rightDrag) || io.WantCaptureMouse || !inViewport) {
        gfx_editor_camera_gesture_end();
        return;
    }
    gX_I1 pan = rightDrag || io.KeyShift;
    gX_I32 mode = pan ? 1 : 2;
    if (s_cameraGestureMode != mode) {
        s_cameraGestureMode = mode;
        s_cameraGestureStartPos = camera.camera3D.position;
        s_cameraGestureStartTarget = camera.camera3D.target;
    }
    gX_F32 dx = io.MouseDelta.x;
    gX_F32 dy = io.MouseDelta.y;
    if (pan) {
        gX_F32 distance = sqrtf(gfx_vec3_dot((gX_F32v3){
            camera.camera3D.target.x - camera.camera3D.position.x,
            camera.camera3D.target.y - camera.camera3D.position.y,
            camera.camera3D.target.z - camera.camera3D.position.z},
            (gX_F32v3){
                camera.camera3D.target.x - camera.camera3D.position.x,
                camera.camera3D.target.y - camera.camera3D.position.y,
                camera.camera3D.target.z - camera.camera3D.position.z}));
        gX_F32 viewHeight = viewport.height > 1.0f ? viewport.height : 1.0f;
        gX_F32 worldPerPixel = camera.camera3D.projection == CAMERA_ORTHOGRAPHIC
            ? camera.camera3D.fovy / viewHeight
            : 2.0f * distance * tanf(camera.camera3D.fovy * 0.5f * gX_DEG2RAD)
                / viewHeight;
        gX_F32v3 right = gfx_camera_right(&camera.camera3D);
        gX_F32v3 up = gfx_vec3_normalize(camera.camera3D.up);
        gX_F32v3 panDelta = {
            worldPerPixel * (-dx * right.x + dy * up.x),
            worldPerPixel * (-dx * right.y + dy * up.y),
            worldPerPixel * (-dx * right.z + dy * up.z)};
        camera.camera3D.position.x += panDelta.x;
        camera.camera3D.position.y += panDelta.y;
        camera.camera3D.position.z += panDelta.z;
        camera.camera3D.target.x += panDelta.x;
        camera.camera3D.target.y += panDelta.y;
        camera.camera3D.target.z += panDelta.z;
    } else {
        gX_Camera_Yaw(&camera.camera3D, -dx * 0.005f, 1);
        gX_Camera_Pitch(&camera.camera3D, -dy * 0.005f, 1, 1, 0);
    }
    (void)mouse;
}

//opencode: switch a plane drag onto the held-V view plane once the key event
//   arrives (one frame after the click), re-anchoring the drag baseline so the
//   model does not jump when the plane changes.
static gX_I0 gfx_editor_view_plane_sync(gX_Ray ray, gX_F32v3 position,
    ImVec2 mouse) {
    gX_I1 wantView = (s_moveAxis == 4)
        || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyDown(ImGuiKey_V));
    if (wantView == s_dragViewPlane) return;
    s_dragViewPlane = wantView;
    s_dragPlanePoint = position;
    s_dragPlaneNormal = wantView ? gfx_camera_forward(&camera.camera3D)
        : (s_moveAxis == 2 ? gfx_camera_right(&camera.camera3D)
            : (gX_F32v3){0.0f, 1.0f, 0.0f});
    gX_F32v3 hit;
    if (!gfx_ray_plane(ray, s_dragPlanePoint, s_dragPlaneNormal, &hit)) return;
    s_dragStartHit = hit;
    s_dragStartPosition = position;
    s_dragStartMouseX = mouse.x;
    s_dragStartMouseY = mouse.y;
}

//opencode: entering independent handles mirrors the current out handles when
//   the in side was never authored, so the curve shape does not jump.
static gX_I0 gfx_path_mirror_in(const gX_F32 *tangents, gX_F32 *tangentsIn,
    gX_I32 count) {
    if (!tangents || !tangentsIn || count <= 0) return;
    for (gX_I32 i = 0; i < count * 3; i++) {
        if (tangentsIn[i] != 0.0f) return;
    }
    memcpy(tangentsIn, tangents, (size_t)count * 3 * sizeof(gX_F32));
}

//opencode: world position of a path handle (out = P + out, in = P - in),
//   honoring the mirror switch when independence is off.
static gX_I1 gfx_path_handle_position(const gX_F32 *waypoints,
    const gX_F32 *tangents, const gX_F32 *tangentsIn, gX_I1 independent,
    gX_I32 index, gX_I32 side, gX_F32v3 *outPosition) {
    if (!waypoints || !outPosition || index < 0) return 0;
    gX_F32v3 point = {waypoints[index * 3], waypoints[index * 3 + 1],
        waypoints[index * 3 + 2]};
    const gX_F32 *handle = tangents ? &tangents[index * 3] : NULL;
    if (side == 1 && independent && tangentsIn)
        handle = &tangentsIn[index * 3];
    if (!handle) return 0;
    if (side == 0) {
        *outPosition = (gX_F32v3){point.x + handle[0], point.y + handle[1],
            point.z + handle[2]};
    } else {
        *outPosition = (gX_F32v3){point.x - handle[0], point.y - handle[1],
            point.z - handle[2]};
    }
    return 1;
}

//opencode: viewport handle source for the selected object — only an active
//   Bezier waypoint path exposes grabbable handles.
static gX_I1 gfx_editor_path_handle_source(gX_RendererGFX *gfx,
    const gX_F32 **outWaypoints, const gX_F32 **outTangents,
    const gX_F32 **outTangentsIn, gX_I1 *outIndependent, gX_I32 *outCount,
    gX_I1 *outIsLight) {
    gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
    if (light && light->orbitEnabled && light->pathMode == 1
        && light->pathBezier && light->pathWaypointCount >= 2) {
        *outWaypoints = light->pathWaypoints;
        *outTangents = light->pathTangents;
        *outTangentsIn = light->pathTangentsIn;
        *outIndependent = light->pathHandlesIndependent;
        *outCount = light->pathWaypointCount;
        *outIsLight = 1;
        return 1;
    }
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    if (model && model->loaded && model->params.orbitEnabled
        && model->params.pathMode == 1 && model->params.pathBezier
        && model->params.pathWaypointCount >= 2) {
        *outWaypoints = model->params.pathWaypoints;
        *outTangents = model->params.pathTangents;
        *outTangentsIn = model->params.pathTangentsIn;
        *outIndependent = model->params.pathHandlesIndependent;
        *outCount = model->params.pathWaypointCount;
        *outIsLight = 0;
        return 1;
    }
    return 0;
}

//opencode: nearest handle within a small game-pixel radius wins over object
//   picking so authored handles stay grabbable while the model moves.
static gX_I1 gfx_editor_path_handle_pick(gX_RendererGFX *gfx, gX_F32v2 gameMouse,
    gX_I32 *outIndex, gX_I32 *outSide, gX_I1 *outIsLight,
    gX_F32v3 *outPosition) {
    const gX_F32 *waypoints = NULL, *tangents = NULL, *tangentsIn = NULL;
    gX_I1 independent = 0, isLight = 0;
    gX_I32 count = 0;
    if (!gfx_editor_path_handle_source(gfx, &waypoints, &tangents, &tangentsIn,
            &independent, &count, &isLight))
        return 0;
    if (count > gX_GFX_MODEL_WAYPOINTS) count = gX_GFX_MODEL_WAYPOINTS;
    gX_F32 best = 12.0f * 12.0f;
    gX_I32 bestIndex = -1, bestSide = -1;
    gX_F32v3 bestPosition = {0.0f, 0.0f, 0.0f};
    for (gX_I32 i = 0; i < count; i++) {
        for (gX_I32 side = 0; side < 2; side++) {
            gX_F32v3 position;
            if (!gfx_path_handle_position(waypoints, tangents, tangentsIn,
                    independent, i, side, &position))
                continue;
            gX_F32v2 screen = gX_Camera_GetWorldToScreen(position,
                camera.camera3D);
            gX_F32 dx = screen.x - gameMouse.x;
            gX_F32 dy = screen.y - gameMouse.y;
            gX_F32 distance = dx * dx + dy * dy;
            if (distance < best) {
                best = distance;
                bestIndex = i;
                bestSide = side;
                bestPosition = position;
            }
        }
    }
    if (bestIndex < 0) return 0;
    *outIndex = bestIndex;
    *outSide = bestSide;
    *outIsLight = isLight;
    *outPosition = bestPosition;
    return 1;
}

//opencode: write the dragged handle position back as a tangent; grabbing the in
//   side of a mirrored path switches it to independent handles first (mirroring
//   the out side) so the curve does not jump.
static gX_I0 gfx_editor_handle_apply(gX_RendererGFX *gfx, gX_F32v3 position) {
    if (s_dragHandleIndex < 0) return;
    const gX_F32 *waypoints = NULL, *tangents = NULL, *tangentsIn = NULL;
    gX_I1 independent = 0, isLight = 0;
    gX_I32 count = 0;
    if (!gfx_editor_path_handle_source(gfx, &waypoints, &tangents, &tangentsIn,
            &independent, &count, &isLight))
        return;
    if (s_dragHandleIndex >= count) return;
    gX_F32 *outTangent = (gX_F32 *)tangents + s_dragHandleIndex * 3;
    gX_F32 *inTangent = (gX_F32 *)tangentsIn + s_dragHandleIndex * 3;
    const gX_F32v3 point = {waypoints[s_dragHandleIndex * 3],
        waypoints[s_dragHandleIndex * 3 + 1],
        waypoints[s_dragHandleIndex * 3 + 2]};
    if (s_dragHandleSide == 1 && !independent) {
        gfx_path_mirror_in(tangents, (gX_F32 *)tangentsIn, count);
        if (isLight) {
            gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
            if (light) light->pathHandlesIndependent = 1;
        } else {
            gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
            if (model) model->params.pathHandlesIndependent = 1;
        }
    }
    if (s_dragHandleSide == 0) {
        outTangent[0] = position.x - point.x;
        outTangent[1] = position.y - point.y;
        outTangent[2] = position.z - point.z;
    } else {
        inTangent[0] = point.x - position.x;
        inTangent[1] = point.y - position.y;
        inTangent[2] = point.z - position.z;
    }
}

//opencode: path edits may happen while a motion path drives the object, which
//   skips per-frame observation — push the pre-drag state explicitly.
static gX_I0 gfx_editor_handle_commit(gX_RendererGFX *gfx) {
    gfx_model_history_ensure();
    if (s_dragHandleIsLight) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
        if (!light || !s_dragStartLightEditValid) return;
        gfx_light_history_push(s_lightUndo, &s_lightUndoCount,
            s_dragStartLightEdit);
        gfx_light_edit_capture(&s_lightHistoryCurrent, light);
        s_lightHistoryLight = s_selLight;
        s_lightHistoryValid = 1;
        s_lightRedoCount = 0;
        s_dragStartLightEditValid = 0;
    } else {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
        if (!model || !s_dragStartEditValid) return;
        gfx_model_history_push(s_modelUndo, &s_modelUndoCount, s_dragStartEdit);
        gfx_model_edit_capture(&s_modelHistoryCurrent, model);
        s_modelHistoryModel = s_selModel;
        s_modelHistoryValid = 1;
        s_modelRedoCount = 0;
        s_dragStartEditValid = 0;
    }
}

//opencode: authored-camera markers — the selected camera's position and
//   target are grabbable in the viewport (camera-facing drag plane).
static gX_I1 gfx_editor_camera_marker_pick(gX_F32v2 gameMouse, gX_I32 *outKind,
    gX_F32v3 *outPosition) {
    if (s_selectedCamera < 0 || s_selectedCamera >= s_cameraCount) return 0;
    const gX_GFX_Editor_Camera *entry = &s_cameras[s_selectedCamera];
    gX_F32 best = 12.0f * 12.0f;
    gX_I32 bestKind = 0;
    gX_F32v3 bestPosition = {0.0f, 0.0f, 0.0f};
    for (gX_I32 kind = 1; kind <= 2; kind++) {
        gX_F32v3 world = (kind == 1) ? entry->view.position
                                     : entry->view.target;
        gX_F32v2 screen = gX_Camera_GetWorldToScreen(world, camera.camera3D);
        gX_F32 dx = screen.x - gameMouse.x;
        gX_F32 dy = screen.y - gameMouse.y;
        gX_F32 distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            bestKind = kind;
            bestPosition = world;
        }
    }
    if (bestKind == 0) return 0;
    *outKind = bestKind;
    *outPosition = bestPosition;
    return 1;
}

static gX_I0 gfx_editor_camera_drag_apply(gX_F32v3 position) {
    if (s_draggingCamera == 0 || s_selectedCamera < 0
        || s_selectedCamera >= s_cameraCount) return;
    gX_GFX_Editor_Camera *entry = &s_cameras[s_selectedCamera];
    if (s_dragCameraKind == 1) entry->view.position = position;
    else entry->view.target = position;
}

//opencode: harness hook — log the selected object's handle screen/world
//   positions so gates can aim virtual-input drags at them.
static gX_I0 gfx_editor_path_trace(gX_RendererGFX *gfx) {
    const gX_F32 *waypoints = NULL, *tangents = NULL, *tangentsIn = NULL;
    gX_I1 independent = 0, isLight = 0;
    gX_I32 count = 0;
    if (!gfx_editor_path_handle_source(gfx, &waypoints, &tangents, &tangentsIn,
            &independent, &count, &isLight))
        return;
    if (count > gX_GFX_MODEL_WAYPOINTS) count = gX_GFX_MODEL_WAYPOINTS;
    for (gX_I32 i = 0; i < count; i++) {
        for (gX_I32 side = 0; side < 2; side++) {
            gX_F32v3 position;
            if (!gfx_path_handle_position(waypoints, tangents, tangentsIn,
                    independent, i, side, &position))
                continue;
            gX_F32v2 screen = gX_Camera_GetWorldToScreen(position,
                camera.camera3D);
            //opencode: the viewport may be a scaled region of the window, so
            //   report window coordinates the virtual-input bridge can use.
            gX_Rectangle viewport = gX_GUI_ViewportBox_Get();
            gX_F32 viewScale = (maindata.screenWidth > 0)
                ? viewport.width / (gX_F32)maindata.screenWidth : 1.0f;
            gX_TraceLog(gX_LOG_INFO,
                "GFX_PATHHANDLE: kind=%s index=%d side=%s independent=%d "
                "sx=%.1f sy=%.1f rx=%.1f ry=%.1f "
                "wx=%.4f wy=%.4f wz=%.4f",
                isLight ? "light" : "model", i, side == 0 ? "out" : "in",
                independent, viewport.x + screen.x * viewScale,
                viewport.y + screen.y * viewScale, screen.x, screen.y,
                position.x, position.y, position.z);
        }
    }
}

//opencode: viewport picking and keyboard-chord transform gestures. Free move
//   keeps the established world-plane drag; axis move uses a closest-line solve.
static gX_I0 gfx_editor_viewport_input(gX_RendererGFX *gfx) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse = ImGui::GetMousePos();
    gX_F32v2 gameMouse = gX_GUI_ScreenToGame(mouse.x, mouse.y);
    gX_Rectangle viewport = gX_GUI_ViewportBox_Get();
    gX_I1 inViewport = mouse.x >= viewport.x && mouse.x <= viewport.x + viewport.width
        && mouse.y >= viewport.y && mouse.y <= viewport.y + viewport.height;
    gfx_editor_camera_input(io, mouse, viewport, inViewport);
    //opencode: prefab click-to-place — the next viewport click sets the
    //   placement point on the ground plane.
    if (s_prefabPlaceArm && inViewport
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !io.WantCaptureMouse && !io.WantTextInput) {
        gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
        gX_F32v3 hit;
        if (gfx_ray_plane(ray, (gX_F32v3){0.0f, 0.0f, 0.0f},
                (gX_F32v3){0.0f, 1.0f, 0.0f}, &hit)) {
            s_prefabPlaceAt = hit;
            s_prefabPlaceInit = 1;
            s_prefabPlaceArm = 0;
            gfx_prefab_status("Placement point set", 1);
        }
    }
    //opencode: harness hook — gates aim virtual-input drags at the handles.
    const gX_I8 *pathTrace = getenv("GX_GFX_PATH_TRACE");
    if (pathTrace && pathTrace[0] && pathTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u)
        gfx_editor_path_trace(gfx);

    //opencode: camera marker drag — a camera-facing plane through the grab
    //   point keeps the marker under the cursor.
    if (s_draggingCamera) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!io.WantTextInput) {
                gX_Ray cameraRay = gX_Mouse_GetRay(gameMouse, camera.camera3D);
                gX_F32v3 hit;
                if (gfx_ray_plane(cameraRay, s_dragPlanePoint, s_dragPlaneNormal,
                        &hit)) {
                    if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                        || fabsf(mouse.y - s_dragStartMouseY) > 1.0f)
                        s_dragMoved = 1;
                    gfx_editor_camera_drag_apply(hit);
                }
            }
        } else {
            s_draggingCamera = 0;
        }
    }
    //opencode: Bezier handle drag — a camera-facing plane through the grab
    //   point keeps the handle under the cursor.
    if (s_draggingHandle) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!io.WantTextInput) {
                gX_Ray handleRay = gX_Mouse_GetRay(gameMouse, camera.camera3D);
                gX_F32v3 hit;
                if (gfx_ray_plane(handleRay, s_dragPlanePoint, s_dragPlaneNormal,
                        &hit)) {
                    if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                        || fabsf(mouse.y - s_dragStartMouseY) > 1.0f)
                        s_dragMoved = 1;
                    gfx_editor_handle_apply(gfx, hit);
                }
            }
        } else {
            if (s_dragMoved) gfx_editor_handle_commit(gfx);
            s_draggingHandle = 0;
            s_dragHandleIndex = -1;
            s_dragHandleSide = 0;
            s_dragHandleIsLight = 0;
            s_dragMoved = 0;
        }
        return;
    }

    if (s_draggingLight) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, s_dragLight);
        if (!light) {
            s_draggingLight = 0;
            s_dragMoved = 0;
            s_dragLight = -1;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!io.WantTextInput) {
                gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
                if (s_dragMode == gX_GFX_EDITOR_DRAG_AXIS) {
                    gX_F32 parameter = 0.0f;
                    gX_F32v3 axis = gfx_editor_axis_vector(s_dragAxis);
                    if (gfx_ray_axis_parameter(ray, s_dragStartPosition, axis,
                            &parameter)) {
                        gX_F32 delta = parameter - s_dragStartAxisParameter;
                        if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                            || fabsf(mouse.y - s_dragStartMouseY) > 1.0f)
                            s_dragMoved = 1;
                        light->pos = (gX_F32v3){
                            s_dragStartPosition.x + axis.x * delta,
                            s_dragStartPosition.y + axis.y * delta,
                            s_dragStartPosition.z + axis.z * delta};
                        if (io.KeyCtrl) {
                            if (s_dragAxis == 0) light->pos.x = gfx_editor_snap_value(
                                light->pos.x, s_translateSnapStep);
                            else if (s_dragAxis == 1) light->pos.y = gfx_editor_snap_value(
                                light->pos.y, s_translateSnapStep);
                            else light->pos.z = gfx_editor_snap_value(
                                light->pos.z, s_translateSnapStep);
                        }
                    }
                } else {
                    gfx_editor_view_plane_sync(ray, light->pos, mouse);
                    gX_F32v3 hit;
                    if (gfx_ray_plane(ray, s_dragPlanePoint, s_dragPlaneNormal, &hit)) {
                        gX_F32v3 delta = {hit.x - s_dragStartHit.x,
                            hit.y - s_dragStartHit.y, hit.z - s_dragStartHit.z};
                        if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                            || fabsf(mouse.y - s_dragStartMouseY) > 1.0f)
                            s_dragMoved = 1;
                        light->pos = s_dragStartPosition;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 1) light->pos.x += delta.x;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 2) light->pos.y += delta.y;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 3) light->pos.z += delta.z;
                        if (io.KeyCtrl) {
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 1)
                                light->pos.x = gfx_editor_snap_value(
                                    light->pos.x, s_translateSnapStep);
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 2)
                                light->pos.y = gfx_editor_snap_value(
                                    light->pos.y, s_translateSnapStep);
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 3)
                                light->pos.z = gfx_editor_snap_value(
                                    light->pos.z, s_translateSnapStep);
                        }
                    }
                }
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (s_dragMoved) {
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: light drag end light=%d startPos=(%.3f,%.3f,%.3f) pos=(%.3f,%.3f,%.3f)",
                    s_dragLight,
                    s_dragStartPosition.x, s_dragStartPosition.y,
                    s_dragStartPosition.z,
                    light->pos.x, light->pos.y, light->pos.z);
                //opencode: one undo entry per completed light drag.
                if (s_dragStartLightEditValid && s_lightHistoryValid
                    && s_lightHistoryLight == s_dragLight) {
                    gfx_light_history_push(s_lightUndo, &s_lightUndoCount,
                        s_dragStartLightEdit);
                    gfx_light_edit_capture(&s_lightHistoryCurrent, light);
                    s_lightRedoCount = 0;
                }
            }
            s_dragStartLightEditValid = 0;
            s_draggingLight = 0;
            s_dragMoved = 0;
            s_dragLight = -1;
            s_dragAxis = -1;
            s_dragViewPlane = 0;
            s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
        }
        return;
    }

    if (s_draggingModel) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, s_dragModel);
        if (!model) {
            s_draggingModel = 0;
            s_dragMoved = 0;
            s_dragModel = -1;
            s_dragStartEditValid = 0;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (s_dragMode == gX_GFX_EDITOR_DRAG_PLANE && !io.WantTextInput) {
                gX_I32 modifierAxis = gfx_editor_modifier_axis();
                if (!io.KeyAlt && ImGui::IsKeyDown(ImGuiKey_R)) {
                    s_dragMode = gX_GFX_EDITOR_DRAG_ROTATE;
                    s_dragAxis = modifierAxis;
                    s_dragStartPosition = model->params.pos;
                    s_dragStartRotation = model->params.rotation;
                    s_dragStartScale = model->params.scale;
                    s_dragStartMouseX = mouse.x;
                    s_dragStartMouseY = mouse.y;
                } else if (!io.KeyAlt && ImGui::IsKeyDown(ImGuiKey_S)) {
                    s_dragMode = gX_GFX_EDITOR_DRAG_SCALE;
                    s_dragAxis = modifierAxis;
                    s_dragStartPosition = model->params.pos;
                    s_dragStartRotation = model->params.rotation;
                    s_dragStartScale = model->params.scale;
                    s_dragStartMouseX = mouse.x;
                    s_dragStartMouseY = mouse.y;
                } else if (modifierAxis >= 0) {
                    gX_F32 parameter = 0.0f;
                    gX_F32v3 axis = gfx_editor_axis_vector(modifierAxis);
                    if (gfx_ray_axis_parameter(gX_Mouse_GetRay(gameMouse, camera.camera3D),
                            model->params.pos, axis, &parameter)) {
                        s_dragMode = gX_GFX_EDITOR_DRAG_AXIS;
                        s_dragAxis = modifierAxis;
                        s_dragStartPosition = model->params.pos;
                        s_dragStartAxisParameter = parameter;
                        s_dragStartMouseX = mouse.x;
                        s_dragStartMouseY = mouse.y;
                    }
                }
            }
            if ((s_dragMode == gX_GFX_EDITOR_DRAG_ROTATE
                    || s_dragMode == gX_GFX_EDITOR_DRAG_SCALE) && !io.WantTextInput) {
                gX_I32 modifierAxis = io.KeyAlt ? -1 : gfx_editor_modifier_axis();
                if (modifierAxis != s_dragAxis) {
                    s_dragAxis = modifierAxis;
                    s_dragStartPosition = model->params.pos;
                    s_dragStartRotation = model->params.rotation;
                    s_dragStartScale = model->params.scale;
                    s_dragStartMouseX = mouse.x;
                    s_dragStartMouseY = mouse.y;
                }
            }
            if (s_dragMode == gX_GFX_EDITOR_DRAG_SCALE) {
                gX_F32 deltaY = s_dragStartMouseY - mouse.y;
                if (fabsf(deltaY) > 1.0f) s_dragMoved = 1;
                gX_F32 factor = expf(deltaY * 0.01f);
                if (io.KeyCtrl) factor = gfx_editor_snap_value(factor, s_scaleSnapStep);
                if (factor < 0.1f) factor = 0.1f;
                if (factor > 100.0f) factor = 100.0f;
                model->params.scale = s_dragStartScale;
                if (s_dragAxis == 0) model->params.scale.x = gfx_editor_scale_component(
                    s_dragStartScale.x, factor);
                else if (s_dragAxis == 1) model->params.scale.y = gfx_editor_scale_component(
                    s_dragStartScale.y, factor);
                else if (s_dragAxis == 2) model->params.scale.z = gfx_editor_scale_component(
                    s_dragStartScale.z, factor);
                else {
                    model->params.scale.x = gfx_editor_scale_component(s_dragStartScale.x, factor);
                    model->params.scale.y = gfx_editor_scale_component(s_dragStartScale.y, factor);
                    model->params.scale.z = gfx_editor_scale_component(s_dragStartScale.z, factor);
                }
            } else if (s_dragMode == gX_GFX_EDITOR_DRAG_ROTATE) {
                gX_F32 deltaX = mouse.x - s_dragStartMouseX;
                gX_F32 deltaY = mouse.y - s_dragStartMouseY;
                if (fabsf(deltaX) > 1.0f || fabsf(deltaY) > 1.0f) s_dragMoved = 1;
                model->params.rotation = s_dragStartRotation;
                if (s_dragAxis == 0) model->params.rotation.x += deltaX * 0.5f;
                else if (s_dragAxis == 1) model->params.rotation.y += deltaX * 0.5f;
                else if (s_dragAxis == 2) model->params.rotation.z += deltaX * 0.5f;
                else {
                    model->params.rotation.x += deltaY * 0.5f;
                    model->params.rotation.y += deltaX * 0.5f;
                }
                if (io.KeyCtrl) {
                    if (s_dragAxis == 0) model->params.rotation.x = gfx_editor_snap_value(
                        model->params.rotation.x, s_rotationSnapStep);
                    else if (s_dragAxis == 1) model->params.rotation.y = gfx_editor_snap_value(
                        model->params.rotation.y, s_rotationSnapStep);
                    else if (s_dragAxis == 2) model->params.rotation.z = gfx_editor_snap_value(
                        model->params.rotation.z, s_rotationSnapStep);
                    else {
                        model->params.rotation.x = gfx_editor_snap_value(
                            model->params.rotation.x, s_rotationSnapStep);
                        model->params.rotation.y = gfx_editor_snap_value(
                            model->params.rotation.y, s_rotationSnapStep);
                    }
                }
            } else {
                gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
                if (s_dragMode == gX_GFX_EDITOR_DRAG_AXIS) {
                    gX_F32 parameter = 0.0f;
                    gX_F32v3 axis = gfx_editor_axis_vector(s_dragAxis);
                    if (gfx_ray_axis_parameter(ray, s_dragStartPosition, axis, &parameter)) {
                        gX_F32 delta = parameter - s_dragStartAxisParameter;
                        if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                            || fabsf(mouse.y - s_dragStartMouseY) > 1.0f) s_dragMoved = 1;
                        model->params.pos = (gX_F32v3){
                            s_dragStartPosition.x + axis.x * delta,
                            s_dragStartPosition.y + axis.y * delta,
                            s_dragStartPosition.z + axis.z * delta};
                        if (io.KeyCtrl) {
                            if (s_dragAxis == 0) model->params.pos.x = gfx_editor_snap_value(
                                model->params.pos.x, s_translateSnapStep);
                            else if (s_dragAxis == 1) model->params.pos.y = gfx_editor_snap_value(
                                model->params.pos.y, s_translateSnapStep);
                            else model->params.pos.z = gfx_editor_snap_value(
                                model->params.pos.z, s_translateSnapStep);
                        }
                    }
                } else {
                    gfx_editor_view_plane_sync(ray, model->params.pos, mouse);
                    gX_F32v3 hit;
                    if (gfx_ray_plane(ray, s_dragPlanePoint, s_dragPlaneNormal, &hit)) {
                        gX_F32v3 delta = {
                            hit.x - s_dragStartHit.x,
                            hit.y - s_dragStartHit.y,
                            hit.z - s_dragStartHit.z};
                        if (fabsf(mouse.x - s_dragStartMouseX) > 1.0f
                            || fabsf(mouse.y - s_dragStartMouseY) > 1.0f) s_dragMoved = 1;
                        model->params.pos = s_dragStartPosition;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 1) model->params.pos.x += delta.x;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 2) model->params.pos.y += delta.y;
                        if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 3) model->params.pos.z += delta.z;
                        if (io.KeyCtrl) {
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 1)
                                model->params.pos.x = gfx_editor_snap_value(
                                    model->params.pos.x, s_translateSnapStep);
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 2)
                                model->params.pos.y = gfx_editor_snap_value(
                                    model->params.pos.y, s_translateSnapStep);
                            if (s_dragViewPlane || s_moveAxis == 0 || s_moveAxis == 3)
                                model->params.pos.z = gfx_editor_snap_value(
                                    model->params.pos.z, s_translateSnapStep);
                        }
                    }
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (s_dragMoved) {
                gfx_editor_gesture_trace(model);
                //opencode: one undo entry per completed viewport drag.
                if (s_dragStartEditValid && s_modelHistoryValid
                    && s_modelHistoryModel == s_dragModel) {
                    gfx_model_history_push(s_modelUndo, &s_modelUndoCount,
                        s_dragStartEdit);
                    gfx_model_edit_capture(&s_modelHistoryCurrent, model);
                    s_modelRedoCount = 0;
                }
            }
            s_dragStartEditValid = 0;
            s_draggingModel = 0;
            s_dragMoved = 0;
            s_dragModel = -1;
            s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
            s_dragAxis = -1;
            s_dragViewPlane = 0;
        }
        return;
    }

    if (!s_pickEnabled || !inViewport || io.WantCaptureMouse
        || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    //opencode: camera markers are grabbable before path handles and object
    //   picking — the camera is explicitly selected while they show.
    gX_I32 cameraKind = 0;
    gX_F32v3 cameraMarker = {0.0f, 0.0f, 0.0f};
    if (gfx_editor_camera_marker_pick(gameMouse, &cameraKind, &cameraMarker)) {
        gX_Ray cameraRay = gX_Mouse_GetRay(gameMouse, camera.camera3D);
        s_dragPlanePoint = cameraMarker;
        s_dragPlaneNormal = gfx_camera_forward(&camera.camera3D);
        if (gfx_ray_plane(cameraRay, s_dragPlanePoint, s_dragPlaneNormal,
                &s_dragStartHit)) {
            s_draggingCamera = 1;
            s_dragCameraKind = cameraKind;
            s_dragStartMouseX = mouse.x;
            s_dragStartMouseY = mouse.y;
            s_dragMoved = 0;
        }
        return;
    }
    //opencode: Bezier handles are grabbable before object picking so path
    //   authoring does not fight selection.
    gX_I32 handleIndex = -1;
    gX_I32 handleSide = 0;
    gX_I1 handleIsLight = 0;
    gX_F32v3 handlePosition = {0.0f, 0.0f, 0.0f};
    if (gfx_editor_path_handle_pick(gfx, gameMouse, &handleIndex, &handleSide,
            &handleIsLight, &handlePosition)) {
        gX_Ray handleRay = gX_Mouse_GetRay(gameMouse, camera.camera3D);
        s_dragPlanePoint = handlePosition;
        s_dragPlaneNormal = gfx_camera_forward(&camera.camera3D);
        if (gfx_ray_plane(handleRay, s_dragPlanePoint, s_dragPlaneNormal,
                &s_dragStartHit)) {
            s_draggingHandle = 1;
            s_dragHandleIndex = handleIndex;
            s_dragHandleSide = handleSide;
            s_dragHandleIsLight = handleIsLight;
            s_dragHandleStart = handlePosition;
            s_dragStartMouseX = mouse.x;
            s_dragStartMouseY = mouse.y;
            s_dragMoved = 0;
            gfx_model_history_ensure();
            if (handleIsLight) {
                gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
                if (light) {
                    gfx_light_edit_capture(s_dragStartLightEdit, light);
                    s_dragStartLightEditValid = 1;
                }
            } else {
                gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
                if (model) {
                    gfx_model_edit_capture(s_dragStartEdit, model);
                    s_dragStartEditValid = 1;
                }
            }
            gX_TraceLog(gX_LOG_INFO, "GfxEd: handle grab %d side %d (light %d)",
                handleIndex, handleSide, handleIsLight);
            return;
        }
    }
    gX_F32 modelDistance = INFINITY;
    gX_I32 pickedModel = gX_RendererGFX_Model_Pick(gfx, gameMouse, &modelDistance);
    gX_F32 lightDistance = INFINITY;
    gX_I32 pickedLight = gfx_light_pick(gfx, gameMouse, &lightDistance);
    if (pickedLight >= 0 && (pickedModel < 0 || lightDistance < modelDistance)) {
        s_selLight = pickedLight;
        s_selModel = -1;
        gX_RendererGFX_Light *light = gfx_light_at(gfx, pickedLight);
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: viewport click %.0f,%.0f -> light %d", mouse.x, mouse.y,
            pickedLight);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: picked light %d at distance %.2f",
            pickedLight, lightDistance);
        s_dragStartPosition = light->pos;
        s_dragStartMouseX = mouse.x;
        s_dragStartMouseY = mouse.y;
        s_dragLight = pickedLight;
        s_dragAxis = io.WantTextInput ? -1 : gfx_editor_modifier_axis();
        s_dragViewPlane = 0;
        gfx_model_history_ensure();
        gfx_light_edit_capture(s_dragStartLightEdit, light);
        s_dragStartLightEditValid = 1;
        //opencode: make sure the pre-drag state is history-current before the
        //   drag suppresses per-frame observation.
        gfx_light_history_observe(pickedLight, light);
        gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
        s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
        if (s_dragAxis >= 0) {
            gX_F32 parameter = 0.0f;
            gX_F32v3 axis = gfx_editor_axis_vector(s_dragAxis);
            if (gfx_ray_axis_parameter(ray, light->pos, axis, &parameter)) {
                s_dragMode = gX_GFX_EDITOR_DRAG_AXIS;
                s_dragStartAxisParameter = parameter;
            } else {
                s_dragAxis = -1;
                return;
            }
        } else {
            s_dragViewPlane = (s_moveAxis == 4)
                || (!io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_V));
            s_dragPlanePoint = light->pos;
            s_dragPlaneNormal = s_dragViewPlane ? gfx_camera_forward(&camera.camera3D)
                : (s_moveAxis == 2 ? gfx_camera_right(&camera.camera3D)
                    : (gX_F32v3){0.0f, 1.0f, 0.0f});
            if (!gfx_ray_plane(ray, s_dragPlanePoint, s_dragPlaneNormal,
                    &s_dragStartHit)) return;
        }
        s_draggingLight = 1;
        s_dragMoved = 0;
        return;
    }
    s_selLight = -1;
    s_selModel = pickedModel;
    gX_TraceLog(gX_LOG_INFO, "GfxEd: viewport click %.0f,%.0f -> model %d", mouse.x, mouse.y, s_selModel);
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    if (!model) {
        s_selModel = -1;
        s_selLight = -1;
        return;
    }
    gX_TraceLog(gX_LOG_INFO, "GfxEd: picked model %d at distance %.2f", s_selModel, modelDistance);
    s_dragStartPosition = model->params.pos;
    s_dragStartRotation = model->params.rotation;
    s_dragStartScale = model->params.scale;
    s_dragStartMouseX = mouse.x;
    s_dragStartMouseY = mouse.y;
    s_dragModel = s_selModel;
    s_dragAxis = io.WantTextInput ? -1 : gfx_editor_modifier_axis();
    s_dragViewPlane = 0;
    gfx_model_history_ensure();
    gfx_model_edit_capture(s_dragStartEdit, model);
    s_dragStartEditValid = 1;
    //opencode: make sure the pre-drag state is history-current before the
    //   drag suppresses per-frame observation.
    gfx_model_history_observe(s_selModel, model);
    if (!io.WantTextInput && !io.KeyAlt && ImGui::IsKeyDown(ImGuiKey_R)) {
        s_dragMode = gX_GFX_EDITOR_DRAG_ROTATE;
        s_draggingModel = 1;
        s_dragMoved = 0;
        return;
    }
    if (!io.WantTextInput && !io.KeyAlt && ImGui::IsKeyDown(ImGuiKey_S)) {
        s_dragMode = gX_GFX_EDITOR_DRAG_SCALE;
        s_draggingModel = 1;
        s_dragMoved = 0;
        return;
    }
    gX_Ray ray = gX_Mouse_GetRay(gameMouse, camera.camera3D);
    if (s_dragAxis >= 0) {
        gX_F32v3 axis = gfx_editor_axis_vector(s_dragAxis);
        s_dragMode = gX_GFX_EDITOR_DRAG_AXIS;
        if (!gfx_ray_axis_parameter(ray, s_dragStartPosition, axis,
                &s_dragStartAxisParameter)) {
            s_dragModel = -1;
            s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
            s_dragAxis = -1;
            return;
        }
        s_draggingModel = 1;
        s_dragMoved = 0;
        return;
    }
    s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
    s_dragAxis = -1;
    s_dragViewPlane = (s_moveAxis == 4)
        || (!io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_V));
    s_dragPlanePoint = model->params.pos;
    s_dragPlaneNormal = s_dragViewPlane ? gfx_camera_forward(&camera.camera3D)
        : (s_moveAxis == 2 ? gfx_camera_right(&camera.camera3D)
            : (gX_F32v3){0.0f, 1.0f, 0.0f});
    if (!gfx_ray_plane(ray, s_dragPlanePoint, s_dragPlaneNormal, &s_dragStartHit)) {
        s_dragModel = -1;
        return;
    }
    s_draggingModel = 1;
    s_dragMoved = 0;
}

//opencode: expose transform snapping increments and direct position resets.
static gX_I0 gfx_model_translate_controls(gX_RendererGFX_Model *model) {
    ImGui::SeparatorText("Move");
    ImGui::TextDisabled("Click-drag moves on the selected plane.");
    ImGui::TextDisabled("Hold X/Y/Z + drag to move on that world axis.");
    ImGui::TextDisabled("Hold V + drag to move on the camera view plane.");
    ImGui::TextDisabled("Hold R + drag to rotate; R+X/Y/Z constrains it.");
    ImGui::TextDisabled("Hold S + drag to scale uniformly; S+X/Y/Z scales one axis.");
    ImGui::TextDisabled("Hold Ctrl during a drag to snap translation, rotation, or scale.");
    ImGui::Text("Drag plane");
    ImGui::RadioButton("XZ", &s_moveAxis, 0);
    ImGui::SameLine();
    ImGui::RadioButton("X", &s_moveAxis, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Y", &s_moveAxis, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Z", &s_moveAxis, 3);
    ImGui::SameLine();
    ImGui::RadioButton("View", &s_moveAxis, 4);
    ImGui::DragFloat("step", &s_moveStep, 0.01f, 0.01f, 10.0f, "%.2f");
    if (ImGui::Button("-X")) model->params.pos.x -= s_moveStep;
    ImGui::SameLine();
    if (ImGui::Button("+X")) model->params.pos.x += s_moveStep;
    ImGui::SameLine();
    if (ImGui::Button("-Y")) model->params.pos.y -= s_moveStep;
    ImGui::SameLine();
    if (ImGui::Button("+Y")) model->params.pos.y += s_moveStep;
    ImGui::SameLine();
    if (ImGui::Button("-Z")) model->params.pos.z -= s_moveStep;
    ImGui::SameLine();
    if (ImGui::Button("+Z")) model->params.pos.z += s_moveStep;
    if (ImGui::Button("Reset position")) model->params.pos = (gX_F32v3){0.0f, 0.0f, 0.0f};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("translation snap", &s_translateSnapStep, 0.05f, 0.01f, 10.0f, "%.2f");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("rotation snap", &s_rotationSnapStep, 1.0f, 1.0f, 90.0f, "%.0f deg");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("scale factor snap", &s_scaleSnapStep, 0.01f, 0.01f, 1.0f, "%.2f");
}

//opencode: provide Blender-style position, rotation, and scale reset shortcuts.
static gX_I0 gfx_editor_keyboard_input(gX_RendererGFX *gfx) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || io.WantCaptureKeyboard) return;
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) gfx_preset_save(gfx);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        s_inspectorPanelOpen = 1;
        ImGui::OpenPopup("Confirm GFX preset load");
    }
    //opencode: undo/redo targets the selected model, else the selected light;
    //   Shift+Ctrl+Z also redoes.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (model) gfx_model_history_step(s_selModel, model, io.KeyShift ? 1 : 0);
        else if (light) gfx_light_history_step(s_selLight, light, io.KeyShift ? 1 : 0);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (model) gfx_model_history_step(s_selModel, model, 1);
        else if (light) gfx_light_history_step(s_selLight, light, 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        if (model) gfx_camera_frame_model(model);
        else if (light) gfx_camera_frame_light(light);
    }
    if (io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && ImGui::IsKeyPressed(ImGuiKey_G) && model)
        model->params.pos = (gX_F32v3){0.0f, 0.0f, 0.0f};
    if (io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && ImGui::IsKeyPressed(ImGuiKey_R) && model)
        model->params.rotation = (gX_F32v3){0.0f, 0.0f, 0.0f};
    if (io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && ImGui::IsKeyPressed(ImGuiKey_S) && model)
        model->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (model) {
            gX_RendererGFX_Model_Remove(gfx, (gX_U32)s_selModel);
            s_selModel = -1;
            s_modelRevertModel = -1;
            s_modelRevertValid = 0;
            gfx_model_history_reset();
            gfx_model_snapshots_clear();
            return;
        }
        if (light) {
            gX_RendererGFX_Light_Remove(gfx, (gX_U32)s_selLight);
            s_selLight = -1;
            return;
        }
    }
    gX_F32 step = io.KeyShift ? s_moveStep * 10.0f : s_moveStep;
    if (model) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) model->params.pos.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) model->params.pos.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) model->params.pos.z -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) model->params.pos.z += step;
    } else if (light) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) light->pos.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) light->pos.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) light->pos.z -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) light->pos.z += step;
    }
}

static gX_I0 gfx_model_selection_sync(gX_RendererGFX *gfx) {
    if (s_selModel >= 0 && !gfx_model_at(gfx, s_selModel)) {
        s_selModel = -1;
        s_modelRevertModel = -1;
        s_modelRevertValid = 0;
        gfx_model_history_reset();
        gfx_model_snapshots_clear();
    }
    if (s_selModel < 0 && s_selLight < 0) {
        gfx_model_history_reset();
        for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
            if (gfx_model_at(gfx, (gX_I32)mi)) {
                s_selModel = (gX_I32)mi;
                break;
            }
        }
    }
    if (s_modelRevertModel != s_selModel || !s_modelRevertValid)
        gfx_model_revert_capture(gfx, s_selModel);
}

//opencode: opt-in headless proof for the live-only snapshot path. It mutates
//   and restores state in one frame, then clears its temporary snapshot slot.
//ai: exercise label placement through snapshots and the actual undo/redo parameter-copy path.
static gX_I0 gfx_snapshot_headless_smoke(gX_RendererGFX *gfx) {
    const char *hook = getenv("GX_GFX_SNAPSHOT_SMOKE");
    if (!hook || hook[0] == '0' || s_headlessSnapshotSmokeDone
        || maindata.frameCount < 3) return;
    s_headlessSnapshotSmokeDone = 1;
    gfx_model_selection_sync(gfx);
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    if (!model) {
        gX_TraceLog(gX_LOG_ERROR, "GFX_SNAPSHOT_SMOKE: FAIL no selected model");
        return;
    }
    gX_GFX_Model_Edit_State expectedModel;
    gX_GFX_Renderer_Edit_State expectedRenderer;
    gX_Camera3D expectedCamera = camera.camera3D;
    gfx_model_edit_capture(&expectedModel, model);
    gfx_renderer_edit_capture(&expectedRenderer, gfx);
    gfx_model_history_ensure();
    gX_I32 savedSlot = s_modelSnapshotSlot;
    gX_I8 savedName[sizeof(s_modelSnapshotName)];
    strncpy(savedName, s_modelSnapshotName, sizeof(savedName) - 1);
    savedName[sizeof(savedName) - 1] = 0;
    gX_GFX_Model_Snapshot savedSnapshot = s_modelSnapshots[0];
    s_modelSnapshotSlot = 0;
    strcpy(s_modelSnapshotName, "headless smoke");
    gfx_model_snapshot_save(gfx, model, s_selModel);
    model->params.pos.x += 0.75f;
    model->params.rotation.x += 10.0f;
    model->params.textOffset = (gX_F32v3){0.75f, 1.25f, -0.5f};
    model->params.textColor.r ^= 127;
    model->params.textWorldSize.x += 0.25f;
    model->params.textAnchorTop = !model->params.textAnchorTop;
    model->params.textShadowPixels += 0.5f;
    model->useTextures = expectedModel.useTextures ? 0 : 1;
    camera.camera3D.position.x += 0.25f;
    camera.camera3D.target.y += 0.25f;
    gfx->state.SHOW_SSR_VISUALIZER = !expectedRenderer.gfxState.SHOW_SSR_VISUALIZER;
    gfx->state.ssrDebugMode = expectedRenderer.gfxState.ssrDebugMode + 1;
    gfx->ssr.maxSteps = expectedRenderer.ssrMaxSteps + 1;
    gfx->reflection.bias = expectedRenderer.reflectionBias + 0.1f;
    renderer.state.usePP = expectedRenderer.usePP ? 0 : 1;
    gfx_ssr_normal_buffer_set(gfx, expectedRenderer.normalBufferEnabled ? 0 : 1);
    gfx_model_snapshot_restore(gfx, model, s_selModel);
    gX_GFX_Model_Edit_State actualModel;
    gX_GFX_Renderer_Edit_State actualRenderer;
    gfx_model_edit_capture(&actualModel, model);
    gfx_renderer_edit_capture(&actualRenderer, gfx);
    gX_I1 modelOK = expectedModel.params.pos.x == actualModel.params.pos.x
        && expectedModel.params.pos.y == actualModel.params.pos.y
        && expectedModel.params.pos.z == actualModel.params.pos.z
        && expectedModel.params.rotation.x == actualModel.params.rotation.x
        && expectedModel.params.textOffset.x == actualModel.params.textOffset.x
        && expectedModel.params.textOffset.y == actualModel.params.textOffset.y
        && expectedModel.params.textOffset.z == actualModel.params.textOffset.z
        && expectedModel.params.textColor.r == actualModel.params.textColor.r
        && expectedModel.params.textWorldSize.x == actualModel.params.textWorldSize.x
        && expectedModel.params.textAnchorTop == actualModel.params.textAnchorTop
        && expectedModel.params.textShadowPixels == actualModel.params.textShadowPixels
        && expectedModel.useTextures == actualModel.useTextures;
    gX_I1 cameraOK = expectedCamera.position.x == camera.camera3D.position.x
        && expectedCamera.position.y == camera.camera3D.position.y
        && expectedCamera.position.z == camera.camera3D.position.z
        && expectedCamera.target.x == camera.camera3D.target.x
        && expectedCamera.target.y == camera.camera3D.target.y
        && expectedCamera.target.z == camera.camera3D.target.z;
    gX_I1 rendererOK = expectedRenderer.gfxState.SHOW_SSR_VISUALIZER
        == actualRenderer.gfxState.SHOW_SSR_VISUALIZER
        && expectedRenderer.gfxState.ssrDebugMode == actualRenderer.gfxState.ssrDebugMode
        && expectedRenderer.usePP == actualRenderer.usePP
        && expectedRenderer.normalBufferEnabled == actualRenderer.normalBufferEnabled
        && expectedRenderer.ssrMaxSteps == actualRenderer.ssrMaxSteps
        && expectedRenderer.reflectionBias == actualRenderer.reflectionBias;
    if (!modelOK) gfx_model_edit_apply(model, &expectedModel);
    gX_I1 LabelHistoryOK = 1;
#ifndef GX_LEAN_RELEASE
    gfx_model_history_reset();
    gfx_model_history_observe(s_selModel, model);
    model->params.textOffset.x += 0.5f;
    model->params.textColor.r ^= 127;
    gfx_model_history_observe(s_selModel, model);
    gfx_model_history_step(s_selModel, model, 0);
    LabelHistoryOK = model->params.textOffset.x == expectedModel.params.textOffset.x
        && model->params.textColor.r == expectedModel.params.textColor.r;
    gfx_model_history_step(s_selModel, model, 1);
    LabelHistoryOK = LabelHistoryOK
        && model->params.textOffset.x == expectedModel.params.textOffset.x + 0.5f;
    LabelHistoryOK = LabelHistoryOK
        && model->params.textColor.r == (gX_U8)(expectedModel.params.textColor.r ^ 127);
    gfx_model_edit_apply(model, &expectedModel);
    gX_TraceLog(LabelHistoryOK ? gX_LOG_NOTICE : gX_LOG_ERROR,
        "GFX_LABEL_HISTORY: %s", LabelHistoryOK ? "PASS" : "FAIL");
#endif
    if (!cameraOK) camera.camera3D = expectedCamera;
    if (!rendererOK) gfx_renderer_edit_apply(gfx, &expectedRenderer);
    s_modelSnapshots[0] = savedSnapshot;
    s_modelSnapshotSlot = savedSlot;
    strncpy(s_modelSnapshotName, savedName, sizeof(s_modelSnapshotName) - 1);
    s_modelSnapshotName[sizeof(s_modelSnapshotName) - 1] = 0;
    gfx_model_history_reset();
    gfx_model_history_observe(s_selModel, model);
    gX_TraceLog(gX_LOG_INFO, "GFX_SNAPSHOT_SMOKE: %s model=%d camera=%d renderer=%d",
        modelOK && cameraOK && rendererOK && LabelHistoryOK ? "PASS" : "FAIL",
        modelOK, cameraOK, rendererOK);
}

//opencode: apply a model-local material preset without changing other models.
static gX_I0 gfx_model_material_preset(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_I32 preset) {
    if (!gfx || !model) return;
    model->params.usePBR = 1;
    model->params.enableReflections = preset == 1 ? 0 : 1;
    model->params.planarReflection = 0;
    model->params.nonPlanarReflection = 0;
    model->params.hasNormalMap = 0;
    model->params.injectedNormalMapID = -1;
    model->params.enableBlending = 0;
    model->params.blendOpacity = 1.0f;
    model->params.enableRefraction = 0;
    model->params.refractiveIndex = 1.5f;
    model->params.emissiveFactor = 0.0f;
    model->params.emissiveColor = (gX_Color){0, 0, 0, 255};
    if (preset == 0) {
        model->params.metalness = 1.0f;
        model->params.roughness = 0.02f;
    } else if (preset == 1) {
        model->params.metalness = 0.0f;
        model->params.roughness = 0.75f;
        model->params.albedoTint = (gX_Color){190, 205, 225, 255};
    } else if (preset == 3) {
        //opencode: Y2K glass — matches the validated showcase-y2k glass
        //   sphere (blend + refraction); clears neon emission branch-locally.
        model->params.metalness = 0.10f;
        model->params.roughness = 0.12f;
        model->params.albedoTint = (gX_Color){180, 240, 255, 255};
        model->params.emissiveFactor = 0.0f;
        model->params.emissiveColor = (gX_Color){0, 0, 0, 255};
        model->params.enableBlending = 1;
        model->params.blendOpacity = 0.34f;
        model->params.enableRefraction = 1;
        model->params.refractiveIndex = 1.5f;
    } else {
        model->params.metalness = 0.25f;
        model->params.roughness = 0.18f;
        model->params.emissiveFactor = 2.5f;
        model->params.emissiveColor = (gX_Color){255, 40, 180, 255};
        model->params.albedoTint = (gX_Color){255, 80, 220, 255};
    }
    if (model->params.enableReflections) gfx->state.ENABLE_REFLECTIONS = 1;
}

//opencode: transform a model-local vertex through the same live SRT controls as rendering.
static gX_F32v3 gfx_model_plane_transform_point(gX_RendererGFX_Model *model,
    gX_Matrix rotation, gX_F32v3 local) {
    gX_F32v3 scaled = {
        local.x * model->params.scale.x,
        local.y * model->params.scale.y,
        local.z * model->params.scale.z};
    return gfx_reflection_plane_rotate(rotation, scaled);
}

//opencode: report the signed-plane distance from the current transformed mesh center.
static gX_F32 gfx_model_reflection_plane_offset(gX_RendererGFX_Model *model) {
    if (!model || !model->loaded) return -1.0f;
    gX_Matrix rotation = gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD});
    gX_F32v3 localCenter = {0.0f, 0.0f, 0.0f};
    if (model->aabbValid) {
        localCenter = (gX_F32v3){
            (model->aabbMin[0] + model->aabbMax[0]) * 0.5f,
            (model->aabbMin[1] + model->aabbMax[1]) * 0.5f,
            (model->aabbMin[2] + model->aabbMax[2]) * 0.5f};
    }
    gX_F32v3 centerOffset = gfx_model_plane_transform_point(model, rotation, localCenter);
    gX_F32v3 worldCenter = {
        model->params.pos.x + centerOffset.x,
        model->params.pos.y + centerOffset.y,
        model->params.pos.z + centerOffset.z};
    gX_F32v3 normal;
    gX_F32 distance;
    if (!gfx_reflection_plane_world(model, &normal, &distance)) return -1.0f;
    return fabsf(gfx_vec3_dot(normal, worldCenter) - distance);
}

//opencode: preserve the current world-space plane when changing its model-follow behavior.
static gX_I0 gfx_model_reflection_plane_follow_set(gX_RendererGFX_Model *model,
    gX_I1 followsModel) {
    if (!model || model->params.reflectionPlaneFollowsModel == followsModel) return;
    gX_F32v3 worldNormal;
    gX_F32 worldDistance;
    if (!gfx_reflection_plane_world(model, &worldNormal, &worldDistance)) {
        worldNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
        worldDistance = 0.0f;
    }
    model->params.reflectionPlaneFollowsModel = followsModel;
    gfx_reflection_plane_store_world(model, worldNormal, worldDistance);
}

//opencode: least-squares PCA plane fit over transformed mesh vertices with an AABB fallback.
static gX_I1 gfx_model_generate_reflection_plane(gX_RendererGFX_Model *model) {
    if (!model || !model->loaded) return 0;
    gX_Matrix rotation = gX_Matrix_RotateXYZ((gX_F32v3){
        model->params.rotation.x * gX_DEG2RAD,
        model->params.rotation.y * gX_DEG2RAD,
        model->params.rotation.z * gX_DEG2RAD});
    gX_F32 sum[3] = {0.0f, 0.0f, 0.0f};
    gX_U32 vertexCount = 0;
    if (model->model.meshes && model->model.meshCount > 0) {
        for (gX_I32 mi = 0; mi < model->model.meshCount; mi++) {
            gX_Mesh *mesh = &model->model.meshes[mi];
            if (!mesh->vertices || mesh->vertexCount <= 0) continue;
            for (gX_I32 vi = 0; vi < mesh->vertexCount; vi++) {
                gX_F32v3 point = gfx_model_plane_transform_point(model, rotation,
                    (gX_F32v3){mesh->vertices[vi * 3], mesh->vertices[vi * 3 + 1],
                        mesh->vertices[vi * 3 + 2]});
                sum[0] += point.x;
                sum[1] += point.y;
                sum[2] += point.z;
                vertexCount++;
            }
        }
    }
    if (vertexCount > 0) {
        gX_F32 mean[3] = {
            sum[0] / (gX_F32)vertexCount,
            sum[1] / (gX_F32)vertexCount,
            sum[2] / (gX_F32)vertexCount};
        gX_F32 covariance[3][3] = {{0.0f}};
        for (gX_I32 mi = 0; mi < model->model.meshCount; mi++) {
            gX_Mesh *mesh = &model->model.meshes[mi];
            if (!mesh->vertices || mesh->vertexCount <= 0) continue;
            for (gX_I32 vi = 0; vi < mesh->vertexCount; vi++) {
                gX_F32v3 point = gfx_model_plane_transform_point(model, rotation,
                    (gX_F32v3){mesh->vertices[vi * 3], mesh->vertices[vi * 3 + 1],
                        mesh->vertices[vi * 3 + 2]});
                gX_F32 delta[3] = {point.x - mean[0], point.y - mean[1], point.z - mean[2]};
                for (gX_I32 row = 0; row < 3; row++)
                    for (gX_I32 column = 0; column < 3; column++)
                        covariance[row][column] += delta[row] * delta[column]
                            / (gX_F32)vertexCount;
            }
        }
        gX_F32 eigenvectors[3][3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}};
        for (gX_I32 iteration = 0; iteration < 24; iteration++) {
            gX_I32 p = 0;
            gX_I32 q = 1;
            gX_F32 offDiagonal = fabsf(covariance[p][q]);
            if (fabsf(covariance[0][2]) > offDiagonal) {
                p = 0;
                q = 2;
                offDiagonal = fabsf(covariance[p][q]);
            }
            if (fabsf(covariance[1][2]) > offDiagonal) {
                p = 1;
                q = 2;
                offDiagonal = fabsf(covariance[p][q]);
            }
            if (offDiagonal < 0.000001f) break;
            gX_F32 angle = 0.5f * atan2f(2.0f * covariance[p][q],
                covariance[q][q] - covariance[p][p]);
            gX_F32 cosine = cosf(angle);
            gX_F32 sine = sinf(angle);
            gX_F32 pp = covariance[p][p];
            gX_F32 qq = covariance[q][q];
            gX_F32 pq = covariance[p][q];
            for (gX_I32 axis = 0; axis < 3; axis++) {
                if (axis == p || axis == q) continue;
                gX_F32 ap = covariance[axis][p];
                gX_F32 aq = covariance[axis][q];
                covariance[axis][p] = cosine * ap - sine * aq;
                covariance[p][axis] = covariance[axis][p];
                covariance[axis][q] = sine * ap + cosine * aq;
                covariance[q][axis] = covariance[axis][q];
            }
            covariance[p][p] = cosine * cosine * pp - 2.0f * sine * cosine * pq
                + sine * sine * qq;
            covariance[q][q] = sine * sine * pp + 2.0f * sine * cosine * pq
                + cosine * cosine * qq;
            covariance[p][q] = 0.0f;
            covariance[q][p] = 0.0f;
            for (gX_I32 axis = 0; axis < 3; axis++) {
                gX_F32 vp = eigenvectors[axis][p];
                gX_F32 vq = eigenvectors[axis][q];
                eigenvectors[axis][p] = cosine * vp - sine * vq;
                eigenvectors[axis][q] = sine * vp + cosine * vq;
            }
        }
        gX_I32 minAxis = 0;
        gX_I32 maxAxis = 0;
        for (gX_I32 axis = 1; axis < 3; axis++) {
            if (covariance[axis][axis] < covariance[minAxis][minAxis]) minAxis = axis;
            if (covariance[axis][axis] > covariance[maxAxis][maxAxis]) maxAxis = axis;
        }
        gX_F32 maxEigenvalue = covariance[maxAxis][maxAxis];
        if (maxEigenvalue > 0.000001f) {
            gX_F32 minEigenvalue = fmaxf(0.0f, covariance[minAxis][minAxis]);
            gX_F32v3 normal = gfx_vec3_normalize((gX_F32v3){
                eigenvectors[0][minAxis],
                eigenvectors[1][minAxis],
                eigenvectors[2][minAxis]});
            gX_F32v3 worldCenter = {
                model->params.pos.x + mean[0],
                model->params.pos.y + mean[1],
                model->params.pos.z + mean[2]};
            gX_F32v3 toCamera = {
                camera.camera3D.position.x - worldCenter.x,
                camera.camera3D.position.y - worldCenter.y,
                camera.camera3D.position.z - worldCenter.z};
            if (gfx_vec3_dot(normal, toCamera) < 0.0f)
                normal = (gX_F32v3){-normal.x, -normal.y, -normal.z};
            gfx_reflection_plane_store_world(model, normal,
                gfx_vec3_dot(normal, worldCenter));
            s_planarFitModel = s_selModel;
            s_planarFitRatio = sqrtf(minEigenvalue / maxEigenvalue);
            return 1;
        }
    }
    gX_F32 bounds[3] = {2.0f, 2.0f, 2.0f};
    gX_F32 center[3] = {0.0f, 0.0f, 0.0f};
    if (model->aabbValid) {
        for (gX_I32 axis = 0; axis < 3; axis++) {
            bounds[axis] = model->aabbMax[axis] - model->aabbMin[axis];
            center[axis] = (model->aabbMax[axis] + model->aabbMin[axis]) * 0.5f;
        }
    }
    gX_F32 extents[3] = {
        fabsf(bounds[0] * model->params.scale.x),
        fabsf(bounds[1] * model->params.scale.y),
        fabsf(bounds[2] * model->params.scale.z)};
    gX_I32 minAxis = 0;
    gX_I32 maxAxis = 0;
    for (gX_I32 axis = 1; axis < 3; axis++) {
        if (extents[axis] < extents[minAxis]) minAxis = axis;
        if (extents[axis] > extents[maxAxis]) maxAxis = axis;
    }
    gX_F32v3 localNormal = minAxis == 0
        ? (gX_F32v3){1.0f, 0.0f, 0.0f}
        : (minAxis == 1 ? (gX_F32v3){0.0f, 1.0f, 0.0f}
            : (gX_F32v3){0.0f, 0.0f, 1.0f});
    gX_F32 scaleAxis = minAxis == 0 ? model->params.scale.x
        : (minAxis == 1 ? model->params.scale.y : model->params.scale.z);
    if (fabsf(scaleAxis) < 0.000001f) scaleAxis = 1.0f;
    gX_F32v3 inverseScaledNormal = {
        localNormal.x / scaleAxis,
        localNormal.y / scaleAxis,
        localNormal.z / scaleAxis};
    gX_F32v3 normal = gfx_vec3_normalize((gX_F32v3){
        rotation.m0 * inverseScaledNormal.x + rotation.m4 * inverseScaledNormal.y
            + rotation.m8 * inverseScaledNormal.z,
        rotation.m1 * inverseScaledNormal.x + rotation.m5 * inverseScaledNormal.y
            + rotation.m9 * inverseScaledNormal.z,
        rotation.m2 * inverseScaledNormal.x + rotation.m6 * inverseScaledNormal.y
            + rotation.m10 * inverseScaledNormal.z});
    gX_F32v3 centerOffset = gfx_model_plane_transform_point(model, rotation,
        (gX_F32v3){center[0], center[1], center[2]});
    gX_F32v3 worldCenter = {
        model->params.pos.x + centerOffset.x,
        model->params.pos.y + centerOffset.y,
        model->params.pos.z + centerOffset.z};
    gX_F32v3 toCamera = {
        camera.camera3D.position.x - worldCenter.x,
        camera.camera3D.position.y - worldCenter.y,
        camera.camera3D.position.z - worldCenter.z};
    if (gfx_vec3_dot(normal, toCamera) < 0.0f)
        normal = (gX_F32v3){-normal.x, -normal.y, -normal.z};
    gfx_reflection_plane_store_world(model, normal,
        gfx_vec3_dot(normal, worldCenter));
    s_planarFitModel = s_selModel;
    s_planarFitRatio = extents[maxAxis] > 0.000001f
        ? sqrtf(extents[minAxis] / extents[maxAxis]) : 1.0f;
    return 0;
}

//opencode: choose whether the selected model samples environment or generated planar reflection.
static gX_I0 gfx_model_reflection_mode_set(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model, gX_I32 mode) {
    if (!gfx || !model) return;
    if (mode < 0 || mode > 5) mode = 0;
    model->params.enableReflections = mode == 1 ? 1 : 0;
    model->params.planarReflection = mode >= 2 ? 1 : 0;
    model->params.nonPlanarReflection = mode == 4 || mode == 5 ? 1 : 0;
    //opencode: Off must clear the separate per-model SSR receiver as well.
    model->params.receiveSSR = mode == 2 || mode == 4 ? 1 : 0;
    if (mode == 2 || mode == 4) gfx->state.ENABLE_SSR = 1;
    if (mode == 1) gfx->state.ENABLE_REFLECTIONS = 1;
    if (mode >= 2) gfx->state.ENABLE_PLANAR_REFLECTIONS = 1;
    if (mode >= 2) gfx_model_generate_reflection_plane(model);
}

//opencode: flip a fitted plane's front-facing normal without moving its plane equation.
static gX_I0 gfx_model_reflection_normal_flip(gX_RendererGFX_Model *model) {
    if (!model) return;
    model->params.reflectionNormal = (gX_F32v3){
        -model->params.reflectionNormal.x,
        -model->params.reflectionNormal.y,
        -model->params.reflectionNormal.z};
    model->params.reflectionPlaneHeight = -model->params.reflectionPlaneHeight;
}

//opencode: combine the polished mirror material with a mesh-fitted planar reflection.
static gX_I0 gfx_model_make_planar_mirror(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model) {
    if (!gfx || !model) return;
    gX_I1 environmentEnabled = gfx->state.ENABLE_REFLECTIONS;
    gfx_model_material_preset(gfx, model, 0);
    gfx->state.ENABLE_REFLECTIONS = environmentEnabled;
    gfx_model_reflection_mode_set(gfx, model, 3);
}

//opencode: searchable authoring stays compact in the inspector and exposes the mirror actions clearly.
static gX_I0 gfx_model_material_browser(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model) {
    if (ImGui::Button("Browse materials...")) {
        s_materialSearch[0] = 0;
        ImGui::OpenPopup("GFX Material Browser");
    }
    if (!ImGui::BeginPopupModal("GFX Material Browser", NULL,
            ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("Apply a material to model %d only. Reflection presets enable the global renderer gate but do not mark other models reflective.",
        s_selModel);
    ImGui::InputTextWithHint("##gfxMaterialSearch", "Search material presets...",
        s_materialSearch, sizeof(s_materialSearch));
    static const gX_I8 *names[] = {"Mirror", "Matte", "Neon", "Glass"};
    static const gX_I8 *descriptions[] = {
        "Polished metal with the current albedo tint and environment reflection.",
        "Soft, non-reflective PBR surface.",
        "Pink emissive PBR surface with environment reflection.",
        "Cyan tinted blend with refraction and environment reflection."};
    static const ImVec4 swatches[] = {
        ImVec4(0.78f, 0.82f, 0.88f, 1.0f),
        ImVec4(0.55f, 0.62f, 0.72f, 1.0f),
        ImVec4(1.0f, 0.12f, 0.65f, 1.0f),
        ImVec4(0.45f, 0.88f, 1.0f, 0.55f)};
    if (ImGui::BeginTable("GfxMaterialCards", 2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH
                | ImGuiTableFlags_SizingStretchSame)) {
        gX_U32 matching = 0;
        for (gX_I32 preset = 0; preset < 4; preset++) {
            if (!gfx_text_search_matches(names[preset], s_materialSearch)
                && !gfx_text_search_matches(descriptions[preset], s_materialSearch)) continue;
            matching++;
            ImGui::TableNextColumn();
            ImGui::PushID(preset);
            ImGui::ColorButton("##materialSwatch", swatches[preset],
                ImGuiColorEditFlags_NoTooltip, ImVec2(34.0f, 34.0f));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("%s", names[preset]);
            ImGui::TextWrapped("%s", descriptions[preset]);
            if (ImGui::Button("Apply material")) {
                gfx_model_material_preset(gfx, model, preset);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (matching == 0) ImGui::TextDisabled("No material presets match this search.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("Planar mirror fits a best-fit plane through the selected model's transformed mesh and uses a polished reflective material.");
    ImGui::BeginDisabled(!model->loaded);
    if (ImGui::Button("Make planar mirror")) {
        gfx_model_make_planar_mirror(gfx, model);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (!model->loaded) ImGui::TextDisabled("Wait for the model mesh to finish loading.");
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

//opencode: texture injection is deliberately explicit so loading a normal map
//   never changes a model until the user presses the button.
static gX_I0 gfx_model_inject_normal(gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    if (!gfx_preset_file_exists(s_normalTexturePath)) return;
    gX_I32 textureID = gX_RendererGFX_Texture_Add(gfx, s_normalTexturePath);
    if (textureID >= 0) {
        model->params.injectedNormalMapID = textureID;
        model->params.hasNormalMap = 1;
    }
}

//opencode: assign any of the renderer's eight standard material maps from the
//   shared texture heap, preserving the path and slot in the scene preset.
static gX_I0 gfx_model_texture_slots_panel(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model) {
    if (!ImGui::CollapsingHeader("Texture slots")) return;
    int slot = s_selectedMapSlot;
    if (slot < 0) slot = 0;
    if (slot >= gX_GFX_MODEL_TEXTURE_SLOTS) slot = gX_GFX_MODEL_TEXTURE_SLOTS - 1;
    if (ImGui::SliderInt("slot", &slot, 0, gX_GFX_MODEL_TEXTURE_SLOTS - 1))
        s_selectedMapSlot = slot;
    ImGui::PushID(slot);
    int purpose = model->texturePurpose[slot];
    if (purpose < 0 || purpose >= gX_GFX_MAX_MATERIAL_MAPS) purpose = gX_MATERIAL_MAP_ALBEDO;
    if (ImGui::Combo("map role", &purpose, s_materialMapNames,
            gX_GFX_MAX_MATERIAL_MAPS)) {
        model->texturePurpose[slot] = purpose;
        gfx_model_map_mark_present(model, purpose);
    }
    gfx_texture_combo("texture", gfx, &model->textureID[slot],
        gX_GFX_ASSET_TARGET_MODEL_SLOT, s_selModel, slot, purpose);
    ImGui::InputText("asset path", s_normalTexturePath, sizeof(s_normalTexturePath));
    if (ImGui::Button("Load path into slot")) {
        gX_I32 textureID = gfx_texture_find_or_add(gfx, s_normalTexturePath);
        if (textureID >= 0) {
            model->textureID[slot] = textureID;
            model->texturePurpose[slot] = purpose;
            gfx_model_map_mark_present(model, purpose);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear slot")) {
        model->textureID[slot] = -1;
        model->texturePurpose[slot] = -1;
    }
    ImGui::TextDisabled("%u textures in scene preset", gfx->textures.amount);
    ImGui::PopID();
}

//opencode: map factors and UV transforms cover the map controls already stored
//   in Model_Parameters and therefore round-trip with the scene preset.
static gX_I0 gfx_model_map_tuning_panel(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model) {
    if (!ImGui::CollapsingHeader("Map tuning")) return;
    gX_I1 albedo = model->params.hasAlbedoMap ? 1 : 0;
    if (ImGui::Checkbox("albedo map", (bool *)&albedo)) model->params.hasAlbedoMap = albedo;
    ImGui::DragFloat("albedo factor", &model->params.albedoMapFactor, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat2("albedo UV scale", (float *)&model->params.albedoMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("albedo UV offset", (float *)&model->params.albedoMapOffset, 0.01f, -10.0f, 10.0f);

    gX_I1 metalness = model->params.hasMetalnessMap ? 1 : 0;
    if (ImGui::Checkbox("metalness map", (bool *)&metalness)) model->params.hasMetalnessMap = metalness;
    ImGui::DragFloat("metalness factor", &model->params.metalnessMapFactor, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat2("metalness UV scale", (float *)&model->params.metalnessMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("metalness UV offset", (float *)&model->params.metalnessMapOffset, 0.01f, -10.0f, 10.0f);

    gX_I1 roughness = model->params.hasRoughnessMap ? 1 : 0;
    if (ImGui::Checkbox("roughness map", (bool *)&roughness)) model->params.hasRoughnessMap = roughness;
    ImGui::DragFloat("roughness factor", &model->params.roughnessMapFactor, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat2("roughness UV scale", (float *)&model->params.roughnessMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("roughness UV offset", (float *)&model->params.roughnessMapOffset, 0.01f, -10.0f, 10.0f);

    gX_I1 ao = model->params.hasAoMap ? 1 : 0;
    if (ImGui::Checkbox("occlusion map", (bool *)&ao)) model->params.hasAoMap = ao;
    ImGui::DragFloat("occlusion map factor", &model->params.aoMapFactor, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat2("occlusion UV scale", (float *)&model->params.aoMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("occlusion UV offset", (float *)&model->params.aoMapOffset, 0.01f, -10.0f, 10.0f);

    gX_I1 height = model->params.hasHeightMap ? 1 : 0;
    if (ImGui::Checkbox("height map", (bool *)&height)) model->params.hasHeightMap = height;
    ImGui::DragFloat("height factor", &model->params.heightMapFactor, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat2("height UV scale", (float *)&model->params.heightMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("height UV offset", (float *)&model->params.heightMapOffset, 0.01f, -10.0f, 10.0f);
    const gX_I8 *parallaxModes[] = { "Simple", "Steep + POM", "POM + self-shadow" };
    int parallaxMode = model->params.parallaxMode;
    if (parallaxMode < 0 || parallaxMode > 2) parallaxMode = 2;
    if (ImGui::Combo("parallax mode", &parallaxMode, parallaxModes, 3))
        model->params.parallaxMode = parallaxMode;

    gX_I1 emission = model->params.hasEmissiveMap ? 1 : 0;
    if (ImGui::Checkbox("emission map", (bool *)&emission)) model->params.hasEmissiveMap = emission;
    ImGui::DragFloat("emission map factor", &model->params.emissiveMapFactor, 0.02f, 0.0f, 8.0f);
    ImGui::DragFloat2("emission UV scale", (float *)&model->params.emissiveMapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("emission UV offset", (float *)&model->params.emissiveMapOffset, 0.01f, -10.0f, 10.0f);

    gX_I1 gloss = model->params.hasGlossMap ? 1 : 0;
    if (ImGui::Checkbox("gloss map", (bool *)&gloss)) model->params.hasGlossMap = gloss;
    ImGui::DragFloat("gloss factor", &model->params.glossMapFactor, 0.01f, 0.0f, 2.0f);
    gfx_texture_combo("gloss texture", gfx, &model->params.glossMapID,
        gX_GFX_ASSET_TARGET_MODEL_GLOSS, s_selModel, -1, -1);

    gX_I1 lightmap = model->params.hasLightmap ? 1 : 0;
    if (ImGui::Checkbox("static lightmap", (bool *)&lightmap)) model->params.hasLightmap = lightmap;
    ImGui::DragFloat("lightmap strength", &model->params.lightmapStrength, 0.01f, 0.0f, 8.0f);
    ImGui::DragFloat2("lightmap UV scale", (float *)&model->params.lightmapScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat2("lightmap UV offset", (float *)&model->params.lightmapOffset, 0.01f, -10.0f, 10.0f);
    gfx_texture_combo("lightmap texture", gfx, &model->params.lightmapID,
        gX_GFX_ASSET_TARGET_MODEL_LIGHTMAP, s_selModel, -1, -1);
}

//opencode: the material library scans only its dedicated directory and file
//   extension; complete scene presets remain outside this browser.
static gX_I0 gfx_material_assets_scan(gX_I0) {
    if (s_materialAssetFiles.paths) UnloadDirectoryFiles(s_materialAssetFiles);
    s_materialAssetFiles = (FilePathList){0};
    if (!DirectoryExists("resources/tools/materials")
        && MakeDirectory("resources/tools/materials") != 0) return;
    s_materialAssetFiles = LoadDirectoryFilesEx("resources/tools/materials", "FILES*", false);
    if (s_materialAssetFiles.count > 1)
        qsort(s_materialAssetFiles.paths, s_materialAssetFiles.count,
            sizeof(s_materialAssetFiles.paths[0]), gfx_preset_asset_compare);
    gX_U32 validCount = 0;
    for (gX_U32 ai = 0; ai < s_materialAssetFiles.count; ai++) {
        const gX_I8 *path = s_materialAssetFiles.paths[ai];
        if (!gfx_material_asset_path_valid(path) || !gfx_preset_file_exists(path)) continue;
        s_materialAssetFiles.paths[validCount++] = s_materialAssetFiles.paths[ai];
    }
    s_materialAssetFiles.count = validCount;
}

//opencode: release the transient asset file list when the editor closes.
static gX_I0 gfx_material_assets_release(gX_I0) {
    if (s_materialAssetFiles.paths) UnloadDirectoryFiles(s_materialAssetFiles);
    s_materialAssetFiles = (FilePathList){0};
    s_materialAssetSelectedPath[0] = 0;
}

//opencode: browse reusable surface files without exposing unrelated tools or
//   renderer presets to the material-override workflow.
static gX_I0 gfx_material_asset_browser_popup(
    gX_RendererGFX_MaterialOverride *target) {
    if (ImGui::Button("Browse material assets...")) {
        gfx_material_assets_scan();
        snprintf(s_materialAssetSelectedPath, sizeof(s_materialAssetSelectedPath), "%s",
            s_materialAssetPath);
        ImGui::OpenPopup("GFX Material Assets");
    }
    if (!ImGui::BeginPopupModal("GFX Material Assets", NULL,
            ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextDisabled("Reusable per-material surface assets");
    ImGui::BeginChild("GfxMaterialAssetFiles", ImVec2(520.0f, 240.0f), true);
    gX_U32 matching = 0;
    for (gX_U32 ai = 0; ai < s_materialAssetFiles.count; ai++) {
        const gX_I8 *path = s_materialAssetFiles.paths[ai];
        matching++;
        gX_I1 selected = strcmp(s_materialAssetSelectedPath, path) == 0;
        if (ImGui::Selectable(path, selected))
            snprintf(s_materialAssetSelectedPath, sizeof(s_materialAssetSelectedPath), "%s", path);
    }
    if (matching == 0) ImGui::TextDisabled("No material assets found. Save an override to create one.");
    ImGui::EndChild();
    ImGui::TextWrapped("Selected: %s", s_materialAssetSelectedPath[0]
        ? s_materialAssetSelectedPath : "none");
    gX_I1 pathFits = s_materialAssetSelectedPath[0]
        && strlen(s_materialAssetSelectedPath) < sizeof(s_materialAssetPath);
    ImGui::BeginDisabled(!pathFits);
    if (ImGui::Button("Use path")) {
        snprintf(s_materialAssetPath, sizeof(s_materialAssetPath), "%s",
            s_materialAssetSelectedPath);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!target);
    if (ImGui::Button("Load and apply")) {
        if (gfx_material_asset_load_apply(s_materialAssetSelectedPath, target))
            gfx_material_asset_set_status("Material asset applied; reload the model to update its meshes", 1);
        else
            gfx_material_asset_set_status("Material asset load failed or file is invalid", 0);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) gfx_material_assets_scan();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

//opencode: save the chosen per-model override as a reusable surface asset.
static gX_I0 gfx_material_asset_save_override(
    const gX_RendererGFX_MaterialOverride *source) {
    if (!source) {
        gfx_material_asset_set_status("Select a material override first", 0);
        return;
    }
    if (!gfx_material_asset_path_valid(s_materialAssetPath)) {
        gfx_material_asset_set_status("Asset path must end in .material.json", 0);
        return;
    }
    if (!gfx_material_asset_save(source, s_materialAssetName,
            s_materialAssetDescription, s_materialAssetPath)) {
        gfx_material_asset_set_status("Material asset save failed", 0);
        return;
    }
    gfx_material_assets_scan();
    gfx_material_asset_set_status("Reusable material asset saved", 1);
}

//opencode: record the last ImGui item's screen rect as (x0,y0,x1,y1) for the
//   pose-editor harness trace.
static gX_I0 gfx_item_rect4(gX_F32 *out) {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    out[0] = min.x;
    out[1] = min.y;
    out[2] = max.x;
    out[3] = max.y;
}

//opencode: sidecar material overrides can be authored and persisted in the
//   editor; reload the model to rebuild its per-mesh resolved materials.
static gX_I0 gfx_model_overrides_panel(gX_RendererGFX *gfx,
    gX_RendererGFX_Model *model) {
    //opencode: harness hook — expand the section and scroll it into view so
    //   virtual-input gates can reach the map Browse buttons.
    const gX_I8 *focusHook = getenv("GX_GFX_MATMAP_FOCUS");
    gX_I1 focus = (focusHook && focusHook[0] && focusHook[0] != '0') ? 1 : 0;
    if (focus) ImGui::SetNextItemOpen(1, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader("Per-material overrides")) return;
    if (focus) ImGui::SetScrollHereY(0.0f);
    if (model->params.materialOverrideCount < 0) model->params.materialOverrideCount = 0;
    if (model->params.materialOverrideCount > gX_GFX_MATERIAL_OVERRIDES)
        model->params.materialOverrideCount = gX_GFX_MATERIAL_OVERRIDES;
    if (ImGui::Button("Add material override")
        && model->params.materialOverrideCount < gX_GFX_MATERIAL_OVERRIDES) {
        gX_RendererGFX_MaterialOverride *entry = &model->params.materialOverrides[
            model->params.materialOverrideCount++];
        s_selectedMaterialOverride = model->params.materialOverrideCount - 1;
        memset(entry, 0, sizeof(*entry));
        entry->albedo = (gX_Color){255, 255, 255, 255};
        entry->roughness = -1.0f;
        entry->metalness = -1.0f;
        entry->albedoMapID = -1;
        entry->emissiveMapID = -1;
    }
    gX_I32 removeIndex = -1;
    for (gX_I32 oi = 0; oi < model->params.materialOverrideCount
        && oi < gX_GFX_MATERIAL_OVERRIDES; oi++) {
        gX_RendererGFX_MaterialOverride *entry = &model->params.materialOverrides[oi];
        ImGui::PushID(oi);
        ImGui::Separator();
        bool assetSource = s_selectedMaterialOverride == oi;
        if (ImGui::RadioButton("Asset source", assetSource)) s_selectedMaterialOverride = oi;
        ImGui::SameLine();
        ImGui::TextDisabled("Override %d", oi + 1);
        ImGui::InputText("material name", entry->material, sizeof(entry->material));
        gfx_color_edit("albedo", &entry->albedo);
        ImGui::DragFloat("roughness", &entry->roughness, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("metalness", &entry->metalness, 0.01f, -1.0f, 1.0f);
        gfx_color_edit("emissive", &entry->emissiveColor);
        ImGui::DragFloat("emissive strength", &entry->emissive, 0.02f, 0.0f, 16.0f);
        //opencode: per-material map browsing — pick the texture from the asset
        //   browser grid (paths persist; X clears the binding).
        gX_F32 browseAlbedoRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gX_F32 browseEmissiveRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        //opencode: constrain the path fields so the Browse/clear buttons stay
        //   inside the panel.
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("albedo map path", entry->albedoMap, sizeof(entry->albedoMap));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##matmap_albedo"))
            gfx_asset_browser_open_material_map(gfx, s_selModel, oi, 0);
        gfx_item_rect4(browseAlbedoRect);
        ImGui::SameLine();
        if (ImGui::SmallButton("X##matmap_albedo_clear")) {
            entry->albedoMap[0] = 0;
            entry->albedoMapID = -1;
        }
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("emissive map path", entry->emissiveMap, sizeof(entry->emissiveMap));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##matmap_emissive"))
            gfx_asset_browser_open_material_map(gfx, s_selModel, oi, 1);
        gfx_item_rect4(browseEmissiveRect);
        ImGui::SameLine();
        if (ImGui::SmallButton("X##matmap_emissive_clear")) {
            entry->emissiveMap[0] = 0;
            entry->emissiveMapID = -1;
        }
        //opencode: harness hook — the material-map gate aims a virtual-input
        //   click at the Browse buttons from these rects.
        const gX_I8 *mapTrace = getenv("GX_MATMAP_UI_TRACE");
        if (mapTrace && mapTrace[0] && mapTrace[0] != '0'
            && (ImGui::GetFrameCount() % 30u) == 0u) {
            gX_TraceLog(gX_LOG_INFO,
                "GFX_MATMAP: override=%d albedo=(%.0f,%.0f,%.0f,%.0f) "
                "emissive=(%.0f,%.0f,%.0f,%.0f)",
                oi, browseAlbedoRect[0], browseAlbedoRect[1],
                browseAlbedoRect[2], browseAlbedoRect[3], browseEmissiveRect[0],
                browseEmissiveRect[1], browseEmissiveRect[2],
                browseEmissiveRect[3]);
        }
        if (ImGui::Button("Remove override")) removeIndex = oi;
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        for (gX_I32 oi = removeIndex; oi + 1 < model->params.materialOverrideCount; oi++)
            model->params.materialOverrides[oi] = model->params.materialOverrides[oi + 1];
        model->params.materialOverrideCount--;
        memset(&model->params.materialOverrides[model->params.materialOverrideCount], 0,
            sizeof(model->params.materialOverrides[0]));
        if (s_selectedMaterialOverride > removeIndex) s_selectedMaterialOverride--;
        if (s_selectedMaterialOverride >= model->params.materialOverrideCount)
            s_selectedMaterialOverride = model->params.materialOverrideCount - 1;
    }
    if (model->params.materialOverrideCount <= 0) s_selectedMaterialOverride = 0;
    if (s_selectedMaterialOverride < 0
        || s_selectedMaterialOverride >= model->params.materialOverrideCount)
        s_selectedMaterialOverride = model->params.materialOverrideCount > 0 ? 0 : -1;
    gX_RendererGFX_MaterialOverride *assetTarget = s_selectedMaterialOverride >= 0
        ? &model->params.materialOverrides[s_selectedMaterialOverride] : NULL;
    ImGui::SeparatorText("Reusable material asset");
    if (assetTarget) {
        ImGui::TextWrapped("Asset source: %s", assetTarget->material[0]
            ? assetTarget->material : "unnamed material override");
    } else {
        ImGui::TextDisabled("Add a material override to create or apply a material asset.");
    }
    ImGui::InputText("asset name", s_materialAssetName, sizeof(s_materialAssetName));
    ImGui::InputText("asset description", s_materialAssetDescription,
        sizeof(s_materialAssetDescription));
    ImGui::InputText("asset path", s_materialAssetPath, sizeof(s_materialAssetPath));
    ImGui::BeginDisabled(!assetTarget);
    if (ImGui::Button("Save selected override as asset"))
        gfx_material_asset_save_override(assetTarget);
    ImGui::SameLine();
    if (ImGui::Button("Load and apply asset")) {
        if (gfx_material_asset_load_apply(s_materialAssetPath, assetTarget))
            gfx_material_asset_set_status("Material asset applied; reload the model to update its meshes", 1);
        else
            gfx_material_asset_set_status("Material asset load failed or file is invalid", 0);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    gfx_material_asset_browser_popup(assetTarget);
    if (s_materialAssetStatus[0]) {
        ImVec4 statusColor = s_materialAssetStatusOK
            ? ImVec4(0.45f, 0.85f, 0.52f, 1.0f)
            : ImVec4(1.0f, 0.42f, 0.35f, 1.0f);
        ImGui::TextColored(statusColor, "%s", s_materialAssetStatus);
    }
    ImGui::TextDisabled("Changes apply after reloading the model.");
    if (ImGui::Button("Reload model and apply overrides")) gfx_model_reload(gfx, model);
}

//opencode: the normal target can be enabled after editor startup without an
//   environment variable; the next renderer update performs the deferred load.
static gX_I0 gfx_ssr_normal_buffer_set(gX_RendererGFX *gfx, gX_I1 enabled) {
    gfx->ssr.normalBufferEnabled = enabled;
    gfx->ssr.normalPass = 0;
    if (enabled) {
        gfx->ssr.normalMap.wrapper.loadingState.loadAsap = 1;
    } else {
        gfx->ssr.normalMap.wrapper.loadingState.loadAsap = 0;
        if (gfx->ssr.normalMap.wrapper.loadingState.loaded)
            gX_RendererGFX_RenderTexture_Unload(&gfx->ssr.normalMap);
    }
}

static const gX_I32 s_ssrDebugModes[] = {
    0, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 23, 24, 25, 26, 27
};
static const gX_I8 *s_ssrDebugModeNames[] = {
    "normal", "quality", "state machine", "confidence", "reject probe",
    "reconstructed normal", "bounce", "reject census", "hit normal",
    "hit facing", "hit type", "march coverage", "aux gate", "environment",
    "accepted hit", "planar UV", "world position", "matrix", "depth", "joint depth"
};

static gX_I32 gfx_ssr_debug_index(gX_I32 mode) {
    for (gX_I32 i = 0; i < (gX_I32)(sizeof(s_ssrDebugModes) / sizeof(s_ssrDebugModes[0])); i++)
        if (s_ssrDebugModes[i] == mode) return i;
    return 0;
}

//opencode: isolate model surface authoring from the long model/material inspector.
static gX_I0 gfx_model_surface_panel(gX_RendererGFX *gfx) {
    gfx_model_selection_sync(gfx);
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    if (!model) {
        ImGui::TextDisabled("Select a model in the Scene panel to edit its surface.");
        return;
    }
    ImGui::TextDisabled("Surface controls for %d: %s", s_selModel,
        gfx_model_basename(model->path));
    gX_I1 excludeFromPlanarCapture = model->params.excludeFromPlanarCapture ? 1 : 0;
    if (ImGui::Checkbox("exclude from planar capture", (bool *)&excludeFromPlanarCapture))
        model->params.excludeFromPlanarCapture = excludeFromPlanarCapture;
    gfx_text_disabled_wrapped(
        "Use on backing geometry that occludes the mirror view.");
    static const gX_I8 *reflectionModes[] = {
        "Off (plain object)", "Environment", "Planar + SSR", "Planar only",
        "Curved + SSR", "Curved only"};
    int reflectionMode = model->params.planarReflection
        ? (model->params.nonPlanarReflection
            ? (model->params.receiveSSR ? 4 : 5)
            : (model->params.receiveSSR ? 2 : 3))
        : (model->params.enableReflections ? 1 : 0);
    ImGui::BeginDisabled(!model->loaded);
    if (ImGui::Combo("reflection mode", &reflectionMode, reflectionModes, 6))
        gfx_model_reflection_mode_set(gfx, model, reflectionMode);
    ImGui::EndDisabled();
    if (!model->loaded) ImGui::TextDisabled("Reflection modes require a loaded model mesh.");
    gfx_text_disabled_wrapped(
        "Off (plain object) disables this model's mirror/SSR reflections; its material settings and other mirrors stay active.");
    if (reflectionMode == 1 && !gfx->state.ENABLE_REFLECTIONS) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "Inactive: environment reflections are globally off.");
        if (ImGui::Button("Enable environment reflections"))
            gfx->state.ENABLE_REFLECTIONS = 1;
    }
    if (reflectionMode >= 2 && !gfx->state.ENABLE_PLANAR_REFLECTIONS) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "Inactive: planar captures are globally off.");
        if (ImGui::Button("Enable planar captures"))
            gfx->state.ENABLE_PLANAR_REFLECTIONS = 1;
    }
    gX_I1 receiveSSR = model->params.receiveSSR ? 1 : 0;
    if (ImGui::Checkbox("receive SSR (this model)", (bool *)&receiveSSR)) {
        model->params.receiveSSR = receiveSSR ? 1 : 0;
        if (receiveSSR) gfx->state.ENABLE_SSR = 1;
    }
    gfx_text_disabled_wrapped(
        "SSR is a separate global effect; this switch controls reception "
        "for this model.");
    if (model->params.receiveSSR && !gfx->state.ENABLE_SSR) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "Inactive: global SSR is off.");
        if (ImGui::Button("Enable SSR for opted-in models")) gfx->state.ENABLE_SSR = 1;
    }
    if (ImGui::Button("SSR only on this model")) {
        for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
            gX_RendererGFX_Model *candidate = gfx_model_at(gfx, (gX_I32)mi);
            if (candidate) candidate->params.receiveSSR = mi == (gX_U32)s_selModel;
        }
        gfx->state.ENABLE_SSR = 1;
    }
    ImGui::BeginDisabled(!model->loaded);
    if (ImGui::Button("Make planar mirror", ImVec2(-FLT_MIN, 0.0f)))
        gfx_model_make_planar_mirror(gfx, model);
    if (ImGui::Button("Generate best-fit plane", ImVec2(-FLT_MIN, 0.0f)))
        gfx_model_generate_reflection_plane(model);
    if (ImGui::Button("Flip plane normal", ImVec2(-FLT_MIN, 0.0f)))
        gfx_model_reflection_normal_flip(model);
    ImGui::EndDisabled();
    gfx_text_disabled_wrapped(
        "Best-fit uses transformed mesh vertices. Flipping the normal "
        "preserves the plane.");
    if (s_planarFitModel == s_selModel && s_planarFitRatio >= 0.0f) {
        ImGui::TextDisabled("Plane fit thickness ratio: %.4f (0 = flat)", s_planarFitRatio);
        if (s_planarFitRatio > 0.12f)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "This model is volumetric; planar reflection is an approximation.");
    } else {
        gfx_text_disabled_wrapped(
            "Generate a plane to check whether this mesh is flat enough.");
    }
    gX_I1 followsModel = model->params.reflectionPlaneFollowsModel ? 1 : 0;
    if (ImGui::Checkbox("plane follows model rotation", (bool *)&followsModel))
        gfx_model_reflection_plane_follow_set(model, followsModel);
    gfx_text_disabled_wrapped(
        "When enabled, the plane normal is model-local and its origin follows the model position.");
    ImGui::DragFloat(model->params.reflectionPlaneFollowsModel
            ? "model-local plane offset" : "world plane distance",
        &model->params.reflectionPlaneHeight, 0.05f, -100.0f, 100.0f);
    ImGui::DragFloat3("reflection normal", (float *)&model->params.reflectionNormal, 0.02f, -1.0f, 1.0f);
    ImGui::DragFloat("wave amplitude", &model->params.waveAmplitude, 0.01f, 0.0f, 2.0f);
    gX_I1 alphaTest = model->params.enableAlphaTest ? 1 : 0;
    if (ImGui::Checkbox("alpha test", (bool *)&alphaTest)) model->params.enableAlphaTest = alphaTest ? 1 : 0;
    ImGui::DragFloat("alpha cutoff", &model->params.alphaCutoff, 0.01f, 0.0f, 1.0f);
    gfx_editor_glass_panel(model, gfx->state.shaderQuality, gfx_color_edit);
    gX_I1 water = model->params.hasWater ? 1 : 0;
    if (ImGui::Checkbox("water material", (bool *)&water)) model->params.hasWater = water;
    ImGui::DragFloat("water scroll U", &model->params.waterScrollU, 0.01f, -4.0f, 4.0f);
    ImGui::DragFloat("water scroll V", &model->params.waterScrollV, 0.01f, -4.0f, 4.0f);
}

static gX_I0 gfx_model_list_panel(gX_RendererGFX *gfx) {
    gfx_model_selection_sync(gfx);
    gX_U32 nModels = 0;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++)
        if (gfx_model_at(gfx, (gX_I32)mi)) nModels++;
    ImGui::SeparatorText("Scene models");
    ImGui::Text("models: %u / %u", nModels, gfx->models.amount);
    if (ImGui::Button("Duplicate") && s_selModel >= 0) {
        gX_RendererGFX_Model *source = gfx_model_at(gfx, s_selModel);
        if (source && source->path[0]) {
            gX_I32 index = gX_RendererGFX_Model_Add(gfx, source->path);
            gX_RendererGFX_Model *clone = gfx_model_at(gfx, index);
            if (clone) {
                clone->params = source->params;
                clone->params.pos.x += 1.0f;
                s_selModel = index;
                s_selLight = -1;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && s_selModel >= 0) {
        gX_RendererGFX_Model_Remove(gfx, (gX_U32)s_selModel);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: model %d removed", s_selModel);
        s_selModel = -1;
        s_modelRevertModel = -1;
        s_modelRevertValid = 0;
    }
    gX_F32 modelListHeight = ImGui::GetTextLineHeightWithSpacing() * 10.0f
        + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGui::BeginChild("GfxModelList", ImVec2(0, modelListHeight), true);
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, (gX_I32)mi);
        if (!model) continue;
        gX_I8 label[192];
        snprintf(label, sizeof(label), "%u: %s%s", mi, gfx_model_basename(model->path),
            model->loaded ? "" : " [loading]");
        if (ImGui::Selectable(label, (gX_I32)mi == s_selModel)) {
            s_selModel = (gX_I32)mi;
            s_selLight = -1;
        }
    }
    ImGui::EndChild();
}

//opencode: model motion-path helpers are defined with the orbit authoring below.
static gX_I0 gfx_model_orbit_capture_center(gX_RendererGFX_Model *model);
static gX_I0 gfx_model_orbit_panel(gX_RendererGFX_Model *model);

//opencode: shared rig selection — the Rig panel lists bones and the Animation
//   panel's pose editor edits the selected one.
static gX_I32 s_rigBone = 0;
static gX_I32 s_rigBoneModel = -1;

//opencode: merged keyframe lookup — nearest key at or before/after the
//   playhead across every channel of the clip.
static gX_F32 gfx_model_animation_neighbor_key(
    struct gX_RendererGFX_GLB_Animation *animation, gX_F32 time, gX_I1 forward) {
    gX_F32 best = forward ? 3.4e38f : -3.4e38f;
    for (gX_I32 ci = 0; ci < animation->channelCount; ci++) {
        gX_RendererGFX_GLB_AnimChannel *channel = &animation->channels[ci];
        if (!channel->times || channel->keyframeCount <= 0) continue;
        for (gX_I32 ki = 0; ki < channel->keyframeCount; ki++) {
            gX_F32 keyTime = channel->times[ki];
            if (forward ? (keyTime > time + 0.0005f)
                        : (keyTime < time - 0.0005f)) {
                if (forward ? (keyTime < best) : (keyTime > best))
                    best = keyTime;
            }
        }
    }
    return best;
}

//opencode: keyframe timeline — merged key ticks across the clip's channels,
//   trim-range handles, click/drag scrubbing, and the playhead. The evaluator
//   clamps playback into [animStart, animEnd], so the widget edits the same
//   range it displays and returns the merged key count for the status line.
static gX_I32 gfx_model_animation_timeline(gX_RendererGFX_Model *model,
    struct gX_RendererGFX_GLB_Data *glb, gX_I32 clip, gX_F32 duration,
    gX_F32 rangeStart, gX_F32 rangeEnd) {
    gX_RendererGFX_Model_Parameters *params = &model->params;
    struct gX_RendererGFX_GLB_Animation *animation = &glb->animations[clip];
    if (duration <= 0.0f) duration = 1.0f;
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 96.0f) size.x = 96.0f;
    ImGui::InvisibleButton("##anim_timeline", ImVec2(size.x, 48.0f));
    ImVec2 rectMin = ImGui::GetItemRectMin();
    ImVec2 rectMax = ImGui::GetItemRectMax();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    gX_F32 x0 = rectMin.x + 8.0f;
    gX_F32 x1 = rectMax.x - 8.0f;
    gX_F32 scale = (x1 - x0) / duration;
    gX_F32 barTop = rectMin.y + 26.0f;
    gX_F32 barBottom = rectMax.y - 6.0f;
    draw->AddRectFilled(rectMin, rectMax, IM_COL32(16, 18, 22, 255), 4.0f);
    draw->AddRectFilled(ImVec2(x0, barTop), ImVec2(x1, barBottom),
        IM_COL32(44, 48, 56, 255), 2.0f);
    gX_F32 trimX0 = x0 + rangeStart * scale;
    gX_F32 trimX1 = x0 + rangeEnd * scale;
    draw->AddRectFilled(ImVec2(trimX0, barTop), ImVec2(trimX1, barBottom),
        IM_COL32(74, 96, 136, 255), 2.0f);
    //opencode: merged key ticks (3px merge radius keeps dense clips readable).
    gX_F32 lastTick = -3.4e38f;
    gX_I32 mergedKeys = 0;
    for (gX_I32 ci = 0; ci < animation->channelCount; ci++) {
        gX_RendererGFX_GLB_AnimChannel *channel = &animation->channels[ci];
        if (!channel->times || channel->keyframeCount <= 0) continue;
        for (gX_I32 ki = 0; ki < channel->keyframeCount; ki++) {
            gX_F32 keyTime = channel->times[ki];
            gX_F32 keyX = x0 + keyTime * scale;
            if (keyX - lastTick < 3.0f) continue;
            lastTick = keyX;
            mergedKeys++;
            gX_I1 inside = (keyTime >= rangeStart - 0.0005f
                && keyTime <= rangeEnd + 0.0005f);
            draw->AddLine(ImVec2(keyX, rectMin.y + 8.0f),
                ImVec2(keyX, barBottom),
                inside ? IM_COL32(236, 206, 118, 255)
                       : IM_COL32(120, 108, 72, 150), 1.0f);
        }
    }
    gX_F32 playheadX = x0 + (gX_F32)params->animTime * scale;
    if (playheadX < x0) playheadX = x0;
    if (playheadX > x1) playheadX = x1;
    draw->AddLine(ImVec2(playheadX, rectMin.y + 4.0f),
        ImVec2(playheadX, rectMax.y - 2.0f), IM_COL32(255, 96, 96, 255), 2.0f);
    draw->AddTriangleFilled(ImVec2(playheadX - 5.0f, rectMin.y + 3.0f),
        ImVec2(playheadX + 5.0f, rectMin.y + 3.0f),
        ImVec2(playheadX, rectMin.y + 11.0f), IM_COL32(255, 96, 96, 255));
    draw->AddRectFilled(ImVec2(trimX0 - 2.0f, barTop - 2.0f),
        ImVec2(trimX0 + 2.0f, barBottom + 2.0f), IM_COL32(198, 220, 255, 255));
    draw->AddRectFilled(ImVec2(trimX1 - 2.0f, barTop - 2.0f),
        ImVec2(trimX1 + 2.0f, barBottom + 2.0f), IM_COL32(198, 220, 255, 255));
    if (ImGui::IsItemActive()) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        gX_F32 time = (mouse.x - x0) / scale;
        if (time < 0.0f) time = 0.0f;
        if (time > duration) time = duration;
        gX_I1 onStart = fabsf(mouse.x - trimX0) <= 6.0f;
        gX_I1 onEnd = fabsf(mouse.x - trimX1) <= 6.0f;
        if (trimX1 - trimX0 < 12.0f) { onStart = 0; onEnd = 0; }
        if (onStart && !onEnd) {
            if (time < rangeEnd - 0.02f) params->animStart = time;
        } else if (onEnd) {
            if (time > rangeStart + 0.02f) params->animEnd = time;
        } else {
            if (time < rangeStart) time = rangeStart;
            if (time > rangeEnd) time = rangeEnd;
            params->animTime = time;
        }
    }
    //opencode: harness hook — the animation gate reads the track rect and the
    //   resolved range to aim virtual-input clicks at the timeline.
    const gX_I8 *uiTrace = getenv("GX_ANIM_UI_TRACE");
    if (uiTrace && uiTrace[0] && uiTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_ANIMUI: x0=%.1f y0=%.1f x1=%.1f y1=%.1f duration=%.3f "
            "start=%.3f end=%.3f time=%.3f",
            x0, rectMin.y, x1, rectMax.y, duration, rangeStart, rangeEnd,
            params->animTime);
    }
    return mergedKeys;
}

//opencode: GLB clip playback + keyframe timeline — clip picker, transport,
//   trim range, key navigation, and scrub. The evaluator samples the clip into
//   the skeleton every frame, so the controls only touch the model's animation
//   parameters.
static gX_I0 gfx_model_animation_panel(gX_RendererGFX_Model *model) {
    struct gX_RendererGFX_GLB_Data *glb = model->glbData;
    gX_I32 clipCount = glb ? glb->animCount : 0;
    if (clipCount <= 0) return;
    //opencode: harness hook — expand the section and scroll it into view so
    //   virtual-input gates can reach the timeline without scrolling by hand.
    const gX_I8 *focusHook = getenv("GX_GFX_ANIM_FOCUS");
    gX_I1 focus = (focusHook && focusHook[0] && focusHook[0] != '0') ? 1 : 0;
    if (focus) ImGui::SetNextItemOpen(1, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader("Animation")) return;
    if (focus) ImGui::SetScrollHereY(0.0f);
    gX_RendererGFX_Model_Parameters *params = &model->params;
    gX_F32 saveKeyRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    gX_I32 clip = params->animClip;
    if (clip < 0 || clip >= clipCount) clip = 0;
    if (ImGui::BeginCombo("clip", glb->animations[clip].name)) {
        for (gX_I32 ci = 0; ci < clipCount; ci++) {
            if (ImGui::Selectable(glb->animations[ci].name, ci == clip)) {
                params->animClip = ci;
                params->animTime = 0.0;
                params->animPlaying = 1;
            }
        }
        ImGui::EndCombo();
    }
    //opencode: cross-fade — secondary clip and blend weight (0 = primary only,
    //   1 = secondary only); the primary drives the shared playhead.
    gX_I32 clip2 = params->animClip2;
    const gX_I8 *clip2Name = (clip2 >= 0 && clip2 < clipCount)
        ? glb->animations[clip2].name : "none";
    if (ImGui::BeginCombo("blend clip", clip2Name)) {
        if (ImGui::Selectable("none", clip2 < 0)) params->animClip2 = -1;
        for (gX_I32 ci = 0; ci < clipCount; ci++) {
            if (ImGui::Selectable(glb->animations[ci].name, ci == clip2))
                params->animClip2 = ci;
        }
        ImGui::EndCombo();
    }
    gX_I1 blendEnabled = (clip2 >= 0 && clip2 < clipCount) ? 1 : 0;
    gX_F32 blendRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ImGui::BeginDisabled(!blendEnabled);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("blend", &params->animBlend, 0.01f, 0.0f, 1.0f);
    gfx_item_rect4(blendRect);
    ImGui::SameLine();
    if (ImGui::SmallButton("Swap") && blendEnabled) {
        params->animClip = clip2;
        params->animClip2 = clip;
        params->animBlend = 1.0f - params->animBlend;
        clip = params->animClip;
    }
    ImGui::EndDisabled();
    //opencode: harness hook — the animation gate aims a virtual-input drag at
    //   the blend slider from this rect trace.
    const gX_I8 *blendTrace = getenv("GX_ANIM_UI_TRACE");
    if (blendTrace && blendTrace[0] && blendTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_ANIMBLEND: w=%.3f enabled=%d rect=(%.0f,%.0f,%.0f,%.0f)",
            params->animBlend, blendEnabled, blendRect[0], blendRect[1],
            blendRect[2], blendRect[3]);
    }
    gX_F32 duration = 0.0f;
    for (gX_I32 ci = 0; ci < glb->animations[clip].channelCount; ci++) {
        gX_RendererGFX_GLB_AnimChannel *channel = &glb->animations[clip].channels[ci];
        if (!channel->times || channel->keyframeCount <= 0) continue;
        gX_F32 end = channel->times[channel->keyframeCount - 1];
        if (end > duration) duration = end;
    }
    gX_F32 rangeStart = 0.0f;
    gX_F32 rangeEnd = duration;
    gX_RendererGFX_Animation_ClipRange(params->animStart, params->animEnd,
        duration, &rangeStart, &rangeEnd);
    gX_I1 playing = params->animPlaying ? 1 : 0;
    if (ImGui::Button(playing ? "Pause" : "Play"))
        params->animPlaying = playing ? 0 : 1;
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        params->animTime = rangeStart;
        params->animPlaying = 1;
    }
    ImGui::SameLine();
    gX_I1 loop = params->animLoop ? 1 : 0;
    if (ImGui::Checkbox("Loop", (bool *)&loop))
        params->animLoop = loop;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("speed", &params->animSpeed, 0.05f, -4.0f, 4.0f);
    struct gX_RendererGFX_GLB_Animation *animation = &glb->animations[clip];
    gX_I32 mergedKeys = gfx_model_animation_timeline(model, glb, clip,
        duration, rangeStart, rangeEnd);
    gX_F32 time = (gX_F32)params->animTime;
    if (ImGui::ArrowButton("##anim_start", ImGuiDir_Left)) {
        gX_F32 key = gfx_model_animation_neighbor_key(animation, time, 0);
        if (key > -3.0e37f) {
            if (key < rangeStart) key = rangeStart;
            if (key > rangeEnd) key = rangeEnd;
            params->animTime = key;
        }
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##anim_end", ImGuiDir_Right)) {
        gX_F32 key = gfx_model_animation_neighbor_key(animation, time, 1);
        if (key < 3.0e37f) {
            if (key < rangeStart) key = rangeStart;
            if (key > rangeEnd) key = rangeEnd;
            params->animTime = key;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.2fs / %.2fs", time, duration);
    gX_F32 trimWidth = ImGui::GetContentRegionAvail().x * 0.38f;
    if (trimWidth < 64.0f) trimWidth = 64.0f;
    gX_F32 startField = rangeStart;
    ImGui::SetNextItemWidth(trimWidth);
    if (ImGui::DragFloat("##anim_start", &startField, 0.01f, 0.0f, duration,
            "start %.2fs"))
        params->animStart = startField;
    ImGui::SameLine();
    gX_F32 endField = rangeEnd;
    ImGui::SetNextItemWidth(trimWidth);
    if (ImGui::DragFloat("##anim_end", &endField, 0.01f, 0.0f, duration,
            "end %.2fs"))
        params->animEnd = endField;
    ImGui::SameLine();
    if (ImGui::SmallButton("Full clip")) {
        params->animStart = 0.0f;
        params->animEnd = -1.0f;
    }
    ImGui::TextDisabled("clip %d/%d, %.2fs, %d channels, %d keys",
        clip, clipCount, duration, animation->channelCount, mergedKeys);

    //opencode: pose editing — cycle/select a bone, author a live transform
    //   override, and key the effective pose into the selected clip. The fields
    //   stay laid out (disabled) without an override so the harness trace can
    //   aim at them before the first click.
    if (ImGui::TreeNodeEx("Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
        gX_ModelSkeleton *skeleton = &model->model.skeleton;
        gX_I32 boneCount = skeleton->boneCount;
        if (boneCount <= 0 || !model->model.currentPose) {
            ImGui::TextDisabled("No skeleton to pose.");
        } else {
            if (s_rigBoneModel != s_selModel) {
                s_rigBoneModel = s_selModel;
                s_rigBone = 0;
            }
            if (s_rigBone >= boneCount) s_rigBone = 0;
            if (ImGui::ArrowButton("##rig_prev", ImGuiDir_Left))
                s_rigBone = (s_rigBone + boneCount - 1) % boneCount;
            ImGui::SameLine();
            if (ImGui::ArrowButton("##rig_next", ImGuiDir_Right))
                s_rigBone = (s_rigBone + 1) % boneCount;
            ImGui::SameLine();
            ImGui::Text("%d: %s", s_rigBone, skeleton->bones[s_rigBone].name);
            gX_I32 overrideCount = params->poseOverrideCount;
            if (overrideCount > gX_GFX_MODEL_POSE_OVERRIDES)
                overrideCount = gX_GFX_MODEL_POSE_OVERRIDES;
            gX_I32 overrideIndex = -1;
            for (gX_I32 oi = 0; oi < overrideCount; oi++) {
                if ((gX_I32)params->poseOverrides[oi * 11] == s_rigBone) {
                    overrideIndex = oi;
                    break;
                }
            }
            gX_F32 overrideRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            gX_F32 rotationRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            gX_F32 keyRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            gX_F32 clearRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (overrideIndex < 0) {
                if (ImGui::SmallButton("Override")) {
                    if (params->poseOverrideCount
                        < gX_GFX_MODEL_POSE_OVERRIDES) {
                        gX_Transform pose = model->model.currentPose[s_rigBone];
                        gX_F32 *entry = &params->poseOverrides[
                            params->poseOverrideCount * 11];
                        entry[0] = (gX_F32)s_rigBone;
                        entry[1] = pose.translation.x;
                        entry[2] = pose.translation.y;
                        entry[3] = pose.translation.z;
                        entry[4] = pose.rotation.x;
                        entry[5] = pose.rotation.y;
                        entry[6] = pose.rotation.z;
                        entry[7] = pose.rotation.w;
                        entry[8] = pose.scale.x;
                        entry[9] = pose.scale.y;
                        entry[10] = pose.scale.z;
                        params->poseOverrideCount++;
                        overrideIndex = params->poseOverrideCount - 1;
                    }
                }
                gfx_item_rect4(overrideRect);
            } else {
                ImGui::TextDisabled("override active at %.2fs", time);
            }
            gX_I1 hasOverride = overrideIndex >= 0;
            gX_F32 *entry = hasOverride
                ? &params->poseOverrides[overrideIndex * 11] : NULL;
            gX_F32 translation[3] = {0.0f, 0.0f, 0.0f};
            gX_F32 rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            gX_F32 scale[3] = {1.0f, 1.0f, 1.0f};
            if (entry) {
                translation[0] = entry[1];
                translation[1] = entry[2];
                translation[2] = entry[3];
                rotation[0] = entry[4];
                rotation[1] = entry[5];
                rotation[2] = entry[6];
                rotation[3] = entry[7];
                scale[0] = entry[8];
                scale[1] = entry[9];
                scale[2] = entry[10];
            }
            ImGui::BeginDisabled(!hasOverride);
            if (ImGui::DragFloat3("bone pos", translation, 0.01f) && entry) {
                entry[1] = translation[0];
                entry[2] = translation[1];
                entry[3] = translation[2];
            }
            if (ImGui::DragFloat4("bone rot", rotation, 0.01f) && entry) {
                gX_F32 length = sqrtf(rotation[0] * rotation[0]
                    + rotation[1] * rotation[1] + rotation[2] * rotation[2]
                    + rotation[3] * rotation[3]);
                if (length > 0.0001f) {
                    rotation[0] /= length;
                    rotation[1] /= length;
                    rotation[2] /= length;
                    rotation[3] /= length;
                } else {
                    rotation[0] = 0.0f;
                    rotation[1] = 0.0f;
                    rotation[2] = 0.0f;
                    rotation[3] = 1.0f;
                }
                entry[4] = rotation[0];
                entry[5] = rotation[1];
                entry[6] = rotation[2];
                entry[7] = rotation[3];
            }
            gfx_item_rect4(rotationRect);
            if (ImGui::DragFloat3("bone scale", scale, 0.01f) && entry) {
                entry[8] = scale[0];
                entry[9] = scale[1];
                entry[10] = scale[2];
            }
            if (ImGui::SmallButton("Key") && entry) {
                gX_RendererGFX_Animation_PoseKey(model, s_rigBone);
                //opencode: the new key now drives the pose — drop the override
                //   (swap-remove keeps the array compact).
                params->poseOverrides[overrideIndex * 11] =
                    params->poseOverrides[(params->poseOverrideCount - 1) * 11];
                params->poseOverrideCount--;
                entry = NULL;
                hasOverride = 0;
            }
            gfx_item_rect4(keyRect);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear") && entry) {
                params->poseOverrides[overrideIndex * 11] =
                    params->poseOverrides[(params->poseOverrideCount - 1) * 11];
                params->poseOverrideCount--;
                entry = NULL;
                hasOverride = 0;
            }
            gfx_item_rect4(clearRect);
            ImGui::EndDisabled();
            //opencode: harness hook — the animation gate aims virtual-input
            //   clicks/drags at the pose widgets from this rect trace.
            const gX_I8 *poseTrace = getenv("GX_ANIMPOSE_TRACE");
            if (poseTrace && poseTrace[0] && poseTrace[0] != '0'
                && (ImGui::GetFrameCount() % 30u) == 0u) {
                gX_I32 totalKeys = 0;
                for (gX_I32 ci = 0; ci < animation->channelCount; ci++)
                    totalKeys += animation->channels[ci].keyframeCount;
                gX_TraceLog(gX_LOG_INFO,
                    "GFX_ANIMPOSE: bone=%d over=%d keys=%d "
                    "ovr=(%.0f,%.0f,%.0f,%.0f) rot=(%.0f,%.0f,%.0f,%.0f) "
                    "key=(%.0f,%.0f,%.0f,%.0f) clr=(%.0f,%.0f,%.0f,%.0f)",
                    s_rigBone, hasOverride ? 1 : 0, totalKeys,
                    overrideRect[0], overrideRect[1], overrideRect[2],
                    overrideRect[3], rotationRect[0], rotationRect[1],
                    rotationRect[2], rotationRect[3], keyRect[0], keyRect[1],
                    keyRect[2], keyRect[3], clearRect[0], clearRect[1],
                    clearRect[2], clearRect[3]);
            }
        }
        ImGui::TreePop();
    }
    //opencode: authored-key persistence — writes every clip channel key to the
    //   `<model>.anim.json` sidecar next to the model (loaded on model load).
    if (ImGui::SmallButton("Save keys")) {
        gX_I32 saved = gX_RendererGFX_Animation_SidecarSave(model);
        gX_TraceLog(gX_LOG_NOTICE, "GfxEd: anim sidecar save %s",
            saved ? "ok" : "failed");
    }
    gfx_item_rect4(saveKeyRect);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload"))
        gX_RendererGFX_Animation_SidecarLoad(model);
    ImGui::SameLine();
    ImGui::TextDisabled("<model>.anim.json");
    //opencode: harness hook — the animation gate aims a click at Save keys.
    const gX_I8 *saveTrace = getenv("GX_ANIM_UI_TRACE");
    if (saveTrace && saveTrace[0] && saveTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_ANIMSAVE: rect=(%.0f,%.0f,%.0f,%.0f)", saveKeyRect[0],
            saveKeyRect[1], saveKeyRect[2], saveKeyRect[3]);
    }
}

//opencode: read-only rig inspector — surfaces the loaded skeleton and the
//   parsed GLB clips (target node, path, key count, duration) for the selected
//   model. Posing and playback stay out until a keyframe timeline exists.
static gX_I0 gfx_model_rig_panel(gX_RendererGFX *gfx, gX_RendererGFX_Model *model) {
    static gX_I8 s_rigTracePath[128] = {0};
    const gX_I8 *trace = getenv("GX_GFX_RIG_TRACE");
    if (!model->loaded) {
        if (ImGui::CollapsingHeader("Rig"))
            ImGui::TextDisabled("Waiting for the model load.");
        return;
    }
    gX_ModelSkeleton *skeleton = &model->model.skeleton;
    struct gX_RendererGFX_GLB_Data *glb = model->glbData;
    gX_I32 clipCount = glb ? glb->animCount : 0;
    if (trace && trace[0] && trace[0] != '0'
        && strcmp(s_rigTracePath, model->path) != 0) {
        snprintf(s_rigTracePath, sizeof(s_rigTracePath), "%s", model->path);
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: rig model=%d path=%s bones=%d clips=%d anim0=%s channels=%d",
            s_selModel, model->path, skeleton->boneCount, clipCount,
            clipCount > 0 ? glb->animations[0].name : "-",
            clipCount > 0 ? glb->animations[0].channelCount : 0);
    }
    if (!ImGui::CollapsingHeader("Rig")) return;
    if (s_rigBoneModel != s_selModel) {
        s_rigBoneModel = s_selModel;
        s_rigBone = 0;
    }
    gX_I1 showSkeleton = gfx->state.SHOW_SKELETON ? 1 : 0;
    if (ImGui::Checkbox("show in viewport", (bool *)&showSkeleton))
        gfx->state.SHOW_SKELETON = showSkeleton ? 1 : 0;
    ImGui::TextDisabled("Bind-pose joints and parent links for every rigged model.");
    if (skeleton->boneCount > 0) {
        ImGui::Text("bones: %d", skeleton->boneCount);
        gX_F32 listHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f
            + ImGui::GetStyle().WindowPadding.y * 2.0f;
        ImGui::BeginChild("GfxRigBones", ImVec2(0.0f, listHeight), true);
        for (gX_I32 bi = 0; bi < skeleton->boneCount; bi++) {
            gX_I8 label[96];
            snprintf(label, sizeof(label), "%d: %s (parent %d)", bi,
                skeleton->bones[bi].name, skeleton->bones[bi].parent);
            if (ImGui::Selectable(label, bi == s_rigBone)) s_rigBone = bi;
        }
        ImGui::EndChild();
        if (s_rigBone >= skeleton->boneCount) s_rigBone = 0;
        gX_Transform bind = skeleton->bindPose[s_rigBone];
        ImGui::TextDisabled("bind pos %.3f %.3f %.3f", bind.translation.x,
            bind.translation.y, bind.translation.z);
        ImGui::TextDisabled("bind rot %.3f %.3f %.3f %.3f", bind.rotation.x,
            bind.rotation.y, bind.rotation.z, bind.rotation.w);
        ImGui::TextDisabled("bind scale %.3f %.3f %.3f", bind.scale.x,
            bind.scale.y, bind.scale.z);
    } else {
        ImGui::TextDisabled("No skeleton on this model.");
    }
    ImGui::Separator();
    if (clipCount > 0) {
        static const gX_I8 *pathNames[] = {
            "translation", "rotation", "scale", "weights", "uv_offset"
        };
        ImGui::Text("clips: %d", clipCount);
        for (gX_I32 ci = 0; ci < clipCount; ci++) {
            gX_RendererGFX_GLB_Animation *clip = &glb->animations[ci];
            ImGui::TextDisabled("%d: %s", ci, clip->name);
            for (gX_I32 ki = 0; ki < clip->channelCount; ki++) {
                gX_RendererGFX_GLB_AnimChannel *channel = &clip->channels[ki];
                const gX_I8 *nodeName = "-";
                if (channel->targetNode >= 0 && channel->targetNode < glb->nodeCount)
                    nodeName = glb->nodes[channel->targetNode].name;
                const gX_I8 *pathName = channel->targetPath >= 0
                    && channel->targetPath < 5
                    ? pathNames[channel->targetPath] : "?";
                gX_F32 duration = channel->keyframeCount > 0
                    ? channel->times[channel->keyframeCount - 1] : 0.0f;
                ImGui::TextDisabled("   %s.%s  %d keys  %.2fs", nodeName,
                    pathName, channel->keyframeCount, duration);
            }
        }
    } else {
        ImGui::TextDisabled("No parsed GLB clips.");
    }
    ImGui::TextDisabled("Read-only; posing and playback are follow-ups.");
}

//opencode: v0.6.11 — model-level authoring: add/remove, transform, material,
//   reflections, alpha, labels, and runtime diagnostics.
//opencode: add direct rotation/scale resets and a selection transform revert.
//ai: label offset/style/top controls persist independently of the model/motion path.
static gX_I0 gfx_models_panel(gX_RendererGFX *gfx) {
    gfx_model_selection_sync(gfx);
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    if (!model) {
        ImGui::TextDisabled("Select a model to edit it.");
        return;
    }
    ImGui::SeparatorText("Model inspector");
    const gX_I8 *selectedLabel = gfx_model_basename(model->path);
    if (ImGui::BeginCombo("selected model", selectedLabel)) {
        for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
            gX_RendererGFX_Model *candidate = gfx_model_at(gfx, (gX_I32)mi);
            if (!candidate) continue;
            gX_I8 label[192];
            snprintf(label, sizeof(label), "%u: %s", mi, gfx_model_basename(candidate->path));
            if (ImGui::Selectable(label, (gX_I32)mi == s_selModel)) {
                s_selModel = (gX_I32)mi;
                s_selLight = -1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text("asset: %s", gfx_model_basename(model->path));
    ImGui::TextWrapped("path: %s", model->path);
    ImGui::TextDisabled("%s | meshes=%d", model->loaded ? "loaded" : "loading",
        model->loaded ? model->model.meshCount : 0);
    //opencode: surface transforms do not silently move an authored reflection
    //   plane; expose a measured offset and an explicit best-fit realignment.
    if (model->params.planarReflection && model->loaded) {
        gX_F32 planeOffset = gfx_model_reflection_plane_offset(model);
        if (planeOffset > 0.25f) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                "Planar reflection plane misses the model center by %.2f units.",
                planeOffset);
            ImGui::PopTextWrapPos();
            ImGui::TextDisabled("Custom offsets are allowed; align if unintended.");
            if (ImGui::Button("Align plane to model")) {
                gX_F32 previousOffset = planeOffset;
                gfx_model_generate_reflection_plane(model);
                gX_TraceLog(gX_LOG_INFO,
                    "GfxEd: planar plane aligned model=%d offset=%.3f->%.3f",
                    s_selModel, previousOffset,
                    gfx_model_reflection_plane_offset(model));
            }
        }
    }
    if (ImGui::Button("Frame camera")) gfx_camera_frame_model(model);
    ImGui::SeparatorText("Camera presets");
    if (ImGui::Button("Top"))
        gfx_camera_inspection_view(model, (gX_F32v3){0.0f, 1.0f, 0.0f},
            (gX_F32v3){0.0f, 0.0f, -1.0f});
    ImGui::SameLine();
    if (ImGui::Button("Front"))
        gfx_camera_inspection_view(model, (gX_F32v3){0.0f, 0.0f, 1.0f},
            (gX_F32v3){0.0f, 1.0f, 0.0f});
    ImGui::SameLine();
    if (ImGui::Button("Side"))
        gfx_camera_inspection_view(model, (gX_F32v3){1.0f, 0.0f, 0.0f},
            (gX_F32v3){0.0f, 1.0f, 0.0f});
    ImGui::SameLine();
    if (ImGui::Button("Grazing"))
        gfx_camera_inspection_view(model, (gX_F32v3){1.0f, 0.22f, 1.0f},
            (gX_F32v3){0.0f, 1.0f, 0.0f});
    ImGui::TextDisabled("Views focus the selected model and remain live-only.");
    if (ImGui::Button("Reset defaults")) gfx_model_reset_defaults(model);
    ImGui::SameLine();
    if (ImGui::Button("Revert selection") && s_modelRevertValid
        && s_modelRevertModel == s_selModel) {
        model->params = s_modelRevertParams;
        model->useTextures = s_modelRevertUseTextures;
    }
    ImGui::TextDisabled("Revert restores the state captured on selection.");
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        gfx_model_translate_controls(model);
        //opencode: the motion path owns the position while it is enabled.
        ImGui::BeginDisabled(model->params.orbitEnabled != 0);
        ImGui::DragFloat3("position", (float *)&model->params.pos, 0.05f);
        ImGui::EndDisabled();
        ImGui::DragFloat3("scale", (float *)&model->params.scale, 0.01f, 0.001f, 100.0f);
        ImGui::DragFloat3("rotation", (float *)&model->params.rotation, 1.0f);
        if (ImGui::Button("Reset rotation"))
            model->params.rotation = (gX_F32v3){0.0f, 0.0f, 0.0f};
        ImGui::SameLine();
        if (ImGui::Button("Reset scale"))
            model->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
        ImGui::SameLine();
        if (ImGui::Button("Revert transform") && s_modelRevertValid
            && s_modelRevertModel == s_selModel) {
            model->params.pos = s_modelRevertParams.pos;
            model->params.rotation = s_modelRevertParams.rotation;
            model->params.scale = s_modelRevertParams.scale;
        }
        ImGui::TextDisabled("Revert restores the transform captured when this selection began.");
        gfx_model_orbit_panel(model);
        int instances = (int)model->params.instanceCount;
        if (instances < 1) instances = 1;
        if (instances > 1024) instances = 1024;
        if (ImGui::SliderInt("instances", &instances, 1, 1024))
            model->params.instanceCount = (gX_U32)instances;
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        gX_I1 pbr = model->params.usePBR ? 1 : 0;
        if (ImGui::Checkbox("use PBR", (bool *)&pbr)) model->params.usePBR = pbr ? 1 : 0;
        ImGui::DragFloat("albedo", &model->params.albedo, 0.01f, 0.0f, 2.0f);
        gfx_color_edit("albedo tint", &model->params.albedoTint);
        gX_I1 topTint = model->params.useTopTint ? 1 : 0;
        if (ImGui::Checkbox("top-face tint", (bool *)&topTint)) model->params.useTopTint = topTint ? 1 : 0;
        if (model->params.useTopTint) gfx_color_edit("top tint", &model->params.albedoTintTop);
        if (model->params.usePBR) {
            ImGui::DragFloat("metalness", &model->params.metalness, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("roughness", &model->params.roughness, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("ambient occlusion", &model->params.ao, 0.01f, 0.0f, 1.0f);
        } else {
            ImGui::DragFloat("shininess", &model->params.shininess, 1.0f, 0.0f, 512.0f);
        }
        ImGui::DragFloat3("specular color", (float *)&model->params.specularColor, 0.01f, 0.0f, 1.0f);
        gX_I1 em = model->params.hasEmissiveMap ? 1 : 0;
        if (ImGui::Checkbox("emissive map", (bool *)&em)) model->params.hasEmissiveMap = em ? 1 : 0;
        ImGui::DragFloat("emissive factor", &model->params.emissiveFactor, 0.02f, 0.0f, 8.0f);
        gfx_color_edit("emissive color", &model->params.emissiveColor);
        ImGui::InputText("emissive material filter", model->params.emissiveMaterial,
            sizeof(model->params.emissiveMaterial));
        ImGui::TextDisabled("Optional material-name substring; empty applies the model-level emissive values.");
        gfx_model_material_browser(gfx, model);
    }
    gfx_model_texture_slots_panel(gfx, model);
    gfx_model_map_tuning_panel(gfx, model);
    if (ImGui::CollapsingHeader("Normal map")) {
        gX_I1 normal = model->params.hasNormalMap ? 1 : 0;
        if (ImGui::Checkbox("use normal map", (bool *)&normal)) model->params.hasNormalMap = normal ? 1 : 0;
        ImGui::DragFloat("normal strength", &model->params.normalMapFactor, 0.02f, 0.0f, 2.0f);
        ImGui::DragFloat2("UV scale", (float *)&model->params.normalMapScale, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat2("UV offset", (float *)&model->params.normalMapOffset, 0.01f, -10.0f, 10.0f);
        ImGui::PushItemWidth(-FLT_MIN);
        ImGui::InputText("texture path", s_normalTexturePath, sizeof(s_normalTexturePath));
        ImGui::PopItemWidth();
        if (ImGui::Button("Inject normal texture")) gfx_model_inject_normal(gfx, model);
        ImGui::SameLine();
        if (ImGui::Button("Clear injected texture")) {
            model->params.injectedNormalMapID = -1;
            model->params.hasNormalMap = 0;
        }
        ImGui::TextDisabled("injected texture id: %d", model->params.injectedNormalMapID);
    }
    //opencode: UX pass — a collapsed "Label" header hid the only way to
    //   author 3D text; renamed and opened by default.
    if (ImGui::CollapsingHeader("3D text label",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Billboard or flat sign anchored to the model; "
            "the Scene panel's Add 3D text button enables it quickly.");
        ImGui::DragFloat3("label offset", (float *)&model->params.textOffset, 0.05f);
        if (ImGui::Button("Reset label offset"))
            model->params.textOffset = (gX_F32v3){0.0f, 0.0f, 0.0f};
        ImGui::TextDisabled("World-axis offset from model position; follows the model anchor.");
        gX_I1 AtTop = model->params.textAnchorTop ? 1 : 0;
        if (ImGui::Checkbox("above model bounds", (bool *)&AtTop)) model->params.textAnchorTop = AtTop;
        if (AtTop) ImGui::DragFloat("label gap", &model->params.textGap, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat2("world size##label", (float *)&model->params.textWorldSize, 0.01f, 0.01f, 100.0f);
        gX_I1 Fit = model->params.textFit ? 1 : 0;
        if (ImGui::Checkbox("preserve text aspect", (bool *)&Fit)) model->params.textFit = Fit;
        gfx_color_edit("text color / opacity", &model->params.textColor);
        gfx_color_edit("shadow color / opacity", &model->params.textShadowColor);
        ImGui::DragFloat("shadow pixels", &model->params.textShadowPixels, 0.1f, 0.0f, 4.0f);
        gX_I1 visible = model->params.textVisible ? 1 : 0;
        if (ImGui::Checkbox("show label", (bool *)&visible)) model->params.textVisible = visible ? 1 : 0;
        ImGui::PushItemWidth(-FLT_MIN);
        ImGui::InputText("text", model->params.textString, sizeof(model->params.textString));
        ImGui::PopItemWidth();
        gX_I1 billboard = model->params.textBillboard ? 1 : 0;
        if (ImGui::Checkbox("billboard", (bool *)&billboard)) model->params.textBillboard = billboard ? 1 : 0;
        ImGui::DragFloat("font size", &model->params.textFontSize, 0.5f, 1.0f, 256.0f);
        ImGui::TextDisabled("Raster size fits automatically; world size controls scene size.");
        if (!s_fontFilesLoaded) {
            s_fontFiles = LoadDirectoryFilesEx("resources/font", "FILES*", false);
            s_fontFilesLoaded = 1;
        }
        gX_I8 currentFont[128];
        strncpy(currentFont, model->params.textFontPath, sizeof(currentFont) - 1);
        currentFont[sizeof(currentFont) - 1] = 0;
        if (ImGui::BeginCombo("font", currentFont[0]
                ? gfx_model_basename(currentFont) : "engine default")) {
            if (ImGui::Selectable("engine default", currentFont[0] == 0))
                model->params.textFontPath[0] = 0;
            for (gX_U32 fi = 0; fi < s_fontFiles.count; fi++) {
                const gX_I8 *path = s_fontFiles.paths[fi];
                if (ImGui::Selectable(gfx_model_basename(path),
                        strcmp(path, currentFont) == 0))
                    snprintf(model->params.textFontPath,
                        sizeof(model->params.textFontPath), "%s", path);
            }
            ImGui::EndCombo();
        }
        ImGui::PushItemWidth(-FLT_MIN);
        ImGui::InputText("font path", model->params.textFontPath,
            sizeof(model->params.textFontPath));
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Empty = engine default; loads on the next label draw.");
    }
    gfx_model_rig_panel(gfx, model);
    gfx_model_animation_panel(model);
    if (ImGui::CollapsingHeader("Diagnostics")) {
        gX_I1 textures = model->useTextures ? 1 : 0;
        if (ImGui::Checkbox("use textures", (bool *)&textures)) model->useTextures = textures ? 1 : 0;
        ImGui::Text("visibility: frustum=%s occlusion=%s", model->frustumVisible ? "yes" : "no",
            model->occlusionVisible ? "yes" : "no");
        ImGui::Text("AABB: %s", model->aabbValid ? "ready" : "pending");
    }
    gfx_model_overrides_panel(gfx, model);
}

//opencode: deterministic headless verification mirrors GX_THEME_SAVE without
//   changing normal editor behavior. Load runs before save so a combined
//   LOAD+SAVE run round-trips (save captures the loaded preset, not the
//   pre-load scene); single-hook runs are order-independent.
static gX_I0 gfx_preset_headless_batch(gX_RendererGFX *gfx,
                                       const gX_I8 *manifestPath) {
    FILE *manifest = fopen(manifestPath, "r");
    if (!manifest) {
        gX_TraceLog(gX_LOG_ERROR, "GfxEd: cannot open preset batch '%s'",
            manifestPath);
        return;
    }
    gX_I8 line[640];
    while (fgets(line, sizeof(line), manifest)) {
        gX_I8 *separator = strchr(line, '\t');
        if (!separator) continue;
        *separator = 0;
        gX_I8 *savePath = separator + 1;
        line[strcspn(line, "\r\n")] = 0;
        savePath[strcspn(savePath, "\r\n")] = 0;
        if (!line[0] || !savePath[0]) continue;
        gX_U32 loadLength = (gX_U32)strlen(line);
        gX_U32 saveLength = (gX_U32)strlen(savePath);
        gX_U32 pathCapacity = (gX_U32)sizeof(s_presetPath);
        if (loadLength >= pathCapacity || saveLength >= pathCapacity) {
            gX_TraceLog(gX_LOG_ERROR,
                "GfxEd: preset batch path exceeds %u bytes", pathCapacity - 1);
            continue;
        }
        memcpy(s_presetPath, line, loadLength + 1);
        gfx_preset_load(gfx);
        memcpy(s_presetPath, savePath, saveLength + 1);
        gfx_preset_save_commit(gfx);
    }
    fclose(manifest);
}

//opencode: mid-run state scripts for bijection tests — "frame:FLAG=value;..."
//   flips renderer state flags at exact frames so a gate can enable an effect,
//   disable it, and compare the settled frame against a never-enabled
//   baseline (history/residue check). Flags resolve at apply time: the
//   headless preset load recreates model storage, so model pointers must not
//   be cached across frames (a cached pre-load pointer silently dangles).
typedef struct {
    gX_I32 frame;
    const gX_I8 *name;
    gX_I1 value;
} gX_HeadlessStateChange;

static gX_I8 s_stateChangeNames[16][64];
static gX_HeadlessStateChange s_stateChanges[16];
static gX_I32 s_stateChangeCount = -1;

static gX_I1 *gfx_state_flag_resolve(gX_RendererGFX *gfx, const gX_I8 *name) {
    if (strcmp((const char *)name, "ENABLE_TAA") == 0) return &gfx->state.ENABLE_TAA;
    if (strcmp((const char *)name, "ENABLE_FXAA") == 0) return &gfx->state.ENABLE_FXAA;
    if (strcmp((const char *)name, "ENABLE_SSR") == 0) return &gfx->state.ENABLE_SSR;
    if (strcmp((const char *)name, "ENABLE_PLANAR_REFLECTIONS") == 0)
        return &gfx->state.ENABLE_PLANAR_REFLECTIONS;
    if (strcmp((const char *)name, "ENABLE_REFLECTIONS") == 0)
        return &gfx->state.ENABLE_REFLECTIONS;
    if (strcmp((const char *)name, "ENABLE_BLOOM") == 0) return &gfx->state.ENABLE_BLOOM;
    if (strcmp((const char *)name, "ENABLE_SSAO") == 0) return &gfx->state.ENABLE_SSAO;
    if (strcmp((const char *)name, "ENABLE_SHADOWS") == 0)
        return &gfx->state.ENABLE_SHADOWS;
    if (strcmp((const char *)name, "ENABLE_POINT_SHADOWS") == 0)
        return &gfx->state.ENABLE_POINT_SHADOWS;
    if (strcmp((const char *)name, "ENABLE_FOG") == 0) return &gfx->state.ENABLE_FOG;
    if (strcmp((const char *)name, "ENABLE_VOLUME_FOG") == 0)
        return &gfx->state.ENABLE_VOLUME_FOG;
    if (strcmp((const char *)name, "ENABLE_PARTICLES") == 0)
        return &gfx->state.ENABLE_PARTICLES;
    if (strcmp((const char *)name, "ENABLE_MOTION_BLUR") == 0)
        return &gfx->state.ENABLE_MOTION_BLUR;
    if (strcmp((const char *)name, "ENABLE_DOF") == 0) return &gfx->state.ENABLE_DOF;
    if (strcmp((const char *)name, "ENABLE_WATER") == 0) return &gfx->state.ENABLE_WATER;
    if (strcmp((const char *)name, "ENABLE_GODRAYS") == 0)
        return &gfx->state.ENABLE_GODRAYS;
    gX_RendererGFX_Model *model0 = gfx_model_at(gfx, 0);
    if (!model0) return NULL;
    if (strcmp((const char *)name, "MODEL0_PLANAR_REFLECTION") == 0)
        return &model0->params.planarReflection;
    if (strcmp((const char *)name, "MODEL0_NONPLANAR_REFLECTION") == 0)
        return &model0->params.nonPlanarReflection;
    if (strcmp((const char *)name, "MODEL0_ENABLE_REFLECTIONS") == 0)
        return &model0->params.enableReflections;
    if (strcmp((const char *)name, "MODEL0_RECEIVE_SSR") == 0)
        return &model0->params.receiveSSR;
    return NULL;
}

static gX_I1 gfx_state_flag_known(const gX_I8 *name) {
    static const gX_I8 *names[] = {
        "ENABLE_TAA", "ENABLE_FXAA", "ENABLE_SSR", "ENABLE_PLANAR_REFLECTIONS",
        "ENABLE_REFLECTIONS", "ENABLE_BLOOM", "ENABLE_SSAO", "ENABLE_SHADOWS",
        "ENABLE_POINT_SHADOWS", "ENABLE_FOG", "ENABLE_VOLUME_FOG",
        "ENABLE_PARTICLES", "ENABLE_MOTION_BLUR", "ENABLE_DOF", "ENABLE_WATER",
        "ENABLE_GODRAYS", "MODEL0_PLANAR_REFLECTION",
        "MODEL0_NONPLANAR_REFLECTION", "MODEL0_ENABLE_REFLECTIONS",
        "MODEL0_RECEIVE_SSR",
    };
    for (gX_U32 i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp((const char *)name, (const char *)names[i]) == 0) return 1;
    return 0;
}

static gX_I0 gfx_state_script_apply(gX_RendererGFX *gfx) {
    if (s_stateChangeCount < 0) {
        s_stateChangeCount = 0;
        const char *script = getenv("GX_GFX_STATE_SCRIPT");
        if (script && script[0]) {
            gX_I8 buffer[512];
            snprintf(buffer, sizeof(buffer), "%s", script);
            gX_I8 *save = NULL;
            for (gX_I8 *token = strtok_r(buffer, ";", &save);
                 token && s_stateChangeCount < 16;
                 token = strtok_r(NULL, ";", &save)) {
                gX_I32 frame = 0;
                gX_I8 name[64];
                gX_I32 value = 0;
                if (sscanf(token, "%d:%63[^=]=%d", &frame, name, &value) != 3)
                    continue;
                if (!gfx_state_flag_known(name)) {
                    gX_TraceLog(gX_LOG_ERROR,
                        "GX_GFX_STATE_SCRIPT: unknown flag '%s'", name);
                    continue;
                }
                gX_HeadlessStateChange *change =
                    &s_stateChanges[s_stateChangeCount];
                change->frame = frame;
                change->value = value ? 1 : 0;
                snprintf((char *)s_stateChangeNames[s_stateChangeCount],
                    sizeof(s_stateChangeNames[0]), "%s", name);
                change->name = s_stateChangeNames[s_stateChangeCount];
                s_stateChangeCount++;
            }
        }
    }
    for (gX_I32 i = 0; i < s_stateChangeCount; i++) {
        if (s_stateChanges[i].frame != (gX_I32)maindata.frameCount) continue;
        gX_I1 *flag = gfx_state_flag_resolve(gfx, s_stateChanges[i].name);
        if (!flag || *flag == s_stateChanges[i].value) continue;
        *flag = s_stateChanges[i].value;
        gX_TraceLog(gX_LOG_INFO, "GX_GFX_STATE_SCRIPT: %s=%d frame=%d",
            (const char *)s_stateChanges[i].name,
            (gX_I32)s_stateChanges[i].value, (gX_I32)maindata.frameCount);
    }
}

static gX_I0 gfx_preset_headless_hooks(gX_RendererGFX *gfx) {
    gfx_state_script_apply(gfx);
    const char *materialAssetPath = getenv("GX_GFX_MATERIAL_ASSET_CHECK");
    if (materialAssetPath && materialAssetPath[0]
        && !s_headlessMaterialAssetCheckDone && maindata.frameCount >= 3) {
        gfx_material_asset_headless_check(materialAssetPath);
        s_headlessMaterialAssetCheckDone = 1;
    }
    const char *assetRootAdd = getenv("GX_GFX_ASSET_ROOT_ADD");
    if (assetRootAdd && assetRootAdd[0] && !s_headlessAssetRootCheckDone
        && maindata.frameCount >= 3) {
        gX_I1 ok = gfx_asset_settings_root_add((const gX_I8 *)assetRootAdd);
        gfx_assets_refresh();
        gX_TraceLog(ok ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_ASSET_ROOT_ADD: %s %s", ok ? "PASS" : "FAIL", assetRootAdd);
        s_headlessAssetRootCheckDone = 1;
    }
    const char *renderCamera = getenv("GX_GFX_RENDER_CAMERA");
    if (renderCamera && renderCamera[0] && !s_headlessRenderDone
        && maindata.frameCount >= 5) {
        const char *indexText = getenv("GX_GFX_RENDER_CAMERA_INDEX");
        const char *sizeText = getenv("GX_GFX_RENDER_CAMERA_SIZE");
        if (sizeText && sizeText[0]) {
            gX_I32 wantW = 0, wantH = 0;
            if (sscanf(sizeText, "%dx%d", &wantW, &wantH) == 2
                && wantW > 0 && wantH > 0) {
                s_renderWidth = wantW;
                s_renderHeight = wantH;
            }
        }
        gX_I32 index = indexText ? atoi(indexText) : s_selectedCamera;
        if (index >= 0 && index < s_cameraCount) {
            snprintf(s_renderOutput, sizeof(s_renderOutput), "%s", renderCamera);
            s_selectedCamera = index;
            s_renderPending = 1;
            gX_TraceLog(gX_LOG_NOTICE,
                "GFX_RENDER_CAMERA: queued index=%d path=%s", index,
                renderCamera);
        } else {
            gX_TraceLog(gX_LOG_ERROR,
                "GFX_RENDER_CAMERA: no camera index %d (count %d)", index,
                s_cameraCount);
        }
        s_headlessRenderDone = 1;
    }
    //opencode: headless camera-marker drag check — pick the position marker,
    //   move it, then pick and move the target; both writes must land.
    const char *cameraDragCheck = getenv("GX_GFX_CAMERA_DRAG_CHECK");
    if (cameraDragCheck && cameraDragCheck[0] && !s_headlessCameraDragDone
        && maindata.frameCount >= 10) {
        gX_I32 pickKind = 0;
        gX_F32v3 pickPosition = {0.0f, 0.0f, 0.0f};
        gX_I1 picked = 0;
        gX_I32 posMoved = 0;
        gX_I32 targetMoved = 0;
        if (s_cameraCount > 0) {
            s_selectedCamera = 0;
            gX_F32v2 screen = gX_Camera_GetWorldToScreen(
                s_cameras[0].view.position, camera.camera3D);
            picked = gfx_editor_camera_marker_pick(screen, &pickKind,
                &pickPosition);
            if (picked && pickKind == 1) {
                gX_F32v3 before = s_cameras[0].view.position;
                gX_F32v3 targetBefore = s_cameras[0].view.target;
                s_draggingCamera = 1;
                s_dragCameraKind = 1;
                gfx_editor_camera_drag_apply((gX_F32v3){before.x + 1.0f,
                    before.y, before.z});
                posMoved = (fabsf(s_cameras[0].view.position.x - before.x) > 0.9f
                    && s_cameras[0].view.target.x == targetBefore.x
                    && s_cameras[0].view.target.y == targetBefore.y
                    && s_cameras[0].view.target.z == targetBefore.z) ? 1 : 0;
                s_draggingCamera = 0;
                gX_F32v2 targetScreen = gX_Camera_GetWorldToScreen(
                    s_cameras[0].view.target, camera.camera3D);
                gX_I32 targetKind = 0;
                gX_F32v3 targetPick = {0.0f, 0.0f, 0.0f};
                if (gfx_editor_camera_marker_pick(targetScreen, &targetKind,
                        &targetPick) && targetKind == 2) {
                    gX_F32v3 targetStart = s_cameras[0].view.target;
                    s_draggingCamera = 1;
                    s_dragCameraKind = 2;
                    gfx_editor_camera_drag_apply((gX_F32v3){targetStart.x,
                        targetStart.y + 1.0f, targetStart.z});
                    targetMoved = (fabsf(s_cameras[0].view.target.y
                        - targetStart.y) > 0.9f) ? 1 : 0;
                    s_draggingCamera = 0;
                }
            }
        }
        gX_I1 pass = (picked && pickKind == 1 && posMoved && targetMoved) ? 1 : 0;
        gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_CAMERA_DRAG_CHECK: %s picked=%d kind=%d posMoved=%d "
            "targetMoved=%d", pass ? "PASS" : "FAIL", picked, pickKind,
            posMoved, targetMoved);
        s_headlessCameraDragDone = 1;
    }
    const char *uiTheme = getenv("GX_GFX_UI_THEME");
    if (uiTheme && uiTheme[0] && !s_headlessUiThemeDone
        && maindata.frameCount >= 3) {
        gX_I32 preset = (gX_I32)atoi(uiTheme);
        if (preset < 0) preset = 0;
        if (preset >= GX_SETTINGS_UI_THEME_COUNT)
            preset = GX_SETTINGS_UI_THEME_COUNT - 1;
        settings.uiThemePreset = preset;
        settings.dirtyFlag = 0;
        s_headlessUiThemeDone = 1;
    }
    const char *primitiveCheck = getenv("GX_GFX_PRIMITIVE_CHECK");
    if (primitiveCheck && primitiveCheck[0] && !s_headlessPrimitiveCheckDone
        && maindata.frameCount >= 5) {
        gX_I32 added = 0;
        gX_I8 buffer[256];
        snprintf(buffer, sizeof(buffer), "%s", primitiveCheck);
        gX_I8 *save = NULL;
        for (gX_I8 *token = strtok_r(buffer, ",", &save); token;
             token = strtok_r(NULL, ",", &save)) {
            gX_I32 type = -1;
            for (gX_I32 ti = 0; ti < 6; ti++)
                if (strcmp((const char *)token,
                        (const char *)gfx_primitive_names(ti)) == 0)
                    type = ti;
            if (type >= 0 && gfx_primitive_add(gfx, type, 1.0f, 1) >= 0)
                added++;
        }
        gX_TraceLog(added > 0 ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_PRIMITIVE_CHECK: %s added=%d", added > 0 ? "PASS" : "FAIL",
            added);
        s_headlessPrimitiveCheckDone = 1;
    }
    const char *textureCheck = getenv("GX_GFX_TEXTURE_CHECK");
    if (textureCheck && textureCheck[0] && !s_headlessTextureCheckDone
        && maindata.frameCount >= 5) {
        gX_Color texLight = { 255, 255, 255, 255 };
        gX_Color texDark = { 51, 51, 51, 255 };
        gX_I8 texSolid[640], texChecker[640], texGradient[640],
            texNoise[640], texValue[640], texNormal[640], texOrm[640];
        gX_I32 made = 0;
        made += gfx_texture_generate(0, 64, 64, 0.0f, texLight, texDark,
            "solid", texSolid, sizeof(texSolid));
        made += gfx_texture_generate(1, 64, 64, 8.0f, texLight, texDark,
            "checker", texChecker, sizeof(texChecker));
        made += gfx_texture_generate(2, 64, 64, 0.0f, texDark, texLight,
            "gradient", texGradient, sizeof(texGradient));
        made += gfx_texture_generate(4, 64, 64, 0.5f, texLight, texDark,
            "noise", texNoise, sizeof(texNoise));
        made += gfx_texture_generate(7, 64, 64, 4.0f, texLight, texDark,
            "value", texValue, sizeof(texValue));
        gX_I1 normalOK = gfx_texture_normal_from_height(texChecker, 2.0f,
            "checker_n", texNormal, sizeof(texNormal));
        gX_I1 ormOK = gfx_texture_pack_orm(texChecker, texNoise, texSolid,
            "packed_orm", texOrm, sizeof(texOrm));
        gX_I1 previewOK = (s_texPreview.id > 0) ? 1 : 0;
        gX_I1 pass = (made == 5 && normalOK && ormOK && previewOK) ? 1 : 0;
        gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_TEXTURE_CHECK: %s generated=%d normal=%d orm=%d preview=%d",
            pass ? "PASS" : "FAIL", made, normalOK, ormOK, previewOK);
        s_headlessTextureCheckDone = 1;
    }
    const char *exportCheck = getenv("GX_GFX_EXPORT_CHECK");
    if (exportCheck && exportCheck[0] && !s_headlessExportCheckDone
        && maindata.frameCount >= 20) {
        gX_I32 mtls = 0;
        gX_I32 glbs = 0;
        gX_I32 files = gfx_export_all(gfx, "headless", &mtls, &glbs);
        gX_I1 pass = (files > 0 && mtls == files && glbs > 0) ? 1 : 0;
        gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_EXPORT_CHECK: %s files=%d mtls=%d glbs=%d",
            pass ? "PASS" : "FAIL", files, mtls, glbs);
        s_headlessExportCheckDone = 1;
    }
    const char *decalBakeCheck = getenv("GX_GFX_DECAL_BAKE_CHECK");
    if (decalBakeCheck && decalBakeCheck[0] && !s_headlessDecalBakeDone
        && maindata.frameCount >= 10) {
        gX_RendererGFX_Decal_RemoveAll(gfx);
        gX_RendererGFX_Decal_Add(gfx, (gX_F32v3){0.0f, 0.0f, 0.0f},
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 4.0f, -1, -1.0);
        gX_RendererGFX_Decal_Add(gfx, (gX_F32v3){2.0f, 0.0f, 1.0f},
            (gX_F32v3){0.0f, 1.0f, 0.0f}, 2.0f, -1, -1.0);
        gX_I8 albedoPath[640];
        gX_I1 baked = gfx_decal_bake(gfx, 128, 0.75f, 0.25f,
            (gX_Color){200, 60, 40, 255}, decalBakeCheck, "bake", albedoPath,
            sizeof(albedoPath));
        gX_TraceLog(baked ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_DECAL_BAKE_CHECK: %s albedo=%s", baked ? "PASS" : "FAIL",
            baked ? (const char *)albedoPath : "?");
        s_headlessDecalBakeDone = 1;
    }
    const char *prefabCheck = getenv("GX_GFX_PREFAB_CHECK");
    if (prefabCheck && prefabCheck[0] && !s_headlessPrefabCheckDone
        && maindata.frameCount >= 5) {
        if (s_selModel < 0) s_selModel = 0;
        gX_RendererGFX_Model *anchor = gfx_model_at(gfx, s_selModel);
        if (anchor) {
            gX_RendererGFX_Decal_Add(gfx,
                (gX_F32v3){anchor->params.pos.x + 1.0f,
                    anchor->params.pos.y + 0.02f, anchor->params.pos.z},
                (gX_F32v3){0.0f, 1.0f, 0.0f}, 1.5f, -1, -1.0);
            gX_RendererGFX_Emitter *emitter = gfx_emitter_at(gfx,
                gX_RendererGFX_Emitter_Add(gfx));
            if (emitter) {
                emitter->pos = (gX_F32v3){anchor->params.pos.x + 0.5f,
                    anchor->params.pos.y + 1.0f, anchor->params.pos.z};
                emitter->active = 1;
            }
        }
        const char *taggedCheck = getenv("GX_GFX_PREFAB_TAGGED");
        gX_I1 useTags = (taggedCheck && taggedCheck[0] == '1') ? 1 : 0;
        if (useTags) {
            s_taggedModels = (s_selModel >= 0 && s_selModel < 32)
                ? (1u << s_selModel) : 1u;
            s_taggedLights = 0x3;
            s_taggedDecals = 0x1;
            s_taggedEmitters = 0x1;
        }
        gX_I1 captured = gfx_prefab_capture(gfx, "Headless prefab", prefabCheck,
            8.0f, useTags);
        gX_I32 placed = -1;
        if (anchor) {
            gX_F32v3 at = {anchor->params.pos.x + 2.0f, anchor->params.pos.y,
                anchor->params.pos.z};
            placed = gfx_prefab_place(gfx, prefabCheck, at);
        }
        gX_TraceLog((captured && placed > 0) ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_PREFAB_CHECK: %s captured=%d placed=%d",
            (captured && placed > 0) ? "PASS" : "FAIL", captured, placed);
        s_headlessPrefabCheckDone = 1;
    }
    const char *batchPath = getenv("GX_GFX_PRESET_BATCH");
    if (batchPath && batchPath[0] && !s_headlessPresetLoadDone && maindata.frameCount >= 3) {
        gfx_preset_headless_batch(gfx, batchPath);
        s_headlessPresetLoadDone = 1;
        s_headlessPresetSaveDone = 1;
        return;
    }
    const char *loadPath = getenv("GX_GFX_PRESET_LOAD");
    if (loadPath && loadPath[0] && !s_headlessPresetLoadDone && maindata.frameCount >= 3) {
        strncpy(s_presetPath, loadPath, sizeof(s_presetPath) - 1);
        s_presetPath[sizeof(s_presetPath) - 1] = 0;
        gfx_preset_load(gfx);
        s_headlessPresetLoadDone = 1;
    }
    const char *materialMapPath = getenv("GX_GFX_MATERIAL_MAP_CHECK");
    if (materialMapPath && materialMapPath[0] && !s_headlessMaterialMapCheckDone
        && maindata.frameCount >= 3) {
        const char *materialMapKind = getenv("GX_GFX_MATERIAL_MAP_KIND");
        gX_I32 mapKind = (materialMapKind && materialMapKind[0] == '1') ? 1 : 0;
        //opencode: the selection may still be unset at frame 3 — fall back to
        //   the first scene model so the hook works from a bare preset load.
        gX_I32 mapModelIndex = (s_selModel >= 0) ? s_selModel : 0;
        gX_RendererGFX_Model *model = gfx_model_at(gfx, mapModelIndex);
        gX_I1 ok = gfx_material_override_map_assign(model, 0, mapKind,
            (const gX_I8 *)materialMapPath);
        const gX_I8 *stored = (const gX_I8 *)"";
        if (model && model->params.materialOverrideCount > 0) {
            gX_RendererGFX_MaterialOverride *entry =
                &model->params.materialOverrides[0];
            stored = (mapKind == 1) ? entry->emissiveMap : entry->albedoMap;
        }
        gX_I1 pass = ok && strcmp((const char *)stored, materialMapPath) == 0;
        gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
            "GFX_MATERIAL_MAP_CHECK: %s override=0 kind=%s stored=%s",
            pass ? "PASS" : "FAIL", mapKind == 1 ? "emissive" : "albedo",
            stored);
        s_headlessMaterialMapCheckDone = 1;
    }
    const char *savePath = getenv("GX_GFX_PRESET_SAVE");
    if (savePath && savePath[0] && !s_headlessPresetSaveDone && maindata.frameCount >= 3) {
        strncpy(s_presetPath, savePath, sizeof(s_presetPath) - 1);
        s_presetPath[sizeof(s_presetPath) - 1] = 0;
        gfx_preset_save_commit(gfx);
        s_headlessPresetSaveDone = 1;
    }
    const char *saveUiPath = getenv("GX_GFX_PRESET_SAVE_UI");
    const char *saveUiFrame = getenv("GX_GFX_PRESET_SAVE_UI_FRAME");
    gX_U32 saveUiAt = saveUiFrame ? (gX_U32)atoi(saveUiFrame) : 3;
    if (saveUiPath && saveUiPath[0] && !s_headlessPresetSaveUiDone
        && maindata.frameCount >= saveUiAt) {
        strncpy(s_presetPath, saveUiPath, sizeof(s_presetPath) - 1);
        s_presetPath[sizeof(s_presetPath) - 1] = 0;
        gfx_preset_save(gfx);
        s_headlessPresetSaveUiDone = 1;
    }
}

//opencode: circular-light-path editor helpers are defined with the viewport
//   authoring utilities below the debug panel.
static gX_I0 gfx_light_orbit_capture_center(gX_RendererGFX_Light *light);
static gX_I0 gfx_light_orbit_panel(gX_RendererGFX_Light *light);

//opencode: point-light orbit controls are authored alongside each light's settings.
static gX_I0 gfx_lights_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::DragFloat3("sun dir", (float *)&gfx->sun.dir, 0.02f, -1.0f, 1.0f);
    ImGui::DragFloat("sun brightness", &gfx->sun.brightness, 0.02f, 0.0f, 8.0f);
    gfx_color_edit("sun color", &gfx->sun.color);
    gfx_color_edit("sun diffuse tint", &gfx->sun.diffuseTint);
    gfx_color_edit("sun specular tint", &gfx->sun.specularTint);
    ImGui::DragFloat("sun diffuse factor", &gfx->sun.diffuseFactor, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("sun specular factor", &gfx->sun.specularFactor, 0.01f, 0.0f, 4.0f);
    gX_I1 sunShadows = gfx->sun.castShadows ? 1 : 0;
    if (ImGui::Checkbox("sun casts shadows", (bool *)&sunShadows)) gfx->sun.castShadows = sunShadows;

    //opencode: heap select list — index validated per frame (remove/add
    //   recycle slots; a stale selection resets instead of dangling).
    gX_U32 nLights = 0;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++)
        if (gfx_light_at(gfx, (gX_I32)li)) nLights++;
    ImGui::Text("heap lights: %u", nLights);
    if (ImGui::Button("Add light")) {
        gX_I32 idx = gX_RendererGFX_Light_Add(gfx);
        if (idx >= 0) {
            s_selLight = idx;
            s_selModel = -1;
            gX_TraceLog(gX_LOG_INFO, "GfxEd: light added at %d", idx);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove selected") && s_selLight >= 0) {
        gX_RendererGFX_Light_Remove(gfx, (gX_U32)s_selLight);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: light %d removed", s_selLight);
        s_selLight = -1;
    }
    //opencode: Sky Lab light studio — classic key/fill/rim triple. Additive
    //   (remove by hand); directional dirs are light-travel directions.
    if (ImGui::Button("3-point studio")) {
        gX_I32 key = gX_RendererGFX_Light_Add(gfx);
        if (key >= 0) {
            gX_RendererGFX_Light *l = &((gX_RendererGFX_Light *)gfx->lights.data)[key];
            l->type = 0; l->dir = (gX_F32v3){-0.5f, -0.75f, -0.45f};
            l->brightness = 2.2f; l->color = (gX_Color){255, 238, 220, 255};
        }
        gX_I32 fill = gX_RendererGFX_Light_Add(gfx);
        if (fill >= 0) {
            gX_RendererGFX_Light *l = &((gX_RendererGFX_Light *)gfx->lights.data)[fill];
            l->type = 0; l->dir = (gX_F32v3){0.65f, -0.25f, 0.35f};
            l->brightness = 0.7f; l->color = (gX_Color){200, 220, 255, 255};
        }
        gX_I32 rim = gX_RendererGFX_Light_Add(gfx);
        if (rim >= 0) {
            gX_RendererGFX_Light *l = &((gX_RendererGFX_Light *)gfx->lights.data)[rim];
            l->type = 0; l->dir = (gX_F32v3){-0.15f, -0.55f, 0.8f};
            l->brightness = 1.4f; l->color = (gX_Color){255, 255, 240, 255};
        }
        gX_TraceLog(gX_LOG_INFO, "GfxEd: 3-point studio added (key=%d fill=%d rim=%d)", key, fill, rim);
    }
    if (s_selLight >= 0 && !gfx_light_at(gfx, s_selLight)) s_selLight = -1;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
        gX_RendererGFX_Light *l = gfx_light_at(gfx, (gX_I32)li);
        if (!l) continue;
        gX_I8 cap[64];
        snprintf(cap, sizeof(cap), "%u: %s b=%.2f", li,
            gfx_light_type_name(l->type), (double)l->brightness);
        if (ImGui::Selectable(cap, (gX_I32)li == s_selLight)) {
            s_selLight = (gX_I32)li;
            s_selModel = -1;
        }
    }
    if (s_selLight >= 0) {
        gX_RendererGFX_Light *l = gfx_light_at(gfx, s_selLight);
        if (!l) return;
        ImGui::Separator();
        ImGui::Text("light %d (%s)", s_selLight, gfx_light_type_name(l->type));
        const gX_I8 *types[3] = { "directional", "point", "spot" };
        int ti = (l->type < 0 || l->type > 2) ? 1 : l->type;
        if (ImGui::Combo("type", &ti, types, 3)) l->type = ti;
        ImGui::BeginDisabled(l->orbitEnabled && l->type == 1);
        ImGui::DragFloat3("pos", (float *)&l->pos, 0.1f);
        ImGui::EndDisabled();
        gfx_light_orbit_panel(l);
        ImGui::DragFloat3("dir", (float *)&l->dir, 0.02f, -1.0f, 1.0f);
        ImGui::DragFloat("brightness", &l->brightness, 0.02f, 0.0f, 8.0f);
        gfx_color_edit("color", &l->color);
        gfx_color_edit("diffuse tint", &l->diffuseTint);
        gfx_color_edit("specular tint", &l->specularTint);
        ImGui::DragFloat("diffuse", &l->diffuseFactor, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("specular", &l->specularFactor, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("atten const", &l->attenConst, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("atten linear", &l->attenLinear, 0.005f, 0.0f, 2.0f);
        ImGui::DragFloat("atten quad", &l->attenQuad, 0.005f, 0.0f, 2.0f);
        ImGui::DragFloat("spot cone FOV", &l->fov, 0.5f, 1.0f, 179.0f);
        gX_I1 sh = l->castShadows ? 1 : 0;
        if (ImGui::Checkbox("cast shadows", (bool *)&sh)) l->castShadows = sh ? 1 : 0;
        ImGui::DragFloat("shadow far", &l->shadowFar, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("shadow range (0 = unlimited)", &l->shadowRange, 0.1f, 0.0f, 500.0f);
        int shadowSize = (int)l->shadowSize;
        if (shadowSize < 128) shadowSize = 128;
        if (shadowSize > 4096) shadowSize = 4096;
        if (ImGui::SliderInt("shadow resolution", &shadowSize, 128, 4096))
            l->shadowSize = (gX_U32)shadowSize;
        gX_I1 cookie = l->cookieEnabled ? 1 : 0;
        if (ImGui::Checkbox("projected cookie", (bool *)&cookie)) {
            l->cookieEnabled = cookie;
            gfx->common.cookieAtlasDirty = 1;
        }
        ImGui::InputText("cookie texture", s_normalTexturePath, sizeof(s_normalTexturePath));
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse##cookie"))
            gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
                gX_GFX_ASSET_TARGET_LIGHT_COOKIE, s_selLight, -1, -1, -1);
        if (ImGui::Button("Set cookie from path")
            && gfx_preset_file_exists(s_normalTexturePath))
            gX_RendererGFX_Light_SetCookie(gfx, l, s_normalTexturePath);
        ImGui::SameLine();
        if (ImGui::Button("Clear cookie")) {
            l->cookieEnabled = 0;
            l->cookiePath[0] = 0;
            gfx->common.cookieAtlasDirty = 1;
        }
        if (l->cookiePath[0]) ImGui::TextDisabled("cookie: %s", l->cookiePath);
    }
}

static gX_I0 gfx_shadow_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Shadow")) return;
    gX_I1 shadows = gfx->state.ENABLE_SHADOWS ? 1 : 0;
    if (ImGui::Checkbox("directional / CSM shadows", (bool *)&shadows))
        gfx->state.ENABLE_SHADOWS = shadows;
    gX_I1 ps = gfx->state.ENABLE_POINT_SHADOWS ? 1 : 0;
    if (ImGui::Checkbox("point shadows", (bool *)&ps))
        gfx->state.ENABLE_POINT_SHADOWS = ps ? 1 : 0;
    ImGui::DragFloat("bias min", &gfx->state.shadowBiasMin, 0.0001f, 0.0f, 0.05f, "%.5f");
    ImGui::DragFloat("bias max", &gfx->state.shadowBiasMax, 0.0001f, 0.0f, 0.05f, "%.5f");
    ImGui::DragFloat("point bias", &gfx->state.pointShadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
    ImGui::DragFloat("point min bias", &gfx->state.pointShadowMinBias, 0.0005f, 0.0f, 0.1f, "%.4f");
    ImGui::DragFloat("point extra", &gfx->state.pointShadowExtra, 0.01f, 0.0f, 2.0f);
}

//opencode: controls for configurable standard-shader material and shadow constants.
static gX_I0 gfx_shader_tuning_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Advanced shader tuning")) return;
    ImGui::DragFloat("parallax min layers", &gfx->state.parallaxMinLayers, 1.0f, 1.0f, 128.0f);
    ImGui::DragFloat("parallax max layers", &gfx->state.parallaxMaxLayers, 1.0f, 1.0f, 128.0f);
    ImGui::DragFloat("parallax offset", &gfx->state.parallaxOffset, 0.001f, 0.0f, 0.1f, "%.4f");
    ImGui::DragFloat("parallax shadow offset", &gfx->state.parallaxShadowOffset, 0.001f, 0.0f, 0.1f, "%.4f");
    ImGui::DragFloat("parallax shadow steps", &gfx->state.parallaxShadowSteps, 1.0f, 1.0f, 64.0f);
    ImGui::DragFloat("tangent length threshold", &gfx->state.tangentLengthThreshold, 0.001f, 0.0f, 1.0f, "%.4f");
    ImGui::DragFloat("normal Y threshold", &gfx->state.normalYThreshold, 0.001f, 0.0f, 1.0f, "%.4f");
    ImGui::TextDisabled("Changes tune material/shadow calculations globally; defaults match the renderer preset.");
}

//ai: collapsing headers do not create ID scopes; disambiguate the FX intensity controls.
static gX_I0 gfx_bloom_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Bloom")) return;
    gX_I1 bloom = gfx->state.ENABLE_BLOOM ? 1 : 0;
    if (ImGui::Checkbox("enable HDR composite + bloom", (bool *)&bloom))
        gfx->state.ENABLE_BLOOM = bloom;
    ImGui::DragFloat("threshold", &gfx->bloom.threshold, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("knee", &gfx->bloom.knee, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("intensity##bloom", &gfx->bloom.intensity, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("blur amount", &gfx->bloom.blurAmount, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("composite exposure", &gfx->bloom.exposure, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("vignette", &gfx->bloom.vignette, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("saturation", &gfx->bloom.saturation, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("contrast", &gfx->bloom.contrast, 0.01f, 0.0f, 2.0f);
    gX_I1 autoExposure = gfx->bloom.autoExposure ? 1 : 0;
    if (ImGui::Checkbox("auto exposure", (bool *)&autoExposure))
        gfx->bloom.autoExposure = autoExposure;
    ImGui::DragFloat("exposure compensation", &gfx->bloom.exposureCompensation, 0.01f, -8.0f, 8.0f);
    ImGui::DragFloat("auto exposure min", &gfx->bloom.exposureMin, 0.01f, 0.01f, 8.0f);
    ImGui::DragFloat("auto exposure max", &gfx->bloom.exposureMax, 0.01f, 0.01f, 16.0f);
}

//ai: make the composite-only tone controls visibly inactive on the raw-blit path.
static gX_I0 gfx_look_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Look")) return;
    ImGui::DragFloat("exposure", &gfx->state.exposure, 0.01f, 0.0f, 4.0f);
    int lightingModel = gfx->state.ENABLE_FLAT ? 2
        : (gfx->state.ENABLE_GOURAUD ? 1 : (gfx->state.ENABLE_PHONG ? 0 : 3));
    const gX_I8 *lightingModels[] = { "Phong", "Gouraud", "Flat", "Unlit" };
    if (ImGui::Combo("lighting model", &lightingModel, lightingModels, 4)) {
        gfx->state.ENABLE_PHONG = lightingModel == 0;
        gfx->state.ENABLE_GOURAUD = lightingModel == 1;
        gfx->state.ENABLE_FLAT = lightingModel == 2;
    }
    gX_I1 ambient = gfx->state.ENABLE_LIGHTING_AMBIENT ? 1 : 0;
    if (ImGui::Checkbox("ambient lighting", (bool *)&ambient)) gfx->state.ENABLE_LIGHTING_AMBIENT = ambient;
    gX_I1 diffuse = gfx->state.ENABLE_LIGHTING_DIFFUSE ? 1 : 0;
    if (ImGui::Checkbox("diffuse lighting", (bool *)&diffuse)) gfx->state.ENABLE_LIGHTING_DIFFUSE = diffuse;
    gX_I1 specular = gfx->state.ENABLE_LIGHTING_SPECULAR ? 1 : 0;
    if (ImGui::Checkbox("specular lighting", (bool *)&specular)) gfx->state.ENABLE_LIGHTING_SPECULAR = specular;
    gX_I1 albedoMap = gfx->state.ENABLE_MAP_ALBEDO ? 1 : 0;
    if (ImGui::Checkbox("albedo maps", (bool *)&albedoMap)) gfx->state.ENABLE_MAP_ALBEDO = albedoMap;
    gX_I1 metalnessMap = gfx->state.ENABLE_MAP_METALNESS ? 1 : 0;
    if (ImGui::Checkbox("metalness maps", (bool *)&metalnessMap)) gfx->state.ENABLE_MAP_METALNESS = metalnessMap;
    gX_I1 normalMap = gfx->state.ENABLE_MAP_NORMAL ? 1 : 0;
    if (ImGui::Checkbox("normal maps", (bool *)&normalMap)) gfx->state.ENABLE_MAP_NORMAL = normalMap;
    gX_I1 parallaxMap = gfx->state.ENABLE_MAP_PARALLAX ? 1 : 0;
    if (ImGui::Checkbox("parallax mapping", (bool *)&parallaxMap)) gfx->state.ENABLE_MAP_PARALLAX = parallaxMap;
    gX_I1 detail = gfx->state.ENABLE_DETAIL ? 1 : 0;
    if (ImGui::Checkbox("detail texture", (bool *)&detail)) gfx->state.ENABLE_DETAIL = detail;
    ImGui::DragFloat("detail UV scale", &gfx->detail.uvScale, 0.05f, 0.01f, 100.0f);
    ImGui::DragFloat("detail strength", &gfx->detail.strength, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("detail distance fade", &gfx->detail.distFade, 0.1f, 0.0f, 1000.0f);
    ImGui::InputText("detail texture path", s_normalTexturePath, sizeof(s_normalTexturePath));
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse##detail"))
        gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
            gX_GFX_ASSET_TARGET_DETAIL, -1, -1, -1, -1);
    if (ImGui::Button("Load detail texture")
        && gfx_preset_file_exists(s_normalTexturePath)
        && strlen(s_normalTexturePath) < sizeof(gfx->detail.mapPath)) {
        if (gfx->detail.map.id > 0) gX_Texture_Unload(gfx->detail.map);
        gfx->detail.map = gX_Texture_Load(s_normalTexturePath);
        strncpy(gfx->detail.mapPath, s_normalTexturePath, sizeof(gfx->detail.mapPath) - 1);
        gfx->detail.mapPath[sizeof(gfx->detail.mapPath) - 1] = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear detail texture")) {
        if (gfx->detail.map.id > 0) gX_Texture_Unload(gfx->detail.map);
        gfx->detail.map = (gX_Texture){0};
        gfx->detail.mapPath[0] = 0;
    }
    ImGui::TextDisabled("detail map: %s", gfx->detail.mapPath[0]
        ? gfx_model_basename(gfx->detail.mapPath) : "none");
    gX_I1 frustum = gfx->state.ENABLE_FRUSTUM_CULLING ? 1 : 0;
    if (ImGui::Checkbox("frustum culling", (bool *)&frustum)) gfx->state.ENABLE_FRUSTUM_CULLING = frustum;
    gX_I1 occlusion = gfx->state.ENABLE_OCCLUSION_CULLING ? 1 : 0;
    if (ImGui::Checkbox("occlusion culling", (bool *)&occlusion)) gfx->state.ENABLE_OCCLUSION_CULLING = occlusion;
    gX_I1 ea = gfx->state.ENABLE_ENV_AMBIENT ? 1 : 0;
    if (ImGui::Checkbox("env ambient", (bool *)&ea))
        gfx->state.ENABLE_ENV_AMBIENT = ea ? 1 : 0;
    ImGui::DragFloat("env ambient mix", &gfx->state.envAmbientFactor, 0.01f, 0.0f, 1.0f);
    gX_I1 ck = gfx->state.ENABLE_COOKIES ? 1 : 0;
    if (ImGui::Checkbox("cookies", (bool *)&ck))
        gfx->state.ENABLE_COOKIES = ck ? 1 : 0;
    gX_I1 rim = gfx->state.ENABLE_RIM ? 1 : 0;
    if (ImGui::Checkbox("rim lighting", (bool *)&rim)) gfx->state.ENABLE_RIM = rim;
    ImGui::DragFloat("rim intensity", &gfx->state.rimIntensity, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("rim power", &gfx->state.rimPower, 0.01f, 0.1f, 8.0f);
    gfx_color_edit("rim color", &gfx->state.rimColor);
    const gX_I8 *tonemappers[] = { "None", "ACES", "AgX" };
    int tonemapper = gfx->state.ENABLE_AGX_TONEMAP ? 2
        : (gfx->state.ENABLE_ACES_TONEMAP ? 1 : 0);
    ImGui::BeginDisabled(!gfx->state.ENABLE_BLOOM);
    if (ImGui::Combo("tone mapper", &tonemapper, tonemappers, 3)) {
        gfx->state.ENABLE_ACES_TONEMAP = tonemapper == 1;
        gfx->state.ENABLE_AGX_TONEMAP = tonemapper == 2;
    }
    gX_I1 gamma = gfx->state.ENABLE_GAMMA_CORRECTION ? 1 : 0;
    if (ImGui::Checkbox("gamma correction", (bool *)&gamma))
        gfx->state.ENABLE_GAMMA_CORRECTION = gamma;
    ImGui::EndDisabled();
    if (!gfx->state.ENABLE_BLOOM)
        ImGui::TextDisabled("Tone mapping and gamma require the HDR composite; enable it in Bloom.");
    gX_I1 clustered = gfx->state.ENABLE_CLUSTERED ? 1 : 0;
    if (ImGui::Checkbox("CPU light clustering", (bool *)&clustered))
        gfx->state.ENABLE_CLUSTERED = clustered;
    gX_I1 instancing = gfx->state.ENABLE_INSTANCING ? 1 : 0;
    if (ImGui::Checkbox("hardware instancing (experimental, GL 4.3)", (bool *)&instancing))
        gfx->state.ENABLE_INSTANCING = instancing;
    ImGui::TextDisabled("Instancing is available only when the selected GL level supports SSBOs.");
}

static gX_I0 gfx_quality_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Quality")) return;
    //opencode: ApplyQualityPreset is runtime-safe (showcase.c cycles it
    //   live); the slider stages, Apply commits.
    int qp = gfx->state.qualityPreset;
    if (qp < 0) qp = 0;
    if (qp > 3) qp = 3;
    if (ImGui::SliderInt("preset (0-3)", &qp, 0, 3)) gfx->state.qualityPreset = qp;
    if (ImGui::Button("Apply preset")) {
        gX_RendererGFX_ApplyQualityPreset(gfx);
        gX_TraceLog(gX_LOG_INFO, "GfxEd: quality preset %d applied", qp);
    }
    const gX_I8 *filters[] = { "Point", "Bilinear", "Trilinear", "Anisotropic 4x", "Anisotropic 8x", "Anisotropic 16x" };
    int filter = gfx->state.textureFilter;
    if (filter < 0 || filter > 5) filter = 2;
    if (ImGui::Combo("texture filter", &filter, filters, 6)) {
        gfx->state.textureFilter = filter;
        gX_RendererGFX_Textures_ApplyFilter(gfx);
    }
    const gX_I8 *shaderQualities[] = { "Low", "Medium", "High" };
    int shaderQuality = gfx->state.shaderQuality;
    if (shaderQuality < 0 || shaderQuality > 2) shaderQuality = 0;
    if (ImGui::Combo("shader quality", &shaderQuality, shaderQualities, 3))
        gfx->state.shaderQuality = shaderQuality;
    ImGui::DragFloat("mipmap bias", &gfx->state.mipmapBias, 0.01f, -4.0f, 4.0f);
    const gX_I8 *msaaLevels[] = { "Off", "2x", "4x", "8x" };
    int msaaIndex = gfx->state.msaaLevel == 2 ? 1 : (gfx->state.msaaLevel == 4 ? 2 : (gfx->state.msaaLevel == 8 ? 3 : 0));
    if (ImGui::Combo("MSAA", &msaaIndex, msaaLevels, 4))
        gfx->state.msaaLevel = msaaIndex == 1 ? 2 : (msaaIndex == 2 ? 4 : (msaaIndex == 3 ? 8 : 0));
    ImGui::TextDisabled("MSAA rebuilds the render target when changed; unsupported GL levels keep single-sample rendering.");
}

//opencode: expose an optional persisted fog-to-void color link.
static gX_I0 gfx_fog_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Fog")) return;
    gX_I1 fe = gfx->state.ENABLE_FOG ? 1 : 0;
    if (ImGui::Checkbox("enable fog", (bool *)&fe))
        gfx->state.ENABLE_FOG = fe ? 1 : 0;
    ImGui::DragFloat("near", &gfx->fog.near, 1.0f, 0.0f, 500.0f);
    ImGui::DragFloat("far", &gfx->fog.far, 5.0f, 1.0f, 2000.0f);
    bool matchVoid = gfx->fog.matchVoidColor != 0;
    if (ImGui::Checkbox("match void color", &matchVoid)) {
        gfx->fog.matchVoidColor = matchVoid ? 1 : 0;
        if (matchVoid) gfx->fog.color = gfx->sky.color;
    }
    ImGui::BeginDisabled(gfx->fog.matchVoidColor != 0);
    gfx_color_edit("color", &gfx->fog.color);
    ImGui::EndDisabled();
    if (gfx->fog.matchVoidColor)
        ImGui::TextDisabled("Linked to Sky > color; disable to keep a manual fog color.");
    ImGui::DragFloat("height density", &gfx->fog.heightDensity, 0.001f, 0.0f, 0.5f, "%.4f");
    ImGui::DragFloat("height base", &gfx->fog.heightBase, 0.1f, -50.0f, 100.0f);
}

static gX_I0 gfx_sky_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Sky")) return;
    gX_I1 skybox = gfx->state.ENABLE_SKYBOX ? 1 : 0;
    if (ImGui::Checkbox("cubemap skybox", (bool *)&skybox)) gfx->state.ENABLE_SKYBOX = skybox;
    ImGui::DragFloat("strength", &gfx->sky.strength, 0.02f, 0.0f, 4.0f);
    gfx_color_edit("color", &gfx->sky.color);
    ImGui::InputText("cubemap texture path", s_normalTexturePath, sizeof(s_normalTexturePath));
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse##skybox"))
        gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
            gX_GFX_ASSET_TARGET_SKYBOX, -1, -1, -1, -1);
    if (ImGui::Button("Set cubemap path") && gfx_preset_file_exists(s_normalTexturePath)
        && strlen(s_normalTexturePath) < sizeof(gfx->sky.skyboxPath)) {
        if (gfx->sky.skybox.wrapper.loadingState.loaded)
            gX_RendererGFX_Texture_Unload(&gfx->sky.skybox);
        strncpy(gfx->sky.skyboxPath, s_normalTexturePath, sizeof(gfx->sky.skyboxPath) - 1);
        gfx->sky.skyboxPath[sizeof(gfx->sky.skyboxPath) - 1] = 0;
        strncpy(gfx->sky.skybox.wrapper.path, gfx->sky.skyboxPath,
            sizeof(gfx->sky.skybox.wrapper.path) - 1);
        gfx->sky.skybox.wrapper.path[sizeof(gfx->sky.skybox.wrapper.path) - 1] = 0;
        gfx->sky.skybox.wrapper.isSkybox = 1;
        gfx->sky.skybox.wrapper.loadingState.loadAsap = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear cubemap path")) {
        if (gfx->sky.skybox.wrapper.loadingState.loaded)
            gX_RendererGFX_Texture_Unload(&gfx->sky.skybox);
        gfx->sky.skyboxPath[0] = 0;
        gfx->sky.skybox.wrapper.path[0] = 0;
        gfx->sky.skybox.wrapper.loadingState.loadAsap = 0;
        gfx->state.ENABLE_SKYBOX = 0;
    }
    ImGui::TextDisabled("cubemap: %s", gfx->sky.skyboxPath[0]
        ? gfx_model_basename(gfx->sky.skyboxPath) : "none");
}

static gX_I0 gfx_skylab_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Sky Lab", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 pe = gfx->state.ENABLE_PROCEDURAL_SKY ? 1 : 0;
    if (ImGui::Checkbox("procedural sky", (bool *)&pe))
        gfx->state.ENABLE_PROCEDURAL_SKY = pe ? 1 : 0;
    //opencode: time-of-day — seed from live sun.dir once, then own it.
    if (!s_sunInit) {
        float il = sqrtf(gfx->sun.dir.x * gfx->sun.dir.x + gfx->sun.dir.y * gfx->sun.dir.y + gfx->sun.dir.z * gfx->sun.dir.z);
        if (il > 0.0001f) {
            float tx = -gfx->sun.dir.x / il, ty = -gfx->sun.dir.y / il, tz = -gfx->sun.dir.z / il;
            if (ty > 1.0f) ty = 1.0f;
            if (ty < -1.0f) ty = -1.0f;
            s_sunElev = asinf(ty) * 57.29578f;
            s_sunAzim = atan2f(tz, tx) * 57.29578f;
            if (s_sunAzim < 0) s_sunAzim += 360.0f;
        }
        s_sunInit = 1;
    }
    if (ImGui::SliderFloat("sun elevation", &s_sunElev, 5.0f, 85.0f, "%.1f deg")
        || ImGui::SliderFloat("sun azimuth", &s_sunAzim, 0.0f, 360.0f, "%.1f deg")) {
        float el = s_sunElev * 0.0174533f, az = s_sunAzim * 0.0174533f;
        float ce = cosf(el);
        gfx->sun.dir.x = -(ce * cosf(az));
        gfx->sun.dir.y = -sinf(el);
        gfx->sun.dir.z = -(ce * sinf(az));
    }
    ImGui::DragFloat("altitude km", &gfx->sky.altitude, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("turbidity", &gfx->sky.turbidity, 0.05f, 1.0f, 10.0f);
    ImGui::DragFloat("density", &gfx->sky.density, 0.02f, 0.1f, 6.0f);
    ImGui::DragFloat("hue", &gfx->sky.hue, 0.01f, -1.0f, 1.0f);
    ImGui::DragFloat("horizon", &gfx->sky.horizon, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("sun intensity", &gfx->sky.sunIntensity, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("contrast", &gfx->sky.contrast, 0.01f, 0.5f, 3.0f);
    ImGui::DragFloat("star density", &gfx->sky.starDensity, 0.02f, 0.0f, 3.0f);
    ImGui::DragFloat("star brightness", &gfx->sky.starBrightness, 0.02f, 0.0f, 3.0f);
    ImGui::DragFloat("star twinkle", &gfx->sky.starTwinkle, 0.02f, 0.0f, 2.0f);
}

static gX_I0 gfx_reflect_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Reflect", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 se = gfx->state.ENABLE_SSR ? 1 : 0;
    if (ImGui::Checkbox("SSR", (bool *)&se)) gfx->state.ENABLE_SSR = se ? 1 : 0;
    ImGui::TextDisabled("SSR is global; Surface > receive SSR opts each model in or out.");
    gX_I1 re = gfx->state.ENABLE_REFLECTIONS ? 1 : 0;
    if (ImGui::Checkbox("environment reflections", (bool *)&re))
        gfx->state.ENABLE_REFLECTIONS = re ? 1 : 0;
    ImGui::TextDisabled("Controls model environment reflections; planar capture has its own gate.");
    gX_I1 planar = gfx->state.ENABLE_PLANAR_REFLECTIONS ? 1 : 0;
    if (ImGui::Checkbox("planar captures", (bool *)&planar))
        gfx->state.ENABLE_PLANAR_REFLECTIONS = planar ? 1 : 0;
    gX_I1 av = gfx->state.ENABLE_SSR_AUXVIEW ? 1 : 0;
    if (ImGui::Checkbox("planar SSR fallback", (bool *)&av))
        gfx->state.ENABLE_SSR_AUXVIEW = av ? 1 : 0;
    gX_I1 normalBuffer = gfx->ssr.normalBufferEnabled ? 1 : 0;
    if (ImGui::Checkbox("raster normal buffer (experimental)", (bool *)&normalBuffer))
        gfx_ssr_normal_buffer_set(gfx, normalBuffer);
    ImGui::TextDisabled("normal target: %s", gfx->ssr.normalMap.wrapper.loadingState.loaded ? "loaded" : "not loaded");
    int ms = gfx->ssr.maxSteps;
    if (ms < 4) ms = 4;
    if (ms > 256) ms = 256;
    if (ImGui::SliderInt("max steps", &ms, 4, 256)) gfx->ssr.maxSteps = ms;
    ImGui::DragFloat("step size", &gfx->ssr.stepSize, 0.005f, 0.005f, 0.5f, "%.3f");
    ImGui::DragFloat("max distance", &gfx->ssr.maxDistance, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("depth bias", &gfx->ssr.depthBias, 0.0005f, 0.0f, 0.05f, "%.4f");
    ImGui::DragFloat("opacity", &gfx->ssr.opacity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("roughness", &gfx->ssr.roughness, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("max lod", &gfx->ssr.maxLod, 0.05f, 0.0f, 8.0f);
    ImGui::DragFloat("edge fade", &gfx->ssr.edgeFade, 0.01f, 0.0f, 1.0f);
    int mb = gfx->ssr.maxBounces;
    if (mb < 0) mb = 0;
    if (mb > 4) mb = 4;
    if (ImGui::SliderInt("bounces", &mb, 0, 4)) gfx->ssr.maxBounces = mb;
    ImGui::DragFloat("bounce atten", &gfx->ssr.bounceAtten, 0.01f, 0.0f, 1.0f);
    if (ImGui::TreeNode("Hit accuracy")) {
        ImGui::DragFloat("maximum depth delta", &gfx->ssr.maxDepthDelta,
            0.0001f, 0.0001f, 0.05f, "%.5f");
        ImGui::DragFloat("world-space gap threshold", &gfx->ssr.worldGapThreshold,
            0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("final depth threshold", &gfx->ssr.finalDepthThreshold,
            0.0001f, 0.0001f, 0.05f, "%.5f");
        ImGui::DragFloat("hit facing threshold", &gfx->ssr.hitFacingThreshold,
            0.01f, -1.0f, 1.0f);
        ImGui::TreePop();
    }
    //opencode: global reflection strength — scales BOTH the planar
    //   mix and the cubemap add (reflectionBias). Was mislabeled
    //   "planar bias" with a 0..0.05 range that snapped the 0.5 default
    //   down on first drag, silently killing env reflections.
    ImGui::DragFloat("reflection bias", &gfx->reflection.bias, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::Text("planar: %s cube: %s",
        gfx->reflection.filledThisFrame ? "fresh" : "stale",
        gfx->reflection.cubeUpdatedThisFrame ? "fresh" : "stale");
}

static gX_I0 gfx_fogvolume_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Fog Volume")) return;
    gX_I1 ve = gfx->state.ENABLE_VOLUME_FOG ? 1 : 0;
    if (ImGui::Checkbox("enable volume fog", (bool *)&ve))
        gfx->state.ENABLE_VOLUME_FOG = ve ? 1 : 0;
    ImGui::DragFloat("density", &gfx->fogVolume.density, 0.001f, 0.001f, 0.1f, "%.4f");
    ImGui::DragFloat("height falloff", &gfx->fogVolume.heightFalloff, 0.05f, 0.1f, 20.0f);
    ImGui::DragFloat("scattering", &gfx->fogVolume.scattering, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("hg asymmetry", &gfx->fogVolume.hgAsymmetry, 0.01f, -0.9f, 0.9f);
}

//ai: browsing remains editor-owned; the leaf water panel receives only this action.
static gX_I0 gfx_water_normal_browse(gX_RendererGFX *gfx)
{
    gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
        gX_GFX_ASSET_TARGET_WATER_NORMAL, -1, -1, -1, -1);
}

static gX_I0 gfx_csm_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Shadows (CSM)")) return;
    int cc = gfx->csm.cascadeCount;
    if (cc < 1) cc = 1;
    if (cc > 4) cc = 4;
    if (ImGui::SliderInt("cascades", &cc, 1, 4)) gfx->csm.cascadeCount = cc;
    ImGui::DragFloat("distance 0", &gfx->csm.cascadeDistances[0], 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("distance 1", &gfx->csm.cascadeDistances[1], 0.5f, 1.0f, 400.0f);
    ImGui::DragFloat("distance 2", &gfx->csm.cascadeDistances[2], 1.0f, 1.0f, 800.0f);
    ImGui::DragFloat("distance 3", &gfx->csm.cascadeDistances[3], 1.0f, 1.0f, 1500.0f);
    ImGui::DragFloat("cascade blend scale", &gfx->csm.blendScale, 0.01f, 0.0f, 2.0f);
}

static gX_I0 gfx_godray_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Godrays")) return;
    gX_I1 ge = gfx->state.ENABLE_GODRAYS ? 1 : 0;
    if (ImGui::Checkbox("enable godrays", (bool *)&ge))
        gfx->state.ENABLE_GODRAYS = ge ? 1 : 0;
    int dn = gfx->godray.density;
    if (ImGui::SliderInt("density", &dn, 1, 128)) gfx->godray.density = dn;
    ImGui::DragFloat("decay", &gfx->godray.decay, 0.005f, 0.5f, 1.0f);
    ImGui::DragFloat("weight", &gfx->godray.weight, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("exposure", &gfx->godray.exposure, 0.01f, 0.0f, 2.0f);
}

//ai: retain the visible intensity label while separating it from bloom's widget ID.
static gX_I0 gfx_lensflare_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Lensflare")) return;
    gX_I1 le = gfx->state.ENABLE_LENS_FLARE ? 1 : 0;
    if (ImGui::Checkbox("enable lensflare", (bool *)&le))
        gfx->state.ENABLE_LENS_FLARE = le ? 1 : 0;
    ImGui::DragFloat("intensity##lensflare", &gfx->lensflare.intensity, 0.02f, 0.0f, 4.0f);
    ImGui::DragFloat("dispersal", &gfx->lensflare.dispersal, 0.02f, 0.0f, 4.0f);
}

static gX_I0 gfx_toggles_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Toggles")) return;
    gX_I1 pp = renderer.state.usePP ? 1 : 0;
    if (ImGui::Checkbox("postprocess", (bool *)&pp))
        renderer.state.usePP = pp ? 1 : 0;
    gX_I1 compositor = renderer.state.useCompositor ? 1 : 0;
    if (ImGui::Checkbox("compositor", (bool *)&compositor))
        renderer.state.useCompositor = compositor;
    gX_I1 taa = gfx->state.ENABLE_TAA ? 1 : 0;
    if (ImGui::Checkbox("TAA", (bool *)&taa)) gfx->state.ENABLE_TAA = taa;
    if (taa) ImGui::DragFloat("taa blend", &gfx->taa.blendFactor, 0.01f, 0.0f, 1.0f);
    gX_I1 fxaa = gfx->state.ENABLE_FXAA ? 1 : 0;
    if (ImGui::Checkbox("FXAA", (bool *)&fxaa)) gfx->state.ENABLE_FXAA = fxaa;
    gX_I1 motionBlur = gfx->state.ENABLE_MOTION_BLUR ? 1 : 0;
    if (ImGui::Checkbox("motion blur", (bool *)&motionBlur))
        gfx->state.ENABLE_MOTION_BLUR = motionBlur;
    if (motionBlur) {
        ImGui::DragFloat("motion blur strength", &gfx->motionBlur.strength, 0.01f, 0.0f, 4.0f);
        ImGui::SliderInt("motion blur samples", &gfx->motionBlur.maxSamples, 1, 64);
    }
    gX_I1 dof = gfx->state.ENABLE_DOF ? 1 : 0;
    if (ImGui::Checkbox("depth of field", (bool *)&dof)) gfx->state.ENABLE_DOF = dof;
    if (dof) {
        ImGui::DragFloat("focal distance", &gfx->dof.focalDistance, 0.05f, 0.1f, 500.0f);
        ImGui::DragFloat("focal range", &gfx->dof.focalRange, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat("maximum blur radius", &gfx->dof.maxRadius, 0.1f, 0.0f, 32.0f);
    }
    gX_I1 splat = gfx->state.ENABLE_SPLAT ? 1 : 0;
    if (ImGui::Checkbox("procedural splat fixture (experimental)", (bool *)&splat))
        gfx->state.ENABLE_SPLAT = splat;
}

//opencode: SSAO/GTAO and SSGI controls include explicit labels for the
//   experimental indirect-light contribution.
static gX_I0 gfx_ssao_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("SSAO / GTAO")) return;
    gX_I1 ssao = gfx->state.ENABLE_SSAO ? 1 : 0;
    if (ImGui::Checkbox("enable ambient occlusion", (bool *)&ssao))
        gfx->state.ENABLE_SSAO = ssao;
    gX_I1 gtao = gfx->ssao.useGTAO ? 1 : 0;
    if (ImGui::Checkbox("horizon-scan GTAO", (bool *)&gtao)) gfx->ssao.useGTAO = gtao;
    ImGui::DragFloat("radius", &gfx->ssao.radius, 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat("bias", &gfx->ssao.bias, 0.001f, 0.0f, 0.5f, "%.4f");
    ImGui::DragFloat("strength", &gfx->ssao.strength, 0.01f, 0.0f, 8.0f);
    ImGui::DragFloat("thickness", &gfx->ssao.thickness, 0.01f, 0.01f, 10.0f);
    int kernel = gfx->ssao.kernelSize;
    if (kernel < 1) kernel = 1;
    if (kernel > 64) kernel = 64;
    if (ImGui::SliderInt("kernel size", &kernel, 1, 64)) gfx->ssao.kernelSize = kernel;
    gX_I1 ssgi = gfx->ssao.useSSGI ? 1 : 0;
    if (ImGui::Checkbox("SSGI (experimental)", (bool *)&ssgi)) gfx->ssao.useSSGI = ssgi;
    ImGui::DragFloat("SSGI intensity", &gfx->ssao.ssgiIntensity, 0.02f, 0.0f, 8.0f);
    ImGui::DragFloat("SSGI bounce gain", &gfx->ssao.ssgiBounceGain, 0.01f, 0.0f, 2.0f);
}

//opencode: free-camera mode uses the renderer scene's existing camera input path.
static gX_I0 gfx_free_camera_set(gX_I1 active) {
    s_freeCameraActive = active ? 1 : 0;
    input.toolCameraInputEnabled = s_freeCameraActive;
    input.cursorEnabled = s_freeCameraActive ? 0 : 1;
    input.cursorSwitched = 1;
}

//opencode: show click-through, context-sensitive keycaps at the viewport edge.
static gX_I0 gfx_editor_shortcut_hint(const char *KeyText,
    const char *ActionText, gX_I1 First) {
    if (!First) ImGui::SameLine(0.0f, 9.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.21f, 0.29f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.21f, 0.29f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.21f, 0.29f, 1.0f));
    ImGui::Button(KeyText);
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled("%s", ActionText);
}

//opencode: keep current viewport, transform, and save shortcuts visible.
//opencode: viewport overlay — the selected camera's position/target markers.
static gX_I0 gfx_editor_camera_markers_draw(gX_I0) {
    if (s_renderApplied) return;
    if (s_selectedCamera < 0 || s_selectedCamera >= s_cameraCount) return;
    gX_Rectangle viewport = gX_GUI_ViewportBox_Get();
    if (viewport.width <= 1.0f || viewport.height <= 1.0f) return;
    const gX_GFX_Editor_Camera *entry = &s_cameras[s_selectedCamera];
    gX_Rectangle positionScreen = gX_GUI_GameToScreen((gX_Rectangle){
        entry->view.position.x, entry->view.position.y, 1.0f, 1.0f});
    gX_Rectangle targetScreen = gX_GUI_GameToScreen((gX_Rectangle){
        entry->view.target.x, entry->view.target.y, 1.0f, 1.0f});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(viewport.x, viewport.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport.width, viewport.height),
        ImGuiCond_Always);
    if (ImGui::Begin("GFX Camera Markers", 0, flags)) {
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 position = ImVec2(origin.x + positionScreen.x - viewport.x,
            origin.y + positionScreen.y - viewport.y);
        ImVec2 target = ImVec2(origin.x + targetScreen.x - viewport.x,
            origin.y + targetScreen.y - viewport.y);
        draw->AddLine(position, target, IM_COL32(120, 220, 255, 120), 1.0f);
        draw->AddCircleFilled(position, 5.0f,
            (s_draggingCamera && s_dragCameraKind == 1)
                ? IM_COL32(255, 220, 120, 255) : IM_COL32(120, 220, 255, 220));
        draw->AddCircleFilled(target, 4.0f,
            (s_draggingCamera && s_dragCameraKind == 2)
                ? IM_COL32(255, 220, 120, 255) : IM_COL32(200, 160, 255, 220));
    }
    ImGui::End();
}

static gX_I0 gfx_editor_shortcut_footer(gX_RendererGFX *Gfx,
    gX_I1 FreeCamera) {
    //opencode: the render capture grabs the frame the camera was applied to,
    //   so keep authoring overlays out of it.
    if (s_renderApplied) return;
    gX_Rectangle Viewport = gX_GUI_ViewportBox_Get();
    gX_F32 leftBound = (!FreeCamera && s_scenePanelOpen) ? s_footerSceneW : Viewport.x;
    gX_F32 rightBound = (!FreeCamera && s_inspectorPanelOpen)
        ? (gX_F32)maindata.windowWidth - s_footerInspectorW
        : Viewport.x + Viewport.width;
    //ai: no safe stage area at high zoom/narrow widths; Help keeps the shortcuts.
    if (rightBound <= leftBound + 260.0f) return;
    gX_F32 MaxWidth = rightBound - leftBound - 24.0f;
    if (MaxWidth < 320.0f) return;
    if (MaxWidth > 460.0f) MaxWidth = 460.0f;
    ImGui::SetNextWindowPos(
        ImVec2((leftBound + rightBound) * 0.5f,
            Viewport.y + Viewport.height - 10.0f),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(MaxWidth, 0.0f),
        ImVec2(MaxWidth, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.045f, 0.065f, 0.96f));
    ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("GFX Shortcut Footer", 0, Flags)) {
        gX_RendererGFX_Model *Model = Gfx ? gfx_model_at(Gfx, s_selModel) : 0;
        gX_RendererGFX_Light *Light = Gfx ? gfx_light_at(Gfx, s_selLight) : 0;
        gX_I1 Compact = Viewport.width < 640.0f
            || ImGui::GetStyle().FontScaleMain > 1.2f;
        const char *Status = FreeCamera ? "FREE CAMERA"
            : (s_draggingModel
                ? (s_dragMode == gX_GFX_EDITOR_DRAG_ROTATE ? "ROTATE MODEL"
                    : (s_dragMode == gX_GFX_EDITOR_DRAG_SCALE ? "SCALE MODEL"
                        : "MOVE MODEL"))
                : (s_draggingLight ? "MOVE LIGHT"
                    : (Model ? "MODEL SELECTED"
                        : (Light ? "LIGHT SELECTED" : "EDIT SHORTCUTS"))));
        ImGui::TextColored(ImVec4(0.52f, 0.78f, 1.0f, 1.0f), "%s", Status);
        if (Compact) {
            if (FreeCamera) {
                ImGui::TextWrapped("WASD move · E/Q rise/fall · mouse look · Shift faster · Ctrl slower · wheel speed · Esc exit");
            } else if (s_draggingModel) {
                ImGui::TextWrapped("R rotate · S scale · X/Y/Z constrain · V view plane · Ctrl snap · release LMB to finish");
            } else if (s_draggingLight) {
                ImGui::TextWrapped("X/Y/Z constrain · V view plane · Ctrl snap · release LMB to finish");
            } else if (Model) {
                ImGui::TextWrapped("LMB move · X/Y/Z axis · V view plane · R rotate · S scale · Ctrl snap · Ctrl+Z undo · F frame · MMB orbit · RMB pan · wheel zoom");
            } else if (Light) {
                ImGui::TextWrapped("LMB move · X/Y/Z axis · V view plane · Ctrl snap · Ctrl+Z undo · arrows nudge · F frame · MMB orbit · RMB pan · wheel zoom");
            } else {
                ImGui::TextWrapped("LMB select · MMB orbit · RMB pan · wheel zoom · Ctrl+S save · Ctrl+O load");
            }
        } else if (FreeCamera) {
            gfx_editor_shortcut_hint("WASD", "move", 1);
            gfx_editor_shortcut_hint("E/Q", "rise / fall", 0);
            gfx_editor_shortcut_hint("Mouse", "look", 0);
            gfx_editor_shortcut_hint("Shift", "faster", 0);
            gfx_editor_shortcut_hint("Ctrl", "slower", 0);
            ImGui::NewLine();
            gfx_editor_shortcut_hint("Wheel", "speed", 1);
            gfx_editor_shortcut_hint("Esc", "return to editor", 0);
        } else if (s_draggingModel || s_draggingLight) {
            gfx_editor_shortcut_hint("X/Y/Z", "constrain move", 1);
            if (s_draggingModel) {
                gfx_editor_shortcut_hint("R", "rotate", 0);
                gfx_editor_shortcut_hint("S", "scale", 0);
            }
            gfx_editor_shortcut_hint("V", "view plane", 0);
            gfx_editor_shortcut_hint("Ctrl", "snap", 0);
            gfx_editor_shortcut_hint("Release LMB", "finish drag", 0);
        } else if (Model) {
            gfx_editor_shortcut_hint("LMB", "select / drag move", 1);
            gfx_editor_shortcut_hint("X/Y/Z", "axis", 0);
            gfx_editor_shortcut_hint("V", "view plane", 0);
            gfx_editor_shortcut_hint("R", "rotate", 0);
            gfx_editor_shortcut_hint("S", "scale", 0);
            gfx_editor_shortcut_hint("Ctrl", "snap", 0);
            gfx_editor_shortcut_hint("Ctrl+Z", "undo", 0);
            gfx_editor_shortcut_hint("Del", "remove", 0);
            ImGui::NewLine();
            gfx_editor_shortcut_hint("MMB", "orbit", 1);
            gfx_editor_shortcut_hint("RMB", "pan", 0);
            gfx_editor_shortcut_hint("Wheel", "zoom", 0);
            gfx_editor_shortcut_hint("F", "frame", 0);
            gfx_editor_shortcut_hint("Ctrl+S", "save", 0);
            gfx_editor_shortcut_hint("Ctrl+O", "load", 0);
        } else if (Light) {
            gfx_editor_shortcut_hint("LMB", "select / drag move", 1);
            gfx_editor_shortcut_hint("X/Y/Z", "axis", 0);
            gfx_editor_shortcut_hint("V", "view plane", 0);
            gfx_editor_shortcut_hint("Ctrl", "snap", 0);
            gfx_editor_shortcut_hint("Ctrl+Z", "undo", 0);
            gfx_editor_shortcut_hint("Arrows", "nudge", 0);
            gfx_editor_shortcut_hint("Shift", "10x step", 0);
            gfx_editor_shortcut_hint("Del", "remove", 0);
            ImGui::NewLine();
            gfx_editor_shortcut_hint("MMB", "orbit", 1);
            gfx_editor_shortcut_hint("RMB", "pan", 0);
            gfx_editor_shortcut_hint("Wheel", "zoom", 0);
            gfx_editor_shortcut_hint("F", "frame", 0);
            gfx_editor_shortcut_hint("Ctrl+S", "save", 0);
            gfx_editor_shortcut_hint("Ctrl+O", "load", 0);
        } else {
            gfx_editor_shortcut_hint("LMB", "select", 1);
            gfx_editor_shortcut_hint("Ctrl+S", "save", 0);
            gfx_editor_shortcut_hint("Ctrl+O", "load", 0);
            ImGui::NewLine();
            gfx_editor_shortcut_hint("MMB", "orbit", 1);
            gfx_editor_shortcut_hint("RMB", "pan", 0);
            gfx_editor_shortcut_hint("Wheel", "zoom", 0);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

//opencode: keep the viewport visible and show the keyboard exit while camera
//   input owns the cursor.
static gX_I0 gfx_free_camera_overlay(gX_I0) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("GFX Free Camera", 0, flags)) {
        ImGui::TextUnformatted("Free camera");
        ImGui::TextUnformatted("Esc returns to the editor");
    }
    ImGui::End();
}

//opencode: camera framing is part of an authored scene and is persisted with
//   the GFX preset's camera object.
static gX_I0 gfx_camera_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::Button("Enter free camera", ImVec2(-FLT_MIN, 0.0f))) {
        gfx_free_camera_set(1);
        return;
    }
    ImGui::TextDisabled("Viewport: middle-drag orbit; Shift+middle-drag pan; wheel zoom.");
    ImGui::TextDisabled("Free camera: WASD move, E/Q rise and fall, mouse to look; Esc returns.");
    ImGui::DragFloat3("position", (float *)&camera.camera3D.position, 0.05f);
    ImGui::DragFloat3("target", (float *)&camera.camera3D.target, 0.05f);
    ImGui::DragFloat3("up", (float *)&camera.camera3D.up, 0.01f);
    ImGui::SliderFloat("vertical FOV", &camera.camera3D.fovy, 10.0f, 120.0f, "%.1f deg");
    const gX_I8 *projections[] = { "Perspective", "Orthographic" };
    int projection = camera.camera3D.projection == CAMERA_ORTHOGRAPHIC ? 1 : 0;
    if (ImGui::Combo("projection", &projection, projections, 2))
        camera.camera3D.projection = projection ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    if (ImGui::Button("Frame selected")) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
        gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
        if (model) gfx_camera_frame_model(model);
        else if (light) gfx_camera_frame_light(light);
    }
}

//opencode: safe access for decal heap selections after removal or preset load.
static gX_RendererGFX_Decal *gfx_decal_at(gX_RendererGFX *gfx, gX_I32 index) {
    if (!gfx || !gfx->decals.data || !gfx->decals.meta
        || index < 0 || (gX_U32)index >= gfx->decals.amount) return 0;
    if (!gfx->decals.meta[index].USED) return 0;
    return &((gX_RendererGFX_Decal *)gfx->decals.data)[index];
}

//opencode: safe access for preset-backed emitter heap selections.
static gX_RendererGFX_Emitter *gfx_emitter_at(gX_RendererGFX *gfx, gX_I32 index) {
    if (!gfx || !gfx->particle.emitters.data || !gfx->particle.emitters.meta
        || index < 0 || (gX_U32)index >= gfx->particle.emitters.amount) return 0;
    if (!gfx->particle.emitters.meta[index].USED) return 0;
    return &((gX_RendererGFX_Emitter *)gfx->particle.emitters.data)[index];
}

//opencode: editor controls for all three deterministic GL33 weather layers.
static gX_I0 gfx_weather_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Weather")) return;
    gX_I1 snowfall = gfx->state.ENABLE_SNOWFALL ? 1 : 0;
    if (ImGui::Checkbox("snowfall", (bool *)&snowfall)) gfx->state.ENABLE_SNOWFALL = snowfall;
    int snowCount = gfx->weather.targetCount;
    if (snowCount < 0) snowCount = 0;
    if (snowCount > gX_WEATHER_SNOW_MAX) snowCount = gX_WEATHER_SNOW_MAX;
    if (ImGui::SliderInt("snow count", &snowCount, 0, gX_WEATHER_SNOW_MAX))
        gfx->weather.targetCount = snowCount;
    ImGui::DragFloat2("snow wind XZ", &gfx->weather.windX, 0.01f, -20.0f, 20.0f);
    ImGui::DragFloat("snow fall speed", &gfx->weather.fallSpeed, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloat("snow floor Y", &gfx->weather.floorY, 0.05f, -100.0f, 100.0f);
    ImGui::DragFloat("snow area", &gfx->weather.halfExtent, 0.1f, 1.0f, 1000.0f);
    ImGui::DragFloat("snow spawn height", &gfx->weather.spawnTop, 0.1f, 0.0f, 2000.0f);

    gX_I1 rain = gfx->state.ENABLE_RAIN ? 1 : 0;
    if (ImGui::Checkbox("rain", (bool *)&rain)) gfx->state.ENABLE_RAIN = rain;
    int rainCount = gfx->weather.rainTarget;
    if (rainCount < 0) rainCount = 0;
    if (rainCount > gX_WEATHER_RAIN_MAX) rainCount = gX_WEATHER_RAIN_MAX;
    if (ImGui::SliderInt("rain count", &rainCount, 0, gX_WEATHER_RAIN_MAX))
        gfx->weather.rainTarget = rainCount;
    ImGui::DragFloat2("rain wind XZ", &gfx->weather.rainWindX, 0.01f, -40.0f, 40.0f);
    ImGui::DragFloat("rain fall speed", &gfx->weather.rainFall, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("rain streak length", &gfx->weather.rainLength, 0.01f, 0.0f, 20.0f);

    gX_I1 fogSprites = gfx->state.ENABLE_FOGSPRITE ? 1 : 0;
    if (ImGui::Checkbox("fog sprites", (bool *)&fogSprites)) gfx->state.ENABLE_FOGSPRITE = fogSprites;
    int fogCount = gfx->weather.fogTarget;
    if (fogCount < 0) fogCount = 0;
    if (fogCount > gX_WEATHER_FOGSPRITE_MAX) fogCount = gX_WEATHER_FOGSPRITE_MAX;
    if (ImGui::SliderInt("fog sprite count", &fogCount, 0, gX_WEATHER_FOGSPRITE_MAX))
        gfx->weather.fogTarget = fogCount;
    ImGui::DragFloat2("fog wind XZ", &gfx->weather.fogWindX, 0.01f, -10.0f, 10.0f);
    ImGui::DragFloat("fog opacity", &gfx->weather.fogOpacity, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("fog scale", &gfx->weather.fogScale, 0.01f, 0.01f, 20.0f);
}

//opencode: compute-particle emitter authoring; emitters are included in presets.
static gX_I0 gfx_particles_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("GPU particles (experimental)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 enabled = gfx->state.ENABLE_PARTICLES ? 1 : 0;
    if (ImGui::Checkbox("enable particle system", (bool *)&enabled))
        gfx->state.ENABLE_PARTICLES = enabled;
    //opencode: UX pass — report the real capability: the emitter list is
    //   authored fine at any level, but nothing renders without compute.
    {
        const gX_RHI_Caps *caps = gX_RHI_GetCaps();
        if (caps && caps->hasCompute && caps->hasSSBO) {
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                "compute: available (GL %d.%d) - emitters render",
                caps->glMajor, caps->glMinor);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "compute unavailable at this GL level - restart with "
                "GX_GL43_CONTEXT=1");
        }
    }
    ImGui::Text("emitters: %u / %u", gfx->particle.emitters.amount, gX_GFX_MAX_EMITTERS);
    if (ImGui::Button("Add emitter")) {
        s_selEmitter = gX_RendererGFX_Emitter_Add(gfx);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove emitter") && gfx_emitter_at(gfx, s_selEmitter)) {
        gX_RendererGFX_Emitter_Remove(gfx, (gX_U32)s_selEmitter);
        s_selEmitter = -1;
    }
    for (gX_U32 ei = 0; ei < gfx->particle.emitters.amount; ei++) {
        gX_RendererGFX_Emitter *emitter = gfx_emitter_at(gfx, (gX_I32)ei);
        if (!emitter) continue;
        gX_I8 label[64];
        snprintf(label, sizeof(label), "%u: rate %.1f", ei, (double)emitter->emitRate);
        if (ImGui::Selectable(label, s_selEmitter == (gX_I32)ei)) s_selEmitter = (gX_I32)ei;
    }
    gX_RendererGFX_Emitter *emitter = gfx_emitter_at(gfx, s_selEmitter);
    if (!emitter) return;
    gX_I1 active = emitter->active ? 1 : 0;
    if (ImGui::Checkbox("emitting", (bool *)&active)) emitter->active = active;
    ImGui::DragFloat("emit rate", &emitter->emitRate, 0.1f, 0.0f, 1000.0f);
    ImGui::DragFloat3("emitter position", (float *)&emitter->pos, 0.05f);
    ImGui::DragFloat3("base velocity", (float *)&emitter->baseVel, 0.05f);
    ImGui::DragFloat("spread", &emitter->spread, 0.5f, 0.0f, 180.0f);
    ImGui::DragFloat("particle lifetime", &emitter->life, 0.05f, 0.05f, 120.0f);
    ImGui::DragFloat("speed", &emitter->speed, 0.05f, 0.0f, 100.0f);
    float color[4] = { emitter->color.x, emitter->color.y, emitter->color.z, emitter->color.w };
    if (ImGui::ColorEdit4("particle color", color))
        emitter->color = (gX_F32v4){color[0], color[1], color[2], color[3]};
}

//opencode: place and edit persistent decals from a selected model or camera target.
static gX_I0 gfx_decals_panel(gX_RendererGFX *gfx) {
    if (!ImGui::CollapsingHeader("Decals", ImGuiTreeNodeFlags_DefaultOpen)) return;
    gX_I1 enabled = gfx->state.ENABLE_DECALS ? 1 : 0;
    if (ImGui::Checkbox("enable decals", (bool *)&enabled)) gfx->state.ENABLE_DECALS = enabled;
    ImGui::DragFloat("new decal size", &s_decalSize, 0.05f, 0.01f, 100.0f);
    if (ImGui::Button("Place on selected model")) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
        gX_F32v3 position = model ? model->params.pos : camera.camera3D.target;
        position.y += 0.02f;
        s_selDecal = gX_RendererGFX_Decal_Add(gfx, position,
            (gX_F32v3){0.0f, 1.0f, 0.0f}, s_decalSize, -1, -1.0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove all decals")) {
        gX_RendererGFX_Decal_RemoveAll(gfx);
        s_selDecal = -1;
    }
    for (gX_U32 di = 0; di < gfx->decals.amount; di++) {
        gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, (gX_I32)di);
        if (!decal) continue;
        gX_I8 label[64];
        snprintf(label, sizeof(label), "%u: size %.2f", di, (double)decal->size);
        if (ImGui::Selectable(label, s_selDecal == (gX_I32)di)) s_selDecal = (gX_I32)di;
    }
    gX_RendererGFX_Decal *decal = gfx_decal_at(gfx, s_selDecal);
    if (!decal) return;
    ImGui::DragFloat3("decal position", (float *)&decal->pos, 0.05f);
    ImGui::DragFloat3("decal normal", (float *)&decal->normal, 0.01f, -1.0f, 1.0f);
    ImGui::DragFloat("decal size", &decal->size, 0.05f, 0.01f, 100.0f);
    ImGui::SliderAngle("decal rotation", &decal->rotation, -180.0f, 180.0f);
    ImGui::DragFloat("decal opacity", &decal->alpha, 0.01f, 0.0f, 1.0f);
    gfx_texture_combo("decal texture", gfx, &decal->textureIndex,
        gX_GFX_ASSET_TARGET_DECAL, s_selDecal, -1, -1);
    gX_I1 snow = decal->snowLayer ? 1 : 0;
    if (ImGui::Checkbox("snow material layer", (bool *)&snow)) decal->snowLayer = snow;
    if (ImGui::Button("Remove selected decal")) {
        gfx->decals.meta[s_selDecal].USED = 0;
        s_selDecal = -1;
    }
    ImGui::SeparatorText("Bake (tooling)");
    ImGui::DragInt("bake size##decal", &s_decalBakeSize, 4.0f, 64, 2048);
    ImGui::DragFloat("bake roughness##decal", &s_decalBakeRough, 0.01f,
        0.0f, 1.0f);
    ImGui::DragFloat("bake metalness##decal", &s_decalBakeMetal, 0.01f,
        0.0f, 1.0f);
    gfx_color_edit("bake color##decal", &s_decalBakeColor);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##decalBakeName", "bake name", s_decalBakeName,
        sizeof(s_decalBakeName));
    if (ImGui::Button("Bake decals##decal")) {
        gX_I8 outgoing[640];
        if (gfx_decal_bake(gfx, s_decalBakeSize, s_decalBakeRough,
                s_decalBakeMetal, s_decalBakeColor, NULL, s_decalBakeName,
                outgoing, sizeof(outgoing))) {
            //ai: status buffers are small and paths long — bound the %s so
            //   the truncation is explicit (prefix + precision + NUL fits).
            snprintf((char *)s_decalBakeStatus, sizeof(s_decalBakeStatus),
                "wrote %.*s", (gX_I32)sizeof(s_decalBakeStatus) - 16,
                (const char *)outgoing);
            s_decalBakeStatusOK = 1;
        } else {
            snprintf((char *)s_decalBakeStatus, sizeof(s_decalBakeStatus),
                "bake failed (place a decal first)");
            s_decalBakeStatusOK = 0;
        }
    }
    ImGui::TextDisabled("albedo + ORM, top-down XZ projection");
    if (s_decalBakeStatus[0]) {
        if (s_decalBakeStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                (const char *)s_decalBakeStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                (const char *)s_decalBakeStatus);
    }
}

//opencode: v0.6.11 — viewport and buffer inspection controls make lighting
//   and post-process problems observable without keyboard-only showcase binds.
static gX_I0 gfx_debug_panel(gX_RendererGFX *gfx) {
    if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
        gX_I1 grid = gfx->state.SHOW_GRID ? 1 : 0;
        if (ImGui::Checkbox("grid", (bool *)&grid)) gfx->state.SHOW_GRID = grid ? 1 : 0;
        gX_I1 gizmo = gfx->state.SHOW_GIZMO ? 1 : 0;
        if (ImGui::Checkbox("gizmo", (bool *)&gizmo)) gfx->state.SHOW_GIZMO = gizmo ? 1 : 0;
        gX_I1 boxes = gfx->state.SHOW_BOUNDING_BOX ? 1 : 0;
        if (ImGui::Checkbox("bounding boxes", (bool *)&boxes)) gfx->state.SHOW_BOUNDING_BOX = boxes ? 1 : 0;
        gX_I1 skeleton = gfx->state.SHOW_SKELETON ? 1 : 0;
        if (ImGui::Checkbox("skeleton", (bool *)&skeleton)) gfx->state.SHOW_SKELETON = skeleton ? 1 : 0;
        gX_I1 markers = gfx->state.SHOW_LIGHT_MARKERS ? 1 : 0;
        if (ImGui::Checkbox("light markers", (bool *)&markers)) gfx->state.SHOW_LIGHT_MARKERS = markers ? 1 : 0;
        gX_I1 wire = gfx->state.ENABLE_WIREFRAME ? 1 : 0;
        if (ImGui::Checkbox("wireframe", (bool *)&wire)) gfx->state.ENABLE_WIREFRAME = wire ? 1 : 0;
    }
    if (ImGui::CollapsingHeader("Vertex attribute inspection")) {
        gX_I1 position = gfx->state.SHOW_POSITION ? 1 : 0;
        if (ImGui::Checkbox("position", (bool *)&position)) gfx->state.SHOW_POSITION = position;
        gX_I1 uv0 = gfx->state.SHOW_TEXCOORD ? 1 : 0;
        if (ImGui::Checkbox("UV0", (bool *)&uv0)) gfx->state.SHOW_TEXCOORD = uv0;
        gX_I1 uv1 = gfx->state.SHOW_TEXCOORD2 ? 1 : 0;
        if (ImGui::Checkbox("UV1", (bool *)&uv1)) gfx->state.SHOW_TEXCOORD2 = uv1;
        gX_I1 normal = gfx->state.SHOW_NORMAL ? 1 : 0;
        if (ImGui::Checkbox("normal", (bool *)&normal)) gfx->state.SHOW_NORMAL = normal;
        gX_I1 color = gfx->state.SHOW_COLOR ? 1 : 0;
        if (ImGui::Checkbox("vertex color", (bool *)&color)) gfx->state.SHOW_COLOR = color;
        gX_I1 tangent = gfx->state.SHOW_TANGENT ? 1 : 0;
        if (ImGui::Checkbox("tangent", (bool *)&tangent)) gfx->state.SHOW_TANGENT = tangent;
        ImGui::TextDisabled("Attribute views replace normal shading and are intended for inspection.");
    }
    if (ImGui::CollapsingHeader("Buffer inspection")) {
        gX_I1 depth = gfx->state.SHOW_DEPTH_VISUALIZER ? 1 : 0;
        if (ImGui::Checkbox("depth visualizer", (bool *)&depth)) gfx->state.SHOW_DEPTH_VISUALIZER = depth ? 1 : 0;
        int source = gfx->state.depthVisSource;
        if (source < 0) source = 0;
        if (source > 2) source = 2;
        if (ImGui::SliderInt("depth source", &source, 0, 2)) gfx->state.depthVisSource = source;
        gX_I1 ssr = gfx->state.SHOW_SSR_VISUALIZER ? 1 : 0;
        if (ImGui::Checkbox("SSR visualizer", (bool *)&ssr)) gfx->state.SHOW_SSR_VISUALIZER = ssr ? 1 : 0;
        int ssrModeIndex = gfx_ssr_debug_index(gfx->state.ssrDebugMode);
        if (ImGui::Combo("SSR debug mode", &ssrModeIndex, s_ssrDebugModeNames,
                (int)(sizeof(s_ssrDebugModeNames) / sizeof(s_ssrDebugModeNames[0]))))
            gfx->state.ssrDebugMode = s_ssrDebugModes[ssrModeIndex];
        if (gfx->ssr.normalMap.wrapper.loadingState.loaded) {
            ImGui::Text("Normal buffer preview");
            Texture preview = {0};
            preview.id = gfx->ssr.normalMap.wrapper.rt.texture.id;
            preview.width = gfx->ssr.normalMap.wrapper.rt.texture.width;
            preview.height = gfx->ssr.normalMap.wrapper.rt.texture.height;
            rlImGuiImageRect(&preview, 240, 180,
                Rectangle{0.0f, 0.0f, (float)preview.width, -(float)preview.height});
        }
    }
}

//opencode: expose the independent GL43 path-tracing experiment without
//   serializing a diagnostic viewport choice into authored scene presets.
static gX_I0 gfx_ray_preview_panel(gX_RendererGFX *gfx) {
    if (!gX_RendererGFX_OracleGPU_Available()) {
        ImGui::TextWrapped("Ray preview requires GL 4.3 compute/SSBO; raster remains active.");
        return;
    }
    static const char *modes[] = {"Raster", "Ray preview", "Side by side"};
    int mode = gfx->oracleGpu.previewMode;
    if (mode < GX_GFX_RAY_PREVIEW_RASTER
        || mode > GX_GFX_RAY_PREVIEW_SIDE_BY_SIDE)
        mode = GX_GFX_RAY_PREVIEW_RASTER;
    if (ImGui::Combo("view", &mode, modes, 3)) {
        gfx->oracleGpu.previewMode = mode;
        gfx->oracleGpu.previewSamples = 0;
        gfx->oracleGpu.sampleSignature = 0;
    }
    if (mode == GX_GFX_RAY_PREVIEW_RASTER) {
        ImGui::TextDisabled("Raster view: preview idle.");
        return;
    }
    if (ImGui::Button("Reset samples")) {
        gfx->oracleGpu.previewSamples = 0;
        gfx->oracleGpu.sampleSignature = 0;
    }
    ImGui::Text("Samples: %u/256", gfx->oracleGpu.previewSamples);
    ImGui::Text("Target: %u x %u HDR", gfx->oracleGpu.previewWidth,
        gfx->oracleGpu.previewHeight);
    ImGui::TextWrapped("Opaque RGBA8 base-color maps with simple lighting. Other effects are unsupported.");
}

//opencode: seed Catmull-Rom-equivalent Bezier handles (P_next - P_prev)/6 so
//   enabling the mode preserves the current curve shape; non-zero handles are
//   kept as authored. The in-handles start as a mirror of the out side so the
//   curve does not jump when independence is enabled later. Shared by the
//   model and light path panels.
static gX_I0 gfx_path_seed_tangents(gX_F32 *waypoints, gX_I32 count,
    gX_F32 *tangents, gX_F32 *tangentsIn, gX_I32 maxWaypoints) {
    if (!waypoints || !tangents || count < 2) return;
    if (count > maxWaypoints) count = maxWaypoints;
    for (gX_I32 i = 0; i < count * 3; i++)
        if (tangents[i] != 0.0f) return;
    for (gX_I32 i = 0; i < count; i++) {
        gX_I32 prev = i > 0 ? i - 1 : i;
        gX_I32 next = i + 1 < count ? i + 1 : i;
        tangents[i * 3] = (waypoints[next * 3] - waypoints[prev * 3]) / 6.0f;
        tangents[i * 3 + 1] =
            (waypoints[next * 3 + 1] - waypoints[prev * 3 + 1]) / 6.0f;
        tangents[i * 3 + 2] =
            (waypoints[next * 3 + 2] - waypoints[prev * 3 + 2]) / 6.0f;
    }
    if (tangentsIn)
        memcpy(tangentsIn, tangents, (size_t)count * 3 * sizeof(gX_F32));
}

//opencode: keep the light at its current point when first enabling or
//   re-anchoring a circle, regardless of its phase and selected plane.
static gX_I0 gfx_light_orbit_capture_center(gX_RendererGFX_Light *light) {
    gX_F32 radius = fmaxf(0.0f, light->orbitRadius);
    gX_F32 phase = isfinite(light->orbitPhase) ? light->orbitPhase : 0.0f;
    gX_F32 radians = phase * gX_DEG2RAD;
    gX_F32 x = cosf(radians) * radius;
    gX_F32 y = sinf(radians) * radius;
    light->orbitElapsed = 0.0;
    switch (light->orbitPlane) {
        case 0:
            light->orbitCenter = (gX_F32v3){light->pos.x - x,
                light->pos.y - y, light->pos.z};
            break;
        case 2:
            light->orbitCenter = (gX_F32v3){light->pos.x,
                light->pos.y - x, light->pos.z - y};
            break;
        default:
            light->orbitCenter = (gX_F32v3){light->pos.x - x,
                light->pos.y, light->pos.z - y};
            break;
    }
}

//opencode: point-light path authoring exposes circular and waypoint motion
//   next to the selected light's ordinary properties.
static gX_I0 gfx_light_orbit_panel(gX_RendererGFX_Light *light) {
    if (!ImGui::CollapsingHeader("Motion path", ImGuiTreeNodeFlags_DefaultOpen)) return;
    bool enabled = light->orbitEnabled != 0;
    if (ImGui::Checkbox("Enable motion path", &enabled)) {
        light->orbitEnabled = enabled ? 1 : 0;
        if (enabled && light->type == 1 && light->pathMode == 0)
            gfx_light_orbit_capture_center(light);
    }
    if (light->type != 1) {
        ImGui::TextDisabled("Motion paths currently animate point lights only.");
        return;
    }
    const gX_I8 *modes[] = {"Circle", "Waypoints"};
    int mode = light->pathMode == 1 ? 1 : 0;
    if (ImGui::Combo("mode", &mode, modes, 2)) {
        light->pathMode = mode;
        light->orbitElapsed = 0.0;
        light->orbitPaused = 0;
        if (mode == 0) {
            gfx_light_orbit_capture_center(light);
        } else if (light->pathWaypointCount < 2) {
            gX_I32 count = light->pathWaypointCount;
            if (count < 1) {
                light->pathWaypoints[0] = light->pos.x;
                light->pathWaypoints[1] = light->pos.y;
                light->pathWaypoints[2] = light->pos.z;
                count = 1;
            }
            gX_I32 last = (count - 1) * 3;
            light->pathWaypoints[count * 3] = light->pathWaypoints[last] + 2.0f;
            light->pathWaypoints[count * 3 + 1] = light->pathWaypoints[last + 1];
            light->pathWaypoints[count * 3 + 2] = light->pathWaypoints[last + 2];
            light->pathWaypointCount = count + 1;
        }
    }
    bool paused = light->orbitPaused != 0;
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button(paused ? "Play" : "Pause")) {
        light->orbitPaused = paused ? 0 : 1;
    }
    ImGui::SameLine();
    bool loop = light->orbitLoop != 0;
    if (ImGui::Checkbox("Loop", &loop)) light->orbitLoop = loop ? 1 : 0;
    if (mode == 0) {
        const gX_I8 *planes[] = {"XY", "XZ", "YZ"};
        int plane = light->orbitPlane;
        if (plane < 0 || plane > 2) plane = 1;
        if (ImGui::Combo("plane", &plane, planes, 3)) light->orbitPlane = plane;
        ImGui::DragFloat3("center", (float *)&light->orbitCenter,
            0.05f, -1000.0f, 1000.0f);
        ImGui::DragFloat("radius", &light->orbitRadius, 0.05f, 0.0f, 1000.0f);
        ImGui::DragFloat("speed (degrees/sec)", &light->orbitSpeed,
            1.0f, -720.0f, 720.0f);
        ImGui::SliderFloat("start phase (degrees)", &light->orbitPhase, -360.0f, 360.0f);
        if (ImGui::Button("Keep current position on path"))
            gfx_light_orbit_capture_center(light);
    } else {
        ImGui::DragFloat("speed (units/sec)", &light->orbitSpeed,
            0.1f, 0.0f, 100.0f);
        bool pingPong = light->pathPingPong != 0;
        if (ImGui::Checkbox("Ping-pong", &pingPong))
            light->pathPingPong = pingPong ? 1 : 0;
        bool smooth = light->pathSmooth != 0;
        if (ImGui::Checkbox("Smooth (Catmull-Rom)", &smooth))
            light->pathSmooth = smooth ? 1 : 0;
        bool bezier = light->pathBezier != 0;
        if (ImGui::Checkbox("Bezier handles", &bezier)) {
            light->pathBezier = bezier ? 1 : 0;
            if (bezier)
                gfx_path_seed_tangents(light->pathWaypoints,
                    light->pathWaypointCount, light->pathTangents,
                    light->pathTangentsIn, gX_GFX_LIGHT_WAYPOINTS);
        }
        gX_I1 independent = light->pathHandlesIndependent != 0;
        if (bezier && ImGui::Checkbox("Independent in/out", (bool *)&independent)) {
            light->pathHandlesIndependent = independent ? 1 : 0;
            if (independent)
                gfx_path_mirror_in(light->pathTangents, light->pathTangentsIn,
                    light->pathWaypointCount);
        }
        gX_I32 count = light->pathWaypointCount;
        if (count < 0) count = 0;
        if (count > gX_GFX_LIGHT_WAYPOINTS) count = gX_GFX_LIGHT_WAYPOINTS;
        light->pathWaypointCount = count;
        for (gX_I32 i = 0; i < count; i++) {
            ImGui::PushID(i);
            ImGui::DragFloat3("waypoint", &light->pathWaypoints[i * 3],
                0.05f, -1000.0f, 1000.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X") && count > 2) {
                memmove(&light->pathWaypoints[i * 3],
                    &light->pathWaypoints[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                memmove(&light->pathTangents[i * 3],
                    &light->pathTangents[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                memmove(&light->pathTangentsIn[i * 3],
                    &light->pathTangentsIn[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                count--;
                light->pathWaypointCount = count;
            }
            if (bezier) {
                ImGui::Indent();
                ImGui::DragFloat3("handle out", &light->pathTangents[i * 3],
                    0.05f, -1000.0f, 1000.0f);
                if (independent)
                    ImGui::DragFloat3("handle in",
                        &light->pathTangentsIn[i * 3], 0.05f, -1000.0f, 1000.0f);
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (count < gX_GFX_LIGHT_WAYPOINTS && ImGui::Button("Add waypoint")) {
            gX_I32 last = (count > 0 ? count - 1 : 0) * 3;
            gX_F32v3 anchor = count > 0
                ? (gX_F32v3){light->pathWaypoints[last],
                    light->pathWaypoints[last + 1],
                    light->pathWaypoints[last + 2]}
                : light->pos;
            light->pathWaypoints[count * 3] = anchor.x + 2.0f;
            light->pathWaypoints[count * 3 + 1] = anchor.y;
            light->pathWaypoints[count * 3 + 2] = anchor.z;
            light->pathTangents[count * 3] = 0.0f;
            light->pathTangents[count * 3 + 1] = 0.0f;
            light->pathTangents[count * 3 + 2] = 0.0f;
            light->pathTangentsIn[count * 3] = 0.0f;
            light->pathTangentsIn[count * 3 + 1] = 0.0f;
            light->pathTangentsIn[count * 3 + 2] = 0.0f;
            light->pathWaypointCount = count + 1;
        }
        ImGui::SameLine();
        if (count < gX_GFX_LIGHT_WAYPOINTS
            && ImGui::Button("Add at current position")) {
            light->pathWaypoints[count * 3] = light->pos.x;
            light->pathWaypoints[count * 3 + 1] = light->pos.y;
            light->pathWaypoints[count * 3 + 2] = light->pos.z;
            light->pathTangents[count * 3] = 0.0f;
            light->pathTangents[count * 3 + 1] = 0.0f;
            light->pathTangents[count * 3 + 2] = 0.0f;
            light->pathTangentsIn[count * 3] = 0.0f;
            light->pathTangentsIn[count * 3 + 1] = 0.0f;
            light->pathTangentsIn[count * 3 + 2] = 0.0f;
            light->pathWaypointCount = count + 1;
        }
        ImGui::TextDisabled("Waypoints %d/%d; one-shot pauses on the final point.",
            count, gX_GFX_LIGHT_WAYPOINTS);
    }
    ImGui::TextDisabled("Path preview is visible in the viewport; one-shot motion pauses at the end of the path.");
    ImGui::EndDisabled();
}

//opencode: keep the model at its current point when first enabling or
//   re-anchoring a circle, regardless of its phase and selected plane.
static gX_I0 gfx_model_orbit_capture_center(gX_RendererGFX_Model *model) {
    gX_F32 radius = fmaxf(0.0f, model->params.orbitRadius);
    gX_F32 phase = isfinite(model->params.orbitPhase) ? model->params.orbitPhase : 0.0f;
    gX_F32 radians = phase * gX_DEG2RAD;
    gX_F32 x = cosf(radians) * radius;
    gX_F32 y = sinf(radians) * radius;
    model->params.orbitElapsed = 0.0;
    switch (model->params.orbitPlane) {
        case 0:
            model->params.orbitCenter = (gX_F32v3){model->params.pos.x - x,
                model->params.pos.y - y, model->params.pos.z};
            break;
        case 2:
            model->params.orbitCenter = (gX_F32v3){model->params.pos.x,
                model->params.pos.y - x, model->params.pos.z - y};
            break;
        default:
            model->params.orbitCenter = (gX_F32v3){model->params.pos.x - x,
                model->params.pos.y, model->params.pos.z - y};
            break;
    }
}

//opencode: model motion-path authoring mirrors the point-light circular path
//   and adds a fixed-capacity waypoint polyline.
static gX_I0 gfx_model_orbit_panel(gX_RendererGFX_Model *model) {
    //opencode: harness hook — expand the section and scroll it into view so
    //   virtual-input gates can reach the path controls without scrolling.
    const gX_I8 *pathFocusHook = getenv("GX_GFX_PATH_FOCUS");
    gX_I1 focusPath = (pathFocusHook && pathFocusHook[0]
        && pathFocusHook[0] != '0') ? 1 : 0;
    if (focusPath) ImGui::SetNextItemOpen(1, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader("Motion path")) return;
    if (focusPath) ImGui::SetScrollHereY(0.0f);
    bool enabled = model->params.orbitEnabled != 0;
    if (ImGui::Checkbox("Enable motion path", &enabled)) {
        model->params.orbitEnabled = enabled ? 1 : 0;
        if (enabled && model->params.pathMode == 0)
            gfx_model_orbit_capture_center(model);
    }
    const gX_I8 *modes[] = {"Circle", "Waypoints"};
    int mode = model->params.pathMode == 1 ? 1 : 0;
    if (ImGui::Combo("mode", &mode, modes, 2)) {
        model->params.pathMode = mode;
        model->params.orbitElapsed = 0.0;
        model->params.orbitPaused = 0;
        if (mode == 0) {
            gfx_model_orbit_capture_center(model);
        } else if (model->params.pathWaypointCount < 2) {
            gX_I32 count = model->params.pathWaypointCount;
            if (count < 1) {
                model->params.pathWaypoints[0] = model->params.pos.x;
                model->params.pathWaypoints[1] = model->params.pos.y;
                model->params.pathWaypoints[2] = model->params.pos.z;
                count = 1;
            }
            gX_I32 last = (count - 1) * 3;
            model->params.pathWaypoints[count * 3] =
                model->params.pathWaypoints[last] + 2.0f;
            model->params.pathWaypoints[count * 3 + 1] =
                model->params.pathWaypoints[last + 1];
            model->params.pathWaypoints[count * 3 + 2] =
                model->params.pathWaypoints[last + 2];
            model->params.pathWaypointCount = count + 1;
        }
    }
    bool paused = model->params.orbitPaused != 0;
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button(paused ? "Play" : "Pause")) {
        model->params.orbitPaused = paused ? 0 : 1;
    }
    ImGui::SameLine();
    bool loop = model->params.orbitLoop != 0;
    if (ImGui::Checkbox("Loop", &loop)) model->params.orbitLoop = loop ? 1 : 0;
    if (mode == 0) {
        const gX_I8 *planes[] = {"XY", "XZ", "YZ"};
        int plane = model->params.orbitPlane;
        if (plane < 0 || plane > 2) plane = 1;
        if (ImGui::Combo("plane", &plane, planes, 3)) model->params.orbitPlane = plane;
        ImGui::DragFloat3("center", (float *)&model->params.orbitCenter,
            0.05f, -1000.0f, 1000.0f);
        ImGui::DragFloat("radius", &model->params.orbitRadius, 0.05f, 0.0f, 1000.0f);
        ImGui::DragFloat("speed (degrees/sec)", &model->params.orbitSpeed,
            1.0f, -720.0f, 720.0f);
        ImGui::SliderFloat("start phase (degrees)", &model->params.orbitPhase, -360.0f, 360.0f);
        if (ImGui::Button("Keep current position on path"))
            gfx_model_orbit_capture_center(model);
    } else {
        ImGui::DragFloat("speed (units/sec)", &model->params.orbitSpeed,
            0.1f, 0.0f, 100.0f);
        bool pingPong = model->params.pathPingPong != 0;
        if (ImGui::Checkbox("Ping-pong", &pingPong))
            model->params.pathPingPong = pingPong ? 1 : 0;
        bool smooth = model->params.pathSmooth != 0;
        if (ImGui::Checkbox("Smooth (Catmull-Rom)", &smooth))
            model->params.pathSmooth = smooth ? 1 : 0;
        bool bezier = model->params.pathBezier != 0;
        if (ImGui::Checkbox("Bezier handles", &bezier)) {
            model->params.pathBezier = bezier ? 1 : 0;
            if (bezier)
                gfx_path_seed_tangents(model->params.pathWaypoints,
                    model->params.pathWaypointCount,
                    model->params.pathTangents, model->params.pathTangentsIn,
                    gX_GFX_MODEL_WAYPOINTS);
        }
        gX_I1 independent = model->params.pathHandlesIndependent != 0;
        if (bezier && ImGui::Checkbox("Independent in/out", (bool *)&independent)) {
            model->params.pathHandlesIndependent = independent ? 1 : 0;
            if (independent)
                gfx_path_mirror_in(model->params.pathTangents,
                    model->params.pathTangentsIn,
                    model->params.pathWaypointCount);
        }
        gX_I32 count = model->params.pathWaypointCount;
        if (count < 0) count = 0;
        if (count > gX_GFX_MODEL_WAYPOINTS) count = gX_GFX_MODEL_WAYPOINTS;
        model->params.pathWaypointCount = count;
        for (gX_I32 i = 0; i < count; i++) {
            ImGui::PushID(i);
            ImGui::DragFloat3("waypoint", &model->params.pathWaypoints[i * 3],
                0.05f, -1000.0f, 1000.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X") && count > 2) {
                memmove(&model->params.pathWaypoints[i * 3],
                    &model->params.pathWaypoints[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                memmove(&model->params.pathTangents[i * 3],
                    &model->params.pathTangents[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                memmove(&model->params.pathTangentsIn[i * 3],
                    &model->params.pathTangentsIn[(i + 1) * 3],
                    (gX_U32)(count - i - 1) * 3 * sizeof(gX_F32));
                count--;
                model->params.pathWaypointCount = count;
            }
            if (bezier) {
                ImGui::Indent();
                ImGui::DragFloat3("handle out",
                    &model->params.pathTangents[i * 3], 0.05f, -1000.0f, 1000.0f);
                if (independent)
                    ImGui::DragFloat3("handle in",
                        &model->params.pathTangentsIn[i * 3], 0.05f, -1000.0f,
                        1000.0f);
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (count < gX_GFX_MODEL_WAYPOINTS && ImGui::Button("Add waypoint")) {
            gX_I32 last = (count > 0 ? count - 1 : 0) * 3;
            gX_F32v3 anchor = count > 0
                ? (gX_F32v3){model->params.pathWaypoints[last],
                    model->params.pathWaypoints[last + 1],
                    model->params.pathWaypoints[last + 2]}
                : model->params.pos;
            model->params.pathWaypoints[count * 3] = anchor.x + 2.0f;
            model->params.pathWaypoints[count * 3 + 1] = anchor.y;
            model->params.pathWaypoints[count * 3 + 2] = anchor.z;
            model->params.pathTangents[count * 3] = 0.0f;
            model->params.pathTangents[count * 3 + 1] = 0.0f;
            model->params.pathTangents[count * 3 + 2] = 0.0f;
            model->params.pathTangentsIn[count * 3] = 0.0f;
            model->params.pathTangentsIn[count * 3 + 1] = 0.0f;
            model->params.pathTangentsIn[count * 3 + 2] = 0.0f;
            model->params.pathWaypointCount = count + 1;
        }
        ImGui::SameLine();
        if (count < gX_GFX_MODEL_WAYPOINTS
            && ImGui::Button("Add at current position")) {
            model->params.pathWaypoints[count * 3] = model->params.pos.x;
            model->params.pathWaypoints[count * 3 + 1] = model->params.pos.y;
            model->params.pathWaypoints[count * 3 + 2] = model->params.pos.z;
            model->params.pathTangents[count * 3] = 0.0f;
            model->params.pathTangents[count * 3 + 1] = 0.0f;
            model->params.pathTangents[count * 3 + 2] = 0.0f;
            model->params.pathTangentsIn[count * 3] = 0.0f;
            model->params.pathTangentsIn[count * 3 + 1] = 0.0f;
            model->params.pathTangentsIn[count * 3 + 2] = 0.0f;
            model->params.pathWaypointCount = count + 1;
        }
        ImGui::TextDisabled("Waypoints %d/%d; one-shot pauses on the final point.",
            count, gX_GFX_MODEL_WAYPOINTS);
    }
    ImGui::TextDisabled("Path preview is visible in the viewport; one-shot motion pauses at the end of the path.");
    ImGui::EndDisabled();
}

//opencode: put common selected-object and framing controls beside the scene list.
static gX_I0 gfx_scene_quick_controls(gX_RendererGFX *Gfx) {
    gX_RendererGFX_Model *Model = gfx_model_at(Gfx, s_selModel);
    gX_RendererGFX_Light *Light = gfx_light_at(Gfx, s_selLight);
    if (Model) {
        ImGui::SeparatorText("Quick model edit");
        ImGui::TextDisabled("%d: %s", s_selModel, gfx_model_basename(Model->path));
        ImGui::DragFloat3("position", (float *)&Model->params.pos, 0.05f);
        if (ImGui::Button("Reset position"))
            Model->params.pos = (gX_F32v3){0.0f, 0.0f, 0.0f};
        ImGui::SeparatorText("Quick camera views");
        if (ImGui::Button("Top"))
            gfx_camera_inspection_view(Model, (gX_F32v3){0.0f, 1.0f, 0.0f},
                (gX_F32v3){0.0f, 0.0f, -1.0f});
        ImGui::SameLine();
        if (ImGui::Button("Front"))
            gfx_camera_inspection_view(Model, (gX_F32v3){0.0f, 0.0f, 1.0f},
                (gX_F32v3){0.0f, 1.0f, 0.0f});
        ImGui::SameLine();
        if (ImGui::Button("Side"))
            gfx_camera_inspection_view(Model, (gX_F32v3){1.0f, 0.0f, 0.0f},
                (gX_F32v3){0.0f, 1.0f, 0.0f});
        ImGui::SameLine();
        if (ImGui::Button("Grazing"))
            gfx_camera_inspection_view(Model, (gX_F32v3){1.0f, 0.22f, 1.0f},
                (gX_F32v3){0.0f, 1.0f, 0.0f});
    } else if (Light) {
        ImGui::SeparatorText("Quick light edit");
        ImGui::TextDisabled("%d: %s", s_selLight, gfx_light_type_name(Light->type));
        ImGui::DragFloat3("position", (float *)&Light->pos, 0.05f);
    }
}

//opencode: compact light quick-select for the left scene panel; full controls
//   remain in the Lights inspector tab.
static gX_I0 gfx_scene_lights_panel(gX_RendererGFX *gfx) {
    ImGui::SeparatorText("Lights");
    if (ImGui::Button("Remove selected") && s_selLight >= 0) {
        gX_RendererGFX_Light_Remove(gfx, (gX_U32)s_selLight);
        s_selLight = -1;
    }
    gX_U32 count = 0;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++) {
        gX_RendererGFX_Light *light = gfx_light_at(gfx, (gX_I32)li);
        if (!light) continue;
        count++;
        gX_I8 label[64];
        snprintf(label, sizeof(label), "%u: %s b=%.2f", li,
            gfx_light_type_name(light->type), (double)light->brightness);
        if (ImGui::Selectable(label, (gX_I32)li == s_selLight)) {
            s_selLight = (gX_I32)li;
            s_selModel = -1;
        }
    }
    if (count == 0) ImGui::TextDisabled("no heap lights");
    gX_RendererGFX_Light *selected = gfx_light_at(gfx, s_selLight);
    if (selected) {
        ImGui::DragFloat("brightness", &selected->brightness, 0.02f, 0.0f, 8.0f);
        gfx_color_edit("color", &selected->color);
        ImGui::TextDisabled("Full controls: Lights tab.");
    }
}

//opencode: compact camera list + render controls for the left scene panel.
static gX_I0 gfx_scene_cameras_panel(gX_RendererGFX *gfx) {
    (void)gfx;
    ImGui::SeparatorText("Cameras");
    if (ImGui::Button("Remove##camera") && s_selectedCamera >= 0) {
        for (gX_I32 ci = s_selectedCamera; ci + 1 < s_cameraCount; ci++)
            s_cameras[ci] = s_cameras[ci + 1];
        s_cameraCount--;
        memset(&s_cameras[s_cameraCount], 0, sizeof(s_cameras[0]));
        if (s_selectedCamera >= s_cameraCount) s_selectedCamera = s_cameraCount - 1;
    }
    for (gX_I32 ci = 0; ci < s_cameraCount; ci++) {
        gX_I8 label[160];
        snprintf(label, sizeof(label), "%d: %.*s", ci,
            (gX_I32)sizeof(label) - 16, s_cameras[ci].name);
        if (ImGui::Selectable(label, ci == s_selectedCamera)) {
            s_selectedCamera = ci;
            s_selModel = -1;
            s_selLight = -1;
        }
    }
    if (s_cameraCount == 0) ImGui::TextDisabled("no cameras");
    if (s_selectedCamera >= 0 && s_selectedCamera < s_cameraCount) {
        gX_GFX_Editor_Camera *cameraEntry = &s_cameras[s_selectedCamera];
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("name##camera", cameraEntry->name, sizeof(cameraEntry->name));
        ImGui::DragFloat3("position##camera", (float *)&cameraEntry->view.position, 0.05f);
        ImGui::DragFloat3("target##camera", (float *)&cameraEntry->view.target, 0.05f);
        ImGui::SliderFloat("fovy##camera", &cameraEntry->view.fovy, 15.0f, 120.0f);
        if (ImGui::Button("View -> Camera"))
            cameraEntry->view = camera.camera3D;
        ImGui::SameLine();
        if (ImGui::Button("Look through"))
            camera.camera3D = cameraEntry->view;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("output##render", s_renderOutput, sizeof(s_renderOutput));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::DragInt("w##renderOut", &s_renderWidth, 2.0f, 0, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragInt("h##renderOut", &s_renderHeight, 2.0f, 0, 8192);
        if (ImGui::Button("Render to image"))
            s_renderPending = 1;
        ImGui::SameLine();
        ImGui::TextDisabled("PNG, viewport crop; size 0 = viewport");
        if (s_renderStatus[0]) {
            if (s_renderStatusOK) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", s_renderStatus);
            else ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", s_renderStatus);
        }
    }
}

//ai: create hub — imports and object creation are separate from the scene tree.
static gX_I0 gfx_scene_create_panel(gX_RendererGFX *gfx) {
    //ai: stack the create actions when the scaled panel cannot fit one row.
    gX_I1 narrowCreate = ImGui::GetContentRegionAvail().x < 430.0f;
    gfx_asset_browser_button(gfx);
    if (!narrowCreate) ImGui::SameLine();
    if (gX_Tools_ImGui_GradientButton("Add light", ImVec2(0.0f, 0.0f),
            IM_COL32(64, 44, 92, 255), IM_COL32(34, 26, 54, 255),
            IM_COL32(238, 232, 255, 255))) {
        gX_I32 index = gX_RendererGFX_Light_Add(gfx);
        if (index >= 0) {
            s_selLight = index;
            s_selModel = -1;
        }
    }
    ImGui::SameLine();
    if (gX_Tools_ImGui_GradientButton("Add camera", ImVec2(0.0f, 0.0f),
            IM_COL32(64, 44, 92, 255), IM_COL32(34, 26, 54, 255),
            IM_COL32(238, 232, 255, 255))
        && s_cameraCount < GX_GFX_EDITOR_CAMERAS) {
        gX_GFX_Editor_Camera *cameraEntry = &s_cameras[s_cameraCount];
        memset(cameraEntry, 0, sizeof(*cameraEntry));
        snprintf((char *)cameraEntry->name, sizeof(cameraEntry->name),
            "Camera %d", s_cameraCount + 1);
        cameraEntry->view = camera.camera3D;
        s_selectedCamera = s_cameraCount;
        s_cameraCount++;
    }
    if (ImGui::CollapsingHeader("Add model by path")) {
        ImGui::InputTextWithHint("model path", "type a model file path",
            s_modelPath, sizeof(s_modelPath));
        if (ImGui::Button("Add typed model path")) {
            if (strlen(s_modelPath) >= sizeof(gX_TextLine)) {
                gX_TraceLog(gX_LOG_WARNING, "GfxEd: model path exceeds 127 characters");
            } else if (gfx_preset_file_exists(s_modelPath)) {
                gX_I32 index = gX_RendererGFX_Model_Add(gfx, s_modelPath);
                if (index >= 0) { s_selModel = index; s_selLight = -1; }
                else gX_TraceLog(gX_LOG_WARNING, "GfxEd: model heap is full");
            } else {
                gX_TraceLog(gX_LOG_WARNING, "GfxEd: model not found '%s'", s_modelPath);
            }
        }
    }
    if (gX_Tools_ImGui_GradientButton("Add 3D text", ImVec2(0.0f, 0.0f),
            IM_COL32(64, 44, 92, 255), IM_COL32(34, 26, 54, 255),
            IM_COL32(238, 232, 255, 255)) && s_selModel >= 0) {
        gX_RendererGFX_Model *selected = gfx_model_at(gfx, s_selModel);
        if (selected) {
            if (selected->params.textString[0] == '\0') {
                snprintf(selected->params.textString,
                    sizeof(selected->params.textString), "Label");
                selected->params.textAnchorTop = 1;
                selected->params.textFit = 1;
                selected->params.textShadowColor = (gX_Color){0,0,0,220};
                selected->params.textShadowPixels = 2.0f;
            }
            selected->params.textVisible = 1;
            selected->params.textBillboard = 1;
            gX_TraceLog(gX_LOG_INFO, "GfxEd: 3D text enabled on model %d",
                s_selModel);
        }
    }
    gfx_text_disabled_wrapped("New labels start above the selected model.");
    static const gX_I8 *primitiveNames[] = {
        "cube", "plane", "sphere", "cylinder", "cone", "torus" };
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##primitive", &s_primitiveType, primitiveNames, 6);
    ImGui::DragFloat("size##primitive", &s_primitiveSize, 0.05f, 0.05f, 50.0f);
    ImGui::DragInt("detail##primitive", &s_primitiveDetail, 0.1f, 0, 12);
    if (gX_Tools_ImGui_GradientButton("Add primitive",
            ImVec2(0.0f, 0.0f), IM_COL32(178, 120, 54, 255),
            IM_COL32(96, 58, 88, 255), IM_COL32(255, 244, 224, 255))) {
        if (gfx_primitive_add(gfx, s_primitiveType, s_primitiveSize,
                s_primitiveDetail) < 0)
            gX_TraceLog(gX_LOG_ERROR, "GfxEd: primitive generation failed");
    }
    gfx_text_disabled_wrapped("Procedural mesh; no asset file.");
}

//opencode: prefab library UI — save the selected model + nearby lights, then
//   place copies at a position. Browse-only semantics do not apply here: the
//   captured bundle is a project JSON file inside the prefabs folder.
static gX_I0 gfx_scene_prefabs_panel(gX_RendererGFX *gfx) {
    ImGui::SeparatorText("Prefabs");
    if (!s_prefabListLoaded) gfx_prefab_list_refresh();
    if (ImGui::Button("Refresh##prefabs"))
        gfx_prefab_list_refresh();
    gX_I32 shown = 0;
    for (gX_U32 fi = 0; fi < s_prefabFiles.count; fi++) {
        const gX_I8 *path = s_prefabFiles.paths[fi];
        size_t length = strlen((const char *)path);
        if (length < 12
            || strcmp((const char *)path + length - 12, ".prefab.json") != 0)
            continue;
        const gX_I8 *base = gfx_model_basename(path);
        if (ImGui::Selectable((const char *)base,
                strcmp((const char *)path,
                    (const char *)s_prefabSelectedPath) == 0))
            snprintf((char *)s_prefabSelectedPath, sizeof(s_prefabSelectedPath),
                "%s", (const char *)path);
        shown++;
    }
    if (shown == 0) ImGui::TextDisabled("no prefabs yet (save one below)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##prefabName", s_prefabName, sizeof(s_prefabName));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat("save radius##prefab", &s_prefabRadius, 0.2f, 0.5f, 100.0f);
    if (ImGui::Button("Tag selected")) {
        if (s_selModel >= 0 && s_selModel < 32)
            s_taggedModels |= 1u << s_selModel;
        if (s_selLight >= 0 && s_selLight < 32)
            s_taggedLights |= 1u << s_selLight;
        if (s_selDecal >= 0 && s_selDecal < 32)
            s_taggedDecals |= 1u << s_selDecal;
        if (s_selEmitter >= 0 && s_selEmitter < 32)
            s_taggedEmitters |= 1u << s_selEmitter;
        gfx_prefab_status("Tagged the current selection", 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear tags##prefab")) {
        s_taggedModels = 0;
        s_taggedLights = 0;
        s_taggedDecals = 0;
        s_taggedEmitters = 0;
    }
    bool useTags = s_prefabUseTags != 0;
    if (ImGui::Checkbox("capture tagged group##prefab", &useTags))
        s_prefabUseTags = useTags ? 1 : 0;
    ImGui::TextDisabled("tagged: models %d, lights %d, decals %d, emitters %d",
        __builtin_popcount(s_taggedModels), __builtin_popcount(s_taggedLights),
        __builtin_popcount(s_taggedDecals),
        __builtin_popcount(s_taggedEmitters));
    if (ImGui::Button("Save selected as prefab")) {
        gX_I8 slug[128];
        gX_I32 si = 0;
        for (const gX_I8 *cursor = s_prefabName; *cursor && si < 120; cursor++) {
            gX_I8 c = *cursor;
            if (c >= 'A' && c <= 'Z') c = (gX_I8)(c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'
                || c == '_')
                slug[si++] = c;
            else if (si > 0 && slug[si - 1] != '-')
                slug[si++] = '-';
        }
        if (si == 0) {
            snprintf((char *)slug, sizeof(slug), "prefab");
            si = 6;
        }
        slug[si] = 0;
        if (!DirectoryExists("resources/tools/prefabs"))
            MakeDirectory("resources/tools/prefabs");
        gX_I8 path[320];
        snprintf((char *)path, sizeof(path), "%s/%s.prefab.json",
            "resources/tools/prefabs", (const char *)slug);
        if (gfx_prefab_capture(gfx, s_prefabName, path, s_prefabRadius,
                s_prefabUseTags))
            gfx_prefab_list_refresh();
    }
    if (!s_prefabPlaceInit) {
        s_prefabPlaceAt = camera.camera3D.target;
        s_prefabPlaceInit = 1;
    }
    ImGui::DragFloat3("place at##prefab", (float *)&s_prefabPlaceAt, 0.1f);
    ImGui::SameLine();
    if (ImGui::SmallButton("view target")) {
        s_prefabPlaceAt = camera.camera3D.target;
    }
    bool arm = s_prefabPlaceArm != 0;
    if (ImGui::Checkbox("place at click##prefab", &arm))
        s_prefabPlaceArm = arm ? 1 : 0;
    ImGui::SameLine();
    ImGui::TextDisabled("next viewport click sets the point");
    if (ImGui::Button("Place prefab")) {
        if (s_prefabSelectedPath[0])
            gfx_prefab_place(gfx, s_prefabSelectedPath, s_prefabPlaceAt);
        else
            gfx_prefab_status("Select or save a prefab first", 0);
    }
    if (s_prefabStatus[0]) {
        if (s_prefabStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                s_prefabStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                s_prefabStatus);
    }
}

//opencode: texture utilities (tooling scope): procedural generates, a
//   normal-from-height bake and ORM channel packing, all exported as PNG into
//   the generated-textures folder.
static gX_I0 gfx_scene_texture_panel(gX_RendererGFX *gfx) {
    (void)gfx;
    ImGui::SeparatorText("Texture tools");
    static const gX_I8 *kinds[] = {
        "solid", "checker", "gradient", "radial", "noise", "perlin",
        "cellular", "value" };
    static const gX_I8 *params[] = {
        "params##tex", "cells##tex", "degrees##tex", "density##tex",
        "factor##tex", "scale##tex", "tile##tex", "scale##tex" };
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##texKind", &s_texKind, kinds, 8);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::DragInt("w##texSize", &s_texWidth, 1.0f, 8, 2048);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragInt("h##texSize", &s_texHeight, 1.0f, 8, 2048);
    gfx_color_edit("A##texColor", &s_texColorA);
    ImGui::SameLine();
    gfx_color_edit("B##texColor", &s_texColorB);
    if (s_texKind != 0) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::DragFloat(params[s_texKind], &s_texParam, 0.05f, 0.0f, 64.0f);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##texName", "texture name", s_texName,
        sizeof(s_texName));
    if (ImGui::Button("Generate & save##tex")) {
        gX_I8 want[160];
        gX_I8 outgoing[640];
        const gX_I8 *chosen = s_texName[0] ? s_texName : kinds[s_texKind];
        snprintf((char *)want, sizeof(want), "%s", (const char *)chosen);
        if (gfx_texture_generate(s_texKind, s_texWidth, s_texHeight,
                s_texParam, s_texColorA, s_texColorB, want, outgoing,
                sizeof(outgoing)))
            gfx_texture_status("saved", outgoing, 1);
        else
            gfx_texture_status("generate failed", NULL, 0);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Normal from height (Sobel on luminance)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##texSource", "source image path", s_texSource,
        sizeof(s_texSource));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::DragFloat("strength##tex", &s_texStrength, 0.05f, 0.05f, 16.0f);
    ImGui::SameLine();
    if (ImGui::Button("Bake normal##tex")) {
        gX_I8 want[160];
        gX_I8 outgoing[640];
        if (s_texName[0])
            snprintf((char *)want, sizeof(want), "%s_n", s_texName);
        else
            snprintf((char *)want, sizeof(want), "height_normal");
        if (s_texSource[0] && gfx_texture_normal_from_height(s_texSource,
                s_texStrength, want, outgoing, sizeof(outgoing)))
            gfx_texture_status("saved", outgoing, 1);
        else
            gfx_texture_status("bake failed (need a loadable source image)",
                NULL, 0);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("ORM pack: R occlusion / G roughness / B metallic");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ormAo", "occlusion image", s_ormAo,
        sizeof(s_ormAo));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ormRough", "roughness image", s_ormRough,
        sizeof(s_ormRough));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ormMetal", "metallic image", s_ormMetal,
        sizeof(s_ormMetal));
    if (ImGui::Button("Pack ORM##tex")) {
        gX_I8 want[160];
        gX_I8 outgoing[640];
        if (s_texName[0])
            snprintf((char *)want, sizeof(want), "%s_orm", s_texName);
        else
            snprintf((char *)want, sizeof(want), "packed_orm");
        if (s_ormAo[0] && s_ormRough[0] && s_ormMetal[0]
            && gfx_texture_pack_orm(s_ormAo, s_ormRough, s_ormMetal, want,
                outgoing, sizeof(outgoing)))
            gfx_texture_status("saved", outgoing, 1);
        else
            gfx_texture_status("pack failed (need three loadable images)",
                NULL, 0);
    }
    if (s_texPreview.id > 0) {
        ImGui::SeparatorText("Preview");
        ImGui::Image((ImTextureID)(intptr_t)s_texPreview.id,
            ImVec2(160.0f, 160.0f));
        ImGui::TextDisabled("%s", (const char *)s_texPreviewPath);
    }
    ImGui::TextDisabled("saved under the generated-textures folder");
    if (s_texStatus[0]) {
        if (s_texStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                (const char *)s_texStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                (const char *)s_texStatus);
    }
}

//opencode: scene export (tooling scope): OBJ with the world transform baked;
//   materials and lighting stay in the preset. The folder is overridable
//   headlessly through GX_GFX_EXPORT_DIR.
static gX_I0 gfx_scene_export_panel(gX_RendererGFX *gfx) {
    ImGui::SeparatorText("Scene export");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##exportName", "file prefix", s_exportName,
        sizeof(s_exportName));
    gX_I8 prefix[160];
    snprintf((char *)prefix, sizeof(prefix), "%s",
        s_exportName[0] ? (const char *)s_exportName : "scene");
    if (ImGui::Button("Export selected##export")) {
        if (gfx_export_model(gfx, s_selModel, prefix, NULL, NULL) > 0)
            gfx_export_status("exported to", gfx_export_out_dir(), 1);
        else
            gfx_export_status("export failed (select a loaded model)", NULL, 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export all##export")) {
        if (gfx_export_all(gfx, prefix, NULL, NULL) > 0)
            gfx_export_status("exported to", gfx_export_out_dir(), 1);
        else
            gfx_export_status("nothing loaded to export", NULL, 0);
    }
    ImGui::TextDisabled("OBJ + MTL + GLB, world transform baked");
    if (s_exportStatus[0]) {
        if (s_exportStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                (const char *)s_exportStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                (const char *)s_exportStatus);
    }
}

//opencode: UX finding U5 — GL level readout + persisted request. The level
//   applies at load (shader variants compile per level), so the request is
//   persisted and the hint says restart.
static gX_I0 gfx_scene_gl_panel(gX_RendererGFX *gfx) {
    (void)gfx;
    ImGui::SeparatorText("GL level");
    static const gX_I32 glLevels[] = { 0, 330, 400, 420, 430 };
    static const gX_I8 *glLevelNames[] = {
        "Auto (detected)", "3.3", "4.0", "4.2", "4.3" };
    gX_I32 glIndex = 0;
    for (gX_I32 li = 1; li < 5; li++)
        if (settings.toolGlLevel == glLevels[li]) glIndex = li;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##glLevel", &glIndex, glLevelNames, 5)) {
        settings.toolGlLevel = glLevels[glIndex];
        gX_TraceLog(gX_LOG_INFO,
            "GfxEd: GL level request -> %d (applies on restart)",
            settings.toolGlLevel);
    }
    ImGui::TextDisabled("active: detected %d, in use %d - restart to apply",
        gX_GL.level, gX_GL.useLevel);
    ImGui::TextDisabled("levels above the detected one need");
    ImGui::TextDisabled("GX_GL43_CONTEXT=1 at startup (compute/SSBO)");
    const gX_I8 *glTrace = getenv("GX_GL_UI_TRACE");
    if (glTrace && glTrace[0] && glTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u) {
        gX_TraceLog(gX_LOG_INFO,
            "GFX_GL: detected=%d user=%d compute=%d ssbo=%d setting=%d",
            gX_GL.level, gX_GL.useLevel, gX_GL.hasCompute, gX_GL.hasSSBO,
            settings.toolGlLevel);
    }
}

//opencode: UX pass — browse-only asset roots at a glance. The browser owns
//   the full list; this block makes the feature discoverable from the scene.
static gX_I0 gfx_scene_sources_panel(gX_RendererGFX *gfx) {
    (void)gfx;
    ImGui::SeparatorText("Asset sources");
    gX_I32 roots = 0;
    if (settings.toolAssetRoots[0]) {
        gX_I8 buffer[sizeof(settings.toolAssetRoots)];
        snprintf(buffer, sizeof(buffer), "%s", settings.toolAssetRoots);
        gX_I8 *save = NULL;
        for (gX_I8 *token = strtok_r(buffer, ":", &save); token;
             token = strtok_r(NULL, ":", &save))
            roots++;
    }
    ImGui::TextDisabled("guest roots: %d (GX_ASSETS + settings)", roots);
    static gX_I8 rootInput[256] = {0};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 92.0f);
    ImGui::InputTextWithHint("##sceneAssetRoot",
        "Add asset root (model/ + texture/)", rootInput, sizeof(rootInput));
    ImGui::SameLine();
    if (ImGui::Button("Add root##scene") && rootInput[0]) {
        if (gfx_asset_settings_root_add(rootInput))
            gfx_assets_refresh();
        rootInput[0] = 0;
    }
    ImGui::TextDisabled("Browse-only: external trees stay outside the repo.");
}

//opencode: at-a-glance scene counts for the left panel.
static gX_I0 gfx_scene_stats_panel(gX_RendererGFX *gfx) {
    gX_U32 models = 0, loaded = 0, lights = 0, decals = 0, emitters = 0;
    for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
        gX_RendererGFX_Model *model = gfx_model_at(gfx, (gX_I32)mi);
        if (!model) continue;
        models++;
        if (model->loaded) loaded++;
    }
    for (gX_U32 li = 0; li < gfx->lights.amount; li++)
        if (gfx_light_at(gfx, (gX_I32)li)) lights++;
    for (gX_U32 di = 0; di < gfx->decals.amount; di++)
        if (gfx_decal_at(gfx, (gX_I32)di)) decals++;
    for (gX_U32 ei = 0; ei < gfx->particle.emitters.amount; ei++)
        if (gfx_emitter_at(gfx, (gX_I32)ei)) emitters++;
    ImGui::SeparatorText("Scene stats");
    ImGui::TextDisabled("models %u/%u loaded · lights %u", loaded, models, lights);
    ImGui::TextDisabled("decals %u · emitters %u", decals, emitters);
    if (ImGui::CollapsingHeader("Profiler")) {
        ImGui::TextDisabled("last frame %.2f ms", gX_Profiler_FrameMs());
        static gX_F32 history[120];
        gX_U32 samples = gX_Profiler_HistoryCopy(history, 120);
        if (samples > 1)
            ImGui::PlotLines("frame ms##profiler", history, (gX_I32)samples,
                0, NULL, 0.0f, 33.3f, ImVec2(-1.0f, 48.0f));
        if (ImGui::BeginTable("##profiler", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("zone");
            ImGui::TableSetupColumn("calls",
                ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("ms",
                ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("pct",
                ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableHeadersRow();
            gX_F64 frameMs = gX_Profiler_FrameMs();
            for (gX_U32 pi = 0; pi < gX_Profiler_EntryCount(); pi++) {
                const gX_ProfilerEntry *entry = gX_Profiler_Entry(pi);
                if (!entry || entry->lastCalls == 0) continue;
                gX_F64 ms = (gX_F64)entry->lastNs / 1000000.0;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted((const char *)entry->name);
                ImGui::TableNextColumn();
                ImGui::Text("%u", entry->lastCalls);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", ms);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", frameMs > 0.0 ? 100.0 * ms / frameMs
                    : 0.0);
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Watches")) {
        if (ImGui::SmallButton("watch frame count##dbg"))
            gX_Debug_Watch_AddFrameCount();
        ImGui::SameLine();
        if (ImGui::SmallButton("clear##dbg"))
            gX_Debug_Watch_Clear();
        ImGui::SameLine();
        if (ImGui::SmallButton("print backtrace##dbg"))
            gX_Debug_Backtrace();
        for (gX_I32 wi = 0; wi < gX_Debug_Watch_Count(); wi++) {
            gX_I8 value[96];
            gX_Debug_Watch_Value(wi, value, sizeof(value));
            ImGui::Text("%s = %s", (const char *)gX_Debug_Watch_Name(wi),
                (const char *)value);
        }
        if (gX_Debug_Watch_Count() == 0)
            ImGui::TextDisabled("no watches (headless: GX_DEBUG_WATCH)");
    }
}

//opencode: raymarch is a scene module; the panel binds to its loaded heap
//   slot and decides its own footprint (full controls in the raymarch scene,
//   a single toggle elsewhere).
static gX_RendererRaymarch *gfx_raymarch_active(gX_I0) {
    if (renderer.raymarch.amount > 0 && renderer.raymarch.data)
        return (gX_RendererRaymarch *)renderer.raymarch.data;
    return NULL;
}

//opencode: list raymarch presets (mirrors the prefab list pattern).
static gX_I0 gfx_raymarch_preset_list_release(gX_I0) {
    if (s_rmPresetFiles.paths) UnloadDirectoryFiles(s_rmPresetFiles);
    memset(&s_rmPresetFiles, 0, sizeof(s_rmPresetFiles));
}

static gX_I0 gfx_raymarch_preset_list_refresh(gX_I0) {
    gfx_raymarch_preset_list_release();
    s_rmPresetFiles = LoadDirectoryFilesEx("resources/tools/raymarch",
        "FILES*", false);
    s_rmPresetListLoaded = 1;
}

//ai: 8x8 node workspace; validation permits forward references but rejects cycles and unsafe repeat domains.
static gX_I0 gfx_raymarch_item_evidence(const char *name);
static gX_I0 gfx_raymarch_graph_panel(gX_RendererRaymarch *rm)
{
    const char *fixture = getenv("GX_RAYMARCH_EDITOR_INSPECT");
    if (fixture && fixture[0] == '2') ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    if (!ImGui::CollapsingHeader("SDF node matrix")) return;
    static gX_I32 selected = 0;
    static const char *operations[] = {"empty","object","union","A minus B","intersection",
        "smooth union","smooth A minus B","smooth intersection","infinite repeat","finite repeat"};
    ImGui::PushItemWidth(140);
    ImGui::DragInt("output (-1: bypass)", &rm->graph.root, 0.1f, -1, 63);
    gfx_raymarch_item_evidence("graph_output");
    float width = fmaxf(16.0f,(ImGui::GetContentRegionAvail().x-7*ImGui::GetStyle().ItemSpacing.x)/8);
    for (gX_I32 i = 0; i < GX_RM_GRAPH_SLOTS; i++)
    {
        ImGui::PushID(i);
        gX_I8 label[16];
        snprintf(label,sizeof(label),"%02d%s",i,i == rm->graph.root ? "*" : "");
        if (ImGui::Selectable(label,selected == i,0,ImVec2(width,20))) selected = i;
        if (i == 63) gfx_raymarch_item_evidence("graph_slot63");
        if (i%8 != 7) ImGui::SameLine();
        ImGui::PopID();
    }
    gX_F32v4 *node = &rm->graph.nodes[selected];
    gX_I32 operation = isfinite(node->x) && node->x >= 0 && node->x < 10 ? (gX_I32)node->x : 0;
    if (ImGui::Combo("operation",&operation,operations,10))
    {
        *node = (gX_F32v4){(gX_F32)operation,0,0,0.25f};
        rm->graph.domains[selected] = (gX_F32v4){4,0,0,1};
    }
    gfx_raymarch_item_evidence("graph_operation");
    if (operation)
    {
        gX_I32 a = isfinite(node->y) && fabsf(node->y) < 100000 ? (gX_I32)node->y : -1;
        if (ImGui::DragInt(operation == 1 ? "object slot" : "input A",&a,0.1f,-1,63)) node->y = (gX_F32)a;
        if (operation >= 2 && operation <= 7)
        {
            gX_I32 b = isfinite(node->z) && fabsf(node->z) < 100000 ? (gX_I32)node->z : -1;
            if (ImGui::DragInt("input B",&b,0.1f,-1,63)) node->z = (gX_F32)b;
            if (operation >= 5) ImGui::DragFloat("smoothness",&node->w,0.02f,0,100);
        }
        if (operation >= 8)
        {
            gX_F32v4 *domain = &rm->graph.domains[selected];
            ImGui::DragFloat3("local period (0: off)",(float *)domain,0.05f,0,1000);
            if (operation == 9)
            {
                gX_I32 packed = isfinite(domain->w) && domain->w >= 0 && domain->w <= 16777215 ? (gX_I32)domain->w : 0;
                gX_I32 limits[3] = {packed&255,(packed>>8)&255,(packed>>16)&255};
                if (ImGui::DragInt3("copies each side",limits,0.1f,0,255)
                    && limits[0] >= 0 && limits[0] <= 255 && limits[1] >= 0 && limits[1] <= 255
                    && limits[2] >= 0 && limits[2] <= 255)
                    domain->w = (gX_F32)(limits[0]+256*limits[1]+65536*limits[2]);
            }
            ImGui::TextWrapped("Phase 1 repeats an object node containing a sphere or box. Enabled periods must fit its full local extent.");
        }
        if (ImGui::SmallButton("Use selected as output")) rm->graph.root = selected;
    }
    gX_RendererRaymarch_Graph_Compile(rm);
    if (rm->graph.error[0]) ImGui::TextWrapped("%s; rendering independent objects.",rm->graph.error);
    else ImGui::TextDisabled("%d/32 reachable nodes; * marks output",rm->graph.count);
    ImGui::PopItemWidth();
}

//ai: opt-in audit evidence refers to the rendered item, not merely a panel call.
static gX_I0 gfx_raymarch_item_evidence(const char *name)
{
    if (!getenv("GX_RAYMARCH_EDITOR_INSPECT")) return;
    ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    ImVec2 window = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    gX_I32 visible = ImGui::IsItemVisible() && low.x >= window.x
        && low.y >= window.y && high.x <= window.x + size.x
        && high.y <= window.y + size.y;
    gX_TraceLog(gX_LOG_NOTICE, "GFX_RAYMARCH_ITEM: frame=%d name=%s visible=%d rect=%.1f,%.1f,%.1f,%.1f",
        ImGui::GetFrameCount(), name, visible, low.x, low.y, high.x, high.y);
}

//ai: dimensions use primitive semantics rather than an unlabeled vec4.
static gX_I0 gfx_raymarch_dimensions(gX_I32 type, gX_F32v4 *values)
{
    static const char *labels[14][4] = {
        {"radius"}, {"half width","half height","half depth"},
        {"ring radius","tube radius"}, {"half width","half height","half depth","edge thickness"},
        {"radius","half height"}, {0}, {0}, {0}, {0},
        {"half width","half height","half depth"}, {"half length","radius"},
        {"size"}, {"half width","half height","half depth","corner radius"},
        {"radius","half height"}};
    if (type < 0 || type >= 14) return;
    if (type == 6 || type == 8) {
        ImGui::TextDisabled("This procedural type has fixed dimensions; use axis scale.");
        return;
    }
    if (type == 5) {
        gX_F32 angle = atan2f(values->x, values->y) * 57.29577951f;
        if (ImGui::DragFloat("half-angle (degrees)", &angle, 0.2f, 1.0f, 89.0f)) {
            values->x = sinf(angle * 0.01745329252f);
            values->y = cosf(angle * 0.01745329252f);
        }
        ImGui::DragFloat("height", &values->z, 0.02f, 0.01f, 100.0f);
        return;
    }
    if (type == 7) {
        ImGui::DragFloat("derivative seed", &values->x, 0.05f, 1.0f, 64.0f);
        ImGui::DragFloat("power", &values->y, 0.05f, 2.0f, 16.0f);
        gX_I32 iterations = (gX_I32)values->z;
        if (ImGui::DragInt("iterations", &iterations, 0.1f, 1, 32)) values->z = (gX_F32)iterations;
        ImGui::DragFloat("bailout radius", &values->w, 0.05f, 2.0f, 64.0f);
        return;
    }
    gX_F32 *components[4] = {&values->x,&values->y,&values->z,&values->w};
    for (gX_I32 j = 0; j < 4; j++) {
        gX_F32 upper = j == 3 && (type == 3 || type == 12)
            ? fmaxf(0.01f, fminf(values->x, fminf(values->y, values->z))) : 100.0f;
        if (labels[type][j]) ImGui::DragFloat(labels[type][j], components[j],
            0.02f, j == 0 && type == 10 ? 0.0f : 0.01f, upper);
    }
}

//ai: neutral world orientation compensates the shader's implicit per-slot yaw.
static gX_I0 gfx_raymarch_transform_reset(gX_RendererRaymarch *rm, gX_I32 index)
{
    gX_F32 angle = (gX_F32)index * 1.256f;
    gX_F32 c = cosf(angle), s = sinf(angle);
    rm->objects.matX[index] = (gX_F32v3){c,0,-s};
    rm->objects.matY[index] = (gX_F32v3){0,1,0};
    rm->objects.matZ[index] = (gX_F32v3){s,0,c};
}

//opencode: raymarch authoring panel (revival): live parameter editing plus
//   the restored preset schema's save/load and a shader hot-reload button.
static gX_I0 gfx_raymarch_panel(gX_RendererRaymarch *rm) {
    ImGui::SeparatorText("Raymarch");
    bool raymarchScene = (maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH);
    if (!raymarchScene) {
        //ai: hybrid owns the shared camera and full resolution; legacy modes cannot fight its routing.
        bool hybridDepth = renderer.state.raymarchDepth == 1;
        if (ImGui::Checkbox("hybrid depth (opaque)##rm", &hybridDepth))
            gX_Renderer_RaymarchDepth_Set(hybridDepth);
        gfx_raymarch_item_evidence("hybrid_depth");
        gfx_text_disabled_wrapped("Experimental: shared camera, full resolution, after GFX effects.");
        bool sharedDepth = renderer.state.raymarchDepth == 2;
        if (ImGui::Checkbox("shared scene depth (experimental)##rm", &sharedDepth))
            gX_Renderer_RaymarchDepth_Set(sharedDepth ? 2 : 0);
        gfx_raymarch_item_evidence("shared_depth");
        gfx_text_disabled_wrapped("Before effects; normals opt-in, no SDF motion vectors yet.");
        ImGui::BeginDisabled(renderer.state.raymarchDepth != 0);
        bool raymarchView = renderer.raymarch.state.DRAW_ALL != 0;
        if (ImGui::Checkbox("view raymarch module##rm", &raymarchView)) {
            renderer.raymarch.state.DRAW_ALL = raymarchView ? 1 : 0;
            renderer.raymarch.state.UPDATE_ALL = raymarchView ? 1 : 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("draws over the current scene");
        bool composite = s_raymarchComposite != 0;
        if (ImGui::Checkbox("composite raymarch backdrop##rm", &composite)) {
            s_raymarchComposite = composite ? 1 : 0;
            gX_Renderer_RaymarchComposite_Set(s_raymarchComposite);
            if (s_raymarchComposite) raymarchView = 1;
        }
        if (s_raymarchComposite) {
            ImGui::SameLine();
            ImGui::TextDisabled("gfx blended over raymarch");
        }
        ImGui::EndDisabled();
        if (!raymarchView && !s_raymarchComposite && !renderer.state.raymarchDepth) return;
    }
    ImGui::TextDisabled("%s", (const char *)rm->name);
    bool inspectFixture = getenv("GX_RAYMARCH_EDITOR_INSPECT") != NULL;
    //ai: expose the preset clock with play/pause, scrubbing, and an explicit reset.
    if (inspectFixture) ImGui::SetNextItemOpen(false, ImGuiCond_Always);
    if (ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool animate = rm->state.useInternalTime != 0;
        if (ImGui::Checkbox("animate##rmTime", &animate))
            rm->state.useInternalTime = animate;
        ImGui::DragFloat("seconds##rmTime", &rm->state.time, 0.05f, 0.0f,
            100000.0f);
        if (ImGui::SmallButton("Reset##rmTime")) rm->state.time = 0.0f;
        ImGui::TextDisabled("Pause preserves time; water/cloud speeds drive motion.");
    }
    if (inspectFixture) ImGui::SetNextItemOpen(false, ImGuiCond_Always);
    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char *qualities[] = {"Draft (25%)", "Balanced (35%)",
            "High (50%)", "Full (100%)"};
        gX_I32 quality = gX_RendererRaymarch_Quality_Get(rm);
        if (ImGui::BeginCombo("quality##rm", quality < 0 ? "Custom" : qualities[quality])) {
            for (gX_I32 q = 0; q < 4; q++) {
                if (ImGui::Selectable(qualities[q], q == quality))
                    gX_RendererRaymarch_Quality_Set(rm, q);
            }
            ImGui::EndCombo();
        }
        gfx_text_disabled_wrapped("Quality adjusts resolution, steps and bounces; features stay unchanged.");
        //ai: proxy wireframe mode — stop the SDF march, draw line proxies.
        bool proxy = rm->flags.enableProxy != 0;
        if (ImGui::Checkbox("proxy wireframe##rm", &proxy))
            rm->flags.enableProxy = proxy ? 1 : 0;
        if (rm->flags.enableProxy)
            ImGui::TextDisabled("SDF march off — object/terrain wireframes only");
        if (ImGui::TreeNode("Advanced tracing##rm")) {
            ImGui::DragInt("steps##rm", &rm->parameters.steps, 1.0f, 1, 512);
            ImGui::DragInt("bounces##rm", &rm->parameters.bounces, 0.1f, 1, 8);
            ImGui::DragInt("steps per bounce (0 = shared)##rm", &rm->parameters.stepsPerBounce, 1.0f, 0, 512);
            ImGui::DragFloat("render scale##rm", &rm->parameters.renderScale, 0.01f, 0.1f, 1.0f);
            ImGui::DragFloat("near##rm", &rm->parameters.near, 0.001f, 0.0001f, 1.0f);
            ImGui::DragFloat("far##rm", &rm->parameters.far, 1.0f, 1.0f, 2000.0f);
            ImGui::DragFloat("epsilon##rm", &rm->parameters.epsilon, 0.0001f, 0.00001f, 0.1f);
            ImGui::TreePop();
        }
        bool terrain = rm->flags.enableTerrain != 0;
        bool grid = rm->flags.enableTerrainGrid != 0;
        bool water = rm->flags.enableWater != 0;
        bool fog = rm->flags.enableFog != 0;
        bool sky = rm->flags.enableSky != 0;
        bool ao = rm->flags.enableAO != 0;
        bool objects = rm->flags.enableObjects != 0;
        bool lights = rm->flags.enableLights != 0;
        if (ImGui::Checkbox("terrain##rm", &terrain))
            rm->flags.enableTerrain = terrain ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("grid##rm", &grid))
            rm->flags.enableTerrainGrid = grid ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("water##rm", &water))
            rm->flags.enableWater = water ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("fog##rm", &fog))
            rm->flags.enableFog = fog ? 1 : 0;
        if (ImGui::Checkbox("sky##rm", &sky))
            rm->flags.enableSky = sky ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("AO##rm", &ao))
            rm->flags.enableAO = ao ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("objects##rm", &objects))
            rm->flags.enableObjects = objects ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("lights##rm", &lights))
            rm->flags.enableLights = lights ? 1 : 0;
    }
    //ai: texturing header — skybox + tex0..2 paths, filters, reload, previews,
    //   and asset-browser pickers (mirrors the gfx texture targets).
    if (ImGui::CollapsingHeader("Textures")) {
        gX_RendererGFX *gfx = gfx_state();
        static const gX_I8 *rmFilters[] = {
            "point", "bilinear", "trilinear", "aniso 4x", "aniso 8x",
            "aniso 16x" };
        static const gX_I8 *rmSkyboxLayouts[] = {
            "auto", "line vertical", "line horizontal", "cross 3x4",
            "cross 4x3", "panorama" };
        ImGui::TextDisabled("skybox");
        ImGui::SetNextItemWidth(-140.0f);
        ImGui::InputText("##rmTexSky", (char *)rm->texture.skybox.path,
            sizeof(rm->texture.skybox.path));
        if (ImGui::IsItemDeactivatedAfterEdit())
            gX_RendererRaymarch_Texture_SetPath(rm, -1,
                rm->texture.skybox.path);
        ImGui::SameLine();
        ImGui::BeginDisabled(gfx == NULL);
        if (ImGui::SmallButton("Browse##rmTexSky") && gfx)
            gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
                gX_GFX_ASSET_TARGET_RAYMARCH_SKYBOX, -1, -1, -1, -1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload##rmTexSky"))
            gX_RendererRaymarch_Texture_Reload(rm, -1);
        {
            gX_I32 filter = rm->texture.skybox.filter;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("filter##rmTexSky", &filter, rmFilters, 6)) {
                rm->texture.skybox.filter = filter;
                rm->texture.skybox.filterChanged = true;
                gX_RendererRaymarch_Texture_Reload(rm, -1);
            }
            ImGui::SameLine();
            gX_I32 layout = rm->texture.skybox.skyboxLayout;
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("layout##rmTexSky", &layout, rmSkyboxLayouts, 6)) {
                rm->texture.skybox.skyboxLayout = layout;
                gX_RendererRaymarch_Texture_Reload(rm, -1);
            }
        }
        for (gX_I32 slot = 0; slot < 3; slot++) {
            ImGui::PushID(200 + slot);
            gX_Texture_Wrapper *wrapper = &rm->texture.textures[slot];
            ImGui::TextDisabled("tex%d", slot);
            if (wrapper->loadingState.loaded && wrapper->texture.id > 0) {
                ImGui::Image((ImTextureID)(intptr_t)wrapper->texture.id,
                    ImVec2(48.0f, 48.0f));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(-140.0f);
            ImGui::InputText("##rmTexPath", (char *)wrapper->path,
                sizeof(wrapper->path));
            if (ImGui::IsItemDeactivatedAfterEdit())
                gX_RendererRaymarch_Texture_SetPath(rm, slot, wrapper->path);
            ImGui::BeginDisabled(gfx == NULL);
            if (ImGui::SmallButton("Browse##rmTex") && gfx)
                gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
                    gX_GFX_ASSET_TARGET_RAYMARCH_TEX, -1, slot, -1, -1);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Reload##rmTex"))
                gX_RendererRaymarch_Texture_Reload(rm, slot);
            {
                gX_I32 filter = wrapper->filter;
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("filter##rmTex", &filter, rmFilters, 6)) {
                    wrapper->filter = filter;
                    wrapper->filterChanged = true;
                }
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Sun and fog")) {
        ImGui::DragFloat("brightness##rmSun", &rm->sun.brightness, 0.05f,
            0.0f, 10.0f);
        ImGui::DragFloat("size##rmSun", &rm->sun.size, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat3("direction##rmSun", (float *)&rm->sun.dir, 0.02f);
        gfx_color_edit("sun color##rmSun", &rm->sun.color);
        ImGui::DragFloat("fog near##rmFog", &rm->fog.near, 0.5f, 0.0f,
            500.0f);
        ImGui::DragFloat("fog far##rmFog", &rm->fog.far, 0.5f, 0.0f, 2000.0f);
        gfx_color_edit("fog color##rmFog", &rm->fog.color);
    }
    //ai: volumetric cloud layer (opt-in; off in the default preset).
    if (ImGui::CollapsingHeader("Clouds")) {
        bool cloudsOn = rm->flags.enableClouds;
        if (ImGui::Checkbox("enable##rmClouds", &cloudsOn))
            rm->flags.enableClouds = cloudsOn;
        ImGui::DragFloat("bottom##rmClouds", &rm->clouds.bottom, 0.5f,
            -200.0f, 1000.0f);
        ImGui::DragFloat("top##rmClouds", &rm->clouds.top, 0.5f,
            -200.0f, 2000.0f);
        ImGui::SliderFloat("coverage##rmClouds", &rm->clouds.coverage,
            0.0f, 1.0f);
        ImGui::DragFloat("density##rmClouds", &rm->clouds.density, 0.02f,
            0.0f, 8.0f);
        ImGui::DragFloat("scale##rmClouds", &rm->clouds.scale, 0.001f,
            0.001f, 0.5f);
        ImGui::DragFloat("speed##rmClouds", &rm->clouds.speed, 0.005f,
            0.0f, 2.0f);
        ImGui::DragFloat("absorb##rmClouds", &rm->clouds.absorb, 0.02f,
            0.0f, 8.0f);
        ImGui::DragFloat("powder##rmClouds", &rm->clouds.powder, 0.02f,
            0.0f, 1.0f);
        gfx_color_edit("color##rmClouds", &rm->clouds.color);
        ImGui::TextDisabled("speed animates with state.useInternalTime");
    }
    //ai: volumetric height fog + sun shafts (opt-in).
    if (ImGui::CollapsingHeader("Volumetric fog")) {
        bool volFogOn = rm->flags.enableVolumetricFog != 0;
        if (ImGui::Checkbox("enable##rmVolFog", &volFogOn))
            rm->flags.enableVolumetricFog = volFogOn ? 1 : 0;
        ImGui::DragFloat("density##rmVolFog", &rm->volFog.density,
            0.002f, 0.0f, 1.0f);
        ImGui::DragFloat("height falloff##rmVolFog",
            &rm->volFog.heightFalloff, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("scattering##rmVolFog", &rm->volFog.scattering,
            0.02f, 0.0f, 4.0f);
        ImGui::SliderFloat("hg asymmetry##rmVolFog",
            &rm->volFog.hgAsymmetry, -0.9f, 0.9f);
        gfx_color_edit("color##rmVolFog", &rm->volFog.color);
        ImGui::TextDisabled("sun shafts via the scene SDF (3-tap)");
    }
    if (ImGui::CollapsingHeader("Terrain")) {
        ImGui::DragInt("seed##rmTerrain", &rm->terrain.seed, 1.0f, 0, 100000);
        ImGui::DragFloat3("offset##rmTerrain", (float *)&rm->terrain.offset,
            0.1f);
        ImGui::DragFloat3("scale##rmTerrain", (float *)&rm->terrain.scale,
            0.05f, 0.01f, 100.0f);
        ImGui::TextDisabled("X/Z: noise frequency; Y: terrain height");
        ImGui::DragFloat4("parameters##rmTerrain",
            (float *)&rm->terrain.parameters, 0.01f);
        //ai: detail uses the already authorable tex2 slot, independent of terrain geometry.
        ImGui::SliderFloat("tex2 surface detail##rmTerrain", &rm->terrain.parameters.w, 0.0f, 1.0f);
        gfx_color_edit("color##rmTerrain", &rm->terrain.color);
        gfx_color_edit("steep##rmTerrain", &rm->terrain.colorSteep);
        gfx_color_edit("beach##rmTerrain", &rm->terrain.colorBeach);
    }
    if (ImGui::CollapsingHeader("Water")) {
        //ai: surface height and clock speed are essential to author visible water.
        ImGui::DragFloat3("surface offset##rmWater", (float *)&rm->water.offset, 0.05f);
        gfx_color_edit("color##rmWater", &rm->water.color);
        ImGui::DragFloat4("ripple##rmWater", (float *)&rm->water.ripple,
            0.05f);
        ImGui::DragFloat4("whirlpool##rmWater",
            (float *)&rm->water.whirlpool, 0.05f);
        ImGui::DragFloat("wave speed##rmWater", &rm->water.speed.x, 0.01f);
        ImGui::TextDisabled("Play Time for motion; 2+ bounces for reflections.");
        ImGui::DragFloat("grad clamp##rmWater", &rm->water.gradClamp, 0.01f,
            0.0f, 1.0f);
    }
    if (inspectFixture) ImGui::SetNextItemOpen(getenv("GX_RAYMARCH_EDITOR_INSPECT")[0] != '2', ImGuiCond_Always);
    if (ImGui::CollapsingHeader("Objects")) {
        ImGui::PushItemWidth(140.0f);
        static const gX_I8 *objectTypes[] = {
            "sphere", "cube", "torus", "box frame", "cylinder", "cone",
            "tree", "mandelbulb", "conifer", "menger", "capsule",
            "octahedron", "rounded box", "hex prism" };
        static gX_I32 selectedObject = 0;
        static gX_F32v3 rotationDelta = {0};
        if (selectedObject >= rm->objects.amount) selectedObject = rm->objects.amount - 1;
        if (selectedObject < 0 && rm->objects.amount > 0) selectedObject = 0;
        gX_I8 selectedLabel[64];
        gX_I32 selectedType = selectedObject >= 0 && selectedObject < rm->objects.amount
            ? rm->objects.type[selectedObject] : -1;
        snprintf(selectedLabel, sizeof(selectedLabel), "%d: %s", selectedObject,
            selectedType >= 0 && selectedType < 14 ? objectTypes[selectedType] : "unknown");
        if (ImGui::BeginCombo("selected##rmObj", rm->objects.amount ? selectedLabel : "No objects")) {
            for (gX_I32 i = 0; i < rm->objects.amount; i++) {
                gX_I32 type = rm->objects.type[i];
                gX_I8 label[64];
                snprintf(label, sizeof(label), "%d: %s", i,
                    type >= 0 && type < 14 ? objectTypes[type] : "unknown");
                if (ImGui::Selectable(label, selectedObject == i)) {
                    selectedObject = i;
                    rotationDelta = (gX_F32v3){0};
                }
            }
            ImGui::EndCombo();
        }
        gfx_raymarch_item_evidence("selected");
        if (selectedObject >= 0 && selectedObject < rm->objects.amount) {
            gX_I32 i = selectedObject;
            ImGui::PushID(i);
            if (inspectFixture) {
                static bool reported = false;
                if (!reported) {
                    gX_TraceLog(gX_LOG_NOTICE, "GFX_RAYMARCH_INSPECTOR: selected=%d type=%d", i, rm->objects.type[i]);
                    reported = true;
                }
            }
            {
                gX_I32 type = rm->objects.type[i];
                if (ImGui::Combo("type##rmObj", &type, objectTypes, 14))
                    gX_RendererRaymarch_Object_Type_Set(rm, i, type);
                gfx_raymarch_item_evidence("type");
                ImGui::DragFloat3("position##rmObj",
                    (float *)&rm->objects.pos[i], 0.05f);
                gfx_raymarch_item_evidence("position");
                gX_F32v3 scale = gX_RendererRaymarch_Object_Scale_Get(rm, i);
                if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0
                    || !isfinite(scale.x) || !isfinite(scale.y) || !isfinite(scale.z))
                    ImGui::TextDisabled("Invalid basis: use Reset scale/orientation to repair it.");
                if (ImGui::DragFloat3("axis scale##rmObj", (float *)&scale, 0.02f, 0.01f, 100.0f))
                    gX_RendererRaymarch_Object_Scale_Set(rm, i, scale);
                gfx_raymarch_item_evidence("scale");
                ImGui::DragFloat3("rotate by (degrees)##rmObj", (float *)&rotationDelta, 0.5f, -180.0f, 180.0f);
                if (ImGui::SmallButton("Apply rotation##rmObj")) {
                    gX_RendererRaymarch_Object_Rotate(rm, i, rotationDelta);
                    rotationDelta = (gX_F32v3){0};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset scale/orientation##rmObj")) {
                    gfx_raymarch_transform_reset(rm, i);
                    rotationDelta = (gX_F32v3){0};
                }
                if (ImGui::TreeNode("Advanced matrix##rmObj")) {
                    ImGui::DragFloat3("SDF column X", (float *)&rm->objects.matX[i], 0.01f);
                    ImGui::DragFloat3("SDF column Y", (float *)&rm->objects.matY[i], 0.01f);
                    ImGui::DragFloat3("SDF column Z", (float *)&rm->objects.matZ[i], 0.01f);
                    ImGui::TextDisabled("Inverse SDF basis; imported shear/mirroring is preserved.");
                    ImGui::TreePop();
                }
                gfx_raymarch_dimensions(rm->objects.type[i], &rm->objects.parameters[i]);
                ImGui::ColorEdit4("color##rmObj", (float *)&rm->objects.color[i], ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::SliderFloat("reflection/refraction##rmObj", &rm->objects.material[i].x, 0.0f, 1.0f);
                //ai: authored roughness affects shared HDR/SSR only, not legacy reflection strength.
                ImGui::SliderFloat("shared roughness##rmObj", &rm->objects.roughness[i], 0.0f, 1.0f);
                gX_I32 materialMode = rm->objects.material[i].y > 0.5f ? 1 : 0;
                const char *materialModes[] = {"reflection", "refraction"};
                if (ImGui::Combo("mode##rmObj", &materialMode, materialModes, 2))
                    rm->objects.material[i].y = (gX_F32)materialMode;
                if (materialMode) ImGui::DragFloat("IOR##rmObj", &rm->objects.material[i].z, 0.01f, 0.1f, 3.0f);
                ImGui::DragFloat("emission##rmObj", &rm->objects.material[i].w, 0.02f, 0.0f, 8.0f);
                if (ImGui::TreeNode("Lighting##rmObj")) {
                    ImGui::DragFloat("ambient", &rm->objects.lighting[i].x, 0.01f, 0.0f, 2.0f);
                    ImGui::DragFloat("diffuse", &rm->objects.lighting[i].y, 0.01f, 0.0f, 2.0f);
                    ImGui::DragFloat("specular", &rm->objects.lighting[i].z, 0.01f, 0.0f, 2.0f);
                    ImGui::DragFloat("shininess", &rm->objects.lighting[i].w, 0.5f, 1.0f, 256.0f);
                    ImGui::TreePop();
                }
                //ai: texturing control — texture slot, mapping mode, scale
                //   (previously a raw DragFloat4).
                {
                    static const gX_I8 *rmTexSlots[] = {
                        "tex0", "tex1", "tex2" };
                    static const gX_I8 *rmTexModes[] = {
                        "off", "triplanar", "planar XZ", "planar XY",
                        "planar YZ" };
                    gX_I32 slot = (gX_I32)rm->objects.texturing[i].x;
                    if (slot < 0 || slot > 2) slot = 0;
                    gX_I32 mode = (gX_I32)rm->objects.texturing[i].y;
                    if (mode < 0 || mode > 4) mode = 0;
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::Combo("texture##rmObj", &slot, rmTexSlots, 3))
                        rm->objects.texturing[i].x = (gX_F32)slot;
                    ImGui::SetNextItemWidth(140.0f);
                    if (ImGui::Combo("mapping##rmObj", &mode, rmTexModes, 5))
                        rm->objects.texturing[i].y = (gX_F32)mode;
                    if (ImGui::DragFloat("scale##rmObj",
                            &rm->objects.texturing[i].z, 0.02f, 0.0f, 8.0f))
                        if (rm->objects.texturing[i].z <= 0.0f)
                            rm->objects.texturing[i].z = 1.0f;
                    ImGui::SliderFloat("base-color blend##rmObj", &rm->objects.texturing[i].w, 0.0f, 1.0f);
                    ImGui::TextDisabled("0: texture / 1: base color");
                    gX_Texture_Wrapper *slotWrapper =
                        &rm->texture.textures[slot];
                    if (slotWrapper->loadingState.loaded
                        && slotWrapper->texture.id > 0) {
                        ImGui::Image((ImTextureID)(intptr_t)slotWrapper->texture.id,
                            ImVec2(32.0f, 32.0f));
                    }
                }
                if (ImGui::SmallButton("Focus selected##rmObj")) {
                    gX_Camera3D *view = rm->state.useInternalCamera
                        ? &rm->camera.camera.camera3D : &camera.camera3D;
                    gX_F32 distance = fmaxf(scale.x, fmaxf(scale.y, scale.z));
                    if (!isfinite(distance) || distance < 1.0f) distance = 1.0f;
                    view->target = rm->objects.pos[i];
                    view->position = gX_F32v3_Add(view->target, (gX_F32v3){0,distance,-6*distance});
                    view->up = (gX_F32v3){0,1,0};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("remove##rmObj")) {
                    gX_RendererRaymarch_Object_Remove(rm, i);
                    rotationDelta = (gX_F32v3){0};
                }
            }
            ImGui::PopID();
        }
        if (rm->objects.amount < GX_RM_MAX_OBJECTS
            && ImGui::SmallButton("Add object##rm")) {
            gX_RendererRaymarch_Object_Init(rm, (gX_U32)rm->objects.amount);
            selectedObject = rm->objects.amount++;
            gX_RendererRaymarch_Object_Type_Set(rm, selectedObject, 0);
            gfx_raymarch_transform_reset(rm, selectedObject);
            gX_Camera3D view = rm->state.useInternalCamera ? rm->camera.camera.camera3D : camera.camera3D;
            gX_F32v3 forward = gX_F32v3_Normalize(gX_F32v3_Sub(view.target, view.position));
            rm->objects.pos[selectedObject] = gX_F32v3_Add(view.position, gX_F32v3_Scale(forward, 6.0f));
            rm->objects.color[selectedObject] = (gX_F32v4){1,1,1,1};
            rm->objects.material[selectedObject].w = 0;
            rotationDelta = (gX_F32v3){0};
        }
        ImGui::PopItemWidth();
    }
    gfx_raymarch_graph_panel(rm);
    if (ImGui::CollapsingHeader("Lights")) {
        for (gX_I32 i = 0; i < rm->lights.amount; i++) {
            ImGui::PushID(100 + i);
            gX_I8 label[32];
            snprintf((char *)label, sizeof(label), "light %d", i);
            if (ImGui::TreeNode(label)) {
                ImGui::DragFloat("brightness##rmLight",
                    &rm->lights.brightness[i], 0.05f, 0.0f, 20.0f);
                ImGui::DragFloat3("position##rmLight",
                    (float *)&rm->lights.pos[i], 0.05f);
                ImGui::DragFloat4("color##rmLight",
                    (float *)&rm->lights.color[i], 0.01f);
                if (ImGui::SmallButton("remove##rmLight")) {
                    gX_I32 last = rm->lights.amount - 1;
                    rm->lights.brightness[i] = rm->lights.brightness[last];
                    rm->lights.pos[i] = rm->lights.pos[last];
                    rm->lights.color[i] = rm->lights.color[last];
                    rm->lights.amount--;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (rm->lights.amount < GX_RM_MAX_LIGHTS
            && ImGui::SmallButton("Add light##rm")) {
            gX_RendererRaymarch_Light_Init(rm, (gX_U32)rm->lights.amount);
            rm->lights.amount++;
        }
    }
    if (ImGui::CollapsingHeader("Debug")) {
        bool showPos = rm->debug.showPos != 0;
        bool showDir = rm->debug.showDir != 0;
        bool showRay = rm->debug.showRay != 0;
        bool showGradient = rm->debug.showGradient != 0;
        bool showNormal = rm->debug.showNormal != 0;
        bool showBounceCount = rm->debug.showBounceCount != 0;
        bool showPathSteps = rm->debug.showPathSteps != 0;
        bool showPathLength = rm->debug.showPathLength != 0;
        bool showDist = rm->debug.showDist != 0;
        if (ImGui::Checkbox("position##rmDbg", &showPos))
            rm->debug.showPos = showPos ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("direction##rmDbg", &showDir))
            rm->debug.showDir = showDir ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("ray##rmDbg", &showRay))
            rm->debug.showRay = showRay ? 1 : 0;
        if (ImGui::Checkbox("gradient##rmDbg", &showGradient))
            rm->debug.showGradient = showGradient ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("normal##rmDbg", &showNormal))
            rm->debug.showNormal = showNormal ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("bounces##rmDbg", &showBounceCount))
            rm->debug.showBounceCount = showBounceCount ? 1 : 0;
        if (ImGui::Checkbox("steps##rmDbg", &showPathSteps))
            rm->debug.showPathSteps = showPathSteps ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("path length##rmDbg", &showPathLength))
            rm->debug.showPathLength = showPathLength ? 1 : 0;
        ImGui::SameLine();
        if (ImGui::Checkbox("distance##rmDbg", &showDist))
            rm->debug.showDist = showDist ? 1 : 0;
    }
    if (ImGui::CollapsingHeader("Shader features")) {
        static const gX_I8 *featureNames[] = {
            "CF_FBMNOISE", "CF_WATER", "CF_OBJECTS", "CF_LIGHTS", "CF_FOG",
            "CF_GLOW", "CF_AO", "CF_SKYBOX", "CF_GRID", "CF_DEBUGVIS",
            "CF_SDF_REPEAT", "CF_SDF_SMOOTH", "CF_HSV", "CF_VEGETATION",
            "CF_FBMNOISE3D", "CF_NOISE_TEX", "CF_SKY_PROCEDURAL" };
        ImGui::TextDisabled("compile-time flags (recompiles the shader)");
        for (gX_I32 fi = 0; fi < 17; fi++) {
            bool on = gX_RendererRaymarch_Defines_Get(rm,
                featureNames[fi]) != 0;
            if (ImGui::Checkbox((const char *)featureNames[fi], &on))
                gX_RendererRaymarch_Defines_Set(rm, featureNames[fi],
                    on ? 1 : 0);
        }
    }
    ImGui::SeparatorText("Preset");
    if (!s_rmPresetListLoaded) gfx_raymarch_preset_list_refresh();
    if (ImGui::Button("Refresh##rmPresets"))
        gfx_raymarch_preset_list_refresh();
    ImGui::SameLine();
    if (ImGui::Button("Randomize##rm")) {
        gX_RendererRaymarch_LoadRandom(rm);
        snprintf((char *)s_rmStatus, sizeof(s_rmStatus), "randomized");
        s_rmStatusOK = 1;
    }
    gX_I32 rmShown = 0;
    for (gX_U32 fi = 0; fi < s_rmPresetFiles.count; fi++) {
        const gX_I8 *path = s_rmPresetFiles.paths[fi];
        size_t length = strlen((const char *)path);
        if (length < 5
            || strcmp((const char *)path + length - 5, ".json") != 0)
            continue;
        const gX_I8 *base = gfx_model_basename(path);
        if (ImGui::Selectable((const char *)base,
                strcmp((const char *)path,
                    (const char *)s_rmPresetSelected) == 0))
            snprintf((char *)s_rmPresetSelected, sizeof(s_rmPresetSelected),
                "%s", (const char *)path);
        rmShown++;
    }
    if (rmShown == 0) ImGui::TextDisabled("no saved presets yet");
    if (s_rmPresetSelected[0] && ImGui::Button("Load selected##rm")) {
        if (gX_RendererRaymarch_Preset_Load_New(rm, s_rmPresetSelected) == 0) {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus), "loaded %.*s",
                (gX_I32)sizeof(s_rmStatus) - 16,
                (const char *)s_rmPresetSelected);
            s_rmStatusOK = 1;
        } else {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus),
                "load failed (need a readable preset)");
            s_rmStatusOK = 0;
            GX_ERROR_SET(GX_ERR_PARSE, "raymarch preset load failed: %s",
                s_rmPresetSelected);
        }
    }
    static const gX_I8 *builtinNames[] = {
        "default", "island", "water", "flatlands", "desert", "alien",
        "canyon", "primitives", "cloudscape" };
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##rmBuiltin", &s_rmBuiltinIndex, builtinNames, 9);
    if (ImGui::Button("Load built-in##rm")) {
        gX_RendererRaymarch_Preset_BuiltIn(rm, s_rmBuiltinIndex);
        snprintf((char *)s_rmStatus, sizeof(s_rmStatus), "built-in %s",
            builtinNames[s_rmBuiltinIndex]);
        s_rmStatusOK = 1;
    }
    if (!s_rmPresetPath[0])
        snprintf((char *)s_rmPresetPath, sizeof(s_rmPresetPath), "%s/%s.json",
            "resources/tools/raymarch", "scene");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##rmPresetPath", "preset path", s_rmPresetPath,
        sizeof(s_rmPresetPath));
    if (ImGui::Button("Save##rmPreset")) {
        if (s_rmPresetPath[0]
            && gX_RendererRaymarch_Preset_Save_New(rm, s_rmPresetPath) == 0) {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus), "saved %.*s",
                (gX_I32)sizeof(s_rmStatus) - 16,
                (const char *)s_rmPresetPath);
            s_rmStatusOK = 1;
        } else {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus),
                "save failed (need a writable path)");
            s_rmStatusOK = 0;
            GX_ERROR_SET(GX_ERR_IO, "raymarch preset save failed: %s",
                s_rmPresetPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load##rmPreset")) {
        if (s_rmPresetPath[0]
            && gX_RendererRaymarch_Preset_Load_New(rm, s_rmPresetPath) == 0) {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus), "loaded %.*s",
                (gX_I32)sizeof(s_rmStatus) - 16,
                (const char *)s_rmPresetPath);
            s_rmStatusOK = 1;
        } else {
            snprintf((char *)s_rmStatus, sizeof(s_rmStatus),
                "load failed (need a readable preset)");
            s_rmStatusOK = 0;
            GX_ERROR_SET(GX_ERR_PARSE, "raymarch preset load failed: %s",
                s_rmPresetPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload shader##rm"))
        gX_RendererRaymarch_Shader_Reload(rm);
    if (s_rmStatus[0]) {
        if (s_rmStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                (const char *)s_rmStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                (const char *)s_rmStatus);
    }
    if (!s_rmUiTraced) {
        gX_TraceLog(gX_LOG_INFO, "GFX_RAYMARCH_UI: panel=1");
        s_rmUiTraced = 1;
    }
}

//ai: postprocess chain editor — the module presented by --pp (and usable as a
//   compositor layer elsewhere). Chain entries own their uniforms,
//   deltaAdd/deltaMul animate per loop iteration, and the order is editable.
static gX_I8 s_ppPresetPath[512] = {0};
static FilePathList s_ppPresetFiles = {0};
static gX_I1 s_ppPresetListLoaded = 0;
static gX_I8 s_ppPresetSelected[512] = {0};
static gX_I8 s_ppStatus[160] = {0};
static gX_I1 s_ppStatusOK = 0;

static gX_I0 gfx_pp_preset_list_release(gX_I0) {
    if (s_ppPresetFiles.paths) UnloadDirectoryFiles(s_ppPresetFiles);
    memset(&s_ppPresetFiles, 0, sizeof(s_ppPresetFiles));
}

static gX_I0 gfx_pp_preset_list_refresh(gX_I0) {
    gfx_pp_preset_list_release();
    s_ppPresetFiles = LoadDirectoryFilesEx("resources/tools/pp", "FILES*",
        false);
    s_ppPresetListLoaded = 1;
}


//ai: polisher panel — live tuning of the registered magic numbers; the report
//   prints copy-paste source lines for the changed values.
static gX_I8 s_polisherReport[2048] = {0};
static gX_I1 s_polisherUiTraced = 0;

static gX_I0 gfx_polisher_panel(gX_I0) {
    if (polisher.count == 0) return;
    //ai: trace availability (not expansion) — the gate asserts the panel path.
    if (!s_polisherUiTraced) {
        gX_TraceLog(gX_LOG_INFO, "GFX_POLISHER_UI: panel=1");
        s_polisherUiTraced = 1;
    }
    if (!ImGui::CollapsingHeader("Polisher")) return;
    for (gX_U32 i = 0; i < polisher.count; i++) {
        gX_Polisher_Entry *entry = &polisher.entries[i];
        ImGui::PushID((gX_I32)i);
        if (entry->type == GX_POLISHER_TYPE_F32 && entry->f32Value) {
            if (entry->flags & GX_POLISHER_UNBOUNDED)
                ImGui::DragFloat((const char *)entry->name,
                    entry->f32Value, entry->step);
            else if (entry->flags & GX_POLISHER_LOGSCALE)
                ImGui::SliderFloat((const char *)entry->name,
                    entry->f32Value, entry->f32Min, entry->f32Max, "%.4f",
                    ImGuiSliderFlags_Logarithmic);
            else
                ImGui::SliderFloat((const char *)entry->name,
                    entry->f32Value, entry->f32Min, entry->f32Max);
            entry->modified = (*entry->f32Value != entry->f32Original)
                ? 1 : 0;
        } else if (entry->i32Value) {
            if (entry->flags & GX_POLISHER_UNBOUNDED)
                ImGui::DragInt((const char *)entry->name, entry->i32Value,
                    entry->step);
            else
                ImGui::SliderInt((const char *)entry->name, entry->i32Value,
                    entry->i32Min, entry->i32Max);
            entry->modified = (*entry->i32Value != entry->i32Original)
                ? 1 : 0;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("reset##polish")) {
            if (entry->f32Value) *entry->f32Value = entry->f32Original;
            if (entry->i32Value) *entry->i32Value = entry->i32Original;
            entry->modified = 0;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reset all##polish"))
        gX_Polisher_ResetAll();
    ImGui::SameLine();
    if (ImGui::Button("Generate report##polish"))
        gX_Polisher_GenerateReport(s_polisherReport,
            sizeof(s_polisherReport));
    if (s_polisherReport[0]) {
        ImGui::BeginChild("##polishReport", ImVec2(0.0f, 90.0f), true);
        ImGui::TextUnformatted((const char *)s_polisherReport);
        ImGui::EndChild();
        if (ImGui::SmallButton("Copy##polish"))
            ImGui::SetClipboardText((const char *)s_polisherReport);
    }
}

static gX_I0 gfx_pp_panel(gX_RendererPostProcess *pp) {
    ImGui::SeparatorText("Postprocess");
    //ai: the chain can post-process any scene, not only --pp: enabling it sets
    //   the embedded PP instance's usePP path and feeds it the canvas.
    bool enabled = renderer.state.usePP != 0;
    if (ImGui::Checkbox("enable for this scene##pp", &enabled)) {
        renderer.state.usePP = enabled ? 1 : 0;
        if (enabled) renderer.pp.state.injectCanvas = 1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("chain %u/%d%s", pp->shadersAmount, GX_PP_SHADERS_MAX,
        renderer.state.usePP ? "" : " — off");
    if (ImGui::DragFloat("render scale##pp", &pp->texture.renderScale,
            0.01f, 0.25f, 1.0f) && ImGui::IsItemDeactivatedAfterEdit())
        pp->texture.renderScaleChanged = 1;
    bool inject = pp->state.inject != 0;
    if (ImGui::Checkbox("inject test texture##pp", &inject))
        pp->state.inject = inject ? 1 : 0;
    ImGui::SameLine();
    bool injectCanvas = pp->state.injectCanvas != 0;
    if (ImGui::Checkbox("inject canvas##pp", &injectCanvas))
        pp->state.injectCanvas = injectCanvas ? 1 : 0;
    ImGui::SameLine();
    bool feedback = pp->state.feedback != 0;
    if (ImGui::Checkbox("feedback##pp", &feedback))
        pp->state.feedback = feedback ? 1 : 0;
    if (pp->state.feedback) {
        ImGui::SliderFloat("feedback decay##pp", &pp->state.feedbackDecay,
            0.0f, 1.0f);
        gX_I32 fbBlend = pp->state.feedbackBlendMode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("feedback blend##pp", &fbBlend,
                "alpha\0additive\0multiplied\0"))
            pp->state.feedbackBlendMode = fbBlend;
    }
    if (pp->state.injectCanvas) {
        gX_I32 canvasBlend = pp->state.injectCanvasBlendMode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("canvas blend##pp", &canvasBlend,
                "alpha\0additive\0multiplied\0"))
            pp->state.injectCanvasBlendMode = canvasBlend;
        gfx_color_edit("canvas tint##pp", &pp->state.injectCanvasTint);
    }
    //ai: start texture — the chain input when inject/injectCanvas are off.
    if (ImGui::TreeNode("start texture##pp")) {
        static gX_I8 s_ppStartPath[256] = {0};
        if (!s_ppStartPath[0])
            snprintf((char *)s_ppStartPath, sizeof(s_ppStartPath), "%s/%s",
                "resources/texture", "texture.png");
        ImGui::SetNextItemWidth(-90.0f);
        ImGui::InputText("##ppStartPath", s_ppStartPath,
            sizeof(s_ppStartPath));
        ImGui::SameLine();
        if (ImGui::SmallButton("Set##ppStart"))
            gX_RendererPostProcess_Texture_SetStartPath(pp, s_ppStartPath);
        gX_I32 startBlend = pp->texture.startBlendMode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("start blend##pp", &startBlend,
                "alpha\0additive\0multiplied\0"))
            pp->texture.startBlendMode = startBlend;
        gX_I32 startFilter = pp->texture.startFilterMode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("start filter##pp",
                gX_TextureFilter_String(startFilter))) {
            for (gX_I32 f = 0; f <= gX_TEXTURE_FILTER_TRILINEAR; f++) {
                if (ImGui::Selectable(gX_TextureFilter_String(f), f == startFilter))
                    pp->texture.startFilterMode = f;
            }
            ImGui::EndCombo();
        }
        gX_I32 startWrap = pp->texture.startWrapMode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("start wrap##pp",
                gX_TextureWrap_String(startWrap))) {
            for (gX_I32 w = 0; w <= gX_TEXTURE_WRAP_MIRROR_CLAMP; w++) {
                if (ImGui::Selectable(gX_TextureWrap_String(w), w == startWrap))
                    pp->texture.startWrapMode = w;
            }
            ImGui::EndCombo();
        }
        ImGui::TreePop();
    }

    static gX_I32 s_ppPreset = 0;
    if (s_ppPreset >= GX_PP_PRESET_COUNT) s_ppPreset = 0;
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("preset##pp",
            gX_RendererPostProcess_Presets_String(s_ppPreset))) {
        for (gX_I32 i = 0; i < GX_PP_PRESET_COUNT; i++) {
            if (ImGui::Selectable(gX_RendererPostProcess_Presets_String(i),
                    i == s_ppPreset))
                s_ppPreset = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load##ppPreset"))
        gX_RendererPostProcess_Preset_BuiltIn(pp, s_ppPreset);

    if (!s_ppPresetListLoaded) gfx_pp_preset_list_refresh();
    //ai: split literals keep the deps scanner on the (existing) directory
    //   rather than the runtime-generated default preset file.
    if (!s_ppPresetPath[0])
        snprintf((char *)s_ppPresetPath, sizeof(s_ppPresetPath), "%s/%s.json",
            "resources/tools/pp", "scene");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ppPresetPath", "preset path", s_ppPresetPath,
        sizeof(s_ppPresetPath));
    if (ImGui::Button("Save##ppFile")) {
        s_ppStatusOK = gX_RendererPostProcess_Preset_Save_New(pp,
            s_ppPresetPath) == 0;
        snprintf((char *)s_ppStatus, sizeof(s_ppStatus), "%s %.*s",
            s_ppStatusOK ? "saved" : "save failed",
            (gX_I32)sizeof(s_ppStatus) - 16, (const char *)s_ppPresetPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load##ppFile")) {
        s_ppStatusOK = gX_RendererPostProcess_Preset_Load_New(pp,
            s_ppPresetPath) == 0;
        snprintf((char *)s_ppStatus, sizeof(s_ppStatus), "%s %.*s",
            s_ppStatusOK ? "loaded" : "load failed",
            (gX_I32)sizeof(s_ppStatus) - 16, (const char *)s_ppPresetPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh##ppList")) gfx_pp_preset_list_refresh();
    {
        gX_I32 shown = 0;
        for (gX_U32 fi = 0; fi < s_ppPresetFiles.count; fi++) {
            const gX_I8 *preset = s_ppPresetFiles.paths[fi];
            size_t length = strlen((const char *)preset);
            if (length < 5
                || strcmp((const char *)preset + length - 5, ".json") != 0)
                continue;
            const gX_I8 *base = gfx_model_basename(preset);
            if (ImGui::Selectable((const char *)base,
                    strcmp((const char *)preset,
                        (const char *)s_ppPresetSelected) == 0))
                snprintf((char *)s_ppPresetSelected,
                    sizeof(s_ppPresetSelected), "%s", (const char *)preset);
            shown++;
        }
        if (shown == 0) ImGui::TextDisabled("no saved presets yet");
        if (s_ppPresetSelected[0]
            && ImGui::Button("Load selected##ppFile")) {
            s_ppStatusOK = gX_RendererPostProcess_Preset_Load_New(pp,
                s_ppPresetSelected) == 0;
            snprintf((char *)s_ppStatus, sizeof(s_ppStatus), "%s %.*s",
                s_ppStatusOK ? "loaded" : "load failed",
                (gX_I32)sizeof(s_ppStatus) - 16,
                (const char *)s_ppPresetSelected);
        }
    }
    if (s_ppStatus[0]) {
        if (s_ppStatusOK)
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                (const char *)s_ppStatus);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                (const char *)s_ppStatus);
    }

    //ai: inject (texture overlay) — 8 slots, animated params, per-slot blend
    if (ImGui::CollapsingHeader("Inject (texture overlay)")) {
        bool useCustom = pp->inject.useCustomTexture != 0;
        if (ImGui::Checkbox("custom texture##ppInj", &useCustom))
            pp->inject.useCustomTexture = useCustom ? 1 : 0;
        ImGui::SameLine();
        bool useParam = pp->inject.useParam != 0;
        if (ImGui::Checkbox("animated params##ppInj", &useParam))
            pp->inject.useParam = useParam ? 1 : 0;
        ImGui::SameLine();
        bool connectScale = pp->inject.connectScale != 0;
        if (ImGui::Checkbox("fill scale##ppInj", &connectScale))
            pp->inject.connectScale = connectScale ? 1 : 0;
        gX_I32 amount = (gX_I32)pp->inject.amount;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("slots##ppInj", &amount, 0.1f, 0,
                GX_PP_INJECT_SLOTS))
            pp->inject.amount = (gX_U32)(amount < 0 ? 0 : amount);
        for (gX_I32 slot = 0; slot < GX_PP_INJECT_SLOTS; slot++) {
            ImGui::PushID(300 + slot);
            gX_I8 label[32];
            snprintf((char *)label, sizeof(label), "slot %d", slot);
            if (ImGui::TreeNode(label)) {
                ImGui::SetNextItemWidth(-90.0f);
                ImGui::InputText("##ppInjPath",
                    (char *)pp->inject.texturePaths[slot],
                    sizeof(pp->inject.texturePaths[slot]));
                ImGui::SameLine();
                if (ImGui::SmallButton("Set##ppInj"))
                    gX_RendererPostProcess_Inject_SetTexturePath(pp, slot,
                        pp->inject.texturePaths[slot]);
                gX_I32 blend = pp->inject.blendMode[slot];
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("blend##ppInj", &blend,
                        "alpha\0additive\0multiplied\0"))
                    pp->inject.blendMode[slot] = blend;
                ImGui::DragFloat2("coords##ppInj",
                    (float *)&pp->inject.coords[slot], 0.01f);
                ImGui::DragFloat2("scale##ppInj",
                    (float *)&pp->inject.scale[slot], 0.01f);
                ImGui::DragFloat("rotate##ppInj",
                    &pp->inject.rotate[slot], 0.01f);
                gfx_color_edit("color##ppInj", &pp->inject.color[slot]);
                if (pp->inject.textures[slot]
                    && pp->inject.textures[slot]->id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)
                        pp->inject.textures[slot]->id, ImVec2(48.0f, 48.0f));
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (pp->inject.useParam) {
            ImGui::TextDisabled("animated (slot 0):");
            ImGui::DragFloat2("coords##ppParam",
                (float *)&pp->inject.coordsParam.value, 0.01f);
            ImGui::DragFloat2("scale##ppParam",
                (float *)&pp->inject.scaleParam.value, 0.01f);
            ImGui::DragFloat("rotate##ppParam",
                &pp->inject.rotateParam.value, 0.01f);
            ImGui::DragFloat4("color##ppParam",
                (float *)&pp->inject.colorParam.value, 0.01f);
        }
    }

    static gX_I32 s_ppAdd = 0;
    if (s_ppAdd >= GX_PP_TYPE_COUNT) s_ppAdd = 0;
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("##ppAdd",
            gX_RendererPostProcess_ShaderTypes_String(s_ppAdd))) {
        for (gX_I32 i = 0; i < GX_PP_TYPE_COUNT; i++) {
            if (ImGui::Selectable(gX_RendererPostProcess_ShaderTypes_String(i),
                    i == s_ppAdd))
                s_ppAdd = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add##pp"))
        gX_RendererPostProcess_Shader_Push(pp, s_ppAdd);
    ImGui::SameLine();
    if (ImGui::Button("Reload chain##pp"))
        for (gX_U32 i = 0; i < pp->shadersAmount; i++)
            gX_RendererPostProcess_Shader_Reload(pp, i);

    for (gX_U32 i = 0; i < pp->shadersAmount; i++) {
        gX_RendererPostProcess_Shader *entry = &pp->shaders[i];
        ImGui::PushID((gX_I32)i);
        gX_I8 label[80];
        snprintf((char *)label, sizeof(label), "%u: %s", i,
            entry->name[0] ? (const char *)entry->name : "shader");
        if (ImGui::TreeNode(label)) {
            gX_I32 blend = entry->blendMode;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("blend##pp", &blend,
                    "alpha\0additive\0multiplied\0"))
                entry->blendMode = blend;
            gX_I32 filter = entry->filterMode;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("filter##pp", gX_TextureFilter_String(filter))) {
                for (gX_I32 f = 0; f <= gX_TEXTURE_FILTER_TRILINEAR; f++) {
                    if (ImGui::Selectable(gX_TextureFilter_String(f), f == filter))
                        entry->filterMode = f;
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(120.0f);
            gX_I32 wrap = entry->wrapMode;
            if (ImGui::BeginCombo("wrap##pp", gX_TextureWrap_String(wrap))) {
                for (gX_I32 w = 0; w <= gX_TEXTURE_WRAP_MIRROR_CLAMP; w++) {
                    if (ImGui::Selectable(gX_TextureWrap_String(w), w == wrap))
                        entry->wrapMode = w;
                }
                ImGui::EndCombo();
            }
            gX_I32 loops = (gX_I32)entry->loops;
            if (ImGui::DragInt("loops##pp", &loops, 0.1f, 1, 16))
                entry->loops = (gX_U32)(loops < 1 ? 1 : loops);
            gX_I32 unifAmount = (gX_I32)entry->uniformsAmount;
            if (ImGui::DragInt("uniforms##pp", &unifAmount, 0.1f, 1,
                    GX_PP_UNIFORM_AMOUNT))
                entry->uniformsAmount = (gX_U32)(unifAmount < 1 ? 1
                    : (unifAmount > GX_PP_UNIFORM_AMOUNT ? GX_PP_UNIFORM_AMOUNT
                       : unifAmount));
            if (ImGui::SmallButton("reload##pp"))
                gX_RendererPostProcess_Shader_Reload(pp, i);
            for (gX_U32 u = 0; u < entry->uniformsAmount; u++) {
                ImGui::PushID((gX_I32)u);
                ImGui::DragFloat4("value##pp",
                    (float *)&entry->uniforms[u].value, 0.01f);
                ImGui::DragFloat4("+delta##pp",
                    (float *)&entry->uniforms[u].deltaAdd, 0.01f);
                ImGui::DragFloat4("xdelta##pp",
                    (float *)&entry->uniforms[u].deltaMul, 0.01f);
                ImGui::PopID();
            }
            if (ImGui::SmallButton("up##pp"))
                gX_RendererPostProcess_Shader_Move(pp, i, -1);
            ImGui::SameLine();
            if (ImGui::SmallButton("down##pp"))
                gX_RendererPostProcess_Shader_Move(pp, i, 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("remove##pp")) {
                gX_RendererPostProcess_Shader_Remove(pp, i);
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!s_ppUiTraced) {
        gX_TraceLog(gX_LOG_INFO, "GFX_PP_UI: panel=1");
        s_ppUiTraced = 1;
    }
}

static gX_I0 gfx_scene_panel(gX_RendererGFX *gfx) {
    bool pick = s_pickEnabled != 0;
    if (ImGui::Checkbox("Pick models and lights with mouse", &pick))
        s_pickEnabled = pick ? 1 : 0;
    gfx_text_disabled_wrapped("Click a model or light marker; drag to move it.");
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
    if (model || light) {
        if (ImGui::Button("Frame selected")) {
            if (model) gfx_camera_frame_model(model);
            else gfx_camera_frame_light(light);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Enter free camera"))
        gfx_free_camera_set(1);
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        gfx_model_list_panel(gfx);
        gfx_scene_lights_panel(gfx);
        gfx_scene_cameras_panel(gfx);
    }
    if (ImGui::CollapsingHeader("Create", ImGuiTreeNodeFlags_DefaultOpen)) {
        gfx_scene_create_panel(gfx);
        gfx_scene_prefabs_panel(gfx);
        gfx_scene_sources_panel(gfx);
    }
    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        gX_RendererRaymarch *rm = gfx_raymarch_active();
        if (rm) gfx_raymarch_panel(rm);
        if (renderer.ppLoaded) gfx_pp_panel(&renderer.pp);
        gfx_scene_texture_panel(gfx);
        gfx_scene_gl_panel(gfx);
        gfx_scene_stats_panel(gfx);
        ImGui::SeparatorText("Viewport helpers");
        bool grid = gfx->state.SHOW_GRID != 0;
        bool boxes = gfx->state.SHOW_BOUNDING_BOX != 0;
        bool skeleton = gfx->state.SHOW_SKELETON != 0;
        bool markers = gfx->state.SHOW_LIGHT_MARKERS != 0;
        if (ImGui::Checkbox("grid", &grid)) gfx->state.SHOW_GRID = grid ? 1 : 0;
        if (ImGui::Checkbox("bounding boxes", &boxes)) gfx->state.SHOW_BOUNDING_BOX = boxes ? 1 : 0;
        if (ImGui::Checkbox("skeleton", &skeleton)) gfx->state.SHOW_SKELETON = skeleton ? 1 : 0;
        if (ImGui::Checkbox("light markers", &markers)) gfx->state.SHOW_LIGHT_MARKERS = markers ? 1 : 0;
        ImGui::TextDisabled("Camera: middle-drag orbit, Shift+middle-drag pan, wheel zoom.");
        gX_Tools_BG_PanelBody(1, 1);
        gfx_polisher_panel();
    }
    if (ImGui::CollapsingHeader("Document")) {
        gfx_scene_export_panel(gfx);
    }
}

//ai: selection-first inspector — selected-object actions are above global UI.
static gX_I0 gfx_editor_selection_panel(gX_RendererGFX *gfx) {
    gX_RendererGFX_Model *model = gfx_model_at(gfx, s_selModel);
    gX_RendererGFX_Light *light = gfx_light_at(gfx, s_selLight);
    ImGui::SeparatorText("Selection");
    gX_Tools_ImGui_GradientAccent(IM_COL32(74, 48, 98, 255),
        IM_COL32(224, 170, 82, 255), 2.0f);
    if (model) {
        ImGui::Text("%d: %s", s_selModel, gfx_model_basename(model->path));
        ImGui::TextDisabled("model");
    } else if (light) {
        ImGui::Text("%d: %s", s_selLight, gfx_light_type_name(light->type));
        ImGui::TextDisabled("light");
    } else if (s_selectedCamera >= 0 && s_selectedCamera < s_cameraCount) {
        ImGui::Text("%d: %.*s", s_selectedCamera,
            (gX_I32)sizeof(s_cameras[s_selectedCamera].name),
            s_cameras[s_selectedCamera].name);
        ImGui::TextDisabled("camera");
    } else {
        ImGui::TextDisabled("Nothing selected");
    }
    if (model || light) {
        if (ImGui::Button("Frame")) {
            if (model) gfx_camera_frame_model(model);
            else gfx_camera_frame_light(light);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            if (model) {
                gX_RendererGFX_Model_Remove(gfx, (gX_U32)s_selModel);
                s_selModel = -1;
                s_modelRevertModel = -1;
                s_modelRevertValid = 0;
                gfx_model_history_reset();
                gfx_model_snapshots_clear();
            } else {
                gX_RendererGFX_Light_Remove(gfx, (gX_U32)s_selLight);
                s_selLight = -1;
            }
        }
    }
    gfx_scene_quick_controls(gfx);
    //ai: history is a common task; keep it above the lab tabs at 1024.
    if (model) gfx_model_history_panel(gfx, model, s_selModel);
}

//opencode: hold other editor actions while a browser target is captured.
static gX_I0 gfx_menu_bar(gX_RendererGFX *gfx) {
    if (s_assetBrowserOpen || s_freeCameraActive) return;
    if (!ImGui::BeginMainMenuBar()) return;
    //opencode: Frutiger Aero v2 — gradient bar before the items draw on top.
    gX_Tools_ImGui_Aero_MenuBar();
    //opencode: expose the same scale and navigation controls in the GFX editor.
    gX_Tools_ImGui_AccessibilityMenu();
    //ai: visible focus-view escape; the menu item remains for discoverability.
    if (ImGui::SmallButton(s_focusViewport ? "Exit focus" : "Focus view"))
        gfx_focus_viewport_set(!s_focusViewport);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide both side panels (F6)");
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save preset", "Ctrl+S") && gfx) gfx_preset_save(gfx);
        if (ImGui::MenuItem("Load preset...", "Ctrl+O")) {
            s_inspectorPanelOpen = 1;
            ImGui::OpenPopup("Confirm GFX preset load");
        }
        if (ImGui::MenuItem("Close editor")) gX_Tools_Close_Runtime();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        gX_RendererGFX_Model *model = gfx ? gfx_model_at(gfx, s_selModel) : 0;
        gX_RendererGFX_Light *light = gfx ? gfx_light_at(gfx, s_selLight) : 0;
        bool selection = model != 0 || light != 0;
        if (ImGui::MenuItem("Frame selected", "F", false, selection)) {
            if (model) gfx_camera_frame_model(model);
            else if (light) gfx_camera_frame_light(light);
        }
        if (ImGui::MenuItem("Reset selected position", "R", false, model != 0) && model)
            model->params.pos = (gX_F32v3){0.0f, 0.0f, 0.0f};
        if (ImGui::MenuItem("Remove selected", "Delete", false, selection)) {
            if (model) {
                gX_RendererGFX_Model_Remove(gfx, (gX_U32)s_selModel);
                s_selModel = -1;
                s_modelRevertModel = -1;
                s_modelRevertValid = 0;
                gfx_model_history_reset();
                gfx_model_snapshots_clear();
            } else if (light) {
                gX_RendererGFX_Light_Remove(gfx, (gX_U32)s_selLight);
                s_selLight = -1;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        bool scene = s_scenePanelOpen != 0;
        bool inspector = s_inspectorPanelOpen != 0;
        if (ImGui::Checkbox("Scene panel", &scene)) {
            s_scenePanelOpen = scene ? 1 : 0;
            s_focusViewport = 0;
        }
        if (ImGui::Checkbox("Inspector panel", &inspector)) {
            s_inspectorPanelOpen = inspector ? 1 : 0;
            s_focusViewport = 0;
        }
        if (ImGui::MenuItem(s_focusViewport ? "Exit focus viewport" : "Focus viewport",
                "F6", s_focusViewport))
            gfx_focus_viewport_set(!s_focusViewport);
        if (gfx) {
            bool gizmo = gfx->state.SHOW_GIZMO != 0;
            bool boxes = gfx->state.SHOW_BOUNDING_BOX != 0;
            bool skeleton = gfx->state.SHOW_SKELETON != 0;
            if (ImGui::Checkbox("Transform gizmo", &gizmo)) gfx->state.SHOW_GIZMO = gizmo ? 1 : 0;
            if (ImGui::Checkbox("Bounding boxes", &boxes)) gfx->state.SHOW_BOUNDING_BOX = boxes ? 1 : 0;
            if (ImGui::Checkbox("Skeleton overlay", &skeleton)) gfx->state.SHOW_SKELETON = skeleton ? 1 : 0;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        bool pick = s_pickEnabled != 0;
        if (ImGui::Checkbox("Pick models and lights with mouse", &pick))
            s_pickEnabled = pick ? 1 : 0;
        if (gfx && ImGui::MenuItem("Browse model assets..."))
            gfx_asset_browser_open(gfx, gX_GFX_ASSET_MODELS,
                gX_GFX_ASSET_TARGET_NONE, -1, -1, -1, -1);
        if (gfx && ImGui::MenuItem("Browse texture assets..."))
            gfx_asset_browser_open(gfx, gX_GFX_ASSET_TEXTURES,
                gX_GFX_ASSET_TARGET_NONE, -1, -1, -1, -1);
        if (ImGui::BeginMenu("Translate axis")) {
            ImGui::RadioButton("XZ plane", &s_moveAxis, 0);
            ImGui::RadioButton("X only", &s_moveAxis, 1);
            ImGui::RadioButton("Y only", &s_moveAxis, 2);
            ImGui::RadioButton("Z only", &s_moveAxis, 3);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Report bug...", "F10")) gX_BugReport.open = 1;
        ImGui::Separator();
        ImGui::TextWrapped("Click-drag a visible model or light marker to translate on the selected plane.");
        ImGui::TextWrapped("Hold X/Y/Z + drag for world-axis translation. Hold R + drag to rotate; R+X/Y/Z constrains the rotation axis. Hold S + drag to scale uniformly; S+X/Y/Z scales one axis.");
        ImGui::Text("Move buttons and numeric controls are in Inspector > Models.");
        ImGui::Text("Ctrl during drag snaps; Alt+G/R/S resets position/rotation/scale.");
        ImGui::Text("F frames the selection; Delete removes it.");
        ImGui::Text("Middle-drag orbits; Shift+middle-drag pans; wheel zooms.");
        ImGui::Text("World > Camera enters free camera; Esc returns to authoring.");
        ImGui::Text("F6 hides both side panels for a full-stage view.");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}
#endif

extern "C" gX_I0 gX_Tools_Gfx_Open(gX_I0) {
#if gX_ENABLE_RENDERER == 1
    //opencode: browser state and GL thumbnails are transient across editor sessions.
    gfx_preset_assets_release();
    gfx_material_assets_release();
    gfx_asset_thumbnails_clear();
    s_assetThumbnailClearPending = 0;
    s_assetBrowserOpen = 0;
    s_assetTextureTarget = gX_GFX_ASSET_TARGET_NONE;
    s_assetTargetIndex = -1;
    s_assetTargetSlot = -1;
    s_assetTargetPurpose = -1;
#endif
    s_selLight = -1;
    s_selModel = -1;
    s_modelRevertModel = -1;
    s_modelRevertValid = 0;
#if gX_ENABLE_RENDERER == 1
    gfx_model_history_reset();
    gfx_model_snapshots_clear();
#endif
    s_sunInit = 0;
    s_scenePanelOpen = 1;
    s_inspectorPanelOpen = 1;
    s_focusViewport = 0;
    s_focusSavedScene = 1;
    s_focusSavedInspector = 1;
    s_pickEnabled = 1;
    //ai: headless capture hook for the focus-viewport state.
    const char *focusEnv = getenv("GX_EDITOR_FOCUS_VIEWPORT");
    if (focusEnv && focusEnv[0] && focusEnv[0] != '0')
        gfx_focus_viewport_set(1);
    s_freeCameraActive = 0;
    input.toolCameraInputEnabled = 0;
    s_moveAxis = 0;
    s_draggingModel = 0;
    s_draggingLight = 0;
    s_dragMoved = 0;
    s_dragMode = gX_GFX_EDITOR_DRAG_PLANE;
    s_dragAxis = -1;
    s_dragModel = -1;
    s_dragLight = -1;
    s_presetStatus[0] = 0;
    s_presetOverwritePending = 0;
    s_headlessPresetSaveDone = 0;
    s_headlessPresetSaveUiDone = 0;
    s_headlessPresetLoadDone = 0;
    s_headlessSnapshotSmokeDone = 0;
#if gX_ENABLE_RENDERER == 1
    //opencode: material-asset editor state lives inside the renderer guard.
    s_headlessMaterialAssetCheckDone = 0;
    s_selectedMaterialOverride = 0;
    s_materialAssetStatus[0] = 0;
#endif
    gX_TraceLog(gX_LOG_INFO, "Tools: GFX editor open");
}

//opencode: retain child-window item widths so drag controls have room for labels.
extern "C" gX_I0 gX_Tools_Gfx_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Gfx_Draw(gX_I0) {
#if gX_ENABLE_RENDERER == 1
    gX_I32 palette = gfx_editor_palette_push();
    //opencode: Escape exits free camera; a direct cursor toggle also restores
    //   authoring mode.
    if (s_freeCameraActive && (input.keyboard.keys[gX_KEY_ESCAPE].state.PRESSED
        || input.cursorEnabled))
        gfx_free_camera_set(0);
    if (!s_freeCameraActive && !input.cursorEnabled) {
        input.cursorEnabled = 1;
        input.cursorSwitched = 1;
    }
    if (s_freeCameraActive) {
        gfx_free_camera_overlay();
        gfx_editor_shortcut_footer(0, 1);
        gfx_editor_palette_pop(palette);
        return;
    }
    if (s_assetThumbnailClearPending) {
        gfx_asset_thumbnails_clear();
        s_assetThumbnailClearPending = 0;
    }
    gX_RendererGFX *gfx = gfx_state();
    gfx_camera_render_tick(gfx);
    if (input.keyboard.keys[gX_KEY_F6].state.PRESSED)
        gfx_focus_viewport_set(!s_focusViewport);
    gfx_menu_bar(gfx);
#ifndef NDEBUG
    gX_Tools_Gfx_BugReport_Test();
#endif
    gX_Tools_Gfx_BugReport_Draw();
    if (gfx && !s_assetBrowserOpen && !gX_BugReport.open) {
        gfx_preset_headless_hooks(gfx);
        gfx_editor_viewport_input(gfx);
        gfx_editor_keyboard_input(gfx);
        gfx_model_history_observe(s_selModel, gfx_model_at(gfx, s_selModel));
        gfx_light_history_observe(s_selLight, gfx_light_at(gfx, s_selLight));
    }
    gX_F32 scrW = (gX_F32)maindata.windowWidth;
    gX_F32 scrH = (gX_F32)maindata.windowHeight;
    if (scrW <= 0) scrW = ImGui::GetIO().DisplaySize.x;
    if (scrH <= 0) scrH = ImGui::GetIO().DisplaySize.y;
    gX_F32 menuH = ImGui::GetFrameHeight();
    gX_F32 panelH = scrH - menuH;
    if (panelH < 100.0f) panelH = 100.0f;
    //opencode: scale the fixed side panels with text so enlarged controls
    //   retain room for labels, then cap them on narrow displays.
    gX_F32 uiScale = ImGui::GetStyle().FontScaleMain;
    if (uiScale < 1.0f) uiScale = 1.0f;
    //ai: 320/360 base keeps 1600/1920 dockable while the 1024 window
    //   falls back to the overlay layout.
    gX_F32 sceneW = 320.0f * uiScale;
    if (sceneW < 280.0f) sceneW = 280.0f;
    gX_F32 inspectorW = 360.0f * uiScale;
    if (scrW < 1440.0f) {
        //ai: high text scales need proportionally wider panels; otherwise
        //   150/175% labels clip even though the text is readable.
        gX_F32 extra = uiScale - 1.25f;
        if (extra < 0.0f) extra = 0.0f;
        if (extra > 0.5f) extra = 0.5f;
        gX_F32 sceneCap = scrW * (0.28f + extra * 0.16f);
        gX_F32 inspectorCap = scrW * (0.38f + extra * 0.16f);
        if (sceneW > sceneCap) sceneW = sceneCap;
        if (inspectorW > inspectorCap) inspectorW = inspectorCap;
        if (sceneW < 260.0f) sceneW = 260.0f;
        if (inspectorW < 340.0f) inspectorW = 340.0f;
    }
    if (inspectorW > scrW - sceneW - 20.0f) inspectorW = scrW - sceneW - 20.0f;
    if (inspectorW < 320.0f) inspectorW = 320.0f;
    //opencode: the scene panel owns the dock's left margin; focus mode
    //   reclaims it by moving the margin to zero.
    gX_F32 leftInset = 0.0f;
    if (gui.viewportDocked)
        leftInset = s_scenePanelOpen ? (sceneW - 320.0f) : -320.0f;
    gX_GUI_Viewport_SetLeftInset(leftInset);
    //opencode: reserve the inspector's width in the dock so the scene fits the
    //   visible area instead of rendering behind the panel.
    gX_GUI_Viewport_SetRightInset(
        (gui.viewportDocked && s_inspectorPanelOpen) ? inspectorW : 0.0f);
    s_footerSceneW = sceneW;
    s_footerInspectorW = inspectorW;
    //opencode: harness hook — the dock-layout gate reads the resolved geometry.
    const gX_I8 *dockTrace = getenv("GX_DOCK_TRACE");
    if (dockTrace && dockTrace[0] && dockTrace[0] != '0'
        && (ImGui::GetFrameCount() % 30u) == 0u) {
        gX_Rectangle dockBox = gX_GUI_ViewportBox_Get();
        gX_TraceLog(gX_LOG_INFO,
            "GFX_DOCK: docked=%d window=%ux%u viewport=(%.0f,%.0f,%.0f,%.0f) "
            "inset=%.0f inspector=%.0f scene=%.0f",
            gui.viewportDocked ? 1 : 0, maindata.windowWidth,
            maindata.windowHeight, dockBox.x, dockBox.y, dockBox.width,
            dockBox.height, (gui.viewportDocked && s_inspectorPanelOpen)
                ? inspectorW : 0.0f, inspectorW, sceneW);
    }
    ImGuiCond panelCond = gui.viewportDocked ? ImGuiCond_Always : ImGuiCond_FirstUseEver;

    if (s_scenePanelOpen) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, menuH), panelCond);
        ImGui::SetNextWindowSize(ImVec2(sceneW, panelH), panelCond);
        bool sceneOpen = true;
        ImGuiWindowFlags sceneFlags = s_assetBrowserOpen ? ImGuiWindowFlags_NoInputs : 0;
        if (ImGui::Begin("GFX Scene", &sceneOpen, sceneFlags)) {
            gX_Tools_ImGui_Aero_Panel();
            gX_RendererRaymarch *raymarch = gfx_raymarch_active();
            if (maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH && raymarch) {
                gfx_raymarch_panel(raymarch);
                if (renderer.ppLoaded) gfx_pp_panel(&renderer.pp);
            } else if (gfx) {
                gfx_scene_panel(gfx);
            } else {
                gX_RendererRaymarch *rm = gfx_raymarch_active();
                if (rm) gfx_raymarch_panel(rm);
                if (renderer.ppLoaded) gfx_pp_panel(&renderer.pp);
                else if (!rm) ImGui::TextWrapped(
                    "No GFX state — open a GFX scene first (--gfx).");
            }
        }
        ImGui::End();
        s_scenePanelOpen = sceneOpen ? 1 : 0;
    }

    if (s_inspectorPanelOpen) {
        ImGui::SetNextWindowPos(ImVec2(scrW - inspectorW, menuH), panelCond);
        ImGui::SetNextWindowSize(ImVec2(inspectorW, panelH), panelCond);
        bool inspectorOpen = true;
        ImGuiWindowFlags inspectorFlags = s_assetBrowserOpen ? ImGuiWindowFlags_NoInputs : 0;
        if (ImGui::Begin("GFX Inspector", &inspectorOpen, inspectorFlags)) {
            gX_Tools_ImGui_Aero_Panel();
            if (!gfx) {
                ImGui::TextWrapped("No GFX state — open a GFX scene first (--gfx).");
            } else {
                gfx_editor_selection_panel(gfx);
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Document", ImGuiTreeNodeFlags_DefaultOpen)) {
                    gfx_preset_panel(gfx);
                    ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.60f, 1.0f), "LIVE");
                    ImGui::TextDisabled("Edit live; save preset.");
                }
                ImGui::Separator();
                ImGui::BeginChild("GfxInspectorContent", ImVec2(0, 0), false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.12f, 0.16f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18f, 0.30f, 0.46f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.22f, 0.48f, 0.72f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.55f, 0.82f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(0.18f, 0.32f, 0.46f, 1.0f));
                //opencode: task-oriented lab tabs keep the full renderer surface
                //   available without forcing a narrow dock to show every section.
                if (ImGui::BeginTabBar("GfxLabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    if (ImGui::BeginTabItem("Models")) {
                        if (ImGui::BeginTabBar("GfxModelSections", ImGuiTabBarFlags_FittingPolicyScroll)) {
                            if (ImGui::BeginTabItem("Model")) {
                                gfx_models_panel(gfx);
                                ImGui::EndTabItem();
                            }
                            if (ImGui::BeginTabItem("Surface")) {
                                gfx_model_surface_panel(gfx);
                                ImGui::EndTabItem();
                            }
                            ImGui::EndTabBar();
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Sky")) {
                        gfx_skylab_panel(gfx);
                        gfx_sky_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("World")) {
                        gfx_camera_panel(gfx);
                        gfx_weather_panel(gfx);
                        gfx_decals_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("FX")) {
                        gfx_fog_panel(gfx);
                        gfx_fogvolume_panel(gfx);
                        gfx_bloom_panel(gfx);
                        gfx_godray_panel(gfx);
                        gfx_lensflare_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Lights")) {
                        gfx_lights_panel(gfx);
                        gfx_shadow_panel(gfx);
                        gfx_csm_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Render")) {
                        gfx_look_panel(gfx);
                        gfx_ssao_panel(gfx);
                        gfx_shader_tuning_panel(gfx);
                        gfx_quality_panel(gfx);
                        gfx_toggles_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    //ai: water authoring is discoverable without opening general render tuning.
                    if (ImGui::BeginTabItem("Water")) {
                        gfx_editor_water_panel(gfx, &s_selectedWave, gfx_water_normal_browse,
                            gfx_color_edit, gfx_text_disabled_wrapped);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Ray")) {
                        gfx_ray_preview_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Particles")) {
                        gfx_particles_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Refl")) {
                        gfx_reflect_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Debug")) {
                        gfx_debug_panel(gfx);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::PopStyleColor(5);
                ImGui::EndChild();
            }
        }
        ImGui::End();
        s_inspectorPanelOpen = inspectorOpen ? 1 : 0;
    }
    gfx_editor_camera_markers_draw();
    gfx_editor_shortcut_footer(gfx, 0);
    gfx_asset_browser_window(gfx);
    if (gfx) gfx_snapshot_headless_smoke(gfx);
    gfx_editor_palette_pop(palette);
#else
    ImGui::SetNextWindowPos(ImVec2(40.0f, ImGui::GetFrameHeight() + 40.0f), ImGuiCond_FirstUseEver);
    bool inspectorOpen = true;
    if (ImGui::Begin("GFX Inspector", &inspectorOpen))
        ImGui::TextWrapped("GFX editor needs the renderer cluster (full build).");
    ImGui::End();
#endif
}
