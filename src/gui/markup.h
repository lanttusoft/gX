/**********************************************************************************************
*
*   [gX: src/gui/markup.h]
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


#ifndef GUI_MARKUP_H
#define GUI_MARKUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/heap.h"

#include "config.h"
#include "css.h"

//opencode: dynamic template id for registered markup files (not in the enum)
#define GX_GUI_TEMPLATE_MARKUP 1000

//opencode: markup loader (web box-model Phase 3) — a schema-driven element
//   tree in any of the 4 marshal formats (JSON/YAML/TOML/XML) that builds
//   into the same element arrays templates produce. One schema, all formats,
//   via gX_Marshal_Load. Children become nested layout containers (display
//   field controls FLEX_ROW/FLEX_COL/BLOCK).

typedef struct gX_Markup gX_Markup;

typedef struct gX_MarkupElement {
    gX_I8   type[32];        // rect / text / button / panel / texture / shader / container
    gX_I8   name[128];       // #id (element name)
    gX_I8   cssClass[64];    // class (space-separated)
    gX_I8   cssStyle[256];   // inline style
    gX_F32  gx, gy, gw, gh;  // grid rect (position/size)
    gX_Color color;          // background (optional; default = builder default)
    gX_I8   text[256];       // text content
    gX_F32  fontSize;        // text font size (0 = default)
    gX_I8   texture[128];    // texture file path (texture type)
    gX_I8   shader[128];     // shader frag file (shader type / shader-index)
    gX_I8   display[16];     // container display: block/flex-row/flex-col
    gX_U32  layer;
    gX_I1   visible;
    gX_I1   centerX, centerY; // text centering
    gX_I8   onclick[64];      // script handler name to fire on click (companion .as)
    gX_Heap children;         // nested markup elements → layout containers
} gX_MarkupElement;

typedef struct gX_Markup {
    gX_I8   name[128];
    gX_I8   description[512];
    gX_I8   css[128];        // stylesheet path (loaded via CSS registry)
    gX_I8   script[128];     // companion AngelScript file (onclick handlers)
    gX_I32  templateID;
    gX_Heap elements;         // root markup elements
} gX_Markup;

//opencode: load a markup file (format by extension) into a parsed tree.
//   Returns 0 on success, non-zero on failure. Caller owns gX_Markup_Clear.
gX_I32 gX_GUI_Markup_Load(gX_Markup *markup, const gX_I8 *path);

//opencode: v0.4.0 — save a markup tree back to a file (format by extension).
//   Schema-driven via gX_Marshal_Save; the GUI editor round-trips canvases.
gX_I32 gX_GUI_Markup_Save(gX_Markup *markup, const gX_I8 *path);

//opencode: free the parsed tree (deinit element heaps).
gX_I0 gX_GUI_Markup_Clear(gX_Markup *markup);

//opencode: build a markup tree into a canvas via the element builder.
//   Children become nested layout containers (display controls the mode).
//   Attaches the markup's stylesheet (css path) to the canvas.
gX_I0 gX_GUI_Markup_Build(gX_Markup *markup, gX_GUI_Canvas *canvas);

//opencode: register a template that loads + builds a markup file. The template
//   function (for scene/template dispatch) reloads the file each build.
gX_I0 gX_GUI_Template_RegisterMarkup(const gX_I8 *path);

//opencode: rate-limited mtime watch — triggers a canvas refresh when the
//   registered markup file changes (live editing). Call per frame.
gX_I0 gX_GUI_Markup_CheckReload(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_MARKUP_H
