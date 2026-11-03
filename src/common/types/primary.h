/**********************************************************************************************
*
*   [gX: src/common/types/primary.h]
*
*   Copyright (c) 2025 lanttu
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

#ifndef TYPES_PRIMARY_H
#define TYPES_PRIMARY_H

#include <stdbool.h>
#include <stdint.h>

#define gX_I0                     void
#define gX_I1                     bool

#define gX_I8                     char
#define gX_I16                    int16_t
#define gX_I32                    int32_t
#define gX_I64                    int64_t

#define gX_U8                     unsigned char
#define gX_U16                    uint16_t
#define gX_U32                    uint32_t
#define gX_U64                    uint64_t

#define gX_F32                    float
#define gX_F64                    double

//ai: compile-time alias contract — the same sizes and the bool identity the
//   type audit and every serialized struct depend on.
#if defined(__cplusplus)
static_assert(sizeof(gX_I1) == 1, "gX_I1 must be one byte");
static_assert(sizeof(gX_I8) == 1, "gX_I8 must be one byte");
static_assert(sizeof(gX_I16) == 2, "gX_I16 must be two bytes");
static_assert(sizeof(gX_I32) == 4, "gX_I32 must be four bytes");
static_assert(sizeof(gX_I64) == 8, "gX_I64 must be eight bytes");
static_assert(sizeof(gX_F32) == 4, "gX_F32 must be four bytes");
static_assert(sizeof(gX_F64) == 8, "gX_F64 must be eight bytes");
#else
_Static_assert(sizeof(gX_I1) == 1, "gX_I1 must be one byte");
_Static_assert(sizeof(gX_I8) == 1, "gX_I8 must be one byte");
_Static_assert(sizeof(gX_I16) == 2, "gX_I16 must be two bytes");
_Static_assert(sizeof(gX_I32) == 4, "gX_I32 must be four bytes");
_Static_assert(sizeof(gX_I64) == 8, "gX_I64 must be eight bytes");
_Static_assert(sizeof(gX_F32) == 4, "gX_F32 must be four bytes");
_Static_assert(sizeof(gX_F64) == 8, "gX_F64 must be eight bytes");
_Static_assert(_Generic((gX_I1)0, _Bool: 1, default: 0),
    "gX_I1 must be _Bool, not an integer");
#endif

#endif //TYPES_PRIMARY_H
