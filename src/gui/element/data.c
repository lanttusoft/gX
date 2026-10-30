/**********************************************************************************************
*
*   [gX: src/gui/element/data.c]
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
#include <string.h>

#include "element.h"
#include "modules.h"
#include "../enum.h"
#include "../utils.h"

#include "../../wrappers/wrappers.h"
#include "../../common/enum.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"

#include "../../gxmath/gxmath.h"

#include "../../input.h"

#define GUI_ELEMENT_DATA_LOG_LEVEL gX_LOG_TRACE
//#define GUI_ELEMENT_DATA_LOG_LEVEL gX_LOG_DEBUG

inline gX_I0 gX_GUI_Element_Data_Init(gX_GUI_Element_Data *data) {
    
    memset(data->src.remark, '\0',gX_GUI_ELEMENT_REMARK_MAX);
    memset(data->dst.remark, '\0',gX_GUI_ELEMENT_REMARK_MAX);
    memset(data->format,     '\0',gX_GUI_ELEMENT_FORMAT_MAX);
    
}

#define GUI_ELEMENT_DATA_READ_AGENT(agent) \
    if (data->agent.read) { \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading "#agent); \
        if (data->agent.datatype != gX_DATATYPE_STRING) { \
            /*opencode: NULL ptr guard — unbound wires crashed on read*/ \
            if (!data->agent.ptr) { \
                gX_TraceLog_GUI(gX_LOG_WARNING, "Data: "#agent".ptr NULL on read — skipped"); \
            } else { \
                gX_PointerReceive(data->agent.ptr, (gX_I32)data->agent.datatype, &(data->agent.reg)); \
            } \
        } \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading "#agent" done"); \
    } \

static gX_I0 gX_GUI_Element_Data_ReadPtrs(gX_GUI_Element_Data *data) {
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading ptrs");
    GUI_ELEMENT_DATA_READ_AGENT(src)
    GUI_ELEMENT_DATA_READ_AGENT(dst)
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading done");
}

#define GUI_ELEMENT_DATA_AGENT_COPYOP(whenlc,whenmc) \
    if (data->copySrcToDst##whenmc##Op) {  \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Copying src to dst "#whenlc" operation"); \
        data->dst.reg = data->src.reg; \
    } \
    if (data->copyDstToSrc##whenmc##Op) { \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Copying dst to src "#whenlc" operation"); \
        data->src.reg = data->dst.reg; \
    } \
    if (data->swapSrcAndDst##whenmc##Op) { \
        gX_F32v4 temp = data->src.reg; \
        data->src.reg = data->dst.reg; \
        data->dst.reg = temp;  \
    } \

static gX_I0 gX_GUI_Element_Data_ProcessOperator(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Processing operator");
    
    GUI_ELEMENT_DATA_AGENT_COPYOP(before,Before)
    
    //opencode: operator comes from the data module, not the element.
    //Each data module carries its own operator (data->op).
    switch (data->op) {
        case gX_GUI_ELEMENT_FUNCTION_SET: {
            gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Operation [SET]");
            data->dst.reg.x = data->src.reg.x;
            data->dst.reg.y = data->src.reg.y;
            data->dst.reg.z = data->src.reg.z;
            data->dst.reg.w = data->src.reg.w;
            gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"src: [%.3f %.3f %.3f %.3f]",
                        data->src.reg.x,data->src.reg.y,data->src.reg.z,data->src.reg.w);
            gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"dst: [%.3f %.3f %.3f %.3f]",
                        data->dst.reg.x,data->dst.reg.y,data->dst.reg.z,data->dst.reg.w);
            gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Operation done");
            break;
        }
        case gX_GUI_ELEMENT_FUNCTION_ADD: { data->dst.reg = gX_F32v4_Add(data->src.reg,data->dst.reg); break; }
        case gX_GUI_ELEMENT_FUNCTION_SUB: { data->dst.reg = gX_F32v4_Sub(data->src.reg,data->dst.reg); break; }
        case gX_GUI_ELEMENT_FUNCTION_MUL: { data->dst.reg = gX_F32v4_Mul(data->src.reg,data->dst.reg); break; }
        case gX_GUI_ELEMENT_FUNCTION_DIV: { data->dst.reg = gX_F32v4_Div(data->src.reg,data->dst.reg); break; }
        //opencode: INC/DEC operate on dst.reg — the old code used src.reg as
        //   base (copy-paste from ADD/SUB), so incrementing dst actually set
        //   dst = src + 1 (and 1 when src was never read).
        case gX_GUI_ELEMENT_FUNCTION_INC: { data->dst.reg = gX_F32v4_Add(data->dst.reg,gX_F32v4_One()); break; }
        case gX_GUI_ELEMENT_FUNCTION_DEC: { data->dst.reg = gX_F32v4_Sub(data->dst.reg,gX_F32v4_One()); break; }
        default: break;
    }
    
    GUI_ELEMENT_DATA_AGENT_COPYOP(after,After)
    
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Processing operator done");
}

