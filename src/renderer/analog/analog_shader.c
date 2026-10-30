/**********************************************************************************************
*
*   [gX: src/renderer/analog/analog_shader.c]
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


//opencode: analog.frag GPU path — uniform location loading + upload
#include "analog_shader.h"

#include "../../wrappers/shader.h"
#include "../../common/enum.h"

gX_I0 gX_Analog_LegacyShader_LoadLocations(gX_Analog_LegacyShader *ls, gX_Shader shader) {
    if (shader.id == 0) return;
    ls->locations.inputTexture = gX_Shader_GetLocation(shader, "tex0");
    ls->locations.time         = gX_Shader_GetLocation(shader, "time");
    ls->locations.res          = gX_Shader_GetLocation(shader, "res");
    ls->locations.uniforms     = gX_Shader_GetLocation(shader, "uniforms");
}

gX_I0 gX_Analog_LegacyShader_UploadUniforms(
    gX_Analog_LegacyShader *ls,
    gX_Shader shader,
    gX_F32 time,
    gX_F32 res[2],
    gX_F32 uniforms[10])
{
    if (shader.id == 0) return;

    if (ls->locations.time >= 0)
        gX_Shader_SetValue(shader, ls->locations.time, &time, gX_SHADER_UNIFORM_FLOAT);
    if (ls->locations.res >= 0)
        gX_Shader_SetValue(shader, ls->locations.res, res, gX_SHADER_UNIFORM_VEC2);
    if (ls->locations.uniforms >= 0)
        gX_Shader_SetValue_Array(shader, ls->locations.uniforms, uniforms, gX_SHADER_UNIFORM_FLOAT, 10);
    // tex0 is bound externally via gX_Shader_SetValue_Texture
}
