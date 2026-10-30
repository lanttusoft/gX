/**********************************************************************************************
*
*   [gX: src/gui/canvas.c]
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

#include "gui.h"
#include "canvas.h"
#include "widgetsdf.h"
#include "layout.h"
#include "element/element.h"
#include "../ui/layout_debug.h"
#include "../main.h"

#include "../common/log.h"
#include "../common/enum.h"
#include "../common/macros.h"
#include "../gxmath/gxmath.h"

#include "../wrappers/wrappers.h"
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/batch.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)



#if gX_ENABLE_RENDERER == 1
    #include "../renderer/renderer.h"
#endif
#include "../rhi/rhi.h"
#include "../rhi/rhi_state.h"
#include "../rhi/gl_state.h"

extern gX_DrawingInfo drawinfo;

static gX_I32 gX_GUI_Canvas_Load_ElementHeap(gX_GUI_Canvas *canvas) {
    gX_I32 errors = 0;
    gX_TraceLog_GUI(gX_LOG_TRACE,"Loading element heap");
    
    gX_Heap_Zero(           &(canvas->elements));
    gX_Heap_SetBlockSize(   &(canvas->elements),sizeof(gX_GUI_Element));
    gX_Heap_SetMax(         &(canvas->elements),gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS);
    gX_Heap_SetAmount(      &(canvas->elements),0);
    errors = gX_Heap_Init(  &(canvas->elements));
    
    gX_TraceLog_GUI(gX_LOG_TRACE,"Element heap loading done");
    return errors;
}

gX_I32 gX_GUI_Canvas_Load(gX_GUI_Canvas *canvas) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Load","Loading canvas")
    
    if (!canvas) {
        errors++;
        return errors;
    }
    
    strcpy(canvas->name,"GUI_Canvas");
    strcpy(canvas->description,"GUI canvas for elements");
    
    canvas->canvasID = (gX_I32)(gui.canvases.amount-1);
    
    errors = gX_GUI_Canvas_Load_ElementHeap(canvas);
    
    canvas->canvasBox = (gX_Rectangle){0.0f,0.0f,maindata.screenWidth,maindata.screenHeight};
    canvas->canvasHue = gX_WHITE;
    
    canvas->refresh = false;
    canvas->doTransition = true;
    canvas->transitionDelay = 0.0f;
    canvas->canvasOffset = (gX_F32v2){0,0};
    canvas->canvasScale = (gX_F32v2){1,1};
    canvas->canvasAngle = 0.0f;
    canvas->state = (gX_GUI_Canvas_State){0};
    canvas->state.cursorX = 0;
    canvas->state.cursorY = 0;
    
    canvas->elements.state.UPDATE_ALL = true;
    canvas->elements.state.DRAW_ALL = true;
    
    canvas->camera.camera3D.position =  (gX_F32v3){0.0f, gX_GUI_CANVAS_DEFAULT_CAM_Y, gX_GUI_CANVAS_DEFAULT_CAM_Z};
    canvas->camera.camera3D.target =    (gX_F32v3){0.0f, 0.0f, 0.0f};
    canvas->camera.camera3D.up =        (gX_F32v3){0.0f, gX_GUI_CANVAS_CAM_UP_Y, 0.0f};
    canvas->camera.camera3D.fovy = gX_GUI_CANVAS_DEFAULT_FOVY;
    canvas->camera.camera3D.projection = gX_CAMERA_PERSPECTIVE;
    
    canvas->canvasBox = (gX_Rectangle){0.0f, 0.0f, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight};
    canvas->clipRect = (gX_Rectangle){0, 0, 0, 0}; // disabled by default
    canvas->editLock = 0; //opencode: heap slots recycle — never inherit a lock
    canvas->buffer.loadingState = (gX_LoadingState){0};
    //opencode: buffer dimensions set just before creation in Update (from current canvasBox)
    if (!(canvas->state.doNotLoadBuffer)) canvas->buffer.loadingState.load = true;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas loading done")
    return errors;
}

gX_I32 gX_GUI_Canvas_Unload(gX_GUI_Canvas *canvas) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Unload","Unloading canvas")
    
    if (!canvas) {
        errors++;
        return errors;
    }
    
    if (canvas->buffer.loadingState.loaded) {
        gX_RenderTexture_Unload(canvas->buffer.rt);
        canvas->buffer.loadingState.loaded = false;
    }
    
    gX_TraceLog_GUI(gX_LOG_TRACE,"Unloading elements");
    gX_GUI_Element *elements = canvas->elements.data;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        gX_GUI_Element *element = &elements[i];
        element->state.KEEP_HEAP_ANIMATION = false;
        element->state.KEEP_HEAP_ATTRIBUTE = false;
        element->state.KEEP_HEAP_COLOR = false;
        element->state.KEEP_HEAP_DATA = false;
        element->state.KEEP_HEAP_MODEL = false;
        element->state.KEEP_HEAP_SHADER = false;
        element->state.KEEP_HEAP_SHAPE = false;
        element->state.KEEP_HEAP_SOUND = false;
        element->state.KEEP_HEAP_TEXT = false;
        element->state.KEEP_HEAP_TEXTURE = false;
        errors = gX_GUI_Element_Unload(element);
    }
    gX_TraceLog_GUI(gX_LOG_TRACE,"Unloading done");
    
    gX_TraceLog_GUI(gX_LOG_TRACE,"Deinitializing element heap");
    errors = gX_Heap_DeInit(&(canvas->elements));
    gX_TraceLog_GUI(gX_LOG_TRACE,"Deinitializing done");
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Canvas unloading done")
    return errors;
}

gX_I32 gX_GUI_Canvas_Clear(gX_GUI_Canvas *canvas) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Clear","Clearing canvas")
    
    if (!canvas) {
        gX_TraceLog(gX_LOG_WARNING,"No canvas");
        errors++;
        return errors;
    }
    
    /*if (canvas->elements.data != 0) {
        gX_TraceLog(gX_LOG_WARNING,"No canvas->elements.data");
        errors += 2;
        return errors;
    }*/
    
    //opencode: iterate backwards to avoid skipping elements after removal.
    // gX_GUI_Canvas_Remove_Element calls gX_GUI_Element_Unload which calls
    // gX_GUI_Element_Clear internally — do NOT also call Clear here (was
    // double-clearing, causing redundant work on zeroed structs).
    for (gX_I32 i = (gX_I32)canvas->elements.amount - 1; i >= 0; i--) {
        errors += gX_GUI_Canvas_Remove_Element(canvas, (gX_U32)i);
    }
    
    //opencode: reset per-canvas visual state to safe defaults.
    //   canvasHue/Offset/Scale/clipRect are only initialized in
    //   gX_GUI_Canvas_Load (called once at startup). Without this reset,
    //   interrupted transitions (or any scene switch) leave stale values,
    //   causing invisible elements (hue={0,0,0,0}) or wrong composites.
    canvas->canvasHue = gX_WHITE;
    canvas->canvasOffset = (gX_F32v2){0,0};
    canvas->canvasScale = (gX_F32v2){1,1};
    canvas->canvasAngle = 0.0f;
    canvas->clipRect = (gX_Rectangle){0,0,0,0};
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Clearing done")
    return errors;
}

