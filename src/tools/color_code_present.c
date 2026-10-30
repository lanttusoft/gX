/**********************************************************************************************
*
*   [gX: src/tools/color_code_present.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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
**********************************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "common/alloc.h"
#include "common/color_code.h"
#include "common/color_code_tv.h"
#include "common/enum.h"
#include "common/log.h"
#include "wrappers/wrappers.h"

#define GX_COLOR_CODE_PRESENT_CELL 16
#define GX_COLOR_CODE_SHOT_FRAME   10

//ai: static 1:1/fit presentation loop; no animation, so every captured frame is frozen.
gX_I32 gX_ColorCode_Present(const gX_I8 *payload, gX_I1 tv)
{
    if (!payload) return 1;
    gX_U32 length = (gX_U32)strlen((const char *)payload);
    if (length > GX_COLOR_CODE_MAX_PAYLOAD)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR_CODE: payload %u bytes exceeds %d-byte limit",
            length, GX_COLOR_CODE_MAX_PAYLOAD);
        return 1;
    }
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode((const gX_U8 *)payload, length,
        0, 1, &frame);
    if (status != GX_COLOR_CODE_OK)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: encode failed (%d)", status);
        return 1;
    }
    gX_U32 width = tv ? 1024 : GX_COLOR_CODE_GRID * GX_COLOR_CODE_PRESENT_CELL;
    gX_U32 height = tv ? 768 : width;
    gX_U8 *pixels = (gX_U8 *)gX_Calloc(width * height * 4);
    if (!pixels) return 1;
    if (tv)
    {
        gX_F32 corners[8];
        status = gX_ColorCodeTv_Render(&frame, pixels, width, height,
            0x5A17u, corners);
    }
    else
    {
        status = gX_ColorCode_Render(&frame, pixels, width, height, 0, 0,
            GX_COLOR_CODE_PRESENT_CELL);
    }
    if (status != GX_COLOR_CODE_OK)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: render failed (%d)", status);
        gX_Free(pixels);
        return 1;
    }
    gX_Texture texture = gX_Texture_LoadFromData(width, height, pixels);
    gX_Free(pixels);
    if (texture.id == 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: texture upload failed");
        return 1;
    }
    gX_Texture_SetFilter(texture, gX_TEXTURE_FILTER_POINT);
    gX_Window_SetTitle("gX color code - SPACE fit/1:1, F fullscreen, ESC close");
    gX_I1 fit = 1;
    gX_I1 loop = 1;
    gX_U64 frameCount = 0;
    gX_U64 frameBudget = 0;
    const gX_I8 *frames = getenv("GX_FRAMES");
    if (frames && frames[0]) frameBudget = (gX_U64)atoi(frames);
    const gX_I8 *shot = getenv("GX_COLOR_CODE_SHOT");
    while (loop && !gX_Window_ShouldClose())
    {
        if (gX_Key_IsPressed(gX_KEY_ESCAPE)) loop = 0;
        if (gX_Key_IsPressed(gX_KEY_SPACE)) fit = fit ? 0 : 1;
        if (gX_Key_IsPressed(gX_KEY_F)) gX_Window_ToggleFullScreen();
        gX_Drawing_Begin();
        gX_Drawing_ClearBG((gX_Color){0,0,0,255});
        gX_F32 screenWidth = (gX_F32)gX_GetScreenWidth();
        gX_F32 screenHeight = (gX_F32)gX_GetScreenHeight();
        gX_F32 scale = 1.0f;
        if (fit)
        {
            gX_F32 scaleX = screenWidth / (gX_F32)texture.width;
            gX_F32 scaleY = screenHeight / (gX_F32)texture.height;
            scale = scaleX < scaleY ? scaleX : scaleY;
        }
        gX_F32 drawWidth = (gX_F32)texture.width * scale;
        gX_F32 drawHeight = (gX_F32)texture.height * scale;
        gX_Texture_Draw_Params params = {0};
        params.src = (gX_Rectangle){0, 0, (gX_F32)texture.width,
            (gX_F32)texture.height};
        params.dst = (gX_Rectangle){
            (screenWidth - drawWidth) * 0.5f,
            (screenHeight - drawHeight) * 0.5f,
            drawWidth, drawHeight};
        params.tint = (gX_Color){255,255,255,255};
        params.scale = (gX_F32v2){1.0f, 1.0f};
        gX_Texture_Draw(texture, params);
        gX_Drawing_End();
        frameCount++;
        if (shot && shot[0] && frameCount == GX_COLOR_CODE_SHOT_FRAME)
        {
            gX_TakeScreenShot(shot);
            loop = 0;
        }
        if (frameBudget > 0 && frameCount >= frameBudget) loop = 0;
    }
    gX_Texture_Unload(texture);
    return 0;
}
