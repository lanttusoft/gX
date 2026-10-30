/**********************************************************************************************
*
*   [gX: src/tools/background/bg_panel.h]
*   Background tuning panel — shared body for the GFX and background editors
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


//opencode: one control surface, two hosts — the GFX editor embeds this body
//   inside its Background collapsing section, the standalone background
//   editor (--background-editor) draws it directly. Extracted from
//   gfx_editor.cpp so the palette/decomposer controls exist once.
#ifndef GX_TOOLS_BG_PANEL_H
#define GX_TOOLS_BG_PANEL_H

#include "../../common/types.h"
#include "../../renderer/background.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: active background module instance (NULL when not loaded).
gX_RendererBackground *gX_Tools_BG_Active(gX_I0);
//opencode: draw the tuning body; withHeader=1 wraps it in the GFX editor's
//   "Background" collapsing header, withPresets=0 lets a host draw its own
//   preset workflow (the gate's marker draws either way).
gX_I0 gX_Tools_BG_PanelBody(gX_I1 withHeader, gX_I1 withPresets);

#ifdef __cplusplus
}
#endif

#endif //GX_TOOLS_BG_PANEL_H