#define GUI_ELEMENT_DATA_READ_OP(inside,outside) \
    inside = outside; \
    
#define GUI_ELEMENT_DATA_WRITE_OP(inside,outside) \
    outside = inside; \
    
#define GUI_ELEMENT_DATA_POTENTIO_MODULEACCESS(opuc,opmc) \
static gX_I0 gX_GUI_Element_Data_Potentio_##opmc##Module(gX_GUI_Element_Data *data, gX_GUI_Element *element) { \
    if (data->potentio.useMain) { \
        if (data->potentio.radial) { \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.x,element->params.angle) \
        } \
        else { \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.x,element->rect.x) \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.y,element->rect.y) \
        } \
    } \
    if (element->attributes.amount > 0 && data->potentio.useAttribute && data->potentio.attributeIndex < element->attributes.amount) { \
        gX_GUI_Element_Attribute *attributes = element->attributes.data; \
        gX_U32 ind = data->potentio.attributeIndex; \
        GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.x,attributes[ind].value.x) \
    } \
    if (element->shapes.amount > 0 && data->potentio.useShape && data->potentio.shapeIndex < element->shapes.amount) { \
        gX_GUI_Element_Shape *shapes = element->shapes.data; \
        gX_U32 ind = data->potentio.shapeIndex; \
        if (data->potentio.radial) { \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.x,shapes[ind].scalar[0]) \
        } \
        else { \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.x,shapes[ind].v2[0].x) \
            GUI_ELEMENT_DATA_##opuc##_OP(data->potentio.reg.y,shapes[ind].v2[0].y) \
        } \
    } \

static gX_I0 gX_GUI_Element_Data_Potentio_ReadModule(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    if (data->potentio.useMain) {
        if (data->potentio.radial) {
            GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,element->params.angle)
        }
        else {
            if (data->potentio.enableX) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,element->rect.x)
            if (data->potentio.enableY) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.y,element->rect.y)
        }
    }
    if (element->attributes.amount > 0 && data->potentio.useAttribute && data->potentio.attributeIndex < element->attributes.amount) {
        gX_GUI_Element_Attribute *attributes = element->attributes.data;
        gX_U32 ind = data->potentio.attributeIndex;
        GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,attributes[ind].value.x)
    }
    if (element->shapes.amount > 0 && data->potentio.useShape && data->potentio.shapeIndex < element->shapes.amount) {
        gX_GUI_Element_Shape *shapes = element->shapes.data;
        gX_U32 ind = data->potentio.shapeIndex;
        if (data->potentio.radial) {
            GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,shapes[ind].scalar[0])
        }
        else {
            if (data->potentio.enableX) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,shapes[ind].v2[0].x)
            if (data->potentio.enableY) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.y,shapes[ind].v2[0].y)
        }
    }
}

static gX_I0 gX_GUI_Element_Data_Potentio_WriteModule(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    if (data->potentio.useMain) {
        if (data->potentio.radial) {
            GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.x,element->params.angle)
        }
        else {
            if (data->potentio.enableX) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.x,element->rect.x)
            if (data->potentio.enableY) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.y,element->rect.y)
        }
    }
    if (element->attributes.amount > 0 && data->potentio.useAttribute && data->potentio.attributeIndex < element->attributes.amount) {
        gX_GUI_Element_Attribute *attributes = element->attributes.data;
        gX_U32 ind = data->potentio.attributeIndex;
        GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.x,attributes[ind].value.x)
    }
    if (element->shapes.amount > 0 && data->potentio.useShape && data->potentio.shapeIndex < element->shapes.amount) {
        gX_GUI_Element_Shape *shapes = element->shapes.data;
        gX_U32 ind = data->potentio.shapeIndex;
        if (data->potentio.radial) {
            GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.x,shapes[ind].scalar[0])
        }
        else {
            if (data->potentio.enableX) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.x,shapes[ind].v2[0].x)
            //opencode: added missing enableY guard
            if (data->potentio.enableY) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.reg.y,shapes[ind].v2[0].y)
        }
    }
}

