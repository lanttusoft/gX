/**********************************************************************************************
*
*   [gX: src/common/utils/pointercast.h]
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

#ifndef UTILS_POINTERCAST_H
#define UTILS_POINTERCAST_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../types.h"
    
gX_I0 gX_PointerReceive(gX_I0 *target, gX_I32 datatype, gX_F32v4 *value);
gX_I0 gX_PointerCast(gX_I0 *target, gX_I32 datatype, gX_F32v4 *value);

//opencode: targetSize added — previously unbounded strcpy could overflow
//   caller buffers (data.c passed a 128-byte gX_TextLine against a 1024-byte
//   internal format buffer).
gX_I32 gX_FormatCast(gX_I8 *target, gX_U32 targetSize, const gX_I8 *format, gX_I32 datatype, gX_F32v4 *value);
gX_I32 gX_FormatCast_String(gX_I8 *target, gX_U32 targetSize, const gX_I8 *format, const gX_I8 *str);
    
#ifdef __cplusplus
}
#endif

#endif //UTILS_POINTERCAST_H
