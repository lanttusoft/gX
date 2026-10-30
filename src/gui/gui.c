/**********************************************************************************************
*
*   [gX: src/gui/gui.c]
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


#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "gui.h"
#include "canvas.h"
#include "css.h"
#include "style.h"
#include "markup.h"
#include "widgetsdf.h"
#include "../ui/layout_debug.h"
#include "../module/module.h"
#include "element/element.h"

#include "element/struct.h"

#include "../main.h"
#include "../input.h"

#include "../common/heap.h"
#include "../common/log.h"
#include "../common/msg.h"
#include "../common/profiler.h"
#include "../common/macros.h"
#include "../common/utils.h"

#include "../gxmath/gxmath.h"

#if gX_ENABLE_SCENE == 1
    #include "../scene/scene.h"
#endif

#include "../wrappers/wrappers.h"

#if gX_ENABLE_RENDERER == 1
    #include "../renderer/renderer.h"
#endif

//opencode: RHI for FBO blit operations (RT-to-RT copy, no Y-flip issues)
//opencode: drawinfo for viewport rect source
#include "../wrappers/drawing.h"
#include "../rhi/gl_state.h"
#include "../rhi/rhi_state.h"
#include "../rhi/rhi_fbo.h"

#include "templates/template_registry.h"
#include "templates/element_template_registry.h"
#include "../rhi/rhi.h"
#include <string.h>

gX_GUI gui;

//opencode: interaction foundation — all focus mutation goes through these
//   helpers so state bits and the public focus IDs cannot diverge.
gX_I1 gX_GUI_Canvas_InputAllowed(gX_I32 canvasID) {
    if (canvasID < 0 || (gX_U32)canvasID >= gui.canvases.amount
        || !gui.canvases.data || !gui.canvases.meta
        || !gui.canvases.meta[canvasID].USED) return false;
    if (gui.modalCanvasID >= 0 && gui.modalCanvasID != canvasID) return false;
    return true;
}

gX_I0 gX_GUI_Focus_Clear(gX_I0) {
    if (gX_GUI_Trace_Enabled() && gui.focusCanvasID >= 0)
        gX_GUI_Trace_Event("focus-clear", gui.focusCanvasID,
            gui.focusElementID, NULL);
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas*)gui.canvases.data;
    if (canvases) {
        for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
            gX_GUI_Element *elements = (gX_GUI_Element*)canvases[ci].elements.data;
            if (!elements) continue;
            for (gX_U32 ei = 0; ei < canvases[ci].elements.amount; ei++) {
                elements[ei].state.FOCUSED = false;
            }
        }
    }
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
}

gX_I1 gX_GUI_Focus_Set(gX_I32 canvasID, gX_I32 elementID) {
    if (!gX_GUI_Canvas_InputAllowed(canvasID)) return false;
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasID);
    if (!canvas || elementID < 0 || (gX_U32)elementID >= canvas->elements.amount) return false;
    if (canvas->editLock || !gX_GUI_Canvas_Element_Eligible(canvas, (gX_U32)elementID)) return false;
    gX_GUI_Element *element = &((gX_GUI_Element*)canvas->elements.data)[elementID];
    if (!element->state.FOCUSABLE) return false;
    gX_GUI_Focus_Clear();
    gui.focusCanvasID = canvasID;
    gui.focusElementID = elementID;
    element->state.FOCUSED = true;
    gX_GUI_Trace_Event("focus", canvasID, elementID, NULL);
    return true;
}

gX_I1 gX_GUI_Focus_First(gX_I0) {
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas*)gui.canvases.data;
    if (!canvases) return false;
    for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
        if (!gX_GUI_Canvas_InputAllowed((gX_I32)ci)) continue;
        gX_GUI_Element *elements = (gX_GUI_Element*)canvases[ci].elements.data;
        if (!elements) continue;
        for (gX_U32 ei = 0; ei < canvases[ci].elements.amount; ei++) {
            if (elements[ei].state.FOCUSABLE
                && gX_GUI_Canvas_Element_Eligible(&canvases[ci], ei)) {
                return gX_GUI_Focus_Set((gX_I32)ci, (gX_I32)ei);
            }
        }
    }
    return false;
}

gX_I32 gX_GUI_Modal_Open(gX_I32 canvasID, gX_I1 dismissOnOutside) {
    if (canvasID < 0 || (gX_U32)canvasID >= gui.canvases.amount
        || !gui.canvases.meta || !gui.canvases.meta[canvasID].USED) return -1;
    if (gui.modalCanvasID == canvasID) return 0;
    if (gui.modalCanvasID < 0) {
        gui.modalPreviousFocusCanvasID = gui.focusCanvasID;
        gui.modalPreviousFocusElementID = gui.focusElementID;
    } else {
        //opencode: nested modal (dialog over menu) — push the superseded
        //   modal plus its inner focus; the outer previous-focus stays put
        //   so the bottom close still restores the background.
        if (gui.modalDepth >= GX_GUI_MODAL_STACK) return -1;
        gui.modalStack[gui.modalDepth].canvasID = gui.modalCanvasID;
        gui.modalStack[gui.modalDepth].dismissOnOutside = gui.modalDismissOnOutside;
        gui.modalStack[gui.modalDepth].returnFocusCanvasID = gui.focusCanvasID;
        gui.modalStack[gui.modalDepth].returnFocusElementID = gui.focusElementID;
        gui.modalDepth++;
    }
    gui.modalCanvasID = canvasID;
    gui.modalDismissOnOutside = dismissOnOutside;
    gui.focusFirstPending = true;
    gX_GUI_PointerCapture_Clear();
    gX_GUI_Focus_Clear();
    gX_GUI_Trace_Event("modal-open", canvasID, (gX_I32)gui.modalDepth, NULL);
    return 0;
}

gX_I0 gX_GUI_Modal_Close(gX_I0) {
    gX_I32 previousCanvas = gui.modalPreviousFocusCanvasID;
    gX_I32 previousElement = gui.modalPreviousFocusElementID;
    gX_GUI_PointerCapture_Clear();
    gX_GUI_Focus_Clear();
    if (gui.modalDepth > 0) {
        //opencode: pop back to the superseded modal and its inner focus.
        gX_U32 stackIndex = --gui.modalDepth;
        gui.modalCanvasID = gui.modalStack[stackIndex].canvasID;
        gui.modalDismissOnOutside = gui.modalStack[stackIndex].dismissOnOutside;
        previousCanvas = gui.modalStack[stackIndex].returnFocusCanvasID;
        previousElement = gui.modalStack[stackIndex].returnFocusElementID;
        memset(&gui.modalStack[stackIndex], 0, sizeof(gui.modalStack[stackIndex]));
        gui.focusFirstPending = false;
        if (previousCanvas < 0 || previousElement < 0
            || !gX_GUI_Focus_Set(previousCanvas, previousElement)) {
            gX_GUI_Focus_First();
        }
        gX_GUI_Trace_Event("modal-close", gui.modalCanvasID,
            (gX_I32)gui.modalDepth, NULL);
        return;
    }
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    if (previousCanvas < 0 || previousElement < 0
        || !gX_GUI_Focus_Set(previousCanvas, previousElement))
        gX_GUI_Focus_First();
    gX_GUI_Trace_Event("modal-close", -1, 0, NULL);
}

gX_I1 gX_GUI_Modal_IsOpen(gX_I0) {
    return gui.modalCanvasID >= 0;
}

//opencode: modal dialog — one confirm/alert on the dialog canvas (index 8).
//   Copies labels, holds callbacks, rebuilds the canvas, opens it as a
//   non-dismiss modal (backdrop clicks are swallowed, never pass through).
gX_I32 gX_GUI_Dialog_Open(const gX_I8 *title, const gX_I8 *message,
    const gX_I8 *okLabel, const gX_I8 *cancelLabel,
    gX_I0 (*onOk)(gX_I0 *userData), gX_I0 *onOkData,
    gX_I0 (*onCancel)(gX_I0 *userData), gX_I0 *onCancelData) {
    if (gui.dialogOpen) return -1;
    if (gui.dialogCanvasIndex < 0
        || (gX_U32)gui.dialogCanvasIndex >= gui.canvases.amount) return -1;
    strncpy(gui.dialogTitle, title ? title : "Confirm",
        sizeof(gui.dialogTitle) - 1);
    gui.dialogTitle[sizeof(gui.dialogTitle) - 1] = '\0';
    strncpy(gui.dialogMessage, message ? message : "",
        sizeof(gui.dialogMessage) - 1);
    gui.dialogMessage[sizeof(gui.dialogMessage) - 1] = '\0';
    strncpy(gui.dialogOkLabel, okLabel ? okLabel : "OK",
        sizeof(gui.dialogOkLabel) - 1);
    gui.dialogOkLabel[sizeof(gui.dialogOkLabel) - 1] = '\0';
    if (cancelLabel && cancelLabel[0]) {
        strncpy(gui.dialogCancelLabel, cancelLabel,
            sizeof(gui.dialogCancelLabel) - 1);
        gui.dialogCancelLabel[sizeof(gui.dialogCancelLabel) - 1] = '\0';
        gui.dialogHasCancel = true;
    } else {
        gui.dialogCancelLabel[0] = '\0';
        gui.dialogHasCancel = false;
    }
    gui.dialogOnOk = onOk;
    gui.dialogOnOkData = onOkData;
    gui.dialogOnCancel = onCancel;
    gui.dialogOnCancelData = onCancelData;
    gui.dialogOpen = true;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_DIALOG, gui.dialogCanvasIndex);
    {
        gX_GUI_Canvas *dc = gX_GUI_Canvas_FromIndex(gui.dialogCanvasIndex);
        if (dc) {
            gui.canvases.meta[gui.dialogCanvasIndex].UPDATE = true;
            gui.canvases.meta[gui.dialogCanvasIndex].DRAW = true;
        }
    }
    if (gX_GUI_Modal_Open(gui.dialogCanvasIndex, false) != 0) {
        gui.dialogOpen = false;
        return -1;
    }
    gX_GUI_Trace_Event("dialog-open", gui.dialogCanvasIndex, -1, gui.dialogTitle);
    return 0;
}

gX_I0 gX_GUI_Dialog_Close(gX_I1 confirmed) {
    gX_I0 (*cb)(gX_I0 *userData) = NULL;
    gX_I0 *cbData = NULL;
    if (!gui.dialogOpen) return;
    gX_GUI_Trace_Event(confirmed ? "dialog-ok" : "dialog-cancel",
        gui.dialogCanvasIndex, -1, gui.dialogTitle);
    if (confirmed) {
        cb = gui.dialogOnOk;
        cbData = gui.dialogOnOkData;
    } else {
        cb = gui.dialogOnCancel;
        cbData = gui.dialogOnCancelData;
    }
    gui.dialogOpen = false;
    gui.dialogOnOk = NULL;
    gui.dialogOnOkData = NULL;
    gui.dialogOnCancel = NULL;
    gui.dialogOnCancelData = NULL;
    gui.dialogHasCancel = false;
    if (gui.modalCanvasID == gui.dialogCanvasIndex) gX_GUI_Modal_Close();
    {
        gX_GUI_Canvas *dc = gX_GUI_Canvas_FromIndex(gui.dialogCanvasIndex);
        if (dc) {
            gX_GUI_Canvas_Clear(dc);
            gui.canvases.meta[gui.dialogCanvasIndex].UPDATE = false;
            gui.canvases.meta[gui.dialogCanvasIndex].DRAW = false;
        }
    }
    if (cb) cb(cbData);
}

gX_I1 gX_GUI_Dialog_IsOpen(gX_I0) {
    return gui.dialogOpen;
}

gX_I0 gX_GUI_PointerCapture_Clear(gX_I0) {
    gui.pointerCaptureActive = false;
    gui.pointerCaptureCanvasID = -1;
    gui.pointerCaptureElementID = -1;
}

gX_I1 gX_GUI_PointerCapture_Begin(gX_I32 canvasID, gX_I32 elementID) {
    if (!gX_GUI_Canvas_InputAllowed(canvasID)) return false;
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasID);
    if (!canvas || elementID < 0 || (gX_U32)elementID >= canvas->elements.amount) return false;
    if (!gX_GUI_Canvas_Element_Eligible(canvas, (gX_U32)elementID)) return false;
    gX_GUI_Element *element = &((gX_GUI_Element*)canvas->elements.data)[elementID];
    if (!element->state.DRAGGABLE_LEFT) return false;
    gui.pointerCaptureActive = true;
    gui.pointerCaptureCanvasID = canvasID;
    gui.pointerCaptureElementID = elementID;
    return true;
}

gX_I1 gX_GUI_PointerCapture_IsOwner(const gX_GUI_Canvas *canvas,
    const gX_GUI_Element *element) {
    if (!gui.pointerCaptureActive || !canvas || !element) return false;
    if (gui.pointerCaptureCanvasID < 0
        || (gX_U32)gui.pointerCaptureCanvasID >= gui.canvases.amount) return false;
    gX_GUI_Canvas *owner = &((gX_GUI_Canvas*)gui.canvases.data)[gui.pointerCaptureCanvasID];
    if (owner != canvas || gui.pointerCaptureElementID < 0
        || (gX_U32)gui.pointerCaptureElementID >= canvas->elements.amount) return false;
    return element == &((gX_GUI_Element*)canvas->elements.data)[gui.pointerCaptureElementID];
}

#if GX_GUI_USE_POOLS
//opencode: pre-allocated buffers for pool-based element/module system
static gX_PoolCanvas  g_poolCanvasBuf[GX_POOL_CAP_CANVAS];
static gX_PoolElement  g_poolElementBuf[GX_POOL_CAP_ELEMENTS];
static gX_PoolText     g_poolTextBuf[GX_POOL_CAP_TEXT];
static gX_PoolShader   g_poolShaderBuf[GX_POOL_CAP_SHADER];
static gX_PoolTexture  g_poolTextureBuf[GX_POOL_CAP_TEXTURE];
static gX_PoolModel    g_poolModelBuf[GX_POOL_CAP_MODEL];
static gX_PoolShape    g_poolShapeBuf[GX_POOL_CAP_SHAPE];
static gX_PoolAnim     g_poolAnimBuf[GX_POOL_CAP_ANIM];
static gX_PoolData     g_poolDataBuf[GX_POOL_CAP_DATA];
static gX_PoolSound    g_poolSoundBuf[GX_POOL_CAP_SOUND];
static gX_U16 g_poolCanvasGen[GX_POOL_CAP_CANVAS];
static gX_U16 g_poolElementGen[GX_POOL_CAP_ELEMENTS];
static gX_U16 g_poolTextGen[GX_POOL_CAP_TEXT];
static gX_U16 g_poolShaderGen[GX_POOL_CAP_SHADER];
static gX_U16 g_poolTextureGen[GX_POOL_CAP_TEXTURE];
static gX_U16 g_poolModelGen[GX_POOL_CAP_MODEL];
static gX_U16 g_poolShapeGen[GX_POOL_CAP_SHAPE];
static gX_U16 g_poolAnimGen[GX_POOL_CAP_ANIM];
static gX_U16 g_poolDataGen[GX_POOL_CAP_DATA];
static gX_U16 g_poolSoundGen[GX_POOL_CAP_SOUND];
#endif

//opencode: buffer pool — find a cached RT with matching dimensions, or return an unloaded wrapper
gX_RenderTexture_Wrapper gX_GUI_BufferPool_Fetch(gX_U32 width, gX_U32 height, gX_I1 hdr) {
    for (gX_U32 i = 0; i < GX_GUI_BUFFER_POOL_MAX; i++) {
        if (gui.bufferPool[i].used &&
            gui.bufferPool[i].width == width &&
            gui.bufferPool[i].height == height &&
            gui.bufferPool[i].hdr == hdr) {
            gX_RenderTexture_Wrapper result = gui.bufferPool[i].rt;
            gui.bufferPool[i].used = false;
            gui.bufferPool[i].rt = (gX_RenderTexture_Wrapper){0};
            return result;
        }
    }
    return (gX_RenderTexture_Wrapper){0};
}

//opencode: buffer pool — stash an RT for future reuse, or unload if pool is full
gX_I0 gX_GUI_BufferPool_Stash(gX_RenderTexture_Wrapper rt, gX_U32 width, gX_U32 height, gX_I1 hdr) {
    for (gX_U32 i = 0; i < GX_GUI_BUFFER_POOL_MAX; i++) {
        if (!gui.bufferPool[i].used) {
            gui.bufferPool[i].rt = rt;
            gui.bufferPool[i].width = width;
            gui.bufferPool[i].height = height;
            gui.bufferPool[i].hdr = hdr;
            gui.bufferPool[i].used = true;
            return;
        }
    }
    // Pool full — actually unload the raw texture
    if (rt.loadingState.loaded) gX_RenderTexture_Unload(rt.rt);
}

//opencode: buffer pool — unload all cached RTs and clear pool
gX_I0 gX_GUI_BufferPool_Flush(gX_I0) {
    for (gX_U32 i = 0; i < GX_GUI_BUFFER_POOL_MAX; i++) {
        if (gui.bufferPool[i].used) {
                if (gui.bufferPool[i].rt.loadingState.loaded)
                    gX_RenderTexture_Unload(gui.bufferPool[i].rt.rt);
                gui.bufferPool[i].rt.loadingState.loaded = false;
            gui.bufferPool[i].used = false;
        }
    }
}

//opencode: GUI event log — ring buffer, thread-safe enough for single-threaded use
gX_I0 gX_GUI_EventLog_Add(gX_I32 canvasID, gX_I32 elementID, gX_I8 type, const gX_I8 *detail) {
    gX_U32 idx = gui.eventHead % GX_GUI_EVENT_LOG_MAX;
    gui.eventLog[idx].time = maindata.timeElapsed;
    gui.eventLog[idx].canvasID = canvasID;
    gui.eventLog[idx].elementID = elementID;
    gui.eventLog[idx].type = type;
    if (detail) {
        strncpy(gui.eventLog[idx].detail, detail, sizeof(gui.eventLog[idx].detail) - 1);
        gui.eventLog[idx].detail[sizeof(gui.eventLog[idx].detail) - 1] = '\0';
    } else {
        gui.eventLog[idx].detail[0] = '\0';
    }
    gui.eventHead++;
    gui.eventCount++;
}

gX_I0 gX_GUI_EventLog_Dump(gX_I32 count) {
    gX_U32 total = (gui.eventCount < (gX_U32)count) ? gui.eventCount : (gX_U32)count;
    if (total == 0) {
        gX_TraceLog(gX_LOG_NOTICE, "[GUI_EVENT] (no events)");
        return;
    }
    gX_U32 start = (gui.eventHead >= total) ? gui.eventHead - total : 0;
    for (gX_U32 i = start; i < gui.eventHead; i++) {
        gX_U32 idx = i % GX_GUI_EVENT_LOG_MAX;
        gX_TraceLog(gX_LOG_NOTICE, "[GUI_EVENT] t=%.3f canvas=%d elem=%d type=%c detail=\"%s\"",
            gui.eventLog[idx].time, gui.eventLog[idx].canvasID,
            gui.eventLog[idx].elementID, gui.eventLog[idx].type,
            gui.eventLog[idx].detail);
    }
}

//opencode: GUI audit trace — GX_GUI_TRACE=1 readout for scripted gates.
static gX_I1 s_guiTraceEnabled = false;
static gX_I1 s_guiTraceResolved = false;

gX_I1 gX_GUI_Trace_Enabled(gX_I0) {
    if (!s_guiTraceResolved) {
        s_guiTraceResolved = true;
        const gX_I8 *trace = getenv("GX_GUI_TRACE");
        s_guiTraceEnabled = trace && trace[0] && trace[0] != '0';
    }
    return s_guiTraceEnabled;
}

gX_I0 gX_GUI_Trace_Event(const gX_I8 *type, gX_I32 canvasID, gX_I32 elementID,
    const gX_I8 *detail) {
    if (!gX_GUI_Trace_Enabled()) return;
    gX_TraceLog(gX_LOG_INFO, "GUITRACE: %s canvas=%d element=%d detail=\"%.63s\"",
        type ? type : "?", canvasID, elementID, detail ? detail : "");
}

//opencode: element map after a canvas build — the interaction gates resolve
//   click targets (label -> rect) from this instead of hardcoding layout math.
gX_I0 gX_GUI_Trace_Canvas(gX_I32 canvasIndex) {
    if (!gX_GUI_Trace_Enabled()) return;
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasIndex);
    if (!canvas) return;
    gX_GUI_Element *elements = (gX_GUI_Element*)canvas->elements.data;
    gX_TraceLog(gX_LOG_INFO,
        "GUITRACE: canvas-build index=%d template=%d name=\"%.63s\" elements=%u",
        canvasIndex, canvas->templateID, canvas->name, canvas->elements.amount);
    if (!elements) return;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        if (canvas->elements.meta && !canvas->elements.meta[i].USED) continue;
        gX_GUI_Element *e = &elements[i];
        gX_I8 text[64] = {0};
        if (e->texts.amount > 0 && e->texts.data) {
            gX_GUI_Element_Text *t = (gX_GUI_Element_Text*)e->texts.data;
            snprintf(text, sizeof(text), "%.63s", t->text);
        }
        //opencode: layout verdict per element — the layout gate fails on
        //   overflow/truncation unless the finding is an allowlisted
        //   intentional deviation.
        gX_F32 fitW = 0.0f, fitH = 0.0f, shrink = 1.0f;
        gX_I32 wraps = 0, truncated = 0, overflow = 0;
        if (text[0]) {
            gX_GUI_Element_Text_Layout layout = gX_GUI_Element_Text_Analyze(e, 0);
            fitW = layout.measure.x;
            fitH = layout.measure.y;
            shrink = layout.shrinkScale;
            wraps = layout.wraps ? 1 : 0;
            truncated = layout.truncated ? 1 : 0;
            overflow = (layout.overflowX || layout.overflowY) ? 1 : 0;
        }
        gX_TraceLog(gX_LOG_INFO,
            "GUITRACE: element canvas=%d element=%d name=\"%.63s\" text=\"%s\" "
            "rect=%.0f,%.0f,%.0f,%.0f visible=%d disabled=%d clickable=%d focusable=%d "
            "textfit=%.0f,%.0f wraps=%d truncated=%d overflow=%d shrink=%.2f",
            canvasIndex, (gX_I32)i, e->name, text,
            e->rect.x, e->rect.y, e->rect.width, e->rect.height,
            (gX_I32)e->visible, (gX_I32)e->state.DISABLED,
            (gX_I32)e->state.CLICKABLE_LEFT, (gX_I32)e->state.FOCUSABLE,
            fitW, fitH, wraps, truncated, overflow, shrink);
    }
}

static gX_I32 gX_GUI_Load_CanvasHeap(gX_I0) {
    gX_I32 errors = 0;
    gX_TraceLog_GUI(gX_LOG_INFO,"Loading canvas heap");
    
    gX_Heap_Zero(           &(gui.canvases));
    gX_Heap_SetBlockSize(   &(gui.canvases),sizeof(gX_GUI_Canvas));
    gX_Heap_SetMax(         &(gui.canvases),gX_GUI_DEFAULT_MAX_CANVASES);
    gX_Heap_SetAmount(      &(gui.canvases),0);
    errors = gX_Heap_Init(  &(gui.canvases));
    errors = gX_Heap_Assert(&(gui.canvases));
    
    gX_TraceLog_GUI(gX_LOG_INFO,"Canvas heap loading done");
    return errors;
}

//opencode: v0.6.9 — viewport box: fullscreen normally; centered 4:3 fit
//   inside the window when docked (margins host ImGui docks). Game logical
//   size (maindata.screenW/H) is fixed; viewport scales to fit window.
//ai: current dock layout uses a 320px left strip and a 0.70 minimum game scale.
//opencode: dock geometry (v0.6.9 Phase B): 16:9 window, game shifted right
//   into the space beside the left editor dock. Keep >=320px for usable
//   editor controls (token rows truncate below ~300px) if achievable at
//   >=0.70 scale, else overlay fallback (no widen). All in window pixels.
#define GX_DOCK_MARGIN_MIN 320.0f
#define GX_DOCK_SCALE_MIN 0.70f
//ai: cramp fallback uses the same scale floor; edge-only re-engagement avoids
//   a separate numeric hysteresis band and prevents resize-driven dock flapping.

//opencode: host panels reserve their width here so the docked scene fits the
//   visible area; a change re-fits the viewport immediately.
gX_I0 gX_GUI_Viewport_SetRightInset(gX_F32 inset) {
    if (!isfinite(inset) || inset < 0.0f) inset = 0.0f;
    if (inset == gui.viewportRightInset) return;
    gui.viewportRightInset = inset;
    gX_GUI_Viewport_Refresh();
}

//opencode: left inset is additive to the 320px editor dock; negative values
//   reclaim space when a tool hides its left panel (clamped at 0 in Refresh).
gX_I0 gX_GUI_Viewport_SetLeftInset(gX_F32 inset) {
    if (!isfinite(inset)) inset = 0.0f;
    if (inset == gui.viewportLeftInset) return;
    gui.viewportLeftInset = inset;
    gX_GUI_Viewport_Refresh();
}

static gX_F32 gX_GUI_Dock_LeftMargin(gX_I0) {
    gX_F32 left = GX_DOCK_MARGIN_MIN + gui.viewportLeftInset;
    return left > 0.0f ? left : 0.0f;
}

gX_I0 gX_GUI_Viewport_Refresh(gX_I0) {
    //opencode: use the live client size so decorated-window rounding cannot
    //   leave an asymmetric strip after the renderer-scene direct blit.
    extern gX_I32 gX_GetScreenWidth(gX_I0);
    extern gX_I32 gX_GetScreenHeight(gX_I0);
    gX_I32 liveW = gX_GetScreenWidth();
    gX_I32 liveH = gX_GetScreenHeight();
    gX_F32 ww = (gX_F32)((liveW > 320) ? liveW : (gX_I32)maindata.windowWidth);
    gX_F32 wh = (gX_F32)((liveH > 240) ? liveH : (gX_I32)maindata.windowHeight);
    gX_F32 gw0 = (gX_F32)maindata.screenWidth;
    gX_F32 gh0 = (gX_F32)maindata.screenHeight;
    if (ww <= 0) ww = gw0;
    if (wh <= 0) wh = gh0;
    if (!gui.viewportDocked) {
        //opencode: same pillarbox as drawinfo (manual window resizes safe).
        gX_F32 wa = ww / wh, sa = gw0 / gh0;
        if (wa > sa + 0.001f) {
            gX_F32 vw = wh * sa;
            gui.viewportBox = (gX_Rectangle){(ww - vw) * 0.5f, 0.0f, vw, wh};
        } else if (sa > wa + 0.001f) {
            gX_F32 vh = ww / sa;
            gui.viewportBox = (gX_Rectangle){0.0f, (wh - vh) * 0.5f, ww, vh};
        } else {
            gui.viewportBox = (gX_Rectangle){0.0f, 0.0f, ww, wh};
        }
        return;
    }
    gX_F32 gw = (gX_F32)maindata.screenWidth;
    gX_F32 gh = (gX_F32)maindata.screenHeight;
    //opencode: shrink-to-fit the space beside the left dock minus the host's
    //   right-edge inset (scale >= MIN) so panels never cover the scene.
    gX_F32 usable = ww - gX_GUI_Dock_LeftMargin() - gui.viewportRightInset;
    if (usable < 1.0f) usable = 1.0f;
    gX_F32 s = usable / gw;
    gX_F32 heightScale = wh / gh;
    if (s > heightScale) s = heightScale;
    if (s < GX_DOCK_SCALE_MIN) s = GX_DOCK_SCALE_MIN;
    if (s > 1.0f) s = 1.0f;
    gX_F32 vw = gw * s, vh = gh * s;
    gX_F32 left = gX_GUI_Dock_LeftMargin();
    //opencode: focus mode hides both panels; center the scene instead of
    //   leaving a one-sided black band at the old dock edge.
    if (left <= 0.0f && gui.viewportRightInset <= 0.0f && vw < ww)
        left = (ww - vw) * 0.5f;
    gui.viewportBox = (gX_Rectangle){left, (wh - vh) * 0.5f, vw, vh};
}

//opencode: input owns game coords — ScreenToGame maps every frame in
//   Update_Mouse (undocked full-window math is identity, so no behavior
//   change there; manual resizes + dock pillarbox map correctly).
gX_F32v2 gX_GUI_ScreenToGame(gX_F32 sx, gX_F32 sy) {
    gX_F32 s = gui.viewportBox.width / (gX_F32)maindata.screenWidth;
    if (s <= 0) return (gX_F32v2){sx, sy};
    return (gX_F32v2){(sx - gui.viewportBox.x) / s, (sy - gui.viewportBox.y) / s};
}

gX_Rectangle gX_GUI_ViewportBox_Get(gX_I0) {
    (void)0;
    return gui.viewportBox;
}

gX_Rectangle gX_GUI_GameToScreen(gX_Rectangle r) {
    gX_F32 s = gui.viewportBox.width / (gX_F32)maindata.screenWidth;
    if (s <= 0) return r;
    return (gX_Rectangle){
        gui.viewportBox.x + r.x * s, gui.viewportBox.y + r.y * s,
        r.width * s, r.height * s};
}

gX_I32 gX_GUI_Load(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "GUI_Load","Loading GUI system")
    
    strcpy(gui.name,"GUI");
    strcpy(gui.description,"GUI system for gX");
    gX_TraceLog_GUI(gX_LOG_GREEN,"name: %s",gui.name);
    gX_TraceLog_GUI(gX_LOG_GREEN,"desc: %s",gui.description);
    
    gui.debug = false;
    //opencode: preserve INITIAL_LOAD flag across state reset (set before gX_GUI_Load
    //   in main_load.c to suppress transition animation on initial scene switch)
    gX_I1 savedInitialLoad = gui.state.INITIAL_LOAD;
    gui.state = (gX_GUI_State){0};
    gui.masterIndexSwitchResponse = false;
    gui.state.INITIAL_LOAD = savedInitialLoad;
    
    gui.state.SHOW_MSG = false;
    gui.state.SHOW_DEBUGDISPLAY = false;

    //opencode: v0.6.9 — viewport starts fullscreen (= old canvasBox behavior).
    gui.viewportDocked = false;
    gui.viewportLeftInset = 0.0f;
    gX_GUI_Viewport_Refresh();
    
    //opencode: initialize default style / theme — C defaults live in
    //   gX_GUI_Style_ResetDefaults (style.c) so the theme hot-reload path can
    //   restore them before applying JSON (removed keys fall back).
    gX_GUI_Style_ResetDefaults();
    gui.style.uiFontLoaded      = false; //opencode: runtime resource flag — not a theme token

    //opencode: design tokens — theme.json overrides the defaults above
    //   (missing keys fall back to the C values); must run before the CSS
    //   default sheet build so the sheet derives from the final style.
    gX_GUI_Style_LoadDefault();
    
    //opencode: built-in CSS default sheet from gui.style (class conventions —
    //   .btn/.back/.panel/.title etc.). Runs before canvas loading so the
    //   resolver always has the origin-0 sheet.
    gX_GUI_CSS_BuildDefaultSheet();

    //opencode: v0.2.0 — PRELOAD GUI fragment shaders into the RM heap.
    //   Builder_Shader_Frag only LOOKS UP entries (GetPtr_FileName2); gX's
    //   resource manager ships with an EMPTY shader heap, so without this
    //   preload every element-shader instance silently had a NULL program
    //   and never rasterized (root cause of the "elements won't draw" bug).
    {
        extern gX_Shader *gX_Resource_LoadShaderFrag(const gX_I8 *frag);
        const gX_I8 *guiFrags[] = {
            "guisurface.frag", "sdfmag.frag", "guigradient.frag",
            "guitester.frag", "guimaterials.frag", "guifx.frag", "lens.frag",
            //opencode: the pause blur lives under postprocess/ — the old
            //   basename preload silently failed, leaving every element shader
            //   instance NULL (no pause blur at all). Keep the heap name
            //   identical to the builder lookup string.
            "postprocess/blur.frag", "glass.frag" };
        for (gX_U32 fi = 0; fi < sizeof(guiFrags)/sizeof(guiFrags[0]); fi++) {
            gX_Resource_LoadShaderFrag(guiFrags[fi]);
        }
    }

    //opencode: v0.2.0 — shared SDF widget program + engine UI font
    gX_GUI_WidgetSDF_Init();
    //opencode: validate with IsReady (GPU texture), not just glyphs — a
    //   texture-less handle used to shadow the default-font fallback and the
    //   unconditional flag hid the failure entirely (flaky-load audit).
    //opencode: Kenney Mini Square Mono — same CC0 family, but the only
    //   Kenney face with true diagonal X/K (Future/Pixel collapse to H).
    if (!gui.style.uiFontLoaded) {
        //opencode: space-free name — the deps scanner truncates spaced paths.
        gui.style.uiFont = gX_Font_Load("resources/font/KenneyMiniSquareMono.ttf");
        gui.style.uiFontLoaded = gX_Font_IsReady(gui.style.uiFont);
        if (!gui.style.uiFontLoaded)
            gX_TraceLog(gX_LOG_WARNING, "GUI: UI font failed to load — using default font");
    }
    //opencode: v0.6.0 — SDF twin (same baseSize so metrics match the bitmap)
    if (!gui.style.uiFontSDFLoaded) {
        gui.style.uiFontSDF = gX_Font_LoadSDF("resources/font/KenneyMiniSquareMono.ttf", 32);
        gui.style.uiFontSDFLoaded = gX_Font_IsReady(gui.style.uiFontSDF);
        if (!gui.style.uiFontSDFLoaded)
            gX_TraceLog(gX_LOG_WARNING, "GUI: SDF font load failed — sdf text falls back to bitmap");
    }
    //opencode: title branding font — keep the technical Teletext face scoped
    //   to the wordmark; the shared UI is Kenney Mini Square Mono.
    if (!gui.style.titleFontLoaded) {
        gui.style.titleFont = gX_Font_Load("resources/font/EuropeanTeletext.ttf");
        gui.style.titleFontLoaded = gX_Font_IsReady(gui.style.titleFont);
        if (!gui.style.titleFontLoaded)
            gX_TraceLog(gX_LOG_WARNING, "GUI: title font failed to load — wordmark falls back to UI font");
    }
    
    errors = gX_GUI_Load_CanvasHeap();
    
#if GX_GUI_USE_POOLS
    //opencode: init pools (pre-allocated static buffers)
    gX_Pool_Init(&gui.poolCanvases, g_poolCanvasBuf,  g_poolCanvasGen,  sizeof(gX_PoolCanvas),  GX_POOL_CAP_CANVAS);
    gX_Pool_Init(&gui.poolElements, g_poolElementBuf, g_poolElementGen, sizeof(gX_PoolElement), GX_POOL_CAP_ELEMENTS);
    gX_Pool_Init(&gui.poolTexts,    g_poolTextBuf,    g_poolTextGen,    sizeof(gX_PoolText),    GX_POOL_CAP_TEXT);
    gX_Pool_Init(&gui.poolShaders,  g_poolShaderBuf,  g_poolShaderGen,  sizeof(gX_PoolShader),  GX_POOL_CAP_SHADER);
    gX_Pool_Init(&gui.poolTextures, g_poolTextureBuf, g_poolTextureGen, sizeof(gX_PoolTexture), GX_POOL_CAP_TEXTURE);
    gX_Pool_Init(&gui.poolModels,   g_poolModelBuf,   g_poolModelGen,   sizeof(gX_PoolModel),   GX_POOL_CAP_MODEL);
    gX_Pool_Init(&gui.poolShapes,   g_poolShapeBuf,   g_poolShapeGen,   sizeof(gX_PoolShape),   GX_POOL_CAP_SHAPE);
    gX_Pool_Init(&gui.poolAnims,    g_poolAnimBuf,    g_poolAnimGen,    sizeof(gX_PoolAnim),    GX_POOL_CAP_ANIM);
    gX_Pool_Init(&gui.poolDatas,    g_poolDataBuf,    g_poolDataGen,    sizeof(gX_PoolData),    GX_POOL_CAP_DATA);
    gX_Pool_Init(&gui.poolSounds,   g_poolSoundBuf,   g_poolSoundGen,   sizeof(gX_PoolSound),   GX_POOL_CAP_SOUND);
#endif
    
    gui.canvases.state.UPDATE_ALL = true;
    gui.canvases.state.DRAW_ALL = true;
    
    //gX_TraceLog_SuppressStart();
    //make sure that the add_canvas amount is the same as gui.makeamount
    //opencode: use config constant instead of hardcoded 8
    for (gX_U32 i = 0; i < gX_GUI_DEFAULT_MAX_CANVASES; i++) {
        errors = gX_GUI_Add_Canvas();
    }
    //gX_TraceLog_SuppressEnd();
    
    //special canvases
    gui.doNotClearMenu = true;
    gui.menuCanvasIndex = 5;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_ESCAPEMENU, gui.menuCanvasIndex);
    //opencode: pass the heap struct, not raw data pointer — gX_Heap_SeekPtr_Index
    //   reads amount/max/data from the heap header; passing gui.canvases.data
    //   made menuCanvas always NULL (and a wild pointer if name/desc bytes
    //   were ever non-zero at the misread offsets).
    gui.menuCanvas = gX_Heap_SeekPtr_Index(&gui.canvases, (gX_U32)gui.menuCanvasIndex);
    
    gui.doNotClearDebugDisplay = true;
    gui.debugDisplayCanvasIndex = 6;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_DEBUGDISPLAY, gui.debugDisplayCanvasIndex);
    gui.debugDisplayCanvas = gX_Heap_SeekPtr_Index(&gui.canvases, (gX_U32)gui.debugDisplayCanvasIndex);
    
    gui.doNotClearMessages = true;
    gui.messagesCanvasIndex = 7;
    gX_GUI_AddMake(gX_GUI_TEMPLATE_MESSAGE, gui.messagesCanvasIndex);
    gui.messagesCanvas = gX_Heap_SeekPtr_Index(&gui.canvases, (gX_U32)gui.messagesCanvasIndex);

    //opencode: modal dialog canvas — hidden until Dialog_Open builds it.
    gui.doNotClearDialog = true;
    gui.dialogCanvasIndex = 8;
    gui.dialogCanvas = gX_Heap_SeekPtr_Index(&gui.canvases, (gX_U32)gui.dialogCanvasIndex);
    gui.dialogOpen = false;
    gui.dialogTitle[0] = '\0';
    gui.dialogMessage[0] = '\0';
    gui.dialogOkLabel[0] = '\0';
    gui.dialogCancelLabel[0] = '\0';
    gui.dialogHasCancel = false;
    gui.dialogOnOk = NULL;
    gui.dialogOnOkData = NULL;
    gui.dialogOnCancel = NULL;
    gui.dialogOnCancelData = NULL;

    //opencode: v0.1.40 — headless GUI audit hooks.
    gui.auditTemplateID = -1;
    //   GX_GUI_SHOW_MENU=1 forces the escape menu open (no input needed).
    //   GX_GUI_TEMPLATE_FORCE=<NAME> builds any registered template onto
    //   canvas 1 (a plain default canvas whose UPDATE/DRAW stay on), so the
    //   audit script can capture every template headlessly.
    if (getenv("GX_GUI_SHOW_MENU"))
        gui.state.SHOW_MENU = (gX_I1)atoi(getenv("GX_GUI_SHOW_MENU"));
    {
        const gX_I8 *force = getenv("GX_GUI_TEMPLATE_FORCE");
        if (force && force[0]) {
            gX_I32 templ = -1;
            //opencode: resolve by display name via the X-macro string table
            for (gX_I32 ti = 1; ti < gX_GUI_TEMPLATE_COUNT; ti++) {
                const gX_I8 *nm = gX_GUI_Templates_String(ti);
                if (nm && strcmp(nm, force) == 0) { templ = ti; break; }
            }
            if (templ >= 0) {
                //opencode: canvas 2 — canvas 1 is bound to TEMPLATE_DEMO by the
                //   respond scene table; forcing onto it would collide.
                //   Persisted in gui.auditTemplateID: gX_GUI_Respond re-queues it
                //   after EVERY clearAll, otherwise the first scene switch wipes
                //   the one-shot build and the canvas stays empty forever.
                gui.auditTemplateID = templ;
                gX_GUI_AddMake(templ, 2);
                gX_TraceLog(gX_LOG_NOTICE,
                    "GUI audit: forcing template '%s' (id %i) onto canvas 2", force, templ);
            } else {
                gX_TraceLog(gX_LOG_WARNING,
                    "GUI audit: template '%s' not found in registry", force);
            }
        }
    }
    
    gui.buffer.width = maindata.renderWidth;
    gui.buffer.height = maindata.renderHeight;
    //if (!(gui.state.doNotLoadBuffer)) gui.buffer.loadingState.load = true;
    gui.buffer.loadingState.load = true;
    
    //opencode: gui.ping/pong at screen resolution for 1:1 accumulation → crisp blur source
    gui.ping.width = maindata.screenWidth;
    gui.ping.height = maindata.screenHeight;
    gui.ping.loadingState.load = true;
    
    gui.pong.width = maindata.screenWidth;
    gui.pong.height = maindata.screenHeight;
    gui.pong.loadingState.load = true;

    //opencode: compositing background buffer at screen resolution
    gui.composite.background.width = maindata.screenWidth;
    gui.composite.background.height = maindata.screenHeight;
    gui.composite.background.loadingState.load = true;
    gui.composite.backgroundValid = false;
    gui.composite.blurShader.id = 0;
    gui.composite.blurPasses = 3;
    gui.composite.blurTexSlot = 17;  // dedicated slot (shadow map uses 16)

    gui.directdraw.drawFPS = false;
    gui.blurRadius = gX_GUI_BLUR_RADIUS_DEFAULT;

    //opencode: keyboard focus init
    gui.focusEnabled = true;
    gui.focusCanvasID = -1;
    gui.focusElementID = -1;
    gui.modalCanvasID = -1;
    gui.modalPreviousFocusCanvasID = -1;
    gui.modalPreviousFocusElementID = -1;
    gui.modalDismissOnOutside = false;
    gui.focusFirstPending = false;
    gui.modalDepth = 0;
    gui.pointerCaptureActive = false;
    gui.pointerCaptureCanvasID = -1;
    gui.pointerCaptureElementID = -1;

    gX_GUI_Template_InitRegistry();
    gX_GUI_ElementTemplate_InitRegistry();

    //opencode: lifecycle contract — scene response is allowed only for a
    //   loaded GUI. Without this mark, every scene skipped template creation,
    //   including the opt-in GFX viewport-grid composition canvas.
    gui.loaded = true;
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "Loading done")
    return errors;
}

gX_I32 gX_GUI_Unload(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "GUI_Unload","Unloading GUI system")
    
    gX_TraceLog_SuppressStart();
    
    //opencode: guard unload with loaded check
    if (gui.buffer.loadingState.loaded)           gX_RenderTexture_Unload(gui.buffer.rt);
    if (gui.ping.loadingState.loaded)             gX_RenderTexture_Unload(gui.ping.rt);
    if (gui.pong.loadingState.loaded)             gX_RenderTexture_Unload(gui.pong.rt);
    if (gui.composite.background.loadingState.loaded) gX_RenderTexture_Unload(gui.composite.background.rt);
    if (gui.composite.blurShader.id > 0)              gX_Shader_Unload(gui.composite.blurShader);
    //opencode: v0.2.0 — shared SDF widget program + UI font
    gX_GUI_WidgetSDF_Unload();
    if (gui.style.uiFontLoaded) { gX_Font_Unload(gui.style.uiFont); gui.style.uiFontLoaded = false; }
    if (gui.style.uiFontSDFLoaded) { gX_Font_Unload(gui.style.uiFontSDF); gui.style.uiFontSDFLoaded = false; }
    if (gui.style.titleFontLoaded) { gX_Font_Unload(gui.style.titleFont); gui.style.titleFontLoaded = false; }
    //opencode: flush buffer pool (unload any stashed render textures)
    gX_GUI_BufferPool_Flush();
    
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Unloading canvases");
    gX_GUI_Canvas *canvases = gui.canvases.data;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        gX_GUI_Canvas *canvas = &canvases[i];
        gX_GUI_Canvas_Unload(canvas);
    }
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Unloading done");
    
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Deinitíalizing canvas heap");
    errors = gX_Heap_DeInit(&(gui.canvases));
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Deinitíalizing done");
    
    gui.loaded = false;
    
    gX_TraceLog_SuppressEnd();
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_TraceLog_logtypeLevel, "Unloading done")
    return errors;
}

gX_I32 gX_GUI_Add_Canvas(gX_I0) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Add_Canvas","Adding canvas")
    
    //opencode: use _GetIndex to record actual heap slot (not amount-1, which is wrong when holes exist)
    gX_I32 actualIndex = -1;
    gX_GUI_Canvas *canvas = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&(gui.canvases), &actualIndex);
    errors = gX_GUI_Canvas_Load(canvas);
    if (!errors && actualIndex >= 0) {
        gui.canvases.meta[actualIndex].UPDATE = true;
        gui.canvases.meta[actualIndex].DRAW = true;
    }
    else if (errors || !canvas) {
        gX_TraceLog_GUI(gX_LOG_WARNING,"Canvas cannot be loaded");
        errors++;
    }
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas added")
    return errors;
}

gX_I32 gX_GUI_Remove_Canvas(gX_U32 index) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Remove_Canvas","Removing canvas")

    if (index >= gui.canvases.amount || !gui.canvases.data
        || !gui.canvases.meta || !gui.canvases.meta[index].USED) {
        gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas remove rejected")
        return 1;
    }

    if (gui.dialogCanvasIndex == (gX_I32)index) {
        gui.dialogOpen = false;
        gui.dialogOnOk = NULL;
        gui.dialogOnOkData = NULL;
        gui.dialogOnCancel = NULL;
        gui.dialogOnCancelData = NULL;
        gui.dialogHasCancel = false;
        gui.dialogCanvas = NULL;
        gui.dialogCanvasIndex = -1;
    }
    if (gui.focusCanvasID == (gX_I32)index) gX_GUI_Focus_Clear();
    if (gui.pointerCaptureCanvasID == (gX_I32)index)
        gX_GUI_PointerCapture_Clear();
    gX_U32 kept = 0;
    for (gX_U32 i = 0; i < gui.modalDepth; i++) {
        if (gui.modalStack[i].canvasID == (gX_I32)index) continue;
        if (gui.modalStack[i].returnFocusCanvasID == (gX_I32)index) {
            gui.modalStack[i].returnFocusCanvasID = -1;
            gui.modalStack[i].returnFocusElementID = -1;
        }
        if (kept != i) gui.modalStack[kept] = gui.modalStack[i];
        kept++;
    }
    for (gX_U32 i = kept; i < gui.modalDepth; i++)
        memset(&gui.modalStack[i], 0, sizeof(gui.modalStack[i]));
    gui.modalDepth = kept;
    if (gui.modalPreviousFocusCanvasID == (gX_I32)index) {
        gui.modalPreviousFocusCanvasID = -1;
        gui.modalPreviousFocusElementID = -1;
    }

    gX_GUI_Canvas *canvas = gX_Heap_Remove_Index_NoPtr(&(gui.canvases),index);
    if (!canvas) {
        gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas remove failed")
        return 1;
    }
    if (gui.modalCanvasID == (gX_I32)index) gX_GUI_Modal_Close();
    errors = gX_GUI_Canvas_Unload(canvas);
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas removed")
    return errors;
}

//opencode: compositing pipeline — helper functions

gX_I0 gX_GUI_Composite_CaptureBackground(gX_I0) {
    if (!gui.composite.background.loadingState.loaded) return;

#if gX_ENABLE_RENDERER == 1
    gX_I1 useCanvas = (gX_Renderer_GetCanvasDrawn() && renderer.canvas.texture.id > 0);
#else
    gX_I1 useCanvas = 0;
#endif
    //opencode: title scene draws directly to screen (no renderer.canvas content),
    //   so we capture from the default framebuffer (0) as fallback. The title
    //   scene has already drawn to screen before GUI starts, so this captures it.
    gX_I1 useScreen = !useCanvas;

    if (useCanvas || useScreen) {
        //opencode: save viewport rect BEFORE BeginTexture overwrites drawinfo!
        gX_I32 saveVpX = drawinfo.vpX, saveVpY = drawinfo.vpY;
        gX_I32 saveVpW = drawinfo.vpW, saveVpH = drawinfo.vpH;
        gX_Drawing_BeginTexture(gui.composite.background.rt);
            gX_Drawing_ClearBG((gX_Color){0,0,0,0});
            gX_RHI_FBO prevReadFbo = gX_RHI_GetReadFBO();
#if gX_ENABLE_RENDERER == 1
            if (useCanvas) {
                gX_RHI_Blit(renderer.canvas.id, gui.composite.background.rt.id,
                    0, 0,
                    (gX_U32)renderer.canvas.texture.width,
                    (gX_U32)renderer.canvas.texture.height,
                    0, 0,
                    (gX_U32)gui.composite.background.width,
                    (gX_U32)gui.composite.background.height,
                    gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
            } else {
#endif
                gX_RHI_Blit(0, gui.composite.background.rt.id,
                    (gX_U32)saveVpX, (gX_U32)saveVpY,
                    (gX_U32)saveVpW, (gX_U32)saveVpH,
                    0, 0,
                    (gX_U32)gui.composite.background.width,
                    (gX_U32)gui.composite.background.height,
                    gX_RHI_COLOR_BUFFER_BIT, gX_RHI_LINEAR);
#if gX_ENABLE_RENDERER == 1
            }
#endif
            gX_RHI_BindReadFBO(prevReadFbo);
        gX_Drawing_EndTexture();
        gui.composite.backgroundValid = true;
    } else {
        gui.composite.backgroundValid = false;
    }
}

gX_I1 gX_GUI_Composite_BackgroundValid(gX_I0) {
    return gui.composite.backgroundValid;
}

gX_I0 gX_GUI_Composite_DrawBackground(gX_I0) {
    if (!gui.composite.backgroundValid) { gX_TraceLog(gX_LOG_INFO, "GUI_DRAW_BG: skip (not valid)"); return; }
    if (!gui.composite.background.loadingState.loaded) { gX_TraceLog(gX_LOG_INFO, "GUI_DRAW_BG: skip (not loaded)"); return; }
    gX_TraceLog(gX_LOG_INFO, "GUI_DRAW_BG: drawing composite background");
    gX_Texture_Draw(gui.composite.background.rt.texture, (gX_Texture_Draw_Params){
        .dst = (gX_Rectangle){0, 0, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight},
        .tint = gX_WHITE,
        .scale = {1, 1}
    });

    //opencode: GLFW fix — do NOT set canvasDrawnByGUI here. The background
    //   texture is NOT the canvas itself; it's a frame-delay capture used as
    //   a blur source. Setting drawnByGUI=1 prevents gX_glfw_Drawing_End from
    //   compositing the real (current-frame) canvas on top, so the screen
    //   only shows the stale background.
    //gX_Renderer_SetCanvasDrawnByGUI(1); // REMOVED — was blocking canvas composite
}

//opencode: inter-layer blur — reads composite.background, applies blur.frag via ping-pong,
//  draws blurred result to screen. Must run BEFORE Phase 1 (canvas composites) so the
//  blurred scene is behind the GUI elements.
gX_I0 gX_GUI_Composite_ApplyBlur(gX_I0) {
    if (!gui.composite.backgroundValid) return;
    if (!gui.ping.loadingState.loaded || !gui.pong.loadingState.loaded) return;
    gX_F32 radius = gui.blurRadius;
    if (radius <= 0.0f) { gX_GUI_Composite_DrawBackground(); return; }

    // Lazy-load blur shader
    if (gui.composite.blurShader.id <= 0) {
        gui.composite.blurShader = gX_Shader_Load(0, "resources/shader/postprocess/blur.frag");
        if (gui.composite.blurShader.id <= 0) { gX_GUI_Composite_DrawBackground(); return; }
        gui.composite.blurInputLoc = gX_Shader_GetLocation_Multi(gui.composite.blurShader, 4, "texture0", "input", "inputTex", "tex0");
        gui.composite.blurResLoc = gX_Shader_GetLocation(gui.composite.blurShader, "res");
        gui.composite.blurColLoc = gX_Shader_GetLocation_Multi(gui.composite.blurShader, 2, "albedoColor", "col");
        gui.composite.blurUniformsLoc = gX_Shader_GetLocation(gui.composite.blurShader, "uniforms");
    }

    gX_Shader shader = gui.composite.blurShader;
    gX_F32v2 res = { (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight };

    // Clear both ping-pong buffers
    gX_Drawing_BeginTexture(gui.ping.rt);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
    gX_Drawing_EndTexture();
    gX_Drawing_BeginTexture(gui.pong.rt);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
    gX_Drawing_EndTexture();

    // Multi-pass blur: composite.background → ping/pong alternating
    for (gX_I32 pass = 0; pass < gui.composite.blurPasses; pass++) {
        gX_RenderTexture srcRT = (pass == 0)
            ? gui.composite.background.rt
            : ((pass % 2 == 1) ? gui.pong.rt : gui.ping.rt);
        gX_RenderTexture dstRT = (pass % 2 == 1) ? gui.ping.rt : gui.pong.rt;

        gX_Drawing_BeginTexture(dstRT);
            gX_Drawing_BeginShader(shader);
                gX_Shader_SetValue_V2(shader, gui.composite.blurResLoc, res);
                gX_Shader_SetValue_V4(shader, gui.composite.blurColLoc, (gX_F32v4){1,1,1,1});
                gX_F32v4 uniforms[4] = { {radius,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
                gX_Shader_SetValue_Array(shader, gui.composite.blurUniformsLoc, uniforms, gX_SHADER_UNIFORM_VEC4, 4);
                // Bind input texture via dedicated slot
                gX_Shader_SetValue_TextureSlot(shader, gui.composite.blurInputLoc,
                    srcRT.texture, gui.composite.blurTexSlot);
                gX_Rectangle_Draw((gX_Rectangle){0, 0, res.x, res.y}, gX_WHITE);
            gX_Drawing_EndShader();
        gX_Drawing_EndTexture();
    }

    // Draw final blurred result to screen with correct blend
    gX_RenderTexture finalRT = (gui.composite.blurPasses % 2 == 1) ? gui.pong.rt : gui.ping.rt;
    gX_Drawing_EndBlend();
    gX_Texture_Draw(finalRT.texture, (gX_Texture_Draw_Params){
        .dst = (gX_Rectangle){0, 0, res.x, res.y},
        .tint = gX_WHITE,
        .scale = {1, 1}
    });
    gX_Drawing_EndBlend();

    //gX_Renderer_SetCanvasDrawnByGUI(1); // REMOVED — was blocking canvas composite
}

//opencode: scan remaining canvases to see if any subsequent canvas needs gui.ping content.
//   Shader elements and model glass elements read from gui.ping during draw.
static gX_I1 gX_GUI_Composite_NeedsPingCapture(gX_U32 currentIndex) {
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas *)gui.canvases.data;
    for (gX_U32 j = currentIndex + 1; j < gui.canvases.amount; j++) {
        if (!gui.canvases.meta[j].DRAW) continue;
        gX_GUI_Canvas *c = &canvases[j];
        gX_GUI_Element *elements = (gX_GUI_Element *)c->elements.data;
        if (!elements) continue;
        for (gX_U32 k = 0; k < c->elements.amount; k++) {
            gX_GUI_Element *e = &elements[k];
            if (e->shaders.amount > 0) return true;
        }
    }
    //opencode: also check model glass elements that read from gui.ping
    for (gX_U32 j = currentIndex + 1; j < gui.canvases.amount; j++) {
        if (!gui.canvases.meta[j].DRAW) continue;
        gX_GUI_Canvas *c = &canvases[j];
        gX_GUI_Element *elements = (gX_GUI_Element *)c->elements.data;
        if (!elements) continue;
        for (gX_U32 k = 0; k < c->elements.amount; k++) {
            gX_GUI_Element *e = &elements[k];
            if (e->models.amount > 0) {
                for (gX_U32 m = 0; m < e->models.amount; m++) {
                    gX_GUI_Element_Model *mdl = (gX_GUI_Element_Model *)gX_Heap_SeekPtr_Index(&e->models, m);
                    if (mdl && mdl->extraparams.doGlass) return true;
                }
            }
        }
    }
    return false;
}

static gX_I1 s_prevWant = 0;

//opencode: v0.6.9 Phase B — dock widen/restore + RT realloc.
static gX_U32 s_preDockW = 0, s_preDockH = 0;
//opencode: dock hardening — size the window was widened to (restore the
//   pre-dock size on close only if the user did not resize meanwhile).
static gX_U32 s_dockW = 0, s_dockH = 0;
//opencode: settle grace after OUR SetSize (engage/restore) — the WM applies
//   resizes async; without it the live-size sync snaps maindata back to the
//   stale OS size on the next frame, collapsing the dock (cramp fallback)
//   before the resize lands and sticking restores. ~30 frames ≈ 0.5s.
static gX_I32 s_settleFrames = 0;

static gX_I0 gX_GUI_Dock_RTs_WindowSize(gX_I0) {
    //opencode: ping/pong/composite-bg cover the WINDOW (panel backdrops in
    //   margins); stash old for pool reuse (no leak), flag reload at new dims.
    gX_GUI_BufferPool_Stash(gui.ping, gui.ping.width, gui.ping.height, true);
    gX_GUI_BufferPool_Stash(gui.pong, gui.pong.width, gui.pong.height, true);
    gX_GUI_BufferPool_Stash(gui.composite.background, gui.composite.background.width,
        gui.composite.background.height, true);
    gui.ping.width = maindata.windowWidth;
    gui.ping.height = maindata.windowHeight;
    gui.pong.width = maindata.windowWidth;
    gui.pong.height = maindata.windowHeight;
    gui.composite.background.width = maindata.windowWidth;
    gui.composite.background.height = maindata.windowHeight;
    gui.ping.loadingState.loaded = false;
    gui.pong.loadingState.loaded = false;
    gui.composite.background.loadingState.loaded = false;
    gui.ping.loadingState.load = true;
    gui.pong.loadingState.load = true;
    gui.composite.background.loadingState.load = true;
}

//opencode: headless regression seam for the dock render-target resize state.
gX_I0 gX_GUI_Test_Dock_RTs_WindowSize(gX_I0) {
    gX_GUI_Dock_RTs_WindowSize();
}

//opencode: WM resize sync — maindata is the single source downstream reads,
//   so a live client size that differs re-fits the viewport and the
//   window-sized RTs. Split from the frame path so the headless suite drives
//   the same decision logic; the OS event itself stays integration coverage.
gX_I1 gX_GUI_WindowResize_Sync(gX_I32 liveW, gX_I32 liveH) {
    if (!((liveW > 320) && (liveH > 240))) return false;
    if (((gX_U32)liveW == maindata.windowWidth)
        && ((gX_U32)liveH == maindata.windowHeight)) return false;
    maindata.windowWidth = (gX_U32)liveW;
    maindata.windowHeight = (gX_U32)liveH;
    gX_GUI_Viewport_Refresh();
    gX_GUI_Dock_RTs_WindowSize();
    return true;
}

//opencode: cramped dock falls back to overlay (no resize; hysteresis vs the
//   320px engage bar so border drags do not flap). Stays overlay until tools
//   reopen (no re-widen fights).
gX_I1 gX_GUI_Dock_Cramped_Check(gX_I0) {
    if (!gui.viewportDocked) return false;
    gX_F32 ww = (gX_F32)maindata.windowWidth;
    gX_F32 minViewportW = (gX_F32)maindata.screenWidth * GX_DOCK_SCALE_MIN;
    if ((ww - gX_GUI_Dock_LeftMargin() - gui.viewportRightInset) >= minViewportW)
        return false;
    gui.viewportDocked = 0;
    gX_GUI_Viewport_Refresh();
    gX_TraceLog(gX_LOG_INFO, "GUI: dock cramped (%ux%u) — overlay fallback",
        maindata.windowWidth, maindata.windowHeight);
    return true;
}

static gX_I0 gX_GUI_Dock_Update(gX_I0) {
    extern gX_I1 gX_Tools_RuntimeActive(gX_I0);
    //opencode: GX_TOOLS_CLOSE=<frame> closes all runtime tools once
    //   frameCount hits it (verify restore + script flows; 1 = immediate).
    {
        static gX_I1 closeDone = 0;
        if (!closeDone) {
            const char *clEnv = getenv("GX_TOOLS_CLOSE");
            if (clEnv && clEnv[0] && maindata.frameCount >= (gX_U64)atoi(clEnv)) {
                extern gX_I0 gX_Tools_Close_Runtime(gX_I0);
                gX_Tools_Close_Runtime();
                closeDone = 1;
                gX_TraceLog(gX_LOG_INFO, "GUI: headless close applied");
            }
        }
    }
    gX_I1 want = gX_Tools_RuntimeActive();

    //opencode: dock hardening — follow user/WM resizes. maindata is the
    //   single source downstream reads, so sync it from the OS window,
    //   then re-fit viewport + window-sized RTs. Silent (per-frame safe).
    //   Skipped during the post-SetSize settle grace (our own resize still
    //   in flight — the OS still reports the stale size).
    if (s_settleFrames > 0) {
        s_settleFrames--;
    } else {
        extern gX_I32 gX_GetScreenWidth(gX_I0);
        extern gX_I32 gX_GetScreenHeight(gX_I0);
        gX_GUI_WindowResize_Sync(gX_GetScreenWidth(), gX_GetScreenHeight());
    }
    gX_GUI_Dock_Cramped_Check();

    gX_I1 full = gX_Window_IsFullScreen();
    //opencode: engage on the tool-open edge only (never re-widen a window
    //   the user deliberately resized).
    if (want && !s_prevWant && !gui.viewportDocked && !full) {
        //opencode: predict 16:9 window (fit monitor) + margins; engage only
        //   if usable (>=320px) margins achievable (else overlay fallback,
        //   no widen).
        gX_I32 mon = gX_Monitor_GetCurrent();
        gX_I32 monW = gX_Monitor_GetWidth(mon);
        gX_I32 monH = gX_Monitor_GetHeight(mon);
        if (monW <= 0 || monH <= 0) return;
        gX_F32 th = (gX_F32)maindata.windowHeight;
        gX_F32 tw = th * 16.0f / 9.0f;
        if (tw > (gX_F32)(monW - 32)) {
            tw = (gX_F32)(monW - 32);
            th = tw * 9.0f / 16.0f;
        }
        if (th > (gX_F32)(monH - 64)) {
            th = (gX_F32)(monH - 64);
            tw = th * 16.0f / 9.0f;
        }
        gX_F32 gw = (gX_F32)maindata.screenWidth;
        gX_F32 availableW = tw - gX_GUI_Dock_LeftMargin() - gui.viewportRightInset;
        gX_F32 dockScale = availableW / gw;
        gX_F32 heightScale = th / (gX_F32)maindata.screenHeight;
        if (dockScale > heightScale) dockScale = heightScale;
        if (dockScale < GX_DOCK_SCALE_MIN) return;
        s_preDockW = maindata.windowWidth;
        s_preDockH = maindata.windowHeight;
        maindata.windowWidth = (gX_U32)tw;
        maindata.windowHeight = (gX_U32)th;
        gX_Window_SetSize(maindata.windowWidth, maindata.windowHeight);
        s_settleFrames = 30;
        gX_Window_SetPosition((monW - (gX_I32)tw) / 2, (monH - (gX_I32)th) / 2);
        gui.viewportDocked = 1;
        s_dockW = maindata.windowWidth;
        s_dockH = maindata.windowHeight;
        gX_GUI_Viewport_Refresh();
        gX_GUI_Dock_RTs_WindowSize();
        gX_TraceLog(gX_LOG_INFO, "GUI: dock engaged (%ux%u, left margin %.0fpx)",
            maindata.windowWidth, maindata.windowHeight, GX_DOCK_MARGIN_MIN);
    } else if (!want && gui.viewportDocked) {
        //opencode: dock hardening — restore pre-dock size unless the user
        //   resized: exact match, or within a few px (WM decorations and
        //   rounding report back slightly off the requested size).
        gX_I32 dw = (gX_I32)maindata.windowWidth - (gX_I32)s_dockW;
        gX_I32 dh = (gX_I32)maindata.windowHeight - (gX_I32)s_dockH;
        if (dw < 0) dw = -dw;
        if (dh < 0) dh = -dh;
        if ((s_preDockW > 0) && (s_preDockH > 0) && (dw <= 8) && (dh <= 8)) {
            maindata.windowWidth = s_preDockW;
            maindata.windowHeight = s_preDockH;
            gX_Window_SetSize(maindata.windowWidth, maindata.windowHeight);
            s_settleFrames = 30;
            gX_I32 rmon = gX_Monitor_GetCurrent();
            gX_I32 rmw = gX_Monitor_GetWidth(rmon);
            gX_I32 rmh = gX_Monitor_GetHeight(rmon);
            if (rmw > 0 && rmh > 0)
                gX_Window_SetPosition((rmw - (gX_I32)maindata.windowWidth) / 2,
                    (rmh - (gX_I32)maindata.windowHeight) / 2);
        }
        gui.viewportDocked = 0;
        gX_GUI_Viewport_Refresh();
        gX_GUI_Dock_RTs_WindowSize();
        gX_TraceLog(gX_LOG_INFO, "GUI: dock released (%ux%u)",
            maindata.windowWidth, maindata.windowHeight);
    }
    s_prevWant = want;
}

//opencode: tools overlay (dock + ImGui chrome), shared by the compositing
//   path and the renderer-scene bypass (which returns before the tail).
static gX_I0 gX_GUI_Draw_ToolsOverlay(gX_I0) {    //opencode: v0.6.9 Phase B — dock controller (per-frame transition
    //   check, cheap). See call site for the full contract.
    gX_GUI_Dock_Update();

    //opencode: v0.4.0 — gX Tools suite: ImGui tool chrome drawn after the
    //   canvases + overlays.
    //opencode: v0.6.5 — runtime tools (EXTRAS) draw over any scene
    extern gX_I1 gX_Tools_RuntimeActive(gX_I0);
    //opencode: module overlays (the shell console draws over any scene too,
    //   F10, independent of the exclusive tool set).
    gX_I1 consoleOpen = gX_Module_OverlayOpen();
    if (maindata.masterIndex == gX_SCENE_GUIPLAYGROUND || arguments.hasThemeEditor
        || gX_Tools_RuntimeActive() || consoleOpen) {
        //opencode: v0.6.9 — fullscreen viewport for ImGui (margins
        //   drawable; game phase uses game-rect). No-op until docked.
        gX_RHI_Viewport(0, 0, (gX_I32)maindata.windowWidth, (gX_I32)maindata.windowHeight);
        extern gX_I0 gX_GUI_Draw_ImGui(gX_I0);
        gX_GUI_Draw_ImGui();
    }
}

//opencode: close the diagnostic scope on all GUI exits, including renderer
//   scenes that bypass the normal canvas compositor.
static gX_I0 gX_GUI_Draw_Diagnostics_End(gX_GL_State *before, gX_I1 enabled, const gX_I8 *label) {
    if (!enabled) return;
    gX_GL_State after = {0};
    gX_GL_State_Save(&after);
    gX_GL_State_Diff(before, &after, label);
}

gX_I0 gX_GUI_Draw(gX_I0) {    //opencode: full-frame GL state diff gated behind --gl-diagnostics (see
    //   canvas.c — two snapshots + diff per frame is debug-only overhead).
    gX_GL_State guiDrawBefore = {0};
    gX_I1 guiDiagState = gX_RHI_Diagnostics_IsEnabled();
    if (guiDiagState) gX_GL_State_Save(&guiDrawBefore);
    
#if GX_GUI_USE_POOLS
    //opencode: clear all pools at start of frame. All handles from previous
    //   frame become stale. Templates must rebuild every frame.
    gX_Pool_Clear(&gui.poolElements);
    gX_Pool_Clear(&gui.poolTexts);
    gX_Pool_Clear(&gui.poolShaders);
    gX_Pool_Clear(&gui.poolTextures);
    gX_Pool_Clear(&gui.poolModels);
    gX_Pool_Clear(&gui.poolShapes);
    gX_Pool_Clear(&gui.poolAnims);
    gX_Pool_Clear(&gui.poolDatas);
    gX_Pool_Clear(&gui.poolSounds);
    //opencode: migrate old heap elements to pool entries
    gX_GUI_Pools_MigrateFromHeap();
#endif
    
    //opencode: GFX scenes bypass GUI compositing — blit canvas directly to screen.
    //   GX_GFX_GRID=1 is the explicit exception: its renderer writes four tile
    //   RTs and this normal GUI path composes them into the dashboard.
    //   GUI compositing is unnecessary overhead for full-screen 3D rendering and
    //   the canvas -> background -> screen alpha-blend path causes the white screen
    //   issue (canvas alpha=0 leaves white bgColor visible to the user).
    //opencode: extended to cover ALL renderer scenes — sky/water/wireframe also
    //   draw to renderer.canvas (RGBA16F) and get lost in the compositing pipeline.
#if gX_ENABLE_RENDERER == 1
    if (!gX_RendererGFX_ViewportGrid_Enabled() &&
        (maindata.masterIndex == gX_SCENE_RENDERER_GFX ||
         maindata.masterIndex == gX_SCENE_GFXDEBUG ||
         maindata.masterIndex == gX_SCENE_RENDERER_SKY ||
         maindata.masterIndex == gX_SCENE_RENDERER_WATER ||
         maindata.masterIndex == gX_SCENE_RENDERER_WIREFRAME ||
         maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH)) {
        gX_RHI_FBO prevReadFbo = gX_RHI_GetReadFBO(), prevDrawFbo = gX_RHI_GetDrawFBO();
        gX_Rectangle viewport = gui.viewportBox;
        if (renderer.canvas.texture.id > 0) {
            //opencode: renderer-scene direct blit follows the GUI viewport so
            //   docked tools do not leave a fixed-size black pillar on the side.
            extern gX_I32 gX_GetScreenHeight(gX_I0);
            gX_I32 targetHeight = gX_GetScreenHeight();
            if (targetHeight <= 0) targetHeight = (gX_I32)maindata.windowHeight;
            gX_U32 viewportY = (gX_U32)targetHeight
                - (gX_U32)(viewport.y + viewport.height);
            gX_RHI_Blit(renderer.canvas.id, 0,
                0, 0,
                (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight,
                (gX_U32)viewport.x, viewportY,
                (gX_U32)viewport.width, (gX_U32)viewport.height,
                gX_RHI_COLOR_BUFFER_BIT, gX_RHI_NEAREST);
        }
        gX_RHI_BindReadFBO(prevReadFbo);
        gX_RHI_BindDrawFBO(prevDrawFbo);

        //opencode: renderer scenes bypass the normal composite pipeline, but
        //   EscapeMenu's blur shader still samples gui.ping. Seed it from this
        //   frame's renderer target before drawing the menu so the overlay
        //   never blurs stale content from an earlier scene.
        if (gui.state.SHOW_MENU && gui.ping.loadingState.loaded &&
            renderer.canvas.texture.id > 0) {
            gX_Drawing_BeginTexture(gui.ping.rt);
                gX_Drawing_ClearBG((gX_Color){0,0,0,255});
                gX_RHI_Blit(renderer.canvas.id, gui.ping.rt.id,
                    0, 0,
                    (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight,
                    0, 0,
                    (gX_U32)gui.ping.width, (gX_U32)gui.ping.height,
                    gX_RHI_COLOR_BUFFER_BIT, gX_RHI_LINEAR);
            gX_Drawing_EndTexture();
        }

        //opencode: draw escape menu canvas on top of 3D scene before ImGui.
        //   Normal compositing pipeline (canvas → background → screen) is
        //   skipped for renderer scenes — the 3D canvas blits directly to
        //   screen. Without this, the escape menu (SHOW_MENU=true) never
        //   appears during GFX/water/sky/wireframe scenes.
        gX_GUI_Canvas *gfxCanvases = gui.canvases.data;
        if (gfxCanvases && gui.state.SHOW_MENU &&
            gui.menuCanvasIndex >= 0 && (gX_U32)gui.menuCanvasIndex < gui.canvases.amount)
        {
            gX_GUI_Canvas *mc = &gfxCanvases[gui.menuCanvasIndex];
            if (gui.canvases.meta[gui.menuCanvasIndex].DRAW) {
                gX_GUI_Canvas_Draw(mc);
            }
        }

        //opencode: tools overlay on renderer scenes (GFX editor lives
        //   here) — same gate as the compositing-path call below; without
        //   this the early return skips ImGui entirely on 3D scenes.
        gX_GUI_Draw_ToolsOverlay();
        gX_GUI_Draw_Diagnostics_End(&guiDrawBefore, guiDiagState, "gui_draw_renderer");
        return;
    }
#endif

    //=====================================================================
    // Compositing pipeline
    //=====================================================================

    // Phase 0: Capture scene background
    gX_GUI_Composite_CaptureBackground();

    //opencode: fill gui.buffer with scene canvas for model shader background texture.
    //   Uses gX_RHI_Blit for Y-flip-safe RT-to-RT copy — avoids
    //   DrawTexturePro invertY fragility when both source and dest are FBOs.
#if gX_ENABLE_RENDERER == 1
    if (gui.buffer.loadingState.loaded && renderer.canvas.texture.id > 0) {
        gX_Drawing_BeginTexture(gui.buffer.rt);
            gX_Drawing_ClearBG((gX_Color){0,0,0,255});
            gX_RHI_Blit(renderer.canvas.id, gui.buffer.rt.id,
                0, 0,
                (gX_U32)gui.buffer.width, (gX_U32)gui.buffer.height,
                0, 0,
                (gX_U32)gui.buffer.width, (gX_U32)gui.buffer.height,
                gX_RHI_COLOR_BUFFER_BIT, gX_RHI_LINEAR);
        gX_Drawing_EndTexture();
    }
#endif

    // Initialize gui.ping with scene background (if valid) so shader elements
    // on canvases can read the scene behind. Accumulation adds canvases on top.
    if (gui.ping.loadingState.loaded) {
        gX_Drawing_BeginTexture(gui.ping.rt);
            gX_Drawing_ClearBG((gX_Color){0,0,0,0});
            if (gui.composite.backgroundValid) {
                gX_RHI_Blit(gui.composite.background.rt.id, gui.ping.rt.id,
                    0, 0,
                    (gX_U32)gui.ping.width, (gX_U32)gui.ping.height,
                    0, 0,
                    (gX_U32)gui.ping.width, (gX_U32)gui.ping.height,
                    gX_RHI_COLOR_BUFFER_BIT, gX_RHI_LINEAR);
            }
        gX_Drawing_EndTexture();
    }

    // ---- Apply scene background to screen ----
    // Must run BEFORE canvas composites (Phase 1) so the background is behind GUI.
    // Blur is handled by the EscapeMenu canvas's shader element, which reads from
    // gui.ping (initialized with composite.background + accumulated canvases).
    GX_PROFILE_ZONE_BEGIN("gui:composite");
    gX_GUI_Composite_DrawBackground();
    GX_PROFILE_ZONE_END("gui:composite");

#if gX_ENABLE_SCENE == 1
    if (maindata.masterIndex == gX_SCENE_BATTLE
        || maindata.masterIndex == gX_SCENE_PHYSICS) {
        gX_Scene_Draw_Overlay();
    }
#endif

    // Phase 1: Draw canvases to buffers + screen composite + accumulate to gui.ping
    gX_GUI_Canvas *canvases = gui.canvases.data;
    if (!canvases) {
        gX_TraceLog_GUI(gX_LOG_WARNING, "GUI Draw: no canvases");
        gX_GUI_Draw_Diagnostics_End(&guiDrawBefore, guiDiagState, "gui_draw_empty");
        return;
    }
    GX_PROFILE_ZONE_BEGIN("gui:canvas_draws");
    //opencode: generic layout debug — clear the GUI-tracked entries once per
    //   frame so the overlay/validation only shows the current frame's layout
    if (gX_uiDebugEnabled) gX_UI_Layout_GUI_Clear();
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        gX_GUI_Canvas *canvas = &canvases[i];
        if (!gui.canvases.meta[i].DRAW) continue;

        GX_PROFILE_ZONE_BEGIN("gui:canvas_draw");
#if GX_GUI_USE_POOLS
        gX_GUI_Canvas_Draw_Pools(i);
#else
        gX_GUI_Canvas_Draw(canvas);  // legacy heap-based draw
#endif
        GX_PROFILE_ZONE_END("gui:canvas_draw");

        //opencode: per-canvas screen capture — only when subsequent canvases
        //   need gui.ping (shader elements or model glass effects).
        //   Skips the blit for canvases that don't have any following canvas
        //   that reads from gui.ping, saving a full-screen framebuffer blit.
        //   NOTE: source rect uses viewport position (drawinfo.vpX/vpY) because
        //   the default framebuffer is the WINDOW size (e.g. 2560×1920) while
        //   gui.ping is the RENDER size (e.g. 1024×768). Without this
        //   correction, gX_RHI_Blit reads from the window's bottom-left
        //   corner instead of the viewport's render area.
        if (gui.ping.loadingState.loaded && gX_GUI_Composite_NeedsPingCapture(i)) {
            //opencode: save viewport rect BEFORE BeginTexture overwrites drawinfo!
            //   BeginTexture sets drawinfo.vp to the RT size, but we need the
            //   default FB viewport for the blit source rect. Without this save,
            //   the source rect becomes (0,0,1024,768) instead of (0,0,2560,1920),
            //   capturing only the bottom-left quadrant of the window.
            gX_I32 saveVpX = drawinfo.vpX, saveVpY = drawinfo.vpY;
            gX_I32 saveVpW = drawinfo.vpW, saveVpH = drawinfo.vpH;
            gX_Drawing_BeginTexture(gui.ping.rt);
                gX_Drawing_ClearBG((gX_Color){0,0,0,0});
                gX_RHI_Blit(0, gui.ping.rt.id,
                    (gX_U32)saveVpX, (gX_U32)saveVpY,
                    (gX_U32)saveVpW, (gX_U32)saveVpH,
                    0, 0,
                    (gX_U32)gui.ping.width, (gX_U32)gui.ping.height,
                    gX_RHI_COLOR_BUFFER_BIT, gX_RHI_LINEAR);
            gX_Drawing_EndTexture();
            }
    }
    GX_PROFILE_ZONE_END("gui:canvas_draws");

    //opencode: v0.2.0-pre10 — SDF widget surfaces rasterize AFTER all canvas
    //   composites, straight to the default framebuffer (queue accumulated
    //   during the element loops above; rects pre-mapped to screen space).
    {
        extern gX_I0 gX_GUI_WidgetSDF_Flush(gX_I0);
        gX_GUI_WidgetSDF_Flush();
    }
    
    //opencode: v0.3.8 — transition wipe overlay. Drawn after canvas composites
    //   AND the SDF flush so the sweep covers the entire GUI surface.
    gX_GUI_Transition_DrawOverlay();
    
    //opencode: generic layout debug — validate + overlay + ruler for every
    //   GUI screen (battle draws its own raw-drawn HUD tracking in
    //   battle/ui.c, so it runs its own validate/overlay/ruler).
    if (gX_uiDebugEnabled && maindata.masterIndex != gX_SCENE_BATTLE) {
        gX_UI_Layout_Validate((gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight);
        gX_UI_Layout_DrawOverlay((gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight);
        gX_UI_Layout_DrawRuler((gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight);
    }
    
    if (gui.directdraw.drawLoadingScreen) {
        //opencode: dark overlay + spinner + text
        gX_F32 sw = (gX_F32)maindata.screenWidth;
        gX_F32 sh = (gX_F32)maindata.screenHeight;
        // Semi-transparent dark overlay
        gX_Rectangle overlay = {0, 0, sw, sh};
        gX_Rectangle_Draw(overlay, (gX_Color){0, 0, 0, 180});
        // Spinning dot indicator: rotate a dot around center using elapsed time
        gX_F64 t = maindata.timeElapsed;
        gX_F32 cx = sw * 0.5f;
        gX_F32 cy = sh * 0.5f - 20.0f;
        gX_F32 dotRadius = 6.0f;
        gX_F32 orbitR = 24.0f;
        gX_F32 angle = (gX_F32)fmod(t * 3.0, 2.0 * 3.14159265);
        gX_F32 dx = cx + cosf(angle) * orbitR;
        gX_F32 dy = cy + sinf(angle) * orbitR;
        gX_Circle dot = { {dx, dy}, dotRadius };
        gX_Circle_Draw(dot, (gX_Color){200, 200, 255, 220});
        // Pulsing center glow
        gX_F32 pulse = (gX_F32)(sin(t * 4.0) * 0.3 + 0.7);
        gX_Circle glow = { {cx, cy}, dotRadius * 1.5f };
        gX_U8 alpha = (gX_U8)(pulse * 100);
        gX_Circle_Draw(glow, (gX_Color){100, 100, 180, alpha});
        // Text
        gX_RawString_Draw_Lesser("Loading...",
            (gX_F32v2){cx - 44.0f, cy + 32.0f},
            24, (gX_Color){180, 180, 220, 255});
    }
    g_guiCanvasDrawCount = 0;
    g_guiElementDrawCount = 0;
    if (gui.directdraw.drawFPS) {
        gX_Drawing_FPS((gX_F32v2){16.0f,32.0f});
        //opencode: GPU timer overlay — draw ms per pass below FPS
#if gX_ENABLE_RENDERER == 1
        if (renderer.gfx.data) {
            gX_RendererGFX *g = (gX_RendererGFX *)renderer.gfx.data;
            if (g->timersInited && g->timerTotal.available) {
                gX_F32 gpuX = 60.0f;
                //opencode: draw "GPU:" label
                gX_RawString_Draw_Lesser("GPU:", (gX_F32v2){16.0f, 52.0f}, 10, (gX_Color){200, 200, 200, 200});
                //opencode: per-pass timing with bottleneck coloring
                //   green <1ms, yellow 1-4ms, red >4ms
                #define DRAW_TIMER_MS(name, val) do { \
                    gX_I8 tb[32]; gX_F32 v = (gX_F32)(val); \
                    snprintf(tb, sizeof(tb), "%s=%.1f ", (name), v); \
                    gX_Color tc = {0, 255, 0, 200}; \
                    if (v >= 4.0f) tc = (gX_Color){255, 0, 0, 200}; \
                    else if (v >= 1.0f) tc = (gX_Color){255, 255, 0, 200}; \
                    gX_RawString_Draw_Lesser(tb, (gX_F32v2){gpuX, 52.0f}, 10, tc); \
                    gpuX += (gX_F32)(strlen(name) + 1) * 7.0f + 22.0f; \
                } while(0)
                DRAW_TIMER_MS("shad", g->timerShadow.elapsedMs);
                DRAW_TIMER_MS("refl", g->timerReflections.elapsedMs);
                DRAW_TIMER_MS("opaq", g->timerOpaque.elapsedMs);
                DRAW_TIMER_MS("bloom", g->timerBloom.elapsedMs);
                DRAW_TIMER_MS("ssr", g->timerSSR.elapsedMs);
                DRAW_TIMER_MS("total", g->timerTotal.elapsedMs);
                #undef DRAW_TIMER_MS
            }
        }
#endif
    }

    //opencode: on-screen msg overlay — fade-out with error persistence
    // Respects SHOW_MSG flag: scripts can hide via SetMessagesVisible(false)
    if (gui.state.SHOW_MSG) {
        gX_F64 now = maindata.timeElapsed;
        gX_U32 start = msg.output.startpoint;
        gX_U32 count = 12;
        gX_F32 y = 64.0f;
        for (gX_U32 i = 0; i < count; i++)
        {
            gX_U32 idx = (start + 127 - i) % 128;
            if (msg.output.history[idx][0] == '\0') continue;

            gX_F64 age = now - msg.output.time[idx];
            gX_F64 fadeTime = 8.0;     // seconds before fade starts
            gX_F64 fadeDuration = 2.0; // seconds the fade lasts
            gX_I32 type = msg.output.type[idx];

            // Errors persist much longer
            if (type == gX_LOG_ERROR)
            {
                fadeTime = 55.0;
                fadeDuration = 5.0;
            }
            else if (type == gX_LOG_WARNING)
            {
                fadeTime = 20.0;
                fadeDuration = 3.0;
            }

            // Compute alpha
            gX_F32 alpha = 1.0f;
            if (age > fadeTime + fadeDuration)
            {
                continue; // fully faded — skip
            }
            else if (age > fadeTime)
            {
                alpha = (gX_F32)(1.0 - (age - fadeTime) / fadeDuration);
                if (alpha < 0.0f) continue;
            }

            gX_U8 a = (gX_U8)(alpha * 255.0f);
            gX_Color c;
            if (type == gX_LOG_ERROR)
                c = (gX_Color){255, 80, 80, a};
            else if (type == gX_LOG_WARNING)
                c = (gX_Color){255, 220, 80, a};
            else
                c = (gX_Color){255, 255, 255, a};

            gX_RawString_Draw_Lesser(msg.output.history[idx],
                (gX_F32v2){16.0f, y}, 20.0f, c);
            y += 22.0f;
        }
    } // end if SHOW_MSG
    
    //opencode: rooms overlay drawn directly to screen
    //opencode: gX — game scene (rooms) dropped; no overlay
    
    //opencode: element inspector overlay (F7 to toggle, click to pick)
    //inspector should disable update
    if (gui.inspectorEnabled) {
        gX_F32 sw = (gX_F32)maindata.screenWidth;
        gX_F32 sh = (gX_F32)maindata.screenHeight;
        // Dark semi-transparent overlay
        gX_Rectangle_Draw((gX_Rectangle){0, 0, sw, sh}, (gX_Color){0,0,0,140});
        // Highlight picked element
        if (gui.inspectorCanvasID >= 0 && gui.inspectorElementID >= 0) {
            gX_GUI_Canvas *c = gX_GUI_Canvas_FromIndex(gui.inspectorCanvasID);
            if (c && (gX_U32)gui.inspectorElementID < c->elements.amount) {
                gX_GUI_Element *e = &((gX_GUI_Element*)c->elements.data)[gui.inspectorElementID];
                gX_Rectangle r = e->rect;
                // Scale from canvas buffer coords to screen coords
                //opencode: E-7 — use the draw viewport, not the window size:
                //   letterboxed layouts render the GUI into a sub-viewport of
                //   the window, so windowWidth/Height misplaces the highlight.
                gX_F32 sx = (gX_F32)drawinfo.vpW / c->canvasBox.width;
                gX_F32 sy = (gX_F32)drawinfo.vpH / c->canvasBox.height;
                gX_Rectangle screenRect = {r.x * sx, r.y * sy, r.width * sx, r.height * sy};
                // Yellow highlight rect
                gX_Rectangle_Draw(screenRect, (gX_Color){255,255,0,60});
                gX_Rectangle_Parameters rp = {0};
                rp.lines = true;
                rp.colors[0] = (gX_Color){255,255,0,220};
                gX_Rectangle_Draw_Parameters(screenRect, rp);
                // Info panel at top-right
                gX_I8 info[512];
                gX_F32 panelW = 360.0f, panelH = 260.0f;
                gX_Rectangle infoBg = {sw - panelW - 10, 10, panelW, panelH};
                gX_Rectangle_Draw(infoBg, (gX_Color){20,20,40,240});
                gX_Rectangle_Parameters infoBorder = {0};
                infoBorder.lines = true;
                infoBorder.colors[0] = (gX_Color){100,100,180,200};
                gX_Rectangle_Draw_Parameters(infoBg, infoBorder);
                snprintf(info, sizeof(info),
                    "Inspector — canvas[%d] elem[%d]\n"
                    "  Canvas: %s\n"
                    "  Rect: (%.0f, %.0f, %.0f, %.0f)\n"
                    "  Color: (%d, %d, %d, %d)\n"
                    "  State: HVR=%d CLK=%d PRS=%d FOC=%d DIS=%d DRW=%d\n"
                    "  Rendering: centerXY=%d/%d shadow=%d wire=%d\n"
                    "  canvasHue: (%d,%d,%d,%d)\n"
                    "  canvasBox: (%.0f,%.0f,%.0f,%.0f)",
                    gui.inspectorCanvasID, gui.inspectorElementID,
                    c->name,
                    r.x, r.y, r.width, r.height,
                    e->color.r, e->color.g, e->color.b, e->color.a,
                    e->state.HOVERED, e->state.CLICKED_LEFT, e->state.PRESSED_LEFT,
                    e->state.FOCUSED, e->state.DISABLED, e->stateRendering.RECT_DRAW_WIRE,
                    e->stateRendering.TEXT_CENTER_X, e->stateRendering.TEXT_CENTER_Y,
                    e->stateRendering.TEXT_SHADOW, e->stateRendering.RECT_DRAW_WIRE,
                    c->canvasHue.r, c->canvasHue.g, c->canvasHue.b, c->canvasHue.a,
                    c->canvasBox.x, c->canvasBox.y, c->canvasBox.width, c->canvasBox.height);
                gX_RawString_Draw_Lesser(info,
                    (gX_F32v2){sw - panelW, 20}, 13, (gX_Color){200,220,255,255});
            }
        }
        // Help text at bottom
        gX_RawString_Draw_Lesser("[F7] close inspector  |  Left-click to pick element",
            (gX_F32v2){20, sh - 30}, 14, (gX_Color){180,180,200,200});
    }
    
    //opencode: v0.6.9 Phase B — dock controller (per-frame transition
    //   check, cheap). Widens to 16:9 + centers viewport + docks panels
    //   when ImGui tools open (margins>=200 achievable); restores 4:3 on
    //   close-all. Fullscreen + narrow monitors stay overlay (no widen).
    gX_GUI_Draw_ToolsOverlay();

    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE, "GUI Draw done")
    if (guiDiagState) {
        gX_GUI_Draw_Diagnostics_End(&guiDrawBefore, guiDiagState, "gui_draw");
    }
}

//opencode: D2/D4 debug overlays — element highlighter + canvas heat map
gX_I0 gX_GUI_DrawDebugOverlays(gX_I0) {
    if (!gui.directdraw.showElementOverlay && !gui.directdraw.showCanvasHeatmap) return;
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas*)gui.canvases.data;
    if (!canvases) return;
    for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
        gX_GUI_Canvas *c = &canvases[ci];
        if (gui.directdraw.showCanvasHeatmap) {
            gX_Color heatCol = {(gX_U8)((ci * 80) % 256), (gX_U8)((ci * 50 + 100) % 256), (gX_U8)((ci * 120 + 60) % 256), 30};
            gX_Rectangle_Draw(c->canvasBox, heatCol);
        }
        if (gui.directdraw.showElementOverlay && c->elements.amount > 0) {
            gX_GUI_Element *elems = (gX_GUI_Element*)c->elements.data;
            for (gX_U32 ei = 0; ei < c->elements.amount; ei++) {
                gX_GUI_Element *e = &elems[ei];
                gX_Color ovCol = {(gX_U8)((ci * 100) % 200 + 55), (gX_U8)((ei * 60) % 200 + 55), (gX_U8)((ci * 80 + ei * 40) % 200 + 55), 60};
                gX_Rectangle_Draw_Border(e->rect, ovCol);
                if (e->name[0]) {
                    gX_RawString_Draw_Lesser(e->name, (gX_F32v2){e->rect.x + 2, e->rect.y + 2}, 10.0f, ovCol);
                }
            }
        }
    }
    return;
}

gX_I0 gX_GUI_ShowToast(const gX_I8 *msg) {
    if (!msg || msg[0] == '\0') {
        gui.state.SHOW_MSG = false;
        gui.state.toastMessage[0] = '\0';
        return;
    }
    strncpy(gui.state.toastMessage, msg, sizeof(gui.state.toastMessage) - 1);
    gui.state.toastMessage[sizeof(gui.state.toastMessage) - 1] = '\0';
    gui.state.SHOW_MSG = true;
    //opencode: reset auto-dismiss timer — a fresh toast gets its full 3s even if
    //   the previous toast was already up for nearly its whole lifetime.
    gX_GUI_Toast_ResetTimer();
}
