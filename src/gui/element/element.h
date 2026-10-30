/**********************************************************************************************
*
*   [gX: src/gui/element/element.h]
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


#ifndef GUI_ELEMENT_H
#define GUI_ELEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/heap.h"
#include "../../common/types.h"

#include "struct.h"
#include "module_gen.h"
#include "../canvas.h"

gX_I32 gX_GUI_Element_Load(          gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I32 gX_GUI_Element_Unload(        gX_GUI_Element *element);

gX_I32 gX_GUI_Element_Clear(         gX_GUI_Element *element);

gX_GUI_Element_ModuleGen(Animation)
gX_GUI_Element_ModuleGen(Attribute)
gX_GUI_Element_ModuleGen(Color)
gX_GUI_Element_ModuleGen(Data)
gX_GUI_Element_ModuleGen(Layout)
gX_GUI_Element_ModuleGen(Model)
gX_GUI_Element_ModuleGen(Shader)
gX_GUI_Element_ModuleGen(Shape)
gX_GUI_Element_ModuleGen(Sound)
gX_GUI_Element_ModuleGen(Text)
gX_GUI_Element_ModuleGen(Texture)
gX_GUI_Element_ModuleGen(Timer)
gX_GUI_Element_ModuleGen(TextInput)

//opencode: shader module internals used by texture/model/shape elements when
//   they reference a shader element via shaderIndex
gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader);
gX_I0 gX_GUI_Element_Shader_Uniforms_Init(gX_GUI_Element_Shader *shader);
gX_I0 gX_GUI_Element_Shader_Uniforms_Set(gX_GUI_Element_Shader *shader);
gX_I0 gX_GUI_Element_Shader_UploadUniforms(gX_GUI_Element_Shader *shader, gX_Rectangle box,
    gX_F32 time, gX_F32v2 res, gX_Color col, gX_F32v2 scale, gX_F32 ambientFactor);

gX_I0 gX_GUI_Element_Make(          gX_GUI_Element *element, gX_I32 index);

gX_I0 gX_GUI_Element_Update(        gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw(          gX_GUI_Element *element, gX_GUI_Canvas *canvas);

//opencode: run the per-element data-module update loop (declared here for the
//   callback-context unit tests).
gX_I0 gX_GUI_Element_Update_Datas(  gX_GUI_Element *element);

//opencode: draw-truth text layout verdict for the audit trace and the
//   headless fit tests (wrap/truncate/shrink/overflow flags).
gX_GUI_Element_Text_Layout gX_GUI_Element_Text_Analyze(
    const gX_GUI_Element *element, gX_I32 textIndex);
//opencode: pure shrink-to-fit scale (1 = no shrink; floored at the readable
//   minimum). Font-free so headless tests can pin the floor math.
gX_F32 gX_GUI_Element_Text_FitScale(gX_F32 naturalWidth, gX_F32 rectWidth);

//opencode: standard clickable element setup (sets HOVERABLE, CLICKABLE_LEFT, PRESSABLE_LEFT)
gX_I0 gX_GUI_Element_MakeClickable( gX_GUI_Element *element);

#ifdef __cplusplus
}
#endif

#endif //GUI_ELEMENT_H
