/**********************************************************************************************
*
*   [gX: src/gui/enum.c]
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


#include "enum.h"

#define gX_ENUMNAME_CASE(name,stri) \
    case name: return stri; break;
    
const gX_I8 *gX_GUI_Element_Function_Operators_String(gX_I32 op) {
#define X(op) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_FUNCTION_##op,#op);
    switch (op) {
        gX_GUI_ELEMENT_FUNCTION_OPERATORS
        default: return "undefined";
    }
#undef X    
}

const gX_I8 *gX_GUI_Element_Shapes_String(gX_I32 shape) {
#define X(shape) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_SHAPE_##shape,#shape);
    switch (shape) {
        gX_GUI_ELEMENT_SHAPES
        default: return "undefined";
    }
#undef X
}

//opencode: layout box-model enums (display/justify/align)
const gX_I8 *gX_GUI_Element_Layout_Display_String(gX_I32 display) {
#define X(display) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_LAYOUT_DISPLAY_##display,#display);
    switch (display) {
        gX_GUI_ELEMENT_LAYOUT_DISPLAYS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GUI_Element_Layout_Justify_String(gX_I32 justify) {
#define X(justify) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_LAYOUT_JUSTIFY_##justify,#justify);
    switch (justify) {
        gX_GUI_ELEMENT_LAYOUT_JUSTIFYS
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GUI_Element_Layout_Align_String(gX_I32 align) {
#define X(align) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_LAYOUT_ALIGN_##align,#align);
    switch (align) {
        gX_GUI_ELEMENT_LAYOUT_ALIGNS
        default: return "undefined";
    }
#undef X
}

//opencode: added missing template string functions
const gX_I8 *gX_GUI_Templates_String(gX_I32 templa) {
#define X(templ) gX_ENUMNAME_CASE(gX_GUI_TEMPLATE_##templ,#templ);
    switch (templa) {
        gX_GUI_TEMPLATE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GUI_Element_Templates_String(gX_I32 templa) {
#define X(templ) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_TEMPLATE_##templ,#templ);
    switch (templa) {
        gX_GUI_ELEMENT_TEMPLATE
        default: return "undefined";
    }
#undef X
}

const gX_I8 *gX_GUI_Element_Type_String(gX_I32 type) {
#define X(t) gX_ENUMNAME_CASE(gX_GUI_ELEMENT_TYPE_##t,#t);
    switch (type) {
        gX_GUI_ELEMENT_TYPES
        default: return "UNKNOWN";
    }
#undef X
}

const gX_I8 *gX_GUI_Transition_Templates_String(gX_I32 templa) {
#define X(templ) gX_ENUMNAME_CASE(gX_GUI_TRANSITION_TEMPLATE_##templ,#templ);
    switch (templa) {
        gX_GUI_TRANSITION_TEMPLATES
        default: return "undefined";
    }
#undef X
}
