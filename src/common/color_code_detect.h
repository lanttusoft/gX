/**********************************************************************************************
*
*   [gX: src/common/color_code_detect.h]
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

#ifndef GX_COMMON_COLOR_CODE_DETECT_H
#define GX_COMMON_COLOR_CODE_DETECT_H

#include "color_code.h"

#ifdef __cplusplus
extern "C" {
#endif

//ai: bounded detector result: full-grid corners in image order, ready for
//    gX_ColorCodeImage.perspective + autoOrientation.
typedef struct gX_ColorCodeDetection {
    gX_I8   name[128];
    gX_I8   description[256];
    gX_F32  corners[8];
    gX_U32  foregroundPixels;
    gX_I32  status;
} gX_ColorCodeDetection;

//ai: detect a bright/coloured code on a dark background and return the full grid quad.
gX_I32 gX_ColorCode_Detect(const gX_U8 *pixels, gX_U32 width, gX_U32 height,
    gX_ColorCodeDetection *detection);

#ifdef __cplusplus
}
#endif

#endif
