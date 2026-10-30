/**********************************************************************************************
*
*   [gX: src/gui/templates/element_template_registry.h]
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


#ifndef GUI_ELEMENT_TEMPLATE_REGISTRY_H
#define GUI_ELEMENT_TEMPLATE_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../element/struct.h"

#define GX_GUI_ELEMENT_TEMPLATE_REGISTRY_MAX 16

typedef gX_I0 (*gX_GUI_ElementTemplateFunc)(gX_GUI_Element *element);

typedef struct gX_GUI_ElementTemplateEntry {
    gX_I32                    id;
    gX_GUI_ElementTemplateFunc func;
    const gX_I8              *name;
} gX_GUI_ElementTemplateEntry;

extern gX_GUI_ElementTemplateEntry gX_elementTemplateRegistry[GX_GUI_ELEMENT_TEMPLATE_REGISTRY_MAX];
extern gX_U32 gX_elementTemplateCount;

gX_I0 gX_GUI_ElementTemplate_Register(gX_I32 id, const gX_I8 *name, gX_GUI_ElementTemplateFunc func);
gX_I32 gX_GUI_ElementTemplate_Dispatch(gX_I32 id, gX_GUI_Element *element);
gX_I0 gX_GUI_ElementTemplate_InitRegistry(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_ELEMENT_TEMPLATE_REGISTRY_H