gX_I0 gX_GUI_Canvas_Add_Name(gX_GUI_Canvas *canvas, const gX_I8 *name) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Add_Name","Adding name to canvas")
    
    gX_RawString_Copy(canvas->name,name);
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Name adding done")
}

gX_I0 gX_GUI_Canvas_Add_Description(gX_GUI_Canvas *canvas, const gX_I8 *desc) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Add_Description","Adding description to canvas")
    
    gX_RawString_Copy(canvas->description,desc);
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Description adding done")
}

gX_I0 gX_GUI_Canvas_Add_CanvasID(gX_GUI_Canvas *canvas, gX_I32 id) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Add_CanvasID","Adding ID to canvas")
    
    canvas->canvasID = id;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"ID adding done")
}

gX_I0 gX_GUI_Canvas_Add_TemplateID(gX_GUI_Canvas *canvas, gX_I32 id) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Add_TemplateID","Adding template ID to canvas")
    
    canvas->templateID = id;
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"ID adding done")
}

//opencode: returns the actual heap slot index (>= 0) or -1 when the element
//   heap is full — callers use the slot for id assignment and seeking
//   (previously returned an error count and the slot was unknowable, so the
//   builder's id counter diverged from the real slot when holes existed).
gX_I32 gX_GUI_Canvas_Add_Element(gX_GUI_Canvas *canvas) {
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Add_Element","Adding element to canvas")
    
    //opencode: use _GetIndex to record actual heap slot (not amount-1, which is wrong when holes exist)
    gX_I32 actualIndex = -1;
    gX_GUI_Element *element = gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&(canvas->elements), &actualIndex);
    if (!element) {
        return -1;
    }
    gX_GUI_Element_Load(element,canvas);
    if (actualIndex >= 0) {
        canvas->elements.meta[actualIndex].UPDATE = true;
        canvas->elements.meta[actualIndex].DRAW = true;
    }
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Element adding done")
    return actualIndex;
}

