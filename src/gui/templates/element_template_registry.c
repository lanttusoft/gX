/**********************************************************************************************
*
*   [gX: src/gui/templates/element_template_registry.c]
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


#include "element_template_registry.h"

#include "../../common/log.h"
#include "../enum.h"

//opencode: element template registry — replaces switch in element/make.c

extern gX_I0 gX_GUI_Element_Template_Button(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Slider(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_CheckBox(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_DropDown(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Radio(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Button_Standard(gX_GUI_Element *element);

gX_GUI_ElementTemplateEntry gX_elementTemplateRegistry[GX_GUI_ELEMENT_TEMPLATE_REGISTRY_MAX];
gX_U32 gX_elementTemplateCount = 0;

gX_I0 gX_GUI_ElementTemplate_Register(gX_I32 id, const gX_I8 *name, gX_GUI_ElementTemplateFunc func) {
    if (gX_elementTemplateCount >= GX_GUI_ELEMENT_TEMPLATE_REGISTRY_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "ElementTemplate_Register: registry full (%s)", name);
        return;
    }
    gX_U32 idx = gX_elementTemplateCount++;
    gX_elementTemplateRegistry[idx].id   = id;
    gX_elementTemplateRegistry[idx].name = name;
    gX_elementTemplateRegistry[idx].func = func;
}

gX_I32 gX_GUI_ElementTemplate_Dispatch(gX_I32 id, gX_GUI_Element *element) {
    for (gX_U32 i = 0; i < gX_elementTemplateCount; i++) {
        if (gX_elementTemplateRegistry[i].id == id) {
            gX_elementTemplateRegistry[i].func(element);
            return 0;
        }
    }
    gX_TraceLog(gX_LOG_WARNING, "ElementTemplate_Dispatch: template id %i not found", id);
    return 1;
}

gX_I0 gX_GUI_ElementTemplate_InitRegistry(gX_I0) {
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_BUTTON,         "Button",         gX_GUI_Element_Template_Button);
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_SLIDER,         "Slider",         gX_GUI_Element_Template_Slider);
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_CHECKBOX,       "CheckBox",       gX_GUI_Element_Template_CheckBox);
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_DROPDOWN,       "DropDown",       gX_GUI_Element_Template_DropDown);
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_RADIO,          "Radio",          gX_GUI_Element_Template_Radio);
    gX_GUI_ElementTemplate_Register(gX_GUI_ELEMENT_TEMPLATE_BUTTON_STANDARD,"Button_Standard",gX_GUI_Element_Template_Button_Standard);
}
