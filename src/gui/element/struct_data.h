/**********************************************************************************************
*
*   [gX: src/gui/element/struct_data.h]
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


#ifndef GUI_ELEMENT_STRUCT_DATA_H
#define GUI_ELEMENT_STRUCT_DATA_H

//opencode: LIMITATION — only data[0] on an element reliably fires.
//data[1+] silently fails ExecuteState_Update. All click interactions
//should use gX_GUI_OnClick (binding.h) which creates a single data
//module at index 0 with isFunctionPtr callback.
//The agent/operator system below is retained for slider potentiometers.

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
    
#include "../config.h"
#include "struct_executestate.h"

typedef struct gX_GUI_Element_Data_Agent {
    //opencode: naming convention:
    //  read  = on ReadPtrs: copy ptr value INTO reg
    //  write = on WritePtrs: copy reg value TO ptr
    //  attributeIndex = index of element attribute used by this agent
    //  textIndex = index of element text module used by this agent
    gX_I1       read;
    gX_I1       write;
    
    gX_I1       writePotentio;
    gX_I1       readPotentio;
    
    //reg function ptr: src [args] | dst [return val]
    gX_F32v4    reg; 
    gX_U32      datatype;
    gX_I0      *ptr;
    
    gX_I8       remark[gX_GUI_ELEMENT_REMARK_MAX];
    
    gX_U32      attributeIndex;
    gX_I1       attributeReadBeforeOp;
    gX_I1       attributeWriteBeforeOp;
    gX_I1       attributeReadAfterOp;
    gX_I1       attributeWriteAfterOp;
    
    gX_U32      textIndex;
    gX_I1       textUse;
    gX_U32      textArraySize;
    gX_I1       textAppend;
} gX_GUI_Element_Data_Agent;

typedef struct gX_GUI_Element_Data_Potentio {
    gX_I1       smooth;
    gX_I1       radial;
    gX_I1       enableX;
    gX_I1       enableY;
    
    gX_I1       limit;
    
    gX_I1       copyCursor;
    gX_I1       readBefore;
    gX_I1       readAfter;
    gX_I1       writeBefore;
    gX_I1       writeAfter;
    gX_F32v2    reg;
    
    gX_I1       useMain;
    gX_I1       useAttribute;
    gX_I1       useShape;
    gX_U32      attributeIndex;
    gX_U32      shapeIndex;
    
    gX_F32v2    result;
    gX_F32v2    min;
    gX_F32v2    max;
} gX_GUI_Element_Data_Potentio;

//opencode: aligned owner-scoped callback storage. The union keeps the opaque
//   byte payload correctly aligned when widget callbacks cast it to context
//   structs containing pointers and floating-point values.
typedef union gX_GUI_Element_CallbackContext {
    gX_U64 alignment;
    gX_U8 bytes[gX_GUI_ELEMENT_CALLBACK_CTX_MAX];
} gX_GUI_Element_CallbackContext;

struct gX_GUI_Element;
struct gX_GUI_Element_Data;
typedef gX_I0 *(*gX_GUI_Element_Data_PointerResolver)(
    struct gX_GUI_Element_Data *data, struct gX_GUI_Element *element);

typedef struct gX_GUI_Element_Data {
    gX_I1   isFunctionPtr;
    gX_I0   *funcPtrArg;
    //opencode: v0.4.0 — B6: direct click callback (was gX_I32(*)() legacy
    //   shader-wire signature; clicks were its only caller). The binding
    //   stores the callback here with its arg — no slot-pool indirection.
    gX_I0   (*funcPtr)(gX_I0 *userData);

    //opencode: v0.6.x — owner-scoped callback context (GUI audit finding 4).
    //   When usesCallbackContext is set, the callback receives owner-scoped
    //   bytes instead of funcPtrArg. Markup callbacks may receive the live data
    //   module to read its script path from format alongside the handler bytes.
    gX_I1   usesCallbackContext;
    gX_I1   callbackReceivesOwnerData;
    gX_GUI_Element_CallbackContext callbackCtx;
    gX_GUI_Element_Data_PointerResolver resolveDstPtr;
    
    gX_GUI_Element_Data_Agent src;
    gX_GUI_Element_Data_Agent dst;
    
    gX_I8       format[gX_GUI_ELEMENT_FORMAT_MAX];
    
    gX_I1 init;
    gX_I1 initDone;
    
    gX_I1 useDataOperator;
    gX_I32 op;
    
    gX_GUI_Element_ExecuteState execute;
    gX_I1 executeAnyway;
    gX_I1 executeOneTime;
    
    gX_I1       usePotentio;
    gX_GUI_Element_Data_Potentio potentio;
    
    gX_I1       copySrcToDstBeforeOp;
    gX_I1       copyDstToSrcBeforeOp;
    gX_I1       swapSrcAndDstBeforeOp;
    gX_I1       copySrcToDstAfterOp;
    gX_I1       copyDstToSrcAfterOp;
    gX_I1       swapSrcAndDstAfterOp;
    //opencode: wire label strings for debug introspection
    gX_I8       wireSource[64];  // e.g. "attribute(0)" or "scene.title.index"
    gX_I8       wireTarget[64];  // e.g. "gui.transition.execute"
} gX_GUI_Element_Data;
    
#ifdef __cplusplus
}
#endif

#endif //GUI_ELEMENT_STRUCT_DATA_H
