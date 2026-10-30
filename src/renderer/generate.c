/**********************************************************************************************
*
*   [gX: src/renderer/generate.c]
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


//opencode: procedural pattern generator renderer — GPU-based fullscreen shader patterns
#include "generate.h"

#include <string.h>
#include <math.h>

#include "../camera.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"
#include "../main.h"
#include "../resource/resource.h"
#include "../wrappers/wrappers.h"

#include "renderer.h"
#include "../rhi/rhi.h"

static gX_I0 generate_gpu_load(gX_I0 *arg) {
    gX_RendererGenerate *gene = (gX_RendererGenerate*)arg;
    { gX_Shader *_rsc = gX_Resource_LoadShaderFrag("generate.frag"); if (_rsc) gene->shader.shader = *_rsc; }
    gX_Shader_GetDefaultLocations(&gene->shader.shader);
    gene->shader.timeLoc   = gX_Shader_GetLocation(gene->shader.shader, "time");
    gene->shader.resLoc    = gX_Shader_GetLocation(gene->shader.shader, "res");
    gene->shader.modeLoc   = gX_Shader_GetLocation(gene->shader.shader, "mode");
    gene->shader.speedLoc  = gX_Shader_GetLocation(gene->shader.shader, "speed");
    gene->shader.scaleLoc  = gX_Shader_GetLocation(gene->shader.shader, "scale");
    gene->shader.color1Loc = gX_Shader_GetLocation(gene->shader.shader, "color1");
    gene->shader.color2Loc = gX_Shader_GetLocation(gene->shader.shader, "color2");
    gene->loadingstate.loaded = true;
}

gX_I32 gX_RendererGenerate_Load(gX_RendererGenerate *gene) {
    gX_TRACELOG_PROLOGUE("RendererGenerate_Load","RENDERER_GENERATE: Loading")
    strcpy(gene->name, "[gX] Generate");
    strcpy(gene->description, "procedural pattern generator");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", gene->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", gene->description);

    gene->loadingstate = (gX_LoadingState){0};
    gene->loadingstate.loadAsap = true;

    gene->state.mode   = GX_GENERATE_MODE_PLASMA;
    gene->state.speed  = 1.0f;
    gene->state.scale  = 1.0f;
    gene->state.color1 = (gX_F32v3){1.0f, 0.3f, 0.0f};
    gene->state.color2 = (gX_F32v3){0.0f, 0.3f, 1.0f};
    gene->state.autoCycle = 0;

    gene->startTime = gX_Time_Get();

    gX_TRACELOG_EPILOGUE("RENDERER_GENERATE: Load done")
    return 0;
}

gX_I32 gX_RendererGenerate_Unload(gX_RendererGenerate *gene) {
    if (!gene->loadingstate.loaded) return 0;
    gX_Shader_Unload(gene->shader.shader);
    gene->loadingstate.loaded = false;
    return 0;
}

gX_I0 gX_RendererGenerate_Update(gX_RendererGenerate *gene) {
    gX_LoadingState_Handler(&gene->loadingstate, generate_gpu_load, NULL, gene);

    // Auto-cycle through modes
    if (gene->state.autoCycle && gene->loadingstate.loaded) {
        gX_F64 elapsed = gX_Time_Get() - gene->startTime;
        gX_I32 newMode = ((gX_I32)(elapsed * 0.25)) % GX_GENERATE_MODE_COUNT;
        if (newMode != gene->state.mode) {
            gene->state.mode = newMode;
        }
    }
}

gX_I0 gX_RendererGenerate_DrawTo(gX_RendererGenerate *gene, gX_RenderTexture *outputRT) {
    if (!gene->loadingstate.loaded) return;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG(gX_BLACK);

        gX_Drawing_BeginShader(gene->shader.shader);

            gX_F32 time = (gX_F32)(gX_Time_Get() - gene->startTime) * gene->state.speed;
            gX_Shader_SetValue(gene->shader.shader, gene->shader.timeLoc, &time, gX_SHADER_UNIFORM_FLOAT);
            gX_F32 res[2] = { (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight };
            gX_Shader_SetValue(gene->shader.shader, gene->shader.resLoc, res, gX_SHADER_UNIFORM_VEC2);
            gX_I32 mode = gene->state.mode;
            gX_Shader_SetValue(gene->shader.shader, gene->shader.modeLoc, &mode, gX_SHADER_UNIFORM_INT);
            gX_F32 speed = gene->state.speed;
            gX_Shader_SetValue(gene->shader.shader, gene->shader.speedLoc, &speed, gX_SHADER_UNIFORM_FLOAT);
            gX_F32 scale = gene->state.scale;
            gX_Shader_SetValue(gene->shader.shader, gene->shader.scaleLoc, &scale, gX_SHADER_UNIFORM_FLOAT);
            gX_F32 c1[3] = { gene->state.color1.x, gene->state.color1.y, gene->state.color1.z };
            gX_Shader_SetValue(gene->shader.shader, gene->shader.color1Loc, c1, gX_SHADER_UNIFORM_VEC3);
            gX_F32 c2[3] = { gene->state.color2.x, gene->state.color2.y, gene->state.color2.z };
            gX_Shader_SetValue(gene->shader.shader, gene->shader.color2Loc, c2, gX_SHADER_UNIFORM_VEC3);

            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
}
