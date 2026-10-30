/**********************************************************************************************
*
*   [gX: src/gui/element/attribute.c]
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

#include "element.h"
#include "modules.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"

inline gX_I0 gX_GUI_Element_Attribute_Init(gX_GUI_Element_Attribute *attribute) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing attribute values");
    
    attribute->value = (gX_F32v4){ 0 };
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Attribute,attribute)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Attribute,attribute)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Attribute,attribute,ATTRIBUTE)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Attribute,attribute)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Attribute,attribute)
//gX_GUI_ELEMENT_MODULE_FUNCDEF_LOOP(  Attribute,attribute,Update)
