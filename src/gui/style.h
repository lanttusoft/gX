/**********************************************************************************************
*
*   [gX: src/gui/style.h]
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

#ifndef GUI_STYLE_H
#define GUI_STYLE_H

#include "../common/types.h"
#include "gui.h"

#define GX_GUI_STYLE_DEFAULT_PATH   "resources/style/theme.json"
#define GX_GUI_STYLE_RELOAD_MS      500

gX_I32 gX_GUI_Style_LoadDefault(gX_I0);
//opencode: reset every theme token to its C default (the fallback for keys
//   missing from theme.json). The hot-reload path resets before applying
//   JSON; runtime resource flags (uiFont/uiFontSDF/loaded) are untouched.
gX_I0 gX_GUI_Style_ResetDefaults(gX_I0);
gX_I0 gX_GUI_Style_CheckReload(gX_I0);
gX_I0 gX_GUI_Style_SetThemePath(const gX_I8 *path);
gX_I0 gX_GUI_Style_SetReducedMotion(gX_I1 reduced);
const gX_I8 *gX_GUI_Style_GetThemePath(gX_I0);

//opencode: v0.4.0 — theme editor support (gX Tools suite)
gX_I0 gX_GUI_Style_MarkRefresh(gX_I0);
gX_I32 gX_GUI_Style_Save(const gX_I8 *path);
gX_I32 gX_GUI_Style_TokenCount(gX_I0);
const gX_I8 *gX_GUI_Style_TokenKey(gX_I32 index);
const gX_I8 *gX_GUI_Style_TokenGroup(gX_I32 index);
gX_U32 gX_GUI_Style_TokenDatatype(gX_I32 index);
gX_I0 *gX_GUI_Style_TokenPtr(gX_I32 index);

#endif //GUI_STYLE_H
