/**********************************************************************************************
*
*   [gX: src/renderer/lissajous.c]
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


//opencode: Lissajous renderer — stereo XY phase plot
#include "lissajous.h"

#include <math.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../audio/synth.h"
#include "../wrappers/shape.h"

gX_I32 gX_RendererLissajous_Load(gX_RendererLissajous *lis) {
    gX_TRACELOG_PROLOGUE("RendererLissajous_Load", "LISSAJOUS: Loading")
    strcpy(lis->name, "Lissajous");
    strcpy(lis->description, "stereo XY phase plot");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", lis->name);

    lis->rt = gX_RenderTexture_Load(512, 512);
    lis->scale = 1.0f;
    lis->trail = 0.1f;
    lis->color = (gX_Color){0, 200, 255, 220};
    lis->pointCount = 200;
    lis->pointSize = 2.0f;
    lis->loadingstate.loaded = true;

    gX_TRACELOG_EPILOGUE("LISSAJOUS: Loaded")
    return 0;
}

gX_I32 gX_RendererLissajous_Unload(gX_RendererLissajous *lis) {
    if (!lis->loadingstate.loaded) return 0;
    gX_RenderTexture_Unload(lis->rt);
    lis->loadingstate.loaded = false;
    return 0;
}

gX_I0 gX_RendererLissajous_Update(gX_RendererLissajous *lis) {
    if (!lis->loadingstate.loaded) return;
}

gX_I0 gX_RendererLissajous_DrawTo(gX_RendererLissajous *lis, gX_RenderTexture *outputRT) {
    if (!lis->loadingstate.loaded) return;

    gX_U32 count = g_synthScopeCount;
    if (count < 4) return;

    gX_RenderTexture target = outputRT ? *outputRT : lis->rt;
    gX_F32 texW = (gX_F32)target.texture.width;
    gX_F32 texH = (gX_F32)target.texture.height;
    gX_F32 halfW = texW * 0.5f;
    gX_F32 halfH = texH * 0.5f;
    gX_F32 scale = halfW * 0.8f * lis->scale;

    gX_U32 drawCount = lis->pointCount;
    if (drawCount > count / 2) drawCount = count / 2;

    gX_U32 writePos = g_synthScopeWrite;
    gX_U32 start = (writePos + GX_SYNTH_SCOPE_SIZE - count) % GX_SYNTH_SCOPE_SIZE;

    gX_Drawing_BeginTexture(target);
        // Trail: fade previous content instead of full clear
        if (lis->trail < 0.99f) {
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, (gX_U8)(255 * (1.0f - lis->trail))});
        }

        // Draw connected dots forming the Lissajous curve
        gX_F32 prevX = 0, prevY = 0;
        for (gX_U32 i = 0; i < drawCount; i++) {
            gX_U32 idxL = (start + i * 2) % GX_SYNTH_SCOPE_SIZE;
            gX_U32 idxR = (start + i * 2 + 1) % GX_SYNTH_SCOPE_SIZE;

            gX_F32 lVal = g_synthScope[idxL];
            gX_F32 rVal = g_synthScope[idxR];
            if (lVal < -1.0f) lVal = -1.0f;
            if (lVal > 1.0f) lVal = 1.0f;
            if (rVal < -1.0f) rVal = -1.0f;
            if (rVal > 1.0f) rVal = 1.0f;

            gX_F32 x = halfW + lVal * scale;
            gX_F32 y = halfH + rVal * scale;

            if (i > 0) {
                gX_Line line = {
                    .start = { prevX, prevY },
                    .end   = { x, y },
                    .thickness = lis->pointSize
                };
                gX_Line_Draw(line, lis->color);
            }
            prevX = x;
            prevY = y;
        }
    gX_Drawing_EndTexture();
}
