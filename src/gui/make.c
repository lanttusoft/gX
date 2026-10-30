/**********************************************************************************************
*
*   [gX: src/gui/make.c]
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


#include "gui.h"
#include "canvas.h"
#include "schema.h"

#include <string.h>
#include "../common/log.h"
#include "../common/marshal/json.h"

#include "../wrappers/wrappers.h"
#include "../wrappers/text.h"
#include "../common/text.h"

#include "templates/template_registry.h"

#include "element/struct.h"

//opencode: dump canvas elements to stdout — rect, text, texture info
gX_I0 gX_GUI_Diagnostic_Canvas(gX_I32 canvasIndex) {
    gX_GUI_Canvas *canvas = gX_GUI_Canvas_FromIndex(canvasIndex);
    if (!canvas) {
        gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG] canvas[%d] NULL", canvasIndex);
        return;
    }
    gX_GUI_Element *elems = canvas->elements.data;
    if (!elems) {
        gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG] canvas[%d] \"%s\" no elements", canvasIndex, canvas->name);
        return;
    }
    gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG] canvas[%d] \"%s\" elements=%u template=%d",
        canvasIndex, canvas->name, canvas->elements.amount, canvas->templateID);
    for (gX_U32 i = 0; i < canvas->elements.amount; i++) {
        gX_GUI_Element *e = &elems[i];
        gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG]   [%u] rect=(%.0f,%.0f,%.0f,%.0f) color=(%d,%d,%d,%d) hover=%d click=%d flags=%s%s%s%s%s",
            i,
            e->rect.x, e->rect.y, e->rect.width, e->rect.height,
            e->color.r, e->color.g, e->color.b, e->color.a,
            (gX_I32)e->state.HOVERED, (gX_I32)e->state.CLICKED_LEFT,
            e->state.DISABLED ? " D" : "",
            e->state.HOVERABLE ? " H" : "",
            e->state.CLICKABLE_LEFT ? " CL" : "",
            e->state.DRAGGABLE_LEFT ? " DR" : "",
            e->stateRendering.RECT_NO_DRAW ? " ND" : "");
        //text
        if (e->texts.amount > 0 && e->texts.data) {
            gX_GUI_Element_Text *t = (gX_GUI_Element_Text*)e->texts.data;
            for (gX_U32 ti = 0; ti < e->texts.amount; ti++) {
                gX_GUI_Element_Text *tt = &t[ti];
                if (tt->text[0]) {
                    gX_Font df;
                    df = gX_Font_GetDefault();
                    gX_F32v2 ml;
                    ml = gX_RawString_Measure(tt->text, df, tt->fontSize, (gX_F32)tt->spacing);
                    gX_I1 overflow = (ml.x > e->rect.width);
                    gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG]     text[%u]=\"%s\" fontSize=%.0f measure=(%.0f,%.0f) rectW=%.0f%s",
                        ti, tt->text, (gX_F64)tt->fontSize, (gX_F64)ml.x, (gX_F64)ml.y, (gX_F64)e->rect.width,
                        overflow ? " OVERFLOW" : "");
                }
            }
        }
        //texture
        if (e->textures.amount > 0 && e->textures.data) {
            gX_GUI_Element_Texture *tx = (gX_GUI_Element_Texture*)e->textures.data;
            for (gX_U32 xi = 0; xi < e->textures.amount; xi++) {
                gX_GUI_Element_Texture *ttx = &tx[xi];
                if (ttx->texture) {
                    gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG]     texture[%u] id=%u size=(%d,%d)",
                        xi, ttx->texture->id, ttx->texture->width, ttx->texture->height);
                }
            }
        }
        //shader
        if (e->shaders.amount > 0 && e->shaders.data) {
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG]     shaders=%u", e->shaders.amount);
        }
        //model
        if (e->models.amount > 0 && e->models.data) {
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_DIAG]     models=%u", e->models.amount);
        }
    }
}

gX_I0 gX_GUI_AddMake(gX_I32 makeindex, gX_I32 canvasIndex) {
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"GUI_AddMake","Adding a make statement")
    //opencode: bounds check — configurable via gX_GUI_MAKE_QUEUE_MAX
    if (gui.makeAmount >= gX_GUI_MAKE_QUEUE_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "GUI_AddMake: make queue full (max %i)", gX_GUI_MAKE_QUEUE_MAX);
        return;
    }
    gui.makeToken[gui.makeAmount] = makeindex;
    gui.canvasToken[gui.makeAmount] = canvasIndex;
    gui.makeAmount++;
    gui.make = true;
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"Adding make statement done")
}

gX_I32 gX_GUI_Make(gX_I32 makeIndex, gX_I32 canvasIndex) {
    gX_I32 errors = 0;
    gX_TRACELOG_PROLOGUE_LEVEL(gX_GUI_TRACELOG_LEVEL,"GUI_Make","Making a canvas")
    //opencode: registry dispatch replaces the old switch + extern list
    errors = gX_GUI_Template_Dispatch(makeIndex, canvasIndex);
    //opencode: diagnostic dump after template build
    if (gui.state.DIAGNOSTIC_TEMPLATE) {
        gX_GUI_Diagnostic_Canvas(canvasIndex);
    }
    //opencode: audit element map for scripted interaction gates.
    gX_GUI_Trace_Canvas(canvasIndex);
    gX_TRACELOG_EPILOGUE_LEVEL(gX_GUI_TRACELOG_LEVEL,"Making done")
    return errors;
}

gX_I32 gX_GUI_Make_LoadFile(const gX_I8 *path, gX_I32 canvasIndex) {

    gX_GUI_SchemaJSON_Create(canvasIndex);

    json.schema = &schemaJSON_GUI;
    gX_JSON_Load(path);

    //opencode: validate schema version after load (warns on mismatch)
    gX_GUI_Schema_CheckVersion();

    gX_GUI_SchemaJSON_Delete();

    return 0;
}

gX_I32 gX_GUI_Make_SaveFile(const gX_I8 *path, gX_I32 canvasIndex) {

    gX_GUI_SchemaJSON_Create(canvasIndex);

    json.schema = &schemaJSON_GUI;
    gX_JSON_Save(path);

    gX_GUI_SchemaJSON_Delete();

    return 0;
}
