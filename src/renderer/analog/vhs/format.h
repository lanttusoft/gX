/**********************************************************************************************
*
*   [gX: src/renderer/analog/vhs/format.h]
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


#ifndef ANALOG_VHS_FORMAT_H
#define ANALOG_VHS_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: VHS format component — recording/encoding standard
#include "../../../common/types.h"

#define GX_ANALOG_VHS_FORMAT_ENCODING_NTSC  0
#define GX_ANALOG_VHS_FORMAT_ENCODING_PAL   1
#define GX_ANALOG_VHS_FORMAT_ENCODING_SECAM 2

typedef struct gX_Analog_VHS_Format {
    gX_I32  encoding;    // GX_ANALOG_VHS_FORMAT_ENCODING_*
    gX_I1   hifiAudio;   // Hi-Fi audio tracking affects video
    gX_F32  colorBleed;  // 0.0 (crisp) .. 1.0 (smear)
} gX_Analog_VHS_Format;

#define GX_ANALOG_VHS_FORMAT_DEFAULTS \
    (gX_Analog_VHS_Format){ \
        .encoding = GX_ANALOG_VHS_FORMAT_ENCODING_NTSC, \
        .hifiAudio = 0, \
        .colorBleed = 0.3f \
    }

#ifdef __cplusplus
}
#endif

#endif //ANALOG_VHS_FORMAT_H
