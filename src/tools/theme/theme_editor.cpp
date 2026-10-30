/**********************************************************************************************
*
*   [gX: src/tools/theme/theme_editor.cpp]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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

//opencode: v0.4.0 — gX Tools suite, second tool: theme editor. Immediate-
//   mode ImGui panels over the live title (or any) scene, editing the
//   design-token structs (gui.style) directly. Write-through mutations call
//   gX_GUI_Style_MarkRefresh — the same rebuild path the theme hot-reload
//   uses — so the title re-skins in the same frame, no file round-trip.
//   SAVE writes the live token values back through the style JSON schema
//   (gX_JSON_Save reads dataPtr contents), keeping theme files the single
//   source of truth. The chrome runs on ImGui, so the editor never depends
//   on the gX GUI system being complete (the gui-creates-gui loop broken
//   by the suite's GUI editor applies to theming too).
extern "C" {
#include "../../gui/style.h"
#include "../../common/types.h"
#include "../../common/marshal/json.h"
#include "../../main.h"
#include "../../common/log.h"
}

#include "../imgui/imgui.h"
#include "../tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static gX_I8  s_themePath[256] = { 0 };
static gX_I8  s_pendingSave[256] = { 0 };
static gX_I8  s_pendingEditKey[64] = { 0 };
static gX_U8  s_pendingEditColor[4] = { 0 };
static gX_I1  s_pendingEditSet = 0;
static gX_I1  s_edited = 0;

//opencode: v0.4.0 — reset-to-default baseline. Captured from the token
//   structs at Open (before any headless edit) and after Reload-from-file;
//   per-token/group/all reset restores from it + MarkRefresh.
#define GX_TOOLS_THEME_MAXTOKENS 256
static gX_U8   s_baseline[GX_TOOLS_THEME_MAXTOKENS][4];
static gX_U32  s_baseSize[GX_TOOLS_THEME_MAXTOKENS];
static gX_I32  s_baselineCount = 0;
static gX_I8   s_search[64] = { 0 };

static gX_U32 theme_token_size(gX_U32 dt) {
    switch (dt) {
        case gX_DATATYPE_COLOR: return sizeof(gX_Color);
        case gX_DATATYPE_F32:   return 4;
        case gX_DATATYPE_I32:   return 4;
        case gX_DATATYPE_I16:   return 2;
        case gX_DATATYPE_I8:    return 1;
        case gX_DATATYPE_I1:    return 1;
        default:                return 0;
    }
}

static gX_I0 theme_capture_baseline(gX_I0) {
    s_baselineCount = gX_GUI_Style_TokenCount();
    if (s_baselineCount > GX_TOOLS_THEME_MAXTOKENS) s_baselineCount = GX_TOOLS_THEME_MAXTOKENS;
    for (gX_I32 i = 0; i < s_baselineCount; i++) {
        gX_U32 sz = theme_token_size(gX_GUI_Style_TokenDatatype(i));
        s_baseSize[i] = sz;
        if (sz > 0 && gX_GUI_Style_TokenPtr(i)) {
            memcpy(s_baseline[i], gX_GUI_Style_TokenPtr(i), sz);
        }
    }
}

//opencode: v0.6.6 — value undo (Ctrl+Z / Ctrl+Y). Full-token snapshots
//   (same capture as baseline); pushed on edit-gesture start, popped on
//   hotkey. Restores set edited-by-compare-against-baseline + refresh.
//opencode: heap snapshots (1KB each, malloc on push) — static rings
//   cost 64KB BSS and trip the lean gate; heap only spends while undoable.
#define GX_TOOLS_THEME_UNDO_MAX 32
#define GX_TOOLS_THEME_SNAPSIZE (GX_TOOLS_THEME_MAXTOKENS * 4)
static gX_U8 *s_undoStack[GX_TOOLS_THEME_UNDO_MAX] = { 0 };
static gX_I32 s_undoHead = 0, s_undoCount = 0;
static gX_U8 *s_redoStack[GX_TOOLS_THEME_UNDO_MAX] = { 0 };
static gX_I32 s_redoHead = 0, s_redoCount = 0;
static gX_I1 s_gestureOn = 0;

static gX_I0 theme_snap_capture(gX_U8 *dst) {
    gX_I32 n = gX_GUI_Style_TokenCount();
    if (n > GX_TOOLS_THEME_MAXTOKENS) n = GX_TOOLS_THEME_MAXTOKENS;
    for (gX_I32 i = 0; i < n; i++) {
        gX_U32 sz = theme_token_size(gX_GUI_Style_TokenDatatype(i));
        if (sz > 0 && sz <= 4 && gX_GUI_Style_TokenPtr(i))
            memcpy(dst + (gX_U32)i * 4, gX_GUI_Style_TokenPtr(i), sz);
    }
}

static gX_I0 theme_snap_restore(gX_U8 *src) {
    gX_I32 n = gX_GUI_Style_TokenCount();
    if (n > GX_TOOLS_THEME_MAXTOKENS) n = GX_TOOLS_THEME_MAXTOKENS;
    for (gX_I32 i = 0; i < n; i++) {
        gX_U32 sz = theme_token_size(gX_GUI_Style_TokenDatatype(i));
        if (sz > 0 && sz <= 4 && gX_GUI_Style_TokenPtr(i))
            memcpy(gX_GUI_Style_TokenPtr(i), src + (gX_U32)i * 4, sz);
    }
    //opencode: dirty iff differs from file baseline
    s_edited = 0;
    for (gX_I32 i = 0; i < n && i < s_baselineCount; i++) {
        gX_U32 sz = theme_token_size(gX_GUI_Style_TokenDatatype(i));
        if (sz > 0 && memcmp(gX_GUI_Style_TokenPtr(i), s_baseline[i], sz) != 0) { s_edited = 1; break; }
    }
    gX_GUI_Style_MarkRefresh();
}

static gX_U8 *theme_snap_alloc(gX_U8 **slot) {
    if (!*slot) *slot = (gX_U8 *)malloc(GX_TOOLS_THEME_SNAPSIZE);
    return *slot;
}

static gX_I0 theme_undo_push(gX_I0) {
    gX_U8 *dst = theme_snap_alloc(&s_undoStack[s_undoHead]);
    if (dst) theme_snap_capture(dst);
    s_undoHead = (s_undoHead + 1) % GX_TOOLS_THEME_UNDO_MAX;
    if (s_undoCount < GX_TOOLS_THEME_UNDO_MAX) s_undoCount++;
    for (gX_I32 i = 0; i < GX_TOOLS_THEME_UNDO_MAX; i++) {
        if (s_redoStack[i]) { free(s_redoStack[i]); s_redoStack[i] = 0; }
    }
    s_redoCount = 0;
    s_redoHead = 0;
}

static gX_I0 theme_undo_pop(gX_I0) {
    if (s_undoCount <= 0) return;
    gX_U8 *rd = theme_snap_alloc(&s_redoStack[s_redoHead]);
    if (rd) theme_snap_capture(rd);
    s_redoHead = (s_redoHead + 1) % GX_TOOLS_THEME_UNDO_MAX;
    if (s_redoCount < GX_TOOLS_THEME_UNDO_MAX) s_redoCount++;
    s_undoHead = (s_undoHead - 1 + GX_TOOLS_THEME_UNDO_MAX) % GX_TOOLS_THEME_UNDO_MAX;
    s_undoCount--;
    if (s_undoStack[s_undoHead]) theme_snap_restore(s_undoStack[s_undoHead]);
}

static gX_I0 theme_redo_pop(gX_I0) {
    if (s_redoCount <= 0) return;
    gX_U8 *ud = theme_snap_alloc(&s_undoStack[s_undoHead]);
    if (ud) theme_snap_capture(ud);
    s_undoHead = (s_undoHead + 1) % GX_TOOLS_THEME_UNDO_MAX;
    if (s_undoCount < GX_TOOLS_THEME_UNDO_MAX) s_undoCount++;
    s_redoHead = (s_redoHead - 1 + GX_TOOLS_THEME_UNDO_MAX) % GX_TOOLS_THEME_UNDO_MAX;
    s_redoCount--;
    if (s_redoStack[s_redoHead]) theme_snap_restore(s_redoStack[s_redoHead]);
}

static gX_I0 theme_reset_token(gX_I32 i) {
    if (i < 0 || i >= s_baselineCount || s_baseSize[i] == 0) return;
    memcpy(gX_GUI_Style_TokenPtr(i), s_baseline[i], s_baseSize[i]);
    s_edited = 1;
}

static gX_I1 theme_token_matches_filter(gX_I32 i) {
    if (!s_search[0]) return 1;
    const gX_I8 *key = gX_GUI_Style_TokenKey(i);
    const gX_I8 *group = gX_GUI_Style_TokenGroup(i);
    if (!key) return 0;
    for (gX_U32 si = 0; si < strlen(s_search); si++)
        s_search[si] = (gX_I8)tolower((gX_U8)s_search[si]);
    gX_I8 hay[160];
    snprintf(hay, sizeof(hay), "%s.%s", group ? group : "", key);
    for (gX_I8 *p = hay; *p; p++) *p = (gX_I8)tolower((gX_U8)*p);
    return strstr(hay, s_search) != NULL;
}

static gX_I0 theme_apply_pending_edit(gX_I0) {
    if (!s_pendingEditSet) return;
    gX_I32 count = gX_GUI_Style_TokenCount();
    for (gX_I32 i = 0; i < count; i++) {
        const gX_I8 *key = gX_GUI_Style_TokenKey(i);
        const gX_I8 *group = gX_GUI_Style_TokenGroup(i);
        if (!key) continue;
        //opencode: accept both the bare token key ("bgGradientTop") and the
        //   dotted group-qualified form ("title2.bgGradientTop")
        gX_I1 matches = strcmp(key, s_pendingEditKey) == 0;
        if (!matches && group && s_pendingEditKey[0]) {
            gX_I8 full[96];
            snprintf(full, sizeof(full), "%s.%s", group, key);
            matches = strcmp(full, s_pendingEditKey) == 0;
        }
        if (matches) {
            gX_I0 *ptr = gX_GUI_Style_TokenPtr(i);
            gX_U32 dt = gX_GUI_Style_TokenDatatype(i);
            if (ptr && dt == gX_DATATYPE_COLOR) {
                gX_Color *c = (gX_Color *)ptr;
                c->r = s_pendingEditColor[0];
                c->g = s_pendingEditColor[1];
                c->b = s_pendingEditColor[2];
                c->a = s_pendingEditColor[3];
                gX_TraceLog(gX_LOG_INFO, "Tools: theme edit '%s' -> {%u,%u,%u,%u}",
                    key, c->r, c->g, c->b, c->a);
                s_pendingEditSet = 0;
                s_edited = 1;
                gX_GUI_Style_MarkRefresh();
            }
            return;
        }
    }
    gX_TraceLog(gX_LOG_WARNING, "Tools: GX_THEME_EDIT token '%s' not found",
        s_pendingEditKey);
    s_pendingEditSet = 0;
}

static gX_I0 theme_save(const gX_I8 *path) {
    if (!path || !path[0]) return;
    if (gX_GUI_Style_Save(path) == 0) {
        s_edited = 0;
        gX_TraceLog(gX_LOG_INFO, "Tools: theme saved '%s'", path);
    }
}

//=============================================================================
// token panels — write-through on change
//=============================================================================

//opencode: v0.6.6 — plain-language group guide (hover the header).
static const gX_I8 *theme_group_desc(const gX_I8 *group) {
    if (!strcmp(group, "title2")) return "Title screen: hero band, wordmark, menu buttons, save slots.";
    if (!strcmp(group, "blueprint")) return "Line-art preview (Theme Lab): ink/accent ink, lattice grid.";
    if (!strcmp(group, "core")) return "Global: fonts, radius, borders, shadows, widget look.";
    if (!strcmp(group, "shared")) return "Accents used across screens: hps, bars, highlights.";
    if (!strcmp(group, "escape")) return "Pause menu: panel, buttons, entries.";
    if (!strcmp(group, "battle")) return "Battle HUD: bars, log, menus.";
    if (!strcmp(group, "a11y")) return "Surfaces (canvas/raised/overlay/sunken/inverse + text pairs) and motion durations.";
    return "";
}

//opencode: v0.6.7 — per-token plain descriptions (most-tweaked tokens;
//   fallback is the group guide). Keyed "group.key".
static const gX_I8 *theme_token_desc(const gX_I8 *group, const gX_I8 *key) {
    static gX_I8 id[160];
    snprintf(id, sizeof(id), "%s.%s", group, key);
    struct { const gX_I8 *id; const gX_I8 *d; } table[] = {
        {"title2.heroBand", "Title hero stripe behind the wordmark."},
        {"title2.wordmarkText", "Big gX title text color."},
        {"title2.menuBg1", "Menu button 1 (CONTINUE) fill."},
        {"title2.menuBg2", "Menu button 2 fill."},
        {"title2.menuBg3", "Menu button 3 fill."},
        {"title2.menuBg4", "Menu button 4 fill."},
        {"title2.menuBg5", "DEMOS button fill."},
        {"title2.menuBg6", "EXTRAS button fill."},
        {"title2.menuText1", "Menu button 1 label."},
        {"title2.menuText2", "Menu button 2 label."},
        {"title2.menuText3", "Menu button 3 label."},
        {"title2.quitBg", "QUIT button fill."},
        {"title2.savePanelBg", "Save/extras/credits panel fill."},
        {"title2.saveSlotBg", "Save slot + extras entry fill."},
        {"title2.backBg", "BACK button fill."},
        {"blueprint.inkColor", "Line-art ink (borders, hero outline)."},
        {"blueprint.accentColor", "Line-art accent (hero fill, highlights)."},
        {"blueprint.latticeColor", "Lattice grid line color."},
        {"blueprint.latticeAlpha", "Lattice opacity (0 hides)."},
        {"blueprint.latticeCell", "Lattice cell size, px."},
        {"core.cornerRadius", "Widget corner rounding, px."},
        {"core.borderWidth", "Widget border thickness, px."},
        {"core.shadowOpacity", "Drop shadow strength."},
        {"core.gradientStrength", "Button gradient depth."},
        {"core.buttonBase", "Default button fill."},
        {"core.playBase", "PLAY accent button fill."},
        {"core.backBase", "BACK button fill."},
        {"core.extraBase", "EXTRA button fill."},
        {"core.quitBase", "QUIT button fill."},
        {"core.widgetSDF", "SDF widget surfaces (rounded/AA). Off = flat rects."},
        {"shared.hpGreen", "Healthy bar green."},
        {"shared.textInputBg", "Text field fill."},
        {"a11y.surfaceCanvas", "Base canvas surface."},
        {"a11y.surfaceRaised", "Raised cards/buttons surface."},
        {"a11y.surfaceOverlay", "Overlays/popups surface."},
        {"a11y.surfaceSunken", "Sunken wells/inputs surface."},
        {"a11y.surfaceInverse", "Inverted (dark-on-light) surface."},
        {"a11y.motionShort", "Hover pulse ms."},
        {"a11y.motionBase", "Standard transition ms."},
        {"a11y.motionLong", "Slow transition ms."},
        {"a11y.motionReduced", "Snap instead of animate (reduced motion)."},
        {0, 0},
    };
    for (gX_I32 i = 0; table[i].id; i++) {
        if (!strcmp(id, table[i].id)) return table[i].d;
    }
    return 0;
}

static gX_I0 theme_panel_group(const gX_I8 *group, const gX_I8 *label) {
    //opencode: v0.6.8 — collapsed by default (declutter; ~119 rows hid
    //   the canvas). A non-empty search forces groups open so matches show.
    bool forceOpen = s_search[0] != '\0';
    if (forceOpen) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader(label)) return;
    if (ImGui::IsItemHovered() && theme_group_desc(group)[0])
        ImGui::SetTooltip("%s", theme_group_desc(group));
    gX_I32 count = gX_GUI_Style_TokenCount();
    gX_I32 shown = 0;
    for (gX_I32 i = 0; i < count; i++) {
        const gX_I8 *g = gX_GUI_Style_TokenGroup(i);
        if (!g || strcmp(g, group) != 0) continue;
        if (theme_token_matches_filter(i)) shown++;
    }
    if (s_search[0] && shown == 0) {
        ImGui::TextDisabled("(no matches)");
        return;
    }

    if (ImGui::SmallButton("Reset group")) {
        for (gX_I32 i = 0; i < count; i++) {
            const gX_I8 *g = gX_GUI_Style_TokenGroup(i);
            if (g && strcmp(g, group) == 0 && theme_token_matches_filter(i))
                theme_reset_token(i);
        }
        if (shown > 0) gX_GUI_Style_MarkRefresh();
    }

    for (gX_I32 i = 0; i < count; i++) {
        const gX_I8 *g = gX_GUI_Style_TokenGroup(i);
        if (!g || strcmp(g, group) != 0) continue;
        if (!theme_token_matches_filter(i)) continue;
        const gX_I8 *key = gX_GUI_Style_TokenKey(i);
        gX_I0 *ptr = gX_GUI_Style_TokenPtr(i);
        if (!key || !ptr) continue;
        gX_U32 dt = gX_GUI_Style_TokenDatatype(i);

        gX_I8 labelBuf[96];
        snprintf(labelBuf, sizeof(labelBuf), "%s##%s-%i", key, group, i);
        bool changed = false;
        switch (dt) {
            case gX_DATATYPE_COLOR: {
                gX_Color *c = (gX_Color *)ptr;
                float col[4] = { c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, c->a / 255.0f };
                changed = ImGui::ColorEdit4(labelBuf, col, ImGuiColorEditFlags_AlphaBar);
                if (changed) {
                    c->r = (gX_U8)(col[0] * 255.0f + 0.5f);
                    c->g = (gX_U8)(col[1] * 255.0f + 0.5f);
                    c->b = (gX_U8)(col[2] * 255.0f + 0.5f);
                    c->a = (gX_U8)(col[3] * 255.0f + 0.5f);
                }
                break;
            }
            case gX_DATATYPE_F32:
                changed = ImGui::DragFloat(labelBuf, (float *)ptr, 0.01f, -1024.0f, 1024.0f, "%.3f");
                break;
            case gX_DATATYPE_I32: {
                gX_I32 v = *(gX_I32 *)ptr;
                changed = ImGui::DragInt(labelBuf, &v, 1, -1024, 1024);
                if (changed) *(gX_I32 *)ptr = v;
                break;
            }
            case gX_DATATYPE_I8: {
                gX_I32 v = *(gX_I8 *)ptr;
                changed = ImGui::DragInt(labelBuf, &v, 1, -1024, 1024);
                if (changed) *(gX_I8 *)ptr = (gX_I8)v;
                break;
            }
            case gX_DATATYPE_I16: {
                gX_I32 v = *(gX_I16 *)ptr;
                changed = ImGui::DragInt(labelBuf, &v, 1, -1024, 1024);
                if (changed) *(gX_I16 *)ptr = (gX_I16)v;
                break;
            }
            case gX_DATATYPE_I1: {
                bool b = *((gX_I1 *)ptr) != 0;
                changed = ImGui::Checkbox(labelBuf, &b);
                if (changed) *((gX_I1 *)ptr) = b ? 1 : 0;
                break;
            }
            default:
                ImGui::TextDisabled("%s (type %u)", key, dt);
                break;
        }
        {
            const gX_I8 *td = theme_token_desc(group, key);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", td ? td : theme_group_desc(group));
        }
        if (s_baseSize[i] > 0) {
            ImGui::SameLine();
            char rl[16];
            snprintf(rl, sizeof(rl), "R##%s-%i", group, i);
            if (ImGui::SmallButton(rl)) {
                theme_reset_token(i);
                changed = true;
            }
        }
        if (changed) {
            s_edited = 1;
            gX_GUI_Style_MarkRefresh();
        }
    }
}

//=============================================================================
// entry
//=============================================================================

extern "C" gX_I0 gX_Tools_Theme_Open(const gX_I8 *themePath);
extern "C" gX_I0 gX_Tools_Theme_Open(const gX_I8 *themePath) {
    if (themePath && themePath[0])
        strncpy(s_themePath, themePath, sizeof(s_themePath) - 1);
    gX_TraceLog(gX_LOG_INFO, "Tools: theme editor open — theme '%s'",
        s_themePath[0] ? s_themePath : "(none)");

    //opencode: reset baseline = the FILE state, captured before any
    //   headless edit applies
    theme_capture_baseline();

    //opencode: headless hooks — GX_THEME_EDIT=<key>=<r,g,b,a> applies a token
    //   write-through (capture verification of live re-skin),
    //   GX_THEME_SAVE=<path> writes the theme file once at open
    const char *editEnv = getenv("GX_THEME_EDIT");
    if (editEnv && editEnv[0]) {
        char key[64] = { 0 };
        unsigned r = 0, g = 0, b = 0, a = 0;
        if (sscanf(editEnv, "%63[^=]=%u,%u,%u,%u", key, &r, &g, &b, &a) == 5) {
            strncpy(s_pendingEditKey, key, sizeof(s_pendingEditKey) - 1);
            s_pendingEditColor[0] = (gX_U8)r;
            s_pendingEditColor[1] = (gX_U8)g;
            s_pendingEditColor[2] = (gX_U8)b;
            s_pendingEditColor[3] = (gX_U8)a;
            s_pendingEditSet = 1;
            theme_apply_pending_edit();
        } else {
            gX_TraceLog(gX_LOG_WARNING,
                "Tools: GX_THEME_EDIT expects '<key>=<r,g,b,a>' — got '%s'", editEnv);
        }
    }
    const char *saveEnv = getenv("GX_THEME_SAVE");
    if (saveEnv && saveEnv[0])
        strncpy(s_pendingSave, saveEnv, sizeof(s_pendingSave) - 1);
    if (s_pendingSave[0]) theme_save(s_pendingSave);
}

extern "C" gX_I0 gX_Tools_Theme_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Theme_Draw(gX_I0) {
    //opencode: v0.6.9 Phase B — dock in left margin when docked (slim);
    //   overlay 520px otherwise. Fluid rows fit either width.
    {
        gX_F32 tsw = (gX_F32)maindata.windowWidth;
        gX_F32 tsh = (gX_F32)maindata.windowHeight;
        if (tsw <= 0) { tsw = ImGui::GetIO().DisplaySize.x; tsh = ImGui::GetIO().DisplaySize.y; }
        //opencode: dock hardening — falling-edge snap (see editor).
        static gX_I1 s_prevDocked = 0;
        gX_I1 dockNow = (gui.viewportDocked && (tsw > 0)) ? 1 : 0;
        if (gui.viewportDocked && tsw > 0) {
            gX_F32 mgn = gui.viewportBox.x;
            if (mgn < 160) mgn = 160;
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(mgn, tsh), ImGuiCond_Always);
        } else {
            static int sizeFrames = 0;
            if (s_prevDocked) sizeFrames = 0;
            if (sizeFrames < 5) {
                if (sizeFrames == 0)
                    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(520, tsh > 100 ? tsh - 60 : 1000), ImGuiCond_Always);
                sizeFrames++;
            }
        }
        s_prevDocked = dockNow;
    }
    ImGui::PushItemWidth(-FLT_MIN);
    bool thOpen = (*gX_Tools_RuntimeThemeFlag()) != 0;
    if (!ImGui::Begin("gX Tools - Theme Editor", &thOpen)) {
        ImGui::End();
        ImGui::PopItemWidth();
        if (!thOpen) *gX_Tools_RuntimeThemeFlag() = 0;
        return;
    }
    if (!thOpen) *gX_Tools_RuntimeThemeFlag() = 0;
    ImGui::Text("Theme: %s", s_themePath[0] ? s_themePath : "(none)");
    ImGui::Text("Tokens: %i%s", gX_GUI_Style_TokenCount(),
        s_edited ? " (edited)" : "");
    ImGui::InputTextWithHint("##search", "filter tokens... (R = reset)", s_search, sizeof(s_search));
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset all") && s_baselineCount > 0) {
        for (gX_I32 i = 0; i < s_baselineCount; i++) theme_reset_token(i);
        gX_GUI_Style_MarkRefresh();
    }
    ImGui::Separator();

    //opencode: v0.6.6 — undo gesture + hotkeys (skip while typing so the
    //   search field keeps its own text undo).
    {
        ImGuiIO &tio = ImGui::GetIO();
        if (!tio.WantTextInput) {
            if (tio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                if (tio.KeyShift) theme_redo_pop();
                else theme_undo_pop();
            }
            if (tio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) theme_redo_pop();
        }
        if (ImGui::IsAnyItemActive()) {
            if (!s_gestureOn) { theme_undo_push(); s_gestureOn = 1; }
        } else {
            s_gestureOn = 0;
        }
    }
    ImGui::TextDisabled("Edits apply live. R resets a token. Ctrl+Z undoes.");
    theme_panel_group("title2", "title2 (title screen)");
    theme_panel_group("blueprint", "blueprint (line-art preview)");
    theme_panel_group("core", "core (global)");
    theme_panel_group("shared", "shared (accents)");
    theme_panel_group("escape", "escape (pause menu)");
    theme_panel_group("battle", "battle (HUD)");
    theme_panel_group("a11y", "a11y (surfaces + motion)");

    ImGui::Separator();
    ImGui::TextDisabled("Compare presets (undoable via Ctrl+Z):");
    if (ImGui::SmallButton("Default")) {
        theme_undo_push();
        strncpy(s_themePath, "resources/style/theme.json", sizeof(s_themePath) - 1);
        gX_GUI_Style_SetThemePath(s_themePath);
        gX_GUI_Style_LoadDefault();
        gX_GUI_Style_MarkRefresh();
        s_edited = 0;
        theme_capture_baseline();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Blueprint")) {
        theme_undo_push();
        strncpy(s_themePath, "resources/style/theme_blueprint.json", sizeof(s_themePath) - 1);
        gX_GUI_Style_SetThemePath(s_themePath);
        gX_GUI_Style_LoadDefault();
        gX_GUI_Style_MarkRefresh();
        s_edited = 0;
        theme_capture_baseline();
    }
    ImGui::Separator();
    ImGui::InputText("save path", s_themePath, sizeof(s_themePath));
    bool dirty = s_edited;
    if (ImGui::Button("Save theme")) theme_save(s_themePath);
    ImGui::SameLine();
    ImGui::SameLine();
    if (ImGui::Button("Close")) *gX_Tools_RuntimeThemeFlag() = 0;
    if (ImGui::Button("Reload from file") && s_themePath[0]) {
        gX_GUI_Style_SetThemePath(s_themePath);
        gX_GUI_Style_LoadDefault();
        gX_GUI_Style_MarkRefresh();
        s_edited = 0;
        theme_capture_baseline();
    }
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "unsaved changes");
    }
    ImGui::End();
    ImGui::PopItemWidth();
}