gX_I32 gX_GUI_Canvas_Remove_Element(gX_GUI_Canvas *canvas, gX_U32 index) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Canvas_Remove_Element","Removing element from canvas")
    
    gX_GUI_Element *element = gX_Heap_Remove_Index_NoPtr(&(canvas->elements),index);
    if (!element) {
        errors++;
    }
    else {
        errors = gX_GUI_Element_Unload(element);
        //opencode: audit — a removed slot must be fully inert. Heap removal
        //   clears only USED; stale UPDATE/DRAW (plus the element's restored
        //   defaults: a visible 1x1 black rect) could resurrect the slot in
        //   update/draw/occlusion. Clear the operational metadata, zero the
        //   block so serialized dead slots stay inert, and drop global focus
        //   if it pointed into this slot.
        if (canvas->elements.meta) {
            canvas->elements.meta[index].UPDATE = false;
            canvas->elements.meta[index].DRAW = false;
            canvas->elements.meta[index].UNLOAD_AFTER_MASTERINDEX_CHANGE = false;
        }
        gX_Heap_ClearBlock(&(canvas->elements), index);
        //opencode: focus robustness — removal invalidates the slot; clear
        //   global focus when it referenced exactly this (canvas,index) pair.
        if (gui.focusCanvasID >= 0 && gui.canvases.data) {
            gX_GUI_Canvas *base = (gX_GUI_Canvas*)gui.canvases.data;
            gX_I32 ci = -1;
            if (canvas >= base && canvas < base + gui.canvases.amount) {
                ci = (gX_I32)(canvas - base);
            }
            if (ci == gui.focusCanvasID && (gX_U32)gui.focusElementID == index) {
                gX_GUI_Focus_Clear();
            }
            if (ci == gui.pointerCaptureCanvasID && (gX_U32)gui.pointerCaptureElementID == index) {
                gX_GUI_PointerCapture_Clear();
            }
        }
    }
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Element removing done")
    return errors;
}

//opencode: audit — single interaction-eligibility predicate. Pointer picking,
//   focus traversal, and keyboard/gamepad activation require a live, drawn,
//   updated, visible, enabled slot so a removed or inactive control can never
//   be hovered, clicked, focused, or activated.
gX_I1 gX_GUI_Canvas_Element_Eligible(const gX_GUI_Canvas *canvas, gX_U32 index) {
    if (!canvas || !canvas->elements.data || !canvas->elements.meta) return false;
    if (index >= canvas->elements.amount) return false;
    if (!canvas->elements.meta[index].USED) return false;
    if (!canvas->elements.meta[index].DRAW) return false;
    if (!canvas->elements.meta[index].UPDATE) return false;
    gX_GUI_Element *element = &((gX_GUI_Element*)canvas->elements.data)[index];
    if (!element->visible) return false;
    if (element->state.DISABLED) return false;
    return true;
}

//opencode: audit — build the per-frame render-order index list (stable, by
//   layer, live slots only). The owning element array is never permuted, so
//   heap metadata (USED/UPDATE/DRAW) and element->id stay aligned to their
//   slot; the draw loop renders in order[] order.
gX_I0 gX_GUI_Canvas_BuildDrawOrder(const gX_GUI_Canvas *canvas, gX_U32 *order, gX_U32 maxOrder, gX_U32 *outCount) {
    gX_U32 count = 0;
    if (outCount) *outCount = 0;
    if (!canvas || !canvas->elements.data || !order) return;
    gX_GUI_Element *elements = (gX_GUI_Element*)canvas->elements.data;
    gX_U32 n = canvas->elements.amount;
    for (gX_U32 i = 0; i < n && count < maxOrder; i++) {
        //opencode: live slots only — removed holes never enter render order
        if (!canvas->elements.meta || !canvas->elements.meta[i].USED) continue;
        order[count++] = i;
    }
    //opencode: stable insertion sort by layer (O(n) for nearly-sorted, n<100 typical)
    for (gX_U32 si = 1; si < count; si++) {
        gX_U32 key = order[si];
        gX_U32 keyLayer = elements[key].layer;
        gX_U32 sj = si;
        while (sj > 0 && elements[order[sj - 1]].layer > keyLayer) {
            order[sj] = order[sj - 1];
            sj--;
        }
        order[sj] = key;
    }
    if (outCount) *outCount = count;
}

//opencode: helpers
gX_GUI_Canvas *gX_GUI_Canvas_FromIndex(gX_I32 index) {
    if (index < 0 || (gX_U32)index >= gui.canvases.amount) return 0;
    gX_GUI_Canvas *data = (gX_GUI_Canvas*)gui.canvases.data;
    if (!data) return 0;
    return &data[index];
}

gX_I0 gX_GUI_Canvas_SetTransition(gX_GUI_Canvas *canvas, gX_I1 enabled) {
    if (canvas) canvas->doTransition = enabled;
}

