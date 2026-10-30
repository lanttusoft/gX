/**********************************************************************************************
*
*   [gX: src/scripting/script.h]
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


//ai: ported from gameX src/scripting/script.h — .as file loading, module
//   compilation, context execution (host-internal; not script-facing).
#ifndef GX_SCRIPTING_SCRIPT_H
#define GX_SCRIPTING_SCRIPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../scripting.h"

gX_I32 gX_Script_Init(gX_Script *s);
gX_I32 gX_Script_LoadFile(gX_Script *s, const gX_I8 *path);
gX_I32 gX_Script_LoadFileWithLib(gX_Script *s, const gX_I8 *mainPath, const gX_I8 *libPath);
gX_I32 gX_Script_LoadFileWithLibs(gX_Script *s, const gX_I8 *mainPath, const gX_I8 *libPaths[], gX_U32 libCount);
gX_I32 gX_Script_CompileModule(gX_Script *s, struct asIScriptEngine *engine);
gX_I32 gX_Script_CreateContext(gX_Script *s, struct asIScriptEngine *engine);
gX_I32 gX_Script_RunFunction(gX_Script *s, const gX_I8 *decl);

#ifdef __cplusplus
}
#endif

#endif //GX_SCRIPTING_SCRIPT_H
