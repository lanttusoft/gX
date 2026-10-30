/**********************************************************************************************
*
*   [gX: src/renderer/analog/vhs/tape.h]
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


#ifndef ANALOG_VHS_TAPE_H
#define ANALOG_VHS_TAPE_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: VHS tape component — physical medium state
#include "../../../common/types.h"

#define GX_ANALOG_VHS_TAPE_TYPE_VHS   0
#define GX_ANALOG_VHS_TAPE_TYPE_SVHS  1
#define GX_ANALOG_VHS_TAPE_TYPE_VHSC  2

#define GX_ANALOG_VHS_TAPE_MODE_SP    0
#define GX_ANALOG_VHS_TAPE_MODE_LP    1
#define GX_ANALOG_VHS_TAPE_MODE_EP    2

typedef struct gX_Analog_VHS_Tape {
    gX_I32  type;           // GX_ANALOG_VHS_TAPE_TYPE_*
    gX_I32  recordingMode;  // GX_ANALOG_VHS_TAPE_MODE_*
    gX_F32  wear;           // 0.0 (new) .. 1.0 (worn out)
    gX_F32  tracking;       // -1.0 .. 1.0 (misalignment)
    gX_I32  generationLoss; // 0 = original, N = Nth generation copy
} gX_Analog_VHS_Tape;

#define GX_ANALOG_VHS_TAPE_DEFAULTS \
    (gX_Analog_VHS_Tape){ \
        .type = GX_ANALOG_VHS_TAPE_TYPE_VHS, \
        .recordingMode = GX_ANALOG_VHS_TAPE_MODE_SP, \
        .wear = 0.0f, \
        .tracking = 0.0f, \
        .generationLoss = 0 \
    }

#ifdef __cplusplus
}
#endif

#endif //ANALOG_VHS_TAPE_H
