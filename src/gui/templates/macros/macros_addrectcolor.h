/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_addrectcolor.h]
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


#ifndef GUI_TEMPLATES_MACROS_ADDRECTCOLOR_H
#define GUI_TEMPLATES_MACROS_ADDRECTCOLOR_H

#include "../../enum.h"

#define CURRENTELEMENT_ADD_RECT(x,y,w,h) \
    currentElement->rect = (gX_Rectangle){(x),(y),(w),(h)}; \
    
#define CURRENTELEMENT_ADD_RECT_WH(x,y,w,h) \
    currentElement->rect = (gX_Rectangle){((x)*width),((y)*height),((w)*width),((h)*height)}; \
    
#define CURRENTELEMENT_ADD_RECT_WHD(x,y,w,h) \
    currentElement->rect = (gX_Rectangle){((x)*(gX_F32)widthDiv),((y)*(gX_F32)heightDiv),((w)*(gX_F32)widthDiv),((h)*(gX_F32)heightDiv)}; \
    
#define CURRENTELEMENT_ADD_RECTV(r) \
    currentElement->rect = (r); \
    
    
#define CURRENTELEMENT_ADD_COLOR(r,g,b,a) \
    currentElement->color = (gX_Color){(r),(g),(b),(a)}; \
    
#define CURRENTELEMENT_ADD_COLORV(c) \
    currentElement->color = (c); \
    
    
#define CURRENTELEMENT_ADD_RECT_COLOR(x,y,w,h,r,g,b,a) \
    CURRENTELEMENT_ADD_RECT(x,y,w,h) \
    CURRENTELEMENT_ADD_COLOR(r,g,b,a) \
    
#define CURRENTELEMENT_ADD_RECTWHD_COLOR(x,y,w,h,r,g,b,a) \
    CURRENTELEMENT_ADD_RECT_WHD(x,y,w,h) \
    CURRENTELEMENT_ADD_COLOR(r,g,b,a) \
    
#define CURRENTELEMENT_ADD_RECTV_COLORV(r,c) \
    CURRENTELEMENT_ADD_RECTV((r)); \
    CURRENTELEMENT_ADD_COLORV((c)); \
    
#define CURRENTELEMENT_ADD_RECTWHD_COLORV(x,y,w,h,c) \
    CURRENTELEMENT_ADD_RECT_WHD(x,y,w,h); \
    CURRENTELEMENT_ADD_COLORV((c)); \
    
    
#define GUI_TEMPLATE_ADD_ELEMENT_RECT_COLOR(x,y,w,h,r,g,b,a) \
    GUI_TEMPLATE_ADD_ELEMENT \
    CURRENTELEMENT_ADD_RECT_COLOR(x,y,w,h,r,g,b,a) \
    
#define GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLOR(x,y,w,h,r,g,b,a) \
    GUI_TEMPLATE_ADD_ELEMENT \
    CURRENTELEMENT_ADD_RECTWHD_COLOR(x,y,w,h,r,g,b,a) \
    
#define GUI_TEMPLATE_ADD_ELEMENT_RECTV_COLORV(r,c) \
    GUI_TEMPLATE_ADD_ELEMENT \
    CURRENTELEMENT_ADD_RECTV_COLORV(r,c) \
    
#define GUI_TEMPLATE_ADD_ELEMENT_RECTWHD_COLORV(x,y,w,h,c) \
    GUI_TEMPLATE_ADD_ELEMENT \
    CURRENTELEMENT_ADD_RECTWHD_COLORV(x,y,w,h,c) \

#endif //GUI_TEMPLATES_MACROS_ADDRECTCOLOR_H
