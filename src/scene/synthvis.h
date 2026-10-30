/**********************************************************************************************
*
*   [gX: src/scene/synthvis.h]
*   Synth visualizer scene — playable keyboard + oscillo/lissajous/spectrum
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


//opencode: gameX synthvis port (gX-native): the scene owns the keyboard input and
//   the canvas composite; the renderer's oscillo/lissajous/spectrum modules own
//   the visualizers (their RTs are composited by the scene) and the keyboard is
//   a GUI canvas.
#ifndef GX_SCENE_SYNTHVIS_H
#define GX_SCENE_SYNTHVIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: keyboard band as a fraction of the surface height — the GUI template
//   (screen space) and the scene compositor (render space) share it so the keys
//   align at any render scale.
#define gX_SYNTHVIS_KEYBOARD_FRACTION 0.15f

typedef struct gX_SceneSynthVis {
    gX_I8 name[128];
    gX_I8 description[256];

    // keyboard state — 14 white keys (C4–B5) and 10 black keys
    gX_I1  keysPressed[14];
    gX_I1  keysSharpPressed[10];
    gX_I32 whiteVoiceID[14];   // synth voice index per held white key (-1 = none)
    gX_I32 sharpVoiceID[10];   // synth voice index per held black key (-1 = none)

    gX_I1 loaded;
    gX_I1 firstFrame;
} gX_SceneSynthVis;

gX_I32 gX_SceneSynthVis_Load(gX_SceneSynthVis *vis);
gX_I32 gX_SceneSynthVis_Unload(gX_SceneSynthVis *vis);
gX_I32 gX_SceneSynthVis_Update(gX_SceneSynthVis *vis);
gX_I32 gX_SceneSynthVis_Draw(gX_SceneSynthVis *vis);
//opencode: scene-exit hook — silence notes and drop held-key state (called when
//   the dispatcher leaves the scene).
gX_I0  gX_SceneSynthVis_Park(gX_SceneSynthVis *vis);

//opencode: shared key-bed geometry — the GUI keyboard template and the scene
//   compositor both lay out from these (screen pixels for the template, canvas
//   pixels for the scene). Pure math, headless-testable.
gX_Rectangle gX_SceneSynthVis_WhiteKeyRect(gX_I32 index, gX_F32 width, gX_F32 height);
gX_Rectangle gX_SceneSynthVis_BlackKeyRect(gX_I32 index, gX_F32 width, gX_F32 height);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_SYNTHVIS_H
