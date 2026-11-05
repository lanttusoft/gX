/**********************************************************************************************
*
*   [gX: src/gui/console_state.h]
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

#ifndef GUI_CONSOLE_STATE_H
#define GUI_CONSOLE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define GX_CONSOLE_SAVE_SLOTS 3

//ai: gX-240 cartridge save states — cartridge id, video profile, camera orbit
//   and creation time in save/console_slot_N.json. GX_CONSOLE_SAVE_DIR
//   redirects the folder (probes); GX_NO_SETTINGS_SAVE=1 refuses writes.
gX_I32 gX_ConsoleState_Save(gX_I32 slot);
gX_I32 gX_ConsoleState_Load(gX_I32 slot);
gX_I1  gX_ConsoleState_Exists(gX_I32 slot);
gX_I0  gX_ConsoleState_Describe(gX_I32 slot, gX_I8 *out, gX_I32 size);

#ifdef __cplusplus
}
#endif

#endif //GUI_CONSOLE_STATE_H
