/**********************************************************************************************
*
*   [gX: src/common/color_code_tests.c]
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

#define TEST_WIDTH  408
#define TEST_HEIGHT 408
#define TEST_ORIGIN 24
#define TEST_CELL   8

static gX_U8 *test_image(const gX_ColorCodeFrame *frame, gX_F32 gain,
    gX_F32 bias)
{
    gX_U8 *pixels = (gX_U8 *)gX_Calloc(TEST_WIDTH * TEST_HEIGHT * 4);
    if (!pixels) return NULL;
    if (gX_ColorCode_Render(frame, pixels, TEST_WIDTH, TEST_HEIGHT,
        TEST_ORIGIN, TEST_ORIGIN, TEST_CELL) != GX_COLOR_CODE_OK)
    {
        gX_Free(pixels);
        return NULL;
    }
    for (gX_U32 index = 0; index < TEST_WIDTH * TEST_HEIGHT; index++)
    {
        for (gX_U32 channel = 0; channel < 3; channel++)
        {
            gX_F32 value = (gX_F32)pixels[index * 4 + channel] * gain
                + bias + (gX_F32)((index + channel * 7) % 5);
            if (value < 0.0f) value = 0.0f;
            if (value > 255.0f) value = 255.0f;
            pixels[index * 4 + channel] = (gX_U8)(value + 0.5f);
        }
        pixels[index * 4 + 3] = 255;
    }
    return pixels;
}

static gX_I32 test_decode(const gX_U8 *pixels, gX_ColorCodeResult *result)
{
    gX_ColorCodeImage image = {0};
    image.pixels = pixels;
    image.width = TEST_WIDTH;
    image.height = TEST_HEIGHT;
    image.originX = TEST_ORIGIN;
    image.originY = TEST_ORIGIN;
    image.cellSize = TEST_CELL;
    return gX_ColorCode_Decode(&image, result);
}

static gX_I32 test_decode_auto(const gX_U8 *pixels, gX_ColorCodeResult *result)
{
    gX_ColorCodeImage image = {0};
    image.pixels = pixels;
    image.width = TEST_WIDTH;
    image.height = TEST_HEIGHT;
    image.originX = TEST_ORIGIN;
    image.originY = TEST_ORIGIN;
    image.cellSize = TEST_CELL;
    image.autoOrientation = 1;
    return gX_ColorCode_Decode(&image, result);
}

static gX_U8 *test_transform(const gX_U8 *source, gX_I32 mode)
{
    gX_U8 *target = (gX_U8 *)gX_Calloc(TEST_WIDTH * TEST_HEIGHT * 4);
    if (!target) return NULL;
    for (gX_U32 y = 0; y < TEST_HEIGHT; y++)
        for (gX_U32 x = 0; x < TEST_WIDTH; x++)
        {
            gX_U32 targetX = x;
            gX_U32 targetY = y;
            switch (mode)
            {
                case 1:
                    targetX = TEST_WIDTH - 1 - y;
                    targetY = x;
                    break;
                case 2:
                    targetX = TEST_WIDTH - 1 - x;
                    targetY = TEST_HEIGHT - 1 - y;
                    break;
                case 3:
                    targetX = y;
                    targetY = TEST_WIDTH - 1 - x;
                    break;
                case 4:
                    targetX = TEST_WIDTH - 1 - x;
                    break;
                case 5:
                    targetY = TEST_HEIGHT - 1 - y;
                    break;
                case 6:
                    targetX = y;
                    targetY = x;
                    break;
                case 7:
                    targetX = TEST_WIDTH - 1 - y;
                    targetY = TEST_HEIGHT - 1 - x;
                    break;
                default:
                    break;
            }
            gX_U32 sourceOffset = (y * TEST_WIDTH + x) * 4;
            gX_U32 targetOffset = (targetY * TEST_WIDTH + targetX) * 4;
            memcpy(target + targetOffset, source + sourceOffset, 4);
        }
    return target;
}

static gX_U8 *test_warp_affine(const gX_U8 *source)
{
    const gX_U32 size = 512;
    const gX_F32 origin = (gX_F32)TEST_ORIGIN;
    const gX_F32 span = (gX_F32)(GX_COLOR_CODE_GRID * TEST_CELL);
    const gX_F32 x0 = 60.0f, y0 = 40.0f;
    const gX_F32 x1 = 440.0f, y1 = 80.0f;
    const gX_F32 x3 = 20.0f, y3 = 420.0f;
    const gX_F32 a = x1 - x0, b = x3 - x0;
    const gX_F32 d = y1 - y0, e = y3 - y0;
    const gX_F32 determinant = a * e - b * d;
    gX_U8 *target = (gX_U8 *)gX_Calloc(size * size * 4);
    if (!target || fabsf(determinant) < 0.0001f)
    {
        gX_Free(target);
        return NULL;
    }
    for (gX_U32 y = 0; y < size; y++)
        for (gX_U32 x = 0; x < size; x++)
        {
            gX_F32 px = (gX_F32)x + 0.5f - x0;
            gX_F32 py = (gX_F32)y + 0.5f - y0;
            gX_F32 u = (e * px - b * py) / determinant;
            gX_F32 v = (-d * px + a * py) / determinant;
            gX_U32 offset = (y * size + x) * 4;
            target[offset + 3] = 255;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            gX_I32 sourceX = (gX_I32)(origin + u * span);
            gX_I32 sourceY = (gX_I32)(origin + v * span);
            if (sourceX < 0 || sourceX >= TEST_WIDTH
                || sourceY < 0 || sourceY >= TEST_HEIGHT) continue;
            gX_U32 sourceOffset = ((gX_U32)sourceY * TEST_WIDTH
                + (gX_U32)sourceX) * 4;
            memcpy(target + offset, source + sourceOffset, 4);
        }
    return target;
}

static gX_I32 test_homography(const gX_F32 corners[8], gX_F32 matrix[9])
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
        if (fabsf(denominator) < 0.0001f) return 1;
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
    return 0;
}

static gX_I32 test_invert(const gX_F32 matrix[9], gX_F32 inverse[9])
{
    gX_F32 a = matrix[0], b = matrix[1], c = matrix[2];
    gX_F32 d = matrix[3], e = matrix[4], f = matrix[5];
    gX_F32 g = matrix[6], h = matrix[7], i = matrix[8];
    gX_F32 determinant = a * (e * i - f * h)
        - b * (d * i - f * g)
        + c * (d * h - e * g);
    if (fabsf(determinant) < 0.000001f) return 1;
    inverse[0] = (e * i - f * h) / determinant;
    inverse[1] = (c * h - b * i) / determinant;
    inverse[2] = (b * f - c * e) / determinant;
    inverse[3] = (f * g - d * i) / determinant;
    inverse[4] = (a * i - c * g) / determinant;
    inverse[5] = (c * d - a * f) / determinant;
    inverse[6] = (d * h - e * g) / determinant;
    inverse[7] = (b * g - a * h) / determinant;
    inverse[8] = (a * e - b * d) / determinant;
    return 0;
}

static gX_U8 *test_warp_perspective(const gX_U8 *source)
{
    const gX_U32 size = 512;
    const gX_F32 origin = (gX_F32)TEST_ORIGIN;
    const gX_F32 span = (gX_F32)(GX_COLOR_CODE_GRID * TEST_CELL);
    const gX_F32 corners[8] = {
        90.0f, 50.0f, 420.0f, 70.0f, 430.0f, 450.0f, 60.0f, 430.0f};
    gX_F32 matrix[9];
    gX_F32 inverse[9];
    if (test_homography(corners, matrix) || test_invert(matrix, inverse))
        return NULL;
    gX_U8 *target = (gX_U8 *)gX_Calloc(size * size * 4);
    if (!target) return NULL;
    for (gX_U32 y = 0; y < size; y++)
        for (gX_U32 x = 0; x < size; x++)
        {
            gX_F32 px = (gX_F32)x + 0.5f;
            gX_F32 py = (gX_F32)y + 0.5f;
            gX_F32 w = inverse[6] * px + inverse[7] * py + inverse[8];
            gX_U32 offset = (y * size + x) * 4;
            target[offset + 3] = 255;
            if (fabsf(w) < 0.000001f) continue;
            gX_F32 u = (inverse[0] * px + inverse[1] * py + inverse[2]) / w;
            gX_F32 v = (inverse[3] * px + inverse[4] * py + inverse[5]) / w;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            gX_I32 sourceX = (gX_I32)(origin + u * span);
            gX_I32 sourceY = (gX_I32)(origin + v * span);
            if (sourceX < 0 || sourceX >= TEST_WIDTH
                || sourceY < 0 || sourceY >= TEST_HEIGHT) continue;
            gX_U32 sourceOffset = ((gX_U32)sourceY * TEST_WIDTH
                + (gX_U32)sourceX) * 4;
            memcpy(target + offset, source + sourceOffset, 4);
        }
    return target;
}

static gX_I32 test_perspective(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U8 *source = test_image(&frame, 0.90f, 9.0f);
    if (!source) return 1;
    gX_U8 *warped = test_warp_affine(source);
    gX_Free(source);
    if (!warped) return 1;
    gX_ColorCodeImage image = {0};
    image.pixels = warped;
    image.width = 512;
    image.height = 512;
    image.perspective = 1;
    image.corners[0] = 60.0f;
    image.corners[1] = 40.0f;
    image.corners[2] = 440.0f;
    image.corners[3] = 80.0f;
    image.corners[4] = 400.0f;
    image.corners[5] = 460.0f;
    image.corners[6] = 20.0f;
    image.corners[7] = 420.0f;
    gX_ColorCodeResult result;
    status = gX_ColorCode_Decode(&image, &result);
    gX_Free(warped);
    if (status != GX_COLOR_CODE_OK || result.payloadLength != length
        || memcmp(result.payload, payload, length) != 0
        || result.orientation != 0)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 11 FAIL: perspective status=%d payload=%u",
            status, result.payloadLength);
        errors++;
    }
    return errors;
}

static gX_I32 test_perspective_matrix(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U8 *source = test_image(&frame, 0.91f, 8.0f);
    if (!source) return 1;
    gX_U8 *warped = test_warp_perspective(source);
    gX_Free(source);
    if (!warped) return 1;
    gX_ColorCodeImage image = {0};
    image.pixels = warped;
    image.width = 512;
    image.height = 512;
    image.perspective = 1;
    image.corners[0] = 90.0f;
    image.corners[1] = 50.0f;
    image.corners[2] = 420.0f;
    image.corners[3] = 70.0f;
    image.corners[4] = 430.0f;
    image.corners[5] = 450.0f;
    image.corners[6] = 60.0f;
    image.corners[7] = 430.0f;
    gX_ColorCodeResult result;
    status = gX_ColorCode_Decode(&image, &result);
    gX_Free(warped);
    if (status != GX_COLOR_CODE_OK || result.payloadLength != length
        || memcmp(result.payload, payload, length) != 0)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 12 FAIL: keystone status=%d payload=%u",
            status, result.payloadLength);
        errors++;
    }
    return errors;
}

static gX_I32 test_roundtrip(const gX_U8 *payload, gX_U32 length,
    gX_I32 expectedCompression)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 1, 3, &frame);
    if (status != GX_COLOR_CODE_OK || frame.frameIndex != 1
        || frame.frameTotal != 3 || frame.payloadLength != length)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 1 FAIL: encode status=%d", status);
        return 1;
    }
    if (expectedCompression >= 0 && frame.compression != expectedCompression)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 2 FAIL: compression=%u expected=%d",
            frame.compression, expectedCompression);
        errors++;
    }
    gX_U8 *pixels = test_image(&frame, 0.88f, 9.0f);
    if (!pixels)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 3 FAIL: render allocation");
        return errors + 1;
    }
    gX_ColorCodeResult result;
    status = test_decode(pixels, &result);
    gX_Free(pixels);
    if (status != GX_COLOR_CODE_OK || result.status != GX_COLOR_CODE_OK
        || result.payloadLength != length
        || memcmp(result.payload, payload, length) != 0
        || result.frameIndex != 1 || result.frameTotal != 3)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 4 FAIL: decode status=%d payload=%u expected=%u",
            status, result.payloadLength, length);
        errors++;
    }
    return errors;
}

static gX_I32 test_bit_errors(const gX_U8 *payload, gX_U32 length,
    gX_U32 flipMask, gX_I32 expectedStatus)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U32 flatCell = 50;
    gX_U32 row = GX_COLOR_CODE_DATA_START_ROW + flatCell / GX_COLOR_CODE_INNER;
    gX_U32 col = GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
        + flatCell % GX_COLOR_CODE_INNER;
    frame.cells[row * GX_COLOR_CODE_GRID + col] ^= (gX_U8)flipMask;
    gX_U8 *pixels = test_image(&frame, 0.90f, 7.0f);
    if (!pixels) return 1;
    gX_ColorCodeResult result;
    status = test_decode(pixels, &result);
    gX_Free(pixels);
    if (status != expectedStatus)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 5 FAIL: flip=%u status=%d expected=%d",
            flipMask, status, expectedStatus);
        errors++;
    }
    if (expectedStatus == GX_COLOR_CODE_OK && result.correctedBits == 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 6 FAIL: single-bit correction not reported");
        errors++;
    }
    return errors;
}

static gX_I32 test_single_bit_matrix(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    for (gX_U32 bit = 0; bit < GX_COLOR_CODE_ECC_BITS; bit++)
    {
        gX_ColorCodeFrame frame;
        gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
        if (status != GX_COLOR_CODE_OK) return errors + 1;
        gX_U32 flatCell = bit / GX_COLOR_CODE_BITS_PER_CELL;
        gX_U32 cellBit = GX_COLOR_CODE_BITS_PER_CELL - 1
            - (bit % GX_COLOR_CODE_BITS_PER_CELL);
        gX_U32 row = GX_COLOR_CODE_DATA_START_ROW
            + flatCell / GX_COLOR_CODE_INNER;
        gX_U32 col = GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
            + flatCell % GX_COLOR_CODE_INNER;
        frame.cells[row * GX_COLOR_CODE_GRID + col] ^= (gX_U8)(1u << cellBit);
        gX_U8 *pixels = test_image(&frame, 0.89f, 10.0f);
        if (!pixels) return errors + 1;
        gX_ColorCodeResult result;
        status = test_decode(pixels, &result);
        gX_Free(pixels);
        if (status != GX_COLOR_CODE_OK || result.correctedBits != 1
            || result.payloadLength != length
            || memcmp(result.payload, payload, length) != 0)
        {
            gX_TraceLog(gX_LOG_ERROR,
                "COLOR CODE 9 FAIL: single-bit position %u status=%d corrected=%u",
                bit, status, result.correctedBits);
            errors++;
        }
    }
    return errors;
}

static gX_I32 test_metadata_corruption(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U32 metaRow = GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER;
    gX_U32 metaCol = GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER;
    frame.cells[metaRow * GX_COLOR_CODE_GRID + metaCol] ^= 1;
    gX_U8 *pixels = test_image(&frame, 0.91f, 6.0f);
    if (!pixels) return 1;
    gX_ColorCodeResult result;
    status = test_decode(pixels, &result);
    gX_Free(pixels);
    if (status == GX_COLOR_CODE_OK)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 7 FAIL: metadata corruption accepted");
        errors++;
    }
    return errors;
}

static gX_I32 test_erasure(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U8 *pixels = test_image(&frame, 0.90f, 8.0f);
    if (!pixels) return 1;
    gX_U32 flatCell = 50;
    gX_U32 row = GX_COLOR_CODE_DATA_START_ROW + flatCell / GX_COLOR_CODE_INNER;
    gX_U32 col = GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
        + flatCell % GX_COLOR_CODE_INNER;
    for (gX_U32 y = 0; y < TEST_CELL; y++)
        for (gX_U32 x = 0; x < TEST_CELL; x++)
        {
            gX_U32 px = TEST_ORIGIN + col * TEST_CELL + x;
            gX_U32 py = TEST_ORIGIN + row * TEST_CELL + y;
            gX_U32 offset = (py * TEST_WIDTH + px) * 4;
            pixels[offset + 0] = 128;
            pixels[offset + 1] = 128;
            pixels[offset + 2] = 128;
        }
    gX_ColorCodeResult result;
    status = test_decode(pixels, &result);
    gX_Free(pixels);
    if (status != GX_COLOR_CODE_ERR_ERASURE)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 8 FAIL: erasure status=%d expected=%d",
            status, GX_COLOR_CODE_ERR_ERASURE);
        errors++;
    }
    return errors;
}

static gX_I32 test_orientation(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U8 *source = test_image(&frame, 0.90f, 9.0f);
    if (!source) return 1;
    for (gX_I32 mode = 0; mode < 8; mode++)
    {
        gX_U8 *pixels = test_transform(source, mode);
        if (!pixels)
        {
            errors++;
            break;
        }
        gX_ColorCodeResult result;
        status = test_decode_auto(pixels, &result);
        gX_Free(pixels);
        if (status != GX_COLOR_CODE_OK || result.orientation != mode
            || result.payloadLength != length
            || memcmp(result.payload, payload, length) != 0)
        {
            gX_TraceLog(gX_LOG_ERROR,
                "COLOR CODE 10 FAIL: orientation %d status=%d detected=%d",
                mode, status, result.orientation);
            errors++;
        }
    }
    gX_Free(source);
    return errors;
}

static gX_I32 test_tv(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    const gX_U32 width = 1024;
    const gX_U32 height = 768;
    gX_U8 *pixels = (gX_U8 *)gX_Calloc(width * height * 4);
    if (!pixels) return 1;
    for (gX_U32 seed = 0; seed < 3; seed++)
    {
        gX_F32 corners[8];
        status = gX_ColorCodeTv_Render(&frame, pixels, width, height,
            seed * 7919u, corners);
        if (status != GX_COLOR_CODE_OK)
        {
            errors++;
            break;
        }
        gX_ColorCodeImage image = {0};
        image.pixels = pixels;
        image.width = width;
        image.height = height;
        image.perspective = 1;
        memcpy(image.corners, corners, sizeof(corners));
        gX_ColorCodeResult result;
        status = gX_ColorCode_Decode(&image, &result);
        if (status != GX_COLOR_CODE_OK || result.payloadLength != length
            || memcmp(result.payload, payload, length) != 0)
        {
            gX_TraceLog(gX_LOG_ERROR,
                "COLOR CODE 13 FAIL: TV seed %u status=%d payload=%u",
                seed, status, result.payloadLength);
            errors++;
        }
    }
    gX_Free(pixels);
    return errors;
}

static gX_I32 test_multiframe(void)
{
    gX_I32 errors = 0;
    const gX_U32 length = 500;
    gX_U8 payload[500];
    for (gX_U32 index = 0; index < length; index++)
        payload[index] = (gX_U8)((index * 37u + 11u) & 0xFFu);
    gX_ColorCodeFrame *frames = (gX_ColorCodeFrame *)gX_Calloc(
        sizeof(gX_ColorCodeFrame) * GX_COLOR_CODE_MAX_MULTI_FRAMES);
    gX_ColorCodeResult *results = (gX_ColorCodeResult *)gX_Calloc(
        sizeof(gX_ColorCodeResult) * GX_COLOR_CODE_MAX_MULTI_FRAMES);
    if (!frames || !results)
    {
        gX_Free(frames);
        gX_Free(results);
        return 1;
    }
    gX_U32 count = 0;
    gX_I32 status = gX_ColorCode_EncodeMulti(payload, length, frames,
        GX_COLOR_CODE_MAX_MULTI_FRAMES, &count);
    if (status != GX_COLOR_CODE_OK || count != 2)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 14 FAIL: multi encode %d count=%u",
            status, count);
        errors++;
        goto cleanup;
    }
    for (gX_U32 index = 0; index < count; index++)
    {
        gX_U8 *pixels = test_image(&frames[index], 0.90f, 8.0f);
        if (!pixels)
        {
            errors++;
            goto cleanup;
        }
        status = test_decode(pixels, &results[index]);
        gX_Free(pixels);
        if (status != GX_COLOR_CODE_OK)
        {
            gX_TraceLog(gX_LOG_ERROR,
                "COLOR CODE 15 FAIL: frame %u decode %d", index, status);
            errors++;
            goto cleanup;
        }
    }
    gX_U8 rebuilt[GX_COLOR_CODE_MAX_MULTI_PAYLOAD];
    gX_U32 rebuiltLength = 0;
    status = gX_ColorCode_Reassemble(results, count, rebuilt,
        sizeof(rebuilt), &rebuiltLength);
    if (status != GX_COLOR_CODE_OK || rebuiltLength != length
        || memcmp(rebuilt, payload, length) != 0)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 16 FAIL: reassembly %d length=%u", status, rebuiltLength);
        errors++;
    }
    gX_ColorCodeResult *shuffled = (gX_ColorCodeResult *)gX_Calloc(
        sizeof(gX_ColorCodeResult) * GX_COLOR_CODE_MAX_MULTI_FRAMES);
    if (!shuffled)
    {
        errors++;
        goto cleanup;
    }
    for (gX_U32 index = 0; index < count; index++)
        shuffled[index] = results[count - 1 - index];
    rebuiltLength = 0;
    status = gX_ColorCode_Reassemble(shuffled, count, rebuilt,
        sizeof(rebuilt), &rebuiltLength);
    if (status != GX_COLOR_CODE_OK || rebuiltLength != length
        || memcmp(rebuilt, payload, length) != 0)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 17 FAIL: shuffled reassembly %d", status);
        errors++;
    }
    if (gX_ColorCode_Reassemble(results, count - 1, rebuilt,
            sizeof(rebuilt), &rebuiltLength) != GX_COLOR_CODE_ERR_METADATA)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 18 FAIL: incomplete set accepted");
        errors++;
    }
    for (gX_U32 index = 0; index < count; index++)
        shuffled[index] = results[index];
    shuffled[1] = shuffled[0];
    if (gX_ColorCode_Reassemble(shuffled, count, rebuilt,
            sizeof(rebuilt), &rebuiltLength) != GX_COLOR_CODE_ERR_METADATA)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 19 FAIL: duplicate frame accepted");
        errors++;
    }
    for (gX_U32 index = 0; index < count; index++)
        shuffled[index] = results[index];
    shuffled[1].payloadID ^= 1;
    if (gX_ColorCode_Reassemble(shuffled, count, rebuilt,
            sizeof(rebuilt), &rebuiltLength) != GX_COLOR_CODE_ERR_METADATA)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 20 FAIL: mixed payload IDs accepted");
        errors++;
    }
    for (gX_U32 index = 0; index < count; index++)
        shuffled[index] = results[index];
    for (gX_U32 index = 0; index < count; index++)
        shuffled[index].fullPayloadCRC ^= 1;
    if (gX_ColorCode_Reassemble(shuffled, count, rebuilt,
            sizeof(rebuilt), &rebuiltLength) != GX_COLOR_CODE_ERR_CHECKSUM)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 21 FAIL: bad whole-payload CRC accepted");
        errors++;
    }
    if (gX_ColorCode_Reassemble(results, count, rebuilt, 100,
            &rebuiltLength) != GX_COLOR_CODE_ERR_CAPACITY)
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 22 FAIL: small capacity accepted");
        errors++;
    }
    gX_Free(shuffled);
cleanup:
    gX_Free(frames);
    gX_Free(results);
    return errors;
}

static gX_I32 test_tint(const gX_U8 *payload, gX_U32 length,
    gX_F32 redGain, gX_F32 greenGain, gX_F32 blueGain,
    gX_F32 redBias, gX_F32 greenBias, gX_F32 blueBias)
{
    gX_I32 errors = 0;
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return 1;
    gX_U8 *pixels = test_image(&frame, 1.0f, 0.0f);
    if (!pixels) return 1;
    gX_F32 gains[3] = {redGain, greenGain, blueGain};
    gX_F32 biases[3] = {redBias, greenBias, blueBias};
    for (gX_U32 index = 0; index < TEST_WIDTH * TEST_HEIGHT; index++)
        for (gX_U32 channel = 0; channel < 3; channel++)
        {
            gX_F32 value = (gX_F32)pixels[index * 4 + channel]
                * gains[channel] + biases[channel];
            if (value < 0.0f) value = 0.0f;
            if (value > 255.0f) value = 255.0f;
            pixels[index * 4 + channel] = (gX_U8)(value + 0.5f);
        }
    gX_ColorCodeResult result;
    status = test_decode(pixels, &result);
    gX_Free(pixels);
    if (status != GX_COLOR_CODE_OK || result.payloadLength != length
        || memcmp(result.payload, payload, length) != 0)
    {
        gX_TraceLog(gX_LOG_ERROR,
            "COLOR CODE 23 FAIL: tint %.2f/%.2f/%.2f status=%d",
            redGain, greenGain, blueGain, status);
        errors++;
    }
    return errors;
}

static gX_I32 test_palette_levels(const gX_U8 *payload, gX_U32 length)
{
    gX_I32 errors = 0;
    gX_U8 palette[GX_COLOR_CODE_PALETTE_SIZE][3];
    for (gX_U32 index = 0; index < GX_COLOR_CODE_PALETTE_SIZE; index++)
        gX_ColorCode_Palette((gX_U8)index, &palette[index][0],
            &palette[index][1], &palette[index][2]);
    if (!(palette[2][0] > palette[3][0]
        && palette[4][1] > palette[5][1]
        && palette[6][2] > palette[7][2]))
    {
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE 24 FAIL: full/half levels collapse");
        errors++;
    }
    gX_ColorCodeFrame frame;
    gX_I32 status = gX_ColorCode_Encode(payload, length, 0, 1, &frame);
    if (status != GX_COLOR_CODE_OK) return errors + 1;
    gX_I1 seen[GX_COLOR_CODE_PALETTE_SIZE] = {0};
    for (gX_U32 col = 3; col < GX_COLOR_CODE_ACTIVE - 3; col++)
    {
        gX_U32 cell = (GX_COLOR_CODE_QUIET *
            GX_COLOR_CODE_GRID) + GX_COLOR_CODE_QUIET + col;
        gX_U8 value = frame.cells[cell];
        if (value != (gX_U8)((col - 3) % GX_COLOR_CODE_PALETTE_SIZE))
            errors++;
        if (value < GX_COLOR_CODE_PALETTE_SIZE) seen[value] = 1;
    }
    for (gX_U32 index = 0; index < GX_COLOR_CODE_PALETTE_SIZE; index++)
        if (!seen[index]) errors++;
    return errors;
}

gX_I32 gX_Tests_ColorCode(gX_I0)
{
    gX_I32 errors = 0;
    const gX_U8 raw[] = "gX color code prototype";
    errors += test_roundtrip(raw, (gX_U32)strlen((const char *)raw), 0);
    gX_U8 repeated[96];
    for (gX_U32 index = 0; index < sizeof(repeated); index++)
        repeated[index] = (gX_U8)(index < 48 ? 0x5A : 0xA5);
    errors += test_roundtrip(repeated, sizeof(repeated), 1);
    gX_U8 empty[1] = {0};
    errors += test_roundtrip(empty, 0, -1);
    errors += test_bit_errors(raw, (gX_U32)strlen((const char *)raw), 1,
        GX_COLOR_CODE_OK);
    errors += test_bit_errors(raw, (gX_U32)strlen((const char *)raw), 3,
        GX_COLOR_CODE_ERR_ECC);
    errors += test_single_bit_matrix(raw, (gX_U32)strlen((const char *)raw));
    errors += test_orientation(raw, (gX_U32)strlen((const char *)raw));
    errors += test_perspective(raw, (gX_U32)strlen((const char *)raw));
    errors += test_perspective_matrix(raw, (gX_U32)strlen((const char *)raw));
    errors += test_tv(raw, (gX_U32)strlen((const char *)raw));
    errors += test_multiframe();
    errors += test_tint(raw, (gX_U32)strlen((const char *)raw),
        0.85f, 0.95f, 1.10f, 10.0f, 5.0f, 20.0f);
    errors += test_tint(raw, (gX_U32)strlen((const char *)raw),
        1.10f, 0.90f, 0.80f, 0.0f, 0.0f, 0.0f);
    errors += test_palette_levels(raw, (gX_U32)strlen((const char *)raw));
    errors += test_metadata_corruption(raw, (gX_U32)strlen((const char *)raw));
    errors += test_erasure(raw, (gX_U32)strlen((const char *)raw));
    gX_ColorCodeFrame frame;
    gX_U8 tooLarge[GX_COLOR_CODE_MAX_PAYLOAD + 1];
    memset(tooLarge, 0x11, sizeof(tooLarge));
    if (gX_ColorCode_Encode(tooLarge, sizeof(tooLarge), 0, 1, &frame)
        != GX_COLOR_CODE_ERR_ARGUMENT) errors++;
    gX_U8 *pixels = (gX_U8 *)gX_Calloc(TEST_WIDTH * TEST_HEIGHT * 4);
    if (!pixels) return errors + 1;
    gX_ColorCodeImage image = {0};
    image.pixels = pixels;
    image.width = 32;
    image.height = 32;
    image.originX = 0;
    image.originY = 0;
    image.cellSize = 1;
    gX_ColorCodeResult result;
    if (gX_ColorCode_Decode(&image, &result) != GX_COLOR_CODE_ERR_GEOMETRY)
        errors++;
    gX_Free(pixels);
    if (errors == 0)
        gX_TraceLog(gX_LOG_INFO, "COLOR CODE tests: all passed");
    else
        gX_TraceLog(gX_LOG_ERROR, "COLOR CODE tests: %d errors", errors);
    return errors;
}
