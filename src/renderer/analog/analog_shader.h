/**********************************************************************************************
*
*   [gX: src/renderer/analog/analog_shader.h]
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


#ifndef ANALOG_ANALOG_SHADER_H
#define ANALOG_ANALOG_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: analog.frag GPU path — uniform locations and upload
#include "../../common/types.h"
#include "../../wrappers/shader.h"

typedef struct gX_Analog_LegacyShader {
    struct {
        gX_I32 inputTexture;
        gX_I32 time;
        gX_I32 res;
        gX_I32 uniforms; // uniforms[10] array
    } locations;
} gX_Analog_LegacyShader;

#define GX_ANALOG_LEGACY_SHADER_DEFAULTS \
    (gX_Analog_LegacyShader){ \
        .locations = { -1, -1, -1, -1 } \
    }

gX_I0 gX_Analog_LegacyShader_LoadLocations(gX_Analog_LegacyShader *ls, gX_Shader shader);
gX_I0 gX_Analog_LegacyShader_UploadUniforms(gX_Analog_LegacyShader *ls, gX_Shader shader, gX_F32 time, gX_F32 res[2], gX_F32 uniforms[10]);

#ifdef __cplusplus
}
#endif

#endif //ANALOG_ANALOG_SHADER_H
