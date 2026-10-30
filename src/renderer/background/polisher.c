/**********************************************************************************************
*
*   [gX: src/renderer/background/polisher.c]
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


//ai: background polisher registration — the gameX contract (background_polisher.c)
//   ported natively: runtime copies of the scalar gX_BG_*_DEFVAL defaults are
//   registered under the "Background" category, unbounded, so the editor
//   panel can tune them live and the report prints fold-back source lines.
//   Unlike gameX's report-only mirrors, gX feeds these variables into the
//   parameter load path, so polishing the backdrop works at runtime.

#include "../background.h"
#include "../../common/polisher.h"

#include <string.h>

gX_F32 gX_Bg_ScaleDefval        = gX_BG_SCALE_DEFVAL;
gX_F32 gX_Bg_RotateDefval       = gX_BG_ROTATE_DEFVAL;
gX_F32 gX_Bg_MatrixblendDefval  = gX_BG_MATRIXBLEND_DEFVAL;
gX_F32 gX_Bg_WarpexpDefval      = gX_BG_WARPEXP_DEFVAL;
gX_I32 gX_Bg_PalmodeDefval      = gX_BG_PALMODE_DEFVAL;
gX_F32 gX_Bg_PalamountDefval    = gX_BG_PALAMOUNT_DEFVAL;
gX_F32 gX_Bg_PalspeedDefval     = gX_BG_PALSPEED_DEFVAL;
gX_F32 gX_Bg_PalfrequencyDefval = gX_BG_PALFREQUENCY_DEFVAL;
gX_F32 gX_Bg_PalbgboundDefval   = gX_BG_PALBGBOUND_DEFVAL;
gX_F32 gX_Bg_PalfgboundDefval   = gX_BG_PALFGBOUND_DEFVAL;

gX_I0 gX_Background_RegisterPolisher(gX_I0)
{
    gX_Polisher_RegisterFloat("gX_BG_SCALE_DEFVAL", "Background",
        &gX_Bg_ScaleDefval, 0.0f, 0.0f, 0.01f);
    gX_Polisher_RegisterFloat("gX_BG_ROTATE_DEFVAL", "Background",
        &gX_Bg_RotateDefval, 0.0f, 0.0f, 1.0f);
    gX_Polisher_RegisterFloat("gX_BG_MATRIXBLEND_DEFVAL", "Background",
        &gX_Bg_MatrixblendDefval, 0.0f, 0.0f, 0.01f);
    gX_Polisher_RegisterFloat("gX_BG_WARPEXP_DEFVAL", "Background",
        &gX_Bg_WarpexpDefval, 0.0f, 0.0f, 0.1f);
    gX_Polisher_RegisterInt("gX_BG_PALMODE_DEFVAL", "Background",
        &gX_Bg_PalmodeDefval, 0, 0, 1);
    gX_Polisher_RegisterFloat("gX_BG_PALAMOUNT_DEFVAL", "Background",
        &gX_Bg_PalamountDefval, 0.0f, 0.0f, 1.0f);
    gX_Polisher_RegisterFloat("gX_BG_PALSPEED_DEFVAL", "Background",
        &gX_Bg_PalspeedDefval, 0.0f, 0.0f, 0.01f);
    gX_Polisher_RegisterFloat("gX_BG_PALFREQUENCY_DEFVAL", "Background",
        &gX_Bg_PalfrequencyDefval, 0.0f, 0.0f, 0.01f);
    gX_Polisher_RegisterFloat("gX_BG_PALBGBOUND_DEFVAL", "Background",
        &gX_Bg_PalbgboundDefval, 0.0f, 0.0f, 0.01f);
    gX_Polisher_RegisterFloat("gX_BG_PALFGBOUND_DEFVAL", "Background",
        &gX_Bg_PalfgboundDefval, 0.0f, 0.0f, 0.01f);

    gX_Polisher_ApplyEnv();
    for (gX_U32 i = 0; i < polisher.count; i++) {
        if (!strcmp((const char *)polisher.entries[i].category,
                "Background"))
            polisher.entries[i].flags |= GX_POLISHER_UNBOUNDED;
    }
}
