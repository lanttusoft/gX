/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_color.h]
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


#ifndef GUI_TEMPLATES_MACROS_COLOR_H
#define GUI_TEMPLATES_MACROS_COLOR_H

#define GUI_TEMPLATE_ELEMENT_COLOR_COLOR(r,g,b,a) \
    if (colorOK) { \
        currentColor->color = (gX_Color){(r),(g),(b),(a)}; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_COLOR_COLORV(c) \
    if (colorOK) { \
        currentColor->color = (c); \
    } \

#define GUI_TEMPLATE_ELEMENT_COLOR_ADDGRADIENT(r,g,b,a) \
    if (colorOK) { \
        GUI_TEMPLATE_ELEMENT_COLOR_COLOR(r,g,b,a); \
        currentColor->colorOriginal = currentElement->color; \
        currentColor->gradientColor = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_COLORV_ADDGRADIENT(c) \
    if (colorOK) { \
        GUI_TEMPLATE_ELEMENT_COLOR_COLORV((c)); \
        currentColor->colorOriginal = currentElement->color; \
        currentColor->gradientColor = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_COLOR_ADDRECT(r,g,b,a) \
    if (colorOK) { \
        GUI_TEMPLATE_ELEMENT_COLOR_COLOR(r,g,b,a); \
        currentColor->colorOriginal = currentElement->color; \
        currentColor->rectColor = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_COLORV_ADDRECT(c) \
    if (colorOK) { \
        GUI_TEMPLATE_ELEMENT_COLOR_COLORV((c)) \
        currentColor->colorOriginal = currentElement->color; \
        currentColor->rectColor = true; \
    } \
    

#endif //GUI_TEMPLATES_MACROS_COLOR_H
