/**********************************************************************************************
*
*   [gX: src/common/color_code_detect.c]
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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common/color_code_detect.h"
#include "common/log.h"

#define GX_COLOR_CODE_DETECT_DISTANCE 12.0f

static gX_F32 color_code_detect_distance(const gX_U8 *pixel,
    const gX_F32 background[3])
{
    gX_F32 red = fabsf((gX_F32)pixel[0] - background[0]);
    gX_F32 green = fabsf((gX_F32)pixel[1] - background[1]);
    gX_F32 blue = fabsf((gX_F32)pixel[2] - background[2]);
    gX_F32 distance = red > green ? red : green;
    return distance > blue ? distance : blue;
}

gX_I32 gX_ColorCode_Detect(const gX_U8 *pixels, gX_U32 width, gX_U32 height,
    gX_ColorCodeDetection *detection)
{
    if (!pixels || !detection || width < 32 || height < 32)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    memset(detection, 0, sizeof(*detection));
    snprintf((char *)detection->name, sizeof(detection->name), "%s",
        "Color code detection");
    snprintf((char *)detection->description, sizeof(detection->description),
        "%s", "Background-distance full-grid quad");
    gX_F64 background[3] = {0.0};
    gX_U32 borderCount = 0;
    for (gX_U32 y = 0; y < height; y++)
        for (gX_U32 x = 0; x < width; x++)
        {
            if (x > 1 && x + 2 < width && y > 1 && y + 2 < height) continue;
            gX_U32 offset = (y * width + x) * 4;
            background[0] += pixels[offset + 0];
            background[1] += pixels[offset + 1];
            background[2] += pixels[offset + 2];
            borderCount++;
        }
    if (borderCount == 0)
    {
        detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
        return detection->status;
    }
    gX_F32 base[3] = {
        (gX_F32)(background[0] / (gX_F64)borderCount),
        (gX_F32)(background[1] / (gX_F64)borderCount),
        (gX_F32)(background[2] / (gX_F64)borderCount)
    };
    gX_U32 minSum = 0xFFFFFFFFu;
    gX_I32 maxDiff = -0x7FFFFFFF;
    gX_U32 maxSum = 0;
    gX_I32 minDiff = 0x7FFFFFFF;
    gX_U32 tlX = 0, tlY = 0, trX = 0, trY = 0;
    gX_U32 brX = 0, brY = 0, blX = 0, blY = 0;
    gX_U32 foreground = 0;
    for (gX_U32 y = 0; y < height; y++)
        for (gX_U32 x = 0; x < width; x++)
        {
            gX_U32 offset = (y * width + x) * 4;
            if (color_code_detect_distance(pixels + offset, base)
                <= GX_COLOR_CODE_DETECT_DISTANCE) continue;
            foreground++;
            gX_U32 sum = x + y;
            gX_I32 difference = (gX_I32)x - (gX_I32)y;
            if (sum < minSum)
            {
                minSum = sum;
                tlX = x;
                tlY = y;
            }
            if (difference > maxDiff)
            {
                maxDiff = difference;
                trX = x;
                trY = y;
            }
            if (sum > maxSum)
            {
                maxSum = sum;
                brX = x;
                brY = y;
            }
            if (difference < minDiff)
            {
                minDiff = difference;
                blX = x;
                blY = y;
            }
        }
    detection->foregroundPixels = foreground;
    if (foreground < 100)
    {
        detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
        return detection->status;
    }
    gX_F32 corners[8] = {
        (gX_F32)tlX, (gX_F32)tlY,
        (gX_F32)trX, (gX_F32)trY,
        (gX_F32)brX, (gX_F32)brY,
        (gX_F32)blX, (gX_F32)blY
    };
    gX_F32 area = fabsf(
        (corners[0] * corners[3] - corners[2] * corners[1])
        + (corners[2] * corners[5] - corners[4] * corners[3])
        + (corners[4] * corners[7] - corners[6] * corners[5])
        + (corners[6] * corners[1] - corners[0] * corners[7])) * 0.5f;
    if (area < (gX_F32)width * (gX_F32)height * 0.005f)
    {
        detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
        return detection->status;
    }
    memcpy(detection->corners, corners, sizeof(corners));
    detection->status = GX_COLOR_CODE_OK;
    return GX_COLOR_CODE_OK;
}
