/**********************************************************************************************
*
*   [gX: src/gui/update.c]
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


#include <math.h>

#include "gui.h"
#include "binding.h"
#include "markup.h"
#include "style.h"
#include "element/element.h"
#include "element/struct.h"
#include "../tools/tools.h"

#include "../main.h"
#include "../input.h"
#include "../common/enum/keys.h"

#include "../common/log.h"
#include "../gxmath/constants.h"

//opencode: widget callback contexts are owner-scoped now (builder.h); only the
//   scroll-list geometry registry needs an explicit reset on a full rebuild.
#include "builder.h"
#include "../common/profiler.h"

#include "../wrappers/wrappers.h"

//opencode: file-scope toast auto-dismiss timer so gX_GUI_ShowToast can reset it
//   when a new toast replaces an old one (was block-static — rapid toasts were
//   auto-dismissed almost instantly when the previous toast was near its 3s cap).
static gX_F32 g_toastTimer = 0.0f;

gX_I0 gX_GUI_Toast_ResetTimer(gX_I0) {
    g_toastTimer = 0.0f;
}

static gX_I32 gX_GUI_Update_ClearAll(gX_I0) {
    gX_I32 errors = 0;
    gX_GUI_Canvas *canvases = gui.canvases.data;
    gX_I1 allCanvasesCleared = true;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        gX_GUI_Canvas *canvas = &(canvases[i]);
        if (gui.doNotClearMenu && (i == (gX_U32)gui.menuCanvasIndex)) {
            allCanvasesCleared = false;
            continue;
        }
        if (gui.doNotClearDebugDisplay && (i == (gX_U32)gui.debugDisplayCanvasIndex)) {
            allCanvasesCleared = false;
            continue;
        }
        if (gui.doNotClearMessages && (i == (gX_U32)gui.messagesCanvasIndex)) {
            allCanvasesCleared = false;
            continue;
        }
        if (gui.doNotClearDialog && (i == (gX_U32)gui.dialogCanvasIndex)) {
            allCanvasesCleared = false;
            continue;
        }
        //opencode: removed else — else only bound to last if, caused unintended clears
        errors = gX_GUI_Canvas_Clear(canvas);
    }
    //opencode: a scene switch must never leave a confirm dialog behind —
    //   close it silently (no callback) so stale onOk pointers can't fire.
    if (gui.dialogOpen) {
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
    }
    //opencode: callback contexts are owner-scoped (stored per element data
    //   module), so a partial canvas refresh preserves them automatically. Only
    //   the scroll-list geometry registry is reset when every canvas clears.
    if (allCanvasesCleared) {
        gX_GUI_Builder_Widget_ScrollList_ResetRegistry();
        gX_GUI_Focus_Clear();
        gX_GUI_PointerCapture_Clear();
    }
    gui.clearAll = false;
    return errors;
}

static gX_I32 gX_GUI_Update_Make(gX_I0) {
    gX_I32 errors = 0;
    gX_GUI_Canvas *canvases = gui.canvases.data;
    gX_TraceLog_GUI(gX_LOG_TRACE2,"Making stuff");
    for (gX_U32 i = 0; i < gui.makeAmount; i++) {
        gX_TraceLog_GUI(gX_LOG_TRACE,"i = %i",i);
        gX_I32 canvastoken = gui.canvasToken[i];
        
        if (canvastoken < 0 || (gX_U32)canvastoken >= gui.canvases.amount) {
            gX_TraceLog_GUI(gX_LOG_ERROR, "Invalid canvas token %i (max %i)", canvastoken, gui.canvases.amount);
            continue;
        }
        gX_TraceLog_GUI(gX_LOG_TRACE,"canvastoken: %i",canvastoken);
        gX_GUI_Canvas *canvas = &(canvases[canvastoken]);
        
        gX_TraceLog_GUI(gX_LOG_TRACE,"Clearing canvas");
        errors = gX_GUI_Canvas_Clear(canvas);
        
        gX_TraceLog_GUI(gX_LOG_TRACE,"Making canvas");
        gX_GUI_Make(gui.makeToken[i],canvastoken);
        //opencode: rebuild invalidates element slots — clear focus if it pointed
        //   into this canvas so Enter/Space can't activate a different element
        //   that now occupies the stale slot.
        if (gui.focusCanvasID == canvastoken) {
            gX_GUI_Focus_Clear();
        }
        if (gui.modalCanvasID == canvastoken) gui.focusFirstPending = true;
        if (gui.pointerCaptureCanvasID == canvastoken) gX_GUI_PointerCapture_Clear();
    }
    gX_TraceLog_GUI(gX_LOG_TRACE2,"Making done");
    gui.makeAmount = 0;
    gui.make = false;
    return errors;
}

static gX_I32 gX_GUI_Update_Refresh(gX_I0) {
    gX_I32 errors = 0;
    gX_GUI_Canvas *canvases = gui.canvases.data;
    
    //opencode: deferred refresh gate — if a transition owns the refresh, bail
    //   so owning canvases are not rebuilt while their callbacks are attached.
    if (gui.transition.overrideRefresh) {
        if (!gui.transition.refreshNow) return errors;
    }
    
    //opencode: recyclable global state is limited to the scroll-list geometry
    //   registry; element-owned callback contexts survive partial refreshes.
    gX_I1 allCanvasesRefreshing = true;
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        if (!canvases[i].refresh) {
            allCanvasesRefreshing = false;
            break;
        }
    }
    if (allCanvasesRefreshing) {
        gX_GUI_Builder_Widget_ScrollList_ResetRegistry();
    }
    
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Refreshing stuff");
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        gX_TraceLog_GUI(gX_LOG_TRACE,"i = %i",i);
        gX_GUI_Canvas *canvas = &(canvases[i]);
        if (canvas->refresh) {
            gX_TraceLog_GUI(gX_LOG_TRACE,"Clearing canvas");
            errors = gX_GUI_Canvas_Clear(canvas);
            
            gX_TraceLog_GUI(gX_LOG_TRACE,"Making canvas");
            gX_GUI_Make(canvas->templateID,(gX_I32)i);
            
            canvas->refresh = false;
            //opencode: rebuild invalidates element slots — clear stale focus
            if (gui.focusCanvasID == (gX_I32)i) {
                gX_GUI_Focus_Clear();
            }
            if (gui.modalCanvasID == (gX_I32)i) gui.focusFirstPending = true;
            if (gui.pointerCaptureCanvasID == (gX_I32)i) gX_GUI_PointerCapture_Clear();
        }
    }
    gX_TraceLog_GUI(gX_LOG_DEBUG,"Refreshing done");
    gui.refresh = false;
    gui.transition.refreshNow = false; //opencode: reset so it doesn't leak into transitions
    return errors;
}

//opencode: inverse-transform a screen-space point into canvas buffer space
//   using the same transform the composite applies (offset, canvasScale,
//   canvasAngle — see gX_GUI_CompositeToScreen + gX_Texture_Draw: scale and
//   rotation pivot at the UNSCALED dst center). Pointer hit testing must run
//   in this space so SLIDE/ZOOM/SKEW visuals and their interaction targets
//   stay aligned. Returns false when the canvas has no buffer (elements draw
//   untransformed in screen space) or the transform is degenerate (zero scale
//   = collapsed canvas, nothing hittable). Exported for unit tests.
gX_I1 gX_GUI_Canvas_ToBufferPoint(const gX_GUI_Canvas *canvas, gX_F32v2 screen, gX_F32v2 *out) {
    if (!canvas || !canvas->buffer.loadingState.loaded) return false;
    gX_F32 dstW = canvas->canvasBox.width;
    gX_F32 dstH = canvas->canvasBox.height;
    gX_F32 bufW = (gX_F32)canvas->buffer.width;
    gX_F32 bufH = (gX_F32)canvas->buffer.height;
    if (dstW <= 0.0f || dstH <= 0.0f || bufW <= 0.0f || bufH <= 0.0f) return false;
    gX_F32 sx = canvas->canvasScale.x;
    gX_F32 sy = canvas->canvasScale.y;
    if (sx == 0.0f || sy == 0.0f) return false;
    gX_F32 dx0 = canvas->canvasBox.x + canvas->canvasOffset.x;
    gX_F32 dy0 = canvas->canvasBox.y + canvas->canvasOffset.y;
    gX_F32 cx = dx0 + dstW * 0.5f;
    gX_F32 cy = dy0 + dstH * 0.5f;
    //opencode: unrotate (standard R(θ) with θ in degrees, y-down screen space)
    gX_F32 a = -canvas->canvasAngle * (gX_F32)gX_DEG2RAD;
    gX_F32 cosA = cosf(a), sinA = sinf(a);
    gX_F32 rx = (screen.x - cx) * cosA - (screen.y - cy) * sinA;
    gX_F32 ry = (screen.x - cx) * sinA + (screen.y - cy) * cosA;
    rx /= sx;
    ry /= sy;
    out->x = (cx + rx - dx0) * (bufW / dstW);
    out->y = (cy + ry - dy0) * (bufH / dstH);
    return true;
}

//ai: exact inverse of ToBufferPoint — buffer point to screen. Same transform
//   pieces (canvasOffset, canvasScale, canvasAngle, unscaled dst-center
//   pivot); false when the canvas has no buffer or the scale is degenerate.
gX_I1 gX_GUI_Canvas_ToScreenPoint(const gX_GUI_Canvas *canvas, gX_F32v2 buffer, gX_F32v2 *out) {
    if (!canvas || !canvas->buffer.loadingState.loaded) return false;
    gX_F32 dstW = canvas->canvasBox.width;
    gX_F32 dstH = canvas->canvasBox.height;
    gX_F32 bufW = (gX_F32)canvas->buffer.width;
    gX_F32 bufH = (gX_F32)canvas->buffer.height;
    if (dstW <= 0.0f || dstH <= 0.0f || bufW <= 0.0f || bufH <= 0.0f) return false;
    gX_F32 sx = canvas->canvasScale.x;
    gX_F32 sy = canvas->canvasScale.y;
    if (sx == 0.0f || sy == 0.0f) return false;
    gX_F32 dx0 = canvas->canvasBox.x + canvas->canvasOffset.x;
    gX_F32 dy0 = canvas->canvasBox.y + canvas->canvasOffset.y;
    gX_F32 cx = dx0 + dstW * 0.5f;
    gX_F32 cy = dy0 + dstH * 0.5f;
    gX_F32 rx = (buffer.x * (dstW / bufW)) - cx + dx0;
    gX_F32 ry = (buffer.y * (dstH / bufH)) - cy + dy0;
    gX_F32 ux = rx * sx;
    gX_F32 uy = ry * sy;
    gX_F32 a = canvas->canvasAngle * (gX_F32)gX_DEG2RAD;
    gX_F32 cosA = cosf(a), sinA = sinf(a);
    out->x = cx + ux * cosA - uy * sinA;
    out->y = cy + ux * sinA + uy * cosA;
    return true;
}

//opencode: Reverse + early exit on first hit for correct topmost Z-order picking
static gX_I0 gX_GUI_OcclusionCheck_Update(gX_GUI_OcclusionCheck *oc) {
    //opencode: engine mouse already owns game coords (Update_Mouse maps).
    gX_F32v2 mouseCoord = input.mouse.coord;
    gX_GUI_Canvas *canvases = gui.canvases.data;
    gX_Heap_Meta *canvasmeta = gui.canvases.meta;
    gX_I1 insideRect = false;
    gX_I1 found = false;
    
    gui.occlusion.rect = (gX_Rectangle){0.0f,0.0f,1.0f,1.0f};
    gui.occlusion.canvasID = -1;
    gui.occlusion.elementID = -1;
    gui.occlusion.elementPtr = NULL;
    gui.occlusion.canvasPtr = NULL;

    //opencode: pointer capture — a captured drag owns the pointer even when
    //   it leaves the element rect. Released buttons drop the capture.
    if (gui.pointerCaptureActive) {
        if (!input.mouse.leftButton.DOWN) {
            gX_GUI_PointerCapture_Clear();
        } else if (gui.pointerCaptureCanvasID >= 0
            && (gX_U32)gui.pointerCaptureCanvasID < gui.canvases.amount
            && gX_GUI_Canvas_InputAllowed(gui.pointerCaptureCanvasID)) {
            gX_GUI_Canvas *captureCanvas = &canvases[gui.pointerCaptureCanvasID];
            if (gX_GUI_Canvas_Element_Eligible(captureCanvas,
                (gX_U32)gui.pointerCaptureElementID)) {
                oc->canvasID = gui.pointerCaptureCanvasID;
                oc->elementID = gui.pointerCaptureElementID;
                oc->elementPtr = &((gX_GUI_Element*)captureCanvas->elements.data)[gui.pointerCaptureElementID];
                oc->canvasPtr = captureCanvas;
                oc->rect = oc->elementPtr->rect;
                return;
            }
            gX_GUI_PointerCapture_Clear();
        } else {
            gX_GUI_PointerCapture_Clear();
        }
    }

    //opencode: reverse iteration N→0, early-exit on first hit (topmost element wins)
    for (gX_U32 i = gui.canvases.amount; i > 0; i--) {
        gX_U32 idx = i - 1;
        gX_GUI_Canvas *canvas = &(canvases[idx]);
        gX_GUI_Element *elements = canvas->elements.data;

        if (!canvasmeta || !(canvasmeta[idx].UPDATE)) continue;
        if (!gX_GUI_Canvas_InputAllowed((gX_I32)idx)) continue;

        //opencode: canvas transform for hit testing — buffered canvases test
        //   the mouse in buffer space (offset/scale/angle applied); no-buffer
        //   canvases draw elements untransformed in screen space.
        gX_F32v2 testCoord = mouseCoord;
        gX_I1 hasBuffer = canvas->buffer.loadingState.loaded;
        if (hasBuffer) {
            if (!gX_GUI_Canvas_ToBufferPoint(canvas, mouseCoord, &testCoord)) continue;
        }

        //opencode: drawing uses stable layer order, not raw heap order. Pick
        //   in its reverse order so the visually topmost eligible element wins.
        gX_U32 drawOrder[gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS];
        gX_U32 drawCount = 0;
        gX_GUI_Canvas_BuildDrawOrder(canvas, drawOrder, gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS, &drawCount);
        for (gX_U32 k = drawCount; k > 0; k--) {
            gX_U32 kdx = drawOrder[k - 1];
            //opencode: audit — only live, visible, enabled elements
            //   participate in picking. Removed slots (USED=false) and
            //   hidden/disabled controls can neither occlude nor receive
            //   clicks; a trailing removed slot can no longer act as a
            //   topmost 1x1 occluder at the canvas origin.
            if (!gX_GUI_Canvas_Element_Eligible(canvas, kdx)) continue;
            gX_GUI_Element *element = &(elements[kdx]);
            gX_Rectangle rect = element->rect;
            
            if (element->state.IGNORE_OCCLUSION) continue;
            //opencode: hit test in canvas buffer space — the inverse composite
            //   transform keeps SLIDE/ZOOM/SKEW visuals and their targets aligned
            insideRect = gX_Rectangle_Point_CheckCollision(rect,testCoord);
            if (insideRect) {
                gui.occlusion.rect = rect;
                gui.occlusion.canvasID = (gX_I32)idx;
                gui.occlusion.elementID = (gX_I32)kdx;
                gui.occlusion.elementPtr = element;
                gui.occlusion.canvasPtr = canvas;
                found = true;
                break;
            }
        }
        if (found) break;
    }
}

extern gX_I0 gX_GUI_Respond(gX_I0);
extern gX_I0 gX_GUI_Canvas_Transition_Update(gX_GUI_Canvas_Transition *tra);

//opencode: horizontal arrows adjust focused native sliders; Page keys use a larger step.
static gX_I1 gX_GUI_Focus_AdjustSlider(gX_GUI_Element *element,
    gX_I32 direction, gX_I1 coarse)
{
    gX_GUI_Element_Data *data;
    gX_F32 *value;
    gX_F32 range;
    gX_F32 screenRange;
    gX_F32 step;
    gX_F32 next;
    if (!element || element->type != gX_GUI_ELEMENT_TYPE_SLIDER) return false;
    data = gX_GUI_Element_Data_Get(element, 0);
    if (!data || !data->usePotentio || data->dst.datatype != gX_DATATYPE_F32
        || !data->dst.ptr) return true;
    value = (gX_F32*)data->dst.ptr;
    range = data->potentio.max.y - data->potentio.min.y;
    if (!isfinite(range) || range <= 0.0f || !isfinite(*value)) return true;
    step = range * (coarse ? 0.2f : 0.05f);
    next = *value + (direction < 0 ? -step : step);
    if (next < data->potentio.min.y) next = data->potentio.min.y;
    if (next > data->potentio.max.y) next = data->potentio.max.y;
    *value = next;
    screenRange = data->potentio.max.x - data->potentio.min.x;
    if (screenRange > 0.0f) {
        data->potentio.reg.x = data->potentio.min.x
            + ((next - data->potentio.min.y) / range) * screenRange;
    }
    return true;
}

//opencode: an active ImGui tool owns native GUI input to avoid key and click-through.
gX_I0 gX_GUI_Update(gX_I0) {

    gX_GUI_Canvas *canvases = gui.canvases.data;
    gX_Heap_Meta *canvasesMeta = gui.canvases.meta;
    gui.toolsInputCaptured = gX_Tools_RuntimeActive();
    
    //opencode: markup live-reload — rate-limited mtime watch on the registered
    //   markup file (refreshes markup canvases on change)
    gX_GUI_Markup_CheckReload();
    
    //opencode: design-token live-reload — theme.json mtime watch (rate-limited);
    //   rebuilds the CSS default sheet + canvases when the theme changes
    gX_GUI_Style_CheckReload();
    
    //opencode: check buffer pool before allocating new GPU RTs (recycles after resize)
    if (maindata.backendLoaded && gui.buffer.loadingState.load && !(gui.buffer.loadingState.loaded)) {
        gX_RenderTexture_Wrapper pooled = gX_GUI_BufferPool_Fetch(gui.buffer.width, gui.buffer.height, true);
        if (pooled.loadingState.loaded) {
            gui.buffer = pooled;
        } else {
            gui.buffer.rt = gX_RenderTexture_Load_HDR(gui.buffer.width,gui.buffer.height);
        }
        gui.buffer.loadingState.load = false;
        gui.buffer.loadingState.loaded = true;
    }
    if (maindata.backendLoaded && gui.ping.loadingState.load && !(gui.ping.loadingState.loaded)) {
        gX_RenderTexture_Wrapper pooled = gX_GUI_BufferPool_Fetch(gui.ping.width, gui.ping.height, true);
        if (pooled.loadingState.loaded) {
            gui.ping = pooled;
        } else {
            gui.ping.rt = gX_RenderTexture_Load_HDR(gui.ping.width,gui.ping.height);
        }
        gui.ping.loadingState.load = false;
        gui.ping.loadingState.loaded = true;
    }
    if (maindata.backendLoaded && gui.pong.loadingState.load && !(gui.pong.loadingState.loaded)) {
        gX_RenderTexture_Wrapper pooled = gX_GUI_BufferPool_Fetch(gui.pong.width, gui.pong.height, true);
        if (pooled.loadingState.loaded) {
            gui.pong = pooled;
        } else {
            gui.pong.rt = gX_RenderTexture_Load_HDR(gui.pong.width,gui.pong.height);
        }
        gui.pong.loadingState.load = false;
        gui.pong.loadingState.loaded = true;
    }
    if (maindata.backendLoaded && gui.composite.background.loadingState.load && !(gui.composite.background.loadingState.loaded)) {
        gX_RenderTexture_Wrapper pooled = gX_GUI_BufferPool_Fetch(gui.composite.background.width, gui.composite.background.height, true);
        if (pooled.loadingState.loaded) {
            gui.composite.background = pooled;
        } else {
            gui.composite.background.rt = gX_RenderTexture_Load_HDR(gui.composite.background.width,gui.composite.background.height);
        }
        gui.composite.background.loadingState.load = false;
        gui.composite.background.loadingState.loaded = true;
    }
    //opencode: on resize/refresh, recreate GUI buffers with new render dimensions
    if (maindata.refreshOngoing) {
        //opencode: stash old buffers in pool instead of GPU free → reuse on next alloc
        if (gui.ping.loadingState.loaded)
            gX_GUI_BufferPool_Stash(gui.ping, gui.ping.width, gui.ping.height, true);
        if (gui.pong.loadingState.loaded)
            gX_GUI_BufferPool_Stash(gui.pong, gui.pong.width, gui.pong.height, true);
        if (gui.buffer.loadingState.loaded)
            gX_GUI_BufferPool_Stash(gui.buffer, gui.buffer.width, gui.buffer.height, true);
        if (gui.composite.background.loadingState.loaded)
            gX_GUI_BufferPool_Stash(gui.composite.background, gui.composite.background.width, gui.composite.background.height, true);
        //opencode: ping/pong at screen resolution for 1:1 accumulation; buffer stays at render res for models
        gui.ping.width = maindata.screenWidth;
        gui.ping.height = maindata.screenHeight;
        gui.pong.width = maindata.screenWidth;
        gui.pong.height = maindata.screenHeight;
        gui.buffer.width = maindata.renderWidth;
        gui.buffer.height = maindata.renderHeight;
        gui.composite.background.width = maindata.screenWidth;
        gui.composite.background.height = maindata.screenHeight;
        gui.ping.loadingState = (gX_LoadingState){0};
        gui.pong.loadingState = (gX_LoadingState){0};
        gui.buffer.loadingState = (gX_LoadingState){0};
        gui.composite.background.loadingState = (gX_LoadingState){0};
        gui.ping.loadingState.load = true;
        gui.pong.loadingState.load = true;
        gui.buffer.loadingState.load = true;
        gui.composite.background.loadingState.load = true;
        gui.composite.backgroundValid = false;
        // Mark all canvas buffers for reload
        //opencode: reuse outer 'canvases' instead of redeclaring
        for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
            gX_GUI_Canvas *c = &canvases[i];
            //opencode: update canvasBox to match new screen dimensions so the canvas
            //   buffer is recreated at the correct size and composited full-screen.
            //   Without this, canvasBox stays at the initial load-time window size.
            c->canvasBox.width = (gX_F32)maindata.screenWidth;
            c->canvasBox.height = (gX_F32)maindata.screenHeight;
            if (c->buffer.loadingState.loaded) {
                gX_GUI_BufferPool_Stash(c->buffer, c->buffer.width, c->buffer.height, false);
                c->buffer.loadingState.loaded = false;
                c->buffer.loadingState.load = true;
            }
        }
    }
    
    //opencode: deferred sub-template fade — set by title2_tp_cb when a button click
    //   triggers a sub-template switch. Transition_Fade cannot be called synchronously
    //   from within the data module update (element iteration) without crashing, so
    //   the callback sets a flag and we configure the transition here, safely outside
    //   the element update loop and before the state machine runs.
    if (gui.state.SUBTEMPLATE_FADE) {
        gui.state.SUBTEMPLATE_FADE = false;
        gX_GUI_Transition_Fade(0.15f, 0.0f, 0.15f, 0, 0);
        gui.transition.doRefreshStartMiddle = true;
        gui.transition.overrideRefresh = true;
    }
    
    if (maindata.masterIndexSwitched) {
        gX_TraceLog_GUI(gX_LOG_DEBUG,"maindata.masterIndexSwitched");
        gX_GUI_Respond();
    }
    
    //opencode: toast auto-dismiss after 3 seconds, click-to-dismiss
    {
        if (gui.state.SHOW_MSG) {
            g_toastTimer += (gX_F32)gX_FrameTime_Get();
            //opencode: click anywhere to dismiss toast immediately
            if (input.mouse.leftButton.PRESSED) {
                gui.state.SHOW_MSG = false;
                g_toastTimer = 0.0f;
                gui.state.toastMessage[0] = '\0';
                //opencode: a dismiss click belongs to the toast overlay. Do
                //   not let the same press fall through to a control behind it.
                input.mouse.leftButton.PRESSED = false;
            } else if (g_toastTimer > 3.0f) {
                gui.state.SHOW_MSG = false;
                g_toastTimer = 0.0f;
                gui.state.toastMessage[0] = '\0';
            }
        } else {
            g_toastTimer = 0.0f;
        }
    }

    //opencode: pause menu fade animation
    static gX_I1 prevShowMenu = false;
    static gX_I1 prevShowMsg = false;
    static gX_F32 menuAnimTimer = 0.0f;
    gX_U32 menuCanvasIndex = (gX_U32)gui.menuCanvasIndex;
    if (menuCanvasIndex < gui.canvases.amount) {
        gX_GUI_Canvas *mc = &canvases[menuCanvasIndex];
        if (gui.state.SHOW_MENU) {
            if (canvasesMeta) canvasesMeta[menuCanvasIndex].UPDATE = true;
            if (canvasesMeta) canvasesMeta[menuCanvasIndex].DRAW = true;
            if (!prevShowMenu) {
                gX_GUI_Modal_Open((gX_I32)menuCanvasIndex, false);
                //opencode: rebuild escape menu when it opens
                gX_GUI_AddMake(gX_GUI_TEMPLATE_ESCAPEMENU, (gX_I32)menuCanvasIndex);
                gui.focusFirstPending = true;
                menuAnimTimer = 0.0f;
                mc->canvasHue.a = 0;
            }
            //opencode: reduced motion — snap the pause overlay to full opacity
            //   instead of fading (a11y motionReduced token)
            if (gui.style.a11y.motionReduced) {
                mc->canvasHue.a = 255;
            }
            //opencode: fade rate follows motionShort (default 150ms ≈ 1700/s).
            //   Timer clamped to [0,255] so the close path below can't wrap
            //   a gX_U8 alpha cast (menu reopened after long open time).
            else if (mc->canvasHue.a < 255) {
                gX_F32 fadeMs = gui.style.a11y.motionShort > 0.0f ? gui.style.a11y.motionShort : 150.0f;
                menuAnimTimer += (gX_F32)gX_FrameTime_Get() * (255.0f / (fadeMs / 1000.0f));
                if (menuAnimTimer > 255.0f) menuAnimTimer = 255.0f;
                mc->canvasHue.a = (gX_U8)((gX_I32)menuAnimTimer);
            }
        }
        else {
            if (prevShowMenu) {
                //opencode: reduced motion — snap the pause overlay away
                if (gui.style.a11y.motionReduced) {
                    menuAnimTimer = 0.0f;
                    mc->canvasHue.a = 0;
                }
                //opencode: fade out before hiding
                else if (mc->canvasHue.a > 0) {
                    gX_F32 fadeMs = gui.style.a11y.motionShort > 0.0f ? gui.style.a11y.motionShort : 150.0f;
                    menuAnimTimer -= (gX_F32)gX_FrameTime_Get() * (255.0f / (fadeMs / 1000.0f));
                    if (menuAnimTimer < 0.0f) menuAnimTimer = 0.0f;
                    mc->canvasHue.a = (gX_U8)((gX_I32)menuAnimTimer);
                }
            }
            if (mc->canvasHue.a == 0 || !prevShowMenu) {
                if (canvasesMeta) canvasesMeta[menuCanvasIndex].UPDATE = false;
                if (canvasesMeta) canvasesMeta[menuCanvasIndex].DRAW = false;
                if (gui.modalCanvasID == (gX_I32)menuCanvasIndex) gX_GUI_Modal_Close();
            }
        }
        prevShowMenu = gui.state.SHOW_MENU ? true : false;
    }
    
    gX_U32 debugDisplayCanvasIndex = (gX_U32)gui.debugDisplayCanvasIndex;
    if (debugDisplayCanvasIndex < gui.canvases.amount) {
        if (gui.state.SHOW_DEBUGDISPLAY) {
            if (canvasesMeta) canvasesMeta[debugDisplayCanvasIndex].UPDATE = true;
            if (canvasesMeta) canvasesMeta[debugDisplayCanvasIndex].DRAW = true;
            //opencode: v0.4.1 — rebuild every frame so the stats rows
            //   (fps/scene/screen/canvases) carry live values
            canvases[debugDisplayCanvasIndex].refresh = true;
        }
        else {
            if (canvasesMeta) canvasesMeta[debugDisplayCanvasIndex].UPDATE = false;
            if (canvasesMeta) canvasesMeta[debugDisplayCanvasIndex].DRAW = false;
        }
    }
    
    gX_U32 messagesCanvasIndex = (gX_U32)gui.messagesCanvasIndex;
    if (messagesCanvasIndex < gui.canvases.amount) {
        if (gui.state.SHOW_MSG) {
            if (canvasesMeta) canvasesMeta[messagesCanvasIndex].UPDATE = true;
            if (canvasesMeta) canvasesMeta[messagesCanvasIndex].DRAW = true;
            //opencode: v0.4.1 — rebuild on the toast RISING EDGE so the
            //   baked text carries the current toastMessage (the template
            //   builds once at load, before any toast exists)
            if (!prevShowMsg) canvases[messagesCanvasIndex].refresh = true;
            prevShowMsg = true;
        }
        else {
            if (canvasesMeta) canvasesMeta[messagesCanvasIndex].UPDATE = false;
            if (canvasesMeta) canvasesMeta[messagesCanvasIndex].DRAW = false;
            prevShowMsg = false;
        }
    }
    
    //opencode: if both transition and refresh are requested, let transition
    //block the refresh until the out-phase completes (prevents canvas rebuild before fade-out)
    if (gui.transition.execute && gui.refresh) {
        gui.transition.enabled = true;
        gui.transition.overrideRefresh = true;
    }
    
    if (gui.clearAll)   { gX_GUI_Update_ClearAll(); }
    if (gui.make)       { GX_PROFILE_ZONE_BEGIN("gui:make"); gX_GUI_Update_Make(); GX_PROFILE_ZONE_END("gui:make"); }
    //opencode: v0.4.0 — run the refresh pass when ANY canvas requests it.
    //   The old gate `if (gui.refresh)` meant callbacks that set only
    //   canvas->refresh (TitleSaves/TitleBack sub-view swap, menu/settings
    //   rebuilds in binding.c) NEVER rebuilt — the LOAD GAME sub-view was
    //   dead. Widget pools set both flags so they worked; the bindings did
    //   not. (The transition's deferred-refresh gate inside Update_Refresh
    //   still governs when a transition owns the rebuild.)
    {
        gX_I1 anyCanvasRefresh = false;
        gX_GUI_Canvas *rcanvases = gui.canvases.data;
        if (rcanvases) {
            for (gX_U32 ri = 0; ri < gui.canvases.amount; ri++) {
                if (rcanvases[ri].refresh) { anyCanvasRefresh = true; break; }
            }
        }
        if (gui.refresh || anyCanvasRefresh) {
            GX_PROFILE_ZONE_BEGIN("gui:refresh");
            gX_GUI_Update_Refresh();
            GX_PROFILE_ZONE_END("gui:refresh");
        }
    }

    //opencode: unified transition — single state machine (old System A + callback-based fade)
    if (gui.transition.enabled && gui.transition.execute) {
        gX_GUI_Canvas_Transition_Update(&gui.transition);
    }

    if (gui.focusFirstPending) {
        gX_GUI_Focus_First();
        gui.focusFirstPending = false;
    }

    GX_PROFILE_ZONE_BEGIN("gui:occlusion");
    gX_GUI_OcclusionCheck_Update(&gui.occlusion);
    GX_PROFILE_ZONE_END("gui:occlusion");

    //opencode: scroll lists — wheel over the list and thumb drag run on
    //   hover (no focus needed); both request a rebuild when moved.
    if (!gui.toolsInputCaptured) {
        gX_GUI_ScrollList_UpdateWheel();
        gX_GUI_ScrollList_UpdateDrag();
    }

    //opencode: click-to-focus through the centralized helper so state bits
    //   and focus IDs cannot diverge. A modal scopes outside clicks: they
    //   either dismiss it or are swallowed, never reaching background.
    if (gui.focusEnabled && !gui.toolsInputCaptured
        && (input.mouse.leftButton.PRESSED || input.mouse.rightButton.PRESSED)) {
        gX_I1 clickedOnFocusable = false;
        if (gui.occlusion.canvasID >= 0 && gui.occlusion.elementID >= 0) {
            clickedOnFocusable = gX_GUI_Focus_Set(gui.occlusion.canvasID,
                gui.occlusion.elementID);
        }
        if (!clickedOnFocusable) {
            if (gui.modalCanvasID >= 0) {
                if (gui.modalDismissOnOutside) gX_GUI_Modal_Close();
            } else {
                gX_GUI_Focus_Clear();
            }
        }
    }

    if (!gui.toolsInputCaptured && input.mouse.leftButton.PRESSED && gui.occlusion.canvasID >= 0
        && gui.occlusion.elementID >= 0) {
        gX_GUI_Element *oe = gui.occlusion.elementPtr;
        if (oe && oe->state.DRAGGABLE_LEFT) {
            gX_GUI_PointerCapture_Begin(gui.occlusion.canvasID, gui.occlusion.elementID);
        }
    }

    //opencode: keyboard focus update — Tab/Shift+Tab cycling, Enter/Space activation
    //opencode: gamepad D-pad navigation — Up/Down cycle focus, A button activates
    if (gui.focusEnabled && !gui.toolsInputCaptured) {
        gX_I1 tabPressed = input.keyboard.keys[gX_KEY_TAB].state.PRESSED;
        gX_I1 shiftDown = input.keyboard.keys[gX_KEY_LEFT_SHIFT].state.DOWN
                       || input.keyboard.keys[gX_KEY_RIGHT_SHIFT].state.DOWN;
        gX_I1 enterPressed = input.keyboard.keys[gX_KEY_ENTER].state.PRESSED
                          || input.keyboard.keys[gX_KEY_KP_ENTER].state.PRESSED;
        gX_I1 spacePressed = input.keyboard.keys[gX_KEY_SPACE].state.PRESSED;
        //opencode: keyboard arrow keys for focus navigation
        gX_I1 keyUp    = input.keyboard.keys[gX_KEY_UP].state.PRESSED;
        gX_I1 keyDown  = input.keyboard.keys[gX_KEY_DOWN].state.PRESSED;
        gX_I1 keyLeft  = input.keyboard.keys[gX_KEY_LEFT].state.PRESSED;
        gX_I1 keyRight = input.keyboard.keys[gX_KEY_RIGHT].state.PRESSED;
        gX_I1 keyPgUp  = input.keyboard.keys[gX_KEY_PAGE_UP].state.PRESSED;
        gX_I1 keyPgDn  = input.keyboard.keys[gX_KEY_PAGE_DOWN].state.PRESSED;
        gX_I1 keyHome  = input.keyboard.keys[gX_KEY_HOME].state.PRESSED;
        gX_I1 keyEnd   = input.keyboard.keys[gX_KEY_END].state.PRESSED;
        //opencode: scroll lists consume list nav keys on focused items so
        //   arrows move the selection instead of leaving the list.
        gX_I1 listNav = gX_GUI_ScrollList_HandleNavKeys(keyUp, keyDown,
            keyPgUp, keyPgDn, keyHome, keyEnd);
        if (listNav) {
            keyUp = false;
            keyDown = false;
            keyPgUp = false;
            keyPgDn = false;
            keyHome = false;
            keyEnd = false;
        }
        // Gamepad D-pad and action buttons (read from first connected gamepad)
        gX_I1 gpConnected = input.gamepad[0].available;
        gX_I1 dpadUp    = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_UP].PRESSED;
        gX_I1 dpadDown  = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_DOWN].PRESSED;
        gX_I1 dpadLeft  = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_LEFT].PRESSED;
        gX_I1 dpadRight = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_LEFT_FACE_RIGHT].PRESSED;
        gX_I1 gpActivate = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_RIGHT_FACE_DOWN].PRESSED;
        gX_I1 gpCancel   = gpConnected && input.gamepad[0].buttons[gX_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT].PRESSED;

        //opencode: gamepad B button → cancel/back (Esc equivalent)
        if (gpCancel) {
            if (gX_GUI_Dialog_IsOpen()) {
                gX_GUI_Dialog_Close(false);
            } else if (maindata.exitRequested) {
                maindata.exitRequested = false;
                gui.state.toastMessage[0] = '\0';
            } else if (!gX_GUI_Handle_Back() && gui.state.SHOW_MENU) {
                gui.state.SHOW_MENU = false;
                gX_GUI_Focus_Clear();
            }
        }

        //opencode: when a textinput is focused, arrow keys move the caret —
        //   don't let the focus navigator steal them (Tab still cycles).
        gX_I1 focusedIsTextInput = false;
        gX_GUI_Element *focusedElement = NULL;
        if (gui.focusCanvasID >= 0 && gui.focusElementID >= 0 &&
            gui.focusCanvasID < (gX_I32)gui.canvases.amount) {
            gX_GUI_Canvas *fc = &canvases[gui.focusCanvasID];
            if (fc && (gX_U32)gui.focusElementID < fc->elements.amount) {
                focusedElement = &((gX_GUI_Element*)fc->elements.data)[gui.focusElementID];
                focusedIsTextInput = (focusedElement->type == gX_GUI_ELEMENT_TYPE_TEXTINPUT);
            }
        }
        if (focusedElement && focusedElement->type == gX_GUI_ELEMENT_TYPE_SLIDER) {
            if (keyLeft) {
                gX_GUI_Focus_AdjustSlider(focusedElement, -1, shiftDown);
                keyLeft = false;
            }
            if (keyRight) {
                gX_GUI_Focus_AdjustSlider(focusedElement, 1, shiftDown);
                keyRight = false;
            }
            if (keyPgDn) {
                gX_GUI_Focus_AdjustSlider(focusedElement, -1, true);
                keyPgDn = false;
            }
            if (keyPgUp) {
                gX_GUI_Focus_AdjustSlider(focusedElement, 1, true);
                keyPgUp = false;
            }
        }

        if (tabPressed || enterPressed || spacePressed
            || (!focusedIsTextInput && (keyUp || keyDown || keyLeft || keyRight))
            || dpadUp || dpadDown || dpadLeft || dpadRight || gpActivate) {
            //pass 1: count total focusable elements across all canvases, build virtual index
            gX_U32 totalFocusable = 0;
            gX_I32 focusIndex = -1; // virtual index of current focus

            for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
                gX_GUI_Canvas *c = &canvases[ci];
                if (!gX_GUI_Canvas_InputAllowed((gX_I32)ci)) continue;
                if (!gui.canvases.meta[ci].UPDATE) continue;
                gX_GUI_Element *elems = c->elements.data;
                for (gX_U32 ei = 0; ei < c->elements.amount; ei++) {
                    //opencode: audit — focus traversal visits only live
                    //   (USED), visible, enabled, focusable elements.
                    if (elems[ei].state.FOCUSABLE && gX_GUI_Canvas_Element_Eligible(c, ei)) {
                        if ((gX_I32)ci == gui.focusCanvasID && (gX_I32)ei == gui.focusElementID) {
                            focusIndex = (gX_I32)totalFocusable;
                        }
                        totalFocusable++;
                    }
                }
            }

            if (totalFocusable == 0) {
                gX_GUI_Focus_Clear();
            } else if (enterPressed || spacePressed || gpActivate) {
                //activate focused element if exists (also on gamepad A-button)
                if (gui.focusCanvasID >= 0 && gui.focusElementID >= 0) {
                    gX_GUI_Canvas *fc = &canvases[gui.focusCanvasID];
                    //opencode: verify the focused element is still valid (canvas
                    //   updated, element still focusable + live/visible/enabled)
                    //   before firing a click — stale focus after a rebuild or a
                    //   hidden/disabled control must never activate.
                    if (fc && gX_GUI_Canvas_InputAllowed(gui.focusCanvasID)
                        && gui.canvases.meta[gui.focusCanvasID].UPDATE
                        && (gX_U32)gui.focusElementID < fc->elements.amount) {
                        gX_GUI_Element *fe = &((gX_GUI_Element*)fc->elements.data)[gui.focusElementID];
                        if (fe->state.FOCUSABLE
                            && gX_GUI_Canvas_Element_Eligible(fc, (gX_U32)gui.focusElementID)
                            && !fc->editLock) {
                            gX_GUI_Focus_Set(gui.focusCanvasID, gui.focusElementID);
                            fe->state.CLICKED_LEFT = true;
                        }
                    }
                }
            } else if (tabPressed || keyUp || keyDown || keyLeft || keyRight
                    || dpadDown || dpadRight || dpadUp || dpadLeft) {
                //cycle to next/prev
                gX_I32 nextFocus = focusIndex;
                // D-pad/arrow up/left = previous, down/right = next
                gX_I1 cyclePrev = shiftDown || dpadUp || dpadLeft || keyUp || keyLeft;
                if (cyclePrev) {
                    nextFocus = (focusIndex <= 0) ? (gX_I32)totalFocusable - 1 : focusIndex - 1;
                } else {
                    nextFocus = (focusIndex < 0 || focusIndex >= (gX_I32)totalFocusable - 1) ? 0 : focusIndex + 1;
                }

                //walk to find the element at nextFocus
                gX_I32 counter = -1;
                gX_I1 found = false;
                for (gX_U32 ci = 0; ci < gui.canvases.amount && !found; ci++) {
                    gX_GUI_Canvas *c = &canvases[ci];
                    if (!gX_GUI_Canvas_InputAllowed((gX_I32)ci)) continue;
                    if (!gui.canvases.meta[ci].UPDATE) continue;
                    gX_GUI_Element *elems = c->elements.data;
                    for (gX_U32 ei = 0; ei < c->elements.amount && !found; ei++) {
                        if (elems[ei].state.FOCUSABLE && gX_GUI_Canvas_Element_Eligible(c, ei)) {
                            counter++;
                            if (counter == nextFocus) {
                                gX_GUI_Focus_Set((gX_I32)ci, (gX_I32)ei);
                                found = true;
                            }
                        }
                    }
                }
            }
        }
    }

    //opencode: auto-focus first FOCUSABLE element on scene change
    //   Runs AFTER Make/Refresh so new canvases have their elements
    if (maindata.masterIndexSwitched) {
        gX_GUI_Focus_Clear();
    }

    if (canvases) {
        GX_PROFILE_ZONE_BEGIN("gui:canvas_updates");
        for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
            gX_GUI_Canvas *canvas = &canvases[i];
            if (gui.canvases.meta[i].UPDATE) {
                gX_GUI_Canvas_Update(canvas);
            }
        }
        GX_PROFILE_ZONE_END("gui:canvas_updates");
    }

    if (maindata.masterIndexSwitched && !gX_GUI_Modal_IsOpen()) gX_GUI_Focus_First();
    if (!input.mouse.leftButton.DOWN) gX_GUI_PointerCapture_Clear();

}
