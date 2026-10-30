/**********************************************************************************************
*
*   [gX: src/common/types/text.h]
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

#ifndef TYPES_TEXT_H
#define TYPES_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "primary.h"
    
typedef gX_I8 gX_TextLine64[64];
typedef gX_I8 gX_TextLine128[128];
typedef gX_I8 gX_TextLine256[256];
typedef gX_I8 gX_TextLine512[512];
typedef gX_I8 gX_TextLine1024[1024];
typedef gX_TextLine128 gX_TextLine;

//gX_RawString := gX_I8*

typedef struct gX_String {
    gX_I8          *string;
    gX_U32          length;
} gX_String;
    
#ifdef __cplusplus
}
#endif

#endif //TYPES_TEXT_H
