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

//opencode: clutter/bright-background fallback. The four 3x3 control patterns
//   are rotation/mirror invariant, so a bounded scan can locate them at any
//   orientation. The full-grid quad is extrapolated from their centers and
//   accepted only when the existing decoder validates the payload.
#define GX_COLOR_CODE_CLUTTER_PATTERNS     4
#define GX_COLOR_CODE_CLUTTER_CANDIDATES   8
#define GX_COLOR_CODE_CLUTTER_MIN_CELL     3
#define GX_COLOR_CODE_CLUTTER_MAX_POSITIONS 300000
#define GX_COLOR_CODE_CLUTTER_DECODE_BUDGET 4096

typedef struct gX_ColorCodeClutterCandidate {
    gX_F32 x;
    gX_F32 y;
    gX_U32 score;
} gX_ColorCodeClutterCandidate;

static const gX_U8 s_clutterPatterns[GX_COLOR_CODE_CLUTTER_PATTERNS][9] = {
    {1,1,1, 1,0,1, 1,1,1},
    {0,0,0, 0,1,0, 0,0,0},
    {1,0,1, 0,1,0, 1,0,1},
    {0,1,0, 1,0,1, 0,1,0},
};

static gX_U8 color_code_clutter_luma(const gX_U8 *pixel)
{
    gX_F32 value = 0.2126f * (gX_F32)pixel[0]
        + 0.7152f * (gX_F32)pixel[1]
        + 0.0722f * (gX_F32)pixel[2];
    return (gX_U8)(value + 0.5f);
}

static gX_U32 color_code_clutter_match(const gX_U8 *pixels, gX_U32 width,
    gX_U32 height, gX_U32 x, gX_U32 y, gX_U32 cell, gX_U32 pattern)
{
    if (x + 3 * cell > width || y + 3 * cell > height) return 0;
    gX_U8 values[9];
    gX_U8 minimum = 255;
    gX_U8 maximum = 0;
    for (gX_U32 row = 0; row < 3; row++)
        for (gX_U32 column = 0; column < 3; column++)
        {
            gX_U32 px = x + column * cell + cell / 2;
            gX_U32 py = y + row * cell + cell / 2;
            gX_U8 value = color_code_clutter_luma(
                pixels + (py * width + px) * 4);
            values[row * 3 + column] = value;
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
        }
    if ((gX_U32)maximum - (gX_U32)minimum < 32) return 0;
    gX_U8 threshold = (gX_U8)(((gX_U32)minimum + (gX_U32)maximum) / 2);
    gX_U32 score = 0;
    for (gX_U32 index = 0; index < 9; index++)
        if ((values[index] > threshold ? 1 : 0)
            == s_clutterPatterns[pattern][index])
            score++;
    return score;
}

static gX_I0 color_code_clutter_add(gX_ColorCodeClutterCandidate *list,
    gX_U32 *count, gX_F32 x, gX_F32 y, gX_U32 score, gX_U32 cell)
{
    for (gX_U32 index = 0; index < *count; index++)
    {
        if (fabsf(list[index].x - x) < (gX_F32)cell * 1.5f
            && fabsf(list[index].y - y) < (gX_F32)cell * 1.5f)
        {
            if (score > list[index].score)
            {
                list[index].x = x;
                list[index].y = y;
                list[index].score = score;
            }
            return;
        }
    }
    if (*count < GX_COLOR_CODE_CLUTTER_CANDIDATES)
    {
        list[*count].x = x;
        list[*count].y = y;
        list[*count].score = score;
        (*count)++;
        return;
    }
    gX_U32 worst = 0;
    for (gX_U32 index = 1; index < *count; index++)
        if (list[index].score < list[worst].score) worst = index;
    if (score > list[worst].score)
    {
        list[worst].x = x;
        list[worst].y = y;
        list[worst].score = score;
    }
}