gX_I0 gX_GUI_Canvas_Update( gX_GUI_Canvas *canvas) {
    
    //opencode: size buffer from current canvasBox (templates may have modified it after Load)
    //   Checks buffer pool first to recycle an RT with matching dimensions.
    if (maindata.backendLoaded && canvas->buffer.loadingState.load && !(canvas->buffer.loadingState.loaded)) {
        canvas->buffer.width = (gX_U16)canvas->canvasBox.width;
        canvas->buffer.height = (gX_U16)canvas->canvasBox.height;
        gX_RenderTexture_Wrapper pooled = gX_GUI_BufferPool_Fetch(canvas->buffer.width, canvas->buffer.height, false);
        if (pooled.loadingState.loaded) {
            canvas->buffer = pooled;
            canvas->buffer.loadingState.load = false;
            canvas->buffer.loadingState.loaded = true;
        } else {
            canvas->buffer.rt = gX_RenderTexture_Load(canvas->buffer.width,canvas->buffer.height);
            canvas->buffer.loadingState.load = false;
            if (canvas->buffer.rt.texture.id > 0) {
                canvas->buffer.loadingState.loaded = true;
            }
        }
    }
    
    //gX_TraceLog(gX_LOG_INFO,"canvas->elements.amount %i",canvas->elements.amount);
    gX_GUI_Element *elements = canvas->elements.data;
    
    if (!elements) return;
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        gX_GUI_Element *element = &elements[i];
        //opencode: audit — live traversal requires USED; a removed slot's
        //   stale UPDATE flag must not resurrect its defaults in the loop.
        if (!canvas->elements.meta[i].USED) continue;
        if (!(canvas->elements.meta[i].UPDATE)) continue;
        //opencode: skip Update for static elements (no animation, color, data, sound, interaction)
        //opencode: v0.2.0-pre9 — timers are live state too; skipping them
        //   froze every repeating timer on otherwise-static elements
        if (element->animations.amount == 0 && element->colors.amount == 0
            && element->datas.amount == 0 && element->sounds.amount == 0
            && element->timers.amount == 0
            && !element->state.HOVERABLE && !element->state.CLICKABLE_LEFT
            && !element->state.PRESSABLE_LEFT
            && !element->state.DRAGGABLE_LEFT) {
            continue;
        }
        gX_GUI_Element_Update(element,canvas);
    }
    
}

//opencode: simple AABB overlap test for element culling
static gX_I1 gX_Rect_Overlaps(gX_Rectangle a, gX_Rectangle b) {
    return (a.x < b.x + b.width && a.x + a.width > b.x &&
            a.y < b.y + b.height && a.y + a.height > b.y);
}

gX_U32 g_guiCanvasDrawCount = 0;
gX_U32 g_guiElementDrawCount = 0;

