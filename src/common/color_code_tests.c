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
