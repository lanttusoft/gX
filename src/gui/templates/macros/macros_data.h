/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_data.h]
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


#ifndef GUI_TEMPLATES_MACROS_DATA_H
#define GUI_TEMPLATES_MACROS_DATA_H

#include "../../enum.h"

#define GUI_TEMPLATE_ELEMENT_DATA_RW_SET(agent,r,w) \
    if (dataOK) { \
        currentData->agent.read = r; \
        currentData->agent.write = w; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_PTR_REMARK(agent,p,dt) \
    if (dataOK) { \
        currentData->agent.ptr = &p; \
        currentData->agent.datatype = gX_DATATYPE_##dt; \
        strcpy(currentData->agent.remark,#p); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ADD_DATA_SPR(agent,r,w,p,dt) \
    GUI_TEMPLATE_ELEMENT_ADD_DATA \
    GUI_TEMPLATE_ELEMENT_DATA_RW_SET(agent,r,w) \
    GUI_TEMPLATE_ELEMENT_DATA_PTR_REMARK(agent,p,dt) \
    
#define GUI_TEMPLATE_ELEMENT_DATA_ATTRIBUTE(agent,ind,rb,wb,ra,wa) \
    if (dataOK) { \
        currentData->agent.attributeIndex = ind; \
        currentData->agent.attributeReadBeforeOp = rb; \
        currentData->agent.attributeWriteBeforeOp = wb; \
        currentData->agent.attributeReadAfterOp = ra; \
        currentData->agent.attributeWriteAfterOp = wa; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_TEXT(agent,ind,as) \
    if (dataOK) { \
        currentData->agent.textIndex = ind; \
        currentData->agent.textUse = true; \
        currentData->agent.textArraySize = as; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_FORMAT(f) \
    if (dataOK) { \
        strcpy(currentData->format,f); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_ONCE \
    if (dataOK) { \
        currentData->execute.once = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_HOVERED \
    if (dataOK) { \
        currentData->execute.hovered = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_TOGGLED \
    if (dataOK) { \
        currentData->execute.toggled = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_CLICKED(left,right,middle) \
    if (dataOK) { \
        currentData->execute.clickedLeft = left; \
        currentData->execute.clickedRight = right; \
        currentData->execute.clickedMiddle = middle; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_PRESSED(left,right,middle) \
    if (dataOK) { \
        currentData->execute.pressedLeft = left; \
        currentData->execute.pressedRight = right; \
        currentData->execute.pressedMiddle = middle; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_EXECUTE_DRAGGED(left,right,middle) \
    if (dataOK) { \
        currentData->execute.draggedLeft = left; \
        currentData->execute.draggedRight = right; \
        currentData->execute.draggedMiddle = middle; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_BEFORE_OP(csd,cds,swa) \
    if (dataOK) { \
        currentData->copySrcToDstBeforeOp = csd; \
        currentData->copyDstToSrcBeforeOp = cds; \
        currentData->swapSrcAndDstBeforeOp = swa; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_AFTER_OP(csd,cds,swa) \
    if (dataOK) { \
        currentData->copySrcToDstAfterOp = csd; \
        currentData->copyDstToSrcAfterOp = cds; \
        currentData->swapSrcAndDstAfterOp = swa; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_BUTTON_FUNC(att) \
    if (dataOK) { \
        currentData->src.attributeIndex = (gX_U32)att; \
        currentData->src.attributeReadBeforeOp = true; \
        currentData->copyDstToSrcAfterOp = true; \
        currentData->execute.clickedLeft = true; \
        currentData->execute.once = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_SLIDER_FUNC \
    if (dataOK) { \
        currentData->execute.pressedLeft = true; \
        currentData->execute.draggedLeft = true; \
        currentData->usePotentio = true; \
        currentData->dst.readPotentio = true; \
        currentData->potentio.min.x = currentElement->rect.x; \
        currentData->potentio.max.x = currentElement->rect.x+currentElement->rect.width; \
        currentData->potentio.min.y = 0.0f; \
        currentData->potentio.max.y = 1.0f; \
        currentData->potentio.reg.y = currentElement->rect.y+0.5f*currentElement->rect.height; \
        currentData->potentio.copyCursor = true; \
        currentData->potentio.writeAfter = true; \
        currentData->potentio.enableX = true; \
        currentData->potentio.limit = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_DATA_POTENTIO_ADDSHAPE(ind) \
    if (dataOK) { \
        currentData->potentio.useShape = true; \
        currentData->potentio.shapeIndex = (ind); \
    } \

#endif //GUI_TEMPLATES_MACROS_DATA_H
