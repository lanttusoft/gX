/**********************************************************************************************
*
*   [gX: src/gui/css.h]
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


#ifndef GUI_CSS_H
#define GUI_CSS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#include "config.h"
#include "enum.h"

//opencode: gameCSS — a CSS-subset engine for the GUI (Phase 2).
//
//   Cascade (later wins), compared lexicographically in this order:
//   origin (builder-set element fields 0 < stylesheet rules 1 < inline
//   `style:` attribute 2), then !important, then specificity (id > class >
//   type), then source order within the origin. Matching declarations are
//   collected without a candidate cap (allocated to fit), so the winning
//   rule is never silently truncated. Pseudo-states match the element's
//   live state bits (HOVERED/FOCUSED/PRESSED_LEFT/DISABLED).
//
//   Selectors: type, #id (matches element->name), .class, pseudo (:hover,
//   :focus, :active, :disabled), one-level descendant (e.g. `panel .btn`),
//   comma groups. Specificity (a,b,c) = id, class, type.
//
//   Values: plain numbers = canvas GRID UNITS (consistent with the builder;
//   px accepted as alias); colors #rgb/#rrggbb/#rrggbbaa, rgb()/rgba(),
//   named (raylib palette + common CSS names); keywords (start/center/end/
//   space-between/stretch, block/flex-row/flex-col, none/visible).
//
//   v1 property set (parse-verified; unsupported ones are accepted and
//   ignored): display, position, flex-direction, justify-content,
//   align-items, gap, flex-grow, order, width, height, min/max-width/height,
//   margin[-(top|right|bottom|left)], padding[-(...)], border-*, box-sizing,
//   font-size, color, text-align, line-height, background-color, opacity,
//   layer, z-index, visible, angle, shader-index.

typedef struct gX_GUI_Element gX_GUI_Element;
typedef struct gX_GUI_Canvas gX_GUI_Canvas;

#define gX_GUI_CSS_MAX_RULES_PER_SHEET 64
#define gX_GUI_CSS_MAX_DECLS_PER_RULE  16
#define gX_GUI_CSS_MAX_SHEETS          4  //opencode: compression P5 — was 8 (~415K bss); the registry has a single loader call site (markup.c css field) and no resource in gX populates it; 2x headroom over any plausible use
#define gX_GUI_CSS_MAX_CANVAS_SHEETS   4
#define gX_GUI_CSS_STR_MAX             64

//opencode: supported property IDs
typedef enum gX_CSS_Prop {
    gX_CSS_PROP_NULL = 0,
    gX_CSS_PROP_DISPLAY,
    gX_CSS_PROP_POSITION,
    gX_CSS_PROP_FLEX_DIRECTION,
    gX_CSS_PROP_JUSTIFY_CONTENT,
    gX_CSS_PROP_ALIGN_ITEMS,
    gX_CSS_PROP_GAP,
    gX_CSS_PROP_FLEX_GROW,
    gX_CSS_PROP_ORDER,
    gX_CSS_PROP_WIDTH,
    gX_CSS_PROP_HEIGHT,
    gX_CSS_PROP_MIN_WIDTH,
    gX_CSS_PROP_MIN_HEIGHT,
    gX_CSS_PROP_MAX_WIDTH,
    gX_CSS_PROP_MAX_HEIGHT,
    gX_CSS_PROP_MARGIN,
    gX_CSS_PROP_MARGIN_TOP,
    gX_CSS_PROP_MARGIN_RIGHT,
    gX_CSS_PROP_MARGIN_BOTTOM,
    gX_CSS_PROP_MARGIN_LEFT,
    gX_CSS_PROP_PADDING,
    gX_CSS_PROP_PADDING_TOP,
    gX_CSS_PROP_PADDING_RIGHT,
    gX_CSS_PROP_PADDING_BOTTOM,
    gX_CSS_PROP_PADDING_LEFT,
    gX_CSS_PROP_BORDER_WIDTH,
    gX_CSS_PROP_BORDER_COLOR,
    gX_CSS_PROP_BORDER_RADIUS,
    gX_CSS_PROP_BOX_SIZING,
    gX_CSS_PROP_FONT_SIZE,
    gX_CSS_PROP_COLOR,
    gX_CSS_PROP_TEXT_ALIGN,
    gX_CSS_PROP_LINE_HEIGHT,
    gX_CSS_PROP_BACKGROUND_COLOR,
    gX_CSS_PROP_OPACITY,
    gX_CSS_PROP_LAYER,
    gX_CSS_PROP_Z_INDEX,
    gX_CSS_PROP_VISIBLE,
    gX_CSS_PROP_ANGLE,
    gX_CSS_PROP_SHADER_INDEX,
    gX_CSS_PROP_MAX
} gX_CSS_Prop;

//opencode: value types
typedef enum gX_CSS_ValueType {
    gX_CSS_VALUE_NULL = 0,
    gX_CSS_VALUE_NUMBER,   // number (grid units)
    gX_CSS_VALUE_COLOR,    // color
    gX_CSS_VALUE_KEYWORD,  // identifier (str)
} gX_CSS_ValueType;

typedef struct gX_CSS_Value {
    gX_I32 type;
    gX_F32 number;
    gX_Color color;
    gX_I8   str[gX_GUI_CSS_STR_MAX];
} gX_CSS_Value;

typedef struct gX_CSS_Decl {
    gX_I32 prop;        // gX_CSS_Prop
    gX_CSS_Value value;
    gX_I1   important;
} gX_CSS_Decl;

//opencode: pseudo-state bits — matched against element->state live bits
#define gX_GUI_CSS_PSEUDO_HOVER    1u
#define gX_GUI_CSS_PSEUDO_FOCUS    2u
#define gX_GUI_CSS_PSEUDO_ACTIVE   4u
#define gX_GUI_CSS_PSEUDO_DISABLED 8u

typedef struct gX_CSS_Selector {
    gX_I8   type[gX_GUI_CSS_STR_MAX]; // element type name or "" = any
    gX_I8   id[gX_GUI_CSS_STR_MAX];   // #id — matches element->name
    gX_I8   cls[gX_GUI_CSS_STR_MAX];  // .class — single class per compound
    gX_U32  pseudo;                   // gX_GUI_CSS_PSEUDO_* bitmask
    gX_I8   ancestorType[gX_GUI_CSS_STR_MAX]; // descendant ancestor ("" = none)
    gX_I32  specificity;              // a*100 + b*10 + c
} gX_CSS_Selector;

typedef struct gX_CSS_Rule {
    gX_CSS_Selector selector;
    gX_CSS_Decl decls[gX_GUI_CSS_MAX_DECLS_PER_RULE];
    gX_U32  declCount;
    gX_U32  order;      // source order within its origin (cascade tiebreak)
    gX_I32  origin;     // cascade origin: 0 = built-in gui.style defaults,
                        //   1 = stylesheet files, 2 = inline style
} gX_CSS_Rule;

typedef struct gX_CSS_StyleSheet {
    gX_I8    path[128];
    gX_CSS_Rule rules[gX_GUI_CSS_MAX_RULES_PER_SHEET];
    gX_U32   ruleCount;
    gX_I64   mtime;     // hot-reload: last file modification time
} gX_CSS_StyleSheet;

//opencode: registry — sheets cached by path (re-parsed on mtime change)
gX_I32 gX_GUI_CSS_LoadSheet(const gX_I8 *path);
gX_I0  gX_GUI_CSS_UnloadSheet(const gX_I8 *path);
gX_I0  gX_GUI_CSS_CheckReload(gX_I0); // rate-limited mtime watch (call per frame)

//opencode: parse CSS text into a sheet (string API + unit tests) — returns rule count
gX_I32 gX_GUI_CSS_ParseSheet(const gX_I8 *text, gX_CSS_StyleSheet *sheet);

//opencode: pure selector match (unit-testable) — matches element identity
//   (type/name/class/pseudo/ancestor) without applying anything. canvas is
//   used only for descendant selectors (NULL disables ancestor matching).
gX_I1 gX_GUI_CSS_MatchSelector(const gX_CSS_Selector *sel, const gX_GUI_Element *element, const gX_GUI_Canvas *canvas);

//opencode: apply a single declaration to element fields (unit-testable)
gX_I0 gX_GUI_CSS_ApplyDecl(gX_GUI_Element *element, gX_GUI_Canvas *canvas, const gX_CSS_Decl *decl);

//opencode: canvas resolve — runs after the layer sort in Canvas_Draw, before
//   the layout resolve (CSS sets sizes/margins the layout reads). Applies all
//   sheets attached to the canvas, then each element's inline style.
gX_I0 gX_GUI_CSS_ResolveCanvas(gX_GUI_Canvas *canvas);

//opencode: cascade comparison — true when declaration a sorts BEFORE b
//   (lower cascade key). Key: (origin, important, specificity, order).
//   Exported for unit tests; the resolver uses it for the stable candidate sort.
gX_I1 gX_GUI_CSS_KeyLess(const gX_CSS_Decl *a, const gX_CSS_Rule *ra,
                         const gX_CSS_Decl *b, const gX_CSS_Rule *rb);

//opencode: build the built-in origin-0 stylesheet from gui.style (class
//   conventions: .btn/.back/.play/.extra/.quit/.panel/.panel-overlay/
//   .title/.body/.small/.credits). Call after gui.style is initialized;
//   the resolver always applies it (lowest origin). Class-only — existing
//   templates set no classes, so nothing changes until elements opt in.
gX_I0 gX_GUI_CSS_BuildDefaultSheet(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_CSS_H
