/**********************************************************************************************
*
*   [gX: src/gui/templates/template_header.h]
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


#ifndef GX_GUI_TEMPLATE_HEADER_H
#define GX_GUI_TEMPLATE_HEADER_H

//std
#include <string.h>
#include <math.h>

//local template macros
#include "macros.h"

//gui stuff
#include "../gui.h"
#include "../builder.h"
#include "../canvas.h"
#include "../element/element.h"
#include "../enum.h"

//common utility stuff
#include "../../common/types.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/enum.h"
#include "../../common/utils.h"
#include "../../common/data.h"

//gxmath access
#include "../../gxmath/gxmath.h"

//wrapper access
#include "../../wrappers/wrappers.h"

//maindata access
#include "../../main.h"

//resourcemanager access
#include "../../resource/resource.h"

//audio access
#include "../../audio.h"

//settings access
#include "../../settings.h"

//element templates
extern gX_I0 gX_GUI_Element_Template_Button(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Button_Standard(gX_GUI_Element *element);
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
extern gX_I0 gX_GUI_Element_Template_Slider(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_CheckBox(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_DropDown(gX_GUI_Element *element);
extern gX_I0 gX_GUI_Element_Template_Radio(gX_GUI_Element *element);

extern gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader);

#endif //GX_GUI_TEMPLATE_HEADER_H
