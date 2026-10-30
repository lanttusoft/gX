/**********************************************************************************************
*
*   [gX: src/renderer/gfx/fullscreen_pass.c]
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


//opencode: renderer-layer helper for explicit fullscreen shader passes.
#include "fullscreen_pass.h"

#include "../../wrappers/wrappers.h"

gX_I0 gX_RendererGFX_Fullscreen_Begin(gX_RenderTexture target, gX_Shader shader)
{
    gX_Drawing_BeginTexture(target);
    gX_Drawing_BeginShader(shader);
}

gX_I0 gX_RendererGFX_Fullscreen_Draw(gX_F32 x, gX_F32 y, gX_F32 width, gX_F32 height)
{
    gX_Drawing_DrawScreenQuad(x, y, width, height);
}

gX_I0 gX_RendererGFX_Fullscreen_End(gX_I0)
{
    gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
}
