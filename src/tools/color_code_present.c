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

#include <stdio.h>
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
#define GX_COLOR_CODE_FRAME_TICKS  60

static gX_I32 color_code_present_render(const gX_ColorCodeFrame *frame,
    gX_I1 tv, gX_U8 *pixels, gX_U32 width, gX_U32 height)
{
    if (tv)
    {
        gX_F32 corners[8];
        return gX_ColorCodeTv_Render(frame, pixels, width, height,
            0x5A17u, corners);
    }
    return gX_ColorCode_Render(frame, pixels, width, height, 0, 0,
        GX_COLOR_CODE_PRESENT_CELL);
}

static gX_I0 color_code_present_title(gX_U32 frame, gX_U32 count)
{
    gX_I8 title[192];
    if (count > 1)
        snprintf((char *)title, sizeof(title),
            "gX color code %u/%u - SPACE fit, P pause, LEFT/RIGHT step, F fullscreen, ESC close",
            frame + 1, count);
    else
        snprintf((char *)title, sizeof(title),
            "gX color code - SPACE fit/1:1, F fullscreen, ESC close");
    gX_Window_SetTitle(title);
}

//ai: static 1:1/fit presentation loop; multi-frame payloads cycle once per second.
gX_I32 gX_ColorCode_Present(const gX_I8 *payload, gX_I1 tv)
{
    if (!payload) return 1;
    gX_U32 length = (gX_U32)strlen((const char *)payload);
    if (length > GX_COLOR_CODE_MAX_MULTI_PAYLOAD)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR_CODE: payload %u bytes exceeds %d-byte limit",
            length, GX_COLOR_CODE_MAX_MULTI_PAYLOAD);
        return 1;
    }
    gX_ColorCodeFrame *frames = (gX_ColorCodeFrame *)gX_Calloc(
        sizeof(gX_ColorCodeFrame) * GX_COLOR_CODE_MAX_MULTI_FRAMES);
    if (!frames) return 1;
    gX_U32 frameCount = 0;
    gX_I32 status = gX_ColorCode_EncodeMulti((const gX_U8 *)payload, length,
        frames, GX_COLOR_CODE_MAX_MULTI_FRAMES, &frameCount);
    if (status != GX_COLOR_CODE_OK || frameCount == 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: encode failed (%d)", status);
        gX_Free(frames);
        return 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "COLOR_CODE: %u frame(s)", frameCount);
    gX_U32 width = tv ? 1024 : GX_COLOR_CODE_GRID * GX_COLOR_CODE_PRESENT_CELL;
    gX_U32 height = tv ? 768 : width;
    gX_U8 *pixels = (gX_U8 *)gX_Calloc(width * height * 4);
    if (!pixels)
    {
        gX_Free(frames);
        return 1;
    }
    status = color_code_present_render(&frames[0], tv, pixels, width, height);
    if (status != GX_COLOR_CODE_OK)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: render failed (%d)", status);
        gX_Free(pixels);
        gX_Free(frames);
        return 1;
    }
    gX_Texture texture = gX_Texture_LoadFromData(width, height, pixels);
    if (texture.id == 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR_CODE: texture upload failed");
        gX_Free(pixels);
        gX_Free(frames);
        return 1;
    }
    gX_Texture_SetFilter(texture, gX_TEXTURE_FILTER_POINT);
    gX_U32 currentFrame = 0;
    gX_I1 paused = frameCount <= 1;
    gX_I1 fit = 1;
    gX_I1 loop = 1;
    gX_U64 frameTick = 0;
    gX_U64 loopCount = 0;
    gX_U64 frameBudget = 0;
    const gX_I8 *framesBudget = getenv("GX_FRAMES");
    if (framesBudget && framesBudget[0])
        frameBudget = (gX_U64)atoi(framesBudget);
    const gX_I8 *shot = getenv("GX_COLOR_CODE_SHOT");
    gX_U64 shotFrame = GX_COLOR_CODE_SHOT_FRAME;
    const gX_I8 *shotFrameEnv = getenv("GX_COLOR_CODE_SHOT_FRAME");
    if (shotFrameEnv && shotFrameEnv[0])
    {
        gX_I32 parsed = atoi(shotFrameEnv);
        if (parsed > 0) shotFrame = (gX_U64)parsed;
    }
    color_code_present_title(currentFrame, frameCount);
    while (loop && !gX_Window_ShouldClose())
    {
        if (gX_Key_IsPressed(gX_KEY_ESCAPE)) loop = 0;
        if (gX_Key_IsPressed(gX_KEY_SPACE)) fit = fit ? 0 : 1;
        if (gX_Key_IsPressed(gX_KEY_F)) gX_Window_ToggleFullScreen();
        if (gX_Key_IsPressed(gX_KEY_P) && frameCount > 1)
        {
            paused = paused ? 0 : 1;
            color_code_present_title(currentFrame, frameCount);
        }
        if (gX_Key_IsPressed(gX_KEY_LEFT) && frameCount > 1)
        {
            paused = 1;
            currentFrame = (currentFrame + frameCount - 1) % frameCount;
            status = color_code_present_render(&frames[currentFrame], tv,
                pixels, width, height);
            if (status == GX_COLOR_CODE_OK)
                gX_Texture_Update(texture, pixels);
            color_code_present_title(currentFrame, frameCount);
        }
        if (gX_Key_IsPressed(gX_KEY_RIGHT) && frameCount > 1)
        {
            paused = 1;
            currentFrame = (currentFrame + 1) % frameCount;
            status = color_code_present_render(&frames[currentFrame], tv,
                pixels, width, height);
            if (status == GX_COLOR_CODE_OK)
                gX_Texture_Update(texture, pixels);
            color_code_present_title(currentFrame, frameCount);
        }
        if (frameCount > 1 && !paused && frameTick > 0
            && frameTick % GX_COLOR_CODE_FRAME_TICKS == 0)
        {
            currentFrame = (currentFrame + 1) % frameCount;
            status = color_code_present_render(&frames[currentFrame], tv,
                pixels, width, height);
            if (status == GX_COLOR_CODE_OK)
                gX_Texture_Update(texture, pixels);
            color_code_present_title(currentFrame, frameCount);
            gX_TraceLog(gX_LOG_NOTICE, "COLOR_CODE: frame %u/%u",
                currentFrame + 1, frameCount);
        }
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
        loopCount++;
        frameTick++;
        if (shot && shot[0] && loopCount == shotFrame)
        {
            gX_TakeScreenShot(shot);
            loop = 0;
        }
        if (frameBudget > 0 && loopCount >= frameBudget) loop = 0;
    }
    gX_Texture_Unload(texture);
    gX_Free(pixels);
    gX_Free(frames);
    return 0;
}
