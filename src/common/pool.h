/**********************************************************************************************
*
*   [gX: src/common/pool.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
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


#ifndef GX_POOL_H
#define GX_POOL_H

//opencode: standalone pool allocator for the GUI handle/pool system.
// Generic flat-array pool with generation-gated handles. Pre-allocated
// buffer, no malloc. Designed for desktop and console (PS1/N64/PS2).
//
// Three-tier usage: GUI owns a pool of canvases, a pool of elements,
// and pools of modules (text, shader, texture, ...). Elements reference
// canvases by handle. Modules are referenced by elements via handles
// stored in named arrays (hTexts[4], hShaders[4], ...).
//
// Backward-capacity limits (GX_POOL_CAP_*) are defined per-platform.

#include "types.h"

#include <string.h>

//=============================================================================
// Handle — opaque U32: 16-bit index + 16-bit generation
//=============================================================================

typedef gX_U32 gX_Pool_Handle;

#define GX_POOL_NULL           ((gX_Pool_Handle)0)
#define GX_POOL_HANDLE(idx, gen) \
    ((gX_Pool_Handle)(((gX_U32)(gen) << 16) | (gX_U32)(idx)))
#define GX_POOL_IDX(h)         ((gX_U16)((h) & 0xFFFF))
#define GX_POOL_GEN(h)         ((gX_U16)((h) >> 16))
#define GX_POOL_VALID(h)       ((h) != 0)

//=============================================================================
// Pool struct
//=============================================================================

typedef struct {
    gX_U8    *data;          // pre-allocated buffer: elementSize * capacity bytes
    gX_U16   *generations;   // pre-allocated buffer: sizeof(gX_U16) * capacity bytes
    gX_U32    capacity;      // maximum number of slots
    gX_U32    count;         // current number of allocated slots
    gX_U32    elementSize;   // sizeof per slot
} gX_Pool;

//=============================================================================
// Pool API
//=============================================================================

// Init with pre-allocated buffers. dataBuf must be at least elementSize*capacity
// bytes. genBuf must be at least sizeof(gX_U16)*capacity bytes. Neither buffer
// is freed by the pool — the caller owns them.
gX_I0 gX_Pool_Init(gX_Pool *pool, void *dataBuf, gX_U16 *genBuf,
                     gX_U32 elementSize, gX_U32 capacity);

// Allocate one slot. Returns handle or GX_POOL_NULL if full.
gX_Pool_Handle gX_Pool_Alloc(gX_Pool *pool);

// Get typed pointer for a handle. Returns NULL if handle is null, out of
// range, or has a stale generation (pool was cleared since alloc).
void *gX_Pool_Get(gX_Pool *pool, gX_Pool_Handle h);

// Clear all slots. Resets count to 0, leaving data/generations intact.
// All previously-returned handles become stale (Get returns NULL).
gX_I0 gX_Pool_Clear(gX_Pool *pool);

// Return current allocation count.
gX_U32 gX_Pool_Count(const gX_Pool *pool);

//=============================================================================
// Slot types — the payloads stored in pools
//=============================================================================

// Module slot types
typedef struct { gX_I8 string[64]; gX_U16 fontSize; gX_U8 align; } gX_PoolText;
typedef struct { gX_I8 fragPath[128]; gX_I8 vertPath[128]; } gX_PoolShader;
typedef struct { gX_I32 id; gX_Rectangle src; } gX_PoolTexture;
typedef struct { gX_I32 id; gX_F32v3 pos, rot, scale; } gX_PoolModel;
typedef struct { gX_I32 type; gX_F32 params[8]; } gX_PoolShape;
typedef struct { gX_F32 timer; gX_F32 start[4], end[4]; gX_U8 easing; } gX_PoolAnim;
typedef struct { gX_I32 key, value; } gX_PoolData;
typedef struct { gX_I32 id; gX_I1 loop; } gX_PoolSound;

// Element slot type
typedef struct {
    gX_Pool_Handle hCanvas;          // which canvas this element renders for

    // Named arrays of module handles (GX_POOL_NULL = none)
    gX_Pool_Handle hTexts[4];
    gX_Pool_Handle hShaders[4];
    gX_Pool_Handle hTextures[4];
    gX_Pool_Handle hModels[2];
    gX_Pool_Handle hShapes[2];
    gX_Pool_Handle hAnims[4];
    gX_Pool_Handle hDatas[2];
    gX_Pool_Handle hSounds[2];

    gX_Rectangle rect;               // position and size
    gX_Color     color[4];           // corner colors (rect fill)
    gX_U32       flags;              // NO_DRAW, DRAW_WIRE, CENTER_*, IGNORE_OCCLUSION, ...
    gX_U8        layer;              // draw order within canvas (0 = back)
} gX_PoolElement;

// Canvas slot type
typedef struct {
    gX_Rectangle canvasBox;          // position and size on screen
    gX_Color     canvasHue;          // color multiplier for all elements
    gX_F32v2     canvasOffset;       // position offset for transitions
    gX_F32v2     canvasScale;        // scale for transitions
    gX_U32       stateFlags;         // canvas state
} gX_PoolCanvas;

//=============================================================================
// Per-backend capacity defaults
//=============================================================================

#define GX_POOL_CAP_CANVAS      16
#define GX_POOL_CAP_ELEMENTS    4096
#define GX_POOL_CAP_TEXT        4096
#define GX_POOL_CAP_SHADER      1024
#define GX_POOL_CAP_TEXTURE     1024
#define GX_POOL_CAP_MODEL       256
#define GX_POOL_CAP_SHAPE       1024
#define GX_POOL_CAP_ANIM        2048
#define GX_POOL_CAP_DATA        1024
#define GX_POOL_CAP_SOUND       512

#endif // GX_POOL_H
