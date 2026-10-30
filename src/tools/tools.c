/**********************************************************************************************
*
*   [gX: src/tools/tools.c]
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

//opencode: v0.4.0 — gX Tools suite bootstrap. --tools <markup>:
//   - registers the markup template (live mtime hot-reload),
//   - builds the markup onto canvas 0,
//   - loads the ImGui runtime,
//   - opens the GUI editor targeting that canvas.
//   The editor chrome runs on ImGui so it never depends on the gX GUI
//   system being complete (breaks the gui-creates-gui loop).
#include <string.h>
#include <stdio.h>

#include "../main.h"
#include "../input.h"
#include "../module/module.h"
#include "../gui/gui.h"
#include "../gui/markup.h"
#include "../gui/canvas.h"
#include "../gui/style.h"
#include "../common/enum.h"
#include "../common/log.h"

extern gX_I0 gX_GUI_Load_ImGui(gX_I0);
extern gX_I0 gX_Tools_Editor_Open(gX_I32 canvasIndex, const gX_I8 *markupPath);

//opencode: v0.6.5 — runtime open flags (CLI inits + EXTRAS buttons set these;
//   ImGui window-X writes back; draw gate reads).
static gX_I1 s_runtimeEditor = 0;
static gX_I1 s_runtimeTheme = 0;
static gX_I1 s_runtimeGfx = 0;
static gX_I1 s_runtimeWavetable = 0;
static gX_I1 s_runtimeTextwriter = 0;
static gX_I1 s_runtimeRail = 0;
static gX_I1 s_runtimeBackground = 0;
static gX_I1 s_runtimeSky = 0;
//ai: physics editor — sandbox/applier tuning with presets (routes to the
//   physics scene).
static gX_I1 s_runtimePhysics = 0;
//ai: scripting debugger panel — opt-in builds only (opener logs a hint when
//   GX_USE_ANGELSCRIPT is off).
static gX_I1 s_runtimeScript = 0;
//opencode: preserve the scene's camera-cursor mode across the GFX editor.
static gX_I1 s_gfxCursorRestore = 0;
static gX_I1 s_gfxCursorRestoreValid = 0;

gX_I0 gX_Tools_Init(gX_I0) {
    const gX_I8 *path = arguments.toolsMarkupPath;
    if (!path[0]) {
        gX_TraceLog(gX_LOG_WARNING, "Tools: --tools needs a markup path");
        return;
    }

    //opencode: register markup template (canvas 0) — enables the live
    //   mtime hot-reload that the editor's SAVE + rebuild cycle uses
    gX_GUI_Template_RegisterMarkup(path);
    gX_TraceLog(gX_LOG_INFO, "Tools: markup template registered '%s'", path);

    //opencode: ImGui runtime (editor chrome)
    gX_GUI_Load_ImGui();

    //opencode: editor targets canvas 0 (the markup canvas)
    gX_Tools_Editor_Open(0, path);
    s_runtimeEditor = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: GUI editor open (GUIPLAYGROUND)");
}

//opencode: v0.4.0 — theme editor entry. The theme path is already active//   (set via gX_GUI_Style_SetThemePath before gX_GUI_Load), so the title
//   (or the flagged scene) renders with the target theme from frame one;
//   the tool then edits the live token structs and writes them back.
extern gX_I0 gX_Tools_Theme_Open(const gX_I8 *themePath);
extern gX_I0 gX_Tools_SetImGuiThemeMode(gX_I1 on);

gX_I0 gX_Tools_Theme_Init(gX_I0) {
    const gX_I8 *path = arguments.themeEditorPath;
    if (!path[0]) {
        gX_TraceLog(gX_LOG_WARNING, "Tools: --theme-editor needs a theme path");
        return;
    }
    gX_Tools_SetImGuiThemeMode(1);
    gX_Tools_Theme_Open(path);
    s_runtimeTheme = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: theme editor open (theme '%s')", path);
}

//opencode: v0.6.2 — --themlab — live design preview. Builds the THEMLAB
//   canvas template onto canvas 2 (the same one-shot slot the audit
//   GX_GUI_TEMPLATE_FORCE hook uses; gui.auditTemplateID makes respond
//   re-queue it after every clearAll). No ImGui chrome — pair with
//   --theme-editor to edit tokens live against the gallery.
gX_I0 gX_Tools_ThemLab_Init(gX_I0) {
    //opencode: THEMLAB is built by the gui.c FORCE audit hook during
    //   GUI_Load (see --themlab parse). Nothing late to do — the hook's
    //   auditTemplateID already re-queues it across clearAll.
    gX_TraceLog(gX_LOG_INFO, "Tools: themlab preview via FORCE audit hook");
}
//opencode: v0.6.5 — runtime tool open (EXTRAS menu). CLI flags init tools at
//   load; these open/close them from in-app buttons. Each tool has a runtime
//   flag gating the ImGui draw (gui.c) independently of the CLI flags, and
//   window-X close buttons write back to the same flags.
gX_I1 gX_Tools_RuntimeActive(gX_I0) {
    return (s_runtimeEditor || s_runtimeTheme || s_runtimeGfx || s_runtimeScript
            || s_runtimeWavetable || s_runtimeTextwriter
            || s_runtimePhysics || s_runtimeRail
            || s_runtimeBackground || s_runtimeSky) ? 1 : 0;
}

//opencode: openers are exclusive — every runtime flag clears before the
//   opener sets its own (the ImGui chain draws one tool at a time).
static gX_I0 gX_Tools_ClearRuntimeFlags(gX_I0) {
    s_runtimeEditor = 0;
    s_runtimeTheme = 0;
    s_runtimeGfx = 0;
    s_runtimeScript = 0;
    s_runtimeWavetable = 0;
    s_runtimeTextwriter = 0;
    s_runtimePhysics = 0;
    s_runtimeRail = 0;
    s_runtimeBackground = 0;
    s_runtimeSky = 0;
}

gX_I0 gX_Tools_OpenEditor_Runtime(gX_I0) {
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    //opencode: out-of-box target = canvas 0 (whatever scene is up; title by
    //   default). Save path prefills a scratch file so Save never clobbers
    //   shipped markups by accident.
    gX_Tools_Editor_Open(0, "resources/tools/scratch.json");
    gX_Tools_ClearRuntimeFlags();
    s_runtimeEditor = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: GUI editor opened at runtime (canvas 0)");
}

gX_I0 gX_Tools_OpenTheme_Runtime(gX_I0) {
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(1);
    //opencode: edit the default theme out-of-box (--theme-editor remains
    //   for other theme files).
    gX_Tools_Theme_Open(GX_GUI_STYLE_DEFAULT_PATH);
    gX_Tools_ClearRuntimeFlags();
    s_runtimeTheme = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: theme editor opened at runtime");
}

gX_I0 gX_Tools_OpenThemLab_Runtime(gX_I0) {    //opencode: preview onto canvas 2 (audit re-queue keeps it across
    //   clearAll) + theme editor over it (the documented pair).
    gui.auditTemplateID = gX_GUI_TEMPLATE_THEMLAB;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_THEMLAB, 2);
    gX_Tools_OpenTheme_Runtime();
    gX_TraceLog(gX_LOG_INFO, "Tools: themlab opened at runtime");
}

//opencode: material response gallery uses the same persistent canvas-2 audit
//   path as ThemeLab but does not open an ImGui window over the preview.
gX_I0 gX_Tools_OpenMaterials_Runtime(gX_I0) {
    gui.auditTemplateID = gX_GUI_TEMPLATE_MATERIALSLAB;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_MATERIALSLAB, 2);
    gX_TraceLog(gX_LOG_INFO, "Tools: materials lab opened at runtime");
}

//opencode: shader experiment gallery — same canvas-2 audit path as Materials.
gX_I0 gX_Tools_OpenShaderLab_Runtime(gX_I0) {
    gui.auditTemplateID = gX_GUI_TEMPLATE_SHADERLAB;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_SHADERLAB, 2);
    gX_TraceLog(gX_LOG_INFO, "Tools: shader lab opened at runtime");
}

//opencode: wavetable editor — synth wavetable authoring (draw/generate,
//   preview, raw .f32 fixtures). Exclusive like the other runtime editors.
gX_I0 gX_Tools_OpenWavetable_Runtime(gX_I0) {
    extern gX_I0 gX_Tools_Wavetable_Reset(gX_I0);
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_Wavetable_Reset();
    gX_Tools_ClearRuntimeFlags();
    s_runtimeWavetable = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: wavetable editor opened at runtime");
}

//opencode: textwriter editor — line/tag/choice authoring over the live
//   writer with scene preview. Exclusive like the other runtime editors.
gX_I0 gX_Tools_OpenTextwriter_Runtime(gX_I0) {
    extern gX_I0 gX_Tools_Textwriter_Reset(gX_I0);
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_Textwriter_Reset();
    gX_Tools_ClearRuntimeFlags();
    s_runtimeTextwriter = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: textwriter editor opened at runtime");
}

//ai: physics editor — sandbox/applier tuning with JSON presets; routes to
//   the physics scene (the tuning target) like the textwriter editor.
gX_I0 gX_Tools_OpenPhysics_Runtime(gX_I0) {
    extern gX_I0 gX_Tools_Physics_Reset(gX_I0);
#if gX_ENABLE_RENDERER == 1
    if (maindata.masterIndex != gX_SCENE_PHYSICS) {
        gX_GUI_Focus_Clear();
        gX_MainData_Switch(gX_SCENE_PHYSICS);
    }
#endif
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_Physics_Reset();
    gX_Tools_ClearRuntimeFlags();
    s_runtimePhysics = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: physics editor opened at runtime");
}

//opencode: camera rail editor — the rail lives in the gx_shell module
//   (docs/editors.md placement decision), so the exe opener only checks the
//   module and flips the flag the module's tools-draw hook reads.
gX_I0 gX_Tools_OpenRail_Runtime(gX_I0) {
    if (!gX_Module_ToolsAvailable()) {
        gX_TraceLog(gX_LOG_WARNING,
            "Tools: camera rail editor needs the gx_shell module (docs/dynamic_modules.md)");
        return;
    }
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_ClearRuntimeFlags();
    s_runtimeRail = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: rail editor opened at runtime");
}

//opencode: background editor — focused background authoring; routes to the
//   --background scene (the preview surface), mirroring the physics editor.
gX_I0 gX_Tools_OpenBackground_Runtime(gX_I0) {
#if gX_ENABLE_RENDERER == 1
    if (maindata.masterIndex != gX_SCENE_RENDERER_BACKGROUND) {
        gX_GUI_Focus_Clear();
        gX_MainData_Switch(gX_SCENE_RENDERER_BACKGROUND);
    }
#endif
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_ClearRuntimeFlags();
    s_runtimeBackground = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: background editor opened at runtime");
}

//opencode: sky editor — look parameters + the ephemeris clock; routes to the
//   sky scene (the preview surface).
gX_I0 gX_Tools_OpenSky_Runtime(gX_I0) {
#if gX_ENABLE_RENDERER == 1
    if (maindata.masterIndex != gX_SCENE_RENDERER_SKY) {
        gX_GUI_Focus_Clear();
        gX_MainData_Switch(gX_SCENE_RENDERER_SKY);
    }
#endif
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_ClearRuntimeFlags();
    s_runtimeSky = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: sky editor opened at runtime");
}

gX_I0 gX_Tools_Close_Runtime(gX_I0) {
    extern gX_I0 gX_Tools_Editor_ClearEditLock(gX_I0);
    extern gX_I0 gX_Tools_Editor_SoloRestore(gX_I0);
    extern gX_I0 gX_Tools_Wavetable_Shutdown(gX_I0);
    extern gX_I0 gX_Tools_Physics_Shutdown(gX_I0);
    gX_Tools_Editor_ClearEditLock();
    gX_Tools_Editor_SoloRestore();
    gX_Tools_Wavetable_Shutdown();
    gX_Tools_Physics_Shutdown();
    //opencode: do not leave runtime camera binds enabled after closing tools.
    input.toolCameraInputEnabled = 0;
    if (s_gfxCursorRestoreValid) {
        input.cursorEnabled = s_gfxCursorRestore;
        input.cursorSwitched = 1;
        s_gfxCursorRestoreValid = 0;
    }
    s_runtimeEditor = 0;
    s_runtimeTheme = 0;
    s_runtimeGfx = 0;
    s_runtimeScript = 0;
    s_runtimeWavetable = 0;
    s_runtimeTextwriter = 0;
    s_runtimePhysics = 0;
    s_runtimeRail = 0;
    s_runtimeBackground = 0;
    s_runtimeSky = 0;
    //opencode: drop host panel insets so the next dock starts from the defaults.
    gX_GUI_Viewport_SetLeftInset(0.0f);
    gX_GUI_Viewport_SetRightInset(0.0f);
    gX_TraceLog(gX_LOG_INFO, "Tools: runtime tools closed");
}

gX_I1 *gX_Tools_RuntimeEditorFlag(gX_I0) { return &s_runtimeEditor; }
gX_I1 *gX_Tools_RuntimeThemeFlag(gX_I0) { return &s_runtimeTheme; }
gX_I1 *gX_Tools_RuntimeGfxFlag(gX_I0) { return &s_runtimeGfx; }
gX_I1 *gX_Tools_RuntimeScriptFlag(gX_I0) { return &s_runtimeScript; }
gX_I1 *gX_Tools_RuntimeWavetableFlag(gX_I0) { return &s_runtimeWavetable; }
gX_I1 *gX_Tools_RuntimeTextwriterFlag(gX_I0) { return &s_runtimeTextwriter; }
gX_I1 *gX_Tools_RuntimePhysicsFlag(gX_I0) { return &s_runtimePhysics; }
gX_I1 *gX_Tools_RuntimeRailFlag(gX_I0) { return &s_runtimeRail; }
gX_I1 *gX_Tools_RuntimeBackgroundFlag(gX_I0) { return &s_runtimeBackground; }
gX_I1 *gX_Tools_RuntimeSkyFlag(gX_I0) { return &s_runtimeSky; }

//ai: scripting debugger — opens the ImGui panel (drawn by the gx_shell
//   module; the default build gets a hint instead).
gX_I0 gX_Tools_OpenScript_Runtime(gX_I0) {
    if (!gX_Module_ToolsAvailable()) {
        gX_TraceLog(gX_LOG_WARNING,
            "Tools: scripting debugger needs the gx_shell module (docs/dynamic_modules.md)");
        return;
    }
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    s_runtimeScript = 1;
    s_runtimeEditor = 0;
    s_runtimeTheme = 0;
    s_runtimeGfx = 0;
    s_runtimeWavetable = 0;
    s_runtimeTextwriter = 0;
    gX_TraceLog(gX_LOG_INFO, "Tools: scripting debugger opened at runtime");
}

//opencode: gfx editor — third tool. Exclusive like the theme editor
//   (one dock panel at a time); edits live GFX state, needs no path.
gX_I0 gX_Tools_OpenGfx_Runtime(gX_I0) {
    extern gX_I0 gX_Tools_Gfx_Open(gX_I0);
    if (!s_gfxCursorRestoreValid) {
        s_gfxCursorRestore = input.cursorEnabled;
        if (maindata.masterIndex == gX_SCENE_RENDERER_GFX
            && !input.masterIndexSwitchResponse)
            s_gfxCursorRestore = 0;
        s_gfxCursorRestoreValid = 1;
    }
    input.cursorEnabled = 1;
    input.cursorSwitched = 1;
    gX_GUI_Load_ImGui();
    gX_Tools_SetImGuiThemeMode(0);
    gX_Tools_Gfx_Open();
    gX_Tools_ClearRuntimeFlags();
    s_runtimeGfx = 1;
    gX_TraceLog(gX_LOG_INFO, "Tools: GFX editor opened at runtime");
}
