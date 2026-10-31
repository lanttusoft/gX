/**********************************************************************************************
*
*   [gX: src/common/color_code_tv.h]
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

#ifndef GX_COMMON_COLOR_CODE_TV_H
#define GX_COMMON_COLOR_CODE_TV_H

#include "color_code.h"

#ifdef __cplusplus
extern "C" {
#endif

//ai: deterministic simulated-CRT view of a color-code frame; returns the
//    full-grid corners for the perspective decoder.
gX_I32 gX_ColorCodeTv_Render(const gX_ColorCodeFrame *frame, gX_U8 *pixels,
    gX_U32 width, gX_U32 height, gX_U32 seed, gX_F32 corners[8]);

#ifdef __cplusplus
}
#endif

#endif
