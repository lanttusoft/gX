/**********************************************************************************************
*
*   [gX: src/renderer/teletext.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#ifndef RENDERER_TELETEXT_H
#define RENDERER_TELETEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define gX_TELETEXT_LINES 32
#define gX_TELETEXT_CELLS_PER_LINE 48
#define gX_TELETEXT_MAX_PAGES 5

//opencode: layout and rendering constants
#define GX_TELETEXT_CHAR_SPACE  32    // space codepoint for clearing/padding
#define GX_TELETEXT_FULL_BLOCK  219   // █ full block codepoint
#define GX_TELETEXT_FONT_SIZE   24.0f // glyph render size
#define GX_TELETEXT_TEXT_PAD    2.0f  // text offset inside cell
#define GX_TELETEXT_CELL_W      28.0f // cell width in pixels
#define GX_TELETEXT_CELL_H      32.0f // cell height in pixels
#define GX_TELETEXT_GRID_X      32.0f // grid start X
#define GX_TELETEXT_GRID_Y      64.0f // grid start Y
#define GX_TELETEXT_HEADER_ROW  0     // header row index
#define GX_TELETEXT_HEADER_BAR  1     // header bar row index

typedef struct gX_RendererTeletext_Cell {
    gX_I32 codepoint;
    gX_Color color_text;
    gX_Color color_bg;
} gX_RendererTeletext_Cell;

//opencode: stored page — can be switched into the active display
typedef struct gX_RendererTeletext_Page {
    gX_I32 number;
    gX_RendererTeletext_Cell cells[gX_TELETEXT_LINES][gX_TELETEXT_CELLS_PER_LINE];
} gX_RendererTeletext_Page;

typedef struct gX_RendererTeletext {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 loaded;
    gX_I1 refresh;
    gX_F32 refresh_counter;

    gX_I32 current_page;
    gX_U32 currentPageIndex;
    gX_U32 pageCount;
    gX_RendererTeletext_Page pages[gX_TELETEXT_MAX_PAGES];

    gX_RendererTeletext_Cell cells[gX_TELETEXT_LINES][gX_TELETEXT_CELLS_PER_LINE];

    //opencode: auto-advance timer (seconds of inactivity)
    gX_F32 autoAdvanceTimer;
    gX_I1 autoAdvanceEnabled;

    gX_Font font;

} gX_RendererTeletext;

gX_I32 gX_RendererTeletext_Load(gX_RendererTeletext *ttx);
gX_I32 gX_RendererTeletext_Unload(gX_RendererTeletext *ttx);

gX_I0 gX_RendererTeletext_Update(gX_RendererTeletext *ttx);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas)
gX_I0 gX_RendererTeletext_DrawTo(gX_RendererTeletext *ttx, gX_RenderTexture *outputRT);
#define gX_RendererTeletext_Draw(ttx) gX_RendererTeletext_DrawTo((ttx), NULL)

//opencode: switch to page by page number (scans pages[] for matching number)
gX_I0 gX_RendererTeletext_SetPage(gX_RendererTeletext *ttx, gX_I32 pageNum);

//opencode: diagnostic output for AI probe harness
gX_I0 gX_RendererTeletext_Diagnostic(gX_RendererTeletext *ttx);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_TELETEXT_H