gX_I0 gX_GUI_Canvas_Draw( gX_GUI_Canvas *canvas) {

    //opencode: GL state leak detection is gated behind --gl-diagnostics —
    //   two full snapshots per canvas per frame (~40 glGetIntegerv) is too
    //   costly for a debug-only check.
    gX_GL_State canvasStateBefore = {0};
    gX_I1 diagState = gX_RHI_Diagnostics_IsEnabled();
    if (diagState) gX_GL_State_Save(&canvasStateBefore);

    gX_GUI_Element *elements = canvas->elements.data;
    if (!elements) return;
    g_guiCanvasDrawCount++;

    //opencode: audit — render order is an index list, NOT a physical sort.
    //   Sorting gX_GUI_Element structs in place desynchronized the parallel
    //   heap metadata (USED/UPDATE/DRAW came to describe a different element
    //   after a permutation), broke element->id == slot identity, and let a
    //   later first-empty insertion overwrite an active element moved into a
    //   metadata hole. Build the order array (stable by layer, live slots
    //   only) and draw in that order; the array itself never moves.
    //opencode: gameCSS resolve — applies stylesheet rules + inline styles to
    //   element fields (runs before layout resolve so CSS sizes/margins feed
    //   the flex math). Rate-limited hot-reload check inside.
    gX_GUI_CSS_ResolveCanvas(canvas);

    //opencode: box-model layout resolve — containers position their children
    //   (runs after the order build so child lookup by element id is stable,
    //   before the draw loop so children draw at resolved positions)
    gX_GUI_Canvas_Layout_Resolve(canvas);

    //opencode: resolve CSS before collecting draw order so a stylesheet's
    //   layer/z-index change takes effect in this frame, not the next one.
    gX_U32 drawOrder[gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS];
    gX_U32 drawCount = 0;
    gX_GUI_Canvas_BuildDrawOrder(canvas, drawOrder, gX_GUI_CANVAS_DEFAULT_MAX_ELEMENTS, &drawCount);

    //opencode: render canvas to buffer then draw to screen at canvasBox
    //opencode: use separate alpha blend to prevent alpha squaring in render texture compositing
    gX_I1 useBuf = canvas->buffer.loadingState.loaded;
    if (useBuf) {
        gX_Drawing_BeginTexture(canvas->buffer.rt);
            gX_Drawing_ClearBG((gX_Color){0,0,0,0});

            gX_Drawing_BeginBlendCustomSeparate(
                gX_BLEND_SRC_ALPHA, gX_BLEND_ONE_MINUS_SRC_ALPHA,
                gX_BLEND_ONE,       gX_BLEND_ONE_MINUS_SRC_ALPHA
            );
            //opencode: scissor clip — hardware-accelerated clipping to clipRect
            if (canvas->clipRect.width > 0) {
                gX_Drawing_BeginScissor(canvas->clipRect);
            }
            gX_Rectangle cullRect = {0, 0, (gX_F32)canvas->buffer.width, (gX_F32)canvas->buffer.height};
            for (gX_U32 oi = 0; oi < drawCount; oi++) {
                gX_U32 i = drawOrder[oi];
                gX_GUI_Element *element = &elements[i];
                if (canvas->elements.meta[i].DRAW && element->visible
                    //opencode: skip elements outside buffer bounds (culling)
                    && gX_Rect_Overlaps(element->rect, cullRect)) {
                    g_guiElementDrawCount++;
                    //opencode: generic layout tracking — buffer-space rects
                    //   transformed to screen space via the composite mapping
                    if (gX_uiDebugEnabled || gX_uiLayoutTrackEnabled) {
                        gX_F32 sx = canvas->canvasBox.x + canvas->canvasOffset.x
                            + element->rect.x * (canvas->canvasBox.width / (gX_F32)canvas->buffer.width);
                        gX_F32 sy = canvas->canvasBox.y + canvas->canvasOffset.y
                            + element->rect.y * (canvas->canvasBox.height / (gX_F32)canvas->buffer.height);
                        gX_F32 sw = element->rect.width * (canvas->canvasBox.width / (gX_F32)canvas->buffer.width);
                        gX_F32 sh = element->rect.height * (canvas->canvasBox.height / (gX_F32)canvas->buffer.height);
                        gX_I8 label[64];
                        if (element->name[0] && strcmp(element->name, "unnamed") != 0) {
                            strncpy(label, element->name, sizeof(label) - 1);
                        } else {
                            const gX_I8 *typeStr = gX_GUI_Element_Type_String((gX_I32)element->type);
                            snprintf(label, sizeof(label), "%s_%d",
                                strcmp(typeStr, "UNKNOWN") ? typeStr : "elem", (gX_I32)i);
                        }
                        gX_UI_Layout_GUI_TrackRect(canvas->name, label,
                            sx, sy, sw, sh,
                            element->texts.amount > 0 ? 1 : 0,
                            (element->type == gX_GUI_ELEMENT_TYPE_PANEL ||
                             element->type == gX_GUI_ELEMENT_TYPE_CONTAINER) ? 1 : 0);
                    }
                    gX_GUI_Element_Draw(element,canvas);
                }
            }
            if (canvas->clipRect.width > 0) {
                gX_Drawing_EndScissor();
            }
            gX_Drawing_EndBlend();
        gX_Drawing_EndTexture();

        //opencode: composite canvas buffer to screen. Delegates to helper
        //   for ortho/viewport setup, texture draw, and forced batch flush.
        //opencode: v0.6.9 — dst stays game-space; the GL viewport
        //   (game rect, pillarboxed when docked) centers it. No CPU map.
        gX_GUI_CompositeToScreen(
            canvas->buffer.rt.texture,
            (gX_Rectangle){
                canvas->canvasBox.x + canvas->canvasOffset.x,
                canvas->canvasBox.y + canvas->canvasOffset.y,
                canvas->canvasBox.width,
                canvas->canvasBox.height
            },
            canvas->canvasScale,
            canvas->canvasAngle,
            canvas->name);
    }
    else {
        //opencode: reset to standard alpha blend before drawing no-buffer canvas
        //   elements directly to the current framebuffer. The previously-active
        //   canvas may have left a custom separate blend mode (SRC_ALPHA/ONE)
        //   in place, which would produce wrong alpha results for direct draws.
        gX_Drawing_EndBlend();
        gX_Rectangle cullRect = {0, 0, (gX_F32)maindata.screenWidth, (gX_F32)maindata.screenHeight};
        for (gX_U32 oi = 0; oi < drawCount; oi++) {
            gX_U32 i = drawOrder[oi];
            gX_GUI_Element *element = &elements[i];
            if (canvas->elements.meta[i].DRAW && element->visible
                //opencode: skip elements outside screen bounds (culling)
                && gX_Rect_Overlaps(element->rect, cullRect)) {
                g_guiElementDrawCount++;
                //opencode: generic layout tracking — no-buffer canvas draws
                //   elements directly in screen space
                if (gX_uiDebugEnabled || gX_uiLayoutTrackEnabled) {
                    gX_I8 label[64];
                    if (element->name[0] && strcmp(element->name, "unnamed") != 0) {
                        strncpy(label, element->name, sizeof(label) - 1);
                    } else {
                        const gX_I8 *typeStr = gX_GUI_Element_Type_String((gX_I32)element->type);
                        snprintf(label, sizeof(label), "%s_%d",
                            strcmp(typeStr, "UNKNOWN") ? typeStr : "elem", (gX_I32)i);
                    }
                    gX_UI_Layout_GUI_TrackRect(canvas->name, label,
                        element->rect.x, element->rect.y,
                        element->rect.width, element->rect.height,
                        element->texts.amount > 0 ? 1 : 0,
                        (element->type == gX_GUI_ELEMENT_TYPE_PANEL ||
                         element->type == gX_GUI_ELEMENT_TYPE_CONTAINER) ? 1 : 0);
                }
                gX_GUI_Element_Draw(element,canvas);
            }
        }
        gX_Drawing_EndBlend();
    }
    
    if (diagState) {
        gX_GL_State canvasStateAfter = {0};
        gX_GL_State_Save(&canvasStateAfter);
        gX_GL_State_Diff(&canvasStateBefore, &canvasStateAfter, canvas->name);
    }
}

