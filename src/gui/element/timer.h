/**********************************************************************************************
*
*   [gX: src/gui/element/timer.h]
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


#ifndef GUI_ELEMENT_TIMER_H
#define GUI_ELEMENT_TIMER_H

#include "../../common/types.h"
#include "struct_executestate.h"

typedef enum {
    gX_GUI_TIMER_ONESHOT = 0,
    gX_GUI_TIMER_REPEATING,
    gX_GUI_TIMER_COUNTDOWN,
} gX_GUI_Timer_Mode;

typedef struct gX_GUI_Element_Timer {
    gX_GUI_Timer_Mode mode;
    gX_F32            duration;
    gX_F32            elapsed;
    gX_I1             paused;
    gX_I1             done;

    gX_GUI_Element_ExecuteState execute;
    gX_I0 (*onFire)(gX_I0 *userData);
    gX_I0 *onFireUserData;
} gX_GUI_Element_Timer;

#endif
