/**********************************************************************************************
*
*   [gX: src/gui/utils.c]
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


#include "utils.h"

gX_I0 gX_GUI_Element_ExecuteState_Update(gX_GUI_Element_ExecuteState *es, gX_GUI_Element *element, gX_I1 *execute) {
    
    *execute = false;
    
    if (es->hovered        && element->state.HOVERED)          { *execute = true; }
    else if (es->toggled   && element->state.TOGGLED)          { *execute = true; }
    else if (es->clickedLeft    && element->state.CLICKED_LEFT)    { *execute = true; }
    else if (es->clickedRight   && element->state.CLICKED_RIGHT)   { *execute = true; }
    else if (es->clickedMiddle  && element->state.CLICKED_MIDDLE)  { *execute = true; }
    else if (es->pressedLeft    && element->state.PRESSED_LEFT)    { *execute = true; }
    else if (es->pressedRight   && element->state.PRESSED_RIGHT)   { *execute = true; }
    else if (es->pressedMiddle  && element->state.PRESSED_MIDDLE)  { *execute = true; }
    else if (es->draggedLeft    && element->state.DRAGGED_LEFT)    { *execute = true; }
    else { es->done = false; }
    
    if (es->once && es->done) { *execute = false; }
    
}
