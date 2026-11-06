/**********************************************************************************************
*
*   [gX: src/gui/console_playback.h]
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

#ifndef GUI_CONSOLE_PLAYBACK_H
#define GUI_CONSOLE_PLAYBACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//ai: gX-240 playback clock — real pause, single 1/60 s step, and 2x/4x/8x
//   fast-forward. UI, audio and the OSD keep real time; scene/renderer
//   simulation uses this clock.
#define GX_CONSOLE_PLAYBACK_STEP_DT (1.0 / 60.0)
#define GX_CONSOLE_PLAYBACK_MAX_DELTA 0.25

gX_I0 gX_ConsolePlayback_Reset(gX_I0);
gX_I0 gX_ConsolePlayback_Update(gX_I0);
gX_I1 gX_ConsolePlayback_Active(gX_I0);
gX_I1 gX_ConsolePlayback_Paused(gX_I0);
gX_I1 gX_ConsolePlayback_InputBlocked(gX_I0);
gX_I1 gX_ConsolePlayback_Simulating(gX_I0);
gX_F64 gX_ConsolePlayback_Delta(gX_I0);
gX_I32 gX_ConsolePlayback_FastForward(gX_I0);
gX_I0 gX_ConsolePlayback_RequestStep(gX_I0);
gX_I0 gX_ConsolePlayback_CycleFastForward(gX_I0);
const gX_I8 *gX_ConsolePlayback_Label(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_CONSOLE_PLAYBACK_H
