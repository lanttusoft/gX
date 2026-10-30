/**********************************************************************************************
*
*   [gX: src/tools/gfx/surface_panels.h]
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

//ai: private leaf panels borrow editor services without owning selection, assets or history.
#ifndef GX_TOOLS_GFX_SURFACE_PANELS_H
#define GX_TOOLS_GFX_SURFACE_PANELS_H

#include "../../renderer/gfx.h"

#if gX_ENABLE_RENDERER == 1
void gfx_editor_water_panel(gX_RendererGFX *gfx, gX_I32 *SelectedWave,
    gX_I0 (*BrowseNormal)(gX_RendererGFX *),
    gX_I1 (*ColorEdit)(const gX_I8 *, gX_Color *),
    gX_I0 (*TextWrapped)(const gX_I8 *));
void gfx_editor_glass_panel(gX_RendererGFX_Model *model, gX_I32 ShaderQuality,
    gX_I1 (*ColorEdit)(const gX_I8 *, gX_Color *));
#endif

#endif
