/**********************************************************************************************
*
*   [gX: src/gui/element/struct.h]
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


#ifndef GUI_ELEMENT_STRUCT_H
#define GUI_ELEMENT_STRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../common/text.h"
#include "../../common/heap.h"

#include "../config.h"
#include "../enum.h"

#include "struct_data.h"
#include "struct_executestate.h"
#include "timer.h"
#include "textinput.h"
    
typedef struct gX_GUI_Element_Animation {
    
    gX_GUI_Element_ExecuteState execute;
    gX_I1 executeAnyway;
    gX_I1 executing;
    gX_I1 executeHint;
    gX_I1 waiting;
    gX_I1 Tset;
    
    //listener
    gX_I1 readTransitionGUI;
    gX_I1 readTransitionCanvas;
    gX_I1 readTransitionOut;
    gX_I1 readTransitionIn;
    gX_I1 readCondition;
    
    //condition
    gX_I32 cond;
    gX_I32 *condPtr;
    
    //params
    gX_I1 oscillate;
    gX_I1 reverse;
    gX_I1 reverseWhenDone;
    gX_I1 reverseWhenNotExecute;
    gX_I1 infinite;
    
    gX_F32v4 offset;
    gX_F32v4 result;
    gX_I1 modeAdd;
    
    //unsafe
    gX_I1 doAny;
    gX_I32 datatype;
    gX_I0 *target;
    //-------safe-------//
    //common
    gX_I1 doRect;
    gX_I1 doColor;
    //params
    gX_I1 doScale;
    gX_I1 doAngle;
    gX_I1 doMatrixRow0;
    gX_I1 doMatrixRow1;
    gX_I1 doMatrixRow2;
    gX_I1 doMatrixRow3;
    //models
    gX_I32 modelIndex;
    gX_I1 doModelParamExtraColorMultiplier;
    gX_I1 doModelParamRotate;
    gX_I1 doModelParamTranslate;
    gX_I1 doModelParamScale;
    //texts
    gX_I32 textIndex;
    gX_I1 doTextFontSize;
    gX_I1 doTextOffset;
    gX_I1 doTextSpacing;
    gX_I1 doTextColor;
    //textures
    gX_I32 textureIndex;
    gX_I1 doTextureParamRect;
    gX_I1 doTextureParamScale;
    gX_I1 doTextureParamOrigin;
    gX_I1 doTextureParamRotation;
    gX_I1 doTextureParamTint;
    
    gX_GUI_Easing easing;
    gX_F32 durationWait;
    gX_F32 duration;
    gX_F32 T;
    
    gX_F32v4 start;
    gX_F32v4 end;

    //opencode: animation lifecycle callbacks
    gX_I0 (*onStart)(gX_I0 *userData);
    gX_I0 *onStartUserData;
    gX_I0 (*onComplete)(gX_I0 *userData);
    gX_I0 *onCompleteUserData;
    gX_I1 started;   // true after onStart fires
    
} gX_GUI_Element_Animation;
    
typedef struct gX_GUI_Element_Attribute {
    gX_F32v4    value;
} gX_GUI_Element_Attribute;

typedef struct gX_GUI_Element_Color {
    
    //target
    gX_I1       rectColor;
    gX_I1       textColor;
    gX_I1       shapeColor;
    gX_I1       gradientColor;
    gX_U32      textColorIndex;
    gX_U32      shapeColorIndex;
    
    gX_I1       modeMultiply;
    
    gX_I1       rainbow;
    gX_I1       smoothRandomColor;
    
    gX_F32      T;
    gX_F32      Tseconds;
    
    gX_GUI_Element_ExecuteState execute;
    
    gX_F32v4    factor;
    gX_Color    color;
    gX_Color    colorOriginal;
    
} gX_GUI_Element_Color;

typedef struct gX_GUI_Element_Shader_Locations {
    gX_I32      timeLoc;
    gX_I32      resLoc;
    gX_I32      resRenderLoc;
    gX_I32      rectLoc;
    gX_I32      colorLoc;
    gX_I32      scaleLoc;
    gX_I32      inputTexLoc;
    gX_I32      uniformLoc;
    
    gX_I32      camPosLoc;
    gX_I32      camTargetLoc;
    gX_I32      camUpLoc;
    gX_I32      camFovLoc;
    
    gX_I32      ambientFactorLoc;
    gX_I32      colorMultiplierLoc;
    gX_I32      lightDirLoc;
    
    gX_I32      backsideLoc;
    gX_I32      refIndexLoc;
    
    //opencode: matrix locs for 3D model draws with a custom element shader —
    //   the model module uploads mvp/model/normal/view/projection through
    //   these (same names raylib's default 3D shader uses).
    gX_I32      mvpLoc;
    gX_I32      modelLoc;
    gX_I32      normalLoc;
    gX_I32      viewLoc;
    gX_I32      projLoc;
} gX_GUI_Element_Shader_Locations;

typedef struct gX_GUI_Element_Model_ExtraParams {
    gX_I1 doGlass;
    gX_I1 doBackside;
    gX_F32 colorMultiplier;
    gX_F32 refIndex;
} gX_GUI_Element_Model_ExtraParams;

typedef struct gX_GUI_Element_Model {
    gX_I1       useShader;
    //opencode: index into element->shaders — the shader ELEMENT owns the
    //   program, locs, uniforms, and hot-reload. Replaces the old raw
    //   gX_Shader* + never-populated locs (uniforms silently went to GL
    //   location 0).
    gX_U32      shaderIndex;
    gX_I1       useParameters;
    gX_GUI_Element_Model_ExtraParams extraparams;
    
    gX_Matrix   transform;
    gX_F32v3    paramRotate;
    gX_F32v3    paramTranslate;
    gX_F32v3    paramScale;
    
    gX_Model   *model;
    
} gX_GUI_Element_Model;

typedef struct gX_GUI_Element_Shader_Uniform {
    gX_F32v4 value;
    gX_F32v4 initial;
    gX_F32v4 delta;
} gX_GUI_Element_Shader_Uniform;

typedef struct gX_GUI_Element_Shader {
    gX_I1       loaded;
    gX_U32      loops;
    
    gX_I1       drawShader;
    
    gX_GUI_Element_Shader_Uniform  uniforms[4];
    
    gX_Shader  *shader;
    gX_GUI_Element_Shader_Locations locs;
    
    //opencode: declared size of the shader's `uniforms` array (0..4, queried
    //   at Load) — uploads are clamped to it (GL_INVALID_OPERATION otherwise)
    gX_U32      uniformCount;
    
    //opencode: program id whose locs are cached. The RM resource heap reloads
    //   shaders in place with a NEW program id that bypasses the watch table
    //   (resource.c mtime reload); Draw re-runs Load when this mismatches the
    //   live id so the element self-heals after a resource hot-reload.
    gX_U32      loadedProgramId;

    //opencode: fragment shader file path — set by Builder_Shader_Frag, used by
    //   pool migration to copy to gX_PoolShader entries. 128 bytes per shader
    //   element is acceptable; the struct will be removed once pool system
    //   replaces the old heap entirely.
    gX_I8       fragPath[128];
} gX_GUI_Element_Shader;

typedef struct gX_GUI_Element_Shape {
    gX_U32      state;
    gX_I32      type;
    gX_I32      integer[2];
    gX_F32      scalar[4];
    gX_F32v2    v2[4];
    gX_Color    color;
    //opencode: shader support — when useShader, area shapes (CIRCLE,
    //   TRIANGLE, RECTANGLE) draw via the referenced shader element with
    //   direct geometry (batch path can't use custom programs)
    gX_I1       useShader;
    gX_U32      shaderIndex;
} gX_GUI_Element_Shape;

//opencode: web box-model layout container (Phase 1 — block/flex, no CSS yet).
//   One layout module per element, added by Builder_Layout_Begin. Children are
//   the elements built between Layout_Begin and Layout_End (indices captured at
//   build time). The canvas resolve pass (gX_GUI_Canvas_Layout_Resolve) writes
//   each child's rect position/size from this container's display/justify/align.
typedef struct gX_GUI_Element_Layout {
    gX_U32      state;
    
    gX_I32      display;    // gX_GUI_ELEMENT_LAYOUT_DISPLAY_* (BLOCK/FLEX_ROW/FLEX_COL)
    gX_I32      justify;    // gX_GUI_ELEMENT_LAYOUT_JUSTIFY_* (main-axis distribution)
    gX_I32      align;      // gX_GUI_ELEMENT_LAYOUT_ALIGN_*     (cross-axis placement)
    
    gX_F32      gap;        // spacing between children (grid units)
    gX_F32      padding[4]; // top, right, bottom, left (grid units)
    gX_I1       wrap;       // flex lines wrap onto the cross axis when full

    gX_I32      children[gX_GUI_ELEMENT_DEFAULT_MAX_LAYOUT_CHILDREN];
    gX_U32      childCount;
    //opencode: member heap-slot range (inclusive) captured at Layout_End —
    //   lets an OUTER container skip slots owned by a nested container so
    //   grandchildren are never double-parented (-1 = not closed yet).
    gX_I32      memberFirst;
    gX_I32      memberLast;
} gX_GUI_Element_Layout;

typedef struct gX_GUI_Element_Sound {
    gX_U32      state;
    
    gX_GUI_Element_ExecuteState execute;
    
    gX_Sound   *sound;
    gX_Sound_Parameters params;
    
} gX_GUI_Element_Sound;

typedef struct gX_GUI_Element_Text {
    gX_I1       draw;
    gX_U32      state;
    
    gX_I8       text[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32      length;
    
    gX_F32      fontSize;
    gX_F32v2    offset;
    gX_I32      spacing;
    
    gX_Color    color;
    gX_Color    colorOriginal;
    
    gX_I1       hasCustomFont;
    gX_Font    *customFontPtr;
    gX_U32      customFontIndex;
    
    gX_I1       overflowLogged;  //opencode: prevent overflow warning spam
    gX_F32      lineSpacing;      //opencode: extra line height multiplier (0=default)
    gX_I1       wrap;             //opencode: v0.6.0 — word-wrap to the element rect width
    gX_I1       sdf;              //opencode: v0.6.0 — render with the SDF uiFont twin (crisp at scale)
    gX_I1       autoShrink;       //opencode: opt-in shrink-to-fit before wrap/truncation
    
} gX_GUI_Element_Text;

//opencode: draw-truth layout verdict for one text module — used by the GUI
//   layout audit trace and the headless fit tests.
typedef struct gX_GUI_Element_Text_Layout {
    gX_I1    hasText;
    gX_F32v2 measure;      // drawn bounds (post shrink/wrap/truncation)
    gX_F32v2 natural;      // measured at the configured size, single line
    gX_F32   rectWidth;
    gX_F32   rectHeight;
    gX_F32   lineHeight;
    gX_F32   shrinkScale;  // font scale applied by auto-shrink (1 = none)
    gX_I1    wraps;
    gX_I1    truncated;
    gX_I1    overflowX;
    gX_I1    overflowY;
} gX_GUI_Element_Text_Layout;

typedef struct gX_GUI_Element_Texture {
    gX_U32      state;
    
    gX_I1       useShader;
    gX_U32      shaderIndex;
    
    gX_Texture  *texture;
    gX_Texture_Draw_Params params;
    
} gX_GUI_Element_Texture;

typedef struct gX_GUI_Element_Parameters {
    gX_F32v2    scale;
    gX_F32      angle;
    gX_Matrix   matrix;
} gX_GUI_Element_Parameters;

typedef struct gX_GUI_Element_Rendering {
    gX_U32  USE_MATRIX          : 1;
    
    gX_U32  RECT_NO_DRAW        : 1;
    gX_U32  RECT_DRAW_WIRE      : 1;
    //opencode: v0.2.0 — rect renders via the shared SDF widget program
    //   (attached as an element-shader instance); flat batch rect skipped
    gX_U32  RECT_SDF_SURFACE    : 1;
    //opencode: auto-size element to content (text texture fills element rect)
    gX_U32  RECT_AUTO_WIDTH     : 1;
    gX_U32  RECT_AUTO_HEIGHT    : 1;
    
    gX_U32  TEXT_CENTER_X       : 1;
    gX_U32  TEXT_CENTER_Y       : 1;
    gX_U32  TEXT_SHADOW         : 1;
    gX_U32  TEXT_SIZE_RELATIVE  : 1;
    
    gX_U32  GRADIENT_ENABLE     : 1;
    gX_U32  GRADIENT_STATE      : 2;
    //opencode: per-element depth profile override — 0 = inherit the theme
    //   (surfaceDepth.mode), 1 = none, 2 = horizon, 3 = metallic.
    gX_U32  SURFACE_DEPTH       : 2;
    
    gX_U32  TEXTURE_3D              : 1;
    gX_U32  TEXTURE_CUSTOM_PARAMS   : 1;
    
    gX_U32  SHAPE_PARENTCOLOR   : 1;
} gX_GUI_Element_Rendering;

typedef union gX_GUI_Element_Rendering_Bitmask {
    gX_GUI_Element_Rendering rendering;
    //opencode: U32 not U64 — the bitfield struct is exactly 4 bytes; a U64
    //   member made the union 8 bytes, so writes through bits leaked 4 bytes
    //   into the adjacent element field.
    gX_U32 bits;
} gX_GUI_Element_Rendering_Bitmask;

typedef struct gX_GUI_Element_State {
    // Event flags — transient per-frame interaction
    gX_U32  HOVERED             : 1;
    gX_U32  HOVERED_ENTER       : 1;  // true on frame mouse enters element rect
    gX_U32  HOVERED_EXIT        : 1;  // true on frame mouse leaves element rect
    gX_U32  _prevHovered        : 1;  // internal: previous frame hover state
    gX_U32  TOGGLED             : 1;
    
    gX_U32  CLICKED_LEFT        : 1;
    gX_U32  CLICKED_RIGHT       : 1;
    gX_U32  CLICKED_MIDDLE      : 1;
    
    gX_U32  PRESSED_LEFT        : 1;
    gX_U32  PRESSED_RIGHT       : 1;
    gX_U32  PRESSED_MIDDLE      : 1;
    
    gX_U32  DRAGGED_LEFT        : 1;
    
    // Capability flags — element properties
    gX_U32  HOVERABLE           : 1;
    gX_U32  TOGGLEABLE          : 1;
    
    gX_U32  CLICKABLE_LEFT      : 1;
    gX_U32  PRESSABLE_LEFT      : 1;
    gX_U32  DRAGGABLE_LEFT      : 1;
    
    // Lifecycle flags
    gX_U32  KEEP_HEAP_ANIMATION     : 1;
    gX_U32  KEEP_HEAP_ATTRIBUTE     : 1;
    gX_U32  KEEP_HEAP_COLOR         : 1;
    gX_U32  KEEP_HEAP_DATA          : 1;
    gX_U32  KEEP_HEAP_MODEL         : 1;
    gX_U32  KEEP_HEAP_SHADER        : 1;
    gX_U32  KEEP_HEAP_SHAPE         : 1;
    gX_U32  KEEP_HEAP_SOUND         : 1;
    gX_U32  KEEP_HEAP_TEXT          : 1;
    gX_U32  KEEP_HEAP_TEXTURE       : 1;
    gX_U32  KEEP_HEAP_TIMER         : 1;
    gX_U32  KEEP_HEAP_TEXTINPUT     : 1;

    gX_U32  IGNORE_OCCLUSION    : 1;
    
    // Misc
    gX_U32  FOCUSABLE           : 1;
    gX_U32  FOCUSED             : 1;
    
    gX_U32  DISABLED            : 1;
} gX_GUI_Element_State;

typedef union gX_GUI_Element_State_Bitmask {
    gX_GUI_Element_State state;
    //opencode: U32 not U64 — the bitfield struct is exactly 4 bytes (32 bits);
    //   a U64 member made the union 8 bytes, so writes through bits leaked
    //   4 bytes into stateRendering (and reads misaligned on strict-alignment
    //   targets).
    gX_U32 bits;
} gX_GUI_Element_State_Bitmask;

typedef struct gX_GUI_Element_Function {
    gX_U8   OPERATOR            : 5;
    gX_I8   ID                  : 8;
    
    gX_U8   PADDING             : 3;
} gX_GUI_Element_Function;

typedef struct gX_GUI_Element {
    gX_I8   name[gX_GUI_ELEMENT_NAMEBFR_SIZE];
    gX_I8   description[gX_GUI_ELEMENT_DESCBFR_SIZE];
    
    //opencode: gameCSS — space-separated class list (matched by .class
    //   selectors; element->name is the #id target) + inline style string
    //   (origin 2 — always beats stylesheet rules)
    gX_I8   cssClass[64];
    gX_I8   cssStyle[256];
    
    gX_U32          id;
    //opencode: runtime-only generation distinguishes a reused heap slot from
    //   its previous occupant. Editor handles use id + generation together.
    gX_U32          generation;
    gX_Rectangle    rect;
    gX_Color        color;
    
    //opencode: web box-model per-element margins (grid units) — used only by
    //   layout containers when positioning this element as a child.
    gX_F32          margin[4]; // top, right, bottom, left
    //opencode: flex sizing (grid units unless noted) — grow distributes
    //   leftover main-axis space by weight; min/max clamp the main axis
    //   after growth (<=0 = unset). Unitless grow.
    gX_F32          flexGrow;
    gX_F32          flexMin[2]; // w, h
    gX_F32          flexMax[2]; // w, h
    
    gX_GUI_Element_Parameters   params;
    
    gX_GUI_Element_Type       type;          // element classification (BUTTON, LABEL, etc.)
    gX_U8                     layer;         // z-order (0=default, 255=top)
    gX_I32                    tag;           // user-defined group tag (0=unset)
    gX_I0                    *userData;      // arbitrary attached data (runtime-only, not serialized)
    //opencode: full U32 not bitfield — schema serialization needs &visible
    //   (a bitfield can't have its address taken)
    gX_U32                    visible;       // per-element visibility toggle
    
    gX_GUI_Element_State        state;
    //opencode: v0.2.0 — per-element SDF radius override (-1 = use theme)
    gX_F32                       cornerRadius;
    gX_GUI_Element_Rendering    stateRendering;
    
    gX_Heap         animations;
    gX_Heap         attributes;
    gX_Heap         colors;
    gX_Heap         datas;
    gX_Heap         layouts;
    gX_Heap         models;
    gX_Heap         shaders;
    gX_Heap         shapes;
    gX_Heap         sounds;
    gX_Heap         texts;
    gX_Heap         textures;
    gX_Heap         timers;
    gX_Heap         textinputs;
    
} gX_GUI_Element;

#ifdef __cplusplus
}
#endif

#endif //GUI_ELEMENT_STRUCT_H
