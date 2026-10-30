/**********************************************************************************************
*
*   [gX: src/renderer/oscillo.c]
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


//opencode: oscilloscope renderer — draws synth scope buffer as polyline
#include "oscillo.h"

#include <math.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../audio/synth.h"
#include "../wrappers/shape.h"

gX_I32 gX_RendererOscillo_Load(gX_RendererOscillo *osc) {
    gX_TRACELOG_PROLOGUE("RendererOscillo_Load", "OSCILLO: Loading")
    strcpy(osc->name, "Oscilloscope");
    strcpy(osc->description, "waveform display");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", osc->name);

    osc->rt = gX_RenderTexture_Load(512, 256);
    osc->lineColor = (gX_Color){0, 220, 100, 220};
    osc->lineWidth = 2.0f;
    osc->pointCount = 512;
    osc->ampScale = 1.0f;
    osc->trigger = 0;
    osc->triggerLevel = 0.0f;
    osc->loadingstate.loaded = true;

    gX_TRACELOG_EPILOGUE("OSCILLO: Loaded")
    return 0;
}

gX_I32 gX_RendererOscillo_Unload(gX_RendererOscillo *osc) {
    if (!osc->loadingstate.loaded) return 0;
    gX_RenderTexture_Unload(osc->rt);
    osc->loadingstate.loaded = false;
    return 0;
}

gX_I0 gX_RendererOscillo_Update(gX_RendererOscillo *osc) {
    if (!osc->loadingstate.loaded) return;
}

gX_I0 gX_RendererOscillo_DrawTo(gX_RendererOscillo *osc, gX_RenderTexture *outputRT) {
    if (!osc->loadingstate.loaded) return;
    if (g_synthScopeCount < 2) return;

    gX_U32 count = g_synthScopeCount;
    gX_U32 writePos = g_synthScopeWrite;
    gX_U32 start = (writePos + GX_SYNTH_SCOPE_SIZE - count) % GX_SYNTH_SCOPE_SIZE;

    gX_RenderTexture target = outputRT ? *outputRT : osc->rt;
    gX_F32 texW = (gX_F32)target.texture.width;
    gX_F32 texH = (gX_F32)target.texture.height;
    gX_F32 halfH = texH * 0.5f;
    gX_F32 scale = halfH * 0.9f * osc->ampScale;

    gX_U32 drawCount = osc->pointCount;
    if (drawCount > count) drawCount = count;
    if (drawCount < 2) return;

    gX_U32 step = count / drawCount;
    gX_F32 stepX = texW / (gX_F32)(drawCount - 1);

    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});

        gX_F32 prevY = halfH;
        for (gX_U32 i = 0; i < drawCount; i++) {
            gX_U32 idx = (start + i * step) % GX_SYNTH_SCOPE_SIZE;
            gX_F32 val = g_synthScope[idx];
            if (val < -1.0f) val = -1.0f;
            if (val > 1.0f) val = 1.0f;
            gX_F32 y = halfH - val * scale;

            if (i > 0) {
                gX_Line line = {
                    .start = { (gX_F32)(i - 1) * stepX, prevY },
                    .end   = { (gX_F32)i * stepX, y },
                    .thickness = osc->lineWidth
                };
                gX_Line_Draw(line, osc->lineColor);
            }
            prevY = y;
        }
    gX_Drawing_EndTexture();
}
