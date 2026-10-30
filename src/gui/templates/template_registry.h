/**********************************************************************************************
*
*   [gX: src/gui/templates/template_registry.h]
*   GUI template registry header
*
*   Copyright (c) 2026 lanttu, opencode
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


#ifndef GUI_TEMPLATE_REGISTRY_H
#define GUI_TEMPLATE_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

//opencode: template registry — replaces the manual extern declarations and
//switch dispatch in make.c. Follows the same pattern as the ImGui panel registry.

#define GX_GUI_TEMPLATE_REGISTRY_MAX 64

typedef gX_I0 (*gX_GUI_TemplateFunc)(gX_I32 canvasIndex);

typedef struct gX_GUI_TemplateEntry {
    gX_I32              id;
    gX_GUI_TemplateFunc func;
    const gX_I8        *name;
} gX_GUI_TemplateEntry;

extern gX_GUI_TemplateEntry gX_guiTemplateRegistry[GX_GUI_TEMPLATE_REGISTRY_MAX];
extern gX_U32 gX_guiTemplateCount;

gX_I0 gX_GUI_Template_Register(gX_I32 id, const gX_I8 *name, gX_GUI_TemplateFunc func);

//look up and call a template by ID — returns 0 on success, 1 if not found
gX_I32 gX_GUI_Template_Dispatch(gX_I32 id, gX_I32 canvasIndex);

//initialize the registry with all known templates
gX_I0 gX_GUI_Template_InitRegistry(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GUI_TEMPLATE_REGISTRY_H
