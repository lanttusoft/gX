/**********************************************************************************************
*
*   [gX: src/scripting/api_core.h]
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


//ai: ported from gameX src/scripting/api_core.h — the distilled gX subset:
//   math/easing, Print, Yield/Sleep, key state, time, SetRenderScale,
//   SwitchScene + constants, TakeScreenshot, the Vec3 value type, the Hook
//   registry bridge and the shell assert/quit harness.
#ifndef GX_SCRIPTING_API_CORE_H
#define GX_SCRIPTING_API_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../scripting.h"

gX_I32 gX_Scripting_RegisterCoreAPI(struct asIScriptEngine *engine);
gX_I32 gX_Scripting_RegisterVec3Type(struct asIScriptEngine *engine);
gX_I32 gX_Scripting_RegisterHookAPI(struct asIScriptEngine *engine);

#ifdef __cplusplus
}
#endif

#endif //GX_SCRIPTING_API_CORE_H
