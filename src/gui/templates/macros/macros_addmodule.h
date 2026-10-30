/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_addmodule.h]
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


#ifndef GUI_TEMPLATES_MACROS_ADDMODULE_H
#define GUI_TEMPLATES_MACROS_ADDMODULE_H

#include "../../enum.h"

#define GUI_TEMPLATE_ELEMENT_ADD_MODULE(modlc,moduc) \
    gX_GUI_Element_Add_##moduc(currentElement); \
    modlc##Index++; \
    current##moduc = gX_GUI_Element_##moduc##_Get(currentElement,(gX_U32)modlc##Index); \
    if (!current##moduc) { \
        gX_TraceLog(gX_LOG_WARNING,"GUI: "#moduc" nullptr"); \
        modlc##OK = false; \
    } \
    else modlc##OK = true; \
        
#define GUI_TEMPLATE_ELEMENT_ADD_ANIMATION \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(animation,Animation) \
   
#define GUI_TEMPLATE_ELEMENT_ADD_ATTRIBUTE \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(attribute,Attribute) \

#define GUI_TEMPLATE_ELEMENT_ADD_COLOR \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(color,Color) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_DATA \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(data,Data) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_MODEL \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(model,Model) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_SHADER \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(shader,Shader) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_SHAPE \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(shape,Shape) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_SOUND \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(sound,Sound) \

#define GUI_TEMPLATE_ELEMENT_ADD_TEXT \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(text,Text) \
    
#define GUI_TEMPLATE_ELEMENT_ADD_TEXTURE \
    GUI_TEMPLATE_ELEMENT_ADD_MODULE(texture,Texture) \

#endif //GUI_TEMPLATES_MACROS_ADDMODULE_H
