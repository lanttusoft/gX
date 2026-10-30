/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/macros_model.h]
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


#ifndef GUI_TEMPLATES_MACROS_MODEL_H
#define GUI_TEMPLATES_MACROS_MODEL_H

//opencode: shaderIndex-based shader attachment — the model references a
//   shader ELEMENT by index; the shader element owns program/locs/uniforms
#define GUI_TEMPLATE_ELEMENT_MODEL_ADDSHADER(index) \
    if (modelOK) { \
        currentModel->useShader = true; \
        currentModel->shaderIndex = (index); \
    } \

//opencode: filename-based shader lookup for models — adds a shader element
//   (model-only, no rect pass) and wires the model to it by index
#define GUI_TEMPLATE_ELEMENT_MODEL_ADDSHADER_FRAG(fragfile) \
    if (modelOK) { \
        GUI_TEMPLATE_ELEMENT_ADD_SHADER \
        gX_GUI_Element_Shader *shEl = (currentElement->shaders.amount > 0) \
            ? &((gX_GUI_Element_Shader*)currentElement->shaders.data)[currentElement->shaders.amount - 1] : NULL; \
        if (shEl) { \
            shEl->shader = (gX_Shader*)gX_RMHeap_GetPtr_FileName2(shaderHeap, fragfile); \
            if (shEl->shader) { \
                shEl->drawShader = false; \
                gX_GUI_Element_Shader_Load(shEl); \
                currentModel->useShader = true; \
                currentModel->shaderIndex = currentElement->shaders.amount - 1; \
            } \
            else { \
                gX_TraceLog(gX_LOG_WARNING, "GUI_TEMPLATE: Model shader not found: %s", fragfile); \
            } \
        } \
    } \
    
//opencode: useGUIbuffer removed (dead field) — GUI-buffer material binding no
//   longer supported
#define GUI_TEMPLATE_ELEMENT_MODEL_ADDGUIBUFFER

//opencode: shader locs removed — the shader ELEMENT owns locations (and
//   hot-reload re-queries them)
#define GUI_TEMPLATE_ELEMENT_MODEL_GET_SHADERLOCS

#define GUI_TEMPLATE_ELEMENT_ADD_MODEL_WITHSHADER(mo,sh) \
    GUI_TEMPLATE_ELEMENT_ADD_MODEL \
    if (modelOK) { \
        currentModel->model = &models[mo]; \
        GUI_TEMPLATE_ELEMENT_MODEL_ADDSHADER(sh) \
    } \

//opencode: filename-based variant for model+shader
#define GUI_TEMPLATE_ELEMENT_ADD_MODEL_WITHSHADER_FRAG(mo,fragfile) \
    GUI_TEMPLATE_ELEMENT_ADD_MODEL \
    if (modelOK) { \
        currentModel->model = &models[mo]; \
        GUI_TEMPLATE_ELEMENT_MODEL_ADDSHADER_FRAG(fragfile) \
    } \
    
#define GUI_TEMPLATE_ELEMENT_MODEL_ADDPARAM(sx,sy,sz, tx,ty,tz, rx,ry,rz) \
    if (modelOK) { \
        currentModel->useParameters = true; \
        currentModel->paramScale = (gX_F32v3){(sx),(sy),(sz)}; \
        currentModel->paramTranslate = (gX_F32v3){(tx),(ty),(tz)}; \
        currentModel->paramRotate = (gX_F32v3){(rx),(ry),(rz)}; \
    } \

#endif //GUI_TEMPLATES_MACROS_MODEL_H
