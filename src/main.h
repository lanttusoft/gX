/**********************************************************************************************
*
*   [gX: src/main.h]
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


#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"
#include "common/config.h" //opencode: gX_ENABLE_* module flags

//opencode: max runtime GFX overrides (--gfx-set key=value)
#define GX_GFX_OVERRIDE_MAX 32

//opencode: GFX effect identifiers for --gfx-enable/disable
enum {
    GX_EFFECT_GODRAY = 0,
    GX_EFFECT_BLOOM,
    GX_EFFECT_SSR,
    GX_EFFECT_DOF,
    GX_EFFECT_SSAO,
    GX_EFFECT_TAA,
    GX_EFFECT_MOTION_BLUR,
    GX_EFFECT_LENS_FLARE,
    GX_EFFECT_SHADOWS,
    GX_EFFECT_POINT_SHADOWS,
    GX_EFFECT_FOG,
    GX_EFFECT_WATER,
    GX_EFFECT_PARTICLES,
    GX_EFFECT_COOKIE,
    GX_EFFECT_INSTANCING,
    GX_EFFECT_OCCLUSION,
    GX_EFFECT_COUNT
};

typedef struct gX_MainArguments {
    
    gX_I1   startPoint;
    gX_I1   showHelp;
    
    gX_I1   showcase;       //opencode: --showcase — interactive GFX demo (orbit cam + effect toggles)
    gX_I1   showcaseWater;  //opencode: --showcase-water — showcase with the water surface enabled (floating-island layout)
    gX_I1   showcaseMinimal;    //opencode: v0.1.24 — --showcase-minimal — reflection verification scene: near-mirror plane + one floating cube
    gX_I1   showcasePlanar;     //opencode: v0.1.24 — --showcase-planar — minimal scene with the plane using the exact planar-reflection path (SSR oracle)
    gX_I1   showcaseReflectRow; //opencode: v0.3.4 --reflectrow reflection testbed: plane + material-sweep ball row
    gX_I1   showcaseLedRow;     //opencode: --ledrow night LED testbed: moonlit slab + PBR centerpiece + LED ring + animated rig
    gX_I1   showcaseY2K;        //opencode: --showcase-y2k — opt-in Bryce/Y2K abstract terrain style study
    gX_I1   showcaseForest;     //opencode: --forest dead-forest testbed: ~36 guest trees (guest fallback) + thick fog
    gX_I1   showcaseLake;       //opencode: --lake forest-ringed lake testbed: water plane + banks + tree ring + thin fog
    gX_I1   layoutTest;   //opencode: --layout-test — deliberately broken scene for the auto-seat pass
    gX_I1   layoutCheck;  //opencode: --layout-check — headless layout verify (implies layout-test, exits after verdict)
    //opencode: v0.3.8 — guest assets: runtime-registered external resource
    //   roots (GX_ASSETS env / --assets flag, fallback-only priority) and a
    //   model injection hook (--guest-model subpath[:count], additive ring
    //   in the showcase) for gfx perf testing against foreign content
    gX_I8   guestAssets[8][256];    // external search roots
    gX_I32  guestAssetCount;
    gX_I8   guestModel[256];        // subpath under the resource type dir ("" = none)
    gX_I32  guestCount;             // instances to spawn (default 1)
    gX_I1   fullScreen;
    gX_I1   minimized;      //opencode: --minimized — start window minimized/out of focus
    gX_I1   noGlDebug;      //opencode: --no-gl-debug — skip GL debug callback (headless runs)
    gX_I32  monitor;
    
    gX_I1   hasLogLevel;
    gX_I1   hasConfigFlags;
    gX_I1   hasWindowWidth;
    gX_I1   hasWindowHeight;
    gX_I1   hasScreenWidth;
    gX_I1   hasScreenHeight;
    gX_I1   hasScreenRes;
    gX_I1   hasRenderScale;
    gX_I1   hasTargetFPS;
    gX_I1   hasMasterIndex;
    gX_I1   hasMasterVolume;
    gX_I1   hasTestOnly;
    gX_I1   hasEsDumpShader;    //opencode: --es-dump-shader <in.frag> <out> —
                                //   ES-rewrite frag dump for oracle Mode B
    gX_I8   esDumpPath[512];
    gX_I8   esDumpOutPath[512];
    gX_I1   hasVkDumpShader;     //opencode: --vk-dump-shader <in> <out> [--vk-dump-vert] —
                                 //   Vulkan-dialect dump for glslangValidator verification
    gX_I8   vkDumpPath[512];
    gX_I8   vkDumpOutPath[512];
    gX_I1   vkDumpIsVertex;
    gX_I1   gfxSafeMode;        //opencode: --gfx-safe-mode — disable all effects for debugging
    gX_I1   hasGlDiagnostics;
    gX_I1   hasGlDiagnosticsVerbose;
    
    gX_I32  logLevel;
    gX_U32  configFlags;
    gX_U32  windowWidth;
    gX_U32  windowHeight;
    gX_U32  screenWidth;
    gX_U32  screenHeight;
    gX_F32v2 screenRes;
    gX_F32  renderScale;
    gX_F32  targetFPS;
    gX_I32  masterIndex;
    //opencode: --theme <path> / GX_THEME — aesthetic preset file override
    //   (picked up before gX_GUI_Load via gX_GUI_Style_SetThemePath)
    gX_TextLine128 themePath;
    gX_I1   hasThemePath;
    //opencode: v0.4.0 — gX Tools suite: --tools <markup> opens the GUI
    //   editor on the GUIPLAYGROUND scene with a markup template loaded
    gX_TextLine256 toolsMarkupPath;
    gX_I1   hasTools;
    //opencode: v0.4.0 — gX Tools suite: --theme-editor <path> opens the
    //   theme editor on the current scene with that theme active
    gX_TextLine256 themeEditorPath;
    gX_I1   hasThemeEditor;
    //opencode: v0.6.2 — --themlab — live design preview: THEMLAB canvas
    //   template on canvas 2 over the title scene + the ImGui theme editor
    gX_I1   themlab;
    //opencode: --gfx-editor — GFX editor over the live scene (no routing)
    gX_I1   hasGfxEditor;
    //opencode: --wavetable-editor — synth wavetable editor (no routing)
    gX_I1   hasWavetableEditor;
    //ai: --physics-editor — sandbox/applier tuning (routes to the scene)
    gX_I1   hasPhysicsEditor;
    //opencode: --rail-editor — module-drawn camera rail editor (no routing)
    gX_I1   hasRailEditor;
    //opencode: --background-editor — background authoring (routes to the scene)
    gX_I1   hasBackgroundEditor;
    //opencode: --sky-editor — sky look + ephemeris clock (routes to the scene)
    gX_I1   hasSkyEditor;
    //opencode: --textwriter-editor — textwriter editor (routes to the scene)
    gX_I1   hasTextwriterEditor;
    gX_F32  masterVolume;

    //opencode: --ai-screenshot-final / --ai-screenshot-after N —
    //   headless capture: take a screenshot and exit after N frames
    //   (default 180), for vision verification + parity audits
    gX_I1   aiScreenshotFinal;
    gX_I1   hasAiScreenshotAfter;
    gX_U32  aiScreenshotAfter;
    gX_I1   hasDebugView;       //opencode: --debug-view <0-4> — preset showcase raw-RT view
    gX_U32  debugView;
    gX_I1   hasSsrDebug;        //opencode: --ssr-debug <0-27> — preset SSR shader debug mode
    gX_I32  ssrDebug;
    gX_I1   hasPreset;          //opencode: --preset <0-3> — quality preset override
    gX_I32  preset;
    
    gX_I1   disableModuleAudio;
    gX_I1   disableModuleGUI;
    
    gX_I1   hasTimeout;
    gX_F64  timeout;   // Auto-exit after this many seconds (testing)
    gX_U32  seed;      // --seed N — fixed RNG seed for deterministic runs (0 = time-based)
    gX_I1   hasSeed;
    //opencode: null/headless build + CI — --headless disables the backend,
    //   --frames N auto-exits after N loop frames (headless has no wall-clock
    //   advance so --timeout can never fire there).
    gX_I1   headless;
    gX_I1   hasFrameLimit;
    gX_U32  frameLimit;

    gX_I1   guiTest;

    //opencode: --gfx-harness renders the GFX scene and outputs pixel samples to stdout
    gX_I1   gfxHarness;

    //opencode: --gui-harness reads pixel samples from all canvas buffers and the
    //   final composited screen, outputting SAMPLE lines for debugging GUI layout.
    gX_I1   guiHarness;
    gX_TextLine256 gfxHarnessTech;
    gX_TextLine64 gfxHarnessScene;
    //opencode: --gfx-harness-every=N output samples every N frames (no exit)
    gX_U32  gfxHarnessEvery;
    gX_U32  gfxHarnessFrameTime;  //opencode: --gfx-harness-frametime N — log frame time stats for N frames
    gX_I1   gfxHarnessApplied; //opencode: track if ApplyTech has run
    //opencode: --gfx-harness-camera="posX,posY,posZ,tarX,tarY,tarZ"
    gX_TextLine128 gfxHarnessCamera;
    gX_I1          gfxCameraLocked;      //opencode: lock orbit camera, respect --ai-set-camera
    gX_I1          gfxHarnessGodray;     //opencode: god ray harness — camera, sun, and scene setup
    gX_TextLine64  gfxHarnessEffect;     //opencode: per-effect harness name (bloom, ssr, dof, ssao, taa, godray)
    //opencode: --world-loop-fast — accelerate world day/night for loop testing
    gX_I1   worldLoopFast;
    //opencode: --gfx-profile[=path.csv] — per-frame GPU timing CSV output
    gX_I1           gfxProfile;
    gX_TextLine256  gfxProfilePath;
    //ai: raymarch per-frame GPU timing CSV (--raymarch-profile[=path.csv])
    gX_I1           raymarchProfile;
    gX_TextLine256  raymarchProfilePath;
    //ai: hardware capability report (--hw-report; GX_HW_REPORT=1 env)
    gX_I1           hasHwReport;
    gX_TextLine128 gfxPreset;            //opencode: --gfx-preset <name> — load scene preset from resources/presets/scene/<name>.json
    gX_U32         gfxEnableMask;        //opencode: bitmask of GX_EFFECT_* to force-enable
    gX_U32         gfxDisableMask;       //opencode: bitmask of GX_EFFECT_* to force-disable
    //opencode: --gfx-harness-orbit=deg_per_frame (auto-rotate camera in periodic mode)
    gX_F32  gfxHarnessOrbit;
    //opencode: --gfx-harness-fbo "name:u,v" — sample a named FBO at UV coordinate
    //   Supported names: hdrScene, bloomScene, extract, ping, pong, canvas, screen,
    //   ssr, ssao, reflection, godray, fogVolume, motionBlur, dofCoc, dofBlur
    //   Example: --gfx-harness-fbo "hdrScene:0.5,0.5" --gfx-harness-fbo "canvas:0.25,0.75"
    gX_TextLine128 gfxHarnessFbo[4]; // up to 4 simultaneous FBO samples
    gX_U32         gfxHarnessFboCount;

    //opencode: --gfx-harness-screen samples the final backbuffer after compositing
    //   (what the user actually sees on screen, unlike SAMPLE which reads renderer.canvas)
    gX_I1   gfxHarnessScreen;

    //opencode: --harness-persistent keeps the program running after harness capture
    //   instead of exiting. Combined with --gfx-harness-every=N, captures output
    //   every N frames. Exit on SIGINT/SIGTERM or 'q' key.
    gX_I1   harnessPersistent;

    //opencode: --harness-derivative "param=value" modifies a parameter before capture.
    //   Supported: camera.posX/Y/Z, camera.tarX/Y/Z, camera.fovy,
    //   gfx.ENABLE_SSR/BLOOM/SSAO/FOG, bloom.intensity/exposure/threshold,
    //   ssr.maxSteps/stepSize/maxDistance.
    gX_TextLine256 harnessDerivative;

    //opencode: dynamic module host (docs/dynamic_modules.md) — the shell args
    //   stay in the default build (inert without a module) and --module loads
    //   a .so; GX_MODULE / modules/ auto-scan are the fallbacks.
    gX_TextLine256 asScriptPath;
    gX_I1   hasAsScript;
    //ai: --script-editor — open the scripting debugger panel at load.
    gX_I1   scriptEditor;
    //ai: --console — open the developer console at load.
    gX_I1   console;
    gX_TextLine256 modulePath;
    gX_I1   hasModule;
    //opencode: save/load CLI flags — game-save feature, NOT scripting. Moved
    //   out of the GX_USE_ANGELSCRIPT guard so they exist in kernel-only builds
    //   (scripting disabled) and the save-autoload logic in main_load.c compiles.
    gX_I1   doSave;
    gX_I1   doLoad;

    gX_TextLine256 demoScriptPath;
    gX_I1   hasDemoScript;
    gX_F32  demoSpeed;
    gX_I1   hasDemoSpeed;
    gX_I32  demoSection;
    gX_I1   hasDemoSection;

    //opencode: video player file path override
    gX_TextLine256 videoFilePath;
    gX_I1   demoCapture;
    
    gX_TextLine256 resourceBlacklistPath;
    gX_I1   hasResourceBlacklist;
    //opencode: --layout-dump <path> — write a structured JSON layout dump
    //   (element name→rect per canvas) once at a stable frame
    gX_TextLine256 layoutDumpPath;
    gX_I1   hasLayoutDump;
    gX_I32  layoutDumpFrame;
    gX_I1   noTextures;
    gX_I1   noShaders;
    gX_I1   noModels;
    gX_I1   noFonts;
    gX_I1   noAudio;
    gX_I1   noScripts;
    
    gX_I1   battleHarness;     // --battle-harness
    gX_I1   hasAutoplay;
    gX_I32  difficulty;           // 0=easy, 1=normal, 2=hard (from --difficulty)
    gX_I32  battleBg;             //opencode: --battle-bg N (backdrop override, -1 = procedural)
    gX_I1   battleBgSet;          //opencode: true if --battle-bg was passed

    //opencode: --bg-imgpro <path> runs ImageProcessor pipeline on startup
    gX_I1   hasBgImgPro;
    gX_TextLine256 bgImgProPath;

    //opencode: --bg-harness dumps background renderer state and samples
    gX_I1   bgHarness;
    
    //opencode: --fractal-harness dumps fractal renderer state and samples
    gX_I1   fractalHarness;
    
    //opencode: --bg-preset <path> loads a background preset at startup
    gX_I1   hasBgPreset;
    gX_TextLine256 bgPresetPath;

    //opencode: --bg-palette-mode <n> overrides palette mode (for testing mode 7)
    gX_I1   hasBgPaletteMode;
    gX_I32  bgPaletteMode;

    //opencode: --bg-react <type> triggers a one-shot background reaction
    gX_I1   hasBgReact;
    gX_I32  bgReactType;

    //opencode: --bg-transition <preset> morphs from current bg to the preset
    gX_I1   hasBgTransition;
    gX_TextLine256 bgTransitionPath;
    gX_F32  bgTransitionDuration;

    //opencode: --bg-video <path> uses a video file as the bg texture source
    gX_I1   hasBgVideo;
    gX_TextLine256 bgVideoPath;

    //opencode: --fractal-preset <name> loads a fractal preset at startup
    gX_I1   hasFractalPreset;
    gX_TextLine256 fractalPresetPath;

    //opencode: --analog-mode <ntsc|legacy|vhs> sets analog renderer mode at startup
    gX_I1   hasAnalogMode;
    gX_I32  analogMode;

    //opencode: runtime GFX override table — key=value pairs applied at scene load
    //   CLI: --gfx-set "godray.weight=0.5" --gfx-set "godray.decay=0.9"
    gX_U32  gfxOverrideCount;
    gX_I8   gfxOverrideKeys[GX_GFX_OVERRIDE_MAX][64];
    gX_F64  gfxOverrideValues[GX_GFX_OVERRIDE_MAX];

    //opencode: --capture-sequence <dir> — capture N frames as numbered PPMs for GIF/MP4 encoding
    gX_I1   captureSequence;
    gX_TextLine256 captureSequenceDir;
    gX_U32  captureSequenceMax;
    gX_U32  captureSequenceRate;
    gX_U32  captureSequenceFrame;

    //opencode: --gfx-sweep-effects \"godray,ssr,dof,...\" — binary counter sweep through effect combos
    gX_I1   gfxSweepEnabled;
    gX_I32  gfxSweepEffects[16];     // resolved GX_EFFECT_* constants (-1 = end)
    gX_U32  gfxSweepCount;           // number of sweep effects
    gX_U32  gfxSweepCounter;         // current binary counter value
} gX_MainArguments;

extern gX_MainArguments arguments;

typedef struct gX_MainData_WindowInfo {
    gX_I1       toggleFullScreen;
    gX_I1       minimize;
    gX_I1       maximize;
    gX_I1       restore;
    
    gX_I1       ready;
    gX_I1       fullscreen;
    gX_I1       hidden;
    gX_I1       minimized;
    gX_I1       maximized;
    gX_I1       focused;
    gX_I1       resized;
    
    gX_I1       setState;
    gX_I1       clearState;
    gX_U32      state;
    
    gX_I1       programNameAsWindowTitle;
    gX_I8       title[128];
    
    gX_I32      iconsAmount;
    gX_Image   *icons;
    
    gX_F32v2    position;
    gX_I32      monitor;
    gX_F32v2    minSize;
    gX_F32      opacity;
} gX_MainData_WindowInfo;

typedef struct gX_MainData_MonitorInfo {
    gX_I32      count;
    gX_I32      current;
    gX_I32      width;
    gX_I32      height;
    gX_I32      widthPhysical;
    gX_I32      heightPhysical;
    gX_I32      refreshRate;
    gX_I8       name[128];
} gX_MainData_MonitorInfo;

//typedef struct gX_MainData_Flags;
//typedef struct gX_MainData_Debug;

typedef struct gX_MainData_ModuleInfo {
    gX_I1   arcadeExists;
    gX_I1   audioExists;
    gX_I1   cameraExists;
    gX_I1   gamedataExists;
    gX_I1   guiExists;
    gX_I1   gxmathExists;
    gX_I1   inputExists;
    gX_I1   profilerExists;
    gX_I1   randomExists;
    gX_I1   rendererExists;
    gX_I1   resourceExists;
    gX_I1   sceneExists;
    gX_I1   scriptingExists;
    gX_I1   settingsExists;
} gX_MainData_ModuleInfo;

typedef struct gX_MainData {
    gX_I8       name[128];
    gX_I8       description[256];
    gX_I32      errorcode;
    
    //general
    gX_I1       close;
    gX_I1       loop;
    gX_I1       backendUse;
    gX_I1       backendLoaded;
    gX_I1       backendAudioUse;
    gX_I1       backendAudioLoaded;
    gX_Color    bgColor;
    gX_F32      masterVolume;
    gX_U64      configFlags;
    
    //timing
    gX_F64      timeElapsed; //how long has the game been running
    gX_F32      targetFPS;
    gX_U64      frameCount; //opencode: total frames executed (headless-safe exit counter)
    
    gX_MainData_WindowInfo window;
    gX_MainData_MonitorInfo monitor;
    
    //window: absolute window resolution (actual window resolution)
    gX_U16      windowWidth; //65536
    gX_U16      windowHeight;
    //screen: relative window resolution (gui resolution [2D])
    gX_U16      screenWidth;
    gX_U16      screenHeight;
    //render: rendering resolution for a framebuffer (renderer resolution [2D/3D])
    gX_F32      renderScale;
    gX_U16      renderWidth;
    gX_U16      renderHeight;
    
    gX_F32v2    windowRes;
    gX_F32v2    screenRes;
    gX_F32v2    renderRes;
    
    gX_F32      windowResArray[2];
    gX_F32      screenResArray[2];
    gX_F32      renderResArray[2];
    
    gX_I32      resolution;
    gX_I32      bitdepth;
    
    //scenes
    gX_I32      masterIndex;
    gX_I1       masterIndexSwitch;      //do switching
    gX_I1       masterIndexSwitched;    //awaiting switch
    gX_I1       masterIndexSuccessful;  //switch complete
    
    //refresh
    gX_I1       refresh;
    gX_I1       refreshOngoing;
    gX_I1       refreshSuccessful;
    
    //exit confirmation
    gX_I1       exitRequested;

    //screenshot
    gX_I1       screenshot;
    
    gX_MainData_ModuleInfo moduleinfo;
    
} gX_MainData;

extern gX_MainData maindata;

gX_I32 gX_MainData_Init(gX_I0);
gX_I32 gX_MainData_Reset(gX_I0);
gX_I32 gX_MainData_Refresh(gX_I0);
gX_I32 gX_MainData_Update_Before(gX_I0);
gX_I32 gX_MainData_Update_After(gX_I0);

gX_I32 gX_MainData_Switch(gX_I32 masterIndex);

#ifdef __cplusplus
}
#endif

#endif //MAIN_H