gX_I32 gX_ColorCode_DetectClutter(const gX_U8 *pixels, gX_U32 width,
    gX_U32 height, gX_ColorCodeDetection *detection)
{
    if (!pixels || !detection || width < 32 || height < 32)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    memset(detection, 0, sizeof(*detection));
    snprintf((char *)detection->name, sizeof(detection->name), "%s",
        "Color code clutter detection");
    snprintf((char *)detection->description, sizeof(detection->description),
        "%s", "Corner-pattern full-grid quad");
    gX_U32 maxCell = (width < height ? width : height) / GX_COLOR_CODE_GRID;
    if (maxCell < GX_COLOR_CODE_CLUTTER_MIN_CELL)
    {
        detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
        return detection->status;
    }
    gX_U32 decodes = 0;
    for (gX_U32 cell = GX_COLOR_CODE_CLUTTER_MIN_CELL; cell <= maxCell;
        cell += cell < 16 ? 1 : 2)
    {
        gX_U32 step = cell / 2;
        if (step < 1) step = 1;
        while ((width / step) * (height / step)
                > GX_COLOR_CODE_CLUTTER_MAX_POSITIONS
            && step < cell * 2)
            step++;
        gX_ColorCodeClutterCandidate candidates[GX_COLOR_CODE_CLUTTER_PATTERNS]
            [GX_COLOR_CODE_CLUTTER_CANDIDATES];
        gX_U32 counts[GX_COLOR_CODE_CLUTTER_PATTERNS] = {0};
        for (gX_U32 y = 0; y + 3 * cell <= height; y += step)
            for (gX_U32 x = 0; x + 3 * cell <= width; x += step)
                for (gX_U32 pattern = 0;
                    pattern < GX_COLOR_CODE_CLUTTER_PATTERNS; pattern++)
                {
                    gX_U32 score = color_code_clutter_match(pixels, width,
                        height, x, y, cell, pattern);
                    if (score >= 8)
                        color_code_clutter_add(candidates[pattern],
                            &counts[pattern], (gX_F32)x + (gX_F32)cell * 0.5f,
                            (gX_F32)y + (gX_F32)cell * 0.5f, score, cell);
                }
        if (counts[0] == 0 || counts[1] == 0 || counts[2] == 0
            || counts[3] == 0)
            continue;
        for (gX_U32 a = 0; a < counts[0]; a++)
            for (gX_U32 b = 0; b < counts[1]; b++)
                for (gX_U32 c = 0; c < counts[2]; c++)
                    for (gX_U32 d = 0; d < counts[3]; d++)
                    {
                        if (decodes++ >= GX_COLOR_CODE_CLUTTER_DECODE_BUDGET)
                        {
                            detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
                            return detection->status;
                        }
                        const gX_ColorCodeClutterCandidate *tl = &candidates[0][a];
                        const gX_ColorCodeClutterCandidate *tr = &candidates[1][b];
                        const gX_ColorCodeClutterCandidate *bl = &candidates[2][c];
                        const gX_ColorCodeClutterCandidate *br = &candidates[3][d];
                        gX_F32 exTopX = (tr->x - tl->x) / 38.0f;
                        gX_F32 exTopY = (tr->y - tl->y) / 38.0f;
                        gX_F32 exBottomX = (br->x - bl->x) / 38.0f;
                        gX_F32 exBottomY = (br->y - bl->y) / 38.0f;
                        gX_F32 eyLeftX = (bl->x - tl->x) / 38.0f;
                        gX_F32 eyLeftY = (bl->y - tl->y) / 38.0f;
                        gX_F32 eyRightX = (br->x - tr->x) / 38.0f;
                        gX_F32 eyRightY = (br->y - tr->y) / 38.0f;
                        gX_F32 ux = tr->x - tl->x;
                        gX_F32 uy = tr->y - tl->y;
                        gX_F32 vx = bl->x - tl->x;
                        gX_F32 vy = bl->y - tl->y;
                        gX_F32 uLength = sqrtf(ux * ux + uy * uy);
                        gX_F32 vLength = sqrtf(vx * vx + vy * vy);
                        gX_F32 expectedSide = 38.0f * (gX_F32)cell;
                        if (uLength < expectedSide * 0.6f
                            || uLength > expectedSide * 1.4f
                            || vLength < expectedSide * 0.6f
                            || vLength > expectedSide * 1.4f)
                            continue;
                        if (fabsf(ux * vx + uy * vy)
                            > 0.4f * uLength * vLength)
                            continue;
                        gX_F32 expectedBrX = tr->x + bl->x - tl->x;
                        gX_F32 expectedBrY = tr->y + bl->y - tl->y;
                        gX_F32 brDx = br->x - expectedBrX;
                        gX_F32 brDy = br->y - expectedBrY;
                        if (sqrtf(brDx * brDx + brDy * brDy)
                            > expectedSide * 0.35f)
                            continue;
                        gX_F32 corners[8] = {
                            tl->x - 2.5f * exTopX - 2.5f * eyLeftX,
                            tl->y - 2.5f * exTopY - 2.5f * eyLeftY,
                            tr->x + 4.5f * exTopX - 2.5f * eyRightX,
                            tr->y + 4.5f * exTopY - 2.5f * eyRightY,
                            br->x + 4.5f * exBottomX + 4.5f * eyRightX,
                            br->y + 4.5f * exBottomY + 4.5f * eyRightY,
                            bl->x - 2.5f * exBottomX + 4.5f * eyLeftX,
                            bl->y - 2.5f * exBottomY + 4.5f * eyLeftY,
                        };
                        gX_I1 inside = 1;
                        for (gX_U32 index = 0; index < 8; index += 2)
                            if (!isfinite(corners[index])
                                || !isfinite(corners[index + 1])
                                || corners[index] < 0.0f
                                || corners[index] > (gX_F32)(width - 1)
                                || corners[index + 1] < 0.0f
                                || corners[index + 1] > (gX_F32)(height - 1))
                                inside = 0;
                        if (!inside) continue;
                        gX_F32 area = fabsf(
                            (corners[0] * corners[3] - corners[2] * corners[1])
                            + (corners[2] * corners[5] - corners[4] * corners[3])
                            + (corners[4] * corners[7] - corners[6] * corners[5])
                            + (corners[6] * corners[1] - corners[0] * corners[7]))
                            * 0.5f;
                        if (area < (gX_F32)width * (gX_F32)height * 0.005f)
                            continue;
                        gX_ColorCodeImage image = {0};
                        image.pixels = pixels;
                        image.width = width;
                        image.height = height;
                        image.perspective = 1;
                        image.autoOrientation = 1;
                        memcpy(image.corners, corners, sizeof(corners));
                        gX_ColorCodeResult result;
                        if (gX_ColorCode_Decode(&image, &result)
                            == GX_COLOR_CODE_OK)
                        {
                            memcpy(detection->corners, corners,
                                sizeof(corners));
                            detection->foregroundPixels = 0;
                            detection->status = GX_COLOR_CODE_OK;
                            return GX_COLOR_CODE_OK;
                        }
                    }
    }
    detection->status = GX_COLOR_CODE_ERR_GEOMETRY;
    return detection->status;
}
