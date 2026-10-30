/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/shape.h]
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


#ifndef GUI_TEMPLATES_MACROS_SHAPE_H
#define GUI_TEMPLATES_MACROS_SHAPE_H

#define GUI_TEMPLATE_ELEMENT_ADD_SHAPE_CIRCLE(cx,cy,rad) \
    GUI_TEMPLATE_ELEMENT_ADD_SHAPE \
    if (shapeOK) {  \
        currentShape->type = gX_GUI_ELEMENT_SHAPE_CIRCLE; \
        currentShape->scalar[0] = (rad); \
        currentShape->v2[0] = (gX_F32v2){(cx),(cy)}; \
    } \

#endif //GUI_TEMPLATES_MACROS_SHAPE_H
