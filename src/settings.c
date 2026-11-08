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
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h> //opencode: reject non-finite persisted tool UI scale values.

#include "settings.h"
#include "audio.h"
#include "audio/filter.h"
#include "camera.h"
#include "gui/style.h"
#include "wrappers/wrappers.h"
#include "wrappers/window.h"

gX_Settings settings;

//ai: the settings manifest. Basic rows are the console surface; Advanced adds
//   the practical PC controls; Expert exposes the remaining persisted values.
#define GX_FIELD_BOOL(key_, label_, field_, lvl_) \
    { key_, label_, GX_SETTINGS_FIELD_BOOL, offsetof(gX_Settings, field_), \
      lvl_, 0.0f, 1.0f, 0.0f, "" }
#define GX_FIELD_I32(key_, label_, field_, lvl_, lo_, hi_, rec_) \
    { key_, label_, GX_SETTINGS_FIELD_I32, offsetof(gX_Settings, field_), \
      lvl_, lo_, hi_, rec_, "" }
#define GX_FIELD_U32(key_, label_, field_, lvl_, lo_, hi_, rec_, unit_) \
    { key_, label_, GX_SETTINGS_FIELD_U32, offsetof(gX_Settings, field_), \
      lvl_, lo_, hi_, rec_, unit_ }
#define GX_FIELD_F32(key_, label_, field_, lvl_, lo_, hi_, rec_, unit_) \
    { key_, label_, GX_SETTINGS_FIELD_F32, offsetof(gX_Settings, field_), \
      lvl_, lo_, hi_, rec_, unit_ }

