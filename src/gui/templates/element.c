/**********************************************************************************************
*
*   [gX: src/gui/templates/element.c]
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


#include "template_header.h"

gX_I0 gX_GUI_Element_Template_Button(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_Button(gX_GUI_Element *element) {
    //opencode: operator is set on the data module (data->op), not on the element.
    //stateFunction.OPERATOR was removed — use gX_GUI_Element_Add_Data + data->op instead.
    //opencode: classify template buttons so rendering can style them without
    //   affecting ordinary text and panel elements.
    element->type = gX_GUI_ELEMENT_TYPE_BUTTON;
    element->state.HOVERABLE = true;
    element->state.CLICKABLE_LEFT = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.FOCUSABLE = true;
}

gX_I0 gX_GUI_Element_Template_Slider(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_Slider(gX_GUI_Element *element) {
    element->state.HOVERABLE = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.DRAGGABLE_LEFT = true;
    element->state.FOCUSABLE = true;
}

gX_I0 gX_GUI_Element_Template_CheckBox(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_CheckBox(gX_GUI_Element *element) {
    element->state.HOVERABLE = true;
    element->state.CLICKABLE_LEFT = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.TOGGLEABLE = true;
    element->state.FOCUSABLE = true;
}

gX_I0 gX_GUI_Element_Template_DropDown(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_DropDown(gX_GUI_Element *element) {
    element->state.HOVERABLE = true;
    element->state.CLICKABLE_LEFT = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.FOCUSABLE = true;
}

gX_I0 gX_GUI_Element_Template_Radio(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_Radio(gX_GUI_Element *element) {
    element->state.HOVERABLE = true;
    element->state.CLICKABLE_LEFT = true;
    element->state.PRESSABLE_LEFT = true;
    element->state.FOCUSABLE = true;
}

#define STANDARDBUTTON_FONTSIZE 42
#define STANDARDBUTTON_COLOR (gX_Color){127,127,127,191}
#define STANDARDBUTTON_COLOR_HOVERED (gX_Color){127,255,127,191}

gX_I0 gX_GUI_Element_Template_Button_Standard(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Template_Button_Standard(gX_GUI_Element *element) {
    
    GUI_TEMPLATE_MODULEINDICES
    GUI_TEMPLATE_MODULEOK
    GUI_TEMPLATE_MODULEPOINTERS
    GUI_TEMPLATE_RESOURCEPOINTERS
    GUI_TEMPLATE_RESOURCEPOINTERS_GET
    gX_GUI_Element *currentElement = element;
    
    currentElement->color = STANDARDBUTTON_COLOR;
    
    element->stateRendering.TEXT_CENTER_X = true;
    element->stateRendering.TEXT_CENTER_Y = true;
    GUI_TEMPLATE_ELEMENT_ADD_TEXT_STRING("button")
    GUI_TEMPLATE_ELEMENT_TEXT_FS_OF_SP(STANDARDBUTTON_FONTSIZE,0,0,5)
    GUI_TEMPLATE_ELEMENT_TEXT_SET_FONT(0)
    gX_GUI_Element_Template_Button(element);
    
    GUI_TEMPLATE_ELEMENT_ADD_COLOR
    if (colorOK) {
        currentColor->color = STANDARDBUTTON_COLOR_HOVERED;
        currentColor->colorOriginal = STANDARDBUTTON_COLOR;
        currentColor->rectColor = true;
        currentColor->Tseconds = 0.1f;
        currentColor->execute.hovered = true;
    }
    
    GUI_TEMPLATE_ELEMENT_ADD_ATTRIBUTE_XY((gX_F32)gX_SCENE_TITLE,0.0f)
    GUI_TEMPLATE_ELEMENT_ADD_DATA_SPR(src,false,true,maindata.masterIndex,I32)
    GUI_TEMPLATE_ELEMENT_DATA_BUTTON_FUNC(attributeIndex)
    
    GUI_TEMPLATE_ELEMENT_ADD_ATTRIBUTE_XY(1.0f,0.0f)
    /*GUI_TEMPLATE_ELEMENT_ADD_DATA_SPR(src,false,true,gui.refresh,I1)
    GUI_TEMPLATE_ELEMENT_DATA_BUTTON_FUNC(attributeIndex)*/
    GUI_TEMPLATE_ELEMENT_ADD_DATA_SPR(src,false,true,maindata.masterIndexSwitch,I1)
    GUI_TEMPLATE_ELEMENT_DATA_BUTTON_FUNC(attributeIndex)
    
}