#if GX_GUI_USE_POOLS
//opencode: simple shader cache for pool draw — maps fragPath → gX_Shader.
//   Max 16 cached shaders. Reset each frame by setting cacheCount = 0.
#define GX_POOL_SHADER_CACHE_MAX 16
struct { gX_I8 fragPath[128]; gX_Shader shader; } g_poolShaderCache[GX_POOL_SHADER_CACHE_MAX];
gX_U32 g_poolShaderCacheCount = 0;

static gX_Shader gX_Pool_GetShader(const gX_I8 *fragPath) {
    for (gX_U32 i = 0; i < g_poolShaderCacheCount; i++) {
        if (strcmp(g_poolShaderCache[i].fragPath, fragPath) == 0)
            return g_poolShaderCache[i].shader;
    }
    //opencode: not cached — load and cache
    if (g_poolShaderCacheCount < GX_POOL_SHADER_CACHE_MAX) {
        gX_U32 idx = g_poolShaderCacheCount++;
        strcpy(g_poolShaderCache[idx].fragPath, fragPath);
        g_poolShaderCache[idx].shader = gX_Shader_Load(NULL, fragPath);
        return g_poolShaderCache[idx].shader;
    }
    return (gX_Shader){0};
}

//opencode: allocate an element from the global element pool, bound to a canvas.
gX_Pool_Handle gX_GUI_Pool_AddElement(gX_U32 canvasIndex) {
    gX_Pool_Handle h = gX_Pool_Alloc(&gui.poolElements);
    if (!GX_POOL_VALID(h)) return GX_POOL_NULL;
    gX_PoolElement *e = (gX_PoolElement*)gX_Pool_Get(&gui.poolElements, h);
    if (!e) return GX_POOL_NULL;
    memset(e, 0, sizeof(gX_PoolElement));
    //opencode: store the canvas index in the lower 16 bits of the hCanvas handle.
    //   Generation is ignored for canvas refs (always 0).
    e->hCanvas = GX_POOL_HANDLE(canvasIndex, 0);
    return h;
}

//opencode: draw all pool elements for a given canvas. Brute-force scans the
//   element pool matching by hCanvas index. For each matched element, draws
//   rect followed by shader modules and text modules referenced by handles.
gX_I0 gX_GUI_Canvas_Draw_Pools(gX_U32 canvasIndex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex((gX_I32)canvasIndex);
    if (!canvas) return;
    
        gX_Drawing_BeginTexture(canvas->buffer.rt);
        gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        for (gX_U32 i = 0; i < gui.poolElements.count; i++) {
            gX_PoolElement *e = &((gX_PoolElement*)gui.poolElements.data)[i];
            if (GX_POOL_IDX(e->hCanvas) != canvasIndex) continue;
            
            //opencode: draw rect fill from color[0]
            gX_Rectangle_Draw(e->rect, e->color[0]);
            
            //opencode: draw text modules (simple centered text)
            for (gX_U32 m = 0; m < 4; m++) {
                if (!GX_POOL_VALID(e->hTexts[m])) continue;
                gX_PoolText *t = (gX_PoolText*)gX_Pool_Get(&gui.poolTexts, e->hTexts[m]);
                if (!t) continue;
                gX_RawString_Draw_Lesser(t->string,
                    (gX_F32v2){e->rect.x + e->rect.width * 0.5f,
                               e->rect.y + e->rect.height * 0.5f},
                    (gX_F32)t->fontSize, gX_WHITE);
            }
            
            //opencode: draw shader modules (cached per fragPath)
            for (gX_U32 m = 0; m < 4; m++) {
                if (!GX_POOL_VALID(e->hShaders[m])) continue;
                gX_PoolShader *s = (gX_PoolShader*)gX_Pool_Get(&gui.poolShaders, e->hShaders[m]);
                if (!s) continue;
                gX_Shader sh = gX_Pool_GetShader(s->fragPath);
                if (sh.id > 0) {
                    gX_Drawing_BeginShader(sh);
                    gX_Rectangle_Draw(e->rect, gX_WHITE);
                    gX_Drawing_EndShader();
                }
            }
        }
    gX_Drawing_EndTexture();
    gX_GUI_CompositeToScreen(canvas->buffer.rt.texture, canvas->canvasBox, canvas->canvasScale, canvas->canvasAngle, canvas->name);
}

