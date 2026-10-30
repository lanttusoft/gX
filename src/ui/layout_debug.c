/**********************************************************************************************
*
*   [gX: src/ui/layout_debug.c]
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


//opencode: UI Layout Debug — element tracking, overlap/overflow detection, visual overlay,
//   pixel ruler (M), state explorer panel (R), live style editor ([ ] - =)
#include "layout_debug.h"
#include <math.h>
#include "../wrappers/text.h"
#include "../wrappers/shape.h"
#include "../wrappers/input.h"
#include "../gui/gui.h"
#include "../gui/canvas.h"
#include "../gui/element/struct.h"
#include "../main.h"
#include "../common/log.h"
#include <string.h>
#include <stdio.h>

gX_I1 gX_uiDebugEnabled = 0;
gX_I1 gX_uiLayoutTrackEnabled = 0;

// ── Layout element tracking ──
static gX_UI_LayoutEntry s_entries[GX_UI_LAYOUT_MAX_ENTRIES];
static gX_I32 s_entryCount = 0;

//opencode: generic GUI-canvas tracking — separate table so the manual
//   (battle raw-drawn HUD) entries are never cleared by the GUI path
static gX_UI_LayoutEntry s_guiEntries[GX_UI_LAYOUT_MAX_ENTRIES];
static gX_I32 s_guiEntryCount = 0;

static gX_I32 gX_UI_Layout_EntryInit(gX_UI_LayoutEntry *e, const gX_I8 *label,
    gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h)
{
    strncpy(e->label, label, sizeof(e->label) - 1);
    e->canvas[0] = '\0';
    e->x = x; e->y = y; e->w = w; e->h = h;
    e->isText = 0; e->isPanel = 0; e->offscreen = 0; e->overlapping = 0;
    return 0;
}

gX_I0 gX_UI_Layout_Clear(gX_I0) { s_entryCount = 0; }

gX_I0 gX_UI_Layout_GUI_Clear(gX_I0) { s_guiEntryCount = 0; }

gX_I32 gX_UI_Layout_GUI_Count(gX_I0) { return s_guiEntryCount; }

gX_I0 gX_UI_Layout_GUI_TrackRect(const gX_I8 *canvasName, const gX_I8 *label,
    gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h, gX_I1 isText, gX_I1 isPanel) {
    if (s_guiEntryCount >= GX_UI_LAYOUT_MAX_ENTRIES) return;
    gX_UI_LayoutEntry *e = &s_guiEntries[s_guiEntryCount++];
    gX_UI_Layout_EntryInit(e, label, x, y, w, h);
    if (canvasName) strncpy(e->canvas, canvasName, sizeof(e->canvas) - 1);
    e->r = 100; e->g = 200; e->b = 100; e->a = 180;
    e->isText = isText;
    e->isPanel = isPanel;
}

//opencode: gui_pick — hit-test screen coords against the tracked GUI elements.
//   Iterates back-to-front (later entries are drawn on top) so the first hit
//   is the topmost element; collects ALL hits into outBuf for full context.
gX_I32 gX_UI_Layout_GUI_Pick(gX_F32 x, gX_F32 y, gX_I8 *outBuf, gX_I32 outBufSize) {
    gX_I32 hits = 0;
    if (!outBuf || outBufSize <= 0) return 0;
    outBuf[0] = '\0';
    for (gX_I32 i = s_guiEntryCount - 1; i >= 0; i--) {
        const gX_UI_LayoutEntry *e = &s_guiEntries[i];
        if (x < e->x || x > e->x + e->w || y < e->y || y > e->y + e->h) continue;
        gX_I32 n = snprintf(outBuf + strlen(outBuf), (size_t)(outBufSize - (gX_I32)strlen(outBuf) - 1),
            "%s:%s rect=(%.0f,%.0f,%.0f,%.0f) text=%d panel=%d\n",
            e->canvas[0] ? e->canvas : "?", e->label,
            e->x, e->y, e->w, e->h, (gX_I32)e->isText, (gX_I32)e->isPanel);
        if (n < 0) break;
        hits++;
    }
    return hits;
}

//opencode: merged view over both tables — Validate/Overlay treat the two
//   tables as one continuous entry list
static gX_I32 gX_UI_Layout_TotalCount(gX_I0) { return s_entryCount + s_guiEntryCount; }

static gX_UI_LayoutEntry *gX_UI_Layout_EntryAt(gX_I32 i) {
    if (i < s_entryCount) return &s_entries[i];
    return &s_guiEntries[i - s_entryCount];
}

gX_I0 gX_UI_Layout_TrackRect(const gX_I8 *label, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    if (s_entryCount >= GX_UI_LAYOUT_MAX_ENTRIES) return;
    gX_UI_LayoutEntry *e = &s_entries[s_entryCount++];
    gX_UI_Layout_EntryInit(e, label, x, y, w, h);
    e->r = 100; e->g = 200; e->b = 100; e->a = 180;
}

gX_I0 gX_UI_Layout_TrackPanel(const gX_I8 *label, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h) {
    if (s_entryCount >= GX_UI_LAYOUT_MAX_ENTRIES) return;
    gX_UI_LayoutEntry *e = &s_entries[s_entryCount++];
    gX_UI_Layout_EntryInit(e, label, x, y, w, h);
    e->r = 120; e->g = 120; e->b = 255; e->a = 180;
    e->isPanel = 1;
}

gX_I0 gX_UI_Layout_TrackText(const gX_I8 *label, const gX_I8 *text,
    gX_F32 x, gX_F32 y, gX_F32 fontSize)
{
    if (s_entryCount >= GX_UI_LAYOUT_MAX_ENTRIES) return;
    gX_UI_LayoutEntry *e = &s_entries[s_entryCount++];
    gX_Font font = {0};
    gX_F32v2 measure = gX_RawString_Measure(text, font, fontSize, 1.0f);
    gX_UI_Layout_EntryInit(e, label, x - measure.x * 0.5f, y - measure.y * 0.2f,
        measure.x, measure.y);
    e->r = 200; e->g = 200; e->b = 100; e->a = 180;
    e->isText = 1;
}

static gX_I1 rects_overlap(gX_F32 a_x, gX_F32 a_y, gX_F32 a_w, gX_F32 a_h,
                            gX_F32 b_x, gX_F32 b_y, gX_F32 b_w, gX_F32 b_h)
{
    if (a_x + a_w <= b_x || b_x + b_w <= a_x) return 0;
    if (a_y + a_h <= b_y || b_y + b_h <= a_y) return 0;
    gX_F32 ox = (a_x + a_w < b_x + b_w ? a_x + a_w : b_x + b_w) - (a_x > b_x ? a_x : b_x);
    gX_F32 oy = (a_y + a_h < b_y + b_h ? a_y + a_h : b_y + b_h) - (a_y > b_y ? a_y : b_y);
    return (ox > 4.0f && oy > 4.0f);
}

//opencode: is inner rect fully inside outer rect (with margin)?
static gX_I1 rect_contains(gX_F32 ox, gX_F32 oy, gX_F32 ow, gX_F32 oh,
                           gX_F32 ix, gX_F32 iy, gX_F32 iw, gX_F32 ih)
{
    return (ix + 1.0f >= ox && iy + 1.0f >= oy &&
            ix + iw - 1.0f <= ox + ow && iy + ih - 1.0f <= oy + oh);
}

//opencode: for a panel+text pair, a text fully contained inside the panel is
//   the normal case (text sits on its panel) — only text overflow is a bug.
//   A panel fully contained inside another panel is likewise safe (a nested
//   child, e.g. a badge sitting on the wave bar) — only partial overlaps of
//   unrelated panels are real collisions.
static gX_I1 panel_text_safe(const gX_UI_LayoutEntry *a, const gX_UI_LayoutEntry *b) {
    if (a->isPanel != b->isPanel) {
        const gX_UI_LayoutEntry *panel = a->isPanel ? a : b;
        const gX_UI_LayoutEntry *text  = a->isPanel ? b : a;
        return rect_contains(panel->x, panel->y, panel->w, panel->h,
                             text->x, text->y, text->w, text->h);
    }
    if (a->isPanel && b->isPanel) {
        return rect_contains(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h) ||
               rect_contains(b->x, b->y, b->w, b->h, a->x, a->y, a->w, a->h);
    }
    return 0;
}

gX_I0 gX_UI_Layout_Validate(gX_F32 screenW, gX_F32 screenH) {
    gX_I32 total = gX_UI_Layout_TotalCount();
    gX_I32 offscreenCount = 0, overlapCount = 0;
    for (gX_I32 i = 0; i < total; i++) {
        gX_UI_LayoutEntry *a = gX_UI_Layout_EntryAt(i);
        a->offscreen = (a->x + a->w > screenW + 1.0f || a->y + a->h > screenH + 1.0f) ? 1 : 0;
        a->overlapping = 0;
        for (gX_I32 j = i + 1; j < total; j++) {
            gX_UI_LayoutEntry *b = gX_UI_Layout_EntryAt(j);
            if (panel_text_safe(a, b)) continue;
            if (rects_overlap(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h)) {
                a->overlapping = 1; b->overlapping = 1; overlapCount++;
            }
        }
        if (a->offscreen) offscreenCount++;
    }
    static gX_I32 s_t = 0;
    s_t = (s_t + 1) % 60;
    if (s_t == 0) {
        gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "UI Layout: %d offscreen, %d overlapping pairs (of %d elements)", offscreenCount, overlapCount, total);
        if (offscreenCount > 0 || overlapCount > 0) {
            for (gX_I32 i = 0; i < total; i++) {
                gX_UI_LayoutEntry *e = gX_UI_Layout_EntryAt(i);
                if (e->offscreen) gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "  OFFSCREEN [%s] x=%.0f y=%.0f w=%.0f h=%.0f (scr=%.0fx%.0f)", e->label, e->x, e->y, e->w, e->h, screenW, screenH);
            }
            for (gX_I32 i = 0; i < total; i++) {
                gX_UI_LayoutEntry *a = gX_UI_Layout_EntryAt(i);
                for (gX_I32 j = i + 1; j < total; j++) {
                    gX_UI_LayoutEntry *b = gX_UI_Layout_EntryAt(j);
                    if (panel_text_safe(a, b)) continue;
                    if (rects_overlap(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h))
                        gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "  PAIR [%s] x=%.0f y=%.0f w=%.0f h=%.0f  <->  [%s] x=%.0f y=%.0f w=%.0f h=%.0f",
                            a->label, a->x, a->y, a->w, a->h,
                            b->label, b->x, b->y, b->w, b->h);
                }
            }
        }
    }
}

gX_I0 gX_UI_Layout_DrawOverlay(gX_F32 screenW, gX_F32 screenH) {
    if (!gX_uiDebugEnabled) return;
    gX_I32 total = gX_UI_Layout_TotalCount();
    if (total == 0) return;
    for (gX_I32 i = 0; i < total; i++) {
        gX_UI_LayoutEntry *e = gX_UI_Layout_EntryAt(i);
        gX_Color bc;
        if (e->offscreen || e->overlapping) bc = (gX_Color){255,60,60,160};
        else if (e->x + e->w > screenW - 10 || e->y + e->h > screenH - 10) bc = (gX_Color){255,220,60,140};
        else bc = (gX_Color){60,220,60,120};
        gX_Rectangle_Draw((gX_Rectangle){e->x, e->y, e->w, 1}, bc);
        gX_Rectangle_Draw((gX_Rectangle){e->x, e->y + e->h, e->w, 1}, bc);
        gX_Rectangle_Draw((gX_Rectangle){e->x, e->y, 1, e->h}, bc);
        gX_Rectangle_Draw((gX_Rectangle){e->x + e->w, e->y, 1, e->h + 1}, bc);
        gX_I8 info[96];
        snprintf(info, sizeof(info), "%s (%.0f,%.0f %.0fx%.0f)", e->label, e->x, e->y, e->w, e->h);
        gX_F32 ix = e->x, iy = e->y - 14;
        if (iy < 2) iy = e->y + e->h + 2;
        if (ix + 200 > screenW) ix = screenW - 210;
        if (ix < 2) ix = 2;
        gX_RawString_Draw_Lesser(info, (gX_F32v2){ix, iy}, 11, bc);
    }
}

// ── Pixel ruler (M key to toggle) ──
static gX_I1  s_rulerActive = 0;
static gX_I32 s_rulerPlaced = 0;
static gX_F32 s_rulerAX = 0, s_rulerAY = 0;
static gX_F32 s_rulerBX = 0, s_rulerBY = 0;

gX_I0 gX_UI_Layout_DrawRuler(gX_F32 screenW, gX_F32 screenH) {
    if (!gX_uiDebugEnabled) return;

    // Toggle on M key
    if (gX_Key_IsPressed(77)) { // KEY_M
        s_rulerActive = !s_rulerActive;
        s_rulerPlaced = 0;
    }
    if (!s_rulerActive) return;

    gX_F32v2 mp = gX_Mouse_GetPos();
    gX_Color rulerCol = (gX_Color){255,255,100,220};

    // Place markers on left click
    if (gX_Mouse_IsPressed(0)) {
        if (!s_rulerPlaced) {
            s_rulerAX = mp.x; s_rulerAY = mp.y;
            s_rulerPlaced = 1;
        } else {
            s_rulerBX = mp.x; s_rulerBY = mp.y;
            s_rulerPlaced = 2;
        }
    }

    // Draw crosshair at mouse
    gX_Rectangle_Draw((gX_Rectangle){mp.x - 8, mp.y, 16, 1}, rulerCol);
    gX_Rectangle_Draw((gX_Rectangle){mp.x, mp.y - 8, 1, 16}, rulerCol);

    // Draw markers
    if (s_rulerPlaced >= 1) {
        gX_Rectangle_Draw((gX_Rectangle){s_rulerAX - 4, s_rulerAY - 4, 8, 8}, (gX_Color){100,255,100,200});
        gX_RawString_Draw_Lesser("A", (gX_F32v2){s_rulerAX + 6, s_rulerAY - 8}, 12, (gX_Color){100,255,100,255});
    }
    if (s_rulerPlaced >= 2) {
        gX_Rectangle_Draw((gX_Rectangle){s_rulerBX - 4, s_rulerBY - 4, 8, 8}, (gX_Color){100,100,255,200});
        gX_RawString_Draw_Lesser("B", (gX_F32v2){s_rulerBX + 6, s_rulerBY - 8}, 12, (gX_Color){100,100,255,255});

        // Draw line between A and B
        gX_F32 dx = s_rulerBX - s_rulerAX, dy = s_rulerBY - s_rulerAY;
        gX_F32 dist = sqrtf(dx * dx + dy * dy);
        gX_I8 distTxt[64];
        snprintf(distTxt, sizeof(distTxt), "%.0fpx (%.0f,%.0f)", dist, dx, dy);

        // Draw a dashed approximation: thin rects along the line
        gX_F32 steps = dist / 8.0f;
        if (steps > 1.0f) {
            for (gX_I32 s = 0; s < (gX_I32)steps; s += 2) {
                gX_F32 t = (gX_F32)s / steps;
                gX_F32 px = s_rulerAX + dx * t;
                gX_F32 py = s_rulerAY + dy * t;
                gX_Rectangle_Draw((gX_Rectangle){px, py, 3, 3}, rulerCol);
            }
        }

        gX_F32 labelX = (s_rulerAX + s_rulerBX) * 0.5f;
        gX_F32 labelY = (s_rulerAY + s_rulerBY) * 0.5f - 14;
        if (labelY < 2) labelY = (s_rulerAY + s_rulerBY) * 0.5f + 4;
        gX_RawString_Draw_Lesser(distTxt, (gX_F32v2){labelX, labelY}, 13, rulerCol);
    }

    // Instruction text (top-right)
    gX_RawString_Draw_Lesser(s_rulerPlaced ? "Ruler: click B" : "Ruler: click A",
        (gX_F32v2){screenW - 130, 20}, 12, rulerCol);
    gX_RawString_Draw_Lesser("M: toggle ruler",
        (gX_F32v2){screenW - 130, 34}, 11, (gX_Color){180,180,180,200});
}

// ── Battle state explorer (R key to toggle) ──
static gX_I1 s_stateExplorerActive = 0;

gX_I0 gX_UI_Layout_DrawStatePanel(gX_F32 screenW, gX_F32 screenH,
    gX_I32 phase, gX_I32 menuState, gX_I32 cursorX, gX_I32 subIndex,
    gX_I32 subScroll, gX_I32 activeActorIndex, gX_I32 activeActorType,
    gX_I1 waitingForInput, gX_I32 actionType, gX_U32 actionTarget,
    gX_I32 currentWave, gX_I32 enemyCount, gX_I32 playerCount)
{
    if (!gX_uiDebugEnabled) return;

    // Toggle on R key
    if (gX_Key_IsPressed(82)) s_stateExplorerActive = !s_stateExplorerActive;
    if (!s_stateExplorerActive) return;

    // Semi-transparent panel top-left
    gX_F32 px = 10, py = 10, pw = 260, ph = 280;
    gX_Rectangle_Draw((gX_Rectangle){px - 4, py - 4, pw + 8, ph + 8}, (gX_Color){0,0,0,200});
    gX_RawString_Draw_Lesser("Battle State", (gX_F32v2){px + 4, py + 2}, 14, (gX_Color){255,220,100,255});

    py += 20;
    #define STATE_LINE(fmt, ...) do { \
        gX_I8 _b[64]; snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); \
        gX_RawString_Draw_Lesser(_b, (gX_F32v2){px + 4, py}, 12, (gX_Color){200,200,200,255}); \
        py += 16; \
    } while(0)

    STATE_LINE("Phase: %d", phase);
    STATE_LINE("menuState: %d", menuState);
    STATE_LINE("cursorX: %d", cursorX);
    STATE_LINE("subIndex: %d", subIndex);
    STATE_LINE("subScroll: %d", subScroll);
    STATE_LINE("activeActor: type=%d idx=%d", activeActorType, activeActorIndex);
    STATE_LINE("waitingForInput: %d", (int)waitingForInput);
    STATE_LINE("actionType: %d  target: %u", actionType, actionTarget);
    STATE_LINE("Wave: %d/%d  enemies: %d", currentWave + 1, 3, enemyCount);
    STATE_LINE("Players: %d", playerCount);
    STATE_LINE("---");
    STATE_LINE("R: toggle  M: ruler  [:prev  ]:next");
    STATE_LINE("  -/=:R  </>:G  ,/.:B  Z/X:A");

    #undef STATE_LINE

    // ── Live style editor (always active when panel is shown) ──
    #define STYLE_FIELD_COUNT 18
    static gX_I32 s_styleField = 0;
    static const gX_I8 *s_styleNames[STYLE_FIELD_COUNT] = {
        "panelBg","nameBg","hpBg","mpBg","enBg",
        "enemyPanelBg","enemyNameBg","enemyHpBg",
        "menuBg","menuItemBg","magicBg","magicItemBg",
        "skillsBg","skillsItemBg","itemsBg","itemsItemBg",
        "targetBg","overlayBg"
    };
    // Field accessors (color pointers)
    gX_Color *s_stylePtrs[STYLE_FIELD_COUNT] = {
        &gui.style.battle.panelBg, &gui.style.battle.nameBg,
        &gui.style.battle.hpBg, &gui.style.battle.mpBg, &gui.style.battle.enBg,
        &gui.style.battle.enemyPanelBg, &gui.style.battle.enemyNameBg,
        &gui.style.battle.enemyHpBg,
        &gui.style.battle.menuBg, &gui.style.battle.menuItemBg,
        &gui.style.battle.magicBg, &gui.style.battle.magicItemBg,
        &gui.style.battle.skillsBg, &gui.style.battle.skillsItemBg,
        &gui.style.battle.itemsBg, &gui.style.battle.itemsItemBg,
        &gui.style.battle.targetBg, &gui.style.battle.overlayBg
    };

    // Cycle field with [ / ]
    if (gX_Key_IsPressed(91)) { s_styleField--; if (s_styleField < 0) s_styleField = STYLE_FIELD_COUNT - 1; }
    if (gX_Key_IsPressed(93)) { s_styleField++; if (s_styleField >= STYLE_FIELD_COUNT) s_styleField = 0; }

    // Adjust R with -/=, G with </>, B with ,/., A with [/] (when panel is focused)
    gX_Color *cf = s_stylePtrs[s_styleField];
    if (gX_Key_IsPressed(45) || gX_Key_IsPressed(61)) { // - / =
        gX_I32 delta = gX_Key_IsPressed(61) ? 8 : -8;
        gX_I32 newR = (gX_I32)cf->r + delta;
        cf->r = (gX_U8)(newR < 0 ? 0 : newR > 255 ? 255 : newR);
    }
    if (gX_Key_IsPressed(44) || gX_Key_IsPressed(46)) { // , / .
        gX_I32 delta = gX_Key_IsPressed(46) ? 8 : -8;
        gX_I32 newB = (gX_I32)cf->b + delta;
        cf->b = (gX_U8)(newB < 0 ? 0 : newB > 255 ? 255 : newB);
    }
    // Adjust G with < / >
    if (gX_Key_IsPressed(60) || gX_Key_IsPressed(62)) { // < / >
        gX_I32 delta = gX_Key_IsPressed(62) ? 8 : -8;
        gX_I32 newG = (gX_I32)cf->g + delta;
        cf->g = (gX_U8)(newG < 0 ? 0 : newG > 255 ? 255 : newG);
    }
    // Adjust A with [ / ] (only when panel showed, these keys are already used for cycling)
    // Use Z / X for alpha
    if (gX_Key_IsPressed(122) || gX_Key_IsPressed(120)) { // Z / X
        gX_I32 delta = gX_Key_IsPressed(120) ? 8 : -8;
        gX_I32 newA = (gX_I32)cf->a + delta;
        cf->a = (gX_U8)(newA < 0 ? 0 : newA > 255 ? 255 : newA);
    }

    // Draw style editor panel at bottom of state panel
    gX_F32 spx = px + 4;
    gX_F32 spy = py + 16;
    gX_I8 styleBuf[96];
    snprintf(styleBuf, sizeof(styleBuf), "Edit: [%d/%d] %s",
        s_styleField + 1, STYLE_FIELD_COUNT, s_styleNames[s_styleField]);
    gX_RawString_Draw_Lesser(styleBuf, (gX_F32v2){spx, spy}, 12, (gX_Color){255,220,100,255});
    spy += 16;
    snprintf(styleBuf, sizeof(styleBuf), "  RGB(%3d,%3d,%3d) A=%3d",
        cf->r, cf->g, cf->b, cf->a);
    gX_RawString_Draw_Lesser(styleBuf, (gX_F32v2){spx, spy}, 12, (gX_Color){200,200,200,255});
    spy += 16;
    // Color swatch
    gX_Rectangle_Draw((gX_Rectangle){spx, spy, 60, 16}, *cf);
    gX_RawString_Draw_Lesser("-/=:R  ,/.:B  </>:G  Z/X:A",
        (gX_F32v2){spx + 64, spy + 2}, 10, (gX_Color){160,160,160,255});

    // Extend panel height if needed
    gX_F32 newH = (spy + 24) - (py - 20);
    if (newH > ph) {
        gX_Rectangle_Draw((gX_Rectangle){px - 4, py - 24 + ph, pw + 8, newH - ph + 4}, (gX_Color){0,0,0,200});
    }

    #undef STYLE_FIELD_COUNT
}

//opencode: structured JSON layout dump — element name→rect per canvas with
//   the screen transform already applied (matches what the composite draws).
//   Data source for scripts/layout_report.py + layout_sweep.py.
gX_I32 gX_UI_Layout_Dump(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    gX_GUI_Canvas *canvases = gui.canvases.data;
    fprintf(f, "{\"screenW\":%d,\"screenH\":%d,\"canvases\":[\n",
        (gX_I32)maindata.screenWidth, (gX_I32)maindata.screenHeight);
    gX_I1 firstCanvas = 1;
    for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
        if (!gui.canvases.meta[ci].DRAW) continue;
        gX_GUI_Canvas *canvas = &canvases[ci];
        if (!canvas || !canvas->elements.data) continue;
        gX_GUI_Element *elements = canvas->elements.data;
        gX_F32 scaleX = 1.0f, scaleY = 1.0f, offX = canvas->canvasBox.x + canvas->canvasOffset.x,
                offY = canvas->canvasBox.y + canvas->canvasOffset.y;
        gX_I1 useBuf = canvas->buffer.loadingState.loaded;
        if (useBuf && canvas->buffer.width > 0 && canvas->buffer.height > 0) {
            scaleX = canvas->canvasBox.width / (gX_F32)canvas->buffer.width;
            scaleY = canvas->canvasBox.height / (gX_F32)canvas->buffer.height;
        }
        fprintf(f, "%s{\"index\":%d,\"name\":\"%s\",\"elements\":[\n",
            firstCanvas ? "" : ",\n", (gX_I32)ci, canvas->name);
        firstCanvas = 0;
        gX_I1 firstEl = 1;
        for (gX_U32 ei = 0; ei < canvas->elements.amount; ei++) {
            gX_GUI_Element *e = &elements[ei];
            if (!canvas->elements.meta[ei].DRAW || !e->visible) continue;
            gX_F32 x = offX + e->rect.x * scaleX;
            gX_F32 y = offY + e->rect.y * scaleY;
            gX_F32 w = e->rect.width * scaleX;
            gX_F32 h = e->rect.height * scaleY;
            const gX_I8 *elName = (e->name[0] && strcmp(e->name, "unnamed") != 0)
                ? e->name : "unnamed";
            gX_I8 nameBuf[80];
            if (elName[0] == 'u') {
                const gX_I8 *typeStr = gX_GUI_Element_Type_String((gX_I32)e->type);
                //opencode: untyped elements have no readable Type_String —
                //   use a stable positional label instead of "UNKNOWN_n"
                if (!strcmp(typeStr, "UNKNOWN")) {
                    snprintf(nameBuf, sizeof(nameBuf), "elem_%d", (gX_I32)ei);
                } else {
                    snprintf(nameBuf, sizeof(nameBuf), "%s_%d", typeStr, (gX_I32)ei);
                }
                elName = nameBuf;
            }
            fprintf(f, "%s{\"name\":\"%s\",\"type\":\"%s\",\"text\":%d,\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}",
                firstEl ? "" : ",\n", elName,
                gX_GUI_Element_Type_String((gX_I32)e->type),
                e->texts.amount > 0 ? 1 : 0,
                x, y, w, h);
            firstEl = 0;
        }
        fprintf(f, "]}");
    }
    fprintf(f, "]}\n");
    fclose(f);
    return 0;
}
