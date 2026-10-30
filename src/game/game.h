/**********************************************************************************************
*
*   [gX: src/game/game.h]
*   Game data header — minimal gX shim (ephemeris only)
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


#ifndef GX_GAME_H
#define GX_GAME_H

#include "../common/types.h"

#include "ephemeris.h"

//opencode: gX — game content dropped; the shim only carries the ephemeris
//   (the sky renderer reads celestial body directions from it)
typedef struct gX_GameData {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 enabled;

    gX_Ephemeris ephemeris;
} gX_GameData;

extern gX_GameData gamedata;

gX_I32 gX_GameData_Load(gX_I0);
gX_I32 gX_GameData_Unload(gX_I0);
gX_I0  gX_GameData_Update(gX_I0);

#endif
