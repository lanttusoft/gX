/**********************************************************************************************
*
*   [gX: src/gui/element/model.c]
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
#include "../canvas.h"
#include "../gui.h"
#include "modules.h"

#include "../../wrappers/wrappers.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"

#include "../../gxmath/gxmath.h"

#include "../../main.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_fbo.h"

inline gX_I0 gX_GUI_Element_Model_Init(gX_GUI_Element_Model *model) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing model values");
    
    model->extraparams.colorMultiplier = 1.0f;
    model->extraparams.refIndex = 1.1f;
    
    model->transform = gX_Matrix_Identity();
    
    model->paramRotate = gX_F32v3_Zero();
    model->paramTranslate = gX_F32v3_Zero();
    model->paramScale = gX_F32v3_One();
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Model_Draw(gX_GUI_Element_Model *model, gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    //opencode: guard against NULL model pointer — skip draw if unset
    if (!model->model) {
        gX_TraceLog_GUI(gX_LOG_WARNING, "Model element with no model assigned — skipping draw");
        return;
    }
    
    gX_Color col = gX_Color_Mul(element->color,canvas->canvasHue);
    
    gX_Model modell = *(model->model);
    modell.transform = model->transform;
    
    gX_Model_Parameters params = { 0 };
    //opencode: position/size in canvas-BUFFER coordinates when a buffer is
    //   active (was screenWidth/Height — wrong in buffer mode when the
    //   window size differs from the buffer size)
    gX_F32 posW = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.width  : (gX_F32)maindata.screenWidth;
    gX_F32 posH = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.height : (gX_F32)maindata.screenHeight;
    params.position.x = -(element->rect.x/posW)+0.5f;
    params.position.y = -(element->rect.y/posH)+0.5f;
    params.scale = 1.0f;
    params.color = col;
    
    //opencode: shader element reference — the shader ELEMENT owns the
    //   program, uniform locations, uniforms, and hot-reload. The old
    //   model->shader + model->locs were never populated (all locs 0 →
    //   every SetValue wrote to GL location 0) and the shader was disabled
    //   before the draw anyway.
    gX_GUI_Element_Shader *elShader = NULL;
    if (model->useShader && element->shaders.data && model->shaderIndex < element->shaders.amount) {
        elShader = &((gX_GUI_Element_Shader*)element->shaders.data)[model->shaderIndex];
        if (!elShader->shader) elShader = NULL;
    }
    
    if (model->useParameters) {
        gX_Matrix mat = gX_Matrix_Identity();
        gX_F32v3 rotV = gX_F32v3_Scale(model->paramRotate,2.0f*gX_PI);
        gX_Matrix rot = gX_Matrix_RotateXYZ((gX_F32v3){rotV.x,rotV.y,rotV.z});
        gX_Matrix tra = gX_Matrix_Translate(model->paramTranslate.x,
                                            model->paramTranslate.y,
                                            model->paramTranslate.z);
        gX_Matrix sca = gX_Matrix_Scale(model->paramScale.x,
                                        model->paramScale.y,
                                        model->paramScale.z);
        //opencode: standard TRS convention: T × R × S (translate world, rotate,
        //   scale local). Do NOT use S × R × T — that scales the translation too,
        //   causing paramTranslate to move with paramScale changes.
        mat = gX_Matrix_Multiply(mat,tra);
        mat = gX_Matrix_Multiply(mat,rot);
        mat = gX_Matrix_Multiply(mat,sca);
        modell.transform = mat;
        model->transform = modell.transform;
    }
    
    gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
    gX_RHI_Disable(gX_RHI_CULL_FACE);

    //opencode: dual-pass glass — back faces rendered to gui.ping, front faces read from it.
    // When doGlass is false, renders a single pass as before.
    gX_U32 passes = model->extraparams.doGlass ? 2 : 1;
    gX_U32 saveFbo = 0;

    for (gX_U32 i = 0; i < passes; i++) {
        gX_I32 backside = (i == 0 && model->extraparams.doGlass) ? 1 : 0;
        gX_F32 refindex = (i == 0)
            ? model->extraparams.refIndex
            : (1.0f / model->extraparams.refIndex);

        // Pass 0 (back faces): redirect to gui.ping for double-pass glass
        if (i == 0 && model->extraparams.doGlass && gui.ping.loadingState.loaded) {
            saveFbo = canvas->buffer.rt.id;
            gX_Drawing_BeginTexture(gui.ping.rt);
                gX_Drawing_ClearBG((gX_Color){0,0,0,0});
            gX_Drawing_EndTexture();
            gX_Drawing_BeginTexture(gui.ping.rt);
        }

        gX_Drawing_Begin3D(canvas->camera.camera3D);
        
        if (elShader) {
            if (!elShader->loaded) {
                gX_GUI_Element_Shader_Load(elShader);
                gX_GUI_Element_Shader_Uniforms_Init(elShader);
                elShader->loaded = true;
            }
            //opencode: shader stays ACTIVE across the model draw — the
            //   WithShader wrapper uses the same program instead of the
            //   material shader (raylib_Mesh_Draw would re-enable its own).
            gX_Shader *sh = elShader->shader;
            gX_Drawing_BeginShader(*sh);
                gX_Shader_SetValue_Float(*sh, elShader->locs.timeLoc, (gX_F32)(maindata.timeElapsed));
                gX_Shader_SetValue_V2(   *sh, elShader->locs.resLoc, (gX_F32v2){posW, posH});
                gX_Shader_SetValue_Color(*sh, elShader->locs.colorLoc, col);
                gX_Shader_SetValue_V3(   *sh, elShader->locs.camPosLoc,    canvas->camera.camera3D.position);
                gX_Shader_SetValue_V3(   *sh, elShader->locs.camTargetLoc, canvas->camera.camera3D.target);
                gX_Shader_SetValue_V3(   *sh, elShader->locs.camUpLoc,     canvas->camera.camera3D.up);
                gX_Shader_SetValue_Float(*sh, elShader->locs.camFovLoc,    canvas->camera.camera3D.fovy);
                gX_Shader_SetValue_Float(*sh, elShader->locs.ambientFactorLoc, canvas->camera.ambientFactor);
                gX_Shader_SetValue_V3(   *sh, elShader->locs.lightDirLoc,  canvas->camera.lightDir);
                gX_GUI_Element_Shader_Uniforms_Set(elShader);
                gX_Shader_SetValue_Int(  *sh, elShader->locs.backsideLoc, backside);
                gX_Shader_SetValue_Float(*sh, elShader->locs.colorMultiplierLoc, model->extraparams.colorMultiplier);
                gX_Shader_SetValue_Float(*sh, elShader->locs.refIndexLoc, refindex);
                //opencode: glass input — dedicated slot 24; the WithShader
                //   diffuse sampler binding (locs[5]) is skipped for glass so
                //   it doesn't override this
                if (model->extraparams.doGlass) {
                    gX_Texture bgTex;
                    if (i == 1 && gui.ping.loadingState.loaded) {
                        bgTex = gui.ping.rt.texture;
                    } else if (gui.composite.background.loadingState.loaded) {
                        bgTex = gui.composite.background.rt.texture;
                    } else {
                        bgTex = gui.buffer.rt.texture;
                    }
                    gX_Shader_SetValue_TextureSlot(*sh, elShader->locs.inputTexLoc, bgTex, 24);
                }
                gX_I32 mlocs[6] = {
                    elShader->locs.mvpLoc, elShader->locs.modelLoc,
                    elShader->locs.normalLoc, elShader->locs.viewLoc,
                    elShader->locs.projLoc,
                    (model->extraparams.doGlass) ? -1 : elShader->locs.inputTexLoc
                };
                gX_Model_Draw_Parameters_WithShader(modell, params, (gX_U32)sh->id, mlocs);
            gX_Drawing_EndShader();
        } else {
            gX_Model_Draw_Parameters(modell,params);
        }
        
        gX_Drawing_End3D();

        // End pass 0 (gui.ping) and restore canvas buffer FBO for pass 1
        if (i == 0 && model->extraparams.doGlass && saveFbo > 0) {
            gX_Drawing_EndTexture();
            gX_RHI_BindFBO((gX_U32)saveFbo);
            //opencode: restore the canvas buffer VIEWPORT — EndTexture left
            //   the screen viewport active and Begin3D doesn't set one, so
            //   pass 1 previously rendered into the canvas buffer with the
            //   screen viewport (only correct when window == buffer size).
            gX_RHI_Viewport(0, 0,
                (gX_I32)canvas->buffer.width, (gX_I32)canvas->buffer.height);
        }
    }

    if (cullWas) gX_RHI_Enable(gX_RHI_CULL_FACE);
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Model,model)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Model,model)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Model,model,MODEL)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Model,model)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Model,model)
//gX_GUI_ELEMENT_MODULE_FUNCDEF_LOOP(  Model,model,Draw)

gX_I0 gX_GUI_Element_Draw_Models(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw_Models(gX_GUI_Element *element, gX_GUI_Canvas *canvas) { \
     \
    gX_Heap heapStruct = element->models; \
     \
    if (heapStruct.amount > 0) { \
        gX_GUI_Element_Model *heapPtr = heapStruct.data; \
         \
        for (gX_U32 i = 0; i < heapStruct.amount; i++) { \
            gX_GUI_Element_Model *elementmodule = &heapPtr[i]; \
            gX_GUI_Element_Model_Draw(elementmodule,element,canvas); \
        } \
    } \
}
