/**********************************************************************************************
*
*   [gX: src/gui/widgetsdf.h]
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


#ifndef GUI_WIDGETSDF_H
#define GUI_WIDGETSDF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gui.h"
#include "canvas.h"
#include "element/element.h"

//opencode: v0.2.0 — shared SDF widget surface program (guiwidget.frag).
//   Loaded once in gX_GUI_Load; Draw_Rect routes through it when the theme
//   enables SDF widgets, giving every rect element rounded corners, AA
//   edges, a soft drop shadow and a subtle surface gradient in one pass.

gX_I0    gX_GUI_WidgetSDF_Init(gX_I0);
gX_I0    gX_GUI_WidgetSDF_Unload(gX_I0);
gX_I1    gX_GUI_WidgetSDF_Ready(gX_I0);
gX_I0    gX_GUI_WidgetSDF_Attach(gX_GUI_Element *element);
gX_I0    gX_GUI_WidgetSDF_DrawNow(gX_GUI_Element *, gX_GUI_Canvas *, gX_Color);
gX_I0    gX_GUI_WidgetSDF_Enqueue(gX_GUI_Element *, gX_GUI_Canvas *, gX_Color);
gX_I0    gX_GUI_WidgetSDF_Flush(gX_I0); //opencode: call from Element_Load

#ifdef __cplusplus
}
#endif

#endif //GUI_WIDGETSDF_H
