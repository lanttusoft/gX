/**********************************************************************************************
*
*   [gX: src/gui/templates/macros/shader.h]
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


#ifndef GUI_TEMPLATES_MACROS_SHADER_H
#define GUI_TEMPLATES_MACROS_SHADER_H

#define GUI_TEMPLATE_ELEMENT_ADD_SHADER_SHADERS(index) \
    GUI_TEMPLATE_ELEMENT_ADD_SHADER \
    if (shaderOK) {  \
        currentShader->shader = &(shaders[index]); \
    } \

//opencode: filename-based shader lookup — stable when shader heap order changes
#define GUI_TEMPLATE_ELEMENT_ADD_SHADER_FRAG(fragfile) \
    GUI_TEMPLATE_ELEMENT_ADD_SHADER \
    if (shaderOK) { \
        currentShader->shader = (gX_Shader*)gX_RMHeap_GetPtr_FileName2(shaderHeap, fragfile); \
        if (!currentShader->shader) { \
            gX_TraceLog(gX_LOG_WARNING, "GUI_TEMPLATE: Shader not found: %s", fragfile); \
            shaderOK = false; \
        } \
    } \

 #define GUI_TEMPLATE_ELEMENT_SHADER_GET_LOCS \
    if (shaderOK) { \
        if (currentShader->shader) { \
            /*currentShader->locs.timeLoc =        gX_Shader_GetLocation(*(currentShader->shader),"time"); \
            currentShader->locs.resLoc =         gX_Shader_GetLocation(*(currentShader->shader),"res"); \
            currentShader->locs.rectLoc =        gX_Shader_GetLocation_Multi(*(currentShader->shader),2,"rect","box"); \
            currentShader->locs.colorLoc =       gX_Shader_GetLocation_Multi(*(currentShader->shader),2,"col","albedoColor"); \
            currentShader->locs.inputTexLoc =    gX_Shader_GetLocation_Multi(*(currentShader->shader),2,"texture0","tex0"); \
            currentShader->locs.uniformLoc =     gX_Shader_GetLocation(*(currentShader->shader),"uniforms"); \
            \
            currentShader->locs.camPosLoc =      gX_Shader_GetLocation(*(currentShader->shader),"cameraPos"); \
            currentShader->locs.camTargetLoc =   gX_Shader_GetLocation(*(currentShader->shader),"cameraTarget"); \
            currentShader->locs.camUpLoc =       gX_Shader_GetLocation(*(currentShader->shader),"cameraUp"); \
            currentShader->locs.camFovLoc =      gX_Shader_GetLocation(*(currentShader->shader),"cameraFov"); \
            \
            currentShader->locs.lightDirLoc =    gX_Shader_GetLocation(*(currentShader->shader),"lightDir"); \
            \
            currentShader->locs.backsideLoc =    gX_Shader_GetLocation(*(currentShader->shader),"backside"); \
            currentShader->locs.refIndexLoc =    gX_Shader_GetLocation(*(currentShader->shader),"refIndex"); */\
            gX_GUI_Element_Shader_Load(currentShader); \
        } \
        else { \
            gX_TraceLog(gX_LOG_WARNING,"GUI_TEMPLATE: No shader loaded"); \
        } \
    } \

#endif //GUI_TEMPLATES_MACROS_SHADER_H
