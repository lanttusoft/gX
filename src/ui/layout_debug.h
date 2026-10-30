/**********************************************************************************************
*
*   [gX: src/ui/layout_debug.h]
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


//opencode: UI Layout Debug — element tracking, overlap/overflow detection, visualizer, ruler, state explorer
#ifndef UI_LAYOUT_DEBUG_H
#define UI_LAYOUT_DEBUG_H

#include "../common/types.h"

#define GX_UI_LAYOUT_MAX_ENTRIES 512

typedef struct {
    gX_I8  label[48];
    gX_I8  canvas[24];
    gX_F32 x, y, w, h;
    gX_U8  r, g, b, a;
    gX_I1  isText;
    gX_I1  isPanel;
    gX_I1  offscreen;
    gX_I1  overlapping;
} gX_UI_LayoutEntry;

extern gX_I1 gX_uiDebugEnabled;
//opencode: tracking-only flag — records element rects without the overlay/
//   validation side effects. gui_pick enables it on demand.
extern gX_I1 gX_uiLayoutTrackEnabled;

// Element tracking (manual — battle raw-drawn HUD)
gX_I0 gX_UI_Layout_Clear(gX_I0);
gX_I0 gX_UI_Layout_TrackRect(const gX_I8 *label, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h);
gX_I0 gX_UI_Layout_TrackText(const gX_I8 *label, const gX_I8 *text,
    gX_F32 x, gX_F32 y, gX_F32 fontSize);
gX_I0 gX_UI_Layout_TrackPanel(const gX_I8 *label, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h);
gX_I0 gX_UI_Layout_Validate(gX_F32 screenW, gX_F32 screenH);
gX_I0 gX_UI_Layout_DrawOverlay(gX_F32 screenW, gX_F32 screenH);

//opencode: generic GUI-canvas tracking — every drawn element on every canvas
//   (separate table so the battle manual tracking never gets clobbered)
gX_I0 gX_UI_Layout_GUI_Clear(gX_I0);
gX_I0 gX_UI_Layout_GUI_TrackRect(const gX_I8 *canvasName, const gX_I8 *label,
    gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h, gX_I1 isText, gX_I1 isPanel);
gX_I32 gX_UI_Layout_GUI_Count(gX_I0);

//opencode: hit-test screen coords against the GUI tracking table — writes
//   one "canvas:label rect=(x,y,w,h) text=.. panel=.." line per hit (later
//   entries = drawn on top) into outBuf, returns the match count
gX_I32 gX_UI_Layout_GUI_Pick(gX_F32 x, gX_F32 y, gX_I8 *outBuf, gX_I32 outBufSize);

//opencode: structured JSON layout dump (element name→rect per canvas) — the
//   data source for scripts/layout_report.py + layout_sweep.py
gX_I32 gX_UI_Layout_Dump(const gX_I8 *path);

// Pixel ruler (M key to toggle, click to place markers)
gX_I0 gX_UI_Layout_DrawRuler(gX_F32 screenW, gX_F32 screenH);

// Battle state explorer (R key to toggle)
gX_I0 gX_UI_Layout_DrawStatePanel(gX_F32 screenW, gX_F32 screenH,
    gX_I32 phase, gX_I32 menuState, gX_I32 cursorX, gX_I32 subIndex,
    gX_I32 subScroll, gX_I32 activeActorIndex, gX_I32 activeActorType,
    gX_I1 waitingForInput, gX_I32 actionType, gX_U32 actionTarget,
    gX_I32 currentWave, gX_I32 enemyCount, gX_I32 playerCount);

#endif
