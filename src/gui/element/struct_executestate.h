/**********************************************************************************************
*
*   [gX: src/gui/element/struct_executestate.h]
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


#ifndef GUI_ELEMENT_STRUCT_EXECUTESTATE_H
#define GUI_ELEMENT_STRUCT_EXECUTESTATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
    
typedef struct gX_GUI_Element_ExecuteState {
    gX_I1       done;
    gX_I1       once;
    
    gX_I1       hovered;
    gX_I1       toggled;
    
    gX_I1       clickedLeft;
    gX_I1       clickedRight;
    gX_I1       clickedMiddle;
    
    gX_I1       pressedLeft;
    gX_I1       pressedRight;
    gX_I1       pressedMiddle;
    
    gX_I1       draggedLeft;
    gX_I1       draggedRight;
    gX_I1       draggedMiddle;
    
    gX_I1       scrollWheel;
    
} gX_GUI_Element_ExecuteState;

#ifdef __cplusplus
}
#endif

#endif //GUI_ELEMENT_STRUCT_EXECUTESTATE_H
