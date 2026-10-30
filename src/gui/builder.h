/**********************************************************************************************
*
*   [gX: src/gui/builder.h]
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


#ifndef GUI_BUILDER_H
#define GUI_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/heap.h"

//opencode: forward declarations for element sub-module types used in builder struct
typedef struct gX_GUI_Element_Timer gX_GUI_Element_Timer;

#include "canvas.h"
#include "element/struct.h"
#include "binding.h"

//opencode: forward declaration for ColorPicker widget
typedef struct gX_ColorPicker gX_ColorPicker;

//opencode: immediate-mode GUI element builder — replaces the macro-based template DSL.
//Usage:
//   gX_GUI_Builder b = gX_GUI_Builder_New(canvas, "name", "desc", TEMPLATE_ID);
//   gX_GUI_Builder_Element_Begin(&b);
//       gX_GUI_Builder_Rect(&b, x, y, w, h);
//       gX_GUI_Builder_Color(&b, 255, 255, 255, 255);
//       gX_GUI_Builder_Texture_Begin(&b);        // optional sub-module
//           gX_GUI_Builder_Texture_File(&b, "tex.png");
//       gX_GUI_Builder_Texture_End(&b);
//   gX_GUI_Builder_Element_End(&b);
//   gX_GUI_Builder_Free(&b);

typedef struct gX_GUI_Builder {
    gX_GUI_Canvas       *canvas;
    gX_GUI_Element      *currentElement;
    gX_I32              elementIndex;

    gX_I32              width;
    gX_I32              height;
    gX_I32              widthDiv;
    gX_I32              heightDiv;
    gX_F32              aspRatio;

    //resource pointers (set once at New, used by Begin_* helpers)
    gX_Heap             *shaderHeap;
    gX_Shader           *shaders;
    gX_Heap             *textureHeap;
    gX_Texture          *textures;
    gX_Heap             *fontHeap;
    gX_Font             *fonts;
    gX_Heap             *modelHeap;
    gX_Model            *models;
    gX_Heap             *soundHeap;
    gX_Sound            *sounds;

    //current sub-module pointers (set by Begin_*, nulled by End_*)
    gX_GUI_Element_Texture      *curTexture;
    gX_GUI_Element_Text         *curText;
    gX_GUI_Element_Shader       *curShader;
    gX_GUI_Element_Model        *curModel;
    gX_GUI_Element_Shape        *curShape;
    gX_GUI_Element_Animation    *curAnimation;
    gX_GUI_Element_Data         *curData;
    gX_GUI_Element_Sound        *curSound;
    gX_GUI_Element_Attribute    *curAttribute;
    gX_GUI_Element_Timer        *curTimer;
    gX_GUI_Element_Color        *curColor;

    //module index counters (incremented on each Begin_*)
    gX_I32 texIdx, txtIdx, shdIdx, mdlIdx, shpIdx, aniIdx, datIdx, sndIdx, attIdx, colIdx, timIdx;
    
    //opencode: layout container stack — allows nested containers for relative/fill positioning.
    //   Layout_Begin pushes a container rect in grid units; Layout_End pops it.
    //   Elements inside can use RectGrid with gw=0 or gh=0 to fill the container,
    //   or with positive values for normal grid-relative placement.
    //opencode: box-model layout — Layout_Begin also creates a layout CONTAINER
    //   element; elements built between Begin/End become its children (captured
    //   at End by heap slot id). curLayout/layoutModules/layoutSlots track the
    //   nesting so Layout_* setters write the innermost open container.
    gX_I32       layoutDepth;
    gX_Rectangle layoutStack[8];  // each entry is a grid-relative rect {gx,gy,gw,gh}
    gX_GUI_Element_Layout *layoutModules[8];
    gX_I32       layoutSlots[8];
    gX_GUI_Element_Layout *curLayout;
} gX_GUI_Builder;

//lifecycle
gX_GUI_Builder gX_GUI_Builder_New(gX_GUI_Canvas *canvas, const gX_I8 *name, const gX_I8 *desc, gX_I32 templateID);
gX_I0 gX_GUI_Builder_Free(gX_GUI_Builder *b);

//element (call once per element)
gX_I0 gX_GUI_Builder_Element_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Element_End(gX_GUI_Builder *b);

//core element properties
gX_I0 gX_GUI_Builder_Rect(gX_GUI_Builder *b, gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h);
gX_I0 gX_GUI_Builder_RectGrid(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh);
//opencode: anchor placement — position within the current container (or the
//   canvas grid when outside one). START pins to the origin edge, CENTER
//   centers, END pins to the far edge; offX/offY shift in grid units.
#define GX_GUI_ANCHOR_START  0
#define GX_GUI_ANCHOR_CENTER 1
#define GX_GUI_ANCHOR_END    2
gX_I0 gX_GUI_Builder_RectAnchored(gX_GUI_Builder *b, gX_F32 gridW, gX_F32 gridH,
    gX_I32 hAnchor, gX_I32 vAnchor, gX_F32 offX, gX_F32 offY);
//opencode: percent placement — all fractions (0..1) of the current container
//   box (or the canvas grid when outside one).
gX_I0 gX_GUI_Builder_RectPercent(gX_GUI_Builder *b, gX_F32 xFrac, gX_F32 yFrac,
    gX_F32 wFrac, gX_F32 hFrac);

//opencode: layout container — push a grid-space container rect for relative/fill positioning.
//   Elements inside a Layout_Begin/End pair can set gw=0 or gh=0 in RectGrid to fill the
//   container width/height, or use positive grid values for absolute positioning within it.
//   Nesting depth is limited to 8; exceeding it logs a warning and is a no-op.
//opencode: box-model layout — Layout_Begin now ALSO creates a layout container
//   element at the given grid rect. Elements built between Begin and End become
//   its children: the layout resolve pass (Canvas_Draw) positions each child
//   from display/justify/align/padding/gap + per-child margins. Children should
//   set their SIZE via RectGrid/Rect (position is overwritten by the resolver).
gX_I0 gX_GUI_Builder_Layout_Begin(gX_GUI_Builder *b, gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh);
gX_I0 gX_GUI_Builder_Layout_End(gX_GUI_Builder *b);

//opencode: box-model layout properties (grid units for gap/padding/margin)
gX_I0 gX_GUI_Builder_Layout_Display(gX_GUI_Builder *b, gX_I32 display);
gX_I0 gX_GUI_Builder_Layout_Justify(gX_GUI_Builder *b, gX_I32 justify);
gX_I0 gX_GUI_Builder_Layout_Align(gX_GUI_Builder *b, gX_I32 align);
gX_I0 gX_GUI_Builder_Layout_Gap(gX_GUI_Builder *b, gX_F32 gap);
gX_I0 gX_GUI_Builder_Layout_Padding(gX_GUI_Builder *b, gX_F32 top, gX_F32 right, gX_F32 bottom, gX_F32 left);
gX_I0 gX_GUI_Builder_Layout_Wrap(gX_GUI_Builder *b, gX_I1 wrap);
gX_I0 gX_GUI_Builder_Layout_Grow(gX_GUI_Builder *b, gX_F32 grow);
gX_I0 gX_GUI_Builder_Layout_Min(gX_GUI_Builder *b, gX_F32 minW, gX_F32 minH);
gX_I0 gX_GUI_Builder_Layout_Max(gX_GUI_Builder *b, gX_F32 maxW, gX_F32 maxH);
//opencode: per-child margins (grid units) — set on a child element inside a
//   layout container before Layout_End
gX_I0 gX_GUI_Builder_Margin(gX_GUI_Builder *b, gX_F32 top, gX_F32 right, gX_F32 bottom, gX_F32 left);

//opencode: gameCSS — attach a stylesheet to the canvas (loaded/cached in the
//   global registry, hot-reloaded on mtime change), tag the current element
//   with a class list / name (#id target) / inline style string
gX_I0 gX_GUI_Builder_CSS(gX_GUI_Builder *b, const gX_I8 *path);
gX_I0 gX_GUI_Builder_Class(gX_GUI_Builder *b, const gX_I8 *cls);
gX_I0 gX_GUI_Builder_Style(gX_GUI_Builder *b, const gX_I8 *css);
gX_I0 gX_GUI_Builder_Name(gX_GUI_Builder *b, const gX_I8 *name);
gX_I0 gX_GUI_Builder_Color(gX_GUI_Builder *b, gX_U8 r, gX_U8 g, gX_U8 blu, gX_U8 a);
gX_I0 gX_GUI_Builder_ColorV(gX_GUI_Builder *b, gX_Color color);

//sub-modules (Begin_* adds the module, End_* closes it)
gX_I0 gX_GUI_Builder_Texture_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Texture_File(gX_GUI_Builder *b, const gX_I8 *path);
gX_I0 gX_GUI_Builder_Texture_Set(gX_GUI_Builder *b, gX_Texture *tex);
//opencode: attach a shader element (index into element->shaders) to the texture
gX_I0 gX_GUI_Builder_Texture_Shader(gX_GUI_Builder *b, gX_U32 index);
gX_I0 gX_GUI_Builder_Texture_InvertY(gX_GUI_Builder *b, gX_I1 invert);
gX_I0 gX_GUI_Builder_Texture_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Text_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Text_Set(gX_GUI_Builder *b, const gX_I8 *text);
gX_I0 gX_GUI_Builder_Text_FontSize(gX_GUI_Builder *b, gX_F32 size);
//opencode: shrink long labels to fit the element width (floor in config.h);
//   opt-in per element — not a global typography rule.
gX_I0 gX_GUI_Builder_Text_AutoShrink(gX_GUI_Builder *b, gX_I1 enable);
//opencode: per-element text shadow using the gui.style.textShadow tokens
//   (bitmap and SDF text).
gX_I0 gX_GUI_Builder_Text_Shadow(gX_GUI_Builder *b, gX_I1 enable);
gX_I0 gX_GUI_Builder_Text_Offset(gX_GUI_Builder *b, gX_F32 x, gX_F32 y);
gX_I0 gX_GUI_Builder_Text_Spacing(gX_GUI_Builder *b, gX_I32 spacing);
gX_I0 gX_GUI_Builder_Text_Color(gX_GUI_Builder *b, gX_Color color);
gX_I0 gX_GUI_Builder_Text_CenterX(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Text_CenterY(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Text_Font(gX_GUI_Builder *b, gX_Font *font);
gX_I0 gX_GUI_Builder_Text_FontDefault(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Text_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Shader_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Shader_Frag(gX_GUI_Builder *b, const gX_I8 *fragFile);
gX_I0 gX_GUI_Builder_Shader_Loops(gX_GUI_Builder *b, gX_U32 loops);
gX_I0 gX_GUI_Builder_Shader_Draw(gX_GUI_Builder *b, gX_I1 draw);
gX_I0 gX_GUI_Builder_Shader_Uniform(gX_GUI_Builder *b, gX_U32 index, gX_F32v4 value);
gX_I0 gX_GUI_Builder_Shader_End(gX_GUI_Builder *b);
//opencode: Aero-glass surface shorthand — glass.frag + gui.style.glass
//   uniform upload; zero tokens / GX_GUI_GLASS=0 keep the flat fallback.
gX_I0 gX_GUI_Builder_Shader_Glass(gX_GUI_Builder *b);
//opencode: per-element surface depth override (0 inherit, 1 none, 2 horizon,
//   3 metallic) — lets a theme mix surfaces without a second theme.
gX_I0 gX_GUI_Builder_SurfaceDepth(gX_GUI_Builder *b, gX_I32 mode);

gX_I0 gX_GUI_Builder_Model_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Model_File(gX_GUI_Builder *b, const gX_I8 *path);
//opencode: attach a shader element (index into element->shaders) to the model
gX_I0 gX_GUI_Builder_Model_Shader(gX_GUI_Builder *b, gX_U32 index);
gX_I0 gX_GUI_Builder_Model_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Shape_Begin(gX_GUI_Builder *b, gX_I32 type);
//opencode: attach a shader element (index into element->shaders) to the shape
gX_I0 gX_GUI_Builder_Shape_Shader(gX_GUI_Builder *b, gX_U32 index);
gX_I0 gX_GUI_Builder_Shape_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Animation_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Animation_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Data_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Data_Src(gX_GUI_Builder *b, gX_I0 *ptr, gX_I32 datatype);
gX_I0 gX_GUI_Builder_Data_Dst(gX_GUI_Builder *b, gX_I0 *ptr, gX_I32 datatype);
gX_I0 gX_GUI_Builder_Data_Format(gX_GUI_Builder *b, const gX_I8 *fmt);
gX_I0 gX_GUI_Builder_Data_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Sound_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Sound_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Timer_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Timer_Mode(gX_GUI_Builder *b, gX_I32 mode);
gX_I0 gX_GUI_Builder_Timer_Duration(gX_GUI_Builder *b, gX_F32 seconds);
gX_I0 gX_GUI_Builder_Timer_OnFire(gX_GUI_Builder *b, gX_I0 (*cb)(gX_I0*), gX_I0 *userData);
gX_I0 gX_GUI_Builder_Timer_End(gX_GUI_Builder *b);

gX_I0 gX_GUI_Builder_Attribute_Begin(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Attribute_XY(gX_GUI_Builder *b, gX_F32 x, gX_F32 y);
gX_I0 gX_GUI_Builder_Attribute_End(gX_GUI_Builder *b);

//rendering & state flags
gX_I0 gX_GUI_Builder_RenderFlags(gX_GUI_Builder *b, gX_U32 flags);
gX_I0 gX_GUI_Builder_StateFlags(gX_GUI_Builder *b, gX_U32 flags);

//=============================================================================
// widget templates — high-level composable UI patterns
//=============================================================================

//standard interactive button: rect + color + centered text + hover effect + click state binding
//Automatically sets HOVERABLE, CLICKABLE_LEFT, PRESSABLE_LEFT and binds targetState->statePtr on click.
//gridX/Y/W/H are in grid units (1/8 of screen). statePtr is dereferenced — pass &variable.
gX_I0 gX_GUI_Builder_Widget_Button(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I32 targetState, gX_I32 *statePtr);

//back navigation button — compact button that sets a state value
gX_I0 gX_GUI_Builder_Widget_Back(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY,
    gX_I32 targetState, gX_I32 *statePtr);

//text card header — gradient background with centered text and custom font
gX_I0 gX_GUI_Builder_Widget_TextCard(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text);

//draggable slider — rect + label + shape + potentiometer data binding to a float value
gX_I0 gX_GUI_Builder_Widget_Slider(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_F32 *value, gX_F32 min, gX_F32 max);

//texture button — rect + texture display with optional shader
gX_I0 gX_GUI_Builder_Widget_TextureButton(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_Texture *texture);

//checkbox — rect + text + toggle bool value on click
gX_I0 gX_GUI_Builder_Widget_CheckBox(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I1 *value);

//dropdown — rect + text + cycle through integer options on click
gX_I0 gX_GUI_Builder_Widget_DropDown(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I32 *value, gX_I32 maxValue);

//radio — rect + text + set a specific integer value on click (for radio groups)
gX_I0 gX_GUI_Builder_Widget_Radio(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *text, gX_I32 setValue, gX_I32 *statePtr);

//color picker widget — wraps gX_ColorPicker as a builder widget
//Caller must keep picker alive and call gX_ColorPicker_Update/Draw each frame.
//targetR/G/B are persistent pointers to color bytes to control.
gX_I0 gX_GUI_Builder_Widget_ColorPicker(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY,
    gX_U8 *targetR, gX_U8 *targetG, gX_U8 *targetB,
    gX_ColorPicker *picker);

//toggle switch — binary on/off toggle with label
//Fires via gX_GUI_OnClick_Context; the callback context lives in the element's
//data module, so no per-rebuild pool reset is needed.
gX_I0 gX_GUI_Builder_Widget_ToggleSwitch(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I1 *value);

//stepper/spinner — decrement / value display / increment
//valuePtr points to the variable (edits via gX_GUI_OnClick callbacks)
//datatype: gX_DATATYPE_F32 or gX_DATATYPE_I32
gX_I0 gX_GUI_Builder_Widget_Stepper(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *label, gX_I0 *valuePtr, gX_I32 datatype,
    gX_F32 step, gX_F32 min, gX_F32 max);

//progress bar — track with colored fill, optional percentage text
//value is a pointer to current value (0..100). Use value = &someFloat.
gX_I0 gX_GUI_Builder_Widget_ProgressBar(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_F32 *value, gX_F32 min, gX_F32 max, gX_Color fillColor, gX_I1 showPercent);

//opencode: text input — single-line editable text. textPtr (optional) is
//   synced from the module each frame (always kept up to date); textCapacity
//   must be the full byte size of textPtr (0 disables syncing). placeholder
//   shows when empty. password mode masks with '*'. Enter fires onCommit.
//   The module lives on the element (gX_GUI_Element_TextInput_Get(element, 0)).
gX_I0 gX_GUI_Builder_Widget_TextInput(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *name, gX_I8 *textPtr, gX_U32 textCapacity, gX_U32 maxLength,
    const gX_I8 *placeholder, gX_I1 passwordMode,
    gX_I0 (*onCommit)(gX_I0 *userData), gX_I0 *onCommitUserData);

//scrollable list — container + scrollbar + N items
//items is a null-terminated string array ("item1\0item2\0item3\0\0")
//selectedIndex is updated on click, returns -1 if none selected
gX_I0 gX_GUI_Builder_Widget_ScrollableList(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 *items, gX_I32 itemCount, gX_I32 *selectedIndex,
    gX_I32 *scrollOffset);
//opencode: v0.6.x — drops the scroll-list geometry registry entries when all
//   canvases are rebuilt. Per-element callback contexts are element-owned, so
//   they need no reset.
gX_I0 gX_GUI_Builder_Widget_ScrollList_ResetRegistry(gX_I0);
//opencode: scroll-list runtime — wheel over the list, keyboard nav on focused
//   items, and thumb drag via pointer capture. Called from gX_GUI_Update;
//   returns true when the input was consumed.
gX_I1 gX_GUI_ScrollList_UpdateWheel(gX_I0);
gX_I1 gX_GUI_ScrollList_UpdateDrag(gX_I0);
gX_I1 gX_GUI_ScrollList_HandleNavKeys(gX_I1 up, gX_I1 down, gX_I1 pgup,
    gX_I1 pgdn, gX_I1 home, gX_I1 end);
//opencode: pure scroll math — headless-testable, no input or heap access.
gX_I32 gX_GUI_Scroll_Clamp(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems);
gX_I32 gX_GUI_Scroll_WheelStep(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems, gX_I32 wheelSteps);
gX_I32 gX_GUI_Scroll_PageStep(gX_I32 offset, gX_I32 totalItems, gX_I32 visibleItems, gX_I32 pages);
//opencode: thumb-drag mapping — buffer-space pointer Y onto the scroll range.
gX_I32 gX_GUI_Scroll_DragStep(gX_F32 listY, gX_F32 listH, gX_F32 thumbH,
    gX_F32 mouseY, gX_I32 totalItems, gX_I32 visibleItems);
gX_I0 gX_GUI_Scroll_MoveSelection(gX_I32 *selected, gX_I32 *scroll,
    gX_I32 totalItems, gX_I32 visibleItems, gX_I32 delta);

//tab panel — row of tab buttons + content area
//tabLabels is a null-terminated string array
//activeTab is the currently selected tab index
gX_I0 gX_GUI_Builder_Widget_TabPanel(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    const gX_I8 **tabLabels, gX_I32 tabCount, gX_I32 *activeTab);

//opencode: section divider + header label
gX_F32 gX_GUI_Builder_Widget_Section(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw,
    const gX_I8 *label, gX_F32 labelSize);

//opencode: animation builder helpers
gX_I0 gX_GUI_Builder_Animation_HoverScale(gX_GUI_Builder *b, gX_F32 scaleFactor);
gX_I0 gX_GUI_Builder_Animation_StateEntrance(gX_GUI_Builder *b,
    gX_I32 state, gX_I32 *statePtr, gX_F32 duration);
gX_I0 gX_GUI_Builder_Animation_TransitionFade(gX_GUI_Builder *b);

//opencode: spinner / loading indicator
gX_I0 gX_GUI_Builder_Widget_Spinner(gX_GUI_Builder *b,
    gX_F32 gridX, gX_F32 gridY, gX_F32 gridW, gX_F32 gridH,
    gX_Color color);

//opencode: reusable button sub-effects (generic, not title2-specific)
gX_I0 gX_GUI_Builder_Widget_AddHoverColor(gX_GUI_Builder *b, gX_Color hover, gX_F32 factor);
gX_I0 gX_GUI_Builder_Widget_AddHoverClickSounds(gX_GUI_Builder *b);
gX_I0 gX_GUI_Builder_Widget_AddButtonAnimations(gX_GUI_Builder *b, gX_I32 target, gX_I32 *statePtr);

//opencode: high-level text button — complete interactive button in one call
//   Text button: RectGrid + Color + Text + Template_Button + hover color + sounds
//   If onClick is non-NULL: uses it as the click callback with onClickArg
//   If statePtr is non-NULL: sets *statePtr = targetState on click
//   If both are NULL: button has no action (decorative)
gX_I0 gX_GUI_Builder_Widget_TextButton(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *text, gX_Color baseColor, gX_F32 hoverFactor,
    gX_I32 targetState, gX_I32 *statePtr,
    gX_GUI_ClickCallback onClick, gX_I0 *onClickArg);
// Same as Widget_TextButton but with texture + wireframe instead of text + fill
// Note: differs from Widget_TextureButton (6-param display-only version)
gX_I0 gX_GUI_Builder_Widget_TextureButtonEx(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    gX_Texture *tex, gX_Color wireColor, gX_Color hoverColor,
    gX_I32 targetState, gX_I32 *statePtr,
    gX_GUI_ClickCallback onClick, gX_I0 *onClickArg);

//opencode: gradient background panel — full-grid gradient rect with 2 color sub-modules.
//   Creates a gradient from topColor to bottomColor over the full 8x8 grid.
//   Equivalent to: gradient rect + gradient colors[0].top + colors[1].bottom.
gX_I0 gX_GUI_Builder_Widget_GradientPanel(gX_GUI_Builder *b,
    gX_Color topColor, gX_Color bottomColor);

//opencode: action button — calls a function pointer on click (via data isFunctionPtr).
//   Unlike Widget_Button (state binding) and Widget_TextButton (onClick callback),
//   this uses the raw function pointer path for calling functions like
//   gX_Settings_Apply, gX_Settings_Reset, gX_Settings_Save directly.
gX_I0 gX_GUI_Builder_Widget_ActionButton(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *text, gX_I0 (*funcPtr)(gX_I0 *userData));

//opencode: set tooltip text on the current element — wraps strncpy to element->description.
//   Call AFTER gX_GUI_Builder_Element_Begin but BEFORE or AFTER Element_End
//   (sets a field on b->currentElement).
gX_I0 gX_GUI_Builder_Widget_SetTooltip(gX_GUI_Builder *b, const gX_I8 *tooltip);

//opencode: form row — label text element + widget element in a single grid row.
//   Creates: background rect with section header color, label text with tooltip,
//   then calls the provided builderFn to fill the widget area.
//   The builderFn receives the builder, gridX + labelWidth for label area,
//   gridX + labelWidth for widget area start, gridW - labelWidth for widget width.
typedef gX_I0 (*gX_GUI_Builder_Widget_FormRow_Fn)(gX_GUI_Builder *b,
    gX_F32 labelX, gX_F32 labelY, gX_F32 labelW,
    gX_F32 widgetX, gX_F32 widgetY, gX_F32 widgetW, gX_F32 rowH);
gX_I0 gX_GUI_Builder_Widget_FormRow(gX_GUI_Builder *b,
    gX_F32 gx, gX_F32 gy, gX_F32 gw, gX_F32 gh,
    const gX_I8 *label, const gX_I8 *tooltip,
    gX_GUI_Builder_Widget_FormRow_Fn builderFn);

#ifdef __cplusplus
}
#endif

#endif //GUI_BUILDER_H