static gX_I0 gX_GUI_Element_Data_Potentio_Get(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    //opencode: only copy cursor when the user is actively pressing or dragging.
    //Without this check, copyCursor runs on every frame (including the init frame),
    //snapping the potentiometer to the current mouse position even when idle.
    //opencode: only copy cursor when pressing or dragging
    //(without this, the slider tracks the mouse everywhere, even when not clicked)
    if (data->potentio.copyCursor && (element->state.PRESSED_LEFT || element->state.DRAGGED_LEFT)) {
        gX_F32v2 mouseCoord = input.mouse.coord;
        if (data->potentio.enableX) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,mouseCoord.x)
        if (data->potentio.enableY) GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.y,mouseCoord.y)
        if (data->potentio.radial) {
            gX_Rectangle rect = element->rect;
            gX_F32v2 coord = (gX_F32v2){mouseCoord.y-rect.y-0.5f*rect.height,
                                        mouseCoord.x-rect.x-0.5f*rect.width};
            gX_F32 angle = gX_RAD2DEG*atan2f(coord.x,coord.y)+180.0f;
            GUI_ELEMENT_DATA_READ_OP(data->potentio.reg.x,angle)
        }
        if (data->potentio.limit) {
            //opencode: radial mode holds an ANGLE (0..360) — the old pixel-
            //   range clamp destroyed the value (pinned near 0). Clamp to
            //   [0,360] in radial mode, pixel range only in cartesian mode.
            if (data->potentio.radial) {
                data->potentio.reg.x = gX_F32_Clamp(data->potentio.reg.x, 0.0f, 360.0f);
            } else {
                data->potentio.reg.x = gX_F32_Clamp(data->potentio.reg.x,element->rect.x,element->rect.x+element->rect.width);
                data->potentio.reg.y = gX_F32_Clamp(data->potentio.reg.y,element->rect.y,element->rect.y+element->rect.height);
            }
        }
    }
    if (data->potentio.readBefore) {
        gX_GUI_Element_Data_Potentio_ReadModule(data,element);
    }
    if (data->potentio.writeBefore) {
        gX_GUI_Element_Data_Potentio_WriteModule(data,element);
    }
    if (data->src.writePotentio) { 
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing to potentio from src");
        if (data->potentio.enableX) GUI_ELEMENT_DATA_READ_OP(data->potentio.result.x,data->src.reg.x)
        if (data->potentio.enableY) GUI_ELEMENT_DATA_READ_OP(data->potentio.result.y,data->src.reg.y)
        if (data->potentio.radial)  GUI_ELEMENT_DATA_READ_OP(data->potentio.result.x,data->src.reg.x)  
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing to potentio done");
    }
    if (data->dst.writePotentio) { 
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing to potentio from dst");
        if (data->potentio.enableX) GUI_ELEMENT_DATA_READ_OP(data->potentio.result.x,data->dst.reg.x)
        if (data->potentio.enableY) GUI_ELEMENT_DATA_READ_OP(data->potentio.result.y,data->dst.reg.y)
        if (data->potentio.radial)  GUI_ELEMENT_DATA_READ_OP(data->potentio.result.x,data->dst.reg.x)  
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing to potentio done");
    }
}

static gX_I0 gX_GUI_Element_Data_Potentio_Process(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    gX_F32 rangeX = data->potentio.max.x - data->potentio.min.x;
    gX_F32 rangeY = data->potentio.max.y - data->potentio.min.y;
    gX_F32 TX = (rangeX > 0.0f) ? (data->potentio.reg.x - data->potentio.min.x) / rangeX : 0.0f;
    gX_F32 TY = (rangeY > 0.0f) ? (data->potentio.reg.y - data->potentio.min.y) / rangeY : 0.0f;
    if (data->potentio.limit) {
        TX = gX_F32_Clamp(TX,0.0f,1.0f);
        TY = gX_F32_Clamp(TY,0.0f,1.0f);
    }
    if (data->potentio.enableX) data->potentio.result.x = TX;
    if (data->potentio.enableY) data->potentio.result.y = TY;
    if (data->potentio.radial)  data->potentio.result.x = TX;
}

