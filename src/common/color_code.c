/**********************************************************************************************
*
*   [gX: src/common/color_code.c]
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

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "color_code.h"
#include "log.h"

#define GX_COLOR_CODE_META_ACTIVE_ROW  (GX_COLOR_CODE_BORDER)
#define GX_COLOR_CODE_DATA_ACTIVE_ROW  (GX_COLOR_CODE_BORDER + GX_COLOR_CODE_META_ROWS)
#define GX_COLOR_CODE_META_BITS        (GX_COLOR_CODE_META_BYTES * 8)
#define GX_COLOR_CODE_MAX_DISTANCE     (48.0f * 48.0f * 3.0f)

static const gX_U8 gX_ColorCodeCornerTL[9] = {1,1,1,1,0,1,1,1,1};
static const gX_U8 gX_ColorCodeCornerTR[9] = {0,0,0,0,1,0,0,0,0};
static const gX_U8 gX_ColorCodeCornerBL[9] = {1,0,1,0,1,0,1,0,1};
static const gX_U8 gX_ColorCodeCornerBR[9] = {0,1,0,1,0,1,0,1,0};

//opencode: high-contrast subset recommended by docs/color_code_palette.md.
//   The 3-bit mode uses these eight palette entries for controls, metadata and
//   payload; the 4-bit mode keeps the full sixteen-colour table.
static const gX_U8 gX_ColorCode8Palette[GX_COLOR_CODE_8COLOR_SIZE] = {
    0, 1, 13, 5, 9, 11, 3, 15
};
static const gX_U8 gX_ColorCode8Member[GX_COLOR_CODE_PALETTE_SIZE] = {
    1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1
};

gX_I0 gX_ColorCode_Palette(gX_U8 index, gX_U8 *r, gX_U8 *g, gX_U8 *b)
{
    gX_U8 colors[GX_COLOR_CODE_PALETTE_SIZE][3] = {
        {0,0,0},
        {255,255,255},
        {55,10,10},
        {110,20,20},
        {165,30,30},
        {220,40,40},
        {10,50,20},
        {20,100,40},
        {30,150,60},
        {40,200,80},
        {10,20,55},
        {20,40,110},
        {30,60,165},
        {40,80,220},
        {64,64,64},
        {192,192,192}
    };
    gX_U8 pick = index < GX_COLOR_CODE_PALETTE_SIZE ? index : 0;
    if (r) *r = colors[pick][0];
    if (g) *g = colors[pick][1];
    if (b) *b = colors[pick][2];
}

static gX_U32 color_code_crc32(const gX_U8 *data, gX_U32 length)
{
    gX_U32 crc = 0xFFFFFFFFu;
    for (gX_U32 i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (gX_U32 bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static gX_U16 color_code_get_u16(const gX_U8 *data)
{
    return (gX_U16)((gX_U16)data[0] | ((gX_U16)data[1] << 8));
}

static gX_U32 color_code_get_u32(const gX_U8 *data)
{
    return (gX_U32)data[0]
        | ((gX_U32)data[1] << 8)
        | ((gX_U32)data[2] << 16)
        | ((gX_U32)data[3] << 24);
}

static gX_I0 color_code_put_u16(gX_U8 *data, gX_U16 value)
{
    data[0] = (gX_U8)(value & 0xFF);
    data[1] = (gX_U8)((value >> 8) & 0xFF);
}

static gX_I0 color_code_put_u32(gX_U8 *data, gX_U32 value)
{
    data[0] = (gX_U8)(value & 0xFF);
    data[1] = (gX_U8)((value >> 8) & 0xFF);
    data[2] = (gX_U8)((value >> 16) & 0xFF);
    data[3] = (gX_U8)((value >> 24) & 0xFF);
}

static gX_U8 color_code_parity(gX_U16 value)
{
    gX_U8 parity = 0;
    while (value)
    {
        parity ^= 1;
        value &= (gX_U16)(value - 1);
    }
    return parity;
}

//opencode: opt-in Reed–Solomon erasure layer over GF(256). ECC ID 2 remains
//   the default SECDED + XOR format; ECC ID 3 uses systematic RS(18,16) with
//   a Cauchy parity matrix and recovers any two erased bytes per block.
static gX_I32 s_colorCodeEccMode = GX_COLOR_CODE_ECC_SECDED_XOR;
static gX_I32 s_colorCodePaletteMode = GX_COLOR_CODE_PALETTE_16COLOR;
static gX_U8 s_colorCodeGfExp[512];
static gX_U8 s_colorCodeGfLog[256];
static gX_I1 s_colorCodeGfReady = 0;

static gX_I0 color_code_gf_init(gX_I0)
{
    if (s_colorCodeGfReady) return;
    gX_U16 value = 1;
    for (gX_U32 index = 0; index < 255; index++)
    {
        s_colorCodeGfExp[index] = (gX_U8)value;
        s_colorCodeGfLog[value] = (gX_U8)index;
        value <<= 1;
        if (value & 0x100) value ^= 0x11D;
    }
    for (gX_U32 index = 255; index < 512; index++)
        s_colorCodeGfExp[index] = s_colorCodeGfExp[index - 255];
    s_colorCodeGfLog[0] = 0;
    s_colorCodeGfReady = 1;
}

static gX_U8 color_code_gf_mul(gX_U8 a, gX_U8 b)
{
    if (a == 0 || b == 0) return 0;
    return s_colorCodeGfExp[(gX_U32)s_colorCodeGfLog[a] + s_colorCodeGfLog[b]];
}

static gX_U8 color_code_gf_inv(gX_U8 value)
{
    return s_colorCodeGfExp[255 - s_colorCodeGfLog[value]];
}

static gX_U8 color_code_gf_div(gX_U8 a, gX_U8 b)
{
    if (b == 0) return 0;
    return color_code_gf_mul(a, color_code_gf_inv(b));
}

static gX_U8 color_code_rs_matrix(gX_I32 parity, gX_I32 data)
{
    return color_code_gf_inv((gX_U8)(parity ^ (data + 2)));
}

static gX_I0 color_code_rs_encode(const gX_U8 data[GX_COLOR_CODE_RS_DATA],
    gX_U8 parity[GX_COLOR_CODE_RS_PARITY])
{
    color_code_gf_init();
    for (gX_I32 row = 0; row < GX_COLOR_CODE_RS_PARITY; row++)
    {
        gX_U8 value = 0;
        for (gX_I32 column = 0; column < GX_COLOR_CODE_RS_DATA; column++)
            value ^= color_code_gf_mul(color_code_rs_matrix(row, column),
                data[column]);
        parity[row] = value;
    }
}

static gX_I32 color_code_rs_decode(gX_U8 codeword[GX_COLOR_CODE_RS_BLOCK],
    const gX_U8 erased[GX_COLOR_CODE_RS_BLOCK])
{
    color_code_gf_init();
    gX_I32 unknown[GX_COLOR_CODE_RS_PARITY];
    gX_I32 unknownCount = 0;
    for (gX_I32 index = 0; index < GX_COLOR_CODE_RS_BLOCK; index++)
    {
        if (!erased[index]) continue;
        if (unknownCount >= GX_COLOR_CODE_RS_PARITY)
            return GX_COLOR_CODE_ERR_ERASURE;
        unknown[unknownCount++] = index;
    }
    if (unknownCount == 0) return GX_COLOR_CODE_OK;
    gX_U8 matrix[GX_COLOR_CODE_RS_PARITY][GX_COLOR_CODE_RS_PARITY];
    gX_U8 rhs[GX_COLOR_CODE_RS_PARITY];
    memset(matrix, 0, sizeof(matrix));
    memset(rhs, 0, sizeof(rhs));
    for (gX_I32 row = 0; row < GX_COLOR_CODE_RS_PARITY; row++)
    {
        for (gX_I32 index = 0; index < GX_COLOR_CODE_RS_BLOCK; index++)
        {
            gX_U8 coefficient = (index < GX_COLOR_CODE_RS_DATA)
                ? color_code_rs_matrix(row, index)
                : (gX_U8)(index == GX_COLOR_CODE_RS_DATA + row ? 1 : 0);
            if (coefficient == 0) continue;
            if (erased[index])
            {
                for (gX_I32 slot = 0; slot < unknownCount; slot++)
                    if (unknown[slot] == index)
                        matrix[row][slot] = coefficient;
            }
            else
                rhs[row] ^= color_code_gf_mul(coefficient, codeword[index]);
        }
    }
    if (unknownCount == 1)
    {
        gX_I32 row = 0;
        while (row < GX_COLOR_CODE_RS_PARITY && matrix[row][0] == 0) row++;
        if (row == GX_COLOR_CODE_RS_PARITY) return GX_COLOR_CODE_ERR_ERASURE;
        codeword[unknown[0]] = color_code_gf_div(rhs[row], matrix[row][0]);
        return GX_COLOR_CODE_OK;
    }
    gX_U8 determinant = color_code_gf_mul(matrix[0][0], matrix[1][1])
        ^ color_code_gf_mul(matrix[0][1], matrix[1][0]);
    if (determinant == 0) return GX_COLOR_CODE_ERR_ERASURE;
    gX_U8 value0 = color_code_gf_mul(rhs[0], matrix[1][1])
        ^ color_code_gf_mul(rhs[1], matrix[0][1]);
    gX_U8 value1 = color_code_gf_mul(matrix[0][0], rhs[1])
        ^ color_code_gf_mul(matrix[1][0], rhs[0]);
    codeword[unknown[0]] = color_code_gf_div(value0, determinant);
    codeword[unknown[1]] = color_code_gf_div(value1, determinant);
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_SetEccMode(gX_I32 mode)
{
    if (mode != GX_COLOR_CODE_ECC_SECDED_XOR && mode != GX_COLOR_CODE_ECC_RS)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    s_colorCodeEccMode = mode;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_GetEccMode(gX_I0)
{
    return s_colorCodeEccMode;
}

gX_I32 gX_ColorCode_SetPaletteMode(gX_I32 mode)
{
    if (mode != GX_COLOR_CODE_PALETTE_16COLOR
        && mode != GX_COLOR_CODE_PALETTE_8COLOR)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    s_colorCodePaletteMode = mode;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_GetPaletteMode(gX_I0)
{
    return s_colorCodePaletteMode;
}

static gX_U32 color_code_bits_per_cell(gX_U8 palette)
{
    return palette == GX_COLOR_CODE_PALETTE_8COLOR
        ? GX_COLOR_CODE_BITS_PER_CELL_8 : GX_COLOR_CODE_BITS_PER_CELL_16;
}

static gX_U32 color_code_data_bits(gX_U8 palette)
{
    return palette == GX_COLOR_CODE_PALETTE_8COLOR
        ? GX_COLOR_CODE_DATA_BITS_8 : GX_COLOR_CODE_DATA_BITS_16;
}

static gX_U32 color_code_max_payload_for(gX_U8 palette, gX_I32 ecc)
{
    if (palette == GX_COLOR_CODE_PALETTE_8COLOR)
        return ecc == GX_COLOR_CODE_ECC_RS
            ? GX_COLOR_CODE_8COLOR_RS_MAX_PAYLOAD
            : GX_COLOR_CODE_8COLOR_MAX_PAYLOAD;
    return ecc == GX_COLOR_CODE_ECC_RS
        ? GX_COLOR_CODE_RS_MAX_PAYLOAD : GX_COLOR_CODE_MAX_PAYLOAD;
}

static gX_U32 color_code_max_payload(gX_I0)
{
    return color_code_max_payload_for(s_colorCodePaletteMode,
        s_colorCodeEccMode);
}

static gX_I0 color_code_set_cell(gX_U8 *cells, gX_U32 row, gX_U32 col, gX_U8 value)
{
    if (row < GX_COLOR_CODE_GRID && col < GX_COLOR_CODE_GRID)
        cells[row * GX_COLOR_CODE_GRID + col] = (gX_U8)(value & 15);
}

static gX_I0 color_code_corner(gX_U8 *cells, gX_U32 activeRow,
    gX_U32 activeCol, const gX_U8 pattern[9])
{
    for (gX_U32 row = 0; row < 3; row++)
        for (gX_U32 col = 0; col < 3; col++)
            color_code_set_cell(cells,
                GX_COLOR_CODE_QUIET + activeRow + row,
                GX_COLOR_CODE_QUIET + activeCol + col,
                pattern[row * 3 + col]);
}

static gX_I0 color_code_frame_controls(gX_U8 *cells, gX_U8 palette)
{
    memset(cells, 0, GX_COLOR_CODE_CELLS);
    for (gX_U32 index = 0; index < GX_COLOR_CODE_ACTIVE; index++)
    {
        gX_U8 value = (index & 1) ? 1 : 0;
        color_code_set_cell(cells, GX_COLOR_CODE_QUIET, GX_COLOR_CODE_QUIET + index, value);
        color_code_set_cell(cells, GX_COLOR_CODE_QUIET + GX_COLOR_CODE_ACTIVE - 1,
            GX_COLOR_CODE_QUIET + index, value);
        color_code_set_cell(cells, GX_COLOR_CODE_QUIET + index, GX_COLOR_CODE_QUIET, value);
        color_code_set_cell(cells, GX_COLOR_CODE_QUIET + index,
            GX_COLOR_CODE_QUIET + GX_COLOR_CODE_ACTIVE - 1, value);
    }
    for (gX_U32 row = 0; row < 2; row++)
    {
        gX_U32 gridRow = row == 0
            ? GX_COLOR_CODE_QUIET
            : GX_COLOR_CODE_QUIET + GX_COLOR_CODE_ACTIVE - 1;
        gX_U32 cycle = palette == GX_COLOR_CODE_PALETTE_8COLOR
            ? GX_COLOR_CODE_8COLOR_SIZE : GX_COLOR_CODE_PALETTE_SIZE;
        for (gX_U32 col = 3; col < GX_COLOR_CODE_ACTIVE - 3; col++)
        {
            gX_U32 slot = (col - 3) % cycle;
            gX_U8 value = palette == GX_COLOR_CODE_PALETTE_8COLOR
                ? gX_ColorCode8Palette[slot] : (gX_U8)slot;
            color_code_set_cell(cells, gridRow, GX_COLOR_CODE_QUIET + col,
                value);
        }
    }
    color_code_corner(cells, 0, 0, gX_ColorCodeCornerTL);
    color_code_corner(cells, 0, GX_COLOR_CODE_ACTIVE - 3, gX_ColorCodeCornerTR);
    color_code_corner(cells, GX_COLOR_CODE_ACTIVE - 3, 0, gX_ColorCodeCornerBL);
    color_code_corner(cells, GX_COLOR_CODE_ACTIVE - 3,
        GX_COLOR_CODE_ACTIVE - 3, gX_ColorCodeCornerBR);
}

static gX_I0 color_code_grid_from_flat(gX_U8 *cells, gX_U32 activeRow,
    gX_U32 rowCount, const gX_U8 *flat)
{
    for (gX_U32 row = 0; row < rowCount; row++)
        for (gX_U32 col = 0; col < GX_COLOR_CODE_INNER; col++)
            color_code_set_cell(cells,
                GX_COLOR_CODE_QUIET + activeRow + row,
                GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER + col,
                flat[row * GX_COLOR_CODE_INNER + col]);
}

static gX_I0 color_code_grid_to_flat(const gX_U8 *cells, gX_U32 activeRow,
    gX_U32 rowCount, gX_U8 *flat)
{
    for (gX_U32 row = 0; row < rowCount; row++)
        for (gX_U32 col = 0; col < GX_COLOR_CODE_INNER; col++)
            flat[row * GX_COLOR_CODE_INNER + col] =
                cells[(GX_COLOR_CODE_QUIET + activeRow + row) * GX_COLOR_CODE_GRID
                    + GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER + col];
}

static gX_I0 color_code_bytes_to_bits(const gX_U8 *bytes, gX_U32 byteCount, gX_U8 *bits)
{
    for (gX_U32 index = 0; index < byteCount; index++)
        for (gX_U32 bit = 0; bit < 8; bit++)
            bits[index * 8 + bit] = (gX_U8)((bytes[index] >> (7 - bit)) & 1);
}

static gX_I0 color_code_bits_to_bytes(const gX_U8 *bits, gX_U32 byteCount, gX_U8 *bytes)
{
    for (gX_U32 index = 0; index < byteCount; index++)
    {
        gX_U8 value = 0;
        for (gX_U32 bit = 0; bit < 8; bit++)
            value = (gX_U8)((value << 1) | (bits[index * 8 + bit] & 1));
        bytes[index] = value;
    }
}

static gX_I0 color_code_pack_bits(const gX_U8 *bits, gX_U32 bitCount,
    gX_U8 *cells, gX_U32 cellCount, gX_U32 bitsPerCell)
{
    memset(cells, 0, cellCount);
    for (gX_U32 index = 0; index < bitCount; index++)
    {
        gX_U32 cell = index / bitsPerCell;
        gX_U32 bit = bitsPerCell - 1 - (index % bitsPerCell);
        cells[cell] |= (gX_U8)((bits[index] & 1) << bit);
    }
}

static gX_I0 color_code_unpack_bits(const gX_U8 *cells, gX_U32 cellCount,
    gX_U8 *bits, gX_U32 bitCount, gX_U32 bitsPerCell)
{
    (gX_I0)cellCount;
    for (gX_U32 index = 0; index < bitCount; index++)
    {
        gX_U32 cell = index / bitsPerCell;
        gX_U32 bit = bitsPerCell - 1 - (index % bitsPerCell);
        bits[index] = (gX_U8)((cells[cell] >> bit) & 1);
    }
}

static gX_U16 color_code_hamming_encode(gX_U8 value)
{
    static const gX_U8 positions[8] = {3,5,6,7,9,10,11,12};
    gX_U16 codeword = 0;
    for (gX_U32 index = 0; index < 8; index++)
        if (value & (1u << (7 - index)))
            codeword |= (gX_U16)(1u << (positions[index] - 1));
    gX_U16 p1 = color_code_parity((gX_U16)(codeword
        & ((1u << 2) | (1u << 4) | (1u << 6) | (1u << 8) | (1u << 10))));
    gX_U16 p2 = color_code_parity((gX_U16)(codeword
        & ((1u << 2) | (1u << 5) | (1u << 6) | (1u << 9) | (1u << 10))));
    gX_U16 p4 = color_code_parity((gX_U16)(codeword
        & ((1u << 4) | (1u << 5) | (1u << 6) | (1u << 11))));
    gX_U16 p8 = color_code_parity((gX_U16)(codeword
        & ((1u << 8) | (1u << 9) | (1u << 10) | (1u << 11))));
    codeword |= (gX_U16)(p1 << 0);
    codeword |= (gX_U16)(p2 << 1);
    codeword |= (gX_U16)(p4 << 3);
    codeword |= (gX_U16)(p8 << 7);
    codeword |= (gX_U16)(color_code_parity((gX_U16)(codeword & 0x0FFF)) << 12);
    return codeword;
}

static gX_I32 color_code_hamming_decode(gX_U16 codeword, gX_U8 *value,
    gX_I1 *corrected)
{
    if (!value || !corrected) return GX_COLOR_CODE_ERR_ARGUMENT;
    *corrected = 0;
    gX_U8 syndrome = 0;
    for (gX_U32 position = 1; position <= 12; position++)
        if (codeword & (1u << (position - 1)))
            syndrome = (gX_U8)(syndrome ^ position);
    gX_U8 overall = color_code_parity((gX_U16)(codeword & 0x1FFF));
    if (syndrome == 0 && overall == 1)
    {
        codeword ^= (gX_U16)(1u << 12);
        *corrected = 1;
    }
    else if (syndrome != 0 && overall == 1)
    {
        if (syndrome > 12) return GX_COLOR_CODE_ERR_ECC;
        codeword ^= (gX_U16)(1u << (syndrome - 1));
        *corrected = 1;
    }
    else if (syndrome != 0)
    {
        return GX_COLOR_CODE_ERR_ECC;
    }
    static const gX_U8 positions[8] = {3,5,6,7,9,10,11,12};
    gX_U8 result = 0;
    for (gX_U32 index = 0; index < 8; index++)
        result = (gX_U8)((result << 1)
            | ((codeword >> (positions[index] - 1)) & 1));
    *value = result;
    return GX_COLOR_CODE_OK;
}

static gX_U32 color_code_rle_encode(const gX_U8 *source, gX_U32 length,
    gX_U8 *target, gX_U32 capacity)
{
    gX_U32 input = 0;
    gX_U32 output = 0;
    while (input < length)
    {
        gX_U32 run = 1;
        while (input + run < length && source[input + run] == source[input]
            && run < 130)
            run++;
        if (run >= 3)
        {
            if (output + 2 > capacity) return 0;
            target[output++] = (gX_U8)(0x80 | (run - 3));
            target[output++] = source[input];
            input += run;
            continue;
        }
        gX_U32 start = input;
        gX_U32 count = 0;
        while (input < length && count < 128)
        {
            gX_U32 probe = 1;
            while (input + probe < length && source[input + probe] == source[input]
                && probe < 3)
                probe++;
            if (probe >= 3) break;
            input++;
            count++;
        }
        if (output + 1 + count > capacity) return 0;
        target[output++] = (gX_U8)(count - 1);
        memcpy(target + output, source + start, count);
        output += count;
    }
    return output;
}

static gX_U32 color_code_compress(const gX_U8 *payload, gX_U32 length,
    gX_U8 *compressed, gX_U32 capacity)
{
    if (!payload || !compressed || capacity < 2) return 0;
    if (length > capacity - 1) return 0;
    gX_U8 rle[GX_COLOR_CODE_MAX_COMPRESSED];
    gX_U32 rleLength = color_code_rle_encode(payload, length, rle, sizeof(rle));
    gX_U32 rawLength = length + 1;
    if (rleLength > 0 && rleLength + 1 < rawLength)
    {
        compressed[0] = 1;
        memcpy(compressed + 1, rle, rleLength);
        return rleLength + 1;
    }
    compressed[0] = 0;
    memcpy(compressed + 1, payload, length);
    return rawLength;
}

static gX_I32 color_code_decompress(gX_U8 mode, const gX_U8 *source,
    gX_U32 length, gX_U8 *target, gX_U32 capacity, gX_U32 *targetLength)
{
    if (!source || !target || !targetLength) return GX_COLOR_CODE_ERR_ARGUMENT;
    if (mode == 0)
    {
        if (length > capacity) return GX_COLOR_CODE_ERR_CAPACITY;
        memcpy(target, source, length);
        *targetLength = length;
        return GX_COLOR_CODE_OK;
    }
    if (mode != 1) return GX_COLOR_CODE_ERR_COMPRESSION;
    gX_U32 input = 0;
    gX_U32 output = 0;
    while (input < length)
    {
        gX_U8 control = source[input++];
        if (control & 0x80)
        {
            gX_U32 count = (gX_U32)(control & 0x7F) + 3;
            if (input >= length || output + count > capacity)
                return GX_COLOR_CODE_ERR_COMPRESSION;
            memset(target + output, source[input++], count);
            output += count;
        }
        else
        {
            gX_U32 count = (gX_U32)control + 1;
            if (input + count > length || output + count > capacity)
                return GX_COLOR_CODE_ERR_COMPRESSION;
            memcpy(target + output, source + input, count);
            input += count;
            output += count;
        }
    }
    *targetLength = output;
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_homography(const gX_ColorCodeImage *image,
    gX_F32 matrix[9])
{
    if (!image || !matrix || !image->perspective) return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_F32 x0 = image->corners[0];
    gX_F32 y0 = image->corners[1];
    gX_F32 x1 = image->corners[2];
    gX_F32 y1 = image->corners[3];
    gX_F32 x2 = image->corners[4];
    gX_F32 y2 = image->corners[5];
    gX_F32 x3 = image->corners[6];
    gX_F32 y3 = image->corners[7];
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)
        || !isfinite(x2) || !isfinite(y2) || !isfinite(x3) || !isfinite(y3))
        return GX_COLOR_CODE_ERR_GEOMETRY;
    gX_F32 dx1 = x1 - x2;
    gX_F32 dx2 = x3 - x2;
    gX_F32 dx3 = x0 - x1 + x2 - x3;
    gX_F32 dy1 = y1 - y2;
    gX_F32 dy2 = y3 - y2;
    gX_F32 dy3 = y0 - y1 + y2 - y3;
    gX_F32 denominator = dx1 * dy2 - dx2 * dy1;
    gX_F32 g = 0.0f;
    gX_F32 h = 0.0f;
    gX_F32 a = 0.0f;
    gX_F32 b = 0.0f;
    gX_F32 d = 0.0f;
    gX_F32 e = 0.0f;
    if (fabsf(dx3) < 0.0001f && fabsf(dy3) < 0.0001f)
    {
        a = x1 - x0;
        b = x3 - x0;
        d = y1 - y0;
        e = y3 - y0;
    }
    else
    {
        if (fabsf(denominator) < 0.0001f) return GX_COLOR_CODE_ERR_GEOMETRY;
        g = (dx3 * dy2 - dx2 * dy3) / denominator;
        h = (dx1 * dy3 - dx3 * dy1) / denominator;
        a = x1 - x0 + g * x1;
        b = x3 - x0 + h * x3;
        d = y1 - y0 + g * y1;
        e = y3 - y0 + h * y3;
    }
    matrix[0] = a;
    matrix[1] = b;
    matrix[2] = x0;
    matrix[3] = d;
    matrix[4] = e;
    matrix[5] = y0;
    matrix[6] = g;
    matrix[7] = h;
    matrix[8] = 1.0f;
    return GX_COLOR_CODE_OK;
}

static gX_I0 color_code_map_cell(gX_U32 row, gX_U32 col,
    gX_I32 orientation, gX_U32 *imageRow, gX_U32 *imageCol)
{
    gX_U32 last = GX_COLOR_CODE_GRID - 1;
    switch (orientation & 7)
    {
        case 1:
            *imageRow = col;
            *imageCol = last - row;
            break;
        case 2:
            *imageRow = last - row;
            *imageCol = last - col;
            break;
        case 3:
            *imageRow = last - col;
            *imageCol = row;
            break;
        case 4:
            *imageRow = row;
            *imageCol = last - col;
            break;
        case 5:
            *imageRow = last - row;
            *imageCol = col;
            break;
        case 6:
            *imageRow = col;
            *imageCol = row;
            break;
        case 7:
            *imageRow = last - col;
            *imageCol = last - row;
            break;
        default:
            *imageRow = row;
            *imageCol = col;
            break;
    }
}

static gX_I32 color_code_sample_pixel(const gX_ColorCodeImage *image,
    gX_U32 row, gX_U32 col, gX_F32 color[3])
{
    if (!image || !color || !image->pixels || image->cellSize == 0)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_U32 centerX = image->originX + col * image->cellSize + image->cellSize / 2;
    gX_U32 centerY = image->originY + row * image->cellSize + image->cellSize / 2;
    gX_U32 radius = image->cellSize / 8;
    if (radius < 1) radius = 1;
    gX_F32 sum[3] = {0.0f,0.0f,0.0f};
    gX_U32 count = 0;
    for (gX_I32 y = (gX_I32)centerY - (gX_I32)radius;
        y <= (gX_I32)centerY + (gX_I32)radius; y++)
    {
        if (y < 0 || (gX_U32)y >= image->height) continue;
        for (gX_I32 x = (gX_I32)centerX - (gX_I32)radius;
            x <= (gX_I32)centerX + (gX_I32)radius; x++)
        {
            if (x < 0 || (gX_U32)x >= image->width) continue;
            gX_U32 offset = ((gX_U32)y * image->width + (gX_U32)x) * 4;
            sum[0] += image->pixels[offset + 0];
            sum[1] += image->pixels[offset + 1];
            sum[2] += image->pixels[offset + 2];
            count++;
        }
    }
    if (count == 0) return GX_COLOR_CODE_ERR_GEOMETRY;
    color[0] = sum[0] / (gX_F32)count;
    color[1] = sum[1] / (gX_F32)count;
    color[2] = sum[2] / (gX_F32)count;
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_sample_point(const gX_ColorCodeImage *image,
    gX_F32 x, gX_F32 y, gX_F32 color[3])
{
    if (!image || !color || !image->pixels)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    if (!isfinite(x) || !isfinite(y)) return GX_COLOR_CODE_ERR_GEOMETRY;
    gX_I32 centerX = (gX_I32)floorf(x);
    gX_I32 centerY = (gX_I32)floorf(y);
    gX_I32 radius = 1;
    gX_F32 sum[3] = {0.0f,0.0f,0.0f};
    gX_U32 count = 0;
    for (gX_I32 sampleY = centerY - radius; sampleY <= centerY + radius; sampleY++)
    {
        if (sampleY < 0 || (gX_U32)sampleY >= image->height) continue;
        for (gX_I32 sampleX = centerX - radius; sampleX <= centerX + radius; sampleX++)
        {
            if (sampleX < 0 || (gX_U32)sampleX >= image->width) continue;
            gX_U32 offset = ((gX_U32)sampleY * image->width
                + (gX_U32)sampleX) * 4;
            sum[0] += image->pixels[offset + 0];
            sum[1] += image->pixels[offset + 1];
            sum[2] += image->pixels[offset + 2];
            count++;
        }
    }
    if (count == 0) return GX_COLOR_CODE_ERR_GEOMETRY;
    color[0] = sum[0] / (gX_F32)count;
    color[1] = sum[1] / (gX_F32)count;
    color[2] = sum[2] / (gX_F32)count;
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_sample(const gX_ColorCodeImage *image,
    gX_U32 row, gX_U32 col, gX_I32 orientation, const gX_F32 *homography,
    gX_F32 color[3])
{
    gX_U32 imageRow = 0;
    gX_U32 imageCol = 0;
    color_code_map_cell(row, col, orientation, &imageRow, &imageCol);
    if (!homography)
        return color_code_sample_pixel(image, imageRow, imageCol, color);
    gX_F32 u = ((gX_F32)imageCol + 0.5f) / (gX_F32)GX_COLOR_CODE_GRID;
    gX_F32 v = ((gX_F32)imageRow + 0.5f) / (gX_F32)GX_COLOR_CODE_GRID;
    gX_F32 denominator = homography[6] * u + homography[7] * v + 1.0f;
    if (fabsf(denominator) < 0.000001f) return GX_COLOR_CODE_ERR_GEOMETRY;
    gX_F32 x = (homography[0] * u + homography[1] * v + homography[2])
        / denominator;
    gX_F32 y = (homography[3] * u + homography[4] * v + homography[5])
        / denominator;
    return color_code_sample_point(image, x, y, color);
}

static gX_I32 color_code_palette_index(const gX_F32 color[3],
    const gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3],
    const gX_U8 *subset, gX_U32 subsetSize, gX_U8 *index)
{
    if (!color || !palette || !index || subsetSize == 0)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_F32 best = 1.0e30f;
    gX_U8 bestSlot = 0;
    for (gX_U32 slot = 0; slot < subsetSize; slot++)
    {
        gX_U8 candidate = subset ? subset[slot] : (gX_U8)slot;
        gX_F32 dr = color[0] - palette[candidate][0];
        gX_F32 dg = color[1] - palette[candidate][1];
        gX_F32 db = color[2] - palette[candidate][2];
        gX_F32 distance = dr * dr + dg * dg + db * db;
        if (distance < best)
        {
            best = distance;
            bestSlot = (gX_U8)slot;
        }
    }
    *index = best > GX_COLOR_CODE_MAX_DISTANCE ? 0xFF : bestSlot;
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_calibrate(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography, gX_U8 paletteMode,
    gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3])
{
    gX_F32 sum[GX_COLOR_CODE_PALETTE_SIZE][3] = {{0.0f}};
    gX_U32 count[GX_COLOR_CODE_PALETTE_SIZE] = {0};
    gX_U32 cycle = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? GX_COLOR_CODE_8COLOR_SIZE : GX_COLOR_CODE_PALETTE_SIZE;
    for (gX_U32 row = 0; row < 2; row++)
    {
        gX_U32 gridRow = row == 0
            ? GX_COLOR_CODE_QUIET
            : GX_COLOR_CODE_QUIET + GX_COLOR_CODE_ACTIVE - 1;
        for (gX_U32 col = 3; col < GX_COLOR_CODE_ACTIVE - 3; col++)
        {
            gX_F32 color[3];
            gX_U32 gridCol = GX_COLOR_CODE_QUIET + col;
            if (color_code_sample(image, gridRow, gridCol, orientation,
                    homography, color) != GX_COLOR_CODE_OK)
                return GX_COLOR_CODE_ERR_GEOMETRY;
            gX_U32 slot = (col - 3) % cycle;
            gX_U32 entry = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
                ? gX_ColorCode8Palette[slot] : slot;
            sum[entry][0] += color[0];
            sum[entry][1] += color[1];
            sum[entry][2] += color[2];
            count[entry]++;
        }
    }
    memset(palette, 0,
        sizeof(gX_F32) * GX_COLOR_CODE_PALETTE_SIZE * 3);
    for (gX_U32 entry = 0; entry < GX_COLOR_CODE_PALETTE_SIZE; entry++)
    {
        if (paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
            && !gX_ColorCode8Member[entry])
            continue;
        if (count[entry] < 3) return GX_COLOR_CODE_ERR_GEOMETRY;
        palette[entry][0] = sum[entry][0] / (gX_F32)count[entry];
        palette[entry][1] = sum[entry][1] / (gX_F32)count[entry];
        palette[entry][2] = sum[entry][2] / (gX_F32)count[entry];
    }
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_classify(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography,
    const gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3],
    const gX_U8 *subset, gX_U32 subsetSize, gX_U8 *cells, gX_U16 *erased)
{
    *erased = 0;
    for (gX_U32 row = 0; row < GX_COLOR_CODE_GRID; row++)
    {
        for (gX_U32 col = 0; col < GX_COLOR_CODE_GRID; col++)
        {
            gX_F32 color[3];
            if (color_code_sample(image, row, col, orientation, homography,
                    color) != GX_COLOR_CODE_OK)
                return GX_COLOR_CODE_ERR_GEOMETRY;
            gX_U8 index = 0;
            gX_I32 status = color_code_palette_index(color, palette, subset,
                subsetSize, &index);
            if (status != GX_COLOR_CODE_OK) return status;
            cells[row * GX_COLOR_CODE_GRID + col] = index;
            if (index == 0xFF) (*erased)++;
        }
    }
    return GX_COLOR_CODE_OK;
}

static gX_I32 color_code_classify_region(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography,
    const gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3],
    const gX_U8 *subset, gX_U32 subsetSize,
    gX_U32 activeRow, gX_U32 rowCount, gX_U8 *cells)
{
    for (gX_U32 row = 0; row < rowCount; row++)
    {
        for (gX_U32 col = 0; col < GX_COLOR_CODE_INNER; col++)
        {
            gX_F32 color[3];
            if (color_code_sample(image,
                    GX_COLOR_CODE_QUIET + activeRow + row,
                    GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER + col,
                    orientation, homography, color) != GX_COLOR_CODE_OK)
                return GX_COLOR_CODE_ERR_GEOMETRY;
            gX_U8 index = 0;
            gX_I32 status = color_code_palette_index(color, palette, subset,
                subsetSize, &index);
            if (status != GX_COLOR_CODE_OK) return status;
            cells[row * GX_COLOR_CODE_INNER + col] = index;
        }
    }
    return GX_COLOR_CODE_OK;
}

static gX_I1 color_code_corner_color_matches(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography,
    gX_U32 activeRow, gX_U32 activeCol,
    const gX_U8 pattern[9],
    const gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3],
    const gX_U8 *subset, gX_U32 subsetSize)
{
    for (gX_U32 index = 0; index < 9; index++)
    {
        gX_U32 row = GX_COLOR_CODE_QUIET + activeRow + index / 3;
        gX_U32 col = GX_COLOR_CODE_QUIET + activeCol + index % 3;
        gX_F32 color[3];
        if (color_code_sample(image, row, col, orientation, homography, color)
            != GX_COLOR_CODE_OK)
            return 0;
        gX_U8 entry = 0;
        if (color_code_palette_index(color, palette, subset, subsetSize, &entry)
            != GX_COLOR_CODE_OK)
            return 0;
        if (entry != pattern[index]) return 0;
    }
    return 1;
}

static gX_I1 color_code_orientation_matches(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography, gX_U8 paletteMode)
{
    gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3];
    if (color_code_calibrate(image, orientation, homography, paletteMode,
            palette) != GX_COLOR_CODE_OK)
        return 0;
    const gX_U8 *subset = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? gX_ColorCode8Palette : NULL;
    gX_U32 subsetSize = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? GX_COLOR_CODE_8COLOR_SIZE : GX_COLOR_CODE_PALETTE_SIZE;
    if (!color_code_corner_color_matches(image, orientation, homography, 0, 0,
            gX_ColorCodeCornerTL, palette, subset, subsetSize))
        return 0;
    if (!color_code_corner_color_matches(image, orientation, homography, 0,
            GX_COLOR_CODE_ACTIVE - 3, gX_ColorCodeCornerTR, palette, subset,
            subsetSize))
        return 0;
    if (!color_code_corner_color_matches(image, orientation, homography,
            GX_COLOR_CODE_ACTIVE - 3, 0, gX_ColorCodeCornerBL, palette, subset,
            subsetSize))
        return 0;
    if (!color_code_corner_color_matches(image, orientation, homography,
            GX_COLOR_CODE_ACTIVE - 3, GX_COLOR_CODE_ACTIVE - 3,
            gX_ColorCodeCornerBR, palette, subset, subsetSize))
        return 0;
    return 1;
}

static gX_I1 color_code_corner_matches(const gX_U8 *cells, gX_U32 activeRow,
    gX_U32 activeCol, const gX_U8 pattern[9])
{
    for (gX_U32 index = 0; index < 9; index++)
    {
        gX_U32 row = GX_COLOR_CODE_QUIET + activeRow + index / 3;
        gX_U32 col = GX_COLOR_CODE_QUIET + activeCol + index % 3;
        if (cells[row * GX_COLOR_CODE_GRID + col] != pattern[index])
            return 0;
    }
    return 1;
}

//opencode: metadata is packed at the candidate palette's cell width; the
//   decoder tries 4-bit/16-colour first, then 3-bit/8-colour, and accepts only
//   a header whose palette ID and CRC agree.
static gX_I32 color_code_metadata_read(const gX_ColorCodeImage *image,
    gX_I32 orientation, const gX_F32 *homography,
    const gX_F32 palette[GX_COLOR_CODE_PALETTE_SIZE][3],
    const gX_U8 *subset, gX_U32 subsetSize, gX_U32 bitsPerCell,
    gX_U8 expectedPalette, gX_U8 *meta)
{
    if (!meta) return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_U8 metaCells[GX_COLOR_CODE_META_REGION_CELLS];
    gX_I32 status = color_code_classify_region(image, orientation, homography,
        palette, subset, subsetSize, GX_COLOR_CODE_META_ACTIVE_ROW,
        GX_COLOR_CODE_META_ROWS, metaCells);
    if (status != GX_COLOR_CODE_OK) return status;
    gX_U32 metaCellCount = bitsPerCell == GX_COLOR_CODE_BITS_PER_CELL_8
        ? GX_COLOR_CODE_META_CELLS_8 : GX_COLOR_CODE_META_CELLS;
    for (gX_U32 index = 0; index < metaCellCount; index++)
        if (metaCells[index] == 0xFF) return GX_COLOR_CODE_ERR_ERASURE;
    gX_U8 metaBits[GX_COLOR_CODE_META_BITS];
    color_code_unpack_bits(metaCells, GX_COLOR_CODE_META_REGION_CELLS,
        metaBits, GX_COLOR_CODE_META_BITS, bitsPerCell);
    color_code_bits_to_bytes(metaBits, GX_COLOR_CODE_META_BYTES, meta);
    if (meta[0] != 0x43 || (meta[1] != 5 && meta[1] != 6))
        return GX_COLOR_CODE_ERR_VERSION;
    if (meta[3] != expectedPalette || meta[2] > 1)
        return GX_COLOR_CODE_ERR_PALETTE;
    gX_I32 eccMode = meta[5];
    if ((eccMode != GX_COLOR_CODE_ECC_SECDED_XOR
            && eccMode != GX_COLOR_CODE_ECC_RS)
        || (meta[1] == 5 && eccMode != GX_COLOR_CODE_ECC_SECDED_XOR)
        || (meta[1] == 6 && eccMode != GX_COLOR_CODE_ECC_RS))
        return GX_COLOR_CODE_ERR_PALETTE;
    if (color_code_crc32(meta, 22) != color_code_get_u32(meta + 22))
        return GX_COLOR_CODE_ERR_CHECKSUM;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_EncodeChunk(const gX_U8 *chunk, gX_U32 chunkLength,
    gX_U16 payloadID, gX_U32 fullPayloadCRC, gX_U32 frameIndex,
    gX_U32 frameTotal, gX_ColorCodeFrame *frame)
{
    if (!chunk || !frame || chunkLength > GX_COLOR_CODE_MAX_PAYLOAD
        || frameTotal == 0 || frameTotal > 255 || frameIndex >= frameTotal)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    if (chunkLength > color_code_max_payload())
        return GX_COLOR_CODE_ERR_CAPACITY;
    gX_U8 palette = (gX_U8)s_colorCodePaletteMode;
    gX_U32 bitsPerCell = color_code_bits_per_cell(palette);
    gX_U32 dataBitCount = color_code_data_bits(palette);
    memset(frame, 0, sizeof(*frame));
    snprintf((char *)frame->name, sizeof(frame->name), "%s", "Color code frame");
    snprintf((char *)frame->description, sizeof(frame->description), "%s",
        "45x45 screen transfer frame");
    gX_U8 compressed[GX_COLOR_CODE_MAX_COMPRESSED];
    gX_U32 compressedLength = color_code_compress(chunk, chunkLength,
        compressed, sizeof(compressed));
    if (compressedLength == 0) return GX_COLOR_CODE_ERR_CAPACITY;
    gX_U32 blockCount;
    gX_U32 parityBytes;
    if (s_colorCodeEccMode == GX_COLOR_CODE_ECC_RS)
    {
        blockCount = (compressedLength + GX_COLOR_CODE_RS_DATA - 1)
            / GX_COLOR_CODE_RS_DATA;
        parityBytes = blockCount * GX_COLOR_CODE_RS_PARITY;
    }
    else
    {
        blockCount = (compressedLength + 7) / 8;
        parityBytes = blockCount;
    }
    gX_U32 encodedBytes = compressedLength + parityBytes;
    if (encodedBytes * GX_COLOR_CODE_ECC_BITS > dataBitCount)
        return GX_COLOR_CODE_ERR_CAPACITY;
    gX_U8 encoded[GX_COLOR_CODE_MAX_ENCODED];
    memset(encoded, 0, sizeof(encoded));
    memcpy(encoded, compressed, compressedLength);
    if (s_colorCodeEccMode == GX_COLOR_CODE_ECC_RS)
    {
        for (gX_U32 block = 0; block < blockCount; block++)
        {
            gX_U8 data[GX_COLOR_CODE_RS_DATA];
            memset(data, 0, sizeof(data));
            for (gX_U32 index = 0; index < GX_COLOR_CODE_RS_DATA; index++)
            {
                gX_U32 source = block + index * blockCount;
                if (source < compressedLength) data[index] = compressed[source];
            }
            gX_U8 parity[GX_COLOR_CODE_RS_PARITY];
            color_code_rs_encode(data, parity);
            encoded[compressedLength + block * GX_COLOR_CODE_RS_PARITY]
                = parity[0];
            encoded[compressedLength + block * GX_COLOR_CODE_RS_PARITY + 1]
                = parity[1];
        }
    }
    else
    {
        for (gX_U32 block = 0; block < blockCount; block++)
        {
            gX_U8 parity = 0;
            for (gX_U32 index = block; index < compressedLength; index += blockCount)
                parity ^= compressed[index];
            encoded[compressedLength + block] = parity;
        }
    }
    gX_U8 meta[GX_COLOR_CODE_META_BYTES];
    memset(meta, 0, sizeof(meta));
    meta[0] = 0x43;
    meta[1] = s_colorCodeEccMode == GX_COLOR_CODE_ECC_RS ? 6 : 5;
    meta[2] = frameTotal > 1 ? 1 : 0;
    meta[3] = palette;
    meta[4] = compressed[0];
    meta[5] = s_colorCodeEccMode == GX_COLOR_CODE_ECC_RS
        ? GX_COLOR_CODE_ECC_RS : GX_COLOR_CODE_ECC_SECDED_XOR;
    color_code_put_u16(meta + 6, (gX_U16)chunkLength);
    color_code_put_u16(meta + 8, (gX_U16)compressedLength);
    meta[10] = (gX_U8)frameIndex;
    meta[11] = (gX_U8)frameTotal;
    color_code_put_u16(meta + 12, payloadID);
    color_code_put_u32(meta + 14, color_code_crc32(chunk, chunkLength));
    color_code_put_u32(meta + 18, fullPayloadCRC);
    color_code_put_u32(meta + 22, color_code_crc32(meta, 22));
    color_code_frame_controls(frame->cells, palette);
    gX_U8 metaBits[GX_COLOR_CODE_META_BITS];
    color_code_bytes_to_bits(meta, GX_COLOR_CODE_META_BYTES, metaBits);
    gX_U8 metaCells[GX_COLOR_CODE_META_REGION_CELLS];
    color_code_pack_bits(metaBits, GX_COLOR_CODE_META_BITS, metaCells,
        GX_COLOR_CODE_META_REGION_CELLS, bitsPerCell);
    color_code_grid_from_flat(frame->cells, GX_COLOR_CODE_META_ACTIVE_ROW,
        GX_COLOR_CODE_META_ROWS, metaCells);
    gX_U8 dataBits[GX_COLOR_CODE_DATA_BITS];
    memset(dataBits, 0, sizeof(dataBits));
    for (gX_U32 index = 0; index < encodedBytes; index++)
    {
        gX_U16 codeword = color_code_hamming_encode(encoded[index]);
        for (gX_U32 bit = 0; bit < GX_COLOR_CODE_ECC_BITS; bit++)
            dataBits[index * GX_COLOR_CODE_ECC_BITS + bit] =
                (gX_U8)((codeword >> bit) & 1);
    }
    gX_U8 dataCells[GX_COLOR_CODE_DATA_CELLS];
    color_code_pack_bits(dataBits, encodedBytes * GX_COLOR_CODE_ECC_BITS,
        dataCells, GX_COLOR_CODE_DATA_CELLS, bitsPerCell);
    color_code_grid_from_flat(frame->cells, GX_COLOR_CODE_DATA_ACTIVE_ROW,
        GX_COLOR_CODE_DATA_ROWS, dataCells);
    frame->payloadLength = (gX_U16)chunkLength;
    frame->compressedLength = (gX_U16)compressedLength;
    frame->compression = compressed[0];
    frame->palette = palette;
    frame->frameIndex = (gX_U8)frameIndex;
    frame->frameTotal = (gX_U8)frameTotal;
    frame->payloadID = payloadID;
    frame->fullPayloadCRC = fullPayloadCRC;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_Encode(const gX_U8 *payload, gX_U32 length,
    gX_U32 frameIndex, gX_U32 frameTotal, gX_ColorCodeFrame *frame)
{
    if (!payload) return GX_COLOR_CODE_ERR_ARGUMENT;
    return gX_ColorCode_EncodeChunk(payload, length, 0,
        color_code_crc32(payload, length), frameIndex, frameTotal, frame);
}

gX_I32 gX_ColorCode_EncodeMulti(const gX_U8 *payload, gX_U32 length,
    gX_ColorCodeFrame *frames, gX_U32 maxFrames, gX_U32 *frameCount)
{
    if (!payload || !frames || !frameCount
        || length > GX_COLOR_CODE_MAX_MULTI_PAYLOAD)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    if (length == 0)
    {
        if (maxFrames < 1) return GX_COLOR_CODE_ERR_CAPACITY;
        *frameCount = 1;
        return gX_ColorCode_EncodeChunk(payload, 0, 0, 0, 0, 1, &frames[0]);
    }
    gX_U32 maxPayload = color_code_max_payload();
    gX_U32 count = (length + maxPayload - 1) / maxPayload;
    if (count > maxFrames || count > 255)
        return GX_COLOR_CODE_ERR_CAPACITY;
    gX_U32 fullCRC = color_code_crc32(payload, length);
    gX_U16 payloadID = (gX_U16)(fullCRC & 0xFFFFu);
    if (payloadID == 0) payloadID = 0xFFFFu;
    gX_U32 offset = 0;
    for (gX_U32 index = 0; index < count; index++)
    {
        gX_U32 chunkLength = length - offset;
        if (chunkLength > maxPayload)
            chunkLength = maxPayload;
        gX_I32 status = gX_ColorCode_EncodeChunk(payload + offset, chunkLength,
            payloadID, fullCRC, index, count, &frames[index]);
        if (status != GX_COLOR_CODE_OK) return status;
        offset += chunkLength;
    }
    *frameCount = count;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_Reassemble(const gX_ColorCodeResult *frames,
    gX_U32 frameCount, gX_U8 *payload, gX_U32 capacity,
    gX_U32 *payloadLength)
{
    if (!frames || !payload || !payloadLength || frameCount == 0
        || frameCount > 255)
        return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_U8 frameTotal = frames[0].frameTotal;
    gX_U16 payloadID = frames[0].payloadID;
    gX_U32 fullCRC = frames[0].fullPayloadCRC;
    if (frameTotal != frameCount || frames[0].status != GX_COLOR_CODE_OK)
        return GX_COLOR_CODE_ERR_METADATA;
    gX_U8 seen[255];
    memset(seen, 0, sizeof(seen));
    for (gX_U32 index = 0; index < frameCount; index++)
    {
        if (frames[index].status != GX_COLOR_CODE_OK)
            return frames[index].status;
        if (frames[index].frameTotal != frameTotal
            || frames[index].payloadID != payloadID
            || frames[index].fullPayloadCRC != fullCRC
            || frames[index].frameIndex >= frameTotal
            || seen[frames[index].frameIndex])
            return GX_COLOR_CODE_ERR_METADATA;
        seen[frames[index].frameIndex] = 1;
    }
    gX_U32 total = 0;
    for (gX_U32 frameIndex = 0; frameIndex < frameTotal; frameIndex++)
    {
        const gX_ColorCodeResult *source = NULL;
        for (gX_U32 index = 0; index < frameCount; index++)
            if (frames[index].frameIndex == frameIndex)
            {
                source = &frames[index];
                break;
            }
        if (!source) return GX_COLOR_CODE_ERR_METADATA;
        if (total + source->payloadLength > capacity
            || total + source->payloadLength > GX_COLOR_CODE_MAX_MULTI_PAYLOAD)
            return GX_COLOR_CODE_ERR_CAPACITY;
        memcpy(payload + total, source->payload, source->payloadLength);
        total += source->payloadLength;
    }
    if (color_code_crc32(payload, total) != fullCRC)
        return GX_COLOR_CODE_ERR_CHECKSUM;
    *payloadLength = total;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_Decode(const gX_ColorCodeImage *image,
    gX_ColorCodeResult *result)
{
    if (!image || !result || !image->pixels
        || (!image->perspective && image->cellSize == 0))
        return GX_COLOR_CODE_ERR_ARGUMENT;
    memset(result, 0, sizeof(*result));
    result->orientation = -1;
    snprintf((char *)result->name, sizeof(result->name), "%s", "Color code result");
    snprintf((char *)result->description, sizeof(result->description), "%s",
        "Decoded screen transfer payload");
    gX_F32 homography[9];
    const gX_F32 *projection = NULL;
    if (image->perspective)
    {
        gX_I32 status = color_code_homography(image, homography);
        if (status != GX_COLOR_CODE_OK)
        {
            result->status = status;
            return result->status;
        }
        projection = homography;
    }
    else
    {
        gX_U64 required = (gX_U64)GX_COLOR_CODE_GRID * image->cellSize;
        if ((gX_U64)image->originX + required > image->width
            || (gX_U64)image->originY + required > image->height)
        {
            result->status = GX_COLOR_CODE_ERR_GEOMETRY;
            return result->status;
        }
    }
    gX_I32 orientation = 0;
    if (image->autoOrientation)
    {
        gX_I1 found = 0;
        for (gX_U8 mode = GX_COLOR_CODE_PALETTE_16COLOR;
            mode <= GX_COLOR_CODE_PALETTE_8COLOR && !found; mode++)
        {
            for (gX_I32 candidate = 0; candidate < 8; candidate++)
                if (color_code_orientation_matches(image, candidate,
                        projection, mode))
                {
                    orientation = candidate;
                    found = 1;
                    break;
                }
        }
        if (!found)
        {
            result->status = GX_COLOR_CODE_ERR_GEOMETRY;
            return result->status;
        }
    }
    gX_F32 palette16[GX_COLOR_CODE_PALETTE_SIZE][3];
    gX_F32 palette8[GX_COLOR_CODE_PALETTE_SIZE][3];
    gX_I32 status = color_code_calibrate(image, orientation, projection,
        GX_COLOR_CODE_PALETTE_16COLOR, palette16);
    if (status != GX_COLOR_CODE_OK)
    {
        result->status = status;
        return result->status;
    }
    status = color_code_calibrate(image, orientation, projection,
        GX_COLOR_CODE_PALETTE_8COLOR, palette8);
    if (status != GX_COLOR_CODE_OK)
    {
        result->status = status;
        return result->status;
    }
    gX_U8 meta[GX_COLOR_CODE_META_BYTES];
    gX_U8 meta4[GX_COLOR_CODE_META_BYTES] = {0};
    gX_U8 meta8[GX_COLOR_CODE_META_BYTES] = {0};
    gX_I32 status4 = color_code_metadata_read(image, orientation, projection,
        palette16, NULL, GX_COLOR_CODE_PALETTE_SIZE,
        GX_COLOR_CODE_BITS_PER_CELL_16, GX_COLOR_CODE_PALETTE_16COLOR, meta4);
    gX_I32 status8 = color_code_metadata_read(image, orientation, projection,
        palette8, gX_ColorCode8Palette, GX_COLOR_CODE_8COLOR_SIZE,
        GX_COLOR_CODE_BITS_PER_CELL_8, GX_COLOR_CODE_PALETTE_8COLOR, meta8);
    gX_U8 paletteMode = GX_COLOR_CODE_PALETTE_16COLOR;
    gX_U32 bitsPerCell = GX_COLOR_CODE_BITS_PER_CELL_16;
    if (status4 == GX_COLOR_CODE_OK)
    {
        memcpy(meta, meta4, sizeof(meta));
    }
    else if (status8 == GX_COLOR_CODE_OK)
    {
        memcpy(meta, meta8, sizeof(meta));
        paletteMode = GX_COLOR_CODE_PALETTE_8COLOR;
        bitsPerCell = GX_COLOR_CODE_BITS_PER_CELL_8;
    }
    else
    {
        result->status = (meta4[0] == 0x43
            && (meta4[1] == 5 || meta4[1] == 6)) ? status4 : status8;
        return result->status;
    }
    const gX_U8 *subset = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? gX_ColorCode8Palette : NULL;
    gX_U32 subsetSize = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? GX_COLOR_CODE_8COLOR_SIZE : GX_COLOR_CODE_PALETTE_SIZE;
    const gX_F32 (*palette)[3] = paletteMode == GX_COLOR_CODE_PALETTE_8COLOR
        ? palette8 : palette16;
    gX_U8 cells[GX_COLOR_CODE_CELLS];
    gX_U16 erased = 0;
    status = color_code_classify(image, orientation, projection, palette,
        subset, subsetSize, cells, &erased);
    if (status != GX_COLOR_CODE_OK)
    {
        result->status = status;
        return result->status;
    }
    result->erasedCells = erased;
    gX_I1 cornerTL = color_code_corner_matches(cells, 0, 0, gX_ColorCodeCornerTL);
    gX_I1 cornerTR = color_code_corner_matches(cells, 0, GX_COLOR_CODE_ACTIVE - 3,
        gX_ColorCodeCornerTR);
    gX_I1 cornerBL = color_code_corner_matches(cells, GX_COLOR_CODE_ACTIVE - 3, 0,
        gX_ColorCodeCornerBL);
    gX_I1 cornerBR = color_code_corner_matches(cells, GX_COLOR_CODE_ACTIVE - 3,
        GX_COLOR_CODE_ACTIVE - 3, gX_ColorCodeCornerBR);
    if (!cornerTL || !cornerTR || !cornerBL || !cornerBR)
    {
        result->status = GX_COLOR_CODE_ERR_GEOMETRY;
        return result->status;
    }
    gX_U8 metaCells[GX_COLOR_CODE_META_REGION_CELLS];
    color_code_grid_to_flat(cells, GX_COLOR_CODE_META_ACTIVE_ROW,
        GX_COLOR_CODE_META_ROWS, metaCells);
    gX_U32 metaCellCount = bitsPerCell == GX_COLOR_CODE_BITS_PER_CELL_8
        ? GX_COLOR_CODE_META_CELLS_8 : GX_COLOR_CODE_META_CELLS;
    for (gX_U32 index = 0; index < metaCellCount; index++)
        if (metaCells[index] == 0xFF)
        {
            result->status = GX_COLOR_CODE_ERR_ERASURE;
            return result->status;
        }
    gX_I32 eccMode = meta[5];
    gX_U32 payloadLength = color_code_get_u16(meta + 6);
    gX_U32 compressedLength = color_code_get_u16(meta + 8);
    gX_U32 dataBitCount = color_code_data_bits(paletteMode);
    if (payloadLength > color_code_max_payload_for(paletteMode, eccMode)
        || compressedLength == 0
        || compressedLength > GX_COLOR_CODE_MAX_COMPRESSED
        || meta[4] > 1 || meta[11] == 0 || meta[10] >= meta[11])
    {
        result->status = GX_COLOR_CODE_ERR_METADATA;
        return result->status;
    }
    gX_U32 blockCount = (eccMode == GX_COLOR_CODE_ECC_RS)
        ? (compressedLength + GX_COLOR_CODE_RS_DATA - 1) / GX_COLOR_CODE_RS_DATA
        : (compressedLength + 7) / 8;
    gX_U32 parityBytes = (eccMode == GX_COLOR_CODE_ECC_RS)
        ? blockCount * GX_COLOR_CODE_RS_PARITY : blockCount;
    gX_U32 encodedBytes = compressedLength + parityBytes;
    if (encodedBytes * GX_COLOR_CODE_ECC_BITS > dataBitCount)
    {
        result->status = GX_COLOR_CODE_ERR_METADATA;
        return result->status;
    }
    gX_U8 dataCells[GX_COLOR_CODE_DATA_CELLS];
    color_code_grid_to_flat(cells, GX_COLOR_CODE_DATA_ACTIVE_ROW,
        GX_COLOR_CODE_DATA_ROWS, dataCells);
    gX_U8 dataBits[GX_COLOR_CODE_DATA_BITS];
    color_code_unpack_bits(dataCells, GX_COLOR_CODE_DATA_CELLS, dataBits,
        dataBitCount, bitsPerCell);
    gX_U8 erasedEncoded[GX_COLOR_CODE_MAX_ENCODED];
    memset(erasedEncoded, 0, sizeof(erasedEncoded));
    for (gX_U32 cell = 0; cell < GX_COLOR_CODE_DATA_CELLS; cell++)
    {
        if (dataCells[cell] != 0xFF) continue;
        for (gX_U32 bit = 0; bit < bitsPerCell; bit++)
        {
            gX_U32 bitIndex = cell * bitsPerCell + bit;
            if (bitIndex >= encodedBytes * GX_COLOR_CODE_ECC_BITS) continue;
            erasedEncoded[bitIndex / GX_COLOR_CODE_ECC_BITS] = 1;
        }
    }
    gX_U8 encoded[GX_COLOR_CODE_MAX_ENCODED];
    memset(encoded, 0, sizeof(encoded));
    gX_U16 corrected = 0;
    for (gX_U32 index = 0; index < encodedBytes; index++)
    {
        if (erasedEncoded[index]) continue;
        gX_U16 codeword = 0;
        for (gX_U32 bit = 0; bit < GX_COLOR_CODE_ECC_BITS; bit++)
            codeword |= (gX_U16)(dataBits[index * GX_COLOR_CODE_ECC_BITS + bit] << bit);
        gX_I1 fixed = 0;
        status = color_code_hamming_decode(codeword, &encoded[index], &fixed);
        if (status != GX_COLOR_CODE_OK)
        {
            result->status = status;
            return result->status;
        }
        if (fixed) corrected++;
    }
    result->correctedBits = corrected;
    gX_U16 recovered = 0;
    if (eccMode == GX_COLOR_CODE_ECC_RS)
    {
        for (gX_U32 block = 0; block < blockCount; block++)
        {
            gX_U8 codeword[GX_COLOR_CODE_RS_BLOCK];
            gX_U8 erased[GX_COLOR_CODE_RS_BLOCK];
            memset(codeword, 0, sizeof(codeword));
            memset(erased, 0, sizeof(erased));
            for (gX_U32 index = 0; index < GX_COLOR_CODE_RS_DATA; index++)
            {
                gX_U32 source = block + index * blockCount;
                if (source < compressedLength)
                {
                    codeword[index] = encoded[source];
                    erased[index] = erasedEncoded[source];
                }
            }
            for (gX_U32 parity = 0; parity < GX_COLOR_CODE_RS_PARITY; parity++)
            {
                gX_U32 source = compressedLength + block * GX_COLOR_CODE_RS_PARITY
                    + parity;
                codeword[GX_COLOR_CODE_RS_DATA + parity] = encoded[source];
                erased[GX_COLOR_CODE_RS_DATA + parity] = erasedEncoded[source];
            }
            gX_I32 rsStatus = color_code_rs_decode(codeword, erased);
            if (rsStatus != GX_COLOR_CODE_OK)
            {
                result->status = rsStatus;
                return result->status;
            }
            for (gX_U32 index = 0; index < GX_COLOR_CODE_RS_DATA; index++)
            {
                gX_U32 destination = block + index * blockCount;
                if (destination < compressedLength && erased[index])
                {
                    encoded[destination] = codeword[index];
                    recovered++;
                }
            }
        }
    }
    else
    {
        for (gX_U32 block = 0; block < blockCount; block++)
        {
            gX_U32 parityIndex = compressedLength + block;
            if (erasedEncoded[parityIndex])
            {
                result->status = GX_COLOR_CODE_ERR_ERASURE;
                return result->status;
            }
            gX_U32 erasedCount = 0;
            gX_U32 erasedIndex = 0;
            for (gX_U32 index = block; index < compressedLength; index += blockCount)
                if (erasedEncoded[index])
                {
                    erasedCount++;
                    erasedIndex = index;
                }
            if (erasedCount > 1)
            {
                result->status = GX_COLOR_CODE_ERR_ERASURE;
                return result->status;
            }
            if (erasedCount == 1)
            {
                gX_U8 value = encoded[parityIndex];
                for (gX_U32 index = block; index < compressedLength; index += blockCount)
                    if (index != erasedIndex) value ^= encoded[index];
                encoded[erasedIndex] = value;
                recovered++;
            }
        }
    }
    result->correctedErasures = recovered;
    gX_U8 compressed[GX_COLOR_CODE_MAX_COMPRESSED];
    memcpy(compressed, encoded, compressedLength);
    if (compressed[0] != meta[4])
    {
        result->status = GX_COLOR_CODE_ERR_COMPRESSION;
        return result->status;
    }
    gX_U8 payload[GX_COLOR_CODE_MAX_PAYLOAD];
    gX_U32 decodedLength = 0;
    status = color_code_decompress(compressed[0], compressed + 1,
        compressedLength - 1, payload, sizeof(payload), &decodedLength);
    if (status != GX_COLOR_CODE_OK)
    {
        result->status = status;
        return result->status;
    }
    if (decodedLength != payloadLength
        || color_code_crc32(payload, decodedLength) != color_code_get_u32(meta + 14))
    {
        result->status = GX_COLOR_CODE_ERR_CHECKSUM;
        return result->status;
    }
    memcpy(result->payload, payload, decodedLength);
    result->payloadLength = (gX_U16)decodedLength;
    result->compressedLength = (gX_U16)compressedLength;
    result->frameIndex = meta[10];
    result->frameTotal = meta[11];
    result->payloadID = color_code_get_u16(meta + 12);
    result->fullPayloadCRC = color_code_get_u32(meta + 18);
    result->orientation = orientation;
    result->status = GX_COLOR_CODE_OK;
    return GX_COLOR_CODE_OK;
}

gX_I32 gX_ColorCode_Render(const gX_ColorCodeFrame *frame, gX_U8 *pixels,
    gX_U32 width, gX_U32 height, gX_U32 originX, gX_U32 originY,
    gX_U32 cellSize)
{
    if (!frame || !pixels || cellSize == 0) return GX_COLOR_CODE_ERR_ARGUMENT;
    gX_U64 required = (gX_U64)GX_COLOR_CODE_GRID * cellSize;
    if ((gX_U64)originX + required > width
        || (gX_U64)originY + required > height)
        return GX_COLOR_CODE_ERR_GEOMETRY;
    gX_U64 bytes = (gX_U64)width * height * 4;
    if (bytes > GX_COLOR_CODE_MAX_IMAGE_BYTES) return GX_COLOR_CODE_ERR_CAPACITY;
    for (gX_U32 y = 0; y < height; y++)
        for (gX_U32 x = 0; x < width; x++)
        {
            gX_U64 offset = ((gX_U64)y * width + x) * 4;
            pixels[offset + 0] = 24;
            pixels[offset + 1] = 24;
            pixels[offset + 2] = 24;
            pixels[offset + 3] = 255;
        }
    for (gX_U32 row = 0; row < GX_COLOR_CODE_GRID; row++)
    {
        for (gX_U32 col = 0; col < GX_COLOR_CODE_GRID; col++)
        {
            gX_U8 value = frame->cells[row * GX_COLOR_CODE_GRID + col];
            gX_U8 index = value;
            if (frame->palette == GX_COLOR_CODE_PALETTE_8COLOR
                && row >= GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
                && row < GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
                    + GX_COLOR_CODE_META_ROWS + GX_COLOR_CODE_DATA_ROWS
                && col >= GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
                && col < GX_COLOR_CODE_QUIET + GX_COLOR_CODE_BORDER
                    + GX_COLOR_CODE_INNER)
                index = gX_ColorCode8Palette[value & 7];
            gX_U8 r = 0;
            gX_U8 g = 0;
            gX_U8 b = 0;
            gX_ColorCode_Palette(index, &r, &g, &b);
            gX_U32 startX = originX + col * cellSize;
            gX_U32 startY = originY + row * cellSize;
            for (gX_U32 y = 0; y < cellSize; y++)
                for (gX_U32 x = 0; x < cellSize; x++)
                {
                    gX_U64 offset = ((gX_U64)(startY + y) * width
                        + (startX + x)) * 4;
                    pixels[offset + 0] = r;
                    pixels[offset + 1] = g;
                    pixels[offset + 2] = b;
                    pixels[offset + 3] = 255;
                }
        }
    }
    return GX_COLOR_CODE_OK;
}
