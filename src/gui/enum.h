/**********************************************************************************************
*
*   [gX: src/gui/enum.h]
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


#ifndef GUI_ENUM_H
#define GUI_ENUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define gX_GUI_TEMPLATE \
    X(EXAMPLE) \
    X(DEMO) \
    X(DEMO2) \
    X(SCENE) \
    X(MESSAGE) \
    X(DIALOG) \
    X(ESCAPEMENU) \
    X(DEBUGDISPLAY) \
    X(SETTINGS) \
    X(TITLEGX) \
    X(SDFT) \
    X(GUITESTER) \
    X(GUIEDITOR) \
    X(THEMLAB) \
    X(MATERIALSLAB) \
    X(SHADERLAB) \
    X(VIEWPORTGRID) \
    X(SYNTHVIS_KEYBOARD) \

enum gX_GUI_Templates {
    gX_GUI_TEMPLATE_NULL = 0,
#define X(templ) gX_GUI_TEMPLATE_##templ,
    gX_GUI_TEMPLATE
#undef X
    gX_GUI_TEMPLATE_COUNT //opencode: v0.1.40 — audit loops / registry bounds
};
const gX_I8 *gX_GUI_Templates_String(gX_I32 templa);

#define gX_GUI_ELEMENT_TEMPLATE \
    X(BUTTON) \
    X(SLIDER) \
    X(ROTATEKNOB) \
    X(CHECKBOX) \
    X(DROPDOWN) \
    X(RADIO) \
    X(BUTTON_STANDARD) \
    
enum gX_GUI_Element_Templates {
    gX_GUI_ELEMENT_TEMPLATE_NULL = 0,
#define X(templ) gX_GUI_ELEMENT_TEMPLATE_##templ,
    gX_GUI_ELEMENT_TEMPLATE
#undef X
};
const gX_I8 *gX_GUI_Element_Templates_String(gX_I32 templa);

#define gX_GUI_ELEMENT_FUNCTION_OPERATORS \
    X(SET) \
    X(ADD) \
    X(SUB) \
    X(MUL) \
    X(DIV) \
    X(MOD) \
    X(AND) \
    X(OR ) \
    X(XOR) \
    X(LBS) \
    X(RBS) \
    X(INC) \
    X(DEC) \
    X(TRUE) \
    X(FALSE) \
    X(NEG) \
    X(INV) \
    X(CMP) \
    X(MATH) \
    
enum gX_GUI_Element_Function_Operators {
    gX_GUI_ELEMENT_FUNCTION_NULL = 0,
#define X(op) gX_GUI_ELEMENT_FUNCTION_##op,
    gX_GUI_ELEMENT_FUNCTION_OPERATORS
#undef X
};
const gX_I8 *gX_GUI_Element_Function_Operators_String(gX_I32 op);

#define gX_GUI_ELEMENT_SHAPES \
    X(RECTANGLE) \
    X(POINT) \
    X(LINE) \
    X(BEZIER) \
    X(CIRCLE) \
    X(ELLIPSE) \
    X(RING) \
    X(TRIANGLE) \
    X(POLYGON) \

#define gX_GUI_ELEMENT_LAYOUT_DISPLAYS \
    X(BLOCK) \
    X(FLEX_ROW) \
    X(FLEX_COL) \

enum gX_GUI_Element_Layout_Display {
    gX_GUI_ELEMENT_LAYOUT_DISPLAY_NULL = 0,
#define X(display) gX_GUI_ELEMENT_LAYOUT_DISPLAY_##display,
    gX_GUI_ELEMENT_LAYOUT_DISPLAYS
#undef X
};
const gX_I8 *gX_GUI_Element_Layout_Display_String(gX_I32 display);

#define gX_GUI_ELEMENT_LAYOUT_JUSTIFYS \
    X(START) \
    X(CENTER) \
    X(END) \
    X(SPACE_BETWEEN) \

enum gX_GUI_Element_Layout_Justify {
    gX_GUI_ELEMENT_LAYOUT_JUSTIFY_NULL = 0,
#define X(justify) gX_GUI_ELEMENT_LAYOUT_JUSTIFY_##justify,
    gX_GUI_ELEMENT_LAYOUT_JUSTIFYS
#undef X
};
const gX_I8 *gX_GUI_Element_Layout_Justify_String(gX_I32 justify);

#define gX_GUI_ELEMENT_LAYOUT_ALIGNS \
    X(START) \
    X(CENTER) \
    X(END) \
    X(STRETCH) \

enum gX_GUI_Element_Layout_Align {
    gX_GUI_ELEMENT_LAYOUT_ALIGN_NULL = 0,
#define X(align) gX_GUI_ELEMENT_LAYOUT_ALIGN_##align,
    gX_GUI_ELEMENT_LAYOUT_ALIGNS
#undef X
};
const gX_I8 *gX_GUI_Element_Layout_Align_String(gX_I32 align);
    
enum gX_GUI_Element_Shapes {
    gX_GUI_ELEMENT_SHAPE_NULL = 0,
#define X(shape) gX_GUI_ELEMENT_SHAPE_##shape,
    gX_GUI_ELEMENT_SHAPES
#undef X
};
const gX_I8 *gX_GUI_Element_Shapes_String(gX_I32 shape);

#define gX_GUI_ELEMENT_TYPES \
    X(UNKNOWN) \
    X(RECT) \
    X(LABEL) \
    X(BUTTON) \
    X(SLIDER) \
    X(CHECKBOX) \
    X(DROPDOWN) \
    X(RADIO) \
    X(TOGGLE) \
    X(PROGRESS) \
    X(TEXTURE) \
    X(SHADER) \
    X(SHAPE) \
    X(MODEL) \
    X(PANEL) \
    X(CONTAINER) \
    X(TOOLTIP) \
    X(SPINNER) \
    X(STEPPER) \
    X(SCROLLLIST) \
    X(TABPANEL) \
    X(TEXTINPUT) \

typedef enum {
    gX_GUI_ELEMENT_TYPE_NULL = 0,
#define X(t) gX_GUI_ELEMENT_TYPE_##t,
    gX_GUI_ELEMENT_TYPES
#undef X
} gX_GUI_Element_Type;
const gX_I8 *gX_GUI_Element_Type_String(gX_I32 type);

#define gX_GUI_TRANSITION_TEMPLATES \
    X(TEST) \
    X(FADE_IN) \
    X(FADE_OUT) \
    
enum gX_GUI_Transition_Templates {
    gX_GUI_TRANSITION_TEMPLATE_NULL = 0,
#define X(templ) gX_GUI_TRANSITION_TEMPLATE_##templ,
    gX_GUI_TRANSITION_TEMPLATES
#undef X
};
const gX_I8 *gX_GUI_Transition_Templates_String(gX_I32 templa);

#ifdef __cplusplus
}
#endif

#endif //GUI_ENUM_H
