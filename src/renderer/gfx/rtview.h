/**********************************************************************************************
*
*   [gX: src/renderer/gfx/rtview.h]
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



#ifndef RENDERER_GFX_RTVIEW_H
#define RENDERER_GFX_RTVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "shader.h"

//opencode: RT inspector (GX_RT_VIEW=name[:gain[:bias]]) — draws a debug
//   render target fullscreen after presentation. GX_RT_DUMP_DIR optionally
//   writes native float readbacks for the selected frame range.
typedef struct gX_RendererGFX_RtView {
    gX_RendererGFX_Shader shader;
    gX_I1   locsCached;
    gX_I32  colorMapLoc;
    gX_I32  cubeMapLoc;
    gX_I32  cubeModeLoc;
    gX_I32  channelModeLoc;
    gX_I32  gainLoc;
    gX_I32  biasLoc;
} gX_RendererGFX_RtView;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_RTVIEW_H
