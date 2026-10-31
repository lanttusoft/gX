/**********************************************************************************************
*
*   [gX: src/common/color_code_tv.c]
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
#include <string.h>

#include "common/alloc.h"
#include "common/color_code.h"
#include "common/color_code_tv.h"
#include "common/log.h"

#define GX_COLOR_CODE_TV_CODE_SIZE (GX_COLOR_CODE_GRID * 16)
#define GX_COLOR_CODE_TV_PI        3.14159265358979323846f

static gX_I32 color_code_tv_homography(const gX_F32 corners[8], gX_F32 matrix[9])
{
    gX_F32 dx1 = corners[2] - corners[4];
    gX_F32 dx2 = corners[6] - corners[4];
    gX_F32 dx3 = corners[0] - corners[2] + corners[4] - corners[6];
    gX_F32 dy1 = corners[3] - corners[5];
    gX_F32 dy2 = corners[7] - corners[5];
    gX_F32 dy3 = corners[1] - corners[3] + corners[5] - corners[7];
    gX_F32 denominator = dx1 * dy2 - dx2 * dy1;
    gX_F32 g = 0.0f;
    gX_F32 h = 0.0f;
    gX_F32 a = 0.0f;
    gX_F32 b = 0.0f;
    gX_F32 d = 0.0f;
    gX_F32 e = 0.0f;
    if (fabsf(dx3) < 0.0001f && fabsf(dy3) < 0.0001f)
    {
        a = corners[2] - corners[0];
        b = corners[6] - corners[0];
        d = corners[3] - corners[1];
        e = corners[7] - corners[1];
    }
    else
    {
        if (fabsf(denominator) < 0.0001f) return GX_COLOR_CODE_ERR_GEOMETRY;
        g = (dx3 * dy2 - dx2 * dy3) / denominator;
        h = (dx1 * dy3 - dx3 * dy1) / denominator;
        a = corners[2] - corners[0] + g * corners[2];
        b = corners[6] - corners[0] + h * corners[6];
        d = corners[3] - corners[1] + g * corners[3];
        e = corners[7] - corners[1] + h * corners[7];
    }
    matrix[0] = a;
    matrix[1] = b;
    matrix[2] = corners[0];
    matrix[3] = d;
    matrix[4] = e;
    matrix[5] = corners[1];
    matrix[6] = g;
    matrix[7] = h;
    matrix[8] = 1.0f;
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_tv_invert(const gX_F32 matrix[9], gX_F32 inverse[9])
{
    gX_F32 a = matrix[0], b = matrix[1], c = matrix[2];
    gX_F32 d = matrix[3], e = matrix[4], f = matrix[5];
    gX_F32 g = matrix[6], h = matrix[7], i = matrix[8];
    gX_F32 determinant = a * (e * i - f * h)
        - b * (d * i - f * g)
        + c * (d * h - e * g);
    if (fabsf(determinant) < 0.000001f) return GX_COLOR_CODE_ERR_GEOMETRY;
    inverse[0] = (e * i - f * h) / determinant;
    inverse[1] = (c * h - b * i) / determinant;
    inverse[2] = (b * f - c * e) / determinant;
    inverse[3] = (f * g - d * i) / determinant;
    inverse[4] = (a * i - c * g) / determinant;
    inverse[5] = (c * d - a * f) / determinant;
    inverse[6] = (d * h - e * g) / determinant;
    inverse[7] = (b * g - a * h) / determinant;
    inverse[8] = (a * e - b * d) / determinant;
    return GX_COLOR_CODE_OK;
}

static gX_U8 color_code_tv_sample(const gX_U8 *code, gX_F32 u, gX_F32 v,
    gX_I32 offsetX, gX_I32 channel)
{
    gX_I32 x = (gX_I32)(u * (gX_F32)GX_COLOR_CODE_TV_CODE_SIZE) + offsetX;
    gX_I32 y = (gX_I32)(v * (gX_F32)GX_COLOR_CODE_TV_CODE_SIZE);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= GX_COLOR_CODE_TV_CODE_SIZE) x = GX_COLOR_CODE_TV_CODE_SIZE - 1;
    if (y >= GX_COLOR_CODE_TV_CODE_SIZE) y = GX_COLOR_CODE_TV_CODE_SIZE - 1;
    gX_U32 index = ((gX_U32)y * GX_COLOR_CODE_TV_CODE_SIZE + (gX_U32)x) * 4;
    return code[index + (gX_U32)channel];
}

static gX_U32 color_code_tv_hash(gX_U32 x, gX_U32 y, gX_U32 seed)
{
    gX_U32 value = x * 374761393u + y * 668265263u + seed * 2246822519u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

gX_I32 gX_ColorCodeTv_Render(const gX_ColorCodeFrame *frame, gX_U8 *pixels,
    gX_U32 width, gX_U32 height, gX_U32 seed, gX_F32 corners[8])
{
    if (!frame || !pixels || !corners || width < 64 || height < 64)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_F32 quad[8] = {
        (gX_F32)width * 0.14f, (gX_F32)height * 0.10f,
        (gX_F32)width * 0.86f, (gX_F32)height * 0.12f,
        (gX_F32)width * 0.84f, (gX_F32)height * 0.88f,
        (gX_F32)width * 0.12f, (gX_F32)height * 0.86f
    };
    memcpy(corners, quad, sizeof(quad));
    gX_U8 *code = (gX_U8 *)gX_Calloc(GX_COLOR_CODE_TV_CODE_SIZE
        * GX_COLOR_CODE_TV_CODE_SIZE * 4);
    if (!code) return GX_COLOR_CODE_ERR_CAPACITY;
    gX_I32 status = gX_ColorCode_Render(frame, code,
        GX_COLOR_CODE_TV_CODE_SIZE, GX_COLOR_CODE_TV_CODE_SIZE,
        0, 0, 16);
    if (status != GX_COLOR_CODE_OK)
    {
        gX_Free(code);
        return status;
    }
    gX_F32 matrix[9];
    gX_F32 inverse[9];
    status = color_code_tv_homography(quad, matrix);
    if (status == GX_COLOR_CODE_OK) status = color_code_tv_invert(matrix, inverse);
    if (status != GX_COLOR_CODE_OK)
    {
        gX_Free(code);
        return status;
    }
    gX_F32 centerX = (gX_F32)width * 0.5f;
    gX_F32 centerY = (gX_F32)height * 0.5f;
    gX_F32 radiusX = centerX > 1.0f ? centerX : 1.0f;
    gX_F32 radiusY = centerY > 1.0f ? centerY : 1.0f;
    for (gX_U32 y = 0; y < height; y++)
    {
        for (gX_U32 x = 0; x < width; x++)
        {
            gX_U32 offset = (y * width + x) * 4;
            pixels[offset + 0] = 15;
            pixels[offset + 1] = 15;
            pixels[offset + 2] = 20;
            pixels[offset + 3] = 255;
            gX_F32 px = (gX_F32)x + 0.5f;
            gX_F32 py = (gX_F32)y + 0.5f;
            gX_F32 w = inverse[6] * px + inverse[7] * py + inverse[8];
            if (fabsf(w) < 0.000001f) continue;
            gX_F32 u = (inverse[0] * px + inverse[1] * py + inverse[2]) / w;
            gX_F32 v = (inverse[3] * px + inverse[4] * py + inverse[5]) / w;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            gX_F32 scanline = 0.78f + 0.22f * cosf(
                (py + (gX_F32)(seed & 7u)) * 2.0f * GX_COLOR_CODE_TV_PI / 3.0f);
            gX_F32 maskR = 0.94f + 0.06f * cosf(
                px * 2.0f * GX_COLOR_CODE_TV_PI / 3.0f);
            gX_F32 maskG = 0.94f + 0.06f * cosf(
                px * 2.0f * GX_COLOR_CODE_TV_PI / 3.0f + 2.0943951f);
            gX_F32 maskB = 0.94f + 0.06f * cosf(
                px * 2.0f * GX_COLOR_CODE_TV_PI / 3.0f + 4.1887902f);
            gX_F32 nx = (px - centerX) / radiusX;
            gX_F32 ny = (py - centerY) / radiusY;
            gX_F32 vignette = 1.0f - 0.20f * (nx * nx + ny * ny);
            if (vignette < 0.70f) vignette = 0.70f;
            gX_F32 noise = ((gX_F32)(color_code_tv_hash(x, y, seed) & 255u)
                / 255.0f - 0.5f) * 8.0f;
            gX_F32 red = (gX_F32)color_code_tv_sample(code, u, v, 1, 0);
            gX_F32 green = (gX_F32)color_code_tv_sample(code, u, v, 0, 1);
            gX_F32 blue = (gX_F32)color_code_tv_sample(code, u, v, -1, 2);
            red = red * scanline * maskR * vignette + noise;
            green = green * scanline * maskG * vignette + noise;
            blue = blue * scanline * maskB * vignette + noise;
            if (red < 0.0f) red = 0.0f;
            if (green < 0.0f) green = 0.0f;
            if (blue < 0.0f) blue = 0.0f;
            if (red > 255.0f) red = 255.0f;
            if (green > 255.0f) green = 255.0f;
            if (blue > 255.0f) blue = 255.0f;
            pixels[offset + 0] = (gX_U8)(red + 0.5f);
            pixels[offset + 1] = (gX_U8)(green + 0.5f);
            pixels[offset + 2] = (gX_U8)(blue + 0.5f);
        }
    }
    gX_Free(code);
    return GX_COLOR_CODE_OK;
}
