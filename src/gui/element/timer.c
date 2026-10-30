/**********************************************************************************************
*
*   [gX: src/gui/element/timer.c]
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


#include <time.h>
#include <string.h>

#include "element.h"
#include "../../wrappers/wrappers.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "../canvas.h"
#include "modules.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../main.h"

inline gX_I0 gX_GUI_Element_Timer_Init(gX_GUI_Element_Timer *timer) {
    timer->mode = gX_GUI_TIMER_ONESHOT;
    timer->duration = 1.0f;
    timer->elapsed = 0.0f;
    timer->paused = false;
    timer->done = false;
    timer->onFire = NULL;
    timer->onFireUserData = NULL;
}

static gX_I0 gX_GUI_Element_Timer_Update(gX_GUI_Element_Timer *timer) {
    if (timer->paused || timer->done) return;
    if (timer->duration <= 0.0f) return;

    //opencode: v0.2.0-pre9 — GetFrameTime() can read 0 depending on when the
    //   poll happens relative to raylib's timing mark; fall back to a shared
    //   monotonic wall delta so repeating timers still advance.
    {
        static gX_F64 prevWall = 0.0;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        gX_F64 wall = (gX_F64)now.tv_sec + (gX_F64)now.tv_nsec / 1e9;
        gX_F32 dt = (gX_F32)gX_FrameTime_Get();
        if (dt <= 0.0f && prevWall > 0.0) dt = (gX_F32)(wall - prevWall);
        prevWall = wall;
        timer->elapsed += dt;
    }
    if (timer->elapsed >= timer->duration) {
        if (timer->onFire) {
            timer->onFire(timer->onFireUserData);
        }
        switch (timer->mode) {
            case gX_GUI_TIMER_ONESHOT:
            case gX_GUI_TIMER_COUNTDOWN:
                timer->done = true;
                break;
            case gX_GUI_TIMER_REPEATING:
                timer->elapsed = 0.0f;
                break;
        }
    }
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Timer,timer)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Timer,timer)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Timer,timer,TIMER)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Timer,timer)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Timer,timer)

gX_I0 gX_GUI_Element_Update_Timers(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->timers;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Timer *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Timer *t = &heapPtr[i];
            gX_GUI_Element_Timer_Update(t);
        }
    }
}
