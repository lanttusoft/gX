/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/attribute.h]
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


#ifndef GUI_TEMPLATES_MACROS_ATTRIBUTE_H
#define GUI_TEMPLATES_MACROS_ATTRIBUTE_H

#define GUI_TEMPLATE_ELEMENT_ATTRIBUTE_X(valx) \
    if (attributeOK) { \
        currentAttribute->value.x = (valx); \
    } \

#define GUI_TEMPLATE_ELEMENT_ATTRIBUTE_XY(valx,valy) \
    if (attributeOK) { \
        currentAttribute->value.x = (valx); \
        currentAttribute->value.y = (valy); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ATTRIBUTE_XYZ(valx,valy,valz) \
    if (attributeOK) { \
        currentAttribute->value.x = (valx); \
        currentAttribute->value.y = (valy); \
        currentAttribute->value.z = (valz); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ATTRIBUTE_XYZW(valx,valy,valz,valw) \
    if (attributeOK) { \
        currentAttribute->value.x = (valx); \
        currentAttribute->value.y = (valy); \
        currentAttribute->value.z = (valz); \
        currentAttribute->value.w = (valw); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ADD_ATTRIBUTE_XY(valx,valy) \
    GUI_TEMPLATE_ELEMENT_ADD_ATTRIBUTE \
    GUI_TEMPLATE_ELEMENT_ATTRIBUTE_XY(valx,valy) \

#endif //GUI_TEMPLATES_MACROS_ATTRIBUTE_H