//opencode: migrate all old heap elements to pool entries. Runs after templates
//   build the old heap. Iterates each canvas's old element heap, creates pool
//   elements that mirror the old elements' rect/color/modules. The pool draw
//   path then reads from these pool entries instead of the old heap.
gX_I0 gX_GUI_Pools_MigrateFromHeap(gX_I0) {
    gX_GUI_Canvas *canvases = (gX_GUI_Canvas*)gui.canvases.data;
    if (!canvases) return;
    
    for (gX_U32 ci = 0; ci < gui.canvases.amount; ci++) {
        gX_GUI_Canvas *canvas = &canvases[ci];
        if (!gui.canvases.meta[ci].DRAW) continue;
        
        gX_GUI_Element *oldElements = (gX_GUI_Element*)canvas->elements.data;
        if (!oldElements) continue;
        
        for (gX_U32 ei = 0; ei < canvas->elements.amount; ei++) {
            gX_GUI_Element *old = &oldElements[ei];
            if (!canvas->elements.meta[ei].DRAW) continue;
            
            //opencode: allocate pool element
            gX_Pool_Handle hElem = gX_Pool_Alloc(&gui.poolElements);
            if (!GX_POOL_VALID(hElem)) break;  // pool full, stop migrating
            gX_PoolElement *e = (gX_PoolElement*)gX_Pool_Get(&gui.poolElements, hElem);
            if (!e) break;
            memset(e, 0, sizeof(gX_PoolElement));
            e->hCanvas = GX_POOL_HANDLE(ci, 0);
            e->rect = old->rect;
            e->color[0] = old->color;
            e->flags = old->stateRendering.RECT_NO_DRAW ? 1 : 0;
            if (old->stateRendering.RECT_DRAW_WIRE) e->flags |= 2;
            e->layer = 0;
            
            //opencode: migrate texts (field name is 'text', not 'string')
            gX_GUI_Element_Text *oldTexts = (gX_GUI_Element_Text*)old->texts.data;
            for (gX_U32 ti = 0; ti < old->texts.amount && ti < 4; ti++) {
                gX_Pool_Handle hT = gX_Pool_Alloc(&gui.poolTexts);
                if (!GX_POOL_VALID(hT)) break;
                gX_PoolText *pt = (gX_PoolText*)gX_Pool_Get(&gui.poolTexts, hT);
                if (!pt) break;
                memset(pt, 0, sizeof(gX_PoolText));
                strncpy(pt->string, oldTexts[ti].text, sizeof(pt->string) - 1);
                pt->fontSize = (gX_U16)oldTexts[ti].fontSize;
                pt->align = 0;
                e->hTexts[ti] = hT;
            }
            
            //opencode: migrate colors (first two colors from the colors heap)
            gX_GUI_Element_Color *oldColors = (gX_GUI_Element_Color*)old->colors.data;
            if (old->colors.amount > 0) {
                e->color[0] = oldColors[0].color;
                if (old->colors.amount > 1) {
                    e->color[1] = oldColors[1].color;
                }
            }
            
            //opencode: migrate shaders (frag path stored by Builder_Shader_Frag)
            gX_GUI_Element_Shader *oldShaders = (gX_GUI_Element_Shader*)old->shaders.data;
            for (gX_U32 si = 0; si < old->shaders.amount && si < 4; si++) {
                if (oldShaders[si].fragPath[0] == '\0') continue;
                gX_Pool_Handle hS = gX_Pool_Alloc(&gui.poolShaders);
                if (!GX_POOL_VALID(hS)) break;
                gX_PoolShader *ps = (gX_PoolShader*)gX_Pool_Get(&gui.poolShaders, hS);
                if (!ps) break;
                memset(ps, 0, sizeof(gX_PoolShader));
                strncpy(ps->fragPath, oldShaders[si].fragPath, sizeof(ps->fragPath) - 1);
                e->hShaders[si] = hS;
            }
            
            //opencode: migrate shapes
            gX_GUI_Element_Shape *oldShapes = (gX_GUI_Element_Shape*)old->shapes.data;
            for (gX_U32 si = 0; si < old->shapes.amount && si < 2; si++) {
                gX_Pool_Handle hS = gX_Pool_Alloc(&gui.poolShapes);
                if (!GX_POOL_VALID(hS)) break;
                gX_PoolShape *ps = (gX_PoolShape*)gX_Pool_Get(&gui.poolShapes, hS);
                if (!ps) break;
                memset(ps, 0, sizeof(gX_PoolShape));
                ps->type = oldShapes[si].type;
                for (gX_U32 p = 0; p < 8; p++) ps->params[p] = 0;
                memcpy(&ps->params[0], &oldShapes[si].scalar[0], sizeof(gX_F32) * 4);
                e->hShapes[si] = hS;
            }
            
            //opencode: migrate textures
            gX_GUI_Element_Texture *oldTextures = (gX_GUI_Element_Texture*)old->textures.data;
            for (gX_U32 ti = 0; ti < old->textures.amount && ti < 4; ti++) {
                gX_Pool_Handle hTex = gX_Pool_Alloc(&gui.poolTextures);
                if (!GX_POOL_VALID(hTex)) break;
                gX_PoolTexture *pt = (gX_PoolTexture*)gX_Pool_Get(&gui.poolTextures, hTex);
                if (!pt) break;
                memset(pt, 0, sizeof(gX_PoolTexture));
                pt->id = oldTextures[ti].texture ? (gX_I32)oldTextures[ti].texture->id : 0;
                pt->src = oldTextures[ti].params.src;
                e->hTextures[ti] = hTex;
            }
            
            //opencode: migrate sounds
            gX_GUI_Element_Sound *oldSounds = (gX_GUI_Element_Sound*)old->sounds.data;
            for (gX_U32 si = 0; si < old->sounds.amount && si < 2; si++) {
                gX_Pool_Handle hSnd = gX_Pool_Alloc(&gui.poolSounds);
                if (!GX_POOL_VALID(hSnd)) break;
                gX_PoolSound *ps = (gX_PoolSound*)gX_Pool_Get(&gui.poolSounds, hSnd);
                if (!ps) break;
                memset(ps, 0, sizeof(gX_PoolSound));
                ps->id = oldSounds[si].sound ? (gX_I32)oldSounds[si].sound->frameCount : 0;
                ps->loop = 0;
                e->hSounds[si] = hSnd;
            }
        }
    }
}
#endif

