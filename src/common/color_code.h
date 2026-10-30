/**********************************************************************************************
*
*   [gX: src/common/color_code.h]
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

#ifndef GX_COMMON_COLOR_CODE_H
#define GX_COMMON_COLOR_CODE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_COLOR_CODE_GRID              45
#define GX_COLOR_CODE_QUIET             2
#define GX_COLOR_CODE_BORDER            3
#define GX_COLOR_CODE_META_ROWS         3
#define GX_COLOR_CODE_META_BYTES        20
#define GX_COLOR_CODE_ACTIVE            (GX_COLOR_CODE_GRID - 2 * GX_COLOR_CODE_QUIET)
#define GX_COLOR_CODE_INNER             (GX_COLOR_CODE_ACTIVE - 2 * GX_COLOR_CODE_BORDER)
#define GX_COLOR_CODE_CELLS             (GX_COLOR_CODE_GRID * GX_COLOR_CODE_GRID)
#define GX_COLOR_CODE_META_CELLS        (GX_COLOR_CODE_META_BYTES * 4)
#define GX_COLOR_CODE_META_REGION_CELLS (GX_COLOR_CODE_META_ROWS * GX_COLOR_CODE_INNER)
#define GX_COLOR_CODE_DATA_START_ROW    (GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER + GX_COLOR_CODE_META_ROWS)
#define GX_COLOR_CODE_DATA_ROWS         (GX_COLOR_CODE_GRID - GX_COLOR_CODE_DATA_START_ROW - GX_COLOR_CODE_QUIET - GX_COLOR_CODE_BORDER)
#define GX_COLOR_CODE_DATA_CELLS        (GX_COLOR_CODE_DATA_ROWS * GX_COLOR_CODE_INNER)
#define GX_COLOR_CODE_DATA_BITS         (GX_COLOR_CODE_DATA_CELLS * 2)
#define GX_COLOR_CODE_ECC_BITS          13
#define GX_COLOR_CODE_MAX_COMPRESSED    (GX_COLOR_CODE_DATA_BITS / GX_COLOR_CODE_ECC_BITS)
#define GX_COLOR_CODE_MAX_PAYLOAD       160
#define GX_COLOR_CODE_MAX_IMAGE_BYTES   (64 * 1024 * 1024)

#define GX_COLOR_CODE_OK                 0
#define GX_COLOR_CODE_ERR_ARGUMENT      -1
#define GX_COLOR_CODE_ERR_CAPACITY      -2
#define GX_COLOR_CODE_ERR_GEOMETRY      -3
#define GX_COLOR_CODE_ERR_METADATA      -4
#define GX_COLOR_CODE_ERR_CHECKSUM      -5
#define GX_COLOR_CODE_ERR_ECC           -6
#define GX_COLOR_CODE_ERR_COMPRESSION   -7
#define GX_COLOR_CODE_ERR_VERSION       -8
#define GX_COLOR_CODE_ERR_PALETTE       -9
#define GX_COLOR_CODE_ERR_ERASURE      -10

//ai: a bounded 45x45 screen-code frame; cells hold 2-bit palette indices.
typedef struct gX_ColorCodeFrame {
    gX_I8   name[128];
    gX_I8   description[256];
    gX_U8   cells[GX_COLOR_CODE_CELLS];
    gX_U16  payloadLength;
    gX_U16  compressedLength;
    gX_U8   compression;
    gX_U8   palette;
    gX_U8   frameIndex;
    gX_U8   frameTotal;
} gX_ColorCodeFrame;

//ai: decoded payload plus bounded diagnostics; no allocation escapes the call.
typedef struct gX_ColorCodeResult {
    gX_I8   name[128];
    gX_I8   description[256];
    gX_U8   payload[GX_COLOR_CODE_MAX_PAYLOAD];
    gX_U16  payloadLength;
    gX_U16  compressedLength;
    gX_U16  correctedBits;
    gX_U16  erasedCells;
    gX_U8   frameIndex;
    gX_U8   frameTotal;
    gX_I32  status;
} gX_ColorCodeResult;

//ai: RGBA8 camera image with explicit grid origin and integer cell pitch.
typedef struct gX_ColorCodeImage {
    gX_I8        name[128];
    gX_I8        description[256];
    const gX_U8 *pixels;
    gX_U32       width;
    gX_U32       height;
    gX_U32       originX;
    gX_U32       originY;
    gX_U32       cellSize;
} gX_ColorCodeImage;

//ai: encode a bounded payload into a calibration/control/metadata/payload frame.
gX_I32 gX_ColorCode_Encode(const gX_U8 *payload, gX_U32 length,
    gX_U32 frameIndex, gX_U32 frameTotal, gX_ColorCodeFrame *frame);

//ai: decode an axis-aligned camera image with explicit grid geometry.
gX_I32 gX_ColorCode_Decode(const gX_ColorCodeImage *image,
    gX_ColorCodeResult *result);

//ai: render the frame into an RGBA8 image for screen display or a test camera.
gX_I32 gX_ColorCode_Render(const gX_ColorCodeFrame *frame, gX_U8 *pixels,
    gX_U32 width, gX_U32 height, gX_U32 originX, gX_U32 originY,
    gX_U32 cellSize);

//ai: expose one palette entry for renderers and diagnostics.
gX_I0 gX_ColorCode_Palette(gX_U8 index, gX_U8 *r, gX_U8 *g, gX_U8 *b);

#ifdef __cplusplus
}
#endif

#endif
