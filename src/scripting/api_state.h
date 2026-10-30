/**********************************************************************************************
*
*   [gX: src/scripting/api_state.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: ported from gameX src/scripting/api_state.h — script persistent KV
//   store (survives hot-reload; file serialization is a later slice).
#ifndef GX_SCRIPTING_API_STATE_H
#define GX_SCRIPTING_API_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../scripting.h"

gX_I32 gX_Scripting_RegisterStateAPI(struct asIScriptEngine *engine);

#ifdef __cplusplus
}
#endif

#endif //GX_SCRIPTING_API_STATE_H
