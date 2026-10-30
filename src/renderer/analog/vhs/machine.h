/**********************************************************************************************
*
*   [gX: src/renderer/analog/vhs/machine.h]
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


#ifndef ANALOG_VHS_MACHINE_H
#define ANALOG_VHS_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: VHS machine component — VCR deck state
#include "../../../common/types.h"

typedef struct gX_Analog_VHS_Machine {
    gX_F32  headCondition;  // 0.0 (broken) .. 1.0 (pristine)
    gX_F32  motorNoise;     // 0.0 (steady) .. 1.0 (wild jitter)
    gX_F32  dirtyHeads;     // 0.0 (clean) .. 1.0 (clogged)
    gX_F32  playbackSpeed;  // 0.0 (stopped) .. 1.0 (normal) .. 2.0 (fast)
} gX_Analog_VHS_Machine;

#define GX_ANALOG_VHS_MACHINE_DEFAULTS \
    (gX_Analog_VHS_Machine){ \
        .headCondition = 1.0f, \
        .motorNoise = 0.0f, \
        .dirtyHeads = 0.0f, \
        .playbackSpeed = 1.0f \
    }

#ifdef __cplusplus
}
#endif

#endif //ANALOG_VHS_MACHINE_H
