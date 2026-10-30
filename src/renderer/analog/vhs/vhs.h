/**********************************************************************************************
*
*   [gX: src/renderer/analog/vhs/vhs.h]
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


#ifndef ANALOG_VHS_H
#define ANALOG_VHS_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: VHS mode composite — combines tape, machine, format + shader state
#include "../../../common/types.h"
#include "../../../wrappers/shader.h"
#include "tape.h"
#include "machine.h"
#include "format.h"

typedef struct gX_Analog_VHS {
    gX_Analog_VHS_Tape     tape;
    gX_Analog_VHS_Machine  machine;
    gX_Analog_VHS_Format   format;

    //opencode: uniform locations for vhs.frag
    struct {
        gX_I32 inputTexture;
        gX_I32 time;
        gX_I32 res;

        // Tape uniforms
        gX_I32 tapeWear;
        gX_I32 tapeTracking;
        gX_I32 tapeGeneration;
        gX_I32 tapeRecordingMode;
        gX_I32 tapeType;

        // Machine uniforms
        gX_I32 machineHeadCondition;
        gX_I32 machineMotorNoise;
        gX_I32 machineDirtyHeads;
        gX_I32 machinePlaybackSpeed;

        // Format uniforms
        gX_I32 formatEncoding;
        gX_I32 formatBleed;
        gX_I32 formatHifiAudio;
    } locations;
} gX_Analog_VHS;

#define GX_ANALOG_VHS_DEFAULTS \
    (gX_Analog_VHS){ \
        .tape = GX_ANALOG_VHS_TAPE_DEFAULTS, \
        .machine = GX_ANALOG_VHS_MACHINE_DEFAULTS, \
        .format = GX_ANALOG_VHS_FORMAT_DEFAULTS, \
        .locations = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } \
    }

gX_I0 gX_Analog_VHS_LoadLocations(gX_Analog_VHS *vhs, gX_Shader shader);
gX_I0 gX_Analog_VHS_UploadUniforms(gX_Analog_VHS *vhs, gX_Shader shader, gX_F32 time, gX_F32 res[2]);

#ifdef __cplusplus
}
#endif

#endif //ANALOG_VHS_H
