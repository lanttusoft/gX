/**********************************************************************************************
*
*   [gX: src/gui/element/make.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#include "element.h"

#include "../enum.h"
#include "../config.h"

#include "../templates/element_template_registry.h"

#include "../../common/log.h"

gX_I0 gX_GUI_Element_Make(gX_GUI_Element *element, gX_I32 index) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_GUI_TRACELOG_LEVEL,"GUI_Element_Make","Making an element")
    //opencode: registry dispatch replaces the old switch
    gX_GUI_ElementTemplate_Dispatch(index, element);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_GUI_TRACELOG_LEVEL,"Making done")
}
