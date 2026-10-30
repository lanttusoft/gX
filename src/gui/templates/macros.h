/**********************************************************************************************
*
*   [gX: src/gui/templates/macros.h]
*   GUI templates macros header
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


#ifndef GUI_TEMPLATES_MACROS_H
#define GUI_TEMPLATES_MACROS_H

#include "../enum.h"
#include "../element/element.h"
#include "../../common/types.h"

#include "macros/macros_addmodule.h"
#include "macros/macros_addrectcolor.h"
#include "macros/macros_animation.h"
#include "macros/attribute.h"
#include "macros/macros_color.h"
#include "macros/macros_data.h"
#include "macros/macros_model.h"
#include "macros/shader.h"
#include "macros/shape.h"
#include "macros/sound.h"
#include "macros/state.h"
#include "macros/macros_text.h"
#include "macros/texture.h"

#define GUI_TEMPLATE_TRACELOG_PROLOGUE(name) \
    gX_TRACELOG_PROLOGUE("GUI_Template_"#name,(const gX_I8*)gX_Text_Format( \
        "GUI_TEMPLATE: Making GUI template ["#name"] for canvas %i",canvasindex) \
    ) \
    
#define GUI_TEMPLATE_DIVISION_FACTOR 8

#define GUI_TEMPLATE_MODULEINDICES \
    gX_I32 animationIndex = -1; \
    gX_I32 attributeIndex = -1; \
    gX_I32 colorIndex = -1; \
    gX_I32 dataIndex = -1; \
    gX_I32 modelIndex = -1; \
    gX_I32 shaderIndex = -1; \
    gX_I32 shapeIndex = -1; \
    gX_I32 soundIndex = -1; \
    gX_I32 textIndex = -1; \
    gX_I32 textureIndex = -1; \
    (gX_I0)animationIndex; (gX_I0)attributeIndex; (gX_I0)colorIndex; (gX_I0)dataIndex; \
    (gX_I0)modelIndex; (gX_I0)shaderIndex; (gX_I0)shapeIndex; (gX_I0)soundIndex; \
    (gX_I0)textIndex; (gX_I0)textureIndex; \
    
#define GUI_TEMPLATE_MODULEINDICES_CLEAR \
    animationIndex = -1; \
    attributeIndex = -1; \
    colorIndex = -1; \
    dataIndex = -1; \
    modelIndex = -1; \
    shaderIndex = -1; \
    shapeIndex = -1; \
    soundIndex = -1; \
    textIndex = -1; \
    textureIndex = -1; \
    
#define GUI_TEMPLATE_MODULEOK \
    gX_I1 animationOK = false; \
    gX_I1 attributeOK = false; \
    gX_I1 colorOK = false; \
    gX_I1 dataOK = false; \
    gX_I1 modelOK = false; \
    gX_I1 shaderOK = false; \
    gX_I1 shapeOK = false; \
    gX_I1 soundOK = false; \
    gX_I1 textOK = false; \
    gX_I1 textureOK = false; \
    (gX_I0)animationOK; (gX_I0)attributeOK; (gX_I0)colorOK; (gX_I0)dataOK; \
    (gX_I0)modelOK; (gX_I0)shaderOK; (gX_I0)shapeOK; (gX_I0)soundOK; \
    (gX_I0)textOK; (gX_I0)textureOK; \
    
#define GUI_TEMPLATE_MODULEOK_CLEAR \
    animationOK = false; \
    attributeOK = false; \
    colorOK = false; \
    dataOK = false; \
    modelOK = false; \
    shaderOK = false; \
    shapeOK = false; \
    soundOK = false; \
    textOK = false; \
    textureOK = false; \

#define GUI_TEMPLATE_MODULEPOINTERS \
    gX_GUI_Element_Animation    *currentAnimation = 0; \
    gX_GUI_Element_Attribute    *currentAttribute = 0; \
    gX_GUI_Element_Color        *currentColor = 0; \
    gX_GUI_Element_Data         *currentData = 0; \
    gX_GUI_Element_Model        *currentModel = 0; \
    gX_GUI_Element_Shader       *currentShader = 0; \
    gX_GUI_Element_Shape        *currentShape = 0; \
    gX_GUI_Element_Sound        *currentSound = 0; \
    gX_GUI_Element_Text         *currentText = 0; \
    gX_GUI_Element_Texture      *currentTexture = 0; \
    (gX_I0)currentAnimation; (gX_I0)currentAttribute; (gX_I0)currentColor; \
    (gX_I0)currentData; (gX_I0)currentModel; (gX_I0)currentShader; \
    (gX_I0)currentShape; (gX_I0)currentSound; (gX_I0)currentText; (gX_I0)currentTexture; \
    
//opencode: gX backend does not use resource manager heap lookups
#define GUI_TEMPLATE_RESOURCEPOINTERS \
    gX_Font     *fonts = 0; \
    gX_Model    *models = 0; \
    gX_Shader   *shaders = 0; \
    gX_Sound    *sounds = 0; \
    gX_Texture  *textures = 0; \
    (gX_I0)fonts; (gX_I0)models; (gX_I0)shaders; (gX_I0)sounds; (gX_I0)textures; \
    
#define GUI_TEMPLATE_RESOURCEPOINTERS_GET \
    /* gX backend: resource heaps not available */ \

#define GUI_TEMPLATE_PROLOGUE(name,desc,nameuc) \
    GUI_TEMPLATE_TRACELOG_PROLOGUE(name) \
    gX_GUI_Canvas  *canvas = gX_Heap_SeekPtr_Index(&(gui.canvases), (gX_U32)canvasindex); \
    gX_GUI_Element *currentElement = 0; \
    gX_I32 elementIndex = -1; \
    gX_I32 width = maindata.screenWidth; \
    gX_I32 height = maindata.screenHeight; \
    gX_I32 widthDiv = width/GUI_TEMPLATE_DIVISION_FACTOR; \
    gX_I32 heightDiv = height/GUI_TEMPLATE_DIVISION_FACTOR; \
    gX_F32 aspRatio = (gX_F32)widthDiv/(gX_F32)heightDiv; \
    (gX_I0)aspRatio; \
    gX_GUI_Canvas_Add_Name(canvas, #name); \
    gX_GUI_Canvas_Add_Description(canvas, desc); \
    gX_GUI_Canvas_Add_CanvasID(canvas, canvasindex); \
    gX_GUI_Canvas_Add_TemplateID(canvas, gX_GUI_TEMPLATE_##nameuc); \
    GUI_TEMPLATE_MODULEPOINTERS \
    GUI_TEMPLATE_MODULEINDICES \
    GUI_TEMPLATE_MODULEOK \
    GUI_TEMPLATE_RESOURCEPOINTERS \
    GUI_TEMPLATE_RESOURCEPOINTERS_GET \
    
#define GUI_TEMPLATE_EPILOGUE \
    gX_TRACELOG_EPILOGUE("GUI_TEMPLATE: Making GUI template done") \

    
#define GUI_TEMPLATE_ADD_ELEMENT \
    gX_GUI_Canvas_Add_Element(canvas); \
    elementIndex++; \
    currentElement = gX_Heap_SeekPtr_Index(&(canvas->elements), (gX_U32)elementIndex); \
    currentElement->id = (gX_U32)elementIndex; \
    GUI_TEMPLATE_MODULEINDICES_CLEAR \
    GUI_TEMPLATE_MODULEOK_CLEAR \

#endif //GUI_TEMPLATES_MACROS_H
