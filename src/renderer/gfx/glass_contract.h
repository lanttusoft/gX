/**********************************************************************************************
*
*   [gX: src/renderer/gfx/glass_contract.h]
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

//ai: private routing predicate shared by opaque exclusion and post-capture admission.
#ifndef GX_RENDERER_GFX_GLASS_CONTRACT_H
#define GX_RENDERER_GFX_GLASS_CONTRACT_H

#include "../../common/types.h"

//ai: opacity one removes alpha blending, not the need for current pre-glass scene color.
static inline gX_I1 gfx_glass_post_capture(gX_I1 Refraction, gX_I1 Blending,
    gX_F32 Opacity, gX_I32 Quality)
{
    return (Refraction && Quality >= 1) || (Blending && Opacity < 0.999f);
}

#endif
