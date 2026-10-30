/**********************************************************************************************
*
*   [gX: src/tools/editor/editor.cpp]
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

//opencode: v0.4.0 — gX Tools suite, first tool: GUI editor. Immediate-mode
//   ImGui panels over a live gX canvas (built from a markup template).
//   The chrome runs on ImGui so the editor does not depend on the gX GUI
//   system being finished — the canvas being edited IS the real gX canvas.
//   Write-through edits mutate the live element structs directly (the same
//   pattern guieditor.c proved: no target-canvas refresh while editing, so
//   bound pointers stay valid). SAVE snapshots the live canvas into the
//   markup schema and writes the file; the markup hot-reload picks it up.
extern "C" {
#include "../../gui/gui.h"
#include "../../gui/canvas.h"
#include "../../gui/markup.h"
#include "../../gui/element/element.h"
#include "../../gui/element/struct.h"
#include "../../main.h"
#include "../../input.h"
#include "../../common/log.h"
#include "../../settings.h"
#include "../../common/utils.h"
}

#include "../imgui/imgui.h"
#include "../tools.h"

extern "C" gX_I0 gX_Tools_ImGui_AccessibilityMenu(gX_I0);

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

static gX_I32 s_targetCanvas = 0;
//opencode: generation-checked editor identity. Slot indices alone are unsafe
//   because canvas heaps reuse removed slots during rebuilds.
typedef struct gX_Tools_Editor_Handle {
    gX_I32 canvasIndex;
    gX_U32 elementIndex;
    gX_U32 generation;
} gX_Tools_Editor_Handle;
static gX_Tools_Editor_Handle s_selection = { -1, 0, 0 };
static gX_I32 s_pendingSelect = -1;

static gX_I0 tools_selection_clear(gX_I0) {
    s_selection.canvasIndex = -1;
    s_selection.elementIndex = 0;
    s_selection.generation = 0;
}

static gX_GUI_Element *tools_selection_resolve(gX_GUI_Canvas *canvas) {
    gX_GUI_Element *element = NULL;
    if (!canvas || s_selection.canvasIndex != s_targetCanvas
        || s_selection.elementIndex >= canvas->elements.amount
        || !canvas->elements.data || !canvas->elements.meta
        || !canvas->elements.meta[s_selection.elementIndex].USED) {
        if (s_selection.canvasIndex >= 0) tools_selection_clear();
        return NULL;
    }
    element = &((gX_GUI_Element *)canvas->elements.data)[s_selection.elementIndex];
    if (element->id != s_selection.elementIndex
        || element->generation != s_selection.generation) {
        tools_selection_clear();
        return NULL;
    }
    return element;
}

static gX_I32 tools_selection_index(gX_GUI_Canvas *canvas) {
    return tools_selection_resolve(canvas)
        ? (gX_I32)s_selection.elementIndex : -1;
}

static gX_I0 tools_selection_set(gX_GUI_Canvas *canvas, gX_I32 index) {
    gX_GUI_Element *element = NULL;
    tools_selection_clear();
    if (!canvas || index < 0 || (gX_U32)index >= canvas->elements.amount
        || !canvas->elements.data || !canvas->elements.meta
        || !canvas->elements.meta[index].USED) return;
    element = &((gX_GUI_Element *)canvas->elements.data)[index];
    if (element->id != (gX_U32)index || element->generation == 0) return;
    s_selection.canvasIndex = s_targetCanvas;
    s_selection.elementIndex = (gX_U32)index;
    s_selection.generation = element->generation;
}

//opencode: toolbar add-type — file scope so the toolbar row and the Actions
//   section share one selection (was a function-static inside Actions).
static gX_I32 s_addTypeIdx = 0;
static gX_I0 tools_solo_restore(gX_GUI_Canvas *canvas);
//opencode: edit lock — the target canvas's elements stay inert while the
//   editor is open (no hover/press/drag/click visuals, sounds, actions).
//   Lock lives on the canvas (survives heap realloc); cleared on retarget
//   (old canvas), editor close, and runtime close.
static gX_I0 tools_edit_lock_set(gX_I32 idx) {
    for (gX_I32 ci = 0; ci < 8; ci++) {
        gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(ci);
        if (!c) continue;
        if (ci == idx) {
            c->editLock = 1;
            for (gX_U32 ei = 0; ei < c->elements.amount; ei++)
                (&((gX_GUI_Element *)c->elements.data)[ei])->state.FOCUSED = false;
        } else if (c->editLock) {
            c->editLock = 0;
        }
    }
}
static gX_I0 tools_edit_lock_clear(gX_I0) {
    for (gX_I32 ci = 0; ci < 8; ci++) {
        gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(ci);
        if (c) c->editLock = 0;
    }
}
extern "C" gX_I0 gX_Tools_Editor_ClearEditLock(gX_I0) {
    tools_edit_lock_clear();
}
extern "C" gX_I0 gX_Tools_Editor_SoloRestore(gX_I0) {
    tools_solo_restore(gX_GUI_Canvas_FromIndex(s_targetCanvas));
}
//opencode: v0.6.3 — studio toggles (View menu). Snap gates tools_grid;
//   overlay gates the selection+handles foreground pass.
//opencode: grid draws the 8px snap lattice (+drag guides); independent of
//   snap so the lattice can stay as a ruler with snapping off.
static gX_I1 s_snapOn = 1;
static gX_I1 s_gridOn = 1;
static gX_I1 s_overlayOn = 1;
//opencode: isolate selection — solo shows only the selected element (answer
//   "what does it draw by itself" under overlapping siblings). Stash holds
//   the siblings' DRAW flags; restored on toggle-off, selection/canvas
//   change (re-stashed), and editor close.
static gX_I1 s_soloOn = 0;
static gX_I1 *s_soloStash = 0;
static gX_U32 s_soloAmount = 0;
static gX_I32 s_soloSel = -2;
static gX_I32 s_soloCanvas = -1;
static gX_I0 tools_solo_restore(gX_GUI_Canvas *canvas) {
    if (!s_soloStash || !canvas) {
        if (s_soloStash) {
            free(s_soloStash);
            s_soloStash = 0;
        }
        s_soloAmount = 0;
        s_soloSel = -2;
        s_soloCanvas = -1;
        return;
    }
    for (gX_U32 ei = 0; ei < canvas->elements.amount && ei < s_soloAmount; ei++)
        canvas->elements.meta[ei].DRAW = s_soloStash[ei] ? true : false;
    free(s_soloStash);
    s_soloStash = 0;
    s_soloAmount = 0;
    s_soloSel = -2;
    s_soloCanvas = -1;
}
static gX_I0 tools_solo_apply(gX_GUI_Canvas *canvas) {
    gX_I32 selectionIndex = tools_selection_index(canvas);
    if (!s_soloOn || !canvas || selectionIndex < 0) {
        tools_solo_restore(canvas);
        return;
    }
    if (s_soloStash
        && ((s_soloSel != selectionIndex) || (s_soloCanvas != s_targetCanvas)
            || (s_soloAmount != canvas->elements.amount))) {
        tools_solo_restore(canvas);
    }
    if (!s_soloStash) {
        s_soloStash = (gX_I1 *)malloc(sizeof(gX_I1) * canvas->elements.amount);
        if (!s_soloStash) return;
        for (gX_U32 ei = 0; ei < canvas->elements.amount; ei++)
            s_soloStash[ei] = canvas->elements.meta[ei].DRAW ? 1 : 0;
        s_soloAmount = canvas->elements.amount;
        s_soloSel = selectionIndex;
        s_soloCanvas = s_targetCanvas;
    }
    for (gX_U32 ei = 0; ei < canvas->elements.amount; ei++)
        canvas->elements.meta[ei].DRAW = ((gX_I32)ei == selectionIndex) ? true : false;
}
//opencode: v0.6.6 — usability: tree filter, help window (first-open),
//   remove-confirm + save-overwrite confirm state.
static gX_I8 s_treeFilter[64] = { 0 };
static gX_I1 s_helpOpen = 0;
//opencode: v0.6.6 — first-run tour (0..4 steps, -1 off). Auto-opens when
//   settings.edTourSeen is false; Skip/Finish/X persists seen+dirty.
static gX_I32 s_tourStep = -1;
static gX_I1 s_confirmRemove = 0;
static gX_I1 s_confirmOverwrite = 0;
static gX_I8  s_markupPath[256] = { 0 };
static gX_I8  s_savePath[256] = { 0 };
static gX_I8  s_pendingSave[256] = { 0 };
static gX_I32 s_pendingAdd = -1;
static gX_I32 s_pendingRemove = -1;
static gX_I32 s_pendingUndo = 0;
static gX_I32 s_pendingRedo = 0;


static gX_I0 tools_save_request(gX_I0);

//=============================================================================
// undo/redo — value-mutation snapshots (move/resize/property edits).
//   Structural ops (add/remove/duplicate) are NOT undoable: the element
//   heap is slot-based (meta[] USED flags), so slot count changes cannot be
//   restored by a data memcpy without meta surgery.
//=============================================================================

//opencode: v0.6.7 — DEEP snapshots (structural undo). Captures heap meta
//   + element bytes + every sub-module heap (13 per element) generically.
//   Restores teardown current sub-heaps (seen-list guards pre-existing
//   duplicate aliasing) then deep-restores. Unifies value + structural undo.
typedef struct gX_Tools_EditorSubSnap {
    gX_U32 amount, blockSize, max;
    gX_U8  *data;
    gX_U8  *meta;
} gX_Tools_EditorSubSnap;

#define GX_TOOLS_EDITOR_SUBHEAPS 13

static gX_Heap *tools_element_sub(gX_GUI_Element *el, gX_I32 k) {
    switch (k) {
        case 0: return &el->animations;
        case 1: return &el->attributes;
        case 2: return &el->colors;
        case 3: return &el->datas;
        case 4: return &el->layouts;
        case 5: return &el->models;
        case 6: return &el->shaders;
        case 7: return &el->shapes;
        case 8: return &el->sounds;
        case 9: return &el->texts;
        case 10: return &el->textures;
        case 11: return &el->timers;
        default: return &el->textinputs;
    }
}

typedef struct gX_Tools_EditorSnap {
    gX_U8   *data;         // elements bytes (amount*blockSize)
    gX_U8   *meta;         // heap meta bytes (amount*sizeof(Meta))
    gX_U32   amount;
    gX_U32   blockSize;
    gX_Tools_EditorSubSnap *subs;  // amount*13 (null data/meta when empty)
} gX_Tools_EditorSnap;

static gX_I0 tools_element_deep_copy(gX_GUI_Element *dst, const gX_GUI_Element *src) {
    gX_U32 generation = dst->generation;
    *dst = *src;
    dst->generation = generation;
    for (gX_I32 k = 0; k < GX_TOOLS_EDITOR_SUBHEAPS; k++) {
        gX_Heap *dh = tools_element_sub(dst, k);
        const gX_Heap *sh = tools_element_sub((gX_GUI_Element *)src, k);
        dh->data = 0;
        dh->meta = 0;
        dh->amount = 0;
        dh->blockSize = sh->blockSize;
        dh->max = sh->amount;
        if (sh->amount > 0 && sh->data && sh->blockSize > 0) {
            dh->data = (gX_U8 *)malloc((size_t)sh->amount * sh->blockSize);
            if (dh->data) {
                memcpy(dh->data, sh->data, (size_t)sh->amount * sh->blockSize);
                dh->amount = sh->amount;
            }
        }
        if (sh->amount > 0 && sh->meta) {
            dh->meta = (gX_Heap_Meta *)malloc((size_t)sh->amount * sizeof(gX_Heap_Meta));
            if (dh->meta) memcpy(dh->meta, sh->meta, (size_t)sh->amount * sizeof(gX_Heap_Meta));
        }
    }
}

static gX_I0 tools_snap_free(gX_Tools_EditorSnap *s) {
    if (s->subs) {
        for (gX_U32 i = 0; i < s->amount * GX_TOOLS_EDITOR_SUBHEAPS; i++) {
            if (s->subs[i].data) free(s->subs[i].data);
            if (s->subs[i].meta) free(s->subs[i].meta);
        }
        free(s->subs);
    }
    free(s->data);
    free(s->meta);
    memset(s, 0, sizeof(*s));
}

static gX_I0 tools_snap_capture(gX_Tools_EditorSnap *s, gX_GUI_Canvas *canvas) {
    memset(s, 0, sizeof(*s));
    s->amount = canvas->elements.amount;
    s->blockSize = canvas->elements.blockSize;
    if (s->amount > 0) {
        s->data = (gX_U8 *)malloc((size_t)s->amount * s->blockSize);
        if (s->data) memcpy(s->data, canvas->elements.data, (size_t)s->amount * s->blockSize);
        s->meta = (gX_U8 *)malloc((size_t)s->amount * sizeof(gX_Heap_Meta));
        if (s->meta) memcpy(s->meta, canvas->elements.meta, (size_t)s->amount * sizeof(gX_Heap_Meta));
        s->subs = (gX_Tools_EditorSubSnap *)calloc((size_t)s->amount * GX_TOOLS_EDITOR_SUBHEAPS, sizeof(gX_Tools_EditorSubSnap));
    }
    if (!s->data || !s->meta || (s->amount > 0 && !s->subs)) {
        tools_snap_free(s);
        return;
    }
    const gX_GUI_Element *elems = (const gX_GUI_Element *)canvas->elements.data;
    for (gX_U32 i = 0; i < s->amount; i++) {
        for (gX_I32 k = 0; k < GX_TOOLS_EDITOR_SUBHEAPS; k++) {
            const gX_Heap *sh = tools_element_sub((gX_GUI_Element *)&elems[i], k);
            gX_Tools_EditorSubSnap *d = &s->subs[i * GX_TOOLS_EDITOR_SUBHEAPS + k];
            d->amount = sh->amount;
            d->blockSize = sh->blockSize;
            d->max = sh->max;
            if (sh->amount > 0 && sh->data && sh->blockSize > 0) {
                d->data = (gX_U8 *)malloc((size_t)sh->amount * sh->blockSize);
                if (d->data) memcpy(d->data, sh->data, (size_t)sh->amount * sh->blockSize);
            }
            if (sh->amount > 0 && sh->meta) {
                d->meta = (gX_U8 *)malloc((size_t)sh->amount * sizeof(gX_Heap_Meta));
                if (d->meta) memcpy(d->meta, sh->meta, (size_t)sh->amount * sizeof(gX_Heap_Meta));
            }
        }
    }
}

static gX_I0 tools_snap_restore(const gX_Tools_EditorSnap *s, gX_GUI_Canvas *canvas) {
    if (!s->data || !s->meta || !s->subs) return;
    //opencode: elements array in place (max monotonic; snapshot amount fits).
    if (s->amount > canvas->elements.max) return;
    //opencode: teardown current sub-heaps (seen-list: pre-fix duplicates may
    //   alias blocks; free each block once).
    gX_U8 *seen[2048];
    gX_I32 seenN = 0;
    gX_GUI_Element *elems = (gX_GUI_Element *)canvas->elements.data;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        for (gX_I32 k = 0; k < GX_TOOLS_EDITOR_SUBHEAPS; k++) {
            gX_Heap *h = tools_element_sub(&elems[i], k);
            for (gX_I32 q = 0; q < 2; q++) {
                gX_U8 *p = (q == 0) ? (gX_U8 *)h->data : (gX_U8 *)h->meta;
                if (!p) continue;
                gX_I1 dup = 0;
                for (gX_I32 si = 0; si < seenN; si++) {
                    if (seen[si] == p) { dup = 1; break; }
                }
                if (!dup) {
                    if (seenN < 2048) seen[seenN++] = p;
                    free(p);
                }
            }
            h->data = 0;
            h->meta = 0;
            h->amount = 0;
        }
    }
    canvas->elements.amount = s->amount;
    memcpy(canvas->elements.data, s->data, (size_t)s->amount * s->blockSize);
    memcpy(canvas->elements.meta, s->meta, (size_t)s->amount * sizeof(gX_Heap_Meta));
    elems = (gX_GUI_Element *)canvas->elements.data;
    for (gX_U32 i = 0; i < s->amount; i++) {
        for (gX_I32 k = 0; k < GX_TOOLS_EDITOR_SUBHEAPS; k++) {
            const gX_Tools_EditorSubSnap *d = &s->subs[i * GX_TOOLS_EDITOR_SUBHEAPS + k];
            gX_Heap *h = tools_element_sub(&elems[i], k);
            h->amount = 0;
            h->data = 0;
            h->meta = 0;
            //opencode: memcpy'd Heap struct above carried stale data/meta
            //   pointers + count; restore authoritative snapshot values.
            h->blockSize = d->blockSize;
            h->max = d->amount;
            if (d->amount > 0 && d->data && d->blockSize > 0) {
                h->data = (gX_U8 *)malloc((size_t)d->amount * d->blockSize);
                if (h->data) {
                    memcpy(h->data, d->data, (size_t)d->amount * d->blockSize);
                    h->amount = d->amount;
                }
            }
            if (d->amount > 0 && d->meta) {
                h->meta = (gX_Heap_Meta *)malloc((size_t)d->amount * sizeof(gX_Heap_Meta));
                if (h->meta) memcpy(h->meta, d->meta, (size_t)d->amount * sizeof(gX_Heap_Meta));
            }
        }
    }
    if (!tools_selection_resolve(canvas)) tools_selection_clear();
    canvas->refresh = true;
    gui.refresh = true;
}

#define GX_TOOLS_UNDO_MAX 32

static gX_Tools_EditorSnap s_undo[GX_TOOLS_UNDO_MAX];
static gX_I32 s_undoHead = 0;   // next write slot (ring)
static gX_I32 s_undoCount = 0;  // live entries
static gX_Tools_EditorSnap s_redo[GX_TOOLS_UNDO_MAX];
static gX_I32 s_redoHead = 0;
static gX_I32 s_redoCount = 0;
static gX_I1  s_gestureCaptured = 0;

static gX_I0 tools_undo_push_raw(gX_GUI_Canvas *canvas) {
    if (s_undoCount >= GX_TOOLS_UNDO_MAX) {
        tools_snap_free(&s_undo[s_undoHead]);          // drop oldest
        s_undoCount--;
    }
    tools_snap_capture(&s_undo[s_undoHead], canvas);
    s_undoHead = (s_undoHead + 1) % GX_TOOLS_UNDO_MAX;
    s_undoCount++;
}

static gX_I0 tools_undo_push(gX_GUI_Canvas *canvas) {
    tools_undo_push_raw(canvas);
    for (gX_I32 i = 0; i < s_redoCount; i++) tools_snap_free(&s_redo[i]);
    s_redoHead = 0;
    s_redoCount = 0;
}

static gX_I0 tools_undo_pop(gX_GUI_Canvas *canvas) {
    if (s_undoCount == 0) return;
    //opencode: v0.6.7 — capture CURRENT (post, pre-restore) to redo FIRST.
    //   The old order restored then captured, so redo held pre (no-op).
    if (s_redoCount >= GX_TOOLS_UNDO_MAX) {
        tools_snap_free(&s_redo[s_redoHead]);
        s_redoCount--;
    }
    tools_snap_capture(&s_redo[s_redoHead], canvas);
    s_redoHead = (s_redoHead + 1) % GX_TOOLS_UNDO_MAX;
    s_redoCount++;
    gX_I32 top = (s_undoHead - 1 + GX_TOOLS_UNDO_MAX) % GX_TOOLS_UNDO_MAX;
    tools_snap_restore(&s_undo[top], canvas);
    tools_snap_free(&s_undo[top]);
    s_undoHead = top;
    s_undoCount--;
}

static gX_I0 tools_redo_pop(gX_GUI_Canvas *canvas) {
    if (s_redoCount == 0) return;
    tools_undo_push_raw(canvas);   // current state -> undo (redo ring untouched)
    gX_I32 top = (s_redoHead - 1 + GX_TOOLS_UNDO_MAX) % GX_TOOLS_UNDO_MAX;
    tools_snap_restore(&s_redo[top], canvas);
    tools_snap_free(&s_redo[top]);
    s_redoHead = top;
    s_redoCount--;
}

//=============================================================================
// on-canvas drag + resize — move/resize elements directly with the mouse
//   when the ImGui window is not hovered (WantCaptureMouse false).
//=============================================================================

static gX_I32 s_dragMode = 0;      // 0 none, 1 move, 2 TL, 3 TR, 4 BL, 5 BR,
                                   // 6 L, 7 R, 8 T, 9 B
static gX_I32 s_dragSel = -1;
static gX_F32v2 s_dragStartMouse;
static gX_Rectangle s_dragStartRect;

static gX_I32 tools_hit_test(gX_GUI_Canvas *canvas, gX_F32 x, gX_F32 y) {
    const gX_GUI_Element *elems = (const gX_GUI_Element *)canvas->elements.data;
    for (gX_I32 i = (gX_I32)canvas->elements.amount - 1; i >= 0; i--) {
        const gX_GUI_Element *el = &elems[i];
        if (!el->visible) continue;
        if (x >= el->rect.x && x <= el->rect.x + el->rect.width &&
            y >= el->rect.y && y <= el->rect.y + el->rect.height)
            return i;
    }
    return -1;
}

static gX_I32 tools_resize_handle(const gX_GUI_Element *el, gX_F32 x, gX_F32 y) {
    const gX_F32 g = 6.0f;
    gX_F32 l = el->rect.x, r = el->rect.x + el->rect.width;
    gX_F32 t = el->rect.y, b = el->rect.y + el->rect.height;
    gX_I1 inX = x >= l && x <= r, inY = y >= t && y <= b;
    if (fabsf(x - l) <= g && fabsf(y - t) <= g) return 2;
    if (fabsf(x - r) <= g && fabsf(y - t) <= g) return 3;
    if (fabsf(x - l) <= g && fabsf(y - b) <= g) return 4;
    if (fabsf(x - r) <= g && fabsf(y - b) <= g) return 5;
    if (fabsf(x - l) <= g && inY) return 6;
    if (fabsf(x - r) <= g && inY) return 7;
    if (fabsf(y - t) <= g && inX) return 8;
    if (fabsf(y - b) <= g && inX) return 9;
    return 0;
}

static gX_F32 tools_grid(gX_F32 v) {
    if (!s_snapOn) return v;
    return roundf(v / 8.0f) * 8.0f;
}

static gX_I1 s_pressCaptured = 0; //opencode: press-routing — gesture started on ImGui

static gX_I0 tools_canvas_drag(gX_GUI_Canvas *canvas) {
    //opencode: engine mouse already owns game coords (Update_Mouse maps).
    gX_F32 mx = input.mouse.coord.x, my = input.mouse.coord.y;
    gX_I1 pressed = input.mouse.leftButton.PRESSED != 0;
    gX_I1 down = input.mouse.leftButton.DOWN != 0;
    gX_I1 captured = ImGui::GetIO().WantCaptureMouse != 0;

    if (pressed) {
        //opencode: press routing — a press that starts on ImGui can never
        //   start a canvas gesture (no panel-click select/deselect), and a
        //   press outside the viewport (pillarbox void) is ignored outright.
        s_pressCaptured = captured;
        if (!captured) {
            gX_Rectangle vp = gX_GUI_ViewportBox_Get();
            gX_Rectangle sc = gX_GUI_GameToScreen((gX_Rectangle){mx, my, 0.0f, 0.0f});
            if ((sc.x < vp.x) || (sc.y < vp.y)
                || (sc.x > vp.x + vp.width) || (sc.y > vp.y + vp.height)) {
                s_dragMode = 0;
                s_dragSel = -1;
                return;
            }
            gX_I32 sel = tools_hit_test(canvas, mx, my);
            if (sel >= 0) {
                gX_GUI_Element *el = &((gX_GUI_Element *)canvas->elements.data)[sel];
                tools_selection_set(canvas, sel);
                gX_I32 h = tools_resize_handle(el, mx, my);
                if (h) {
                    s_dragMode = h;
                } else {
                    s_dragMode = 1;
                }
                s_dragSel = sel;
                s_dragStartMouse = (gX_F32v2){ mx, my };
                s_dragStartRect = el->rect;
                tools_undo_push(canvas);
            } else {
                tools_selection_clear();
                s_dragMode = 0;
                s_dragSel = -1;
            }
        }
    }

    if (!down) {
        s_dragMode = 0;
        s_pressCaptured = 0;
        return;
    }
    //opencode: gesture owned by canvas survives panel crossing (no mid-drag
    //   kill); gestures born on ImGui never reach the canvas.
    if (s_pressCaptured) return;

    if (s_dragMode && s_dragSel >= 0
        && tools_selection_index(canvas) == s_dragSel) {
        gX_GUI_Element *el = tools_selection_resolve(canvas);
        if (!el) {
            s_dragMode = 0;
            return;
        }
        gX_F32 dx = mx - s_dragStartMouse.x, dy = my - s_dragStartMouse.y;
        gX_Rectangle r = s_dragStartRect;
        if (s_dragMode == 1) {
            el->rect.x = tools_grid(r.x + dx);
            el->rect.y = tools_grid(r.y + dy);
        } else {
            if (s_dragMode == 2 || s_dragMode == 6 || s_dragMode == 8)
                { r.x = tools_grid(r.x + dx); r.width = s_dragStartRect.x + s_dragStartRect.width - r.x; }
            if (s_dragMode == 4 || s_dragMode == 6)
                { r.y = tools_grid(r.y + dy); r.height = s_dragStartRect.y + s_dragStartRect.height - r.y; }
            if (s_dragMode == 3 || s_dragMode == 7 || s_dragMode == 9)
                { r.width = tools_grid(s_dragStartRect.width + dx); }
            if (s_dragMode == 4 || s_dragMode == 5 || s_dragMode == 9)
                { r.height = tools_grid(s_dragStartRect.height + dy); }
            if (r.width < 8.0f) r.width = 8.0f;
            if (r.height < 8.0f) r.height = 8.0f;
            el->rect = r;
        }
    }
}

//opencode: headless regression hook verifies that an ImGui-owned press cannot
//   start a canvas drag, including when the pointer crosses into the viewport.
extern "C" gX_I32 gX_Tools_Editor_TestInputCapture(gX_I0) {
    ImGuiContext *previousContext = ImGui::GetCurrentContext();
    ImGuiContext *testContext = ImGui::CreateContext();
    if (!testContext) return 1;

    gX_I32 errors = 0;
    gX_I32 savedTargetCanvas = s_targetCanvas;
    gX_Tools_Editor_Handle savedSelection = s_selection;
    gX_I32 savedPendingSelect = s_pendingSelect;
    gX_I32 savedDragMode = s_dragMode;
    gX_I32 savedDragSelection = s_dragSel;
    gX_F32v2 savedDragStartMouse = s_dragStartMouse;
    gX_Rectangle savedDragStartRect = s_dragStartRect;
    gX_I1 savedPressCaptured = s_pressCaptured;
    gX_F32v2 savedMouse = input.mouse.coord;
    gX_I1 savedPressed = input.mouse.leftButton.PRESSED;
    gX_I1 savedDown = input.mouse.leftButton.DOWN;
    gX_I1 savedReleased = input.mouse.leftButton.RELEASED;
    gX_I1 savedUp = input.mouse.leftButton.UP;
    gX_GUI_Canvas canvas = {0};
    gX_Rectangle initialRect = {120.0f, 80.0f, 96.0f, 64.0f};

    s_targetCanvas = 0;
    s_pendingSelect = -1;
    s_dragMode = 0;
    s_dragSel = -1;
    s_pressCaptured = 0;
    if (gX_GUI_Canvas_Load(&canvas) != 0
        || gX_GUI_Canvas_Add_Element(&canvas) != 0) {
        errors++;
    } else {
        gX_GUI_Element *element = &((gX_GUI_Element *)canvas.elements.data)[0];
        element->rect = initialRect;
        element->visible = true;
        tools_selection_set(&canvas, 0);

        ImGui::GetIO().WantCaptureMouse = true;
        input.mouse.coord = (gX_F32v2){150.0f, 100.0f};
        input.mouse.leftButton.PRESSED = true;
        input.mouse.leftButton.DOWN = true;
        input.mouse.leftButton.RELEASED = false;
        input.mouse.leftButton.UP = false;
        tools_canvas_drag(&canvas);

        input.mouse.coord = (gX_F32v2){220.0f, 180.0f};
        input.mouse.leftButton.PRESSED = false;
        ImGui::GetIO().WantCaptureMouse = false;
        tools_canvas_drag(&canvas);

        if (tools_selection_index(&canvas) != 0
            || memcmp(&element->rect, &initialRect, sizeof(initialRect)) != 0
            || s_dragMode != 0 || !s_pressCaptured) {
            errors++;
        }

        input.mouse.leftButton.DOWN = false;
        input.mouse.leftButton.UP = true;
        input.mouse.leftButton.RELEASED = true;
        tools_canvas_drag(&canvas);
        if (s_pressCaptured || s_dragMode != 0) errors++;
        gX_GUI_Canvas_Unload(&canvas);
    }

    s_targetCanvas = savedTargetCanvas;
    s_selection = savedSelection;
    s_pendingSelect = savedPendingSelect;
    s_dragMode = savedDragMode;
    s_dragSel = savedDragSelection;
    s_dragStartMouse = savedDragStartMouse;
    s_dragStartRect = savedDragStartRect;
    s_pressCaptured = savedPressCaptured;
    input.mouse.coord = savedMouse;
    input.mouse.leftButton.PRESSED = savedPressed;
    input.mouse.leftButton.DOWN = savedDown;
    input.mouse.leftButton.RELEASED = savedReleased;
    input.mouse.leftButton.UP = savedUp;
    ImGui::DestroyContext(testContext);
    ImGui::SetCurrentContext(previousContext);
    return errors;
}

//=============================================================================
// snapshot: live canvas -> markup tree (grid units via canvasBox/8)
//=============================================================================

extern "C" gX_I32 gX_Tools_Editor_SaveSnapshot(gX_I0);

//opencode: bounded copy for snapshot strings without strncpy truncation diagnostics.
static gX_I0 tools_string_copy(gX_I8 *destination, gX_U32 capacity,
    const gX_I8 *source) {
    if (!destination || !source || capacity == 0) return;
    gX_U32 i = 0;
    while (i + 1 < capacity && source[i]) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

//opencode: canonical lowercase markup type — the enum String() returns
//   uppercase (BUTTON); the markup schema is lowercase, and a case mismatch
//   rebuilt typed elements as plain rects.
static gX_I0 tools_type_lower(gX_I8 *destination, gX_U32 capacity,
    const gX_I8 *source) {
    tools_string_copy(destination, capacity, source);
    for (gX_U32 i = 0; destination[i]; i++) {
        if (destination[i] >= 'A' && destination[i] <= 'Z')
            destination[i] = (gX_I8)(destination[i] + ('a' - 'A'));
    }
}

static gX_I0 tools_snapshot_element(const gX_GUI_Element *el, gX_MarkupElement *me,
                                    gX_F32 gxDiv, gX_F32 gyDiv) {
    memset(me, 0, sizeof(*me));
    const gX_I8 *ts = gX_GUI_Element_Type_String((gX_I32)el->type);
    tools_type_lower(me->type, sizeof(me->type), ts ? ts : "rect");
    strncpy(me->name, el->name, sizeof(me->name) - 1);
    strncpy(me->cssClass, el->cssClass, sizeof(me->cssClass) - 1);
    strncpy(me->cssStyle, el->cssStyle, sizeof(me->cssStyle) - 1);
    me->gx = el->rect.x / gxDiv;
    me->gy = el->rect.y / gyDiv;
    me->gw = el->rect.width / gxDiv;
    me->gh = el->rect.height / gyDiv;
    me->color = el->color;
    me->layer = el->layer;
    me->visible = el->visible ? 1 : 0;
    if (el->texts.amount > 0 && el->texts.data) {
        const gX_GUI_Element_Text *t = (const gX_GUI_Element_Text *)el->texts.data;
        tools_string_copy(me->text, sizeof(me->text), t->text);
        me->fontSize = t->fontSize;
        me->centerX = el->stateRendering.TEXT_CENTER_X ? 1 : 0;
        me->centerY = el->stateRendering.TEXT_CENTER_Y ? 1 : 0;
    }
}

//opencode: texts beyond the first become child text elements; the first is
//   the element's own text (previously duplicated as a child, which rebuilt
//   every text-bearing element as an anonymous container).
static gX_I0 tools_snapshot_children(const gX_GUI_Element *el, gX_MarkupElement *me,
                                     gX_F32 gxDiv, gX_F32 gyDiv) {
    (void)gxDiv;
    (void)gyDiv;
    for (gX_U32 i = 1; i < el->texts.amount; i++) {
        gX_GUI_Element_Text *t = gX_GUI_Element_Text_Get((gX_GUI_Element *)el, i);
        if (t && t->text[0]) {
            gX_MarkupElement kid = { 0 };
            strncpy(kid.type, "text", sizeof(kid.type) - 1);
            tools_string_copy(kid.text, sizeof(kid.text), t->text);
            kid.fontSize = t->fontSize;
            { gX_MarkupElement *kp = (gX_MarkupElement *)gX_Heap_Add(&me->children); if (kp) *kp = kid; }
        }
    }
}

//opencode: shared canvas -> markup capture (editor save + the round-trip test).
static gX_I0 tools_snapshot_canvas(gX_GUI_Canvas *canvas, gX_Markup *markup) {
    memset(markup, 0, sizeof(*markup));
    tools_string_copy(markup->name, sizeof(markup->name), canvas->name);
    tools_string_copy(markup->description, sizeof(markup->description), canvas->description);
    markup->templateID = canvas->templateID;
    gX_Heap_SetBlockSize(&markup->elements, (gX_U32)sizeof(gX_MarkupElement));
    gX_Heap_SetMax(&markup->elements, 64);
    gX_Heap_Init(&markup->elements);

    gX_F32 gxDiv = canvas->canvasBox.width > 0 ? canvas->canvasBox.width / 8.0f : 1.0f;
    gX_F32 gyDiv = canvas->canvasBox.height > 0 ? canvas->canvasBox.height / 8.0f : 1.0f;

    const gX_GUI_Element *elems = (const gX_GUI_Element *)canvas->elements.data;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        const gX_GUI_Element *el = &elems[i];
        if (!el || el->name[0] == '\0') continue;
        gX_MarkupElement me;
        tools_snapshot_element(el, &me, gxDiv, gyDiv);
        tools_snapshot_children(el, &me, gxDiv, gyDiv);
        { gX_MarkupElement *mp = (gX_MarkupElement *)gX_Heap_Add(&markup->elements); if (mp) *mp = me; }
    }
}

extern "C" gX_I32 gX_Tools_Editor_SaveSnapshot(gX_I0) {
    if (!s_savePath[0]) return 1;
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
    if (!canvas) return 1;
    gX_Markup markup;
    tools_snapshot_canvas(canvas, &markup);
    gX_I32 err = gX_GUI_Markup_Save(&markup, s_savePath);
    gX_GUI_Markup_Clear(&markup);
    return err;
}

//opencode: headless round-trip pin for the editor snapshot path (canvas ->
//   markup file -> load -> rebuild). Types, names, rects, colors, layers,
//   visibility, and the first text must survive; the fixture elements are
//   named so the snapshot includes them (unnamed elements are editor-skipped).
extern "C" gX_I32 gX_Tools_Editor_TestSnapshotRoundTrip(gX_I0) {
    //opencode: per-process path — parallel suite instances (concurrent audit
    //   captures) must not clobber each other's snapshot file.
    gX_I8 pathBuf[160];
    snprintf((char *)pathBuf, sizeof(pathBuf),
        "/tmp/gx_test_%ld_gui_snapshot.json", (long)getpid());
    const gX_I8 *path = pathBuf;
    gX_I32 errors = 0;
    gX_I32 named = 0;
    gX_GUI_Canvas source = {0};
    gX_GUI_Canvas rebuilt = {0};
    gX_Markup markup = {0};
    gX_Markup loaded = {0};
    gX_I1 sourceLoaded = false;
    gX_I1 rebuiltLoaded = false;
    struct Fixture {
        const gX_I8 *name;
        gX_GUI_Element_Type type;
        gX_Rectangle rect;
        const gX_I8 *text;
        gX_I1 visible;
        gX_Color color;
        gX_U32 layer;
    };
    const struct Fixture fixture[4] = {
        {"snap-panel", gX_GUI_ELEMENT_TYPE_PANEL, {100, 100, 400, 300}, "", 1, {10, 20, 30, 255}, 3},
        {"snap-title", gX_GUI_ELEMENT_TYPE_LABEL, {120, 120, 360, 40}, "Snapshot title", 1, {200, 210, 220, 255}, 2},
        {"snap-button", gX_GUI_ELEMENT_TYPE_BUTTON, {120, 180, 200, 60}, "Press", 1, {40, 80, 160, 255}, 1},
        {"snap-hidden", gX_GUI_ELEMENT_TYPE_RECT, {520, 100, 100, 100}, "", 0, {90, 0, 0, 255}, 0},
    };

    if (gX_GUI_Canvas_Load(&source) != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: source canvas");
        return 1;
    }
    sourceLoaded = true;
    source.canvasBox = (gX_Rectangle){0, 0, 1024, 768};
    for (gX_U32 i = 0; i < 4; i++) {
        gX_I32 slot = gX_GUI_Canvas_Add_Element(&source);
        if (slot < 0) { errors++; continue; }
        gX_GUI_Element *element = (gX_GUI_Element *)gX_Heap_SeekPtr_Index(&source.elements, (gX_U32)slot);
        if (!element) { errors++; continue; }
        tools_string_copy(element->name, sizeof(element->name), fixture[i].name);
        element->type = fixture[i].type;
        element->rect = fixture[i].rect;
        element->visible = fixture[i].visible;
        element->color = fixture[i].color;
        element->layer = fixture[i].layer;
        if (fixture[i].text[0]) {
            if (gX_GUI_Element_Add_Text(element) == 0) {
                gX_GUI_Element_Text *text = gX_GUI_Element_Text_Get(element, 0);
                if (text) tools_string_copy(text->text, sizeof(text->text), fixture[i].text);
            } else {
                errors++;
            }
        }
    }

    tools_snapshot_canvas(&source, &markup);
    if (gX_GUI_Markup_Save(&markup, path) != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: save '%s'", path);
        errors++;
    }
    gX_GUI_Markup_Clear(&markup);
    if (errors == 0 && gX_GUI_Markup_Load(&loaded, path) != 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: load '%s'", path);
        errors++;
    }
    if (gX_GUI_Canvas_Load(&rebuilt) == 0) {
        rebuiltLoaded = true;
        rebuilt.canvasBox = source.canvasBox;
        gX_GUI_Markup_Build(&loaded, &rebuilt);
    } else {
        errors++;
    }

    if (errors == 0) {
        const gX_GUI_Element *sourceElements = (const gX_GUI_Element *)source.elements.data;
        const gX_GUI_Element *rebuiltElements = (const gX_GUI_Element *)rebuilt.elements.data;
        for (gX_U32 i = 0; i < source.elements.amount; i++) {
            const gX_GUI_Element *wanted = &sourceElements[i];
            if (!wanted->name[0] || strcmp(wanted->name, "unnamed") == 0) continue;
            named++;
            const gX_GUI_Element *found = NULL;
            for (gX_U32 k = 0; k < rebuilt.elements.amount; k++) {
                if (rebuiltElements[k].name[0]
                    && strcmp(rebuiltElements[k].name, wanted->name) == 0) {
                    found = &rebuiltElements[k];
                    break;
                }
            }
            if (!found) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' missing after rebuild", wanted->name);
                errors++;
                continue;
            }
            if (found->type != wanted->type) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' type %d != %d",
                    wanted->name, (gX_I32)found->type, (gX_I32)wanted->type);
                errors++;
            }
            if (fabsf(found->rect.x - wanted->rect.x) > 1.0f
                || fabsf(found->rect.y - wanted->rect.y) > 1.0f
                || fabsf(found->rect.width - wanted->rect.width) > 1.0f
                || fabsf(found->rect.height - wanted->rect.height) > 1.0f) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' rect (%.0f,%.0f,%.0f,%.0f) != (%.0f,%.0f,%.0f,%.0f)",
                    wanted->name, found->rect.x, found->rect.y, found->rect.width, found->rect.height,
                    wanted->rect.x, wanted->rect.y, wanted->rect.width, wanted->rect.height);
                errors++;
            }
            if ((found->visible != 0) != (wanted->visible != 0)) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' visibility", wanted->name);
                errors++;
            }
            if (found->color.r != wanted->color.r || found->color.g != wanted->color.g
                || found->color.b != wanted->color.b || found->color.a != wanted->color.a) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' color", wanted->name);
                errors++;
            }
            if (found->layer != wanted->layer) {
                gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' layer %u != %u",
                    wanted->name, found->layer, wanted->layer);
                errors++;
            }
            if (wanted->texts.amount > 0 && wanted->texts.data) {
                const gX_GUI_Element_Text *wantedText = (const gX_GUI_Element_Text *)wanted->texts.data;
                gX_I1 present = false;
                if (found->texts.amount > 0 && found->texts.data) {
                    const gX_GUI_Element_Text *foundText = (const gX_GUI_Element_Text *)found->texts.data;
                    present = strcmp(foundText->text, wantedText->text) == 0;
                }
                for (gX_U32 k = 0; !present && k < rebuilt.elements.amount; k++) {
                    if (rebuiltElements[k].texts.amount > 0 && rebuiltElements[k].texts.data) {
                        const gX_GUI_Element_Text *text = (const gX_GUI_Element_Text *)rebuiltElements[k].texts.data;
                        present = strcmp(text->text, wantedText->text) == 0;
                    }
                }
                if (!present) {
                    gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip FAIL: '%s' text lost", wanted->name);
                    errors++;
                }
            }
        }
    }

    gX_GUI_Markup_Clear(&loaded);
    if (rebuiltLoaded) gX_GUI_Canvas_Unload(&rebuilt);
    if (sourceLoaded) gX_GUI_Canvas_Unload(&source);
    remove((char *)path);
    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GUI SNAPSHOT roundtrip tests: all passed (named=%d)", named);
    }
    return errors;
}

//=============================================================================
// actions
//=============================================================================

static gX_I0 tools_add_element(gX_I32 kind) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
    if (!canvas) return;
    tools_undo_push(canvas);
    gX_I32 idx = gX_GUI_Canvas_Add_Element(canvas);
    if (idx < 0) return;
    gX_GUI_Element *el = (gX_GUI_Element *)gX_Heap_SeekPtr_Index(&canvas->elements, (gX_U32)idx);
    if (!el) return;
    el->rect = (gX_Rectangle){ 200, 200, 300, 60 };
    el->visible = 1;
    switch (kind) {
        case 1: el->type = gX_GUI_ELEMENT_TYPE_LABEL;  break;
        case 2: el->type = gX_GUI_ELEMENT_TYPE_BUTTON; gX_GUI_Element_MakeClickable(el); break;
        case 3: el->type = gX_GUI_ELEMENT_TYPE_PANEL;  break;
        case 4: el->type = gX_GUI_ELEMENT_TYPE_TEXTURE; break;
        default: el->type = gX_GUI_ELEMENT_TYPE_RECT;  break;
    }
    tools_selection_set(canvas, idx);
    gX_TraceLog(gX_LOG_INFO, "Tools: added element %i (kind %i)", idx, kind);
}

static gX_I0 tools_remove_element(gX_I32 idx) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
    if (!canvas) return;
    if (idx < 0 || (gX_U32)idx >= canvas->elements.amount) return;
    tools_undo_push(canvas);
    gX_GUI_Canvas_Remove_Element(canvas, (gX_U32)idx);
    if (tools_selection_index(canvas) == idx) tools_selection_clear();
    gX_TraceLog(gX_LOG_INFO, "Tools: removed element %i", idx);
}

static gX_I0 tools_duplicate_element(gX_I32 idx) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
    if (!canvas) return;
    if (idx < 0 || (gX_U32)idx >= canvas->elements.amount) return;
    gX_GUI_Element *src = &((gX_GUI_Element *)canvas->elements.data)[idx];
    tools_undo_push(canvas);
    gX_I32 nidx = gX_GUI_Canvas_Add_Element(canvas);
    if (nidx < 0) return;
    gX_GUI_Element *dst = (gX_GUI_Element *)gX_Heap_SeekPtr_Index(&canvas->elements, (gX_U32)nidx);
    if (!dst) return;
    //opencode: v0.6.7 — clone struct + deep-copy ALL sub-heaps (+16px).
    //   The old texts-only copy aliased shapes/shaders (editing the copy
    //   edited the original); generic copy also makes undo teardown safe.
    tools_element_deep_copy(dst, src);
    dst->rect.x += 16.0f;
    dst->rect.y += 16.0f;
    tools_selection_set(canvas, nidx);
    gX_TraceLog(gX_LOG_INFO, "Tools: duplicated element %i -> %i", idx, nidx);
}

//=============================================================================
// editor panels
//=============================================================================

static gX_I0 tools_panel_tree(gX_GUI_Canvas *canvas) {
    gX_I32 selectionIndex = tools_selection_index(canvas);
    if (!ImGui::CollapsingHeader("Elements", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const gX_GUI_Element *elems = (const gX_GUI_Element *)canvas->elements.data;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        const gX_GUI_Element *el = &elems[i];
        //opencode: v0.6.6 — substring filter (empty matches all)
        if (s_treeFilter[0]) {
            gX_I8 nmLower[128], fLower[64];
            gX_I32 ni = 0;
            for (; el->name[ni] && ni < 127; ni++)
                nmLower[ni] = (el->name[ni] >= 'A' && el->name[ni] <= 'Z') ? (gX_I8)(el->name[ni] + 32) : el->name[ni];
            nmLower[ni] = '\0';
            gX_I32 fi2 = 0;
            for (; s_treeFilter[fi2] && fi2 < 63; fi2++)
                fLower[fi2] = (s_treeFilter[fi2] >= 'A' && s_treeFilter[fi2] <= 'Z') ? (gX_I8)(s_treeFilter[fi2] + 32) : s_treeFilter[fi2];
            fLower[fi2] = '\0';
            if (!strstr(nmLower, fLower)) continue;
        }
        const gX_I8 *ts = gX_GUI_Element_Type_String((gX_I32)el->type);
        gX_I8 label[160];
        //opencode: dock hardening — elide the name to the row width (slim
        //   dock clipped mid-word); id/type stay, full label on tooltip.
        {
            const gX_I8 *nm = el->name[0] ? el->name : (const gX_I8 *)"(unnamed)";
            gX_I8 nmShow[120];
            strncpy(nmShow, nm, sizeof(nmShow) - 1);
            nmShow[sizeof(nmShow) - 1] = '\0';
            gX_F32 avail = ImGui::GetContentRegionAvail().x - 8.0f;
            gX_I32 nmLen = (gX_I32)strlen(nmShow);
            gX_I1 cut = 0;
            //opencode: hidden marker — invisible elements (NO_DRAW/alpha 0)
            //   show why their marker floats over empty canvas.
            const gX_I8 *hid = (el->stateRendering.RECT_NO_DRAW || (el->color.a == 0))
                ? (const gX_I8 *)" (hidden)" : (const gX_I8 *)"";
            snprintf(label, sizeof(label), "[%u] %s %s%s##el%u", el->id,
                nmShow, ts ? ts : "?", hid, i);
            while ((nmLen > 4) && (avail > 40.0f) && (ImGui::CalcTextSize(label).x > avail)) {
                nmLen--;
                nmShow[nmLen] = '\0';
                cut = 1;
                snprintf(label, sizeof(label), "[%u] %s %s%s##el%u", el->id,
                    nmShow, ts ? ts : "?", hid, i);
            }
            if (cut && (nmLen > 3)) {
                nmShow[nmLen - 3] = '.';
                nmShow[nmLen - 2] = '.';
                nmShow[nmLen - 1] = '.';
                nmShow[nmLen] = '\0';
                snprintf(label, sizeof(label), "[%u] %s %s%s##el%u", el->id,
                    nmShow, ts ? ts : "?", hid, i);
            }
        }
        //opencode: visibility eye-toggle — same row, undoable, so hiding
        //   needs no trip to Properties. Mutable lookup: elems[] is const.
        {
            gX_GUI_Element *mut = &((gX_GUI_Element *)canvas->elements.data)[i];
            ImGui::PushID((gX_I32)i);
            if (ImGui::SmallButton(mut->visible ? "o" : "x")) {
                tools_undo_push(canvas);
                mut->visible = mut->visible ? 0 : 1;
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(mut->visible ? "hide element" : "show element");
            ImGui::SameLine();
        }
        //opencode: family tint — rows scan by type at a glance (green =
        //   interactive, lavender = media, cool gray = structural, white =
        //   label/meta). Selected rows keep the tint; ImGui's selected bg
        //   stays readable under all four.
        {
            ImVec4 tc(0.94f, 0.94f, 0.94f, 1.0f);
            switch (el->type) {
                case gX_GUI_ELEMENT_TYPE_BUTTON:
                case gX_GUI_ELEMENT_TYPE_SLIDER:
                case gX_GUI_ELEMENT_TYPE_CHECKBOX:
                case gX_GUI_ELEMENT_TYPE_DROPDOWN:
                case gX_GUI_ELEMENT_TYPE_RADIO:
                case gX_GUI_ELEMENT_TYPE_TOGGLE:
                case gX_GUI_ELEMENT_TYPE_PROGRESS:
                case gX_GUI_ELEMENT_TYPE_TEXTINPUT:
                case gX_GUI_ELEMENT_TYPE_STEPPER:
                case gX_GUI_ELEMENT_TYPE_SCROLLLIST:
                case gX_GUI_ELEMENT_TYPE_TABPANEL:
                case gX_GUI_ELEMENT_TYPE_SPINNER:
                    tc = ImVec4(0.55f, 0.86f, 0.55f, 1.0f);
                    break;
                case gX_GUI_ELEMENT_TYPE_TEXTURE:
                case gX_GUI_ELEMENT_TYPE_SHADER:
                case gX_GUI_ELEMENT_TYPE_MODEL:
                    tc = ImVec4(0.86f, 0.66f, 0.94f, 1.0f);
                    break;
                case gX_GUI_ELEMENT_TYPE_RECT:
                case gX_GUI_ELEMENT_TYPE_PANEL:
                case gX_GUI_ELEMENT_TYPE_CONTAINER:
                case gX_GUI_ELEMENT_TYPE_SHAPE:
                    tc = ImVec4(0.59f, 0.67f, 0.82f, 1.0f);
                    break;
                default:
                    break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
        }
        if (ImGui::Selectable(label, (gX_I32)i == selectionIndex)) {
            tools_selection_set(canvas, (gX_I32)i);
            selectionIndex = (gX_I32)i;
        }
        ImGui::PopStyleColor();
        //opencode: v0.6.9 — full label on hover (slim dock clips rows).
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("[%u] %s %s", el->id, el->name[0] ? el->name : "(unnamed)", ts ? ts : "?");
    }
}

static gX_I0 tools_panel_properties(gX_GUI_Canvas *canvas) {
    gX_GUI_Element *el = tools_selection_resolve(canvas);
    if (!ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!el) {
        //opencode: empty-state guidance — the bare "no selection" left the
        //   whole column reading as dead (editor audit).
        ImGui::TextDisabled("no selection");
        ImGui::TextWrapped("Pick a row in the LEFT list, or click an element on the canvas.");
        ImGui::TextWrapped("The o/x button on each row toggles visibility.");
        ImGui::Text("elements: %u", canvas->elements.amount);
        return;
    }
    ImGui::InputText("name", el->name, gX_GUI_ELEMENT_NAMEBFR_SIZE);
    ImGui::DragFloat("x", &el->rect.x, 1.0f, -2048.0f, 2048.0f, "%.0f");
        ImGui::SetItemTooltip("Left edge, canvas pixels. Arrows nudge; drag on canvas to move.");
    ImGui::DragFloat("y", &el->rect.y, 1.0f, -2048.0f, 2048.0f, "%.0f");
        ImGui::SetItemTooltip("Top edge, canvas pixels.");
    ImGui::DragFloat("w", &el->rect.width, 1.0f, 0.0f, 2048.0f, "%.0f");
        ImGui::SetItemTooltip("Width, canvas pixels. White squares on canvas resize.");
    ImGui::DragFloat("h", &el->rect.height, 1.0f, 0.0f, 2048.0f, "%.0f");
        ImGui::SetItemTooltip("Height, canvas pixels.");
    ImGui::ColorEdit4("color", (float *)&el->color,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
        ImGui::SetItemTooltip("Fill color + opacity. Transparent (A=0) hides the box but keeps children.");
    ImGui::DragFloat("scale", &el->params.scale.x, 0.01f, 0.1f, 10.0f, "%.2f");
        ImGui::SetItemTooltip("Size multiplier around center (1 = off). Used by hover pulse.");
    el->params.scale.y = el->params.scale.x;
    ImGui::DragFloat("angle", &el->params.angle, 0.5f, -360.0f, 360.0f, "%.1f");
        ImGui::SetItemTooltip("Rotation, degrees.");
    ImGui::Checkbox("visible", (bool *)&el->visible);
        ImGui::SetItemTooltip("Uncheck to hide (stays in list, skipped by hit-test).");

    gX_I32 layer = (gX_I32)el->layer;
    if (ImGui::DragInt("layer", &layer, 1, 0, 255)) el->layer = (gX_U8)layer;

    //opencode: v0.6.8 — flag groups collapsed by default (declutter).
    if (ImGui::CollapsingHeader("state")) {
        gX_GUI_Element_State_Bitmask sb;
        sb.state = el->state;
        bool v;
        v = sb.state.HOVERABLE;      if (ImGui::Checkbox("HOVERABLE", &v)) { sb.state.HOVERABLE = v; el->state = sb.state; }
        ImGui::SetItemTooltip("Mouse-over highlights the element. Needed for tooltips.");
        v = sb.state.FOCUSABLE;      if (ImGui::Checkbox("FOCUSABLE", &v)) { sb.state.FOCUSABLE = v; el->state = sb.state; }
        ImGui::SetItemTooltip("Keyboard/gamepad can focus it (Tab / focus ring).");
        v = sb.state.CLICKABLE_LEFT; if (ImGui::Checkbox("CLICKABLE_LEFT", &v)) { sb.state.CLICKABLE_LEFT = v; el->state = sb.state; }
        ImGui::SetItemTooltip("Left-click runs its action. Without this, clicks pass through.");
        v = sb.state.PRESSABLE_LEFT; if (ImGui::Checkbox("PRESSABLE_LEFT", &v)) { sb.state.PRESSABLE_LEFT = v; el->state = sb.state; }
        ImGui::SetItemTooltip("Hold-left shows the pressed (darkened) state.");
        v = sb.state.DISABLED;       if (ImGui::Checkbox("DISABLED", &v)) { sb.state.DISABLED = v; el->state = sb.state; }
        ImGui::SetItemTooltip("Greyed out at 40%% alpha; ignores input.");
    }

    if (ImGui::CollapsingHeader("rendering")) {
        gX_GUI_Element_Rendering_Bitmask rb;
        rb.rendering = el->stateRendering;
        bool v;
        v = rb.rendering.TEXT_CENTER_X;   if (ImGui::Checkbox("TEXT_CENTER_X", &v)) { rb.rendering.TEXT_CENTER_X = v; el->stateRendering = rb.rendering; }
        ImGui::SetItemTooltip("Center text horizontally in the box.");
        v = rb.rendering.TEXT_CENTER_Y;   if (ImGui::Checkbox("TEXT_CENTER_Y", &v)) { rb.rendering.TEXT_CENTER_Y = v; el->stateRendering = rb.rendering; }
        ImGui::SetItemTooltip("Center text vertically in the box.");
        v = rb.rendering.TEXT_SHADOW;     if (ImGui::Checkbox("TEXT_SHADOW", &v)) { rb.rendering.TEXT_SHADOW = v; el->stateRendering = rb.rendering; }
        ImGui::SetItemTooltip("Soft dark copy behind text for readability.");
        v = rb.rendering.RECT_DRAW_WIRE;  if (ImGui::Checkbox("RECT_DRAW_WIRE", &v)) { rb.rendering.RECT_DRAW_WIRE = v; el->stateRendering = rb.rendering; }
        ImGui::SetItemTooltip("Outline only, no fill. Good for guides and frames.");
        v = rb.rendering.RECT_NO_DRAW;    if (ImGui::Checkbox("RECT_NO_DRAW", &v)) { rb.rendering.RECT_NO_DRAW = v; el->stateRendering = rb.rendering; }
        ImGui::SetItemTooltip("Skip the box; draw children only (text/shapes still show).");
    }

    if (ImGui::CollapsingHeader("text")) {
    if (el->texts.amount > 0 && el->texts.data) {
        gX_GUI_Element_Text *t = (gX_GUI_Element_Text *)el->texts.data;
        ImGui::InputText("text", t->text, gX_GUI_ELEMENT_TEXT_MAX);
        ImGui::DragFloat("fontSize", &t->fontSize, 1.0f, 1.0f, 256.0f, "%.0f");
        ImGui::SetItemTooltip("Text height in pixels.");
        ImGui::ColorEdit4("text color", (float *)&t->color, ImGuiColorEditFlags_NoInputs);
        ImGui::SetItemTooltip("Text fill color + opacity.");
    } else {
        ImGui::TextDisabled("no text module (type RECT)");
    }
    }
}

//opencode: toolbar — Add/Dup/Del/Save always visible above the tree so the
//   only action surface is no longer the collapsed Actions section.
static const gX_I8 *tools_add_type_name(gX_I32 idx) {
    static const gX_I8 *names[] = { "RECT", "LABEL", "BUTTON", "PANEL", "TEXTURE", "CONTAINER" };
    if (idx < 0 || idx > 5) idx = 0;
    return names[idx];
}
static gX_I0 tools_panel_toolbar(gX_GUI_Canvas *canvas) {
    gX_I32 selectionIndex = tools_selection_index(canvas);
    (void)canvas;
    if (ImGui::SmallButton("+ Add")) {
        tools_add_element(s_addTypeIdx);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add %s element (type picker lives in Actions below)",
            tools_add_type_name(s_addTypeIdx));
    ImGui::SameLine();
    if (ImGui::SmallButton("Dup") && selectionIndex >= 0) {
        tools_duplicate_element(selectionIndex);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Duplicate selected element (+16px offset)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Del") && selectionIndex >= 0) {
        s_confirmRemove = 1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete selected element (asks first)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save")) {
        tools_save_request();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save markup (overwrite asks unless it is the opened file)");
}

static gX_I0 tools_panel_actions(void) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
    gX_I32 selectionIndex = tools_selection_index(canvas);
    //opencode: v0.6.8 — Actions collapsed by default (declutter).
    if (!ImGui::CollapsingHeader("Actions")) return;

    const gX_I8 *typeNames[] = { "RECT", "LABEL", "BUTTON", "PANEL", "TEXTURE", "CONTAINER" };
    ImGui::Combo("type", &s_addTypeIdx, typeNames, 6);
    if (ImGui::Button("Add element")) {
        tools_add_element(s_addTypeIdx);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove selected") && selectionIndex >= 0) {
        s_confirmRemove = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && selectionIndex >= 0) {
        tools_duplicate_element(selectionIndex);
    }

    ImGui::TextDisabled("undo/redo: Ctrl+Z / Ctrl+Y (moves, edits, add/remove)");

    ImGui::Separator();
    ImGui::InputText("save path", s_savePath, sizeof(s_savePath));
    if (ImGui::Button("Save markup")) {
        tools_save_request();
    }
}

//=============================================================================
// selection overlay — foreground draw list (screen space, above all panels).
//   Maps the selected element rect (canvas buffer px) to screen via the
//   same composite mapping WidgetSDF uses, then draws the outline + the 8
//   resize handles so the invisible 6px grab zones become seeable.
//=============================================================================
static gX_I0 tools_rect_to_screen(gX_GUI_Canvas *canvas, gX_Rectangle r,
    gX_F32 *x0, gX_F32 *y0, gX_F32 *x1, gX_F32 *y1) {
    gX_F32 bw = canvas->buffer.width > 0 ? (gX_F32)canvas->buffer.width : 1.0f;
    gX_F32 bh = canvas->buffer.height > 0 ? (gX_F32)canvas->buffer.height : 1.0f;
    gX_F32 kx = canvas->canvasBox.width / bw;
    gX_F32 ky = canvas->canvasBox.height / bh;
    gX_Rectangle gm;
    gX_Rectangle sc;
    gm.x = canvas->canvasBox.x + canvas->canvasOffset.x + r.x * kx;
    gm.y = canvas->canvasBox.y + canvas->canvasOffset.y + r.y * ky;
    gm.width = r.width * kx;
    gm.height = r.height * ky;
    //opencode: overlay fix — the foreground draw list is screen px, so map
    //   game->screen (identity until docked; pillarbox offset+scale after).
    sc = gX_GUI_GameToScreen(gm);
    *x0 = sc.x;
    *y0 = sc.y;
    *x1 = sc.x + sc.width;
    *y1 = sc.y + sc.height;
}

//opencode: 8px snap grid — the snap math was invisible; draw the lattice it
//   snaps to, clipped to the canvas, majors every 64px.
static gX_I0 tools_draw_grid(gX_GUI_Canvas *canvas) {
    if (!s_gridOn) return;
    if (canvas->canvasBox.width <= 0.0f || canvas->canvasBox.height <= 0.0f) return;
    gX_F32 x0, y0, x1, y1;
    gX_Rectangle full = { 0.0f, 0.0f, canvas->canvasBox.width, canvas->canvasBox.height };
    tools_rect_to_screen(canvas, full, &x0, &y0, &x1, &y1);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
    gX_F32 sx = (x1 - x0) / canvas->canvasBox.width;
    gX_F32 sy = (y1 - y0) / canvas->canvasBox.height;
    for (gX_F32 gx = 0.0f; gx <= canvas->canvasBox.width + 0.5f; gx += 8.0f) {
        gX_I1 major = ((gX_I32)(gx + 0.5f) % 64) == 0;
        dl->AddLine(ImVec2(x0 + gx * sx, y0), ImVec2(x0 + gx * sx, y1),
            major ? IM_COL32(120, 180, 240, 60) : IM_COL32(120, 180, 240, 22),
            major ? 1.5f : 1.0f);
    }
    for (gX_F32 gy = 0.0f; gy <= canvas->canvasBox.height + 0.5f; gy += 8.0f) {
        gX_I1 major = ((gX_I32)(gy + 0.5f) % 64) == 0;
        dl->AddLine(ImVec2(x0, y0 + gy * sy), ImVec2(x1, y0 + gy * sy),
            major ? IM_COL32(120, 180, 240, 60) : IM_COL32(120, 180, 240, 22),
            major ? 1.5f : 1.0f);
    }
    dl->PopClipRect();
}

//opencode: drag guides — full-span accent lines through the dragged rect's
//   edges and center, so snap landings read against the grid. Tied to the
//   grid toggle, not the selection overlay (it is snap feedback).
static gX_I0 tools_draw_drag_guides(gX_GUI_Canvas *canvas) {
    if (!s_gridOn || !s_dragMode || s_dragSel < 0
        || (gX_U32)s_dragSel >= canvas->elements.amount) return;
    if (tools_selection_index(canvas) != s_dragSel) return;
    gX_GUI_Element *el = tools_selection_resolve(canvas);
    if (!el) return;
    gX_F32 x0, y0, x1, y1;
    gX_Rectangle full = { 0.0f, 0.0f, canvas->canvasBox.width, canvas->canvasBox.height };
    gX_F32 cx0, cy0, cx1, cy1;
    tools_rect_to_screen(canvas, full, &cx0, &cy0, &cx1, &cy1);
    tools_rect_to_screen(canvas, el->rect, &x0, &y0, &x1, &y1);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(cx0, cy0), ImVec2(cx1, cy1), true);
    gX_U32 col = IM_COL32(120, 180, 240, 140);
    gX_F32 mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    dl->AddLine(ImVec2(x0, cy0), ImVec2(x0, cy1), col, 1.0f);
    dl->AddLine(ImVec2(x1, cy0), ImVec2(x1, cy1), col, 1.0f);
    dl->AddLine(ImVec2(cx0, y0), ImVec2(cx1, y0), col, 1.0f);
    dl->AddLine(ImVec2(cx0, y1), ImVec2(cx1, y1), col, 1.0f);
    dl->AddLine(ImVec2(mx, cy0), ImVec2(mx, cy1), IM_COL32(120, 180, 240, 70), 1.0f);
    dl->AddLine(ImVec2(cx0, my), ImVec2(cx1, my), IM_COL32(120, 180, 240, 70), 1.0f);
    dl->PopClipRect();
}

static gX_I0 tools_draw_overlay(gX_GUI_Canvas *canvas) {
    gX_GUI_Element *el = tools_selection_resolve(canvas);
    tools_draw_grid(canvas);
    tools_draw_drag_guides(canvas);
    if (!s_overlayOn || !el) return;
    gX_F32 x0, y0, x1, y1;
    tools_rect_to_screen(canvas, el->rect, &x0, &y0, &x1, &y1);
    //opencode: invisible elements (NO_DRAW render flag or zero alpha) get a
    //   yellow marker + tag instead of white — the rect is real and editable,
    //   but nothing draws there, so a white box reads as misaligned.
    gX_I1 hidden = (el->stateRendering.RECT_NO_DRAW || (el->color.a == 0)) ? 1 : 0;
    gX_U32 lineCol = hidden ? IM_COL32(255, 210, 90, 255) : IM_COL32(255, 255, 255, 255);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    //opencode: dark halo under the marker — white reads on dark canvas but
    //   vanishes on light content (and on the solo white backdrop). Inset
    //   the core 2px so the strokes share no pixels (coplanar same-depth
    //   strokes lose to depth-test order and the second one vanishes).
    dl->AddRect(ImVec2(x0 - 1, y0 - 1), ImVec2(x1 + 1, y1 + 1), IM_COL32(10, 10, 12, 255), 0.0f, 0, 4.0f);
    dl->AddRect(ImVec2(x0 + 2, y0 + 2), ImVec2(x1 - 2, y1 - 2), lineCol, 0.0f, 0, 2.0f);
    //opencode: selection wash — translucent fill over the claimed rect so
    //   occluded/empty selections still read (foreground draws above all
    //   canvas layers). Tinted with the hidden state.
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
        hidden ? IM_COL32(255, 210, 90, 40) : IM_COL32(255, 255, 255, 32));
    if (hidden) {
        dl->AddRectFilled(ImVec2(x0, y0 - 20), ImVec2(x0 + 64, y0), IM_COL32(255, 210, 90, 255));
        dl->AddText(ImVec2(x0 + 4, y0 - 19), IM_COL32(20, 20, 20, 255), "hidden");
    }
    //opencode: 8 handles — corners + edge midpoints. The active drag handle
    //   (s_dragMode 2..9, same numbering as tools_resize_handle) fills accent.
    gX_F32 mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    gX_F32v2 hp[8] = {
        {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1},
        {x0, my}, {x1, my}, {mx, y0}, {mx, y1},
    };
    //opencode: dragMode 2..9 -> handle index 0..7 (TL,TR,BL,BR,L,R,T,B)
    for (gX_I32 i = 0; i < 8; i++) {
        gX_F32 hx = hp[i].x, hy = hp[i].y;
        gX_U32 fill = (s_dragMode == i + 2) ? IM_COL32(120, 180, 240, 255) : IM_COL32(40, 45, 60, 255);
        dl->AddRectFilled(ImVec2(hx - 5, hy - 5), ImVec2(hx + 5, hy + 5), fill);
        dl->AddRect(ImVec2(hx - 5, hy - 5), ImVec2(hx + 5, hy + 5), IM_COL32(255, 255, 255, 255));
    }
}

//=============================================================================
// save with overwrite confirm (v0.6.6) — saving back to the opened markup
//   path is expected (no prompt); writing a NEW/existing-other file prompts.
//   Called by the Save button + Ctrl+S.
//=============================================================================
static gX_I0 tools_save_request(gX_I0) {
    if (!s_savePath[0]) return;
    gX_I1 sameAsOpen = (s_markupPath[0] && strcmp(s_savePath, s_markupPath) == 0) ? 1 : 0;
    if (!sameAsOpen) {
        FILE *f = fopen(s_savePath, "rb");
        if (f) { fclose(f); s_confirmOverwrite = 1; return; }
    }
    if (gX_Tools_Editor_SaveSnapshot() == 0) {
        gX_TraceLog(gX_LOG_INFO, "Tools: markup saved '%s'", s_savePath);
    }
}

static gX_I0 tools_draw_modals(gX_I0) {
    if (s_confirmRemove) {
        ImGui::OpenPopup("Confirm remove");
        s_confirmRemove = 0;
    }
    if (ImGui::BeginPopupModal("Confirm remove", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete the selected element? (cannot be undone)");
        if (ImGui::Button("Delete")) {
            gX_I32 selectionIndex = tools_selection_index(gX_GUI_Canvas_FromIndex(s_targetCanvas));
            if (selectionIndex >= 0) {
                gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
                if (canvas) tools_remove_element(selectionIndex);
                tools_selection_clear();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s_confirmOverwrite) {
        ImGui::OpenPopup("Confirm overwrite");
        s_confirmOverwrite = 0;
    }
    if (ImGui::BeginPopupModal("Confirm overwrite", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Overwrite existing file?");
        ImGui::TextDisabled("%s", s_savePath);
        if (ImGui::Button("Overwrite")) {
            if (gX_Tools_Editor_SaveSnapshot() == 0)
                gX_TraceLog(gX_LOG_INFO, "Tools: markup saved '%s'", s_savePath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

//=============================================================================
// entry
//=============================================================================

extern "C" gX_I0 gX_Tools_Editor_Open(gX_I32 canvasIndex, const gX_I8 *markupPath);
extern "C" gX_I0 gX_Tools_Editor_Open(gX_I32 canvasIndex, const gX_I8 *markupPath) {
    s_targetCanvas = canvasIndex;
    tools_selection_clear();
    s_pendingSelect = -1;
    tools_edit_lock_set(s_targetCanvas);
    if (markupPath && markupPath[0]) {
        strncpy(s_markupPath, markupPath, sizeof(s_markupPath) - 1);
        strncpy(s_savePath, markupPath, sizeof(s_savePath) - 1);
    }
    //opencode: headless hooks — GX_TOOLS_SELECT=<idx> pre-selects an element
    //   (capture verification of the property panel), GX_TOOLS_SAVE=1 writes
    //   the snapshot once at open (round-trip verification)
    {
        const char *selEnv = getenv("GX_TOOLS_SELECT");
        if (selEnv && selEnv[0]) s_pendingSelect = atoi(selEnv);
        //opencode: v0.6.6 — GX_TOOLS_FILTER=<text> presets the tree search
        //   (automation/screenshots; same pattern as SELECT/SAVE).
        //opencode: GX_TOOLS_SOLO=1 starts with isolate-selection on
        //   (headless verification of solo rendering).
        const char *soloEnv = getenv("GX_TOOLS_SOLO");
        if (soloEnv && soloEnv[0] == '1') s_soloOn = 1;
        const char *fltEnv = getenv("GX_TOOLS_FILTER");
        if (fltEnv) {
            strncpy(s_treeFilter, fltEnv, sizeof(s_treeFilter) - 1);
            s_treeFilter[sizeof(s_treeFilter) - 1] = '\0';
        }
        //opencode: v0.6.7 — structural automation DEFERRED (canvas is
        //   empty at Open; scene builds it later). Stored here, applied on
        //   the first Draw frame with elements (with SAVE, below).
        const char *addEnv = getenv("GX_TOOLS_ADD");
        if (addEnv && addEnv[0]) s_pendingAdd = atoi(addEnv);
        else s_pendingAdd = -1;
        const char *rmEnv = getenv("GX_TOOLS_REMOVE");
        if (rmEnv && rmEnv[0]) s_pendingRemove = atoi(rmEnv);
        else s_pendingRemove = -1;
        const char *unEnv = getenv("GX_TOOLS_UNDO");
        s_pendingUndo = (unEnv && unEnv[0]) ? atoi(unEnv) : 0;
        const char *reEnv = getenv("GX_TOOLS_REDO");
        s_pendingRedo = (reEnv && reEnv[0]) ? atoi(reEnv) : 0;
        const char *saveEnv = getenv("GX_TOOLS_SAVE");
        if (saveEnv && saveEnv[0]) {
            //opencode: deferred — the markup canvas is still empty at Open
            //   (scene respond builds it later); save on the first Draw frame
            //   that has elements
            strncpy(s_pendingSave, saveEnv, sizeof(s_pendingSave) - 1);
        }
    }
    gX_TraceLog(gX_LOG_INFO, "Tools: GUI editor open — canvas %i, markup '%s'",
        canvasIndex, markupPath ? markupPath : "(none)");
    {
        gX_GUI_Canvas *c0 = gX_GUI_Canvas_FromIndex(s_targetCanvas);
        if (c0) gX_TraceLog(gX_LOG_INFO, "Tools: canvas %i holds %u elements after open hooks", s_targetCanvas, c0->elements.amount);
    }
    //opencode: GX_TOUR=0 suppresses auto-tour (clean headless captures).
    {
        const char *tEnv = getenv("GX_TOUR");
        if ((!tEnv || tEnv[0] != '0') && !settings.edTourSeen) s_tourStep = 0;
    }
}

extern "C" gX_I0 gX_Tools_Editor_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Editor_Draw(gX_I0) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);

    //opencode: deferred headless snapshot — fire once the markup canvas is
    //   built (elements > 0), then clear the request
    if (s_pendingSelect >= 0 && canvas && canvas->elements.amount > 0) {
        tools_selection_set(canvas, s_pendingSelect);
        s_pendingSelect = -1;
    }
    if (s_pendingSave[0] && canvas && canvas->elements.amount > 0) {
        tools_string_copy(s_savePath, sizeof(s_savePath), s_pendingSave);
        if (gX_Tools_Editor_SaveSnapshot() == 0) {
            gX_TraceLog(gX_LOG_INFO, "Tools: headless snapshot saved to '%s'", s_savePath);
        }
        s_pendingSave[0] = '\0';
    }
    //opencode: v0.6.7 — deferred structural hooks (ADD→REMOVE→UNDO→REDO).
    if (canvas && canvas->elements.amount > 0
        && (s_pendingAdd >= 0 || s_pendingRemove >= 0 || s_pendingUndo > 0 || s_pendingRedo > 0)) {
        gX_U32 before = 0, after = 0;
        for (gX_U32 bi = 0; bi < canvas->elements.amount; bi++)
            if (canvas->elements.meta[bi].USED) before++;
        if (s_pendingAdd >= 0) tools_add_element(s_pendingAdd);
        if (s_pendingRemove >= 0 && s_pendingRemove < (gX_I32)canvas->elements.amount) {
            tools_remove_element(s_pendingRemove);
            tools_selection_clear();
        }
        for (gX_I32 ui = 0; ui < s_pendingUndo && ui < 32; ui++) tools_undo_pop(canvas);
        for (gX_I32 ui = 0; ui < s_pendingRedo && ui < 32; ui++) tools_redo_pop(canvas);
        for (gX_U32 bi = 0; bi < canvas->elements.amount; bi++)
            if (canvas->elements.meta[bi].USED) after++;
        gX_TraceLog(gX_LOG_INFO, "Tools: deferred hooks applied (add=%i rm=%i undo=%i redo=%i) used %u -> %u (amount %u)",
            s_pendingAdd, s_pendingRemove, s_pendingUndo, s_pendingRedo, before, after, canvas->elements.amount);
        s_pendingAdd = -1;
        s_pendingRemove = -1;
        s_pendingUndo = 0;
        s_pendingRedo = 0;
    }

    //opencode: undo/redo hotkeys + gesture snapshot capture (unchanged)
    {
        ImGuiIO &io = ImGui::GetIO();
        if (!io.WantTextInput && !io.WantCaptureKeyboard) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                if (io.KeyShift) tools_redo_pop(canvas);
                else tools_undo_pop(canvas);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) tools_redo_pop(canvas);
        }
        if (ImGui::IsAnyItemActive()) {
            if (!s_gestureCaptured) { tools_undo_push(canvas); s_gestureCaptured = 1; }
        } else {
            s_gestureCaptured = 0;
        }
        //opencode: v0.6.6 — shortcuts (skipped while typing in a text field
        //   so Delete/arrows/Ctrl+Z keep editing text).
        if (canvas && !io.WantTextInput && !io.WantCaptureKeyboard) {
            gX_I32 selectionIndex = tools_selection_index(canvas);
            gX_I1 hasSel = selectionIndex >= 0;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { tools_selection_clear(); s_dragMode = 0; }
            selectionIndex = tools_selection_index(canvas);
            hasSel = selectionIndex >= 0;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && hasSel) s_confirmRemove = 1;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && hasSel) {
                tools_duplicate_element(selectionIndex);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                tools_save_request();
            }
            if (hasSel) {
                gX_F32 step = io.KeyShift ? 8.0f : 1.0f;
                gX_I1 moved = 0;
                gX_GUI_Element *el = tools_selection_resolve(canvas);
                if (el) {
                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { el->rect.x -= step; moved = 1; }
                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { el->rect.x += step; moved = 1; }
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { el->rect.y -= step; moved = 1; }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { el->rect.y += step; moved = 1; }
                    if (moved) {
                        if (s_snapOn) {
                            el->rect.x = tools_grid(el->rect.x);
                            el->rect.y = tools_grid(el->rect.y);
                        }
                        tools_undo_push(canvas);
                        canvas->refresh = true;
                        gui.refresh = true;
                    }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F1)) s_helpOpen = !s_helpOpen;
        }
    }

    //opencode: v0.6.3 — studio layout. Fixed side panels leave the canvas
    //   viewport (fullscreen behind) clear in the center. FirstUseEver so a
    //   user arrangement persists via imgui.ini; headless has none.
    //opencode: v0.6.9 — docked geometry uses synchronous maindata window
    //   dims (backend DisplaySize flaps across resize events, jumping panels).
    //   Overlay keeps live DisplaySize (window static there).
    gX_F32 scrW, scrH;
    if (gui.viewportDocked) {
        scrW = (gX_F32)maindata.windowWidth;
        scrH = (gX_F32)maindata.windowHeight;
    } else {
        scrW = ImGui::GetIO().DisplaySize.x;
        scrH = ImGui::GetIO().DisplaySize.y;
    }
    if (scrW <= 0) scrW = (gX_F32)maindata.screenWidth;
    if (scrH <= 0) scrH = (gX_F32)maindata.screenHeight;
    //opencode: v0.6.9 Phase B — dock geometry (slim panels in margins when
    //   docked; overlay sizes otherwise). Margins from live viewport.
    gX_F32 dockL = 0, dockR = 0;
    gX_I1 docked = gui.viewportDocked;
    if (docked) {
        dockL = gui.viewportBox.x;
        dockR = scrW - (gui.viewportBox.x + gui.viewportBox.width);
        if (dockL < 160) dockL = 160;
        if (dockR < 160) dockR = 160;
    }
    gX_F32 leftW = docked ? dockL : 300.0f;
    gX_F32 rightW = docked ? dockR : 340.0f;
    //opencode: docked panels pin every frame (a mid-run widen races
    //   FirstUseEver, misplacing them); overlay stays user-movable.
    ImGuiCond dockCond = docked ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    //opencode: dock hardening — on the docked->overlay falling edge (user
    //   shrank under margins) snap panels into view once; without this
    //   they keep stale pinned coords off-window (FirstUseEver won't move
    //   an existing window). Clears after two frames (then user-movable).
    static gX_I1 s_prevDocked = 0;
    static gX_I32 s_snapFrames = 0;
    if (s_prevDocked && !docked) s_snapFrames = 2;
    s_prevDocked = docked ? 1 : 0;
    if (s_snapFrames > 0) {
        dockCond = ImGuiCond_Always;
        s_snapFrames--;
    }

    //opencode: menu bar — File (save/reload), Edit (undo/redo), View
    //   (snap/overlay), Canvas (retarget). Deterministic, always on top.
    if (ImGui::BeginMainMenuBar()) {
        //opencode: keep accessibility preferences available in the markup editor.
        gX_Tools_ImGui_AccessibilityMenu();
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save markup") && canvas) {
                if (gX_Tools_Editor_SaveSnapshot() == 0)
                    gX_TraceLog(gX_LOG_INFO, "Tools: markup saved '%s'", s_savePath);
            }
            if (ImGui::MenuItem("Reload canvas") && canvas) {
                canvas->refresh = true;
                gui.refresh = true;
            }
            if (ImGui::MenuItem("Close editor")) {
                *gX_Tools_RuntimeEditorFlag() = 0;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) tools_undo_pop(canvas);
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) tools_redo_pop(canvas);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool sn = s_snapOn != 0;
            if (ImGui::Checkbox("Snap 8px", &sn)) s_snapOn = sn ? 1 : 0;
            bool gr = s_gridOn != 0;
            if (ImGui::Checkbox("Grid 8px", &gr)) s_gridOn = gr ? 1 : 0;
            bool ov = s_overlayOn != 0;
            if (ImGui::Checkbox("Selection overlay", &ov)) s_overlayOn = ov ? 1 : 0;
            bool so = s_soloOn != 0;
            if (ImGui::Checkbox("Isolate selection", &so)) {
                s_soloOn = so ? 1 : 0;
                if (!s_soloOn) tools_solo_restore(gX_GUI_Canvas_FromIndex(s_targetCanvas));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Canvas")) {
            ImGui::InputInt("target", &s_targetCanvas);
            canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
            if (canvas) ImGui::TextDisabled("%u elements", canvas->elements.amount);
            else ImGui::TextDisabled("(missing)");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            bool hlp = s_helpOpen != 0;
            if (ImGui::Checkbox("Editor guide (F1)", &hlp)) s_helpOpen = hlp ? 1 : 0;
            if (ImGui::MenuItem("Restart tour")) s_tourStep = 0;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    gX_F32 menuH = ImGui::GetFrameHeight();

    //opencode: LEFT — element tree + add/remove/duplicate + save path
    ImGui::SetNextWindowPos(ImVec2(0, menuH), dockCond);
    ImGui::SetNextWindowSize(ImVec2(leftW, scrH - menuH - 28), dockCond);
    ImGui::PushItemWidth(-FLT_MIN);
    bool edOpen = (*gX_Tools_RuntimeEditorFlag()) != 0;
    if (ImGui::Begin("Editor - Elements", &edOpen)) {
        //opencode: hint-inside (slim dock clipped the "search" label).
        ImGui::InputTextWithHint("##elfilter", "search elements", s_treeFilter, sizeof(s_treeFilter));
        //opencode: v0.6.7 — canvas tabs (switch target without the menu).
        //opencode: existing canvases only (missing slots hidden; keeps
        //   the row inside the 300px panel).
        if (docked) {
            //opencode: compact canvas switch (fits slim dock).
            gX_I8 ccLbl[64];
            snprintf(ccLbl, sizeof(ccLbl), "canvas %i", s_targetCanvas);
            if (ImGui::BeginCombo("canvas", ccLbl)) {
                for (gX_I32 ci = 0; ci < 8; ci++) {
                    gX_GUI_Canvas *cc = gX_GUI_Canvas_FromIndex(ci);
                    if (!cc) continue;
                    gX_U32 used = 0;
                    for (gX_U32 bi = 0; bi < cc->elements.amount; bi++)
                        if (cc->elements.meta[bi].USED) used++;
                    snprintf(ccLbl, sizeof(ccLbl), "%i (%u els)", ci, used);
                    if (ImGui::Selectable(ccLbl, ci == s_targetCanvas)) {
                        s_targetCanvas = ci;
                        tools_selection_clear();
                        canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
                        tools_edit_lock_set(s_targetCanvas);
                    }
                }
                ImGui::EndCombo();
            }
        } else {
                gX_I1 firstTab = 1;
        for (gX_I32 ci = 0; ci < 8; ci++) {
            gX_GUI_Canvas *cc = gX_GUI_Canvas_FromIndex(ci);
            if (!cc) continue;
            gX_U32 used = 0;
            for (gX_U32 bi = 0; bi < cc->elements.amount; bi++)
                if (cc->elements.meta[bi].USED) used++;
            gX_I8 tabLbl[32];
            snprintf(tabLbl, sizeof(tabLbl), "%i (%u)", ci, used);
            if (!firstTab) ImGui::SameLine();
            firstTab = 0;
            if (ImGui::SmallButton(tabLbl)) {
                s_targetCanvas = ci;
                tools_selection_clear();
                canvas = gX_GUI_Canvas_FromIndex(s_targetCanvas);
                tools_edit_lock_set(s_targetCanvas);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Target canvas %i%s", ci, cc ? "" : " (missing)");
        }
            //opencode: tabs carry no selected highlight — keep the one place
            //   that names the target (docked mode shows it in the combo).
            ImGui::Text("Target canvas: %i", s_targetCanvas);
        } // !docked (tabs row)
        //opencode: the canvas combo above already shows the target — the
        //   duplicate line only restated it (editor audit).
        ImGui::Text("Markup: %s", s_markupPath[0] ? s_markupPath : "(none)");
        if (canvas) {
            ImGui::Text("Elements: %u", canvas->elements.amount);
            tools_panel_toolbar(canvas);
            tools_panel_tree(canvas);
            tools_panel_actions();
        }
    }
    ImGui::End();
    ImGui::PopItemWidth();

    //opencode: RIGHT — selection properties
    ImGui::SetNextWindowPos(ImVec2(scrW - rightW, menuH), dockCond);
    ImGui::SetNextWindowSize(ImVec2(rightW, scrH - menuH - 28), dockCond);
    ImGui::PushItemWidth(-FLT_MIN);
    if (ImGui::Begin("Editor - Properties", &edOpen)) {
        if (canvas) tools_panel_properties(canvas);
        else ImGui::TextDisabled("no canvas");
    }
    ImGui::End();
    ImGui::PopItemWidth();

    if (!edOpen) {
        *gX_Tools_RuntimeEditorFlag() = 0;
        tools_edit_lock_clear();
        tools_solo_restore(gX_GUI_Canvas_FromIndex(s_targetCanvas));
    }
    //opencode: BOTTOM — status bar (pinned thin chrome)
    ImGui::SetNextWindowPos(ImVec2(0, scrH - 28), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(scrW, 28), ImGuiCond_Always);
    if (ImGui::Begin("##editor_status", 0,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        const gX_I8 *selName = "";
        gX_F32 sx = 0, sy = 0, sw = 0, sh = 0;
        gX_I32 selectionIndex = tools_selection_index(canvas);
        gX_GUI_Element *el = tools_selection_resolve(canvas);
        if (el) {
            selName = el->name[0] ? el->name : "(unnamed)";
            sx = el->rect.x; sy = el->rect.y; sw = el->rect.width; sh = el->rect.height;
        }
        //opencode: state left, hint right-dim — the single mixed pipe-string
        //   read as one cryptic blob (editor audit).
        ImGui::Text("canvas %i | %u el | sel #%i %s @ (%.0f,%.0f) %.0fx%.0f | snap %s grid %s",
            s_targetCanvas, canvas ? canvas->elements.amount : 0, selectionIndex, selName, sx, sy, sw, sh,
            s_snapOn ? "on" : "off", s_gridOn ? "on" : "off");
        ImGui::SameLine();
        {
            const gX_I8 *hint = "click select - drag move - handles resize - Ctrl+Z undo";
            gX_F32 hw = ImGui::CalcTextSize(hint).x + ImGui::GetStyle().ItemSpacing.x;
            gX_F32 avail = ImGui::GetContentRegionAvail().x;
            if (hw < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - hw);
            ImGui::TextDisabled("%s", hint);
        }
    }
    ImGui::End();

    //opencode: direct manipulation + selection overlay (viewport interaction)
    //opencode: v0.6.6 — first-run guide (F1 toggles; open by default).
    if (s_helpOpen) {
        ImGui::SetNextWindowPos(ImVec2(scrW * 0.5f - 230, scrH * 0.5f - 180), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(460, 360), ImGuiCond_FirstUseEver);
        bool hlp = true;
        if (ImGui::Begin("Editor guide", &hlp)) {
            ImGui::TextWrapped("1. Pick an element in the left list (or click it on canvas).");
            ImGui::TextWrapped("2. Drag to move (8px snap, View menu toggles). Grab the white squares to resize.");
            ImGui::TextWrapped("3. Recolor + tweak on the right. Sliders type numbers; colors open the picker.");
            ImGui::TextWrapped("4. Add / Duplicate / Remove below the list. Remove asks first; undo restores.");
            ImGui::TextWrapped("5. Save markup writes the file (overwrite asks unless it is the opened file).");
            ImGui::SeparatorText("keys");
            ImGui::Text("arrows nudge (Shift = x8) - Del remove - Ctrl+D duplicate");
            ImGui::Text("Ctrl+Z / Ctrl+Y undo value edits - Ctrl+S save - Esc deselect - F1 guide");
            ImGui::SeparatorText("limits");
            ImGui::TextWrapped("Undo covers moves, edits, add/remove. Canvas edits mark it dirty; Save persists.");
        }
        ImGui::End();
        if (!hlp) s_helpOpen = 0;
    }
    //opencode: v0.6.6 — guided tour (first run). Non-modal centered
    //   card + accent highlight on the relevant panel/viewport.
    if (s_tourStep >= 0 && s_tourStep <= 4) {
        const gX_I8 *titles[5] = { "1/5 Pick", "2/5 Move", "3/5 Tweak", "4/5 Keep", "5/5 Go" };
        const gX_I8 *bodies[5] = {
            "Pick an element in the LEFT list (or click it on canvas). Search filters long lists.",
            "Drag it to move (8px snap, View toggles). White squares resize. Arrows nudge, Del removes (asks first).",
            "Recolor + tweak on the RIGHT. Hover any label for plain help. Sliders type numbers.",
            "Add / Duplicate / Remove live under the list. Ctrl+S saves the markup. Ctrl+Z undoes edits (not add/remove).",
            "F1 reopens the full guide anytime. Have fun — Esc deselects, X closes a tool.",
        };
        //opencode: highlight target per step (default studio rects; tour
        //   runs pre-drag so FirstUseEver positions hold).
        {
            ImDrawList *tdl = ImGui::GetForegroundDrawList();
            gX_F32 mH = ImGui::GetFrameHeight();
            ImVec4 hl = ImVec4(0, 0, 0, 0);
            if (s_tourStep == 0) hl = ImVec4(0, mH, 300, scrH - 28);
            else if (s_tourStep == 1) hl = ImVec4(300, mH, scrW - 340, scrH - 28);
            else if (s_tourStep == 2) hl = ImVec4(scrW - 340, mH, scrW, scrH - 28);
            else if (s_tourStep == 3) hl = ImVec4(0, scrH - 340, 300, scrH - 28);
            if (hl.z > hl.x)
                tdl->AddRect(ImVec2(hl.x, hl.y), ImVec2(hl.z, hl.w), IM_COL32(120, 180, 240, 255), 0.0f, 0, 3.0f);
        }
        ImGui::SetNextWindowPos(ImVec2(scrW * 0.5f - 210, scrH * 0.5f - 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowFocus();
        bool tourOpen = true;
        if (ImGui::Begin("Editor tour", &tourOpen)) {
            ImGui::Text("%s", titles[s_tourStep]);
            ImGui::TextWrapped("%s", bodies[s_tourStep]);
            if (s_tourStep > 0 && ImGui::Button("Back")) s_tourStep--;
            ImGui::SameLine();
            if (s_tourStep < 4 && ImGui::Button("Next")) s_tourStep++;
            ImGui::SameLine();
            if (ImGui::Button(s_tourStep == 4 ? "Finish" : "Skip")) {
                s_tourStep = -1;
                settings.edTourSeen = 1;
                settings.dirtyFlag = 1;
            }
        }
        ImGui::End();
        if (!tourOpen) {
            s_tourStep = -1;
            settings.edTourSeen = 1;
            settings.dirtyFlag = 1;
        }
    }
    tools_draw_modals();
    if (canvas) {
        tools_canvas_drag(canvas);
        tools_solo_apply(canvas);
        tools_draw_overlay(canvas);
    }
}