//opencode: reset canvasOffset to {0,0} and canvasScale to {1,1} for all canvases
//   Call after position/scale transition completes to prevent stale transform
gX_I0 gX_GUI_Canvas_ResetTransforms(gX_I0) {
    for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
        gX_GUI_Canvas *c = (gX_GUI_Canvas *)gX_Heap_SeekPtr_Index(&gui.canvases, i);
        if (!c) continue;
        c->canvasOffset = (gX_F32v2){0,0};
        c->canvasScale = (gX_F32v2){1,1};
        c->canvasAngle = 0.0f;
    }
}

//opencode: composite a canvas buffer to screen. Sets ortho+viewport to screen
//   dimensions, draws the texture, forces the rlgl batch to flush immediately
//   (fixing the last-canvas-at-native-size bug), and marks canvas as drawn.
//   The forced flush is required because rlgl's batch system defers the actual
//   GL draw; if flushed later by ImGui or the next frame, the projection/
//   modelview may have changed, causing the last canvas to render at native
//   buffer size in the window corner.
//opencode: v0.6.x — scale is now applied at composite time (driven by the
//   ZOOM transition's canvasScale, which was computed but never rendered).
//   gX_Texture_Draw scales around the UNSCALED dst center, matching the
//   rotation pivot, so offset/scale/angle share one transform that the
//   occlusion pass inverts for pointer hit testing.
gX_I0 gX_GUI_CompositeToScreen(gX_Texture texture, gX_Rectangle dst, gX_F32v2 scale, gX_F32 angle, const gX_I8 *debugLabel) {

    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0, (gX_F64)maindata.screenWidth, (gX_F64)maindata.screenHeight, 0, 0.0, 1.0);
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Viewport(drawinfo.vpX, drawinfo.vpY, drawinfo.vpW, drawinfo.vpH);
    //opencode: disable depth test for composite — leftover depth from 3D scene can
    //   reject the composite quad fragments, producing transparent output.
    gX_I1 depthWas = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    if (depthWas) gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    if (debugLabel) {
        gX_TraceLog(gX_LOG_TRACE2, "CANVAS_DRAW composite: name=%s dst=(%.0f,%.0f,%.0f,%.0f) scale=(%.2f,%.2f) angle=%.1f",
            debugLabel, dst.x, dst.y, dst.width, dst.height, scale.x, scale.y, angle);
    }
    //opencode: canvas buffers use premultiplied RGB from their separate alpha
    //   blend; use ONE here so text and translucent surfaces are not alpha-squared.
    gX_Drawing_BeginBlendCustomSeparate(
        gX_RHI_ONE, gX_RHI_ONE_MINUS_SRC_ALPHA,
        gX_RHI_ONE, gX_RHI_ONE_MINUS_SRC_ALPHA
    );
    gX_Texture_Draw(texture, (gX_Texture_Draw_Params){
        .dst = dst,
        .tint = gX_WHITE,
        .scale = scale,
        .rotation = angle,
        .origin = {0, 0}
    });
    gX_Batch_Flush();
    gX_Drawing_EndBlend();
    if (depthWas) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
#if gX_ENABLE_RENDERER == 1
    gX_Renderer_SetCanvasDrawnByGUI(1);
#endif
}
