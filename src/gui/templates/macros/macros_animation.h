/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_animation.h]
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


#ifndef GUI_TEMPLATES_MACROS_ANIMATION_H
#define GUI_TEMPLATES_MACROS_ANIMATION_H

#include "../../enum.h"

#define GUI_TEMPLATE_ELEMENT_ANIMATION_DO_RECT \
    if (animationOK) { \
        currentAnimation->doRect = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ANIMATION_DO_MODEL_ROTATE(index) \
    if (animationOK) { \
        currentAnimation->modelIndex = index; \
        currentAnimation->doModelParamRotate = true; \
    } \
    
#define GUI_TEMPLATE_ELEMENT_ANIMATION_OFFSET_FROM_ELEMENTRECT \
    if (animationOK) { \
        currentAnimation->offset.x = currentElement->rect.x; \
        currentAnimation->offset.y = currentElement->rect.y; \
        currentAnimation->offset.z = currentElement->rect.width; \
        currentAnimation->offset.w = currentElement->rect.height; \
    } \

#endif //GUI_TEMPLATES_MACROS_ANIMATION_H
