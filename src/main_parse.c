/**********************************************************************************************
*
*   [gX: src/main_parse.c]
*   CLI argument parsing
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

#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "common/enum.h"
#include "gui/style.h"
#include "common/log.h"

//opencode: help function + expanded option handlers
static gX_I0 gX_Parse_Help(const gX_I8 *name) {
    fprintf(stdout,
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -w, --screenWidth <px>     Window width\n"
        "  -h, --screenHeight <px>    Window height\n"
        "      --renderScale <0-1>    Render resolution scale\n"
        "      --targetFPS <n>        Target framerate\n"
        "      --seed <n>             Fixed random seed\n"
        "      --masterIndex <n>      Startup scene index\n"
        "      --fullScreen           Start in fullscreen\n"
        "      --monitor <n>          Monitor index\n"
        "      --logLevel <n>         Log level threshold\n"
        "      --configFlags <n>      Window config flags\n"
        "      --masterVolume <0-1>   Master volume\n"
        "      --preset <n>           Configuration preset\n"        "      --assets <dir>         Guest asset root, repeatable (fallback-only search dir)\n"
        "      --guest-model <p>[:n]  Spawn n instances of a guest model in the showcase\n"
        "      --disableAudio         Disable audio module\n"
        "      --disableGUI           Disable GUI module\n"
        "      --test-only            Run unit tests and exit (no window)\n"
        "      --ai-screenshot-final  Headless capture: screenshot + exit after\n"
        "                             180 frames (vision verification/parity audits)\n"
        "      --ai-screenshot-after <n>  Same, capture at frame n\n"
        "      --layout-dump <path>    Write structured JSON layout dump (element rects)\n"
        "      --layout-dump-frame N  Frame for the layout dump (default 8)\n"
        "      --minimized            Start iconified (interactive use; NOT for\n"
        "                             --ai-screenshot captures — those read\n"
        "                             the backbuffer, which iconified\n"
        "                             windows never present)\n"
        "      --debug-view <0-12>    Showcase: preset raw-RT view (1=ssao 2=ssaoBlur\n"
        "                             3=depth 4=ssr 5=hdrScene 6=LDR 7=bloom.scene\n"
        "                             8=planar 9=skyLUT 10=cookies 11=contact 12=velocity;\n"
        "                             D key cycles all; linearized depth via GX_T_DEPTHVIS)\n"
        "      --ledrow               Showcase: night LED testbed (moonlit slab, PBR\n"
        "                             centerpiece, LED ring, orbit/blink/hue rig;\n"
        "                             GX_T_LEDORBIT/BLINK/HUE electives)\n"
        "      --showcase-y2k         Showcase: opt-in Bryce/Y2K abstract terrain\n"
        "                             study (glass, metal, neon; live editor-ready)\n"
        "      --forest               Showcase: dead-forest testbed (~36 guest trees\n"
        "                             via GX_ASSETS guest fallback, thick fog;\n"
        "                             set GX_ASSETS=<external-assets>/resources)\n"
        "      --lake                 Showcase: forest-ringed lake (water plane,\n"
        "                             banks, ~26 guest trees, thin fog)\n"
        "      --help                 Show this help and exit\n"
        "      --textwriter           Jump to textwriter scene\n"
        "      --synthvis             Jump to synth visualizer scene (playable\n"
        "                             keyboard: Z X C V B N M , . / Q W E R +\n"
        "                             S D G H J 2 3 5 6 7; GX_SYNTHVIS_AUTOPLAY=1\n"
        "                             plays a demo arpeggio for captures)\n"
        "      --title                Jump to title scene\n"
        "      --gfx                  Jump to 3D renderer scene\n"
        "      --background           Fullscreen procedural background preset\n"
        "      --tools <markup>      gX Tools: GUI editor on markup template\n"
        "                              (shipped: resources/tools/tools_demo.json)\n"
        "      --theme-editor <path>   gX Tools: theme editor on the live scene\n"
        "                              (edits design tokens in place, save back to file)\n"
        "      --themlab               gX Tools: live design preview — THEMLAB widget\n"
        "                              gallery over the title scene + theme editor\n"
        "      --materialslab          gX Tools: SpecularCSS-inspired GUI material\n"
        "                              response gallery over the title scene\n"
        "      --shaderlab             gX Tools: translucency/lens/color-math GUI\n"
        "                              shader experiments over the title scene\n"
        "      --gfx-editor          gX Tools: live GFX editor (models/materials/\n"
        "                              reflections/lights/post effects)\n"
        "                              over the GFX scene (routes there; pair\n"
        "                              with --masterIndex N to edit elsewhere)\n"
        "      --wavetable-editor    gX Tools: synth wavetable editor (draw/\n"
        "                              generate, preview, raw .f32 load/save)\n"
        "      --textwriter-editor   gX Tools: textwriter editor (lines/tags/\n"
        "                              choices with live scene preview)\n"
        "      --physics-editor      gX Tools: physics sandbox/applier tuning\n"
        "                              with JSON presets (routes to --physics)\n"
        "      --rail-editor         gX Tools: camera rail editor (keyframe\n"
        "                              grabs, play, JSON presets; needs gx_shell)\n"
        "      --background-editor   gX Tools: background editor (palette/\n"
        "                              decomposer/presets; routes to --background)\n"
        "      --sky-editor          gX Tools: sky editor (look + ephemeris\n"
        "                              clock; routes to --sky)\n"
        "      --gl-diagnostics      Audit GL state at module boundaries\n"
        "      --gl-diagnostics-verbose  Include per-stage GL state dumps\n"
        "      --theme <path>         Aesthetic preset file (GX_THEME env fallback;\n"
        "                              shipped: resources/style/theme_blueprint.json)\n"
        "      --battle               Jump to battle scene\n"
        "      --map                  Jump to map scene\n"
        "      --menu                 Jump to menu scene\n"
        "      --editor               Jump to editor scene\n"
        "      --rooms                Jump to rooms scene\n"
        "      --analog               Jump to analog CRT scene\n"
        "      --clock                Jump to PS2 clock scene\n"
        "      --sky                  Jump to sky scene\n"
        "      --teletext             Jump to teletext scene\n"
        "      --water                Jump to water scene\n"
        "      --wireframe            Jump to wireframe scene\n"
        "      --raymarch             Jump to raymarch scene\n"
        "      --physics              Jump to rigid-body props sandbox\n"
        "                              (GX_PHYSICS_PROPS=<n> prop count,\n"
        "                              GX_PHYSICS_RAGDOLLS=<n> pre-spawns ragdolls,\n"
        "                              GX_PHYSICS_EM=1 charges props, M toggles, GX_PHYSICS_B=x,y,z B field;\n"
        "                              GX_PHYSICS_TEMP=<K> pre-heats, H heats;\n"
        "                              GX_PHYSICS_HEAT/J, GX_PHYSICS_THERMAL=0)\n"
        "      --module <path.so>     Load a dynamic module (default build; the\n"
        "                              shell ships as gx_shell.so — docs/dynamic_modules.md;\n"
        "                              GX_MODULE=a:b / modules/*.so auto-load)\n"
        "      --module-scene         Start the empty module-owned scene\n"
        "                              (docs/engine_frontend_contract.md)\n"
        "      --script <path>        Run an AngelScript shell script (needs the\n"
        "                              gx_shell module)\n"
        "      --script-editor        Open the scripting debugger panel (module)\n"
        "      --console              Open the developer console (module;\n"
        "                              F10 toggles at runtime)\n"
        "\n",
        name ? name : "gX_build");
}

gX_MainArguments arguments;

gX_I32 gX_Parse(int argc, char **argv);
gX_I32 gX_Parse(int argc, char **argv) {
    gX_I32 errors = 0;

    gX_I32 c = 0;
    gX_I32 option_index = 0;
    fprintf(stdout, "parsing arguments...\n");
    static struct option long_options[] = {
        {"screenWidth", required_argument,  NULL, 0},
        {"screenHeight",required_argument,  NULL, 0},
        {"renderScale", required_argument,  NULL, 0},
        {"targetFPS",   required_argument,  NULL, 0},
        {"seed",        required_argument,  NULL, 0},
        {"masterIndex", required_argument,  NULL, 0},
        {"fullScreen",  no_argument,        NULL, 0},
        {"monitor",     required_argument,  NULL, 0},
        {"logLevel",    required_argument,  NULL, 0},
        {"configFlags", required_argument,  NULL, 0},
        {"masterVolume",required_argument,  NULL, 0},
        {"preset",      required_argument,  NULL, 0},
        {"assets",      required_argument,  NULL, 0},    //opencode: v0.3.8 guest asset root (repeatable)
        {"guest-model", required_argument,  NULL, 0},    //opencode: v0.3.8 guest model injection
        {"no-gl-debug", no_argument,        NULL, 0},    //opencode: v0.3.8 skip the GL debug callback
        {"minimized",   no_argument,        NULL, 0},    //opencode: interactive iconified start (NOT for captures: backbuffer reads)
        {"disableAudio",no_argument,        NULL, 0},
        {"test-only",   no_argument,        NULL, 0},
        {"ai-screenshot-final", no_argument, NULL, 0},
        {"ai-screenshot-after", required_argument, NULL, 0},
        {"layout-dump", required_argument, NULL, 0},
        {"layout-dump-frame", required_argument, NULL, 0},
        {"debug-view",  required_argument, NULL, 0},
        {"ssr-debug",   required_argument, NULL, 0},
        {"gfx-profile", optional_argument, NULL, 0},
        {"raymarch-profile", optional_argument, NULL, 0},
        {"hw-report", no_argument, NULL, 0},
        {"gfx-safe-mode", no_argument, NULL, 0},
        {"disableGUI",  no_argument,        NULL, 0},
        {"help",        no_argument,        NULL, 0},
        {"textwriter",  no_argument,        NULL, 0},
        {"synthvis",    no_argument,        NULL, 0},
        {"title",       no_argument,        NULL, 0},
        //opencode: register the documented native battle shortcut.
        {"battle",      no_argument,        NULL, 0},
        {"gfx",         no_argument,        NULL, 0},
        {"showcase",    no_argument,        NULL, 0},
        {"showcase-water", no_argument,     NULL, 0},
        {"showcase-minimal", no_argument,   NULL, 0},
        {"reflectrow", no_argument,     NULL, 0},
        {"ledrow", no_argument,     NULL, 0},
        {"showcase-y2k", no_argument, NULL, 0},
        {"forest", no_argument,     NULL, 0},
        {"lake", no_argument,       NULL, 0},
        {"layout-test", no_argument,   NULL, 0},
        {"layout-check", no_argument,  NULL, 0},
        {"showcase-planar", no_argument,    NULL, 0},
        {"analog",      no_argument,        NULL, 0},
        {"background",  no_argument,        NULL, 0},
        {"bg-imgpro",         required_argument,  NULL, 0},
        {"bg-harness",        no_argument,        NULL, 0},
        {"bg-preset",         required_argument,  NULL, 0},
        {"bg-palette-mode",   required_argument,  NULL, 0},
        {"bg-react",          required_argument,  NULL, 0},
        {"bg-transition",     required_argument,  NULL, 0},
        {"theme",             required_argument,  NULL, 0},
        {"tools",             required_argument,  NULL, 0},
        {"theme-editor",      required_argument,  NULL, 0},
        {"themlab",           no_argument,        NULL, 0},
        {"materialslab",      no_argument,        NULL, 0},
        {"shaderlab",         no_argument,        NULL, 0},
        {"gfx-editor",        no_argument,        NULL, 0},
        //opencode: --wavetable-editor — synth wavetable authoring panel.
        {"wavetable-editor",  no_argument,        NULL, 0},
        //opencode: --textwriter-editor — textwriter authoring panel.
        {"textwriter-editor", no_argument,        NULL, 0},
        //ai: --physics-editor — sandbox/applier tuning panel.
        {"physics-editor",   no_argument,        NULL, 0},
        //opencode: --rail-editor — camera rail editor (module panel).
        {"rail-editor",      no_argument,        NULL, 0},
        //opencode: --background-editor — background authoring panel.
        {"background-editor", no_argument,       NULL, 0},
        //opencode: --sky-editor — sky look + ephemeris clock panel.
        {"sky-editor",       no_argument,        NULL, 0},
        {"gl-diagnostics", no_argument, NULL, 0},
        {"gl-diagnostics-verbose", no_argument, NULL, 0},
        {"clock",       no_argument,        NULL, 0},
        {"sky",         no_argument,        NULL, 0},
        {"teletext",    no_argument,        NULL, 0},
        {"water",       no_argument,        NULL, 0},
        {"wireframe",   no_argument,        NULL, 0},
        {"raymarch",    no_argument,        NULL, 0},
        {"physics",     no_argument,        NULL, 0},
        {"pp",          no_argument,        NULL, 0},
        //ai: dynamic modules — load a .so (shell = gx_shell.so) and the
        //   shell-facing args (inert until a module registers).
        {"module",      required_argument,  NULL, 0},
        {"module-scene", no_argument,       NULL, 0},
        {"script",      required_argument,  NULL, 0},
        {"script-editor", no_argument,      NULL, 0},
        {"console",     no_argument,        NULL, 0},
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc,argv,"w:h:",long_options,&option_index)) != -1) {
        if (c == 0) fprintf(stdout, "args: case %i [long]: arg %s \n", c, optarg);
        else fprintf(stdout, "args: case %i [%c]: arg %s \n", c, c, optarg);
        switch (c) {
            case 0: {
                if (long_options[option_index].flag != 0) break;
                printf ("-> option %s", long_options[option_index].name);
                if (optarg) printf (" with arg %s", optarg);
                printf ("\n");
                if (!strcmp(long_options[option_index].name,"renderScale")) {
                    arguments.renderScale = (gX_F32)atof(optarg);
                    arguments.hasRenderScale = true;
                }
                if (!strcmp(long_options[option_index].name,"screenWidth")) {
                    arguments.screenWidth = (gX_U16)atoi(optarg);
                    arguments.hasScreenWidth = true;
                    printf("screenWidth: %i \n", arguments.screenWidth);
                }
                if (!strcmp(long_options[option_index].name,"screenHeight")) {
                    arguments.screenHeight = (gX_U16)atoi(optarg);
                    arguments.hasScreenHeight = true;
                    printf("screenHeight: %i \n", arguments.screenHeight);
                }
                if (!strcmp(long_options[option_index].name,"masterIndex")) {
                    arguments.masterIndex = (gX_I32)atoi(optarg);
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"targetFPS")) {
                    arguments.targetFPS = (gX_F32)atof(optarg);
                    arguments.hasTargetFPS = true;
                }
                if (!strcmp(long_options[option_index].name,"seed")) {
                    arguments.seed = (gX_U32)strtoul(optarg, NULL, 10);
                    arguments.hasSeed = true;
                }
                if (!strcmp(long_options[option_index].name,"test-only")) {
                    arguments.hasTestOnly = true;
                }
                //ai: dynamic module — --module <path.so>
                if (!strcmp(long_options[option_index].name,"module")) {
                    strncpy(arguments.modulePath, optarg, sizeof(arguments.modulePath) - 1);
                    arguments.hasModule = true;
                }
                //ai: module-owned scene — generic empty frame for a module.
                if (!strcmp(long_options[option_index].name,"module-scene")) {
                    arguments.masterIndex = gX_SCENE_MODULE;
                    arguments.hasMasterIndex = true;
                }
                //ai: scripting shell — --script <path.as> (needs a module)
                if (!strcmp(long_options[option_index].name,"script")) {
                    strncpy(arguments.asScriptPath, optarg, sizeof(arguments.asScriptPath) - 1);
                    arguments.hasAsScript = true;
                }
                //ai: scripting debugger panel — --script-editor
                if (!strcmp(long_options[option_index].name,"script-editor")) {
                    arguments.scriptEditor = true;
                }
                //ai: developer console — --console
                if (!strcmp(long_options[option_index].name,"console")) {
                    arguments.console = true;
                }
                //opencode: headless capture — screenshot then exit
                if (!strcmp(long_options[option_index].name,"ai-screenshot-final")) {
                    arguments.aiScreenshotFinal = true;
                }
                if (!strcmp(long_options[option_index].name,"ai-screenshot-after")) {
                    arguments.hasAiScreenshotAfter = true;
                    arguments.aiScreenshotAfter = (gX_U32)atoi(optarg);
                }
                if (!strcmp(long_options[option_index].name,"layout-dump")) {
                    snprintf(arguments.layoutDumpPath,
                        sizeof(arguments.layoutDumpPath), "%s", optarg);
                    arguments.hasLayoutDump = true;
                }
                if (!strcmp(long_options[option_index].name,"layout-dump-frame")) {
                    arguments.layoutDumpFrame = atoi(optarg);
                    if (arguments.layoutDumpFrame < 1) arguments.layoutDumpFrame = 1;
                }
                if (!strcmp(long_options[option_index].name,"debug-view")) {
                    arguments.hasDebugView = true;
                    arguments.debugView = (gX_U32)atoi(optarg);
                }
                if (!strcmp(long_options[option_index].name,"ssr-debug")) {
                    arguments.hasSsrDebug = true;
                    arguments.ssrDebug = (gX_I32)atoi(optarg);
                }
                //opencode: --gfx-profile[=path.csv] — per-frame GPU timing CSV
                if (!strcmp(long_options[option_index].name,"gfx-profile")) {
                    arguments.gfxProfile = true;
                    if (optarg) snprintf(arguments.gfxProfilePath, sizeof(arguments.gfxProfilePath), "%s", optarg);
                }
                //ai: --hw-report — capability report + hardware tier
                if (!strcmp(long_options[option_index].name,"hw-report")) {
                    arguments.hasHwReport = true;
                }
                //ai: --raymarch-profile[=path.csv] — per-frame raymarch GPU timers
                if (!strcmp(long_options[option_index].name,"raymarch-profile")) {
                    arguments.raymarchProfile = true;
                    snprintf(arguments.raymarchProfilePath,
                        sizeof(arguments.raymarchProfilePath), "%s",
                        optarg ? optarg : "profiles/raymarch_timing.csv");
                }
                if (!strcmp(long_options[option_index].name,"gfx-safe-mode")) {
                    arguments.gfxSafeMode = true;
                }
                if (!strcmp(long_options[option_index].name,"gl-diagnostics")) {
                    arguments.hasGlDiagnostics = true;
                }
                if (!strcmp(long_options[option_index].name,"gl-diagnostics-verbose")) {
                    arguments.hasGlDiagnostics = true;
                    arguments.hasGlDiagnosticsVerbose = true;
                }
                if (!strcmp(long_options[option_index].name,"fullScreen")) {
                    arguments.fullScreen = true;
                }
                if (!strcmp(long_options[option_index].name,"monitor")) {
                    arguments.monitor = (gX_I32)atoi(optarg);
                }
                if (!strcmp(long_options[option_index].name,"logLevel")) {
                    arguments.logLevel = (gX_I32)atoi(optarg);
                    arguments.hasLogLevel = true;
                }
                if (!strcmp(long_options[option_index].name,"configFlags")) {
                    arguments.configFlags = (gX_U32)atoi(optarg);
                    arguments.hasConfigFlags = true;
                }
                if (!strcmp(long_options[option_index].name,"masterVolume")) {
                    arguments.masterVolume = (gX_F32)atof(optarg);
                    arguments.hasMasterVolume = true;
                }
                if (!strcmp(long_options[option_index].name,"preset")) {
                    arguments.hasPreset = true;
                    arguments.preset = (gX_I32)atoi(optarg);
                }
                //opencode: v0.3.8 - guest asset system: --assets registers an
                //   external resource root (fallback-only priority, repeatable
                //   up to 8); --guest-model injects foreign models into the
                //   showcase as an additive ring for gfx perf testing
                if (!strcmp(long_options[option_index].name,"assets")) {
                    if (arguments.guestAssetCount < 8) {
                        gX_I32 n = snprintf(arguments.guestAssets[arguments.guestAssetCount],
                            sizeof(arguments.guestAssets[0]), "%s", optarg);
                        if (n > 0) arguments.guestAssetCount++;
                    } else {
                        fprintf(stderr, "PARSE: --assets max 8 roots, ignoring '%s'\n", optarg);
                    }
                }
if (!strcmp(long_options[option_index].name,"guest-model")) {
                    gX_I8 gm[256];
                    gX_I32 n = snprintf(gm, sizeof(gm), "%s", optarg ? optarg : "");
                    if (n > 0 && (gX_U32)n < sizeof(gm)) {
                        // format: subpath[:count] — count defaults to 1, capped at 64
                        char *colon = strrchr(gm, ':');
                        arguments.guestCount = 1;
                        if (colon && colon != gm) {
                            arguments.guestCount = atoi(colon + 1);
                            if (arguments.guestCount < 1) arguments.guestCount = 1;
                            if (arguments.guestCount > 64) arguments.guestCount = 64;
                            *colon = 0;
                        }
                        snprintf(arguments.guestModel, sizeof(arguments.guestModel), "%s", gm);
                        fprintf(stderr, "PARSE: guest model '%s' x%d\n", arguments.guestModel, arguments.guestCount);
                    }
                }
                //opencode: v0.3.8 — skip the GL debug callback entirely
                //   (headless/harness runs; identical to gameX's --no-gl-debug)
                if (!strcmp(long_options[option_index].name,"no-gl-debug")) {
                    arguments.noGlDebug = true;
                }
                //opencode: iconified start (interactive only — captures read
                //   the backbuffer; see gX_Backend_Init limitation note).
                if (!strcmp(long_options[option_index].name,"minimized")) {
                    arguments.minimized = true;
                }
                if (!strcmp(long_options[option_index].name,"disableAudio")) {
                    arguments.disableModuleAudio = true;
                }
                if (!strcmp(long_options[option_index].name,"disableGUI")) {
                    arguments.disableModuleGUI = true;
                }
                if (!strcmp(long_options[option_index].name,"help")) {
                    gX_Parse_Help(argv[0]);
                    errors = 1;
                    goto parse_ret;
                }
                //opencode: scene shortcuts
                if (!strcmp(long_options[option_index].name,"textwriter")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_TEXTWRITER;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"synthvis")) {
                    arguments.masterIndex = gX_SCENE_SYNTHVIS;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"title")) {
                    arguments.masterIndex = gX_SCENE_TITLE;
                    arguments.hasMasterIndex = true;
                }
                //opencode: route the native battle shortcut to the battle scene.
                if (!strcmp(long_options[option_index].name,"battle")) {
                    arguments.masterIndex = gX_SCENE_BATTLE;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"gfx")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"showcase")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                }
                if (!strcmp(long_options[option_index].name,"showcase-water")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseWater = true;
                }
                //opencode: v0.1.24 — minimal reflection verification scene
                //   (--showcase-minimal) and its exact-planar oracle variant
                //   (--showcase-planar, same scene but the plane mirrors via
                //   the planar-reflection RT instead of SSR)
                if (!strcmp(long_options[option_index].name,"showcase-minimal")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseMinimal = true;
                }
                //opencode: v0.3.4 - reflection material-sweep row testbed
                if (!strcmp(long_options[option_index].name,"reflectrow")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseReflectRow = true;
                }
                //opencode: night LED testbed - moonlit slab, PBR centerpiece,
                //   LED ring, animated light rig (orbit/blink/hue)
                if (!strcmp(long_options[option_index].name,"ledrow")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseLedRow = true;
                }
                //opencode: opt-in visual direction study — never changes the
                //   default GFX scene or any reflection verification fixture.
                if (!strcmp(long_options[option_index].name,"showcase-y2k")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseY2K = true;
                }
                //opencode: dead-forest testbed - 110 guest trees via the
                //   GX_ASSETS guest fallback + thick volume fog (batch read)
                if (!strcmp(long_options[option_index].name,"forest")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseForest = true;
                }
                //opencode: forest-ringed lake testbed - water plane + banks
                //   + tree ring via the GX_ASSETS guest fallback + thin fog
                if (!strcmp(long_options[option_index].name,"lake")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseLake = true;
                }
                if (!strcmp(long_options[option_index].name,"showcase-planar")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.showcaseMinimal = true;
                    arguments.showcasePlanar = true;
                }
                //opencode: layout harness — broken scene + headless verifier
                if (!strcmp(long_options[option_index].name,"layout-test")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.layoutTest = true;
                }
                if (!strcmp(long_options[option_index].name,"layout-check")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                    arguments.showcase = true;
                    arguments.layoutTest = true;
                    arguments.layoutCheck = true;
                }
                //opencode: new renderer scene shortcuts
                if (!strcmp(long_options[option_index].name,"analog")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_ANALOG;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"background")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_BACKGROUND;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"clock")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_CLOCK;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"sky")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_SKY;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"teletext")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_TELETEXT;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"water")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_WATER;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"wireframe")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_WIREFRAME;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-imgpro")) {
                    strncpy(arguments.bgImgProPath, optarg, sizeof(arguments.bgImgProPath) - 1);
                    arguments.hasBgImgPro = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-harness")) {
                    arguments.bgHarness = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-preset")) {
                    strncpy(arguments.bgPresetPath, optarg, sizeof(arguments.bgPresetPath) - 1);
                    arguments.hasBgPreset = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-palette-mode")) {
                    arguments.bgPaletteMode = atoi(optarg);
                    arguments.hasBgPaletteMode = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-react")) {
                    arguments.bgReactType = atoi(optarg);
                    arguments.hasBgReact = true;
                }
                if (!strcmp(long_options[option_index].name,"bg-transition")) {
                    strncpy(arguments.bgTransitionPath, optarg, sizeof(arguments.bgTransitionPath) - 1);
                    arguments.hasBgTransition = true;
                    arguments.bgTransitionDuration = 2.0f;
                }
                if (!strcmp(long_options[option_index].name,"theme")) {
                    strncpy(arguments.themePath, optarg, sizeof(arguments.themePath) - 1);
                    arguments.hasThemePath = true;
                }
                if (!strcmp(long_options[option_index].name,"tools")) {
                    strncpy(arguments.toolsMarkupPath, optarg, sizeof(arguments.toolsMarkupPath) - 1);
                    arguments.hasTools = true;
                    arguments.masterIndex = gX_SCENE_GUIPLAYGROUND;
                    arguments.hasMasterIndex = true;
                }
                if (!strcmp(long_options[option_index].name,"theme-editor")) {
                    strncpy(arguments.themeEditorPath, optarg, sizeof(arguments.themeEditorPath) - 1);
                    arguments.hasThemeEditor = true;
                }
                //opencode: v0.6.2 — --themlab — live design preview. Routes
                //   to the title scene and forces the THEMLAB template onto
                //   canvas 2 (via gX_Tools_ThemLab_Init in main_load) — a
                //   clean preview only. Pair with --theme-editor <path> to
                //   edit tokens live against the gallery.
                if (!strcmp(long_options[option_index].name,"themlab")) {                    arguments.themlab = true;
                    arguments.masterIndex = gX_SCENE_TITLE;
                    arguments.hasMasterIndex = true;
                    //opencode: build via the gui.c GX_GUI_TEMPLATE_FORCE audit
                    //   hook (same early path as the working guitester) — a
                    //   late AddMake leaves shapes/shaders unable to draw.
                    setenv("GX_GUI_TEMPLATE_FORCE", "THEMLAB", 1);
                }
                //opencode: --materialslab — deterministic material response
                //   gallery on canvas 2 over the title scene.
                if (!strcmp(long_options[option_index].name,"materialslab")) {
                    arguments.masterIndex = gX_SCENE_TITLE;
                    arguments.hasMasterIndex = true;
                    setenv("GX_GUI_TEMPLATE_FORCE", "MATERIALSLAB", 1);
                }
                //opencode: --shaderlab — translucency/lens/color-math GUI
                //   shader experiments on canvas 2 over the title scene.
                if (!strcmp(long_options[option_index].name,"shaderlab")) {
                    arguments.masterIndex = gX_SCENE_TITLE;
                    arguments.hasMasterIndex = true;
                    setenv("GX_GUI_TEMPLATE_FORCE", "SHADERLAB", 1);
                }
                //opencode: --gfx-editor — GFX editor over the live scene.
                //   Routes to the GFX scene by default (nothing to edit
                //   elsewhere; mirrors --tools→GUIPLAYGROUND). An explicit
                //   --masterIndex later on the command line wins (parse order).
                //   Opened via Tools_Gfx_Init in main_load.
                if (!strcmp(long_options[option_index].name,"gfx-editor")) {
                    arguments.hasGfxEditor = true;
                    arguments.masterIndex = gX_SCENE_RENDERER_GFX;
                    arguments.hasMasterIndex = true;
                }
                //opencode: --wavetable-editor — synth wavetable editor. No
                //   routing: it edits engine synth state, usable over any
                //   scene (title by default). Opened in main_load.
                if (!strcmp(long_options[option_index].name,"wavetable-editor")) {
                    arguments.hasWavetableEditor = true;
                }
                //opencode: --textwriter-editor — textwriter editor. Routes to
                //   the textwriter scene by default (the preview surface); an
                //   explicit --masterIndex later on the command line wins.
                if (!strcmp(long_options[option_index].name,"textwriter-editor")) {
                    arguments.hasTextwriterEditor = true;
                    arguments.masterIndex = gX_SCENE_RENDERER_TEXTWRITER;
                    arguments.hasMasterIndex = true;
                }
                //ai: --physics-editor — sandbox tuning. Routes to the physics
                //   scene (the tuning target); an explicit --masterIndex later
                //   on the command line wins.
                if (!strcmp(long_options[option_index].name,"physics-editor")) {
                    arguments.hasPhysicsEditor = true;
                    arguments.masterIndex = gX_SCENE_PHYSICS;
                    arguments.hasMasterIndex = true;
                }
                //opencode: --rail-editor — camera rail editor (module panel;
                //   no routing, it works over any scene).
                if (!strcmp(long_options[option_index].name,"rail-editor")) {
                    arguments.hasRailEditor = true;
                }
                //opencode: --background-editor — focused background authoring;
                //   the opener routes to the --background scene.
                if (!strcmp(long_options[option_index].name,"background-editor")) {
                    arguments.hasBackgroundEditor = true;
                }
                //opencode: --sky-editor — sky look + ephemeris clock; the
                //   opener routes to the --sky scene.
                if (!strcmp(long_options[option_index].name,"sky-editor")) {
                    arguments.hasSkyEditor = true;
                }
                if (!strcmp(long_options[option_index].name,"raymarch")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_RAYMARCH;
                    arguments.hasMasterIndex = true;
                }
                //ai: --physics — rigid-body props sandbox (shared physics world)
                if (!strcmp(long_options[option_index].name,"physics")) {
                    arguments.masterIndex = gX_SCENE_PHYSICS;
                    arguments.hasMasterIndex = true;
                }
                //ai: --pp — the postprocess shader-chain scene
                if (!strcmp(long_options[option_index].name,"pp")) {
                    arguments.masterIndex = gX_SCENE_RENDERER_PP;
                    arguments.hasMasterIndex = true;
                }
                break;
            }
            case 'w': {
                arguments.screenWidth = (gX_U16)atoi(optarg);
                arguments.hasScreenWidth = true;
                printf("screenWidth: %i \n", arguments.screenWidth);
                break;
            }
            case 'h': {
                arguments.screenHeight = (gX_U16)atoi(optarg);
                arguments.hasScreenHeight = true;
                printf("screenHeight: %i \n", arguments.screenHeight);
                break;
            }
            case '?': {
                if (optopt == 'w') fprintf(stderr, "Option -%c: [screenWidth] (U16) \n", optopt);
                else if (optopt == 'h') fprintf(stderr, "Option -%c: [screenHeight] (U16) \n", optopt);
                else if (isprint(optopt)) fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else fprintf (stderr,"Unknown option character `\\x%x'.\n",optopt);
                break;
            }
            default: break;
        }
    }
    fprintf(stdout, "done.\n");

    //opencode: v0.3.8 — aesthetic presets: --theme <path> wins, GX_THEME env
    //   falls back (consumed before gX_GUI_Load via gX_GUI_Style_SetThemePath)
    if (!arguments.hasThemePath) {
        const gX_I8 *envTheme = getenv("GX_THEME");
        if (envTheme && envTheme[0]) {
            strncpy(arguments.themePath, envTheme, sizeof(arguments.themePath) - 1);
            arguments.hasThemePath = true;
        }
    }
    if (arguments.hasThemePath) {
        gX_GUI_Style_SetThemePath(arguments.themePath);
        gX_TraceLog(gX_LOG_INFO, "Style: theme preset '%s' (CLI/env)", arguments.themePath);
    } else if (arguments.hasThemeEditor) {
        //opencode: v0.4.0 — the theme editor targets its own theme file, so
        //   the scene renders with it from frame one (same pre-load path as
        //   --theme)
        gX_GUI_Style_SetThemePath(arguments.themeEditorPath);
        gX_TraceLog(gX_LOG_INFO, "Style: theme editor target '%s'", arguments.themeEditorPath);
    }

    return errors;

parse_ret:
    return errors;
}
