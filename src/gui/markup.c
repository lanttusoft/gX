/**********************************************************************************************
*
*   [gX: src/gui/markup.c]
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
#include <stdio.h>
#include <sys/stat.h>

#include "markup.h"
#include "builder.h"
#include "canvas.h"
#include "binding.h"
#include "element/element.h"
#include "templates/template_registry.h"

#include "gui.h" // gui canvases + refresh (CheckReload)
#include "../common/marshal/marshal.h"

#include "../common/log.h"
#include "../common/marshal/marshal.h"
#include "../main.h"
#include "../module/module.h"

//opencode: markup loader — schema-driven element tree → builder calls.

#define GX_MARKUP_ELEMENT_MAX 128

//=============================================================================
// schema — element fields (shared by root + nested children heaps)
//=============================================================================

#define GX_MARKUP_ELEMENT_FIELD_COUNT 20

static gX_U32 gX_MarkupElement_Children_Count(gX_I0 *base) {
    gX_MarkupElement *e = (gX_MarkupElement*)base;
    return e->children.amount;
}
static gX_I0 *gX_MarkupElement_Children_At(gX_I0 *base, gX_U32 index) {
    gX_MarkupElement *e = (gX_MarkupElement*)base;
    if (index >= e->children.amount) return NULL;
    if (!e->children.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&e->children, index);
}
static gX_I32 gX_MarkupElement_Children_Add(gX_I0 *base) {
    gX_MarkupElement *e = (gX_MarkupElement*)base;
    if (!e->children.state.INITIALIZED) {
        gX_Heap_Zero(&e->children);
        gX_Heap_SetBlockSize(&e->children, sizeof(gX_MarkupElement));
        gX_Heap_SetMax(&e->children, GX_MARKUP_ELEMENT_MAX);
        gX_Heap_SetAmount(&e->children, 0);
        gX_Heap_Init(&e->children);
    }
    gX_MarkupElement *c = gX_Heap_Add_FirstEmpty_NoPtr(&e->children);
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    c->visible = true;
    gX_U32 idx = (gX_U32)(c - (gX_MarkupElement*)e->children.data);
    return (gX_I32)idx;
}

static gX_U32 gX_Markup_Elements_Count(gX_I0 *base) {
    gX_Markup *m = (gX_Markup*)base;
    return m->elements.amount;
}
static gX_I0 *gX_Markup_Elements_At(gX_I0 *base, gX_U32 index) {
    gX_Markup *m = (gX_Markup*)base;
    if (index >= m->elements.amount) return NULL;
    if (!m->elements.meta[index].USED) return NULL;
    return gX_Heap_SeekPtr_Index(&m->elements, index);
}
static gX_I32 gX_Markup_Elements_Add(gX_I0 *base) {
    gX_Markup *m = (gX_Markup*)base;
    if (!m->elements.state.INITIALIZED) {
        gX_Heap_Zero(&m->elements);
        gX_Heap_SetBlockSize(&m->elements, sizeof(gX_MarkupElement));
        gX_Heap_SetMax(&m->elements, GX_MARKUP_ELEMENT_MAX);
        gX_Heap_SetAmount(&m->elements, 0);
        gX_Heap_Init(&m->elements);
    }
    gX_MarkupElement *e = gX_Heap_Add_FirstEmpty_NoPtr(&m->elements);
    if (!e) return -1;
    memset(e, 0, sizeof(*e));
    e->visible = true;
    gX_U32 idx = (gX_U32)(e - (gX_MarkupElement*)m->elements.data);
    return (gX_I32)idx;
}

//opencode: markup element type is required to select its renderer/widget path.
static gX_SchemaField gX_markupElementFields[GX_MARKUP_ELEMENT_FIELD_COUNT] = {
    GX_SCHEMA_REQUIRED_STRING("type", gX_MarkupElement, type),
    GX_SCHEMA_STRING("name",    gX_MarkupElement, name),
    GX_SCHEMA_STRING("class",   gX_MarkupElement, cssClass),
    GX_SCHEMA_STRING("style",   gX_MarkupElement, cssStyle),
    GX_SCHEMA_F32("gx",        gX_MarkupElement, gx),
    GX_SCHEMA_F32("gy",        gX_MarkupElement, gy),
    GX_SCHEMA_F32("gw",        gX_MarkupElement, gw),
    GX_SCHEMA_F32("gh",        gX_MarkupElement, gh),
    GX_SCHEMA_COLOR("color",   gX_MarkupElement, color),
    GX_SCHEMA_STRING("text",   gX_MarkupElement, text),
    GX_SCHEMA_F32("fontSize",  gX_MarkupElement, fontSize),
    GX_SCHEMA_STRING("texture",gX_MarkupElement, texture),
    GX_SCHEMA_STRING("shader", gX_MarkupElement, shader),
    GX_SCHEMA_STRING("display",gX_MarkupElement, display),
    GX_SCHEMA_U32("layer",     gX_MarkupElement, layer),
    GX_SCHEMA_BOOL("visible",  gX_MarkupElement, visible),
    GX_SCHEMA_BOOL("centerX",  gX_MarkupElement, centerX),
    GX_SCHEMA_BOOL("centerY",  gX_MarkupElement, centerY),
    GX_SCHEMA_STRING("onclick",gX_MarkupElement, onclick),
    GX_SCHEMA_HEAP("children", gX_MarkupElement, children,
                   gX_MarkupElement_Children_Count, gX_MarkupElement_Children_At,
                   gX_MarkupElement_Children_Add,
                   gX_markupElementFields,
                   GX_MARKUP_ELEMENT_FIELD_COUNT),
};

static gX_SchemaField gX_markupFields[] = {
    GX_SCHEMA_STRING("name",     gX_Markup, name),
    GX_SCHEMA_STRING("description", gX_Markup, description),
    GX_SCHEMA_STRING("css",      gX_Markup, css),
    GX_SCHEMA_STRING("script",   gX_Markup, script),
    GX_SCHEMA_I32("templateID", gX_Markup, templateID),
    GX_SCHEMA_HEAP("elements",   gX_Markup, elements,
                   gX_Markup_Elements_Count, gX_Markup_Elements_At,
                   gX_Markup_Elements_Add,
                   gX_markupElementFields,
                   GX_MARKUP_ELEMENT_FIELD_COUNT),
};

//=============================================================================
// lifecycle
//=============================================================================

gX_I32 gX_GUI_Markup_Load(gX_Markup *markup, const gX_I8 *path) {
    if (!markup || !path) return 1;
    memset(markup, 0, sizeof(*markup));
    gX_I32 err = gX_Marshal_Load(gX_markupFields,
        (gX_U32)(sizeof(gX_markupFields) / sizeof(gX_markupFields[0])),
        markup, path);
    if (err != 0) {
        gX_TraceLog(gX_LOG_ERROR, "MARKUP: failed to load '%s'", path);
        gX_GUI_Markup_Clear(markup);
        return err;
    }
    gX_TraceLog(gX_LOG_INFO, "MARKUP: loaded '%s' (%d root elements)", path,
        (gX_I32)markup->elements.amount);
    return 0;
}

static gX_I0 gX_MarkupElement_ClearChildren(gX_MarkupElement *element) {
    if (!element || !element->children.state.INITIALIZED) return;
    gX_MarkupElement *children = element->children.data;
    if (children) {
        for (gX_U32 i = 0; i < element->children.amount; i++)
            gX_MarkupElement_ClearChildren(&children[i]);
    }
    gX_Heap_DeInit(&element->children);
}

gX_I0 gX_GUI_Markup_Clear(gX_Markup *markup) {
    if (!markup) return;
    if (markup->elements.state.INITIALIZED) {
        gX_MarkupElement *elements = markup->elements.data;
        if (elements) {
            for (gX_U32 i = 0; i < markup->elements.amount; i++)
                gX_MarkupElement_ClearChildren(&elements[i]);
        }
        gX_Heap_DeInit(&markup->elements);
    }
    memset(markup, 0, sizeof(*markup));
}

//opencode: v0.4.0 — markup save: schema-driven write (same field tables as
//   Load, via gX_Marshal_Save's format-by-extension dispatch). The GUI editor
//   round-trips a live canvas through this: snapshot canvas → markup tree →
//   save, then hot-reload picks it up.
gX_I32 gX_GUI_Markup_Save(gX_Markup *markup, const gX_I8 *path) {
    if (!markup || !path) return 1;
    gX_I32 err = gX_Marshal_Save(gX_markupFields,
        (gX_U32)(sizeof(gX_markupFields) / sizeof(gX_markupFields[0])),
        markup, path);
    if (err != 0) {
        gX_TraceLog(gX_LOG_ERROR, "MARKUP: failed to save '%s'", path);
        return err;
    }
    gX_TraceLog(gX_LOG_INFO, "MARKUP: saved '%s' (%d root elements)", path,
        (gX_I32)markup->elements.amount);
    return 0;
}

//=============================================================================
// onclick → script binding context stored with the owning element
//=============================================================================

//opencode: invoked by gX_GUI_OnClick when a markup element with an onclick
//   attribute is clicked — runs the named handler in the markup's script.
static gX_I0 gX_Markup_OnClick_Script(gX_I0 *userData) {
    gX_GUI_Element_Data *data = (gX_GUI_Element_Data*)userData;
    gX_I8 *funcName = data
        ? (gX_I8*)data->callbackCtx.bytes : NULL;
    if (!data || !data->format[0] || !funcName || !funcName[0]) return;
    //opencode: module-provided handler (gx_shell runs the script function);
    //   the host logs the module hint when none is loaded.
    gX_Module_MarkupClick(data->format, funcName);
}

static gX_I0 gX_Markup_BindOnClick(gX_GUI_Element *element, const gX_I8 *scriptPath, const gX_I8 *funcName) {
    if (!element || !scriptPath || !funcName) return;
    gX_I8 handler[64] = {0};
    //opencode: accept both "handler" and "handler()" forms
    snprintf(handler, sizeof(handler), "%s", funcName);
    gX_I8 *paren = strchr(handler, '(');
    if (paren) *paren = '\0';
    gX_GUI_Element_MakeClickable(element);
    if (!gX_GUI_OnClick_Context(element, gX_Markup_OnClick_Script,
        handler, sizeof(handler))) {
        gX_TraceLog_GUI(gX_LOG_WARNING,
            "MARKUP: onclick '%s' could not store owner context", funcName);
        return;
    }
    for (gX_U32 i = 0; i < element->datas.amount; i++) {
        gX_GUI_Element_Data *data = gX_GUI_Element_Data_Get(element, i);
        if (!data || data->funcPtr != gX_Markup_OnClick_Script) continue;
        snprintf(data->format, sizeof(data->format), "%s", scriptPath);
        data->callbackReceivesOwnerData = true;
        return;
    }
    gX_TraceLog_GUI(gX_LOG_WARNING,
        "MARKUP: onclick '%s' could not resolve owner data", funcName);
}

//=============================================================================
// build — markup tree → element builder
//=============================================================================

//opencode: case-insensitive type match — editor snapshots write the enum
//   strings (BUTTON/PANEL/LABEL), hand-authored markup uses lowercase; both
//   must select the same widget path.
static gX_I1 gX_Markup_TypeIs(const gX_MarkupElement *e, const gX_I8 *t) {
    if (!e) return false;
    if (e->type[0] == '\0') return true;
    for (gX_U32 i = 0; t[i]; i++) {
        gX_I8 c = e->type[i];
        if (c >= 'A' && c <= 'Z') c = (gX_I8)(c + ('a' - 'A'));
        if (c != t[i]) return false;
    }
    return e->type[strlen(t)] == '\0';
}

static gX_I8 gX_markupBuildScriptPath[128] = {0};

static gX_I0 gX_GUI_Markup_BuildElement(gX_MarkupElement *e,
    gX_GUI_Builder *b, gX_I1 parentVisible) {
    if (!e) return;
    gX_I1 visible = parentVisible && e->visible;
    gX_I1 isContainer = (e->children.amount > 0);
    
    if (isContainer) {
        //opencode: containers are layout elements — Layout_Begin creates the
        //   element WITH the layout module (no separate element first)
        gX_GUI_Builder_Layout_Begin(b, e->gx, e->gy, e->gw, e->gh);
        gX_GUI_Element *ce = b->currentElement;
        if (!ce) return;
        ce->type = gX_GUI_ELEMENT_TYPE_CONTAINER;
        ce->visible = visible;
        if (e->name[0])     gX_GUI_Builder_Name(b, e->name);
        if (e->cssClass[0]) gX_GUI_Builder_Class(b, e->cssClass);
        if (e->cssStyle[0]) gX_GUI_Builder_Style(b, e->cssStyle);
        if (e->layer != 0)  ce->layer = (gX_U8)e->layer;
        if (e->color.r != 0 || e->color.g != 0 || e->color.b != 0 || e->color.a != 0) {
            ce->color = e->color;
        }
        if (e->display[0]) {
            if (strcmp(e->display, "row") == 0 || strcmp(e->display, "flex-row") == 0)
                gX_GUI_Builder_Layout_Display(b, gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_ROW);
            else if (strcmp(e->display, "flex-col") == 0 || strcmp(e->display, "flex-column") == 0)
                gX_GUI_Builder_Layout_Display(b, gX_GUI_ELEMENT_LAYOUT_DISPLAY_FLEX_COL);
            else
                gX_GUI_Builder_Layout_Display(b, gX_GUI_ELEMENT_LAYOUT_DISPLAY_BLOCK);
        }
        gX_MarkupElement *kids = e->children.data;
        for (gX_U32 k = 0; k < e->children.amount; k++) {
            if (!e->children.meta[k].USED) continue;
            gX_GUI_Markup_BuildElement(&kids[k], b, visible);
        }
        gX_GUI_Builder_Layout_End(b);
        return;
    }
    
    gX_GUI_Builder_Element_Begin(b);
    if (!b->currentElement) return;
    
    gX_GUI_Builder_RectGrid(b, e->gx, e->gy, e->gw, e->gh);
    if (e->name[0])        gX_GUI_Builder_Name(b, e->name);
    if (e->cssClass[0])    gX_GUI_Builder_Class(b, e->cssClass);
    if (e->cssStyle[0])    gX_GUI_Builder_Style(b, e->cssStyle);
    if (e->layer != 0)     b->currentElement->layer = (gX_U8)e->layer;
    b->currentElement->visible = visible;
    //opencode: default background — only when the markup explicitly set one
    if (e->color.r != 0 || e->color.g != 0 || e->color.b != 0 || e->color.a != 0) {
        b->currentElement->color = e->color;
    }
    
    if (gX_Markup_TypeIs(e, "button")) {
        b->currentElement->type = gX_GUI_ELEMENT_TYPE_BUTTON;
        gX_GUI_Element_MakeClickable(b->currentElement);
    } else if (gX_Markup_TypeIs(e, "panel")) {
        b->currentElement->type = gX_GUI_ELEMENT_TYPE_PANEL;
    } else if (gX_Markup_TypeIs(e, "label") || gX_Markup_TypeIs(e, "text")) {
        b->currentElement->type = gX_GUI_ELEMENT_TYPE_LABEL;
    } else {
        //opencode: v0.4.0 — explicit RECT (was UNKNOWN): the tools editor
        //   relies on a truthful type for the element tree + snapshot round-trip
        b->currentElement->type = gX_GUI_ELEMENT_TYPE_RECT;
    }
    
    //opencode: shader module (before texture so shader-index wiring works)
    if (e->shader[0]) {
        gX_GUI_Builder_Shader_Begin(b);
            gX_GUI_Builder_Shader_Frag(b, e->shader);
            gX_GUI_Builder_Shader_Draw(b, false);
        gX_GUI_Builder_Shader_End(b);
    }
    
    //opencode: text content
    if (e->text[0]) {
        gX_GUI_Builder_Text_Begin(b);
            gX_GUI_Builder_Text_Set(b, e->text);
            if (e->fontSize > 0.0f) gX_GUI_Builder_Text_FontSize(b, e->fontSize);
            if (e->centerX) b->currentElement->stateRendering.TEXT_CENTER_X = true;
            if (e->centerY) b->currentElement->stateRendering.TEXT_CENTER_Y = true;
        gX_GUI_Builder_Text_End(b);
    }
    
    //opencode: texture
    if (e->texture[0]) {
        gX_GUI_Builder_Texture_Begin(b);
            gX_GUI_Builder_Texture_File(b, e->texture);
            if (e->shader[0]) gX_GUI_Builder_Texture_Shader(b, 0);
        gX_GUI_Builder_Texture_End(b);
    }
    
    //opencode: onclick handler — bind to the markup's companion script
    if (e->onclick[0]) {
        gX_Markup_BindOnClick(b->currentElement, gX_markupBuildScriptPath, e->onclick);
    }
    
    gX_GUI_Builder_Element_End(b);
}

gX_I0 gX_GUI_Markup_Build(gX_Markup *markup, gX_GUI_Canvas *canvas) {
    if (!markup || !canvas) return;
    gX_GUI_Builder b = gX_GUI_Builder_New(canvas,
        markup->name[0] ? markup->name : "Markup",
        markup->description, markup->templateID);
    
    //opencode: reset script path before the tree walks build; callback payloads
    //   are copied into each owning element's data module.
    memset(gX_markupBuildScriptPath, 0, sizeof(gX_markupBuildScriptPath));
    if (markup->script[0]) {
        snprintf(gX_markupBuildScriptPath, sizeof(gX_markupBuildScriptPath), "%s", markup->script);
        //opencode: lazy-load the companion script (markup dispatch runs after
        //   modules load — the module dedupes against its script list).
        gX_Module_MarkupEnsure(markup->script);
    }
    
    //opencode: attach the markup's stylesheet (registry caches + hot-reloads)
    if (markup->css[0]) {
        gX_GUI_Builder_CSS(&b, markup->css);
    }
    
    gX_MarkupElement *els = markup->elements.data;
    for (gX_U32 i = 0; i < markup->elements.amount; i++) {
        if (!markup->elements.meta[i].USED) continue;
        gX_GUI_Markup_BuildElement(&els[i], &b, true);
    }
    gX_GUI_Builder_Free(&b);
}

//=============================================================================
// template registration — a markup file as a scene template
//=============================================================================

static gX_I8 gX_markupTemplatePath[256] = {0};
static gX_I64 gX_markupTemplateMtime = 0;
static gX_U32 gX_markupReloadCounter = 0;

//opencode: rate-limited mtime watch (shader/css pattern). On change, flags the
//   markup canvases for refresh so the template rebuilds with the new file.
gX_I0 gX_GUI_Markup_CheckReload(gX_I0) {
    if (!gX_markupTemplatePath[0]) return;
    gX_markupReloadCounter++;
    if (gX_markupReloadCounter % 30 != 0) return;
    struct stat st;
    if (stat(gX_markupTemplatePath, &st) != 0) return;
    if (gX_markupTemplateMtime == 0) { gX_markupTemplateMtime = (gX_I64)st.st_mtime; return; }
    if ((gX_I64)st.st_mtime == gX_markupTemplateMtime) return;
    gX_markupTemplateMtime = (gX_I64)st.st_mtime;
    gX_TraceLog(gX_LOG_INFO, "MARKUP: file changed — refreshing markup canvases");
    extern gX_GUI gui;
    if (gui.canvases.data) {
        gX_GUI_Canvas *canvases = gui.canvases.data;
        for (gX_U32 i = 0; i < gui.canvases.amount; i++) {
            if (canvases[i].templateID == GX_GUI_TEMPLATE_MARKUP) {
                canvases[i].refresh = true;
            }
        }
        gui.refresh = true;
    }
}

static gX_I0 gX_GUI_Template_MarkupDispatch(gX_I32 canvasIndex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasIndex);
    if (!canvas || !gX_markupTemplatePath[0]) return;
    gX_Markup markup;
    if (gX_GUI_Markup_Load(&markup, gX_markupTemplatePath) != 0) return;
    gX_GUI_Markup_Build(&markup, canvas);
    gX_GUI_Markup_Clear(&markup);
}

gX_I0 gX_GUI_Template_RegisterMarkup(const gX_I8 *path) {
    if (!path || !path[0]) return;
    strncpy(gX_markupTemplatePath, path, sizeof(gX_markupTemplatePath) - 1);
    gX_GUI_Template_Register(GX_GUI_TEMPLATE_MARKUP, "MARKUP", gX_GUI_Template_MarkupDispatch);
    gX_TraceLog(gX_LOG_INFO, "MARKUP: registered markup template from '%s'", path);
}
