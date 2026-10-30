/**********************************************************************************************
*
*   [gX: src/settings.h]
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


#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif
    
//modes:
//basic - basic controls, for "consoles"
//advanced - for "pcs"
//expert - fine tuning with whatever values, exposes everything
    
#include "common/types.h"
#include "common/config.h"
#include "common/marshal/json.h"
#include "input.h"
    
#include "main.h"

//opencode: stable persisted IDs shared by native and ImGui theme selectors.
#define GX_SETTINGS_UI_THEME_DEFAULT 0
#define GX_SETTINGS_UI_THEME_HIGH_CONTRAST 1
#define GX_SETTINGS_UI_THEME_COLOR_VISION 2
//opencode: Frutiger Aero styling for the ImGui tool suite (glossy glass).
#define GX_SETTINGS_UI_THEME_AERO 3
#define GX_SETTINGS_UI_THEME_COUNT 4
    
typedef struct gX_Settings {
    gX_I8 name[DEFAULT_NAMEBFR_SIZE];
    gX_I8 description[DEFAULT_DESCBFR_SIZE];
    
    gX_I1  fullScreen;
    
    gX_F32 masterVolume;
    //ai: mixer bus user volumes (0..1) — persisted; gX_Settings_Update
    //   pushes field changes to the audio buses the same frame, so the
    //   pause/Settings sliders are live without an Apply press. Master is
    //   the device master volume (0..2, the existing slider range).
    gX_F32 musicVolume;
    gX_F32 sfxVolume;
    gX_F32 synthVolume;
    //ai: master filter (the TPT core on the mixed output) — off by default;
    //   the pause mixer's filter controls write these and the same live sync
    //   applies them.
    gX_I1  masterFilterEnabled;
    gX_I32 masterFilterMode;      //ai: GX_AUDIO_FILTER_LP/HP/BP
    gX_F32 masterFilterCutoff;    //ai: 0..1 (Nyquist-normalized)
    gX_F32 masterFilterResonance; //ai: 0..1
    
    gX_U32 screenWidth;
    gX_U32 screenHeight;
    gX_U32 windowWidth;
    gX_U32 windowHeight;
    gX_F32 renderScale;
    //opencode: user scale for ImGui tool text, spacing, and hit targets.
    gX_F32 toolUiScale;
    //opencode: full Tab/arrow/Enter navigation in ImGui tools.
    gX_I1  toolKeyboardNav;
    //opencode: requested GL feature level (0 = auto/detected); applied at
    //   load because shader variants compile per level.
    gX_I32 toolGlLevel;
    //opencode: browse-only asset roots for the GFX asset browser (colon
    //   separated). Paths are used in place — nothing is copied into the repo.
    gX_I8 toolAssetRoots[1024];
    //opencode: persisted theme preset shared by the native UI and ImGui tools.
    gX_I32 uiThemePreset;
    gX_F32 targetFPS;
    
    gX_F32 cameraSens;
    gX_F32 cameraFov;
    gX_I1  invertY;
    gX_I1  reduceMotion;
    gX_I1  edTourSeen;
    gX_I32  resetFlag;
    gX_I32  applyFlag;
    gX_I32  saveFlag;
    gX_I1   dirtyFlag;

    //opencode: two persisted keyboard slots per standard input action.
    gX_I32  bindKeys[gX_BIND_COMMON_AMOUNT][2];
    //opencode: settings-only sub-view state; not persisted.
    gX_I1   bindView;
    
    gX_JSON_Schema schema;
    
} gX_Settings;
    
extern gX_Settings settings;

gX_I32 gX_Settings_Init(gX_I0);
gX_I32 gX_Settings_Reset(gX_I0);
gX_I32 gX_Settings_Load(gX_I0);
gX_I32 gX_Settings_Save(gX_I0);
gX_I32 gX_Settings_Apply(gX_I0);
//ai: audio-only apply — push the volume fields to the mixer buses
//   (gX_Settings_Update calls it every frame so slider edits are live; the
//   mixer tests pin it without touching theme/window state).
gX_I0 gX_Settings_Audio_Apply(gX_I0);

gX_I0 gX_Settings_Update(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //SETTINGS_H