static gX_I0 gX_GUI_Element_Data_Potentio_Send(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    if (data->potentio.readAfter) {
        gX_GUI_Element_Data_Potentio_ReadModule(data,element);
    }
    if (data->potentio.writeAfter) {
        gX_GUI_Element_Data_Potentio_WriteModule(data,element);
    }
    if (data->src.readPotentio) {
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading from potentio to src");
        if (data->potentio.enableX) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.result.x,data->src.reg.x)
        if (data->potentio.enableY) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.result.y,data->src.reg.y)
        if (data->potentio.radial)  GUI_ELEMENT_DATA_WRITE_OP(data->potentio.result.x,data->src.reg.x)   
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading from potentio done");
    }
    if (data->dst.readPotentio) {
        //opencode: v0.2.0 — map the normalized track fraction through the
        //   declared value range (potentio.min.y..max.y) before handing it to
        //   dst. Sliders bind real-world values (FOV 60..120, volume 0..2);
        //   the old path wrote the raw 0..1 cursor fraction into the bound
        //   float, so any range wider than 0..1 was silently broken.
        gX_F32 mappedX = data->potentio.result.x;
        if (data->potentio.max.y > data->potentio.min.y)
            mappedX = data->potentio.min.y + mappedX * (data->potentio.max.y - data->potentio.min.y);
        if (data->potentio.enableX) GUI_ELEMENT_DATA_WRITE_OP(mappedX,data->dst.reg.x)
        if (data->potentio.enableY) GUI_ELEMENT_DATA_WRITE_OP(data->potentio.result.y,data->dst.reg.y)
        if (data->potentio.radial)  GUI_ELEMENT_DATA_WRITE_OP(data->potentio.result.x,data->dst.reg.x)
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Reading from potentio done");
    }
}

