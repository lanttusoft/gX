/**********************************************************************************************
*
*   [gX: src/gui/element/element.c]
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

#include "element.h"

#include "../gui.h"
#include "../widgetsdf.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../gxmath/color.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/vector2.h"

#include "../../input.h"
#include "../../main.h"
#include "../../common/enum/keys.h"

#define INIT_HEAP(namelc,namecc,nameuc) \
    gX_TraceLog_GUI(gX_LOG_TRACE,"Initializing "#namelc" heap"); \
    gX_Heap_Zero(           &(element->namelc##s)); \
    gX_Heap_SetBlockSize(   &(element->namelc##s),sizeof(gX_GUI_Element_##namecc)); \
    gX_Heap_SetMax(         &(element->namelc##s),gX_GUI_ELEMENT_DEFAULT_MAX_##nameuc##S); \
    gX_Heap_SetAmount(      &(element->namelc##s),0); \
    errors = gX_Heap_Init(  &(element->namelc##s)); \
    if (errors) gX_TraceLog_GUI(gX_LOG_ERROR, "Failed to init "#namelc" heap"); \
    element->state.KEEP_HEAP_##nameuc = true; \
    gX_TraceLog_GUI(gX_LOG_TRACE,"Initializing "#namelc" heap done"); \
    
//opencode: monotonically changing runtime identity for element heap slots.
static gX_U32 gX_GUI_Element_Generation = 0;

static gX_I0 gX_GUI_Element_DefaultValues(gX_GUI_Element *element) {
    element->rect   = (gX_Rectangle){0,0,1,1};
    element->color  = (gX_Color){0,0,0,255};
    
    element->params         = (gX_GUI_Element_Parameters){0};
    element->params.scale   = gX_F32v2_One();
    element->params.matrix  = gX_Matrix_Identity();
    
    element->type     = gX_GUI_ELEMENT_TYPE_UNKNOWN;
    element->layer    = 0;
    element->tag      = 0;
    element->userData = NULL;
    element->visible  = 1;
    
    element->state          = (gX_GUI_Element_State){0};
    element->stateRendering = (gX_GUI_Element_Rendering){0};

    //opencode: v0.2.0 — SDF radius override sentinel (-1 = use theme value)
    element->cornerRadius = -1.0f;

    //opencode: attach the shared SDF widget surface (theme-gated)
    gX_GUI_WidgetSDF_Attach(element);
    
    //opencode: gameCSS fields — zeroed (never assume zeroed heap blocks)
    element->cssClass[0]    = '\0';
    element->cssStyle[0] = '\0';

    //opencode: flex sizing defaults — no growth, no min/max clamps
    element->flexGrow = 0.0f;
    element->flexMin[0] = 0.0f;
    element->flexMin[1] = 0.0f;
    element->flexMax[0] = 0.0f;
    element->flexMax[1] = 0.0f;
}

gX_I32 gX_GUI_Element_Load(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Element_Load","Loading element")
    
    strcpy(element->name,"unnamed"); //opencode: changed from "GUI_Element" for clarity
    //opencode: empty default — "gui element" rendered as a stray micro-caption
    //   on every focused widget; tooltips now show only where explicitly set.
    element->description[0] = '\0';
    
    //opencode: ID from element pointer offset ensures correct heap index even
    //with holes (Add_FirstEmpty can place element at a non-contiguous slot).
    //Occlusion check later compares element->id against this index.
    if (canvas->elements.data && canvas->elements.meta) {
        gX_I8 *base = (gX_I8*)canvas->elements.data;
        gX_I8 *elem = (gX_I8*)element;
        gX_U32 offset = (gX_U32)(elem - base);
        gX_U32 stride = sizeof(gX_GUI_Element);
        element->id = (stride > 0) ? (offset / stride) : 0;
    }
    gX_GUI_Element_Generation++;
    if (gX_GUI_Element_Generation == 0) gX_GUI_Element_Generation = 1;
    element->generation = gX_GUI_Element_Generation;
    
    gX_GUI_Element_DefaultValues(element);
    
    //opencode: v0.2.0 — mark SDF-surface elements for the composite-stage
    //   widget pass (theme + env gated inside WidgetSDF_Ready)
    if (gX_GUI_WidgetSDF_Ready()) element->stateRendering.RECT_SDF_SURFACE = 1;
    
    //opencode: lazy allocation — heaps are no longer pre-allocated here; first Add_* call creates them
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Element loading done")
    return errors;
}

#define DEINIT_HEAP(namelc,nameuc) \
    if (!(element->state.KEEP_HEAP_##nameuc)) { \
        gX_TraceLog_GUI(gX_LOG_TRACE,"Deinitializing "#namelc" heap"); \
        errors = gX_Heap_DeInit(&(element->namelc##s)); \
    } \

gX_I32 gX_GUI_Element_Unload(gX_GUI_Element *element) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Element_Unload","Unloading element")
    
    errors = gX_GUI_Element_Clear(element);
    
    DEINIT_HEAP(animation,ANIMATION)
    DEINIT_HEAP(attribute,ATTRIBUTE)
    DEINIT_HEAP(color,COLOR)
    DEINIT_HEAP(data,DATA)
    //opencode: layout has no KEEP_HEAP_* state bit (the 32-bit state bitfield
    //   is full) — Add_Layout is custom and the heap is always deinit'd here.
    if (element->layouts.state.INITIALIZED) {
        errors = gX_Heap_DeInit(&(element->layouts));
    }
    DEINIT_HEAP(model,MODEL)
    DEINIT_HEAP(shader,SHADER)
    DEINIT_HEAP(shape,SHAPE)
    DEINIT_HEAP(sound,SOUND)
    DEINIT_HEAP(text,TEXT)
    DEINIT_HEAP(texture,TEXTURE)
    DEINIT_HEAP(timer,TIMER)
    DEINIT_HEAP(textinput,TEXTINPUT)
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Element unloading done")
    return errors;
}

gX_I32 gX_GUI_Element_Clear(gX_GUI_Element *element) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"GUI_Element_Clear","Clearing element")
    
    gX_GUI_Element_DefaultValues(element);
    
    errors = gX_GUI_Element_Clear_Animation(element);
    errors = gX_GUI_Element_Clear_Attribute(element);
    errors = gX_GUI_Element_Clear_Color(element);
    errors = gX_GUI_Element_Clear_Data(element);
    errors = gX_GUI_Element_Clear_Layout(element);
    errors = gX_GUI_Element_Clear_Model(element);
    errors = gX_GUI_Element_Clear_Shader(element);
    errors = gX_GUI_Element_Clear_Shape(element);
    errors = gX_GUI_Element_Clear_Sound(element);
    errors = gX_GUI_Element_Clear_Text(element);
    errors = gX_GUI_Element_Clear_Texture(element);
    errors = gX_GUI_Element_Clear_Timer(element);
    errors = gX_GUI_Element_Clear_TextInput(element);
    
    gX_TRACELOG_EPILOGUE_MODULE(gX_LOG_MODULE_GUI, gX_LOG_TRACE,"Clearing done")
    return errors;
}

//loop

extern gX_I0 gX_GUI_Element_Update_Animations(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Update_Colors(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Update_Datas(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Update_Sounds(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Update_Timers(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Update_TextInputs(gX_GUI_Element *element);

gX_I0 gX_GUI_Element_MakeClickable(gX_GUI_Element *element) {
    if (!element) return;
    element->state.HOVERABLE = true;
    element->state.CLICKABLE_LEFT = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.FOCUSABLE = true;
}

//opencode: runtime ImGui tools share the screen, so their native GUI underlay is inert.
static gX_I0 gX_GUI_Element_Update_State(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    gX_GUI_Element_State *state = &element->state;
    
    //opencode: guard against stale canvas reference
    if (!canvas) return;
    //opencode: audit — disabled/hidden/removed elements are inert: clear any
    //   prior interaction bits (the old DISABLED early return left stale
    //   CLICKED_LEFT/PRESSED_* set, and hidden elements kept hover/click/
    //   focus state). element->id is the stable heap slot, so a removed slot
    //   fails the USED half of the eligibility predicate.
    if (!gX_GUI_Canvas_Element_Eligible(canvas, element->id)) {
        state->HOVERED = false;
        state->HOVERED_ENTER = false;
        state->HOVERED_EXIT = false;
        state->_prevHovered = false;
        state->TOGGLED = false;
        state->CLICKED_LEFT = false;
        state->CLICKED_RIGHT = false;
        state->CLICKED_MIDDLE = false;
        state->PRESSED_LEFT = false;
        state->PRESSED_RIGHT = false;
        state->PRESSED_MIDDLE = false;
        state->DRAGGED_LEFT = false;
        state->FOCUSED = false;
        return;
    }
    
    gX_F32v2 mouseCoordDelta = input.mouse.coordDelta;
    gX_F32 mouseCoordDeltaSpeed = gX_F32v2_Length(mouseCoordDelta);
    gX_I1 mouseLeftPressed = input.mouse.leftButton.PRESSED;
    gX_I1 mouseRightPressed = input.mouse.rightButton.PRESSED;
    gX_I1 mouseMiddlePressed = input.mouse.middleButton.PRESSED;
    gX_I1 mouseLeftDown = input.mouse.leftButton.DOWN;
    gX_I1 mouseRightDown = input.mouse.rightButton.DOWN;
    gX_I1 mouseMiddleDown = input.mouse.middleButton.DOWN;
    
    //opencode: modal scope — background canvases stay inert while a modal
    //   owns input, even if a stale focus bit survived the modal open.
    if (gui.modalCanvasID >= 0) {
        gX_GUI_Canvas *modalCanvas = gX_GUI_Canvas_FromIndex(gui.modalCanvasID);
        if (modalCanvas && canvas != modalCanvas) {
            state->HOVERED = false;
            state->HOVERED_ENTER = false;
            state->HOVERED_EXIT = false;
            state->_prevHovered = false;
            state->TOGGLED = false;
            state->CLICKED_LEFT = false;
            state->CLICKED_RIGHT = false;
            state->CLICKED_MIDDLE = false;
            state->PRESSED_LEFT = false;
            state->PRESSED_RIGHT = false;
            state->PRESSED_MIDDLE = false;
            state->DRAGGED_LEFT = false;
            state->FOCUSED = false;
            return;
        }
    }

    gX_I1 insideRect = false;
    //opencode: match by pointer, not id — the canvas layer sort permutes the
    //   live element array so the heap-slot id no longer equals the sorted
    //   array index that occlusion used to store. Pointer comparison is
    //   permutation-independent.
    //opencode: v0.4.0 — B1: also match the canvas by pointer (canvasPtr).
    //   The old canvas->canvasID == occlusion.canvasID compared a creation-
    //   order id against the heap INDEX, which drifts once a canvas is
    //   removed (heap holes) — clicks went dead silently.
    if ((canvas == gui.occlusion.canvasPtr) &&
        (element == gui.occlusion.elementPtr ||
         (gui.occlusion.elementPtr == NULL && element->id == (gX_U32)gui.occlusion.elementID))) {
        insideRect = true;
    }
    //opencode: pointer capture — the captured drag keeps its pressed/drag
    //   state even when the pointer leaves its rect; occlusion already
    //   routes to the owner, this is belt-and-suspenders for direct update.
    if (!insideRect && gX_GUI_PointerCapture_IsOwner(canvas, element)) {
        insideRect = true;
    }
    
    state->HOVERED = (state->HOVERABLE && insideRect) ? true : false;
    state->HOVERED_ENTER = state->HOVERED && !state->_prevHovered;
    state->HOVERED_EXIT  = !state->HOVERED && state->_prevHovered;
    state->_prevHovered  = state->HOVERED;
    
    if (insideRect) {
        if (state->CLICKABLE_LEFT && mouseLeftPressed)      { state->CLICKED_LEFT = true;  }
        else                                                { state->CLICKED_LEFT = false; }
        // Right/middle click events kept for sound/debug; no capability gate needed
        if (mouseRightPressed)  { state->CLICKED_RIGHT = true;  } else { state->CLICKED_RIGHT = false;  }
        if (mouseMiddlePressed) { state->CLICKED_MIDDLE = true; } else { state->CLICKED_MIDDLE = false; }
        
        if (state->TOGGLEABLE && (mouseLeftPressed || mouseRightPressed || mouseMiddlePressed)) {
            state->TOGGLED = true;
        }
        else { state->TOGGLED = false; }
        
        if (state->PRESSABLE_LEFT && mouseLeftDown)     { state->PRESSED_LEFT = true;  }
        else                                            { state->PRESSED_LEFT = false; }
        if (mouseRightDown)                             { state->PRESSED_RIGHT = true;  }
        else                                            { state->PRESSED_RIGHT = false; }
        if (mouseMiddleDown)                            { state->PRESSED_MIDDLE = true;  }
        else                                            { state->PRESSED_MIDDLE = false; }
        
        //opencode: drag threshold — require minimum movement to prevent jitter-triggers
        if (state->DRAGGABLE_LEFT && mouseLeftDown && (mouseCoordDeltaSpeed > gX_GUI_DRAG_THRESHOLD)) { state->DRAGGED_LEFT = true; }
        else                                                                                          { state->DRAGGED_LEFT = false; }
        
    }
    else {
        state->HOVERED = false;
        state->PRESSED_LEFT = false;
        state->PRESSED_RIGHT = false;
        state->PRESSED_MIDDLE = false;
        state->DRAGGED_LEFT = false;
        state->CLICKED_LEFT = false;
        state->CLICKED_RIGHT = false;
        state->CLICKED_MIDDLE = false;
        state->TOGGLED = false;
    }
    
    //opencode: transition blocks input — v0.4.0: only during the OUT phase
    //   (the old scene exiting). During IN the arriving scene's buttons are
    //   live; the old blanket `executing` gate kept every button dead for
    //   the whole entrance (1.25s skew-slide) — the "buttons don't work"
    //   report. FadeFromBlack (single out phase) still blocks for its 0.25s.
    //   NOTE: gated on out.execute ONLY — `executing` is cleared by the
    //   setup-frame reset block (transition.c), which opened a 1-frame hole
    //   on the very first transition frame; out.execute is set before that
    //   reset and stays true for the whole out phase.
    if (gui.transition.out.execute) {
        state->HOVERED = false;
        state->CLICKED_LEFT = false;
        state->PRESSED_LEFT = false;
        state->DRAGGED_LEFT = false;
    }

    //opencode: edit lock — the editor's target canvas stays inert while
    //   edited (no hover/press/drag/click visuals, sounds, or actions;
    //   selection + outline are driven by the editor itself).
    if (canvas->editLock || gui.toolsInputCaptured) {
        state->HOVERED = false;
        state->HOVERED_ENTER = false;
        state->HOVERED_EXIT = false;
        state->CLICKED_LEFT = false;
        state->CLICKED_RIGHT = false;
        state->CLICKED_MIDDLE = false;
        state->PRESSED_LEFT = false;
        state->PRESSED_RIGHT = false;
        state->PRESSED_MIDDLE = false;
        state->DRAGGED_LEFT = false;
        state->TOGGLED = false;
    }

    //opencode: keyboard focus activation — Enter/Space acts as left click on focused element
    if (state->FOCUSED && state->CLICKABLE_LEFT && !canvas->editLock
        && !gui.toolsInputCaptured
        //opencode: audit — never activate a hidden/disabled/removed control
        //   (stale focus after rebuild must not fire the occupant of a slot)
        && gX_GUI_Canvas_Element_Eligible(canvas, element->id)) {
        if (input.keyboard.keys[gX_KEY_ENTER].state.PRESSED ||
            input.keyboard.keys[gX_KEY_KP_ENTER].state.PRESSED ||
            input.keyboard.keys[gX_KEY_SPACE].state.PRESSED) {
            state->CLICKED_LEFT = true;
        }
    }
    
    //opencode: GUI event logging — record state transitions for AI/script feedback
    // Only log on rising edges (false → true) to avoid spamming every frame.
    {
        // Click events (one-shot, always log when true)
        if (state->CLICKED_LEFT) {
            gX_I8 detail[64] = {0};
            if (element->texts.amount > 0 && element->texts.data) {
                gX_GUI_Element_Text *t = &((gX_GUI_Element_Text*)element->texts.data)[0];
                snprintf(detail, sizeof(detail), "%.63s", t->text);
            }
            gX_GUI_EventLog_Add(canvas->canvasID, (gX_I32)element->id, 'C', detail);
            gX_GUI_Trace_Event("click", canvas->canvasID,
                (gX_I32)element->id, detail);
        }
    }
    
}

gX_I0 gX_GUI_Element_Update(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    gX_GUI_Element_Update_State(element,canvas);
    
    //opencode: v0.3.8 — hover-reactive selection ("menu personality" pack,
    //   feature 3): frame-rate-independent scale pulse toward 1.06 on
    //   hover/focus (mouse + keyboard share the effect). The rect draw and
    //   the SDF surface enqueue both apply the scale around the center.
    if (element->state.HOVERABLE) {
        gX_F32 dt = (gX_F32)gX_FrameTime_Get();
        //opencode: v0.6.0 — motion tokens: hover pulse rates to motionShort;
        //   reduced motion (a11y) snaps the scale instead of animating.
        gX_F32 tau = gui.style.a11y.motionShort / 1000.0f;
        if (tau < 0.01f) tau = 0.15f;
        gX_F32 k = (gui.style.a11y.motionReduced) ? 1.0f : 1.0f - expf(-dt / tau);
        gX_F32 target = (element->state.HOVERED || element->state.FOCUSED) ? 1.06f : 1.0f;
        element->params.scale.x += (target - element->params.scale.x) * k;
        element->params.scale.y += (target - element->params.scale.y) * k;
    }
    
    gX_GUI_Element_Update_Animations(element);
    gX_GUI_Element_Update_Colors(element);
    gX_GUI_Element_Update_Datas(element);
    gX_GUI_Element_Update_Sounds(element);
    gX_GUI_Element_Update_Timers(element);
    gX_GUI_Element_Update_TextInputs(element);
    
}

extern gX_I0 gX_GUI_Element_Draw_Models(  gX_GUI_Element *element, gX_GUI_Canvas *canvas);
extern gX_I0 gX_GUI_Element_Draw_Shaders( gX_GUI_Element *element, gX_GUI_Canvas *canvas);
extern gX_I0 gX_GUI_Element_Draw_Shapes  (gX_GUI_Element *element, gX_GUI_Canvas *canvas);
extern gX_I0 gX_GUI_Element_Draw_Textures(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
extern gX_I0 gX_GUI_Element_Draw_Texts(   gX_GUI_Element *element, gX_GUI_Canvas *canvas);
extern gX_I0 gX_GUI_Element_Draw_TextInputs(gX_GUI_Element *element, gX_GUI_Canvas *canvas);

static gX_I0 gX_GUI_Element_Draw_Rect(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    if (element->stateRendering.RECT_NO_DRAW) return;
    if (!canvas) return; //opencode: guard against stale canvas reference
    
    gX_Rectangle_Parameters params = { 0 };
    
    //opencode: default all four corners to element->color × canvasHue.
    //   Was hardcoded to RED/GREEN/BLUE for corners [1-3], causing every
    //   element to render with a rainbow gradient background unless overridden.
    gX_Color hueColor = gX_Color_Mul(element->color, canvas->canvasHue);
    //opencode: pressed state — darken color when button is held
    if (element->state.PRESSED_LEFT) {
        hueColor.r = (gX_U8)((gX_U32)hueColor.r * 7 / 10);
        hueColor.g = (gX_U8)((gX_U32)hueColor.g * 7 / 10);
        hueColor.b = (gX_U8)((gX_U32)hueColor.b * 7 / 10);
    }
    //opencode: disabled state — mute value and alpha while keeping the
    //   surface visible enough to read as an unavailable control.
    if (element->state.DISABLED) {
        hueColor.r = (gX_U8)((gX_U32)hueColor.r * 7 / 10);
        hueColor.g = (gX_U8)((gX_U32)hueColor.g * 7 / 10);
        hueColor.b = (gX_U8)((gX_U32)hueColor.b * 7 / 10);
        hueColor.a = (gX_U8)((gX_U32)hueColor.a * 6 / 10);
    }
    //opencode: v0.3.8 — hover-reactive glow: brighten toward white on hover
    //   (subtle, complements the scale pulse)
    //opencode: main-menu polish — keyboard focus gets the same fill lift as
    //   pointer hover, so the selected row is not communicated by the ring alone.
    if ((element->state.HOVERED || element->state.FOCUSED)
        && !element->state.PRESSED_LEFT && !element->state.DISABLED) {
        hueColor.r = (gX_U8)((gX_U32)hueColor.r + ((gX_U32)(255 - hueColor.r) * 40 / 255));
        hueColor.g = (gX_U8)((gX_U32)hueColor.g + ((gX_U32)(255 - hueColor.g) * 40 / 255));
        hueColor.b = (gX_U8)((gX_U32)hueColor.b + ((gX_U32)(255 - hueColor.b) * 40 / 255));
    }

    //opencode: v0.2.0 — SDF-surface elements skip the flat batch rect and
    //   draw in element order so panels cannot cover later canvas content
    //opencode: v0.6.4 — elements carrying shapes/shaders/textures/models
    //   must NOT SDF (proven: pristine SDF buries composited children;
    //   gate ON = end-gui yellow, gate OFF = dark). Flat bg keeps layers.
    if (element->stateRendering.RECT_SDF_SURFACE
        && element->shapes.amount == 0 && element->shaders.amount == 0
        && element->textures.amount == 0 && element->models.amount == 0) {
        gX_GUI_WidgetSDF_DrawNow(element, canvas, hueColor);
        return;
    }

    params.colors[0] = hueColor;
    params.colors[1] = hueColor;
    params.colors[2] = hueColor;
    params.colors[3] = hueColor;
    //opencode: button identity — use a restrained vertical value gradient on
    //   buttons only, keeping white labels readable without styling text-only
    //   elements. gradientStrength=0 restores a flat theme.
    if (element->type == gX_GUI_ELEMENT_TYPE_BUTTON &&
        !element->stateRendering.GRADIENT_ENABLE) {
        gX_F32 strength = gui.style.gradientStrength;
        if (strength < 0.0f) strength = 0.0f;
        if (strength > 1.0f) strength = 1.0f;
        params.colors[0].r = (gX_U8)((gX_F32)hueColor.r * (1.0f - 0.04f * strength));
        params.colors[0].g = (gX_U8)((gX_F32)hueColor.g * (1.0f - 0.04f * strength));
        params.colors[0].b = (gX_U8)((gX_F32)hueColor.b * (1.0f - 0.04f * strength));
        params.colors[1].r = (gX_U8)((gX_F32)hueColor.r * (1.0f - 0.60f * strength));
        params.colors[1].g = (gX_U8)((gX_F32)hueColor.g * (1.0f - 0.60f * strength));
        params.colors[1].b = (gX_U8)((gX_F32)hueColor.b * (1.0f - 0.60f * strength));
        params.gradientVertical = strength > 0.001f;
    }
    //opencode: v0.3.8 — hover-reactive scale pulse
    params.scale = element->params.scale;
    
    //opencode: gradient color overrides — replace individual corners when
    //   the element has explicit gradientColor data attached.
    if (element->colors.amount > 0 && element->colors.data) {
        gX_GUI_Element_Color *colors = element->colors.data;
        for (gX_U32 i = 0; i <= 3; i++) {
            if (element->colors.amount > i) {
                if (colors[i].gradientColor) {
                    params.colors[i] = gX_Color_Mul(colors[i].color, canvas->canvasHue);
                }
            }
            else break;
        }
    }
    
    if (element->stateRendering.GRADIENT_ENABLE) {
        if (element->stateRendering.GRADIENT_STATE == 0) params.gradientVertical = true;
        if (element->stateRendering.GRADIENT_STATE == 1) params.gradientHorizontal = true;
        if (element->stateRendering.GRADIENT_STATE >= 2) params.gradientFourPoint = true;
    }
    if (element->stateRendering.RECT_DRAW_WIRE) {
        params.lines = true;
    }
    
    gX_Rectangle_Draw_Parameters(element->rect, params);
}

gX_I0 gX_GUI_Element_Draw(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    //opencode: rounded focus ring follows the button corner radius instead of
    //   using a square line around a rounded surface.
    if (element->state.FOCUSED) {
        gX_Rectangle focusRect = element->rect;
        gX_F32 ringOffset = gui.style.focusRingOffset;
        gX_F32 radius = (element->cornerRadius >= 0.0f)
            ? element->cornerRadius : gui.style.cornerRadius;
        gX_F32 minSize;
        gX_Rectangle_Parameters fp = { 0 };
        focusRect.x -= ringOffset;
        focusRect.y -= ringOffset;
        focusRect.width += ringOffset * 2.0f;
        focusRect.height += ringOffset * 2.0f;
        minSize = (focusRect.width < focusRect.height) ? focusRect.width : focusRect.height;
        fp.lines = true;
        fp.isRound = true;
        fp.roundness = (minSize > 0.0f) ? ((radius + ringOffset) * 2.0f / minSize) : 0.0f;
        fp.lineThickness = gui.style.borderWidth;
        fp.colors[0] = gui.style.focusRing;
        gX_Rectangle_Draw_Parameters(focusRect, fp);
    }

    gX_GUI_Element_Draw_Rect(element,canvas);
    
    gX_GUI_Element_Draw_Textures(element,canvas);
    
    gX_GUI_Element_Draw_Shaders(element,canvas);
    
    gX_GUI_Element_Draw_Models(element,canvas);
    
    gX_GUI_Element_Draw_Shapes(element,canvas);
    
    gX_GUI_Element_Draw_Texts(element,canvas);
    
    gX_GUI_Element_Draw_TextInputs(element,canvas);

    //opencode: tooltip — show element description on hover or keyboard focus
    if ((element->state.HOVERED || element->state.FOCUSED) && element->description[0]) {
        //opencode: engine UI font like every other text (was hardcoded default).
        gX_Font defFont = gui.style.uiFontLoaded ? gui.style.uiFont : gX_Font_GetDefault();
        //opencode: scale with the window like all UI type (was fixed 14px).
        gX_F32 tipScale = (gX_F32)maindata.screenWidth / 1024.0f * gX_scale_mult;
        if (tipScale < 0.5f) tipScale = 0.5f;
        if (tipScale > 4.0f) tipScale = 4.0f;
        gX_F32 tipSize = 14.0f * tipScale;
        //opencode: cursor-anchored on hover — element-anchored tips collided
        //   with tight rows once type scaled with the window (extras audit);
        //   keyboard focus keeps the element anchor (no cursor relation).
        gX_F32v2 tipPos;
        if (element->state.HOVERED) {
            tipPos.x = input.mouse.coord.x + 14.0f;
            tipPos.y = input.mouse.coord.y + 20.0f;
        } else {
            tipPos.x = element->rect.x;
            tipPos.y = element->rect.y + element->rect.height + 4.0f;
        }
        // flip above if below screen, clamp so top edge stays visible
        if (tipPos.y + tipSize > (gX_F32)maindata.screenHeight) {
            tipPos.y = element->rect.y - tipSize - 4.0f;
            if (tipPos.y < 2.0f) tipPos.y = 2.0f;
        }
        // clamp X so tooltip doesn't extend past right edge
        gX_F32v2 tipLen = gX_RawString_Measure(element->description, defFont, tipSize, 1);
        if (tipPos.x + tipLen.x + 8 > (gX_F32)maindata.screenWidth) {
            tipPos.x = (gX_F32)maindata.screenWidth - tipLen.x - 12.0f;
            if (tipPos.x < 2.0f) tipPos.x = 2.0f;
        }
        gX_Rectangle tipBg = {tipPos.x - 4, tipPos.y - 2, tipLen.x + 8, tipSize + 4};
        gX_Rectangle_Draw(tipBg, (gX_Color){8, 8, 14, 235});
        //opencode: visible plate edge — the dark fill vanishes on dark panels,
        //   making floating tips read as broken in-place text (extras audit).
        gX_Rectangle_Parameters tp = { 0 };
        tp.lines = true;
        tp.colors[0] = gui.style.blueprint.accentColor;
        gX_Rectangle_Draw_Parameters(tipBg, tp);
        gX_RawString_Draw(element->description, defFont, tipPos, (gX_F32v2){0,0}, 0.0f, tipSize, 1, gX_WHITE);
    }
    
}
