/**********************************************************************************************
*
*   [gX: src/gui/templates/synthvis.c]
*   gX — a synth visualizer scene keyboard template
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


//opencode: synthvis keyboard — 14 white + 10 black key elements sharing the
//   scene's key-bed geometry (key presses light the keys up). Visualizers are
//   drawn by the scene, not here.
#include "template_header.h"

#include "../../scene/scene.h"

gX_I0 gX_GUI_Template_SynthVis_Keyboard(gX_I32 canvasindex);
gX_I0 gX_GUI_Template_SynthVis_Keyboard(gX_I32 canvasindex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasindex);
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "SynthVis_Keyboard",
        "synth keyboard", gX_GUI_TEMPLATE_SYNTHVIS_KEYBOARD);

    gX_SceneSynthVis *vis = &scene.synthvis;

    for (gX_I32 i = 0; i < 14; i++) {
        gX_Rectangle rect = gX_SceneSynthVis_WhiteKeyRect(i,
            (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_Rect(&b, rect.x, rect.y, rect.width, rect.height);
            if (vis->keysPressed[i]) gX_GUI_Builder_Color(&b, 0, 180, 255, 255);
            else gX_GUI_Builder_Color(&b, 220, 220, 230, 255);
        gX_GUI_Builder_Element_End(&b);
    }

    for (gX_I32 i = 0; i < 10; i++) {
        gX_Rectangle rect = gX_SceneSynthVis_BlackKeyRect(i,
            (gX_F32)b.width, (gX_F32)b.height);
        gX_GUI_Builder_Element_Begin(&b);
            gX_GUI_Builder_Rect(&b, rect.x, rect.y, rect.width, rect.height);
            if (vis->keysSharpPressed[i]) gX_GUI_Builder_Color(&b, 0, 140, 220, 255);
            else gX_GUI_Builder_Color(&b, 30, 30, 40, 255);
        gX_GUI_Builder_Element_End(&b);
    }

    gX_GUI_Builder_Free(&b);
}
