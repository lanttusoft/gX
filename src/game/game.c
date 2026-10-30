/**********************************************************************************************
*
*   [gX: src/game/game.c]
*   Game data — minimal gX shim (ephemeris only)
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


#include "../common/types.h"
#include <string.h>

#include "game.h"
#include "ephemeris.h"

gX_GameData gamedata;

gX_I32 gX_GameData_Load(gX_I0) {
    strcpy(gamedata.name, "gamedata");
    strcpy(gamedata.description, "gX game data shim");
    gamedata.enabled = true;
    gX_Ephemeris_SetupDefaults(&gamedata.ephemeris);
    gX_Ephemeris_Update(&gamedata.ephemeris, 0.0);
    return 0;
}

gX_I32 gX_GameData_Unload(gX_I0) {
    return 0;
}

gX_I0 gX_GameData_Update(gX_I0) {
    (void)0;
}
