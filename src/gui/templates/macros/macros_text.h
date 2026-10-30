/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_text.h]
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


#ifndef GUI_TEMPLATES_MACROS_TEXT_H
#define GUI_TEMPLATES_MACROS_TEXT_H

#include "../../enum.h"

#define GUI_TEMPLATE_ELEMENT_ADD_TEXT_STRING(string) \
    GUI_TEMPLATE_ELEMENT_ADD_TEXT \
    if (textOK) { \
        strcpy(currentText->text,string); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_FONTSIZE(fs) \
    if (textOK) { \
        currentText->fontSize = (fs); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_OFFSET(ox,oy) \
    if (textOK) { \
        currentText->offset = (gX_F32v2){(ox),(oy)}; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_SPACING(s) \
    if (textOK) { \
        currentText->spacing = (s); \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_FS_OF_SP(fs,ox,oy,s) \
    GUI_TEMPLATE_ELEMENT_TEXT_FONTSIZE(fs) \
    GUI_TEMPLATE_ELEMENT_TEXT_OFFSET(ox,oy) \
    GUI_TEMPLATE_ELEMENT_TEXT_SPACING(s) \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_COLOR(r,g,b,a) \
    if (textOK) { \
        currentText->color = (gX_Color){(r),(g),(b),(a)}; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_COLORV(c) \
    if (textOK) { \
        currentText->color = c; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_SET_FONT(ind) \
    if (textOK) { \
        currentText->hasCustomFont = true; \
        currentText->customFontPtr = &fonts[ind]; \
        currentText->customFontIndex = ind; \
    } \

#define GUI_TEMPLATE_ELEMENT_CENTER_TEXT_X \
    currentElement->stateRendering.TEXT_CENTER_X = true; \
    
#define GUI_TEMPLATE_ELEMENT_CENTER_TEXT_Y \
    currentElement->stateRendering.TEXT_CENTER_Y = true; \
    
#define GUI_TEMPLATE_ELEMENT_CENTER_TEXT \
    GUI_TEMPLATE_ELEMENT_CENTER_TEXT_X \
    GUI_TEMPLATE_ELEMENT_CENTER_TEXT_Y \
    
#define GUI_TEMPLATE_ELEMENT_TEXT_SHADOW \
    currentElement->stateRendering.TEXT_SHADOW = true; \

#endif //GUI_TEMPLATES_MACROS_TEXT_H
