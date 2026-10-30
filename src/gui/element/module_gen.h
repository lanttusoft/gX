/**********************************************************************************************
*
*   [gX: src/gui/element/module_gen.h]
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


#ifndef GUI_ELEMENT_MODULE_GEN_H
#define GUI_ELEMENT_MODULE_GEN_H

//opencode: generates per-module function declarations (Init, Clear, Get, Add, Remove, Clear_)
#define gX_GUI_Element_ModuleGen(name) \
    gX_I0 gX_GUI_Element_##name##_Init(     gX_GUI_Element_##name *modu); \
    gX_I0 gX_GUI_Element_##name##_Clear(    gX_GUI_Element_##name *modu); \
    gX_GUI_Element_##name *gX_GUI_Element_##name##_Get(gX_GUI_Element *element, gX_U32 index); \
    gX_I32 gX_GUI_Element_Add_##name(      gX_GUI_Element *element); \
    gX_I32 gX_GUI_Element_Remove_##name(   gX_GUI_Element *element, gX_U32 index); \
    gX_I32 gX_GUI_Element_Clear_##name(    gX_GUI_Element *element); /*clear all*/ \

#endif //GUI_ELEMENT_MODULE_GEN_H
