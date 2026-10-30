/**********************************************************************************************
*
*   [gX: src/renderer/analog/vhs/vhs.c]
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


//opencode: VHS GPU path — uniform location loading + upload for tape/machine/format shader
#include "vhs.h"

#include <string.h>

#include "../../../wrappers/shader.h"
#include "../../../common/enum.h"

gX_I0 gX_Analog_VHS_LoadLocations(gX_Analog_VHS *vhs, gX_Shader shader) {
    if (shader.id == 0) return;
    vhs->locations.inputTexture      = gX_Shader_GetLocation(shader, "tex0");
    vhs->locations.time              = gX_Shader_GetLocation(shader, "time");
    vhs->locations.res               = gX_Shader_GetLocation(shader, "res");
    vhs->locations.tapeWear          = gX_Shader_GetLocation(shader, "tape_wear");
    vhs->locations.tapeTracking      = gX_Shader_GetLocation(shader, "tape_tracking");
    vhs->locations.tapeGeneration    = gX_Shader_GetLocation(shader, "tape_generation");
    vhs->locations.tapeRecordingMode = gX_Shader_GetLocation(shader, "tape_recording_mode");
    vhs->locations.tapeType          = gX_Shader_GetLocation(shader, "tape_type");
    vhs->locations.machineHeadCondition = gX_Shader_GetLocation(shader, "machine_head_condition");
    vhs->locations.machineMotorNoise    = gX_Shader_GetLocation(shader, "machine_motor_noise");
    vhs->locations.machineDirtyHeads    = gX_Shader_GetLocation(shader, "machine_dirty_heads");
    vhs->locations.machinePlaybackSpeed = gX_Shader_GetLocation(shader, "machine_playback_speed");
    vhs->locations.formatEncoding    = gX_Shader_GetLocation(shader, "format_encoding");
    vhs->locations.formatBleed       = gX_Shader_GetLocation(shader, "format_bleed");
    vhs->locations.formatHifiAudio   = gX_Shader_GetLocation(shader, "format_hifi_audio");
}

gX_I0 gX_Analog_VHS_UploadUniforms(gX_Analog_VHS *vhs, gX_Shader shader, gX_F32 time, gX_F32 res[2]) {
    if (shader.id == 0) return;

    //opencode: always-uploaded uniforms
    if (vhs->locations.time >= 0)
        gX_Shader_SetValue(shader, vhs->locations.time, &time, gX_SHADER_UNIFORM_FLOAT);
    if (vhs->locations.res >= 0)
        gX_Shader_SetValue(shader, vhs->locations.res, res, gX_SHADER_UNIFORM_VEC2);

    //opencode: tape uniforms
    if (vhs->locations.tapeWear >= 0) {
        gX_F32 val = vhs->tape.wear;
        gX_Shader_SetValue(shader, vhs->locations.tapeWear, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.tapeTracking >= 0) {
        gX_F32 val = vhs->tape.tracking;
        gX_Shader_SetValue(shader, vhs->locations.tapeTracking, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.tapeGeneration >= 0) {
        gX_I32 val = vhs->tape.generationLoss;
        gX_Shader_SetValue(shader, vhs->locations.tapeGeneration, &val, gX_SHADER_UNIFORM_INT);
    }
    if (vhs->locations.tapeRecordingMode >= 0) {
        gX_I32 val = vhs->tape.recordingMode;
        gX_Shader_SetValue(shader, vhs->locations.tapeRecordingMode, &val, gX_SHADER_UNIFORM_INT);
    }
    if (vhs->locations.tapeType >= 0) {
        gX_I32 val = vhs->tape.type;
        gX_Shader_SetValue(shader, vhs->locations.tapeType, &val, gX_SHADER_UNIFORM_INT);
    }

    //opencode: machine uniforms
    if (vhs->locations.machineHeadCondition >= 0) {
        gX_F32 val = vhs->machine.headCondition;
        gX_Shader_SetValue(shader, vhs->locations.machineHeadCondition, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.machineMotorNoise >= 0) {
        gX_F32 val = vhs->machine.motorNoise;
        gX_Shader_SetValue(shader, vhs->locations.machineMotorNoise, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.machineDirtyHeads >= 0) {
        gX_F32 val = vhs->machine.dirtyHeads;
        gX_Shader_SetValue(shader, vhs->locations.machineDirtyHeads, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.machinePlaybackSpeed >= 0) {
        gX_F32 val = vhs->machine.playbackSpeed;
        gX_Shader_SetValue(shader, vhs->locations.machinePlaybackSpeed, &val, gX_SHADER_UNIFORM_FLOAT);
    }

    //opencode: format uniforms
    if (vhs->locations.formatEncoding >= 0) {
        gX_I32 val = vhs->format.encoding;
        gX_Shader_SetValue(shader, vhs->locations.formatEncoding, &val, gX_SHADER_UNIFORM_INT);
    }
    if (vhs->locations.formatBleed >= 0) {
        gX_F32 val = vhs->format.colorBleed;
        gX_Shader_SetValue(shader, vhs->locations.formatBleed, &val, gX_SHADER_UNIFORM_FLOAT);
    }
    if (vhs->locations.formatHifiAudio >= 0) {
        gX_I32 val = (gX_I32)vhs->format.hifiAudio;
        gX_Shader_SetValue(shader, vhs->locations.formatHifiAudio, &val, gX_SHADER_UNIFORM_INT);
    }
}