#define GUI_ELEMENT_DATA_ATTRIBUTE_OP(when) \
    if (element->attributes.amount > 0) { \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Attributes amount %i > 0",element->attributes.amount); \
        gX_GUI_Element_Attribute *attributes = element->attributes.data; \
        /*opencode: bounds check attribute indices */ \
        gX_U32 aiSrc = (attributeIndexSrc < element->attributes.amount) ? attributeIndexSrc : 0; \
        gX_U32 aiDst = (attributeIndexDst < element->attributes.amount) ? attributeIndexDst : 0; \
        gX_F32v4 *attributeSrc = &(attributes[aiSrc].value); \
        gX_F32v4 *attributeDst = &(attributes[aiDst].value); \
        if (data->src.attributeWrite##when##Op) { *attributeSrc = data->src.reg; } \
        if (data->dst.attributeWrite##when##Op) { *attributeDst = data->dst.reg; } \
        if (data->src.attributeRead##when##Op)  { data->src.reg = *attributeSrc; } \
        if (data->dst.attributeRead##when##Op)  { data->dst.reg = *attributeDst; } \
    } \
    
//opencode: textTarget parameterized — the dst expansion previously wrote
//   into textSrc (copy-paste bug): a dst.textUse wire overwrote the src
//   element's text instead of the dst element's.
#define GUI_ELEMENT_DATA_AGENT_TEXT_USE(agent, textTarget) \
    if (data->agent.textUse) { \
        if (data->agent.datatype != gX_DATATYPE_STRING) { \
            gX_FormatCast(textline, (gX_U32)sizeof(textline), data->format, (gX_I32)data->agent.datatype, &data->agent.reg); \
        } \
        else if (data->agent.datatype == gX_DATATYPE_STRING) { \
            /*opencode: NULL ptr guard for STRING sources*/ \
            if (!data->agent.ptr) { \
                gX_TraceLog_GUI(gX_LOG_WARNING, "Data: "#agent".ptr NULL for STRING — skipped"); \
            } else { \
                gX_FormatCast_String(textline, (gX_U32)sizeof(textline), data->format, (gX_I8*)(data->agent.ptr)); \
            } \
        } \
        /*opencode: bounded string copy to prevent overflow*/ \
        if (data->agent.textAppend) { \
            gX_U32 room = sizeof(textTarget->text) - strlen(textTarget->text) - 1; \
            strncat(textTarget->text,textline,room); \
        } else { \
            strncpy(textTarget->text,textline,sizeof(textTarget->text)); \
            textTarget->text[sizeof(textTarget->text) - 1] = 0; \
        } \
    } \

static gX_I0 gX_GUI_Element_Data_Process(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Processing");
    gX_U32 attributeIndexSrc = data->src.attributeIndex;
    gX_U32 attributeIndexDst = data->dst.attributeIndex;
    gX_U32 textIndexSrc = data->src.textIndex;
    gX_U32 textIndexDst = data->dst.textIndex;
    
    //IIa: before operator
    GUI_ELEMENT_DATA_ATTRIBUTE_OP(Before)
    if (data->usePotentio) {
        gX_GUI_Element_Data_Potentio_Get(data,element);
    }
    
    //IIb: operator (only when data module has one configured)
    if (data->useDataOperator) {
        gX_GUI_Element_Data_ProcessOperator(data,element);
    }
    if (data->usePotentio) {
        gX_GUI_Element_Data_Potentio_Process(data,element);
    }
    
    //IIc: after operator
    GUI_ELEMENT_DATA_ATTRIBUTE_OP(After)
    if (data->usePotentio) {
        gX_GUI_Element_Data_Potentio_Send(data,element);
    }
    
    if (element->texts.amount > 0) {
        gX_TextLine textline = { 0 };
        gX_GUI_Element_Text *texts = element->texts.data;
        //opencode: bounds check text indices against amount
        gX_U32 tiSrc = (textIndexSrc < element->texts.amount) ? textIndexSrc : 0;
        gX_U32 tiDst = (textIndexDst < element->texts.amount) ? textIndexDst : 0;
        gX_GUI_Element_Text *textSrc = &(texts[tiSrc]);
        gX_GUI_Element_Text *textDst = &(texts[tiDst]);
        GUI_ELEMENT_DATA_AGENT_TEXT_USE(src, textSrc)
        GUI_ELEMENT_DATA_AGENT_TEXT_USE(dst, textDst)
    }
    
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Processing done");
}

#define GUI_ELEMENT_DATA_WRITE_AGENT(agent) \
    if (data->agent.write) {  \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing "#agent); \
        if (data->agent.datatype != gX_DATATYPE_STRING) { \
            /*opencode: NULL ptr guard — unbound wires crashed on write*/ \
            if (!data->agent.ptr) { \
                gX_TraceLog_GUI(gX_LOG_WARNING, "Data: "#agent".ptr NULL on write — skipped"); \
            } else { \
                gX_PointerCast(data->agent.ptr, (gX_I32)data->agent.datatype, &(data->agent.reg)); \
            } \
        } \
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing "#agent" done"); \
    } \

static gX_I0 gX_GUI_Element_Data_WritePtrs(gX_GUI_Element_Data *data) {
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing ptrs");
    GUI_ELEMENT_DATA_WRITE_AGENT(src)
    GUI_ELEMENT_DATA_WRITE_AGENT(dst)
    gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Writing ptrs done");
}

static gX_I0 gX_GUI_Element_Data_Update(gX_GUI_Element_Data *data, gX_GUI_Element *element) {
    
    gX_I1 init = false;
    gX_I1 execute = false;
    
    if (data->init && !(data->initDone)) { init = true; }
    
    gX_GUI_Element_ExecuteState_Update(&data->execute, element, &execute);
    
    if (data->executeAnyway) { execute = true; }
    
    if (execute || init) {
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Executing");

        if (data->resolveDstPtr) data->dst.ptr = data->resolveDstPtr(data, element);
        
        if (data->isFunctionPtr) {
            if (data->funcPtr) {
                //opencode: v0.4.0 — direct callback (void return; the old
                //   gX_I32-return dispatch wrote the slot index to reg.x)
                //opencode: v0.6.x — owner-scoped context is resolved at call
                //   time: the data heap can realloc while modules are added at
                //   build time, so a context address stored in funcPtrArg at
                //   attach time could dangle.
                gX_I0 *callbackArg = data->funcPtrArg;
                if (data->usesCallbackContext) {
                    callbackArg = data->callbackReceivesOwnerData
                        ? (gX_I0*)data : (gX_I0*)data->callbackCtx.bytes;
                }
                data->funcPtr(callbackArg);
            } else {
                gX_TraceLog_GUI(gX_LOG_WARNING, "Data: isFunctionPtr set but funcPtr is NULL");
            }
        }
        else {
        
            //I: read
            gX_GUI_Element_Data_ReadPtrs(data);
            
            //II: process
            gX_GUI_Element_Data_Process(data,element);
            
            //III: write
            gX_GUI_Element_Data_WritePtrs(data);
        
        }
        
        if (init) data->initDone = true;
        
        gX_TraceLog_GUI(GUI_ELEMENT_DATA_LOG_LEVEL,"Executing done");
        data->execute.done = true;
    }
    
}

//opencode: CONVENTION — use gX_GUI_OnClick for click interactions.
//Multiple data modules on one element are broken (data[1+] never fires).
//The agent/operator/write system below still works for data[0] only.
//Use isFunctionPtr / OnClick for all new code.
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Data,data)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Data,data)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Data,data,DATA)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Data,data)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Data,data)
//opencode: inlined from LOOP macro for navigability
gX_I0 gX_GUI_Element_Update_Datas(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Update_Datas(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->datas;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Data *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Data *elementmodule = &heapPtr[i];
            gX_GUI_Element_Data_Update(elementmodule,element);
        }
    }
}
