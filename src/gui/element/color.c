/**********************************************************************************************
*
*   [gX: src/gui/element/color.c]
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


#include <string.h>

#include "element.h"
#include "modules.h"

#include "../utils.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/random.h"
#include "../../common/utils.h"

#include "../../gxmath/color.h"
#include "../../gxmath/scalar.h"

#include "../../main.h"

inline gX_I0 gX_GUI_Element_Color_Init(gX_GUI_Element_Color *color) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing color values");
    
    color->factor = (gX_F32v4){ 1.0f, 1.0f, 1.0f, 1.0f };
    color->color = (gX_Color){ 0 };
    //opencode: initialize colorOriginal to white so lerp doesn't go through black if unset by caller
    color->colorOriginal = (gX_Color){ 255, 255, 255, 255 };
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Color_Update(gX_GUI_Element_Color *color, gX_GUI_Element *element) {
    
    gX_I1 execute = false;
    
    gX_GUI_Element_ExecuteState_Update(&color->execute, element, &execute);
    
    //opencode: guard against targetFPS == 0 dividing by zero
    gX_F32 fps = (maindata.targetFPS > 0.0f) ? maindata.targetFPS : 60.0f;
    if (execute) {
        color->T += 1.0f/(color->Tseconds*fps);
    }
    else {
        color->T -= 1.0f/(color->Tseconds*fps);
    }
    
    if (color->T <= 0.0f) {color->T = 0.0f;}
    if (color->T >= 1.0f) {color->T = 1.0f;}
    
    if (color->smoothRandomColor) {
        color->color = rng.colorSmooth;
    }
    
    if (color->rectColor) {
        if (color->modeMultiply) {
            gX_Color colorOriginal = color->colorOriginal;
            gX_F32v4 colOrig = (gX_F32v4){colorOriginal.r,colorOriginal.g,colorOriginal.b,colorOriginal.a};
            gX_F32v4 factor = color->factor;
            element->color.r = (gX_U8)(gX_F32_Lerp(colOrig.x, factor.x*colOrig.x, color->T));
            element->color.g = (gX_U8)(gX_F32_Lerp(colOrig.y, factor.y*colOrig.y, color->T));
            element->color.b = (gX_U8)(gX_F32_Lerp(colOrig.z, factor.z*colOrig.z, color->T));
            element->color.a = (gX_U8)(gX_F32_Lerp(colOrig.w, factor.w*colOrig.w, color->T));
        }
        else {
            element->color = gX_Color_Lerp(color->colorOriginal, color->color, color->T);
        }
    }
    //opencode: bounds check textColorIndex against element->texts.amount
    if (color->textColor && element->texts.amount > 0 && color->textColorIndex < element->texts.amount) {
        gX_GUI_Element_Text *texts = element->texts.data;
        texts[color->textColorIndex].color = gX_Color_Lerp(color->colorOriginal, color->color, color->T);
    }
    
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Color,color)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Color,color)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Color,color,COLOR)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Color,color)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Color,color)
//opencode: inlined from LOOP macro for navigability
gX_I0 gX_GUI_Element_Update_Colors(gX_GUI_Element *element);
gX_I0 gX_GUI_Element_Update_Colors(gX_GUI_Element *element) {
    gX_Heap heapStruct = element->colors;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Color *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Color *elementmodule = &heapPtr[i];
            gX_GUI_Element_Color_Update(elementmodule,element);
        }
    }
}
