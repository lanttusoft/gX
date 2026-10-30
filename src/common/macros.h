/**********************************************************************************************
*
*   [gX: src/common/macros.h]
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

#ifndef MACROS_H
#define MACROS_H

#define gX_BLANK            (gX_Color){0,0,0,0}
#define gX_BLACK            (gX_Color){0,0,0,255}
#define gX_RED_PURE         (gX_Color){255,0,0,255}
#define gX_GREEN_PURE       (gX_Color){0,255,0,255}
#define gX_BLUE_PURE        (gX_Color){0,0,255,255}
#define gX_CYAN_PURE        (gX_Color){0,255,255,255}
#define gX_YELLOW_PURE      (gX_Color){255,255,0,255}
#define gX_MAGENTA_PURE     (gX_Color){255,0,255,255}
#define gX_WHITE            (gX_Color){255,255,255,255}
#define gX_DARKRED_PURE     (gX_Color){127,0,0,255}
#define gX_DARKGREEN_PURE   (gX_Color){0,127,0,255}
#define gX_DARKBLUE_PURE    (gX_Color){0,0,127,255}
#define gX_DARKCYAN_PURE    (gX_Color){0,127,127,255}
#define gX_DARKYELLOW_PURE  (gX_Color){127,127,0,255}
#define gX_DARKMAGENTA_PURE (gX_Color){127,0,127,255}
#define gX_LIGHTGRAY        (gX_Color){191,191,191,255}
#define gX_GRAY             (gX_Color){127,127,127,255}
#define gX_DARKGRAY         (gX_Color){63,63,63,255}

#define gX_RED      gX_RED_PURE
#define gX_GREEN    gX_GREEN_PURE
#define gX_BLUE     gX_BLUE_PURE
#define gX_CYAN     gX_CYAN_PURE
#define gX_YELLOW   gX_YELLOW_PURE
#define gX_MAGENTA  gX_MAGENTA_PURE

#define gX_PURPLE gX_MAGENTA

// Texture parameters (equivalent to OpenGL defines)
#define gX_GL_TEXTURE_WRAP_S                    0x2802
#define gX_GL_TEXTURE_WRAP_T                    0x2803
#define gX_GL_TEXTURE_MAG_FILTER                0x2800
#define gX_GL_TEXTURE_MIN_FILTER                0x2801

#define gX_GL_TEXTURE_FILTER_NEAREST            0x2600
#define gX_GL_TEXTURE_FILTER_LINEAR             0x2601
#define gX_GL_TEXTURE_FILTER_MIP_NEAREST        0x2700
#define gX_GL_TEXTURE_FILTER_NEAREST_MIP_LINEAR 0x2702
#define gX_GL_TEXTURE_FILTER_LINEAR_MIP_NEAREST 0x2701
#define gX_GL_TEXTURE_FILTER_MIP_LINEAR         0x2703
#define gX_GL_TEXTURE_FILTER_ANISOTROPIC        0x3000
#define gX_GL_TEXTURE_MIPMAP_BIAS_RATIO         0x4000

#define gX_GL_TEXTURE_BORDER_COLOR              0x1004

#define gX_GL_FRAGMENT_SHADER                   0x8B30
#define gX_GL_VERTEX_SHADER                     0x8B31
#define gX_GL_COMPUTE_SHADER                    0x91B9

#define gX_GL_GEOMETRY_SHADER                   0x8DD9
#define gX_GL_TESS_CONTROL_SHADER               0x8E88
#define gX_GL_TESS_EVALUATION_SHADER            0x8E87

#endif //MACROS_H