static const gX_Settings_FieldInfo gX_Settings_Fields[] = {
    GX_FIELD_BOOL("fullScreen", "Fullscreen", fullScreen, GX_SETTINGS_UI_BASIC),
    GX_FIELD_F32("masterVolume", "Master volume", masterVolume, GX_SETTINGS_UI_BASIC, 0.0f, 2.0f, 1.0f, ""),
    GX_FIELD_F32("targetFPS", "Target FPS", targetFPS, GX_SETTINGS_UI_BASIC, 15.0f, 240.0f, 60.0f, "fps"),
    GX_FIELD_I32("uiThemePreset", "UI theme", uiThemePreset, GX_SETTINGS_UI_BASIC, 0.0f, 3.0f, 0.0f),
    GX_FIELD_BOOL("reduceMotion", "Reduce motion", reduceMotion, GX_SETTINGS_UI_BASIC),
    GX_FIELD_F32("cameraSens", "Camera sensitivity", cameraSens, GX_SETTINGS_UI_BASIC, 0.5f, 10.0f, 3.0f, ""),
    GX_FIELD_BOOL("invertY", "Invert Y", invertY, GX_SETTINGS_UI_BASIC),
    GX_FIELD_F32("cameraFov", "Camera FOV", cameraFov, GX_SETTINGS_UI_ADVANCED, 60.0f, 120.0f, 90.0f, "deg"),
    GX_FIELD_F32("toolUiScale", "Tool UI scale", toolUiScale, GX_SETTINGS_UI_ADVANCED, 1.0f, 1.75f, 1.25f, "x"),
    GX_FIELD_BOOL("toolKeyboardNav", "Tool keyboard navigation", toolKeyboardNav, GX_SETTINGS_UI_ADVANCED),
    GX_FIELD_F32("musicVolume", "Music volume", musicVolume, GX_SETTINGS_UI_ADVANCED, 0.0f, 2.0f, 1.0f, ""),
    GX_FIELD_F32("sfxVolume", "SFX volume", sfxVolume, GX_SETTINGS_UI_ADVANCED, 0.0f, 2.0f, 1.0f, ""),
    GX_FIELD_F32("synthVolume", "Synth volume", synthVolume, GX_SETTINGS_UI_ADVANCED, 0.0f, 2.0f, 1.0f, ""),
    GX_FIELD_F32("renderScale", "Render scale", renderScale, GX_SETTINGS_UI_EXPERT, 0.25f, 1.0f, 1.0f, "x"),
    GX_FIELD_I32("presentFilter", "Present filter", presentFilter, GX_SETTINGS_UI_BASIC, 0.0f, 1.0f, 0.0f),
    GX_FIELD_BOOL("consoleSSAA2x", "Console 2x SSAA", consoleSSAA2x, GX_SETTINGS_UI_EXPERT),
    GX_FIELD_BOOL("consolePRISM1", "Console PRISM-1", consolePRISM1, GX_SETTINGS_UI_EXPERT),
    GX_FIELD_BOOL("consoleGrain", "Console film grain", consoleGrain, GX_SETTINGS_UI_EXPERT),
    GX_FIELD_I32("toolGlLevel", "GL level request", toolGlLevel, GX_SETTINGS_UI_EXPERT, 0.0f, 430.0f, 0.0f),
    GX_FIELD_I32("uiLevel", "UI level", uiLevel, GX_SETTINGS_UI_EXPERT, 0.0f, 2.0f, 1.0f),
    GX_FIELD_U32("screenWidth", "Virtual render width", screenWidth, GX_SETTINGS_UI_EXPERT, 320.0f, 3840.0f, 1024.0f, "px"),
    GX_FIELD_U32("screenHeight", "Virtual render height", screenHeight, GX_SETTINGS_UI_EXPERT, 240.0f, 2160.0f, 768.0f, "px"),
    GX_FIELD_U32("windowWidth", "Window width", windowWidth, GX_SETTINGS_UI_EXPERT, 320.0f, 3840.0f, 1024.0f, "px"),
    GX_FIELD_U32("windowHeight", "Window height", windowHeight, GX_SETTINGS_UI_EXPERT, 240.0f, 2160.0f, 768.0f, "px"),
    GX_FIELD_BOOL("masterFilterEnabled", "Master filter", masterFilterEnabled, GX_SETTINGS_UI_EXPERT),
    GX_FIELD_I32("masterFilterMode", "Filter mode", masterFilterMode, GX_SETTINGS_UI_EXPERT, 0.0f, 2.0f, 0.0f),
    GX_FIELD_F32("masterFilterCutoff", "Filter cutoff", masterFilterCutoff, GX_SETTINGS_UI_EXPERT, 0.0f, 1.0f, 0.8f, ""),
    GX_FIELD_F32("masterFilterResonance", "Filter resonance", masterFilterResonance, GX_SETTINGS_UI_EXPERT, 0.0f, 1.0f, 0.0f, ""),
    GX_FIELD_BOOL("edTourSeen", "Tour seen", edTourSeen, GX_SETTINGS_UI_EXPERT),
};
#undef GX_FIELD_BOOL
#undef GX_FIELD_I32
#undef GX_FIELD_U32
#undef GX_FIELD_F32

gX_U32 gX_Settings_FieldCount(gX_I0)
{
    return (gX_U32)(sizeof(gX_Settings_Fields) / sizeof(gX_Settings_Fields[0]));
}

const gX_Settings_FieldInfo *gX_Settings_FieldAt(gX_U32 index)
{
    if (index >= gX_Settings_FieldCount()) return NULL;
    return &gX_Settings_Fields[index];
}

const gX_I8 *gX_Settings_UILevelName(gX_I32 level)
{
    switch (level) {
        case GX_SETTINGS_UI_BASIC: return "Basic";
        case GX_SETTINGS_UI_ADVANCED: return "Advanced";
        case GX_SETTINGS_UI_EXPERT: return "Expert";
        default: return "Advanced";
    }
}

//ai: parse the persisted/env spelling once; -1 means "not a level".
gX_I32 gX_Settings_UILevelParse(const gX_I8 *text)
{
    if (!text || !text[0]) return -1;
    if (!strcasecmp((const char *)text, "basic") || !strcmp((const char *)text, "0"))
        return GX_SETTINGS_UI_BASIC;
    if (!strcasecmp((const char *)text, "advanced") || !strcmp((const char *)text, "1"))
        return GX_SETTINGS_UI_ADVANCED;
    if (!strcasecmp((const char *)text, "expert") || !strcmp((const char *)text, "2"))
        return GX_SETTINGS_UI_EXPERT;
    return -1;
}

