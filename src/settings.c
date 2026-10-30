/**********************************************************************************************
*
*   [gX: src/settings.c]
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


#include <string.h>
#include <stdlib.h>
#include <math.h> //opencode: reject non-finite persisted tool UI scale values.

#include "settings.h"
#include "audio.h"
#include "audio/filter.h"
#include "camera.h"
#include "gui/style.h"
#include "wrappers/wrappers.h"
#include "wrappers/window.h"

gX_Settings settings;

static gX_I32 s_appliedUiThemePreset = -1;

//ai: last bus value pushed to the audio mixer per bus — a slider drag edits
//   the settings field and the per-frame sync applies exactly the changes.
static gX_F32 s_audioApplied[GX_AUDIO_BUS_COUNT] = {-1.0f, -1.0f, -1.0f, -1.0f};

//ai: master filter push (defined after the bus sync; the sync helper calls it).
static gX_I0 gX_Settings_FilterSync(gX_I1 force);

//ai: settings field behind each mixer bus.
static gX_F32 gX_Settings_AudioField(gX_I32 bus) {
    switch (bus) {
        case GX_AUDIO_BUS_MUSIC: return settings.musicVolume;
        case GX_AUDIO_BUS_SFX:   return settings.sfxVolume;
        case GX_AUDIO_BUS_SYNTH: return settings.synthVolume;
        default:                 return settings.masterVolume;
    }
}

//ai: push settings volumes to the live mixer; force re-applies everything
//   (load/reset/Apply), otherwise only fields that changed since the last
//   push (live slider edits from the pause menu or the Settings panel).
static gX_I0 gX_Settings_AudioSync(gX_I1 force) {
    for (gX_I32 bus = 0; bus < GX_AUDIO_BUS_COUNT; bus++) {
        gX_F32 value = gX_Settings_AudioField(bus);
        if (!force && value == s_audioApplied[bus]) continue;
        gX_Audio_BusSetVolume(bus, value);
        s_audioApplied[bus] = value;
    }
    //ai: the master filter rides the same live path (defined below; the
    //   forward declaration keeps the sync helper first).
    gX_Settings_FilterSync(force);
}

//ai: master filter push — the setter resets the filter state, so only call
//   it when one of the four values actually changed (the per-frame apply
//   path runs every frame).
static struct {
    gX_I1 enabled;
    gX_I32 mode;
    gX_F32 cutoff;
    gX_F32 resonance;
} s_filterApplied = {-1, -1, -1.0f, -1.0f};

static gX_I0 gX_Settings_FilterSync(gX_I1 force) {
    if (!force
        && settings.masterFilterEnabled == s_filterApplied.enabled
        && settings.masterFilterMode == s_filterApplied.mode
        && settings.masterFilterCutoff == s_filterApplied.cutoff
        && settings.masterFilterResonance == s_filterApplied.resonance) {
        return;
    }
    gX_Audio_MasterFilter_Set(settings.masterFilterEnabled,
        settings.masterFilterMode, settings.masterFilterCutoff,
        settings.masterFilterResonance);
    s_filterApplied.enabled = settings.masterFilterEnabled;
    s_filterApplied.mode = settings.masterFilterMode;
    s_filterApplied.cutoff = settings.masterFilterCutoff;
    s_filterApplied.resonance = settings.masterFilterResonance;
}

//ai: public audio-only apply (see settings.h) — the per-frame live path.
gX_I0 gX_Settings_Audio_Apply(gX_I0) {
    gX_Settings_AudioSync(0);
}

//ai: sanitize persisted volumes after a load (a corrupt file must not
//   silence the run or blow the device up).
static gX_I0 gX_Settings_AudioClamp(gX_I0) {
    if (!isfinite(settings.masterVolume) || settings.masterVolume < 0.0f) settings.masterVolume = 1.0f;
    if (settings.masterVolume > 2.0f) settings.masterVolume = 2.0f;
    if (!isfinite(settings.musicVolume) || settings.musicVolume < 0.0f) settings.musicVolume = 1.0f;
    if (settings.musicVolume > 1.0f) settings.musicVolume = 1.0f;
    if (!isfinite(settings.sfxVolume) || settings.sfxVolume < 0.0f) settings.sfxVolume = 1.0f;
    if (settings.sfxVolume > 1.0f) settings.sfxVolume = 1.0f;
    if (!isfinite(settings.synthVolume) || settings.synthVolume < 0.0f) settings.synthVolume = 1.0f;
    if (settings.synthVolume > 1.0f) settings.synthVolume = 1.0f;
    if (settings.masterFilterMode < GX_AUDIO_FILTER_LP
        || settings.masterFilterMode > GX_AUDIO_FILTER_BP) {
        settings.masterFilterMode = GX_AUDIO_FILTER_LP;
    }
    if (!isfinite(settings.masterFilterCutoff)
        || settings.masterFilterCutoff < 0.001f) settings.masterFilterCutoff = 0.8f;
    if (settings.masterFilterCutoff > 0.99f) settings.masterFilterCutoff = 0.99f;
    if (!isfinite(settings.masterFilterResonance)
        || settings.masterFilterResonance < 0.0f) settings.masterFilterResonance = 0.0f;
    if (settings.masterFilterResonance > 1.0f) settings.masterFilterResonance = 1.0f;
}

#define GX_SCHEMAJSON_ADDOBJECT(name) \
    GX_SCHEMAJSON_ADDOBJECT_TEMPLATE(settings.schema,name) \

//opencode: keep saved accessibility preferences in the supported UI range.
static gX_F32 gX_Settings_ToolUIScale_Clamp(gX_F32 scale)
{
    if (!isfinite(scale) || scale < 1.0f) return 1.0f;
    if (scale > 1.75f) return 1.75f;
    return scale;
}

//opencode: keep native theme-file selection and the persisted preset index in sync.
static const gX_I8 *gX_Settings_UITheme_Path(gX_I32 preset)
{
    switch (preset) {
        case GX_SETTINGS_UI_THEME_HIGH_CONTRAST:
            return "resources/style/theme_high_contrast.json";
        case GX_SETTINGS_UI_THEME_COLOR_VISION:
            return "resources/style/theme_color_vision.json";
        case GX_SETTINGS_UI_THEME_AERO:
            //opencode: the Aero theme styles the ImGui tools; the native GUI
            //   keeps the default blueprint theme.
            return GX_GUI_STYLE_DEFAULT_PATH;
        default:
            return GX_GUI_STYLE_DEFAULT_PATH;
    }
}

static gX_I32 gX_Settings_UITheme_Clamp(gX_I32 preset)
{
    if (preset < GX_SETTINGS_UI_THEME_DEFAULT
        || preset >= GX_SETTINGS_UI_THEME_COUNT) return GX_SETTINGS_UI_THEME_DEFAULT;
    return preset;
}

//opencode: persisted keyboard bind defaults shared by settings init/reset.
static gX_I0 gX_Settings_Bind_Defaults(gX_I0) {
    for (gX_I32 i = 0; i < gX_BIND_COMMON_AMOUNT; i++) {
        settings.bindKeys[i][0] = -1;
        settings.bindKeys[i][1] = -1;
    }
    settings.bindKeys[gX_BIND_MOVE_FORWARD][0] = gX_KEY_W;
    settings.bindKeys[gX_BIND_MOVE_FORWARD][1] = gX_KEY_UP;
    settings.bindKeys[gX_BIND_MOVE_BACKWARD][0] = gX_KEY_S;
    settings.bindKeys[gX_BIND_MOVE_BACKWARD][1] = gX_KEY_DOWN;
    settings.bindKeys[gX_BIND_MOVE_LEFT][0] = gX_KEY_A;
    settings.bindKeys[gX_BIND_MOVE_LEFT][1] = gX_KEY_LEFT;
    settings.bindKeys[gX_BIND_MOVE_RIGHT][0] = gX_KEY_D;
    settings.bindKeys[gX_BIND_MOVE_RIGHT][1] = gX_KEY_RIGHT;
    settings.bindKeys[gX_BIND_MOVE_UP][0] = gX_KEY_E;
    settings.bindKeys[gX_BIND_MOVE_DOWN][0] = gX_KEY_Q;
    settings.bindKeys[gX_BIND_ROTATE_CLOCKWISE][0] = gX_KEY_E;
    settings.bindKeys[gX_BIND_ROTATE_COUNTERCLOCKWISE][0] = gX_KEY_Q;
    settings.bindKeys[gX_BIND_CONFIRM][0] = gX_KEY_ENTER;
    settings.bindKeys[gX_BIND_CONFIRM][1] = gX_KEY_SPACE;
    settings.bindKeys[gX_BIND_CANCEL][0] = gX_KEY_ESCAPE;
    settings.bindKeys[gX_BIND_MENU][0] = gX_KEY_ESCAPE;
}

#define GX_SCHEMAJSON_ADD_BIND(name) \
    GX_SCHEMAJSON_ADDOBJECT_TERM(bind_##name##_0,(settings.bindKeys[gX_BIND_##name][0]),I32) \
    GX_SCHEMAJSON_ADDOBJECT_TERM(bind_##name##_1,(settings.bindKeys[gX_BIND_##name][1]),I32)

static gX_I32 gX_Settings_Schema_Init(gX_I0) {
    //opencode: gX — compact arena (was 8192 static = 2.5 MB bss for 15 fields)
    //ai: capacity must cover EVERY registered term (binds are two terms each):
    //   the master-filter fields pushed the count past the old 64-object
    //   arena and corrupted the heap. 96 keeps real headroom.
    gX_JSON_Schema_EnsureArena(&settings.schema, 96);
    settings.schema.loadedCount = 0;
    settings.schema.objectsCount = 96;
    GX_SCHEMAJSON_INIT
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(fullScreen,(settings.fullScreen),I1)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(masterVolume,(settings.masterVolume),F32)
    //ai: mixer bus volumes persist with the rest of the audio settings.
    GX_SCHEMAJSON_ADDOBJECT_TERM(musicVolume,(settings.musicVolume),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(sfxVolume,(settings.sfxVolume),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(synthVolume,(settings.synthVolume),F32)
    //ai: master filter (mixed-output TPT filter + its parameters).
    GX_SCHEMAJSON_ADDOBJECT_TERM(masterFilterEnabled,(settings.masterFilterEnabled),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(masterFilterMode,(settings.masterFilterMode),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(masterFilterCutoff,(settings.masterFilterCutoff),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(masterFilterResonance,(settings.masterFilterResonance),F32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(screenWidth,(settings.screenWidth),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(screenHeight,(settings.screenHeight),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(windowWidth,(settings.windowWidth),U32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(windowHeight,(settings.windowHeight),U32)
    
    GX_SCHEMAJSON_ADDOBJECT_TERM(renderScale,(settings.renderScale),F32)
    //opencode: persist the tool accessibility preferences with ordinary settings.
    GX_SCHEMAJSON_ADDOBJECT_TERM(toolUiScale,(settings.toolUiScale),F32)
    //opencode: persisted GL feature-level request (0 = auto).
    GX_SCHEMAJSON_ADDOBJECT_TERM(toolGlLevel,(settings.toolGlLevel),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(toolKeyboardNav,(settings.toolKeyboardNav),I1)
    //opencode: browse-only asset roots survive restarts without touching presets.
    GX_SCHEMAJSON_ADDOBJECT_STRING(toolAssetRoots,(settings.toolAssetRoots))
    GX_SCHEMAJSON_ADDOBJECT_TERM(uiThemePreset,(settings.uiThemePreset),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(targetFPS,(settings.targetFPS),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(cameraSens,(settings.cameraSens),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(cameraFov,(settings.cameraFov),F32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(invertY,(settings.invertY),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(reduceMotion,(settings.reduceMotion),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(edTourSeen,(settings.edTourSeen),I1)

#define X(bind) GX_SCHEMAJSON_ADD_BIND(bind)
    gX_BINDS_COMMON
#undef X
    
    settings.schema.loadedCount = (gX_U32)(jsoncontext.currentObjectIndex + 1);
    return 0;
}

gX_I32 gX_Settings_Init(gX_I0) {
    
    settings.masterVolume = 1.0f;
    settings.musicVolume = 1.0f;
    settings.sfxVolume = 1.0f;
    settings.synthVolume = 1.0f;
    //ai: the master filter ships off; LP at 0.8 (~19 kHz) is the neutral
    //   starting point when it is switched on.
    settings.masterFilterEnabled = 0;
    settings.masterFilterMode = GX_AUDIO_FILTER_LP;
    settings.masterFilterCutoff = 0.8f;
    settings.masterFilterResonance = 0.0f;
    
    settings.screenWidth = maindata.screenWidth;
    settings.screenHeight = maindata.screenHeight;
    settings.windowWidth = maindata.windowWidth;
    settings.windowHeight = maindata.windowHeight;
    settings.renderScale = maindata.renderScale;
    //opencode: initialize accessible defaults for the editor tools.
    settings.toolUiScale = 1.0f;
    settings.toolAssetRoots[0] = 0;
    settings.toolGlLevel = 0;
    settings.toolKeyboardNav = 1;
    settings.uiThemePreset = GX_SETTINGS_UI_THEME_DEFAULT;
    s_appliedUiThemePreset = -1;
    gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
    
    settings.cameraSens = 3.0f;
    settings.cameraFov = 90.0f;
    settings.invertY = false;
    settings.reduceMotion = false;
    settings.edTourSeen = false;
    gX_Settings_Bind_Defaults();
    settings.resetFlag = false;
    settings.applyFlag = false;
    settings.saveFlag = false;
    settings.dirtyFlag = false;
    settings.bindView = false;
    settings.targetFPS = maindata.targetFPS;
    
    gX_Settings_Schema_Init();
    
    //ai: seed the mixer with the default volumes (a settings file, when
    //   present, re-pushes the loaded values in gX_Settings_Load).
    gX_Settings_AudioSync(1);

    return 0;
}

gX_I32 gX_Settings_Reset(gX_I0) {
    settings.fullScreen   = false;
    settings.masterVolume = 1.0f;
    settings.musicVolume  = 1.0f;
    settings.sfxVolume    = 1.0f;
    settings.synthVolume  = 1.0f;
    settings.masterFilterEnabled = 0;
    settings.masterFilterMode = GX_AUDIO_FILTER_LP;
    settings.masterFilterCutoff = 0.8f;
    settings.masterFilterResonance = 0.0f;
    settings.screenWidth  = maindata.screenWidth;
    settings.screenHeight = maindata.screenHeight;
    settings.windowWidth  = maindata.windowWidth;
    settings.windowHeight = maindata.windowHeight;
    settings.renderScale  = maindata.renderScale;
    //opencode: reset tool readability and navigation preferences with settings.
    settings.toolUiScale  = 1.0f;
    settings.toolAssetRoots[0] = 0;
    settings.toolGlLevel = 0;
    settings.toolKeyboardNav = 1;
    settings.uiThemePreset = GX_SETTINGS_UI_THEME_DEFAULT;
    settings.targetFPS    = maindata.targetFPS;
    settings.cameraSens   = 3.0f;
    settings.cameraFov    = 90.0f;
    settings.invertY      = false;
    settings.reduceMotion = false;
    settings.edTourSeen   = false;
    gX_Settings_Bind_Defaults();
    settings.resetFlag    = false;
    settings.applyFlag    = false;
    settings.saveFlag     = false;
    settings.dirtyFlag    = false;
    gX_Settings_AudioSync(1);
    gX_TraceLog(gX_LOG_INFO, "Settings reset to defaults");
    return 0;
}

gX_I32 gX_Settings_Load(gX_I0) {
    json.schema = &settings.schema;
    if (gX_JSON_Load("resources/settings.json") != 0) {
        gX_TraceLog(gX_LOG_NOTICE, "Settings_Load: could not load resources/settings.json — using defaults");
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_DEFAULT;
        gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
        gX_Settings_AudioSync(1);
        return 1;
    }
    //opencode: sanitize saved scale before any ImGui context reads it.
    settings.toolUiScale = gX_Settings_ToolUIScale_Clamp(settings.toolUiScale);
    settings.uiThemePreset = gX_Settings_UITheme_Clamp(settings.uiThemePreset);
    gX_GUI_Style_SetThemePath(gX_Settings_UITheme_Path(settings.uiThemePreset));
    s_appliedUiThemePreset = settings.uiThemePreset;
    //ai: persisted volume sanitizing + the --masterVolume override (the CLI
    //   value was parsed but never applied anywhere).
    gX_Settings_AudioClamp();
    if (arguments.hasMasterVolume) {
        gX_F32 argVolume = arguments.masterVolume;
        if (!isfinite(argVolume) || argVolume < 0.0f) argVolume = 0.0f;
        if (argVolume > 2.0f) argVolume = 2.0f;
        settings.masterVolume = argVolume;
    }
    gX_Settings_AudioSync(1);
    gX_TraceLog(gX_LOG_INFO, "Settings loaded");
    return 0;
}

gX_I32 gX_Settings_Save(gX_I0) {
    //opencode: test/smoke harnesses must not persist transient window geometry
    //   into the user's settings file.
    if (getenv("GX_NO_SETTINGS_SAVE")) {
        settings.dirtyFlag = false;
        return 0;
    }
    json.schema = &settings.schema;
    gX_JSON_Save("resources/settings.json");
    settings.dirtyFlag = false;
    gX_TraceLog(gX_LOG_INFO, "Settings saved");
    return 0;
}

gX_I32 gX_Settings_Apply(gX_I0) {
    
    //ai: the mixer buses are the audio half of an Apply (live push; the
    //   per-frame sync also applies slider edits without an Apply press).
    gX_Settings_AudioSync(1);

    maindata.masterVolume = settings.masterVolume;
    gX_MasterVolume_Set(maindata.masterVolume);
    
    maindata.renderScale = settings.renderScale;
    //opencode: clamp render scale to a sane range — a corrupt/cornered value
    //   (e.g. 0.009 saved by a buggy slider) collapses the canvas to near-zero
    //   and makes every scene unreadable. Clamp to [0.25, 1.0].
    if (maindata.renderScale < 0.25f) maindata.renderScale = 0.25f;
    if (maindata.renderScale > 1.0f) maindata.renderScale = 1.0f;
    settings.renderScale = maindata.renderScale;
    
    maindata.targetFPS = settings.targetFPS;
    gX_TargetFPS_Set(maindata.targetFPS);

    //opencode: keep live settings inside the tool UI's supported scale range.
    settings.toolUiScale = gX_Settings_ToolUIScale_Clamp(settings.toolUiScale);
    
    camera.camera3D.fovy = settings.cameraFov;
    //opencode: clamp FOV to sane degrees range — a corrupt/radians value (e.g.
    //   0.39 saved from a bad slider) collapsed the camera to a ~22° cone and
    //   made battle actors look huge/clipped.
    if (camera.camera3D.fovy < 30.0f) camera.camera3D.fovy = 30.0f;
    if (camera.camera3D.fovy > 120.0f) camera.camera3D.fovy = 120.0f;
    settings.cameraFov = camera.camera3D.fovy;
    settings.dirtyFlag = true;
    
    if (settings.fullScreen != gX_Window_IsFullScreen()) {
        gX_Window_ToggleFullScreen();
        settings.fullScreen = gX_Window_IsFullScreen();
    }
    
    maindata.refresh = true;
    
    return 0;
}

//opencode: execute settings button actions after GUI input has been processed.
gX_I0 gX_Settings_Update(gX_I0) {
    //ai: live mixer sync — a slider drag edits a settings field and the bus
    //   follows the same frame (no Apply press needed); deduplicated so an
    //   idle frame costs four float compares.
    gX_Settings_Audio_Apply();
    if (settings.resetFlag) {
        gX_Settings_Reset();
        settings.dirtyFlag = true;
    }
    settings.uiThemePreset = gX_Settings_UITheme_Clamp(settings.uiThemePreset);
    if (settings.uiThemePreset != s_appliedUiThemePreset) {
        gX_GUI_Style_SetThemePath(gX_Settings_UITheme_Path(settings.uiThemePreset));
        if (gX_GUI_Style_LoadDefault() == 0) {
            s_appliedUiThemePreset = settings.uiThemePreset;
            gX_GUI_Style_MarkRefresh();
            settings.dirtyFlag = true;
            gX_TraceLog(gX_LOG_INFO, "Settings: UI theme preset %d applied",
                settings.uiThemePreset);
        }
    }
    //ai: test-only deterministic override for capture gates; the persisted
    //   setting remains authoritative when the hook is unset.
    const gX_I8 *reduceMotionHook = getenv("GX_REDUCE_MOTION");
    if (reduceMotionHook && reduceMotionHook[0])
        settings.reduceMotion = (reduceMotionHook[0] == '1');
    settings.reduceMotion = settings.reduceMotion != 0;
    gX_GUI_Style_SetReducedMotion(settings.reduceMotion);
    if (settings.applyFlag) {
        settings.applyFlag = false;
        gX_Settings_Apply();
    }
    if (settings.saveFlag) {
        settings.saveFlag = false;
        gX_Settings_Save();
    }
}
