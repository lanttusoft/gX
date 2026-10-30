/**********************************************************************************************
*
*   [gX: src/renderer/teletext.c]
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


//opencode: teletext renderer — page system, demo content, keyboard navigation, diagnostic
#include "teletext.h"

#include <string.h>
#include <time.h>
#include <math.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/path.h"
#include "../main.h"
#include "../wrappers/wrappers.h"

#include "renderer.h"

//opencode: auto-advance idle timeout in seconds
#define GX_TELETEXT_AUTO_ADVANCE_DELAY 15.0f

//opencode: helper — set a single cell in a stored page
static gX_I0 gX_teletext_set_cell(gX_RendererTeletext *ttx, gX_U32 pageIdx, gX_U32 row, gX_U32 col, gX_I32 cp, gX_Color fg, gX_Color bg) {
    if (pageIdx >= gX_TELETEXT_MAX_PAGES) return;
    if (row >= gX_TELETEXT_LINES || col >= gX_TELETEXT_CELLS_PER_LINE) return;
    ttx->pages[pageIdx].cells[row][col].codepoint = cp;
    ttx->pages[pageIdx].cells[row][col].color_text = fg;
    ttx->pages[pageIdx].cells[row][col].color_bg = bg;
}

//opencode: helper — fill a full row of a stored page from a null-terminated string
static gX_I0 gX_teletext_fill_line(gX_RendererTeletext *ttx, gX_U32 pageIdx, gX_U32 row, const gX_I8 *text, gX_Color fg, gX_Color bg) {
    gX_U32 len = (gX_U32)strlen(text);
    if (len > gX_TELETEXT_CELLS_PER_LINE) len = gX_TELETEXT_CELLS_PER_LINE;
    for (gX_U32 col = 0; col < len; col++) {
        gX_teletext_set_cell(ttx, pageIdx, row, col, (gX_I32)text[col], fg, bg);
    }
    for (gX_U32 col = len; col < gX_TELETEXT_CELLS_PER_LINE; col++) {
        gX_teletext_set_cell(ttx, pageIdx, row, col, GX_TELETEXT_CHAR_SPACE, fg, bg);
    }
}

//opencode: helper — fill a range of columns in a row
static gX_I0 gX_teletext_fill_span(gX_RendererTeletext *ttx, gX_U32 pageIdx, gX_U32 row, gX_U32 startCol, const gX_I8 *text, gX_Color fg, gX_Color bg) {
    gX_U32 len = (gX_U32)strlen(text);
    for (gX_U32 col = 0; col < len && (startCol + col) < gX_TELETEXT_CELLS_PER_LINE; col++) {
        gX_teletext_set_cell(ttx, pageIdx, row, startCol + col, (gX_I32)text[col], fg, bg);
    }
}

//opencode: helper — draw a horizontal rule across the full page width
static gX_I0 gX_teletext_hr(gX_RendererTeletext *ttx, gX_U32 pageIdx, gX_U32 row, gX_I8 ch, gX_Color fg, gX_Color bg) {
    for (gX_U32 col = 0; col < gX_TELETEXT_CELLS_PER_LINE; col++) {
        gX_teletext_set_cell(ttx, pageIdx, row, col, (gX_I32)ch, fg, bg);
    }
}

//opencode: populate page 100 — welcome / system info
static gX_I0 gX_teletext_populate_100(gX_RendererTeletext *ttx, gX_U32 pageIdx) {
    ttx->pages[pageIdx].number = 100;
    gX_Color bg = {0, 0, 40, 255};
    gX_Color yellow = {255, 255, 0, 255};
    gX_Color white = {200, 200, 200, 255};
    gX_Color cyan = {0, 255, 255, 255};
    gX_Color green = {0, 200, 0, 255};

    // Clear page
    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_teletext_set_cell(ttx, pageIdx, r, c, 32, white, bg);
        }
    }

    // Row 1: full-width header bar
    gX_Color hdrBg = {0, 0, 128, 255};
    gX_teletext_hr(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, GX_TELETEXT_CHAR_SPACE, white, hdrBg);
    gX_teletext_fill_span(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, 12, "TELETEXT 100", yellow, hdrBg);

    // Row 3: welcome message
    gX_teletext_fill_line(ttx, pageIdx, 3, "* * *   W E L C O M E   * * *", yellow, bg);

    // Row 5-7: engine info
    gX_teletext_fill_line(ttx, pageIdx, 5, "ENGINE:  gameX / gX", cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 6, 0, "STATUS:  Teletext renderer active", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 7, 0, "PAGES:   100-104  (use PGUP/PGDN)", white, bg);

    // Row 9: separator
    gX_teletext_hr(ttx, pageIdx, 9, 61, yellow, bg);  // '='

    // Row 11-13: navigation help
    gX_teletext_fill_span(ttx, pageIdx, 11, 4, "NAVIGATION:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 12, 4, "PGUP  -- next page", white, bg);
    gX_teletext_fill_span(ttx, pageIdx, 13, 4, "PGDN  -- previous page", white, bg);

    // Row 15-17: autoadvance info
    gX_teletext_fill_span(ttx, pageIdx, 15, 4, "Auto-advance cycles every 15s", cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 16, 4, "Press any key to reset timer", white, bg);

    // Row 19: separator
    gX_teletext_hr(ttx, pageIdx, 19, 61, yellow, bg);

    // Row 21-23: demo pages listing
    gX_teletext_fill_span(ttx, pageIdx, 21, 4, "AVAILABLE PAGES:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 22, 4, "100  Welcome Screen", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 23, 4, "101  System Information", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 24, 4, "102  Game Data", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 25, 4, "103  Color Chart", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 26, 4, "104  Pattern Demo", green, bg);
}

//opencode: populate page 101 — system information
static gX_I0 gX_teletext_populate_101(gX_RendererTeletext *ttx, gX_U32 pageIdx) {
    ttx->pages[pageIdx].number = 101;
    gX_Color bg = {10, 10, 10, 255};
    gX_Color yellow = {255, 255, 0, 255};
    gX_Color white = {200, 200, 200, 255};
    gX_Color cyan = {0, 220, 220, 255};
    gX_Color green = {0, 200, 0, 255};

    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_teletext_set_cell(ttx, pageIdx, r, c, 32, white, bg);
        }
    }

    gX_Color hdrBg = {80, 0, 0, 255};
    gX_teletext_hr(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, GX_TELETEXT_CHAR_SPACE, white, hdrBg);
    gX_teletext_fill_span(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, 12, "TELETEXT 101", yellow, hdrBg);

    gX_teletext_fill_span(ttx, pageIdx, 3, 2, "SYSTEM INFORMATION", yellow, bg);
    gX_teletext_hr(ttx, pageIdx, 4, 45, cyan, bg);  // '-'

    gX_teletext_fill_span(ttx, pageIdx, 6,  2, "BUILD:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 6, 10, __DATE__ " " __TIME__, white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 8,  2, "RESOLUTION:", yellow, bg);
    // Dynamic content updated in Update

    gX_teletext_fill_span(ttx, pageIdx, 10, 2, "FRAMERATE:", yellow, bg);
    // Dynamic

    gX_teletext_fill_span(ttx, pageIdx, 12, 2, "RENDERER:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 12, 13, "Teletext 32x48 cell grid", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 14, 2, "FONT:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 14, 9, "EuropeanTeletext.ttf", white, bg);

    gX_teletext_hr(ttx, pageIdx, 16, 45, cyan, bg);

    gX_teletext_fill_span(ttx, pageIdx, 18, 2, "PAGE NAVIGATION:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 20, 2, "PGUP  = NEXT PAGE", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 21, 2, "PGDN  = PREV PAGE", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 23, 2, "HOME  = PAGE 100", green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 24, 2, "END   = LAST PAGE", green, bg);

    gX_teletext_hr(ttx, pageIdx, 26, 45, cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 28, 2, "STATUS:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 28, 10, "OPERATIONAL", green, bg);
}

//opencode: populate page 102 — game data placeholder
static gX_I0 gX_teletext_populate_102(gX_RendererTeletext *ttx, gX_U32 pageIdx) {
    ttx->pages[pageIdx].number = 102;
    gX_Color bg = {0, 20, 0, 255};
    gX_Color yellow = {255, 255, 0, 255};
    gX_Color white = {200, 200, 200, 255};
    gX_Color green = {0, 200, 0, 255};
    gX_Color cyan = {0, 255, 255, 255};

    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_teletext_set_cell(ttx, pageIdx, r, c, 32, white, bg);
        }
    }

    gX_Color hdrBg = {0, 64, 0, 255};
    gX_teletext_hr(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, GX_TELETEXT_CHAR_SPACE, white, hdrBg);
    gX_teletext_fill_span(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, 12, "TELETEXT 102", yellow, hdrBg);

    gX_teletext_fill_span(ttx, pageIdx, 3, 2, "GAME DATA", yellow, bg);
    gX_teletext_hr(ttx, pageIdx, 4, 61, green, bg);

    gX_teletext_fill_span(ttx, pageIdx, 6,  2, "PLAYER:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 6, 11, "---", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 8,  2, "LEVEL:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 8, 10, "---", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 10, 2, "HP:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 10, 7, "--- / ---", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 12, 2, "MP:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 12, 7, "--- / ---", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 14, 2, "GIL:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 14, 8, "---", white, bg);

    gX_teletext_fill_span(ttx, pageIdx, 16, 2, "LOCATION:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 16, 13, "---", white, bg);

    gX_teletext_hr(ttx, pageIdx, 18, 61, green, bg);

    gX_teletext_fill_span(ttx, pageIdx, 20, 2, "PARTY:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 21, 2, "  1. ---", cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 22, 2, "  2. ---", cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 23, 2, "  3. ---", cyan, bg);

    gX_teletext_hr(ttx, pageIdx, 25, 61, green, bg);
    gX_teletext_fill_span(ttx, pageIdx, 27, 2, "DATA LOAD:", yellow, bg);
    gX_teletext_fill_span(ttx, pageIdx, 27, 13, "PENDING -- no save file", white, bg);
}

//opencode: populate page 103 — color chart
static gX_I0 gX_teletext_populate_103(gX_RendererTeletext *ttx, gX_U32 pageIdx) {
    ttx->pages[pageIdx].number = 103;
    gX_Color bg = {0, 0, 0, 255};
    gX_Color yellow = {255, 255, 0, 255};
    gX_Color white = {200, 200, 200, 255};

    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_teletext_set_cell(ttx, pageIdx, r, c, 32, white, bg);
        }
    }

    gX_Color hdrBg = {64, 0, 64, 255};
    gX_teletext_hr(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, GX_TELETEXT_CHAR_SPACE, white, hdrBg);
    gX_teletext_fill_span(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, 12, "TELETEXT 103", yellow, hdrBg);

    gX_teletext_fill_span(ttx, pageIdx, 3, 2, "COLOR CHART", yellow, bg);
    gX_teletext_hr(ttx, pageIdx, 4, 45, white, bg);

    // Color rows: each row has a text label and a colored background span
    struct { gX_I32 r, g, b; const gX_I8 *label; } colors[] = {
        {255, 0, 0,   "RED"},
        {0, 255, 0,   "GREEN"},
        {0, 0, 255,   "BLUE"},
        {255, 255, 0, "YELLOW"},
        {0, 255, 255, "CYAN"},
        {255, 0, 255, "MAGENTA"},
        {255, 128, 0, "ORANGE"},
        {128, 128, 128, "GRAY"},
    };

    for (gX_U32 i = 0; i < 8 && (6 + (gX_I32)i * 2) < (gX_I32)gX_TELETEXT_LINES; i++) {
        gX_U32 row = 6 + i * 2;
        gX_Color fg = {colors[i].r, colors[i].g, colors[i].b, 255};
        // Draw color block (8 chars wide)
        for (gX_U32 c = 2; c < 10; c++) {
            gX_teletext_set_cell(ttx, pageIdx, row, c, GX_TELETEXT_FULL_BLOCK, fg, fg);  // █ full block
        }
        gX_teletext_fill_span(ttx, pageIdx, row, 12, colors[i].label, fg, bg);
    }

    gX_teletext_hr(ttx, pageIdx, 23, 45, white, bg);
    gX_teletext_fill_span(ttx, pageIdx, 25, 2, "Color blocks use codepoint 219 (full block)", white, bg);
    gX_teletext_fill_span(ttx, pageIdx, 26, 2, "Text rendered in matching color above", white, bg);
}

//opencode: populate page 104 — pattern demo
static gX_I0 gX_teletext_populate_104(gX_RendererTeletext *ttx, gX_U32 pageIdx) {
    ttx->pages[pageIdx].number = 104;
    gX_Color bg = {0, 0, 30, 255};
    gX_Color yellow = {255, 255, 0, 255};
    gX_Color white = {200, 200, 200, 255};
    gX_Color cyan = {0, 255, 255, 255};
    gX_Color green = {0, 200, 0, 255};

    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_teletext_set_cell(ttx, pageIdx, r, c, 32, white, bg);
        }
    }

    gX_Color hdrBg = {64, 0, 0, 255};
    gX_teletext_hr(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, GX_TELETEXT_CHAR_SPACE, white, hdrBg);
    gX_teletext_fill_span(ttx, pageIdx, GX_TELETEXT_HEADER_BAR, 12, "TELETEXT 104", yellow, hdrBg);

    gX_teletext_fill_span(ttx, pageIdx, 3, 2, "PATTERN DEMO", yellow, bg);
    gX_teletext_hr(ttx, pageIdx, 4, 45, cyan, bg);

    // Build a checkered pattern in the center of the page
    gX_Color fgA = {255, 200, 0, 255};
    gX_Color fgB = {0, 200, 255, 255};
    gX_Color bgA = {40, 20, 0, 255};
    gX_Color bgB = {0, 20, 40, 255};

    for (gX_U32 r = 0; r < 14; r++) {
        gX_U32 row = 6 + r;
        for (gX_U32 c = 0; c < 28; c++) {
            gX_U32 col = 10 + c;
            gX_I1 isEven = ((r + c) % 2) == 0;
            gX_teletext_set_cell(ttx, pageIdx, row, col, GX_TELETEXT_FULL_BLOCK,
                isEven ? fgA : fgB,
                isEven ? bgA : bgB);
        }
    }

    // Label the pattern
    gX_teletext_fill_span(ttx, pageIdx, 21, 10, "<<< CHECKERBOARD >>>", green, bg);

    gX_teletext_hr(ttx, pageIdx, 23, 45, cyan, bg);
    gX_teletext_fill_span(ttx, pageIdx, 25, 2, "Full block (cp=219) with alternating colors", white, bg);
    gX_teletext_fill_span(ttx, pageIdx, 26, 2, "14 rows x 28 columns pattern area", white, bg);
    gX_teletext_fill_span(ttx, pageIdx, 27, 2, "Col 10 to 37, Rows 6 to 19", white, bg);
}

//opencode: copy page cells into the active display buffer, preserving header row
static gX_I0 gX_teletext_activate_page(gX_RendererTeletext *ttx, gX_U32 idx) {
    if (idx >= gX_TELETEXT_MAX_PAGES || idx >= ttx->pageCount) return;
    ttx->currentPageIndex = idx;
    ttx->current_page = ttx->pages[idx].number;

    // Copy all cells from the stored page into the display buffer
    for (gX_U32 r = 0; r < gX_TELETEXT_LINES; r++) {
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            ttx->cells[r][c] = ttx->pages[idx].cells[r][c];
        }
    }

    ttx->refresh = true;
    ttx->refresh_counter = 0.0f;  // force immediate header update
}

//opencode: copy current time into header row (row 0) of display buffer
static gX_I0 gX_teletext_update_header(gX_RendererTeletext *ttx) {
    time_t tim = time(0);
    struct tm tm = *localtime(&tim);
    gX_I8 header[gX_TELETEXT_CELLS_PER_LINE + 1];
    gX_I32 pageNum = ttx->current_page;
    snprintf(header, sizeof(header), "%.4d-%.2d-%.2d %.2d:%.2d:%.2d  PAGE %d",
        1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, pageNum);
    gX_U32 len = (gX_U32)strlen(header);
    gX_Color hdrFg = {255, 255, 0, 255};
    gX_Color hdrBg = {0, 0, 128, 255};

    // Clear header row
    for (gX_U32 i = 0; i < gX_TELETEXT_CELLS_PER_LINE; i++) {
        if (i < len) {
            ttx->cells[0][i].codepoint = (gX_I32)header[i];
            ttx->cells[0][i].color_text = hdrFg;
            ttx->cells[0][i].color_bg = hdrBg;
        } else {
            ttx->cells[0][i].codepoint = GX_TELETEXT_CHAR_SPACE;
            ttx->cells[0][i].color_text = hdrFg;
            ttx->cells[0][i].color_bg = hdrBg;
        }
    }
}

gX_I32 gX_RendererTeletext_Load(gX_RendererTeletext *ttx) {
    gX_TRACELOG_PROLOGUE("RendererTeletext_Load","RENDERER_TELETEXT: Loading")
    strcpy(ttx->name, "[gX] Teletext");
    strcpy(ttx->description, "teletext display with page system");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", ttx->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", ttx->description);
    // Load EuropeanTeletext font for glyph rendering

    ttx->font = gX_Font_Load(GX_PATH("font", "EuropeanTeletext.ttf"));
    gX_TraceLog_RENDERER(gX_LOG_INFO, "teletext font loaded (glyphs=%d)",
        ttx->font.glyphCount);

    // Initialize display cells to spaces on black
    for (gX_U32 k = 0; k < gX_TELETEXT_LINES; k++) {
        for (gX_U32 i = 0; i < gX_TELETEXT_CELLS_PER_LINE; i++) {
            ttx->cells[k][i].codepoint = GX_TELETEXT_CHAR_SPACE;
            ttx->cells[k][i].color_text = gX_WHITE;
            ttx->cells[k][i].color_bg = gX_BLACK;
        }
    }

    // Populate demo pages
    ttx->pageCount = 0;

    gX_teletext_populate_100(ttx, 0); ttx->pageCount++;
    gX_teletext_populate_101(ttx, 1); ttx->pageCount++;
    gX_teletext_populate_102(ttx, 2); ttx->pageCount++;
    gX_teletext_populate_103(ttx, 3); ttx->pageCount++;
    gX_teletext_populate_104(ttx, 4); ttx->pageCount++;

    // Activate page 100
    gX_teletext_activate_page(ttx, 0);

    // Auto-advance setup
    ttx->autoAdvanceTimer = 0.0f;
    ttx->autoAdvanceEnabled = true;

    ttx->refresh = true;
    gX_TraceLog_RENDERER(gX_LOG_INFO, "RENDERER_TELETEXT: Load done (%u pages)", ttx->pageCount);
    gX_TRACELOG_EPILOGUE("RENDERER_TELETEXT: Load done")
    return 0;
}

gX_I32 gX_RendererTeletext_Unload(gX_RendererTeletext *ttx) {
    (void)ttx;
    if (ttx->font.texture.id > 0) gX_Font_Unload(ttx->font);
    return 0;
}

gX_I0 gX_RendererTeletext_SetPage(gX_RendererTeletext *ttx, gX_I32 pageNum) {
    for (gX_U32 i = 0; i < ttx->pageCount; i++) {
        if (ttx->pages[i].number == pageNum) {
            gX_teletext_activate_page(ttx, i);
            return;
        }
    }
}

gX_I0 gX_RendererTeletext_Update(gX_RendererTeletext *ttx) {
    gX_F32 dt = gX_FrameTime_Get();

    //opencode: keyboard page navigation
    gX_I1 navigated = false;

    if (gX_Key_IsPressed(gX_KEY_PAGE_DOWN) || gX_Key_IsPressed(gX_KEY_DOWN)) {
        if (ttx->currentPageIndex + 1 < ttx->pageCount) {
            gX_teletext_activate_page(ttx, ttx->currentPageIndex + 1);
        } else {
            gX_teletext_activate_page(ttx, 0);  // wrap to first
        }
        navigated = true;
    }

    if (gX_Key_IsPressed(gX_KEY_PAGE_UP) || gX_Key_IsPressed(gX_KEY_UP)) {
        if (ttx->currentPageIndex > 0) {
            gX_teletext_activate_page(ttx, ttx->currentPageIndex - 1);
        } else {
            gX_teletext_activate_page(ttx, ttx->pageCount - 1);  // wrap to last
        }
        navigated = true;
    }

    //opencode: HOME goes to page 100, END goes to last page
    if (gX_Key_IsPressed(gX_KEY_HOME)) {
        gX_teletext_activate_page(ttx, 0);
        navigated = true;
    }
    if (gX_Key_IsPressed(gX_KEY_END)) {
        if (ttx->pageCount > 0) {
            gX_teletext_activate_page(ttx, ttx->pageCount - 1);
        }
        navigated = true;
    }

    //opencode: reset auto-advance timer on any navigation or key press
    if (navigated) {
        ttx->autoAdvanceTimer = 0.0f;
    } else {
        // Check if any key was pressed (reset timer, but don't navigate)
        if (gX_Key_IsPressed(gX_KEY_SPACE) || gX_Key_IsPressed(gX_KEY_ENTER) ||
            gX_Key_IsPressed(gX_KEY_BACKSPACE) || gX_Key_IsPressed(gX_KEY_ESCAPE)) {
            ttx->autoAdvanceTimer = 0.0f;
        }
    }

    //opencode: auto-advance — cycle through pages after idle timeout
    if (ttx->autoAdvanceEnabled && ttx->pageCount > 1) {
        ttx->autoAdvanceTimer += dt;
        if (ttx->autoAdvanceTimer >= GX_TELETEXT_AUTO_ADVANCE_DELAY) {
            gX_U32 nextIdx = (ttx->currentPageIndex + 1) % ttx->pageCount;
            gX_teletext_activate_page(ttx, nextIdx);
            ttx->autoAdvanceTimer = 0.0f;
        }
    }

    // Update timestamp in header row (row 0) every second
    ttx->refresh_counter -= dt;
    if (ttx->refresh_counter <= 0.0f) {
        gX_teletext_update_header(ttx);
        ttx->refresh_counter = 1.0f;
        ttx->refresh = true;
    }
}

gX_I0 gX_RendererTeletext_DrawTo(gX_RendererTeletext *ttx, gX_RenderTexture *outputRT) {
    if (!ttx->refresh) return;
    if (ttx->font.texture.id == 0) return;
    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG(gX_BLACK);
        for (gX_U32 k = 0; k < gX_TELETEXT_LINES; k++) {
            for (gX_U32 i = 0; i < gX_TELETEXT_CELLS_PER_LINE; i++) {
                gX_Rectangle cellRect = {
                    GX_TELETEXT_GRID_X + GX_TELETEXT_CELL_W * (gX_F32)i,
                    GX_TELETEXT_GRID_Y + GX_TELETEXT_CELL_H * (gX_F32)k,
                    GX_TELETEXT_CELL_W, GX_TELETEXT_CELL_H
                };
                gX_Rectangle_Draw(cellRect, ttx->cells[k][i].color_bg);
                gX_I32 cp = ttx->cells[k][i].codepoint;
                if (cp > 0 && cp <= 255) {
                    gX_I8 ch[2] = { (gX_I8)cp, 0 };
                    gX_RawString_Draw_Lesser(ch,
                        (gX_F32v2){ cellRect.x + GX_TELETEXT_TEXT_PAD, cellRect.y + GX_TELETEXT_TEXT_PAD },
                        GX_TELETEXT_FONT_SIZE, ttx->cells[k][i].color_text);
                }
            }
        }
    gX_Drawing_EndTexture();
    ttx->refresh = false;
}

//opencode: diagnostic output for AI probe harness — reads cell grid state
gX_I0 gX_RendererTeletext_Diagnostic(gX_RendererTeletext *ttx) {
    if (!ttx) return;
    printf("SAMPLE teletext page=%d idx=%u count=%u\n",
        ttx->current_page, ttx->currentPageIndex, ttx->pageCount);

    // Read and print header row
    gX_I8 headerBuf[gX_TELETEXT_CELLS_PER_LINE + 1];
    gX_U32 hLen = 0;
    for (gX_U32 i = 0; i < gX_TELETEXT_CELLS_PER_LINE; i++) {
        gX_I32 cp = ttx->cells[0][i].codepoint;
        if (cp > 0 && cp <= 127) {
            headerBuf[hLen++] = (gX_I8)cp;
        } else {
            break;
        }
    }
    headerBuf[hLen] = 0;
    printf("SAMPLE teletext header \"%s\"\n", headerBuf);

    // Sample cells at key positions
    struct { gX_U32 row, col; const char *label; } cellSamples[] = {
        {0,  0,  "header_start"},
        {0, 20,  "header_mid"},
        {15, 24, "center"},
        {31, 0,  "last_line_start"},
        {1,  0,  "second_row_start"},
        {3,  0,  "content_start"},
    };
    gX_U32 nSamps = sizeof(cellSamples) / sizeof(cellSamples[0]);
    for (gX_U32 s = 0; s < nSamps; s++) {
        gX_U32 row = cellSamples[s].row;
        gX_U32 col = cellSamples[s].col;
        if (row >= gX_TELETEXT_LINES || col >= gX_TELETEXT_CELLS_PER_LINE) continue;
        gX_RendererTeletext_Cell *c = &ttx->cells[row][col];
        printf("SAMPLE teletext cell[%u][%u] %s cp=%d fg=(%d,%d,%d,%d) bg=(%d,%d,%d,%d)\n",
            row, col, cellSamples[s].label,
            c->codepoint,
            c->color_text.r, c->color_text.g, c->color_text.b, c->color_text.a,
            c->color_bg.r, c->color_bg.g, c->color_bg.b, c->color_bg.a);
    }

    // Dump all non-space cells in rows 2-5 as sample text
    for (gX_U32 r = 2; r <= 5; r++) {
        gX_I8 lineBuf[gX_TELETEXT_CELLS_PER_LINE + 1];
        gX_U32 lLen = 0;
        for (gX_U32 c = 0; c < gX_TELETEXT_CELLS_PER_LINE; c++) {
            gX_I32 cp = ttx->cells[r][c].codepoint;
            if (cp > 0 && cp <= 127) {
                lineBuf[lLen++] = (gX_I8)cp;
            } else if (cp != GX_TELETEXT_CHAR_SPACE) {
                lineBuf[lLen++] = '?';
            }
        }
        lineBuf[lLen] = 0;
        // Trim trailing spaces
        while (lLen > 0 && lineBuf[lLen - 1] == ' ') {
            lineBuf[--lLen] = 0;
        }
        if (lLen > 0) {
            printf("SAMPLE teletext line[%u] \"%s\"\n", r, lineBuf);
        }
    }

    fflush(stdout);
}
