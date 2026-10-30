/**********************************************************************************************
*
*   [gX: src/gui/element/modules.h]
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


#ifndef ELEMENT_MODULES_H
#define ELEMENT_MODULES_H

#ifdef __cplusplus
extern "C" {
#endif

#define gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR(uppercase,lowercase) \
inline gX_I0 gX_GUI_Element_##uppercase##_Clear(gX_GUI_Element_##uppercase *lowercase) { \
    *lowercase = (gX_GUI_Element_##uppercase){ 0 }; \
} \

#define gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(uppercase,lowercase) \
gX_GUI_Element_##uppercase *gX_GUI_Element_##uppercase##_Get(gX_GUI_Element *element, gX_U32 index) { \
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"GUI_Element_"#uppercase"_Get","GUI_ELEMENT: Getting "#lowercase) \
     \
    gX_GUI_Element_##uppercase *ptr = 0; \
     \
    /*opencode: check amount not max*/ \
    if (index >= element->lowercase##s.amount) { \
        gX_TRACELOG_EPILOGUE("GUI_ELEMENT: Index exceeds max") \
        return ptr; \
    } \
    /*opencode: reject removed (hole) slots — Remove leaves USED=false with \
       amount unchanged, so Get previously returned a zeroed block as valid*/ \
    if (!element->lowercase##s.meta[index].USED) { \
        gX_TRACELOG_EPILOGUE("GUI_ELEMENT: Index not in use") \
        return ptr; \
    } \
     \
    gX_GUI_Element_##uppercase *heap = element->lowercase##s.data; \
    ptr = &(heap[index]); \
     \
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"GUI_ELEMENT: Getting "#lowercase" successful") \
    return ptr; \
} \

//opencode: lazy init — heap allocated on first Add_* instead of at Load
//Note: Clear zeros everything. Init sets non-zero defaults.
//When adding a new field to a struct, update Init if the field needs
//a non-zero default value.
#define gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(uppercase,lowercase,uppersingular) \
gX_I32 gX_GUI_Element_Add_##uppercase(gX_GUI_Element *element) { \
    gX_I32 errors = 0; \
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"GUI_Element_Add_"#uppercase,"GUI_ELEMENT: Adding "#lowercase" to element") \
     \
    if (!(element->lowercase##s).state.INITIALIZED) { \
        gX_Heap_Zero(&(element->lowercase##s)); \
        gX_Heap_SetBlockSize(&(element->lowercase##s), sizeof(gX_GUI_Element_##uppercase)); \
        gX_Heap_SetMax(&(element->lowercase##s), gX_GUI_ELEMENT_DEFAULT_MAX_##uppersingular##S); \
        gX_Heap_SetAmount(&(element->lowercase##s), 0); \
        gX_Heap_Init(&(element->lowercase##s)); \
        element->state.KEEP_HEAP_##uppersingular = true; \
    } \
     \
    gX_GUI_Element_##uppercase *elementmodule = gX_Heap_Add_FirstEmpty_NoPtr(&(element->lowercase##s)); \
    if (!elementmodule) { \
        gX_TraceLog(gX_LOG_WARNING, "GUI_ELEMENT: "#uppercase" heap full (max %i)", \
            gX_GUI_ELEMENT_DEFAULT_MAX_##uppersingular##S); \
        errors++; \
        return errors; \
    } \
    gX_TraceLog(gX_LOG_TRACE,"GUI_ELEMENT: Clearing "#lowercase); \
    gX_GUI_Element_##uppercase##_Clear(elementmodule); \
    gX_TraceLog(gX_LOG_TRACE,"GUI_ELEMENT: Initializing "#lowercase); \
    gX_GUI_Element_##uppercase##_Init(elementmodule); \
     \
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"GUI_ELEMENT: "#uppercase" added to element") \
    return errors; \
} \

#define gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(uppercase,lowercase) \
gX_I32 gX_GUI_Element_Remove_##uppercase(gX_GUI_Element *element, gX_U32 index) { \
    gX_I32 errors = 0; \
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"GUI_Element_Remove_"#uppercase,"GUI_ELEMENT: Removing "#lowercase" from element") \
     \
    gX_GUI_Element_##uppercase *elementmodule = gX_Heap_Remove_Index_NoPtr(&(element->lowercase##s),index); \
    if (!elementmodule) { \
        errors++; \
        return errors; \
    } \
    gX_GUI_Element_##uppercase##_Clear(elementmodule); \
     \
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"GUI_ELEMENT: "#uppercase" removed from element") \
    return errors; \
} \

#define gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE(uppercase,lowercase) \
gX_I32 gX_GUI_Element_Clear_##uppercase(gX_GUI_Element *element) { \
    gX_I32 errors = 0; \
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,"GUI_Element_Clear_"#uppercase,"GUI_ELEMENT: Clearing all "#lowercase"s from element") \
     \
    gX_GUI_Element_##uppercase *heapPtr = element->lowercase##s.data; \
    for (gX_U32 i = 0; i < element->lowercase##s.amount; i++) { \
        gX_GUI_Element_##uppercase *elementmodule = &heapPtr[i]; \
        gX_GUI_Element_##uppercase##_Clear(elementmodule); \
    } \
     \
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,"GUI_ELEMENT: "#uppercase"s cleared from element") \
    return errors; \
}

#ifdef __cplusplus
}
#endif

#endif //ELEMENT_MODULES_H
