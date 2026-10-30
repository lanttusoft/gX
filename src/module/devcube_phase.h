/**********************************************************************************************
*
*   [gX: src/module/devcube_phase.h]
*   DevCube 60 Hz wall-clock phase — shared by the module and its headless test
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


//ai: the devcube is the engine avatar: its phase is quantized to a monotonic
//    60 Hz clock, never to render dt, render scale, or connection-quality lag.
#ifndef GX_MODULE_DEVCUBE_PHASE_H
#define GX_MODULE_DEVCUBE_PHASE_H

#include "../common/types.h"

#define GX_DEVCUBE_HZ              60
#define GX_DEVCUBE_NS_PER_SECOND   1000000000LL

//ai: elapsed monotonic nanoseconds -> nearest 60 Hz tick. The half-tick
//    rounding keeps exact render boundaries stable (20 fps = 3 ticks/frame,
//    30 fps = 2, 60 fps = 1) without accumulating frame-time error.
static inline gX_I64 gX_DevCube_PhaseTicks(gX_I64 elapsedNs)
{
    if (elapsedNs <= 0) return 0;
    return (elapsedNs * GX_DEVCUBE_HZ
        + GX_DEVCUBE_NS_PER_SECOND / 2) / GX_DEVCUBE_NS_PER_SECOND;
}

//ai: phase in seconds at the 60 Hz tick rate (what the avatar animates from).
static inline gX_F64 gX_DevCube_PhaseSeconds(gX_I64 elapsedNs)
{
    return (gX_F64)gX_DevCube_PhaseTicks(elapsedNs) / (gX_F64)GX_DEVCUBE_HZ;
}

#endif //GX_MODULE_DEVCUBE_PHASE_H
