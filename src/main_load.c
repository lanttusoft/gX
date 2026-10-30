/**********************************************************************************************
*
*   [gX: src/main_load.c]
*   Loading stage (loadtime)
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


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "main.h"
#include "input.h"
#include "camera.h"
#include "wrappers/wrappers.h"
#include "common/hardware.h"

#include "audio.h"

#include "gui/gui.h"
#include "gui/markup.h"

#if gX_ENABLE_RENDERER == 1
    #include "renderer/renderer.h"
#endif
#if gX_ENABLE_PHYSICS == 1
    #include "physics/physics.h"
#endif
#if gX_ENABLE_SCENE == 1
    #include "scene/scene.h"
#endif

#include "resource/resource.h"
#include "settings.h"

#include "common/config.h"
#include "common/enum.h"
#include "common/log.h"
#include "common/random.h"
#include "common/shader_compiler.h"
#include "rhi/rhi_caps.h"
#include "rhi/rhi_state.h"

#include "game/game.h"

#include "module/module.h"

#include "asm/asm.h"

gX_I32 gX_MainData_Refresh(gX_I0) {
    
    maindata.renderWidth = (gX_U16)(maindata.renderScale*maindata.screenWidth);
    maindata.renderHeight = (gX_U16)(maindata.renderScale*maindata.screenHeight);
    
    return 0;
}

gX_I32 gX_MainData_Reset(gX_I0) {
    
    if (arguments.hasScreenWidth) { 
        maindata.windowWidth = arguments.screenWidth;
        maindata.screenWidth = arguments.screenWidth;
    }
    else { 
        maindata.windowWidth = MAINDATA_DEFAULT_WINDOWWIDTH;
        maindata.screenWidth = MAINDATA_DEFAULT_SCREENWIDTH; 
    }
    if (arguments.hasScreenHeight) {
        maindata.windowHeight = arguments.screenHeight;
        maindata.screenHeight = arguments.screenHeight;
    }
    else {
        maindata.windowHeight = MAINDATA_DEFAULT_WINDOWHEIGHT;
        maindata.screenHeight = MAINDATA_DEFAULT_SCREENHEIGHT;
    }
    
    if (arguments.hasRenderScale) maindata.renderScale = arguments.renderScale;
    else maindata.renderScale = 1.0f; //opencode: full-res default — 0.25 made the demo a 384x288 blocky upscale
    
    gX_MainData_Refresh();
    
    return 0;
}

gX_I32 gX_MainData_Init(gX_I0) {
    gX_I32 errors = 0;
    
    maindata = (gX_MainData){0};
    
    strcpy(maindata.name,"gX project");
    strcpy(maindata.description,"globals");
    
    errors = gX_MainData_Reset();
    
    maindata.loop = true;
    maindata.backendUse = true;
    maindata.backendAudioUse = true;
    //ai: --disableAudio now actually disables the device (the flag was
    //   parsed into arguments.disableModuleAudio but never applied, so the
    //   run still opened miniaudio/PulseAudio).
    if (arguments.disableModuleAudio) maindata.backendAudioUse = false;
    maindata.bgColor = (gX_Color){255,255,255,255};
    maindata.masterVolume = 1.0f;
    
    maindata.targetFPS = 60.0f;

    //opencode: kernel cluster — moduleinfo mirror (see proofs/
    //   ModuleInfoConsistency). Always-on modules read 1; the cluster
    //   trio follows the build flags (kernel-only → all false).
    maindata.moduleinfo.rendererExists = (gX_ENABLE_RENDERER == 1);
    maindata.moduleinfo.gamedataExists = (gX_ENABLE_GAME == 1);
    maindata.moduleinfo.sceneExists    = (gX_ENABLE_SCENE == 1);

    if (arguments.hasMasterIndex) {
        errors = gX_MainData_Switch(arguments.masterIndex);
    }
#if gX_ENABLE_SCENE == 1
    else {
        //opencode: v0.3.8 — fresh-install rule (frontend_design.md): ONLY a
        //   genuinely first-ever launch (no settings file AND no save data)
        //   skips the title and lands in the renderer demo. Any returning
        //   user — settings.json from a previous run — gets the title, even
        //   without a save. GX_SKIPTITLE=1 forces gfx, =0 forces title.
        //   (Directory enumeration lands with the real save-slot system —
        //   raylib's bundled dirent.h shadows the system header, so keep
        //   this to plain stdio for now.)
        gX_I32 startScene = gX_SCENE_TITLE;
        {
            FILE *probe = fopen("resources/settings.json", "rb");
            gX_I1 returning = (probe != NULL);
            if (probe) fclose(probe);
            if (!returning) {
                probe = fopen(GX_SAVE_PATH, "rb");
                if (probe) { fclose(probe); returning = true; }
            }
            if (!returning) startScene = gX_SCENE_RENDERER_GFX;
            const gX_I8 *skip = getenv("GX_SKIPTITLE");
            if (skip && skip[0]) startScene = atoi(skip) ? gX_SCENE_RENDERER_GFX : gX_SCENE_TITLE;
        }
        errors = gX_MainData_Switch(startScene);
    }
#else
    //opencode: kernel-only — no scene switch; masterIndex stays NULL
    //   (maindata zero-init). CLI --scene still honored above.
    else {
        (void)errors;
    }
#endif

    return errors;
}

static gX_I32 gX_Backend_Init(gX_I0) {
    gX_I32 errors = 0;
    
    if (maindata.backendUse) {
        //opencode: audit window isolation — unfocused at creation (raylib maps
        //   FLAG_WINDOW_UNFOCUSED to glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE)),
        //   then parked off-screen after the monitor fit below. GPU audits can
        //   share a live desktop session: no focus steal, no pointer grab, and
        //   presenting continues (unlike --minimized, whose iconified
        //   backbuffer starves the capture readback).
        //   GX_AUDIT_WINDOW=hidden creates the window unmapped instead: nothing
        //   appears on the desktop at all (Wayland compositors ignore absolute
        //   positioning, so off-screen parking is best-effort there).
        gX_I1 auditWindow = 0;
        gX_I1 auditWindowHidden = 0;
        {
            const gX_I8 *hook = getenv("GX_AUDIT_WINDOW");
            if (hook && hook[0] == '1') auditWindow = 1;
            if (hook && strcmp((const char *)hook, "hidden") == 0) {
                auditWindow = 1;
                auditWindowHidden = 1;
            }
        }
        if (auditWindow)
            SetConfigFlags(auditWindowHidden ? FLAG_WINDOW_HIDDEN
                                             : FLAG_WINDOW_UNFOCUSED);
        gX_Backend_Load(maindata.windowWidth,maindata.windowHeight,maindata.name);
        //opencode: benchmark-only uncapped presentation; simulation timing is
        //   still fixed by GX_FIXED_DT, so measured frames are repeatable.
        if (getenv("GX_BENCHMARK_UNCAPPED")) gX_TargetFPS_Set(0.0f);
        else gX_TargetFPS_Set(maindata.targetFPS);
        gX_ExitKey_Set(input.exitKey);
        maindata.backendLoaded = true;
        //opencode: v0.6.6 — clamp window to the monitor (the 1536x1152
        //   default overflows smaller screens). Post-window so the monitor
        //   query is valid; skip when fullscreen. Shrinks window+render
        //   together, centers, marks settings dirty so the fit persists.
        if (!gX_Window_IsFullScreen()) {
            gX_I32 mon = gX_Monitor_GetCurrent();
            gX_I32 monW = gX_Monitor_GetWidth(mon);
            gX_I32 monH = gX_Monitor_GetHeight(mon);
            gX_I32 maxW = monW - 32, maxH = monH - 64;
            if (monW > 0 && monH > 0
                && ((gX_I32)maindata.windowWidth > maxW || (gX_I32)maindata.windowHeight > maxH)) {
                gX_U32 fitW = maindata.windowWidth, fitH = maindata.windowHeight;
                if ((gX_I32)fitW > maxW) fitW = (gX_U32)maxW;
                if ((gX_I32)fitH > maxH) fitH = (gX_U32)maxH;
                gX_Window_SetSize(fitW, fitH);
                gX_Window_SetPosition((monW - (gX_I32)fitW) / 2, (monH - (gX_I32)fitH) / 2);
                maindata.windowWidth = fitW;
                maindata.windowHeight = fitH;
                maindata.screenWidth = fitW;
                maindata.screenHeight = fitH;
                gX_MainData_Refresh();
                settings.dirtyFlag = true;
                gX_TraceLog(gX_LOG_INFO, "MAIN: window %ux%u exceeds monitor %dx%d — fitted to %ux%u",
                    maindata.windowWidth, maindata.windowHeight, monW, monH, fitW, fitH);
            }
        }
        //opencode: --minimized audit captures — iconify post-fit so the
        //   capture window never steals focus.
        //   LIMITATION (proven 2026-09-17, then reverted): --ai-screenshot*
        //   reads the BACKBUFFER (LoadImageFromScreen, wrappers.c), which an
        //   iconified window never presents — minimized captures came out
        //   empty. An FBO-readback canvas path was built and produced correct
        //   pixels, but iconified runs also stall ~2.6s/frame in present
        //   (157s vs 17s for 60 frames, identical user time) AND fast runs
        //   would diverge from visible baselines wherever wall-clock
        //   adaptation exists (AE rates, cloud drift). So --minimized stays
        //   interactive-only; audit scripts must NOT pass it. GX_AUDIT_WINDOW=1
        //   is the supported isolation hook instead: unfocused at creation,
        //   parked off-screen here, and input.c skips the pointer grab.
        if (arguments.minimized) {
            gX_Window_Minimize();
            gX_TraceLog(gX_LOG_INFO, "MAIN: window minimized (--minimized)");
        }
        if (auditWindow) {
            //opencode: park after the monitor fit so the clamp cannot re-center
            //   the audit window over the user's desktop. Hidden windows are
            //   never mapped, so positioning is unnecessary.
            if (!auditWindowHidden) gX_Window_SetPosition(-32000, -32000);
            gX_TraceLog(gX_LOG_INFO,
                "MAIN: audit window %s (GX_AUDIT_WINDOW)",
                auditWindowHidden ? "created hidden" : "parked off-screen");
        }
    }
    if (maindata.backendAudioUse) {
        gX_BackendAudio_Load();
        gX_MasterVolume_Set(maindata.masterVolume);
        maindata.backendAudioLoaded = true;
    }
    
    return errors;
}

gX_I32 gX_Load(gX_I0);
//opencode: v0.3.8 - guest asset roots: register external search dirs at
//   runtime (GX_ASSETS env, colon-separated; --assets flags already parsed
//   into arguments). Guests go AFTER gX_Path_Init() so they never enter the
//   shader include-path chain, and AFTER the default dirs so they are
//   fallback-only: gX's own assets always win, engine shaders cannot be
//   shadowed by guest files. Nothing is committed to the repo - the paths
//   live only in the caller's environment.
static gX_I0 gX_RegisterGuestAssets(gX_I0)
{
    gX_Path_Init();   // lazy-safe; also seeds the default search dirs

    //opencode: env form of --guest-model (same "subpath[:count]" format)
    if (!arguments.guestModel[0]) {
        const gX_I8 *gm = getenv("GX_GUEST_MODEL");
        if (gm && gm[0]) {
            gX_I8 buf[256];
            gX_I32 n = snprintf(buf, sizeof(buf), "%s", gm);
            if (n > 0 && (gX_U32)n < sizeof(buf)) {
                char *colon = strrchr(buf, ':');
                arguments.guestCount = 1;
                if (colon && colon != buf) {
                    arguments.guestCount = atoi(colon + 1);
                    if (arguments.guestCount < 1) arguments.guestCount = 1;
                    if (arguments.guestCount > 64) arguments.guestCount = 64;
                    *colon = 0;
                }
                snprintf(arguments.guestModel, sizeof(arguments.guestModel), "%s", buf);
            }
        }
    }

    gX_I8 roots[8][256];
    gX_I32 count = 0;
    const gX_I8 *env = getenv("GX_ASSETS");
    if (env && env[0]) {
        gX_I8 buf[1024];
        gX_I32 n = snprintf(buf, sizeof(buf), "%s", env);
        if (n > 0 && (gX_U32)n < sizeof(buf)) {
            gX_I8 *save = NULL;
            for (gX_I8 *tok = strtok_r(buf, ":", &save);
                 tok && count < 8;
                 tok = strtok_r(NULL, ":", &save)) {
                if (!tok[0]) continue;
                snprintf(roots[count], sizeof(roots[0]), "%s", tok);
                count++;
            }
        }
    }
    for (gX_I32 i = 0; i < arguments.guestAssetCount && count < 8; i++) {
        snprintf(roots[count], sizeof(roots[0]), "%s", arguments.guestAssets[i]);
        count++;
    }
    for (gX_I32 i = 0; i < count; i++) {
        struct stat st;
        if (stat(roots[i], &st) != 0 || !S_ISDIR(st.st_mode)) {
            gX_TraceLog_MAIN(gX_LOG_WARNING, "GUEST: root '%s' missing, skipping", roots[i]);
            continue;
        }
        //opencode: normalize to a trailing slash — the path system
        //   concatenates dir + alias + subpath directly
        gX_I8 norm[256];
        gX_I32 nn = snprintf(norm, sizeof(norm), "%s", roots[i]);
        if (nn > 0 && norm[nn - 1] != '/' && (gX_U32)(nn + 1) < sizeof(norm)) {
            norm[nn] = '/';
            norm[nn + 1] = 0;
        }
        gX_Path_AddSearchDir(norm);
        gX_TraceLog_MAIN(gX_LOG_INFO, "GUEST: registered asset root '%s'", norm);
    }
}

gX_I32 gX_Load(gX_I0) {
    gX_I32 errors = 0;
    
    gX_TraceLog_MAIN(gX_LOG_INFO,"Loading stuff");
    
    errors = gX_MainData_Init();
    gX_RegisterGuestAssets();   //opencode: v0.3.8 - before any resource load
    //opencode: run header - dump every GX_* env var the run saw (hooks apply
    //   silently across 46+ sites; this one line shows the effective input)
    //   plus the scene/preset selection. First thing a hunt log needs.
    {
        extern char **environ;
        gX_I8 line[1024];
        line[0] = 0;
        for (char **e = environ; e && *e; e++) {
            if (strncmp(*e, "GX_", 3) != 0) continue;
            if (line[0]) strncat((char *)line, " ", sizeof(line) - strlen((char *)line) - 1);
            strncat((char *)line, *e, sizeof(line) - strlen((char *)line) - 1);
        }
        gX_TraceLog_MAIN(gX_LOG_NOTICE, "ENV:%s%s", line[0] ? " " : " (no GX_* vars)", line);
        gX_TraceLog_MAIN(gX_LOG_NOTICE, "RUN: showcase=%d reflectrow=%d ledrow=%d forest=%d lake=%d preset=%d debugview=%d",
            (gX_I32)arguments.showcase, (gX_I32)arguments.showcaseReflectRow,
            (gX_I32)arguments.showcaseLedRow, (gX_I32)arguments.showcaseForest,
            (gX_I32)arguments.showcaseLake,
            (gX_I32)arguments.preset, (gX_I32)arguments.debugView);
    }
    gX_Settings_Init(); //opencode: gX — settings lifecycle (theme/audio/window prefs)
    gX_Settings_Load(); // silent fail if no save file
    errors = gX_GameData_Load();
    errors = gX_Input_Load();
    errors = gX_Camera_Init();
    errors = gX_Random_Init();
    //opencode: gX — backend (GL context) must init before any GPU resource
    //   creation (renderer/gui create textures, shaders, FBOs at load)
    errors = gX_Backend_Init();
    //opencode: native desktop GL 3.x requires its legacy-GLSL preprocessor
    // dialect (130/140/150) before batch, GUI, resource, and renderer
    // shader loading.
    {
        const gX_RHI_Caps *caps = gX_RHI_GetCaps();
        gX_I32 legacy = gX_RHI_Shader_LegacyGLSL();
        gX_Shader_Compiler_SetDesktopLegacyGLSL(legacy);
        if (legacy) {
            gX_TraceLog_MAIN(gX_LOG_NOTICE, "GLSL%d: desktop compatibility dialect enabled (%d.%d, GLSL %d)%s",
                legacy,
                caps ? caps->glMajor : 0, caps ? caps->glMinor : 0,
                caps ? caps->shaderModel : 0,
                getenv("GX_GLSL130") ? " [host probe]" : "");
        }
    }
    //ai: hardware report — --hw-report or GX_HW_REPORT=1 (gate-friendly).
    {
        const char *hwEnv = getenv("GX_HW_REPORT");
        if (arguments.hasHwReport || (hwEnv && hwEnv[0] == '1'))
            gX_Hardware_Report();
    }
    //opencode: enable opt-in boundary diagnostics only after the GL context
    //   exists, before resource and scene loading starts.
    if (arguments.hasGlDiagnostics) {
        gX_RHI_Diagnostics_SetEnabled(1);
        if (arguments.hasGlDiagnosticsVerbose) gX_RHI_StageDump_SetEnabled(1);
    }
    //opencode: batch needs a live GL context — its internal shader/VAO/VBO/IBO
    //   are created here (must run after gX_Backend_Init, before any batch draws)
    extern gX_I0 gX_Batch_Init(gX_I0);
    if (maindata.backendLoaded) gX_Batch_Init();
    errors = gX_ResourceManager_Load();
#if gX_ENABLE_GUI == 1
    //opencode: v0.4.0 — mark the startup scene switch so the title entrance
    //   plays a short fade-from-black instead of the skew-slide swing (the
    //   INITIAL_LOAD gate in respond.c). Preserved across gX_GUI_Load's
    //   state reset (gui.c saves/restores it).
    gui.state.INITIAL_LOAD = true;
    errors = gX_GUI_Load();
#endif
#if gX_ENABLE_RENDERER == 1
    errors = gX_Renderer_Load();
#endif
#if gX_ENABLE_PHYSICS == 1
    //ai: shared physics world — before the scene loads so scene-owned
    //   bodies can spawn into it during scene load.
    errors = gX_Physics_Init();
#endif
#if gX_ENABLE_SCENE == 1
    errors = gX_Scene_Load();
#endif
    errors = gX_Audio_Load();

    //opencode: dynamic modules load after audio and before the tools/markup
    //   builds so module-provided markup handlers are in place (gx_shell
    //   loads the scripting engine + console here). Explicit --module/GX_MODULE
    //   failures fail the boot; modules/ auto-scan misses only warn.
    errors += gX_Module_LoadConfigured();

    //opencode: shell args without a module are warned about (the args stay in
    //   the default CLI so one binary serves both profiles).
    if (!gX_Module_AnyLoaded()) {
        if (arguments.hasAsScript) {
            gX_TraceLog(gX_LOG_WARNING,
                "MAIN: --script needs a module (gx_shell.so) — arg ignored");
        }
        if (arguments.console) {
            gX_TraceLog(gX_LOG_WARNING,
                "MAIN: --console needs a module (gx_shell.so) — arg ignored");
        }
    }

    //ai: headless markup hook — GX_GUI_MARKUP=<path> registers the file as
    //   the markup template and builds it on the audit canvas (2) without
    //   opening the editor, so scripts can query and click it (DOM +
    //   virtual input). --tools opens the GUI editor instead.
    {
        const gX_I8 *markupPath = getenv("GX_GUI_MARKUP");
        if (markupPath && markupPath[0]) {
            gX_GUI_Template_RegisterMarkup(markupPath);
            gui.auditTemplateID = GX_GUI_TEMPLATE_MARKUP;
            gX_GUI_AddMake(GX_GUI_TEMPLATE_MARKUP, 2);
            gX_TraceLog(gX_LOG_NOTICE, "GUI: markup '%s' built on canvas 2 (GX_GUI_MARKUP)", markupPath);
        }
    }

    //opencode: v0.4.0 — gX Tools suite: --tools opens the GUI editor on the
    //   GUIPLAYGROUND scene with a markup template (registered for live
    //   hot-reload) + the ImGui runtime for the editor chrome.
    //opencode: --gfx-editor opens the GFX editor over the live scene
    //   (renderer-gated inside; kernel-only shows a hint panel).
    if (arguments.hasGfxEditor) {
        extern gX_I0 gX_Tools_OpenGfx_Runtime(gX_I0);
        gX_Tools_OpenGfx_Runtime();
    }
    //opencode: --wavetable-editor opens the synth wavetable editor (usable
    //   over any scene; the panel edits engine synth state).
    if (arguments.hasWavetableEditor) {
        extern gX_I0 gX_Tools_OpenWavetable_Runtime(gX_I0);
        gX_Tools_OpenWavetable_Runtime();
    }
    //opencode: --textwriter-editor opens the textwriter editor (the CLI
    //   parse routes to the textwriter scene unless overridden).
    if (arguments.hasTextwriterEditor) {
        extern gX_I0 gX_Tools_OpenTextwriter_Runtime(gX_I0);
        gX_Tools_OpenTextwriter_Runtime();
    }
    //opencode: --rail-editor opens the camera rail editor (module panel; the
    //   opener warns when the gx_shell module is missing).
    if (arguments.hasRailEditor) {
        extern gX_I0 gX_Tools_OpenRail_Runtime(gX_I0);
        gX_Tools_OpenRail_Runtime();
    }
    //opencode: --background-editor opens the background editor (the opener
    //   routes to the --background scene).
    if (arguments.hasBackgroundEditor) {
        extern gX_I0 gX_Tools_OpenBackground_Runtime(gX_I0);
        gX_Tools_OpenBackground_Runtime();
    }
    //opencode: --sky-editor opens the sky editor (the opener routes to the
    //   --sky scene).
    if (arguments.hasSkyEditor) {
        extern gX_I0 gX_Tools_OpenSky_Runtime(gX_I0);
        gX_Tools_OpenSky_Runtime();
    }
    //ai: --physics-editor opens the physics sandbox tuning panel (the CLI
    //   parse routes to the physics scene unless overridden).
    if (arguments.hasPhysicsEditor) {
        extern gX_I0 gX_Tools_OpenPhysics_Runtime(gX_I0);
        gX_Tools_OpenPhysics_Runtime();
    }
    //ai: scripting debugger panel — --script-editor (needs the gx_shell
    //   module; the opener logs a hint without it).
    if (arguments.scriptEditor) {
        extern gX_I0 gX_Tools_OpenScript_Runtime(gX_I0);
        gX_Tools_OpenScript_Runtime();
    }
    if (maindata.masterIndex == gX_SCENE_GUIPLAYGROUND && arguments.hasTools) {
        extern gX_I0 gX_Tools_Init(gX_I0);
        gX_Tools_Init();
    }

    //opencode: v0.4.0 — gX Tools suite: --theme-editor opens the theme
    //   editor on the current scene (title by default); the theme path was
    //   activated pre-load in parse, so the scene renders with it at once
    if (arguments.hasThemeEditor) {
        extern gX_I0 gX_Tools_Theme_Init(gX_I0);
        gX_Tools_Theme_Init();
    }

    //opencode: v0.6.2 — --themlab — live design preview. Runs after the
    //   theme editor chrome so the editor window sits over the preview.
    if (arguments.themlab) {
        extern gX_I0 gX_Tools_ThemLab_Init(gX_I0);
        gX_Tools_ThemLab_Init();
    }

    //opencode: v0.6.5 — GX_TOOLS_OPEN=<editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics> —
    //   headless/automation hook for the in-app (EXTRAS) tool openers. Same
    //   runtime API the buttons call, so captures verify the real path.
    {
        const gX_I8 *openEnv = getenv("GX_TOOLS_OPEN");
        if (openEnv && openEnv[0]) {
            extern gX_I0 gX_Tools_OpenEditor_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenTheme_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenThemLab_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenMaterials_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenGfx_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenScript_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenWavetable_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenTextwriter_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenPhysics_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenRail_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenBackground_Runtime(gX_I0);
            extern gX_I0 gX_Tools_OpenSky_Runtime(gX_I0);
            if (!strcmp(openEnv, "editor")) gX_Tools_OpenEditor_Runtime();
            else if (!strcmp(openEnv, "theme")) gX_Tools_OpenTheme_Runtime();
            else if (!strcmp(openEnv, "themlab")) gX_Tools_OpenThemLab_Runtime();
            else if (!strcmp(openEnv, "materials")) gX_Tools_OpenMaterials_Runtime();
            else if (!strcmp(openEnv, "gfx")) gX_Tools_OpenGfx_Runtime();
            else if (!strcmp(openEnv, "script")) gX_Tools_OpenScript_Runtime();
            else if (!strcmp(openEnv, "wavetable")) gX_Tools_OpenWavetable_Runtime();
            else if (!strcmp(openEnv, "textwriter")) gX_Tools_OpenTextwriter_Runtime();
            else if (!strcmp(openEnv, "physics")) gX_Tools_OpenPhysics_Runtime();
            else if (!strcmp(openEnv, "rail")) gX_Tools_OpenRail_Runtime();
            else if (!strcmp(openEnv, "background")) gX_Tools_OpenBackground_Runtime();
            else if (!strcmp(openEnv, "sky")) gX_Tools_OpenSky_Runtime();
            else gX_TraceLog(gX_LOG_WARNING, "MAIN: GX_TOOLS_OPEN expects editor|theme|themlab|materials|gfx|script|wavetable|textwriter|physics|rail|background|sky — got '%s'", openEnv);
        }
    }
    
    gX_TraceLog_MAIN(gX_LOG_INFO,"Loading done");
    
    if (errors > 0) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: errors: %i",errors);
    }
    
    return errors;
}
