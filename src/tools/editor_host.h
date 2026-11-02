/**********************************************************************************************
*
*   [gX: src/tools/editor_host.h]
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
*
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/

#ifndef GX_EDITOR_HOST_H
#define GX_EDITOR_HOST_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: global editor host — the always-summonable toolbar root. It owns
//   the application menu bar (gX/File/Edit/View/<Active Editor>/Window/Help)
//   and routes editor switching/console/quit. Panels remain the existing tool
//   windows until the workspace migration lands.
gX_I0 gX_EditorHost_Toggle(gX_I0);
gX_I0 gX_EditorHost_SetOpen(gX_I1 open);
gX_I1 gX_EditorHost_IsOpen(gX_I0);
gX_I0 gX_EditorHost_Update(gX_I0);
gX_I0 gX_EditorHost_Draw(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