//ai: effective level for every surface; a valid GX_UI_LEVEL wins over the
//   persisted value, and an out-of-range stored value falls back to Advanced.
gX_Settings_UILevel gX_Settings_UILevelEffective(gX_I0)
{
    gX_I32 override = gX_Settings_UILevelParse((const gX_I8 *)getenv("GX_UI_LEVEL"));
    if (override >= GX_SETTINGS_UI_BASIC && override < GX_SETTINGS_UI_LEVEL_COUNT)
        return (gX_Settings_UILevel)override;
    if (settings.uiLevel < GX_SETTINGS_UI_BASIC
        || settings.uiLevel >= GX_SETTINGS_UI_LEVEL_COUNT)
        return GX_SETTINGS_UI_ADVANCED;
    return (gX_Settings_UILevel)settings.uiLevel;
}

//ai: audits can pin a level without touching the persisted settings file.
gX_I1 gX_Settings_UILevelOverrideActive(gX_I0)
{
    gX_I32 override = gX_Settings_UILevelParse((const gX_I8 *)getenv("GX_UI_LEVEL"));
    return (override >= GX_SETTINGS_UI_BASIC
        && override < GX_SETTINGS_UI_LEVEL_COUNT) ? 1 : 0;
}

//ai: one comparison for every editor gate (Basic <= Advanced <= Expert).
gX_I1 gX_Settings_UILevelAllows(gX_Settings_UILevel level)
{
    return gX_Settings_UILevelEffective() >= level ? 1 : 0;
}

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
    GX_SCHEMAJSON_ADDOBJECT_TERM(presentFilter,(settings.presentFilter),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(consoleSSAA2x,(settings.consoleSSAA2x),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(consolePRISM1,(settings.consolePRISM1),I1)
    GX_SCHEMAJSON_ADDOBJECT_TERM(consoleGrain,(settings.consoleGrain),I1)
    //opencode: persist the tool accessibility preferences with ordinary settings.
    GX_SCHEMAJSON_ADDOBJECT_TERM(toolUiScale,(settings.toolUiScale),F32)
    //opencode: persisted GL feature-level request (0 = auto).
    GX_SCHEMAJSON_ADDOBJECT_TERM(toolGlLevel,(settings.toolGlLevel),I32)
    GX_SCHEMAJSON_ADDOBJECT_TERM(uiLevel,(settings.uiLevel),I32)
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
    
    settings.screenWidth = maindata.consoleOutputWidth
        ? maindata.consoleOutputWidth
        : (maindata.renderOverride ? maindata.renderOverrideWidth : maindata.screenWidth);
    settings.screenHeight = maindata.consoleOutputHeight
        ? maindata.consoleOutputHeight
        : (maindata.renderOverride ? maindata.renderOverrideHeight : maindata.screenHeight);
    settings.windowWidth = maindata.windowWidth;
    settings.windowHeight = maindata.windowHeight;
    settings.renderScale = maindata.renderScale;
    settings.presentFilter = maindata.presentFilter;
    settings.consoleSSAA2x = (maindata.consoleSSAA == 2) ? 1 : 0;
    settings.consolePRISM1 = maindata.consolePRISM1 ? 1 : 0;
    settings.consoleGrain = maindata.consoleGrain ? 1 : 0;
    //opencode: initialize accessible defaults for the editor tools.
    //  125% keeps the 13px base readable at 1024x768 and 1920x1080.
    settings.toolUiScale = 1.25f;
    settings.toolAssetRoots[0] = 0;
    settings.toolGlLevel = 0;
    settings.uiLevel = GX_SETTINGS_UI_ADVANCED;
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
    settings.screenWidth  = maindata.consoleOutputWidth
        ? maindata.consoleOutputWidth
        : (maindata.renderOverride ? maindata.renderOverrideWidth : maindata.screenWidth);
    settings.screenHeight = maindata.consoleOutputHeight
        ? maindata.consoleOutputHeight
        : (maindata.renderOverride ? maindata.renderOverrideHeight : maindata.screenHeight);
    settings.windowWidth  = maindata.windowWidth;
    settings.windowHeight = maindata.windowHeight;
    settings.renderScale  = maindata.renderScale;
    settings.presentFilter = 0;
    settings.consoleSSAA2x = false;
    settings.consolePRISM1 = false;
    settings.consoleGrain = false;
    //opencode: reset tool readability and navigation preferences with settings.
    settings.toolUiScale  = 1.25f;
    settings.toolAssetRoots[0] = 0;
    settings.toolGlLevel = 0;
    settings.uiLevel = GX_SETTINGS_UI_ADVANCED;
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
    //ai: a missing settings file is the expected first-run state, not a load
    //   failure. Probe first so the JSON loader does not emit a WARN before
    //   the newcomer reaches the title.
    FILE *probe = fopen("resources/settings.json", "rb");
    gX_I1 exists = probe != NULL;
    if (probe) fclose(probe);
    if (!exists || gX_JSON_Load("resources/settings.json") != 0) {
        if (!exists) {
            gX_TraceLog(gX_LOG_INFO,
                "Settings_Load: no settings file yet — using defaults");
        } else {
            gX_TraceLog(gX_LOG_NOTICE,
                "Settings_Load: could not load resources/settings.json — using defaults");
        }
        settings.uiThemePreset = GX_SETTINGS_UI_THEME_DEFAULT;
        gX_GUI_Style_SetThemePath(GX_GUI_STYLE_DEFAULT_PATH);
        gX_Settings_AudioSync(1);
        return 1;
    }
    //opencode: sanitize saved scale before any ImGui context reads it.
    settings.toolUiScale = gX_Settings_ToolUIScale_Clamp(settings.toolUiScale);
    //ai: audit hook — pin the tool scale without touching the persisted file.
    {
        const gX_I8 *scaleEnv = getenv("GX_UI_SCALE");
        if (scaleEnv && scaleEnv[0]) {
            gX_F32 scale = (gX_F32)atof((const char *)scaleEnv);
            if (isfinite(scale) && scale >= 1.0f && scale <= 1.75f)
                settings.toolUiScale = scale;
        }
    }
    if (settings.uiLevel < GX_SETTINGS_UI_BASIC
        || settings.uiLevel >= GX_SETTINGS_UI_LEVEL_COUNT)
        settings.uiLevel = GX_SETTINGS_UI_ADVANCED;
    settings.presentFilter = settings.presentFilter != 0 ? 1 : 0;
    settings.consoleSSAA2x = settings.consoleSSAA2x ? 1 : 0;
    {
        const gX_I8 *ssaaEnv = getenv("GX_CONSOLE_SSAA");
        if (ssaaEnv && (ssaaEnv[0] == '0' || ssaaEnv[0] == '1'
                || ssaaEnv[0] == '4'))
            settings.consoleSSAA2x = (ssaaEnv[0] == '1'
                && maindata.consoleSSAA == 2) ? 1 : 0;
    }
    settings.consolePRISM1 = settings.consolePRISM1 ? 1 : 0;
    {
        //ai: the automation hook wins over the persisted toggle so A/B gates
        //   are deterministic; the product route remains the shell/pause row.
        const gX_I8 *prismHook = getenv("GX_CONSOLE_PRISM");
        if (prismHook && (prismHook[0] == '0' || prismHook[0] == '1'))
            settings.consolePRISM1 = prismHook[0] == '1' ? 1 : 0;
        maindata.consolePRISM1 = (settings.consolePRISM1
            && maindata.virtualScreen
            && maindata.consoleOutputWidth == 320
            && maindata.consoleOutputHeight == 240) ? 1 : 0;
    }
    settings.consoleGrain = settings.consoleGrain ? 1 : 0;
    {
        const gX_I8 *grainHook = getenv("GX_CONSOLE_GRAIN");
        if (grainHook && (grainHook[0] == '0' || grainHook[0] == '1'))
            settings.consoleGrain = grainHook[0] == '1' ? 1 : 0;
        maindata.consoleGrain = (settings.consoleGrain
            && maindata.virtualScreen
            && maindata.consoleOutputWidth == 320
            && maindata.consoleOutputHeight == 240) ? 1 : 0;
    }
    //ai: a CLI/env filter wins over the persisted value; otherwise the saved
    //   preference applies to the blit immediately. The virtual screen stays
    //   a per-session override unless the console shell owns the layout.
    {
        const gX_I8 *filterHook = getenv("GX_PRESENT_FILTER");
        gX_I1 filterOverride = (arguments.hasPresentFilter
            || (filterHook && filterHook[0])) ? 1 : 0;
        if (filterOverride) settings.presentFilter = maindata.presentFilter;
        else maindata.presentFilter = settings.presentFilter;
        if (maindata.virtualScreen) {
            settings.screenWidth = maindata.consoleOutputWidth
                ? maindata.consoleOutputWidth : maindata.renderOverrideWidth;
            settings.screenHeight = maindata.consoleOutputHeight
                ? maindata.consoleOutputHeight : maindata.renderOverrideHeight;
        }
    }
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

gX_I0 gX_Settings_Apply_VirtualScreen(gX_I0) {
    settings.presentFilter = settings.presentFilter != 0 ? 1 : 0;
    settings.consoleSSAA2x = settings.consoleSSAA2x ? 1 : 0;
    maindata.presentFilter = settings.presentFilter;
    //ai: the persisted screen size is the 240p output; 2x SSAA renders the
    //   scene at twice that size. The GUI screen stays window-sized.
    gX_U32 outW = settings.screenWidth;
    gX_U32 outH = settings.screenHeight;
    gX_I1 SSAA = (settings.consoleSSAA2x && outW == 320 && outH == 240) ? 1 : 0;
    gX_U32 applyW = outW * (SSAA ? 2u : 1u);
    gX_U32 applyH = outH * (SSAA ? 2u : 1u);
    if (outW >= 64 && outW <= 4096
        && outH >= 64 && outH <= 4096
        && (outW != maindata.windowWidth
            || outH != maindata.windowHeight)) {
        maindata.renderOverride = 1;
        maindata.renderOverrideWidth = (gX_U16)applyW;
        maindata.renderOverrideHeight = (gX_U16)applyH;
        maindata.consoleSSAA = SSAA ? 2 : 0;
        maindata.consoleOutputWidth = (gX_U16)outW;
        maindata.consoleOutputHeight = (gX_U16)outH;
        maindata.virtualScreen = 1;
    } else {
        maindata.renderOverride = 0;
        maindata.consoleSSAA = 0;
        maindata.consoleOutputWidth = maindata.windowWidth;
        maindata.consoleOutputHeight = maindata.windowHeight;
        maindata.virtualScreen = 0;
    }
    //ai: PRISM-1 is meaningful only on the 320x240 console output; Native and
    //   non-240p virtual sizes keep the stored preference but bypass the pass.
    maindata.consolePRISM1 = (settings.consolePRISM1 && maindata.virtualScreen
        && maindata.consoleOutputWidth == 320
        && maindata.consoleOutputHeight == 240) ? 1 : 0;
    maindata.consoleGrain = (settings.consoleGrain && maindata.virtualScreen
        && maindata.consoleOutputWidth == 320
        && maindata.consoleOutputHeight == 240) ? 1 : 0;
    gX_MainData_Refresh();
    //ai: a runtime virtual-screen change must reload the renderer canvas and
    //   scene targets, not just the maindata dimensions (black frame otherwise).
    maindata.refresh = true;
}

gX_I0 gX_Settings_Apply_ConsolePRISM(gX_I0) {
    settings.consolePRISM1 = settings.consolePRISM1 ? 1 : 0;
    maindata.consolePRISM1 = (settings.consolePRISM1 && maindata.virtualScreen
        && maindata.consoleOutputWidth == 320
        && maindata.consoleOutputHeight == 240) ? 1 : 0;
    settings.dirtyFlag = 1;
}

gX_I0 gX_Settings_Apply_ConsoleGrain(gX_I0) {
    settings.consoleGrain = settings.consoleGrain ? 1 : 0;
    maindata.consoleGrain = (settings.consoleGrain && maindata.virtualScreen
        && maindata.consoleOutputWidth == 320
        && maindata.consoleOutputHeight == 240) ? 1 : 0;
    settings.dirtyFlag = 1;
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
    gX_Settings_Apply_VirtualScreen();
    
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
