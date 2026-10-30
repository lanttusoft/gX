/**********************************************************************************************
*
*   [gX: src/gui/element/texture.c]
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


#include <stdlib.h>
#include <string.h>

#include "element.h"
#include "modules.h"

#include "../../wrappers/wrappers.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_enums.h"
#include "../../rhi/batch.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../gxmath/gxmath.h"
#include "../../main.h"

extern gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader);

inline gX_I0 gX_GUI_Element_Texture_Init(gX_GUI_Element_Texture *texture) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing texture values");
    
    texture->texture = 0x0;
    
    texture->params.scale = (gX_F32v2){1.0f,1.0f};
    texture->params.origin = (gX_F32v2){0.0f,0.0f};
    texture->params.rotation = 0.0f;
    texture->params.tint = gX_WHITE;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

static gX_I0 gX_GUI_Element_Texture_Draw(gX_GUI_Element_Texture *texture, gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    //opencode: guard against NULL texture pointer — skip draw if unset
    if (!texture->texture) {
        gX_TraceLog_GUI(gX_LOG_WARNING, "Texture element with no texture assigned — skipping draw");
        return;
    }
    
    //opencode: start from element defaults, then apply overrides.
    //   Uses new gX_Texture_Draw_Params with center-origin convention.
    //   RT sources get auto-invert from the backend. File textures display
    //   correctly with default scale={1,1} (no flip needed).
    gX_Texture_Draw_Params params = (gX_Texture_Draw_Params){ 0 };
    //ai: audit-only probe switch — force the plain texture branch for
    //    headless capture comparisons.
    gX_I1 forcePlain = getenv("GX_GUI_TEXTURE_FORCE_PLAIN") != NULL;
    
    params.dst = element->rect;
    params.scale = element->params.scale;
    params.rotation = element->params.angle;
    params.tint = gX_Color_Mul(element->color,canvas->canvasHue);
    
    //custom params (if active, replaces all the above defaults)
    if (element->stateRendering.TEXTURE_CUSTOM_PARAMS) {
        params = texture->params;
        params.dst = element->rect; //safety: rect must come from element
    } else {
        //opencode: element rotation/tint only apply without custom params —
        //   the old unconditional overwrite silently killed custom rotation
        //   and tint stored in texture->params.
        params.rotation = element->params.angle;
        params.tint = gX_Color_Mul(element->color,canvas->canvasHue);
    }
    
    //opencode: TEXTURE_ALT_METHOD, TEXTURE_INVERT_X, TEXTURE_INVERT_Y
    //   removed — alt method folded into backend, invertX dead, invertY
    //   auto-handled by RT detection. Negative scale.x/y for manual flip
    //   is set via params.scale if needed.
    
    gX_I1 matrixPushed = false;
    if (element->stateRendering.TEXTURE_3D) {
        gX_Drawing_Begin3D(canvas->camera.camera3D);
        
        if (element->stateRendering.USE_MATRIX) {
            gX_RHI_Matrix_Push();
            gX_Matrix mat = element->params.matrix;
            gX_F32a16 arr = gX_Matrix_To_F32a16(mat);
            gX_RHI_Matrix_Mult(arr.v);
            matrixPushed = true;
        }
        
        //opencode: bounds check shaderIndex, NULL check heap+shader
        if (!forcePlain && texture->useShader && element->shaders.data
            && texture->shaderIndex < element->shaders.amount) {
            gX_GUI_Element_Shader *shaders = element->shaders.data;
            gX_GUI_Element_Shader *elShader = &shaders[texture->shaderIndex];
            if (elShader->shader) {
                gX_Drawing_BeginShader(*(elShader->shader));
                if (!elShader->loaded) {
                    gX_GUI_Element_Shader_Load(elShader);
                    elShader->loaded = true;
                }
                if (elShader->locs.inputTexLoc != -1) {
                    gX_Shader_SetValue_TextureSlot(*elShader->shader, elShader->locs.inputTexLoc, *texture->texture, 0);
                }
            }
        }
        
        gX_Texture_Draw3D_Params(*texture->texture,params,(gX_F32v3){element->rect.x,element->rect.y,0.0f});
        
        //opencode: bounds check shaderIndex, NULL check heap+shader
        if (!forcePlain && texture->useShader && element->shaders.data
            && texture->shaderIndex < element->shaders.amount) {
            gX_GUI_Element_Shader *shaders = element->shaders.data;
            gX_GUI_Element_Shader *elShader = &shaders[texture->shaderIndex];
            if (elShader->shader) {
                gX_Drawing_EndShader();
            }
        }
        
        if (matrixPushed) {
            gX_RHI_Matrix_Pop();
        }
        
        gX_Drawing_End3D();
    }
    else {
        if (element->stateRendering.USE_MATRIX) {
            gX_RHI_Matrix_Push();
            gX_Matrix mat = element->params.matrix;
            gX_F32a16 arr = gX_Matrix_To_F32a16(mat);
            gX_RHI_Matrix_Mult(arr.v);
            matrixPushed = true;
        }
        
        //opencode: bounds check shaderIndex, NULL check heap+shader
        gX_GUI_Element_Shader *elShader = NULL;
        if (texture->useShader && element->shaders.data && texture->shaderIndex < element->shaders.amount) {
            elShader = &((gX_GUI_Element_Shader*)element->shaders.data)[texture->shaderIndex];
            if (!elShader->shader) elShader = NULL;
        }
        
        if (elShader && !forcePlain) {
            //opencode: shader path — direct quad draw with the element shader.
            //   The old path wrapped gX_Texture_Draw in BeginShader/EndShader,
            //   but the batch flush ALWAYS switches to its own program, so the
            //   custom shader never rendered the texture (silent no-op).
            if (!elShader->loaded) {
                gX_GUI_Element_Shader_Load(elShader);
                gX_GUI_Element_Shader_Uniforms_Init(elShader);
                elShader->loaded = true;
            }
            gX_F32 resW = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.width  : (gX_F32)maindata.renderWidth;
            gX_F32 resH = canvas->buffer.loadingState.loaded ? (gX_F32)canvas->buffer.height : (gX_F32)maindata.renderHeight;
            gX_Shader *sh = elShader->shader;
            gX_Drawing_BeginShader(*sh);
                gX_GUI_Element_Shader_UploadUniforms(elShader, element->rect,
                    (gX_F32)(maindata.timeElapsed), (gX_F32v2){resW, resH},
                    gX_Color_Mul(element->color, canvas->canvasHue),
                    element->params.scale, canvas->camera.ambientFactor);
                gX_Shader_SetValue_TextureSlot(*sh, elShader->locs.inputTexLoc, *texture->texture, 24);
                gX_Drawing_DrawScreenQuad(element->rect.x, element->rect.y,
                    element->rect.width, element->rect.height);
            gX_Drawing_EndShader();
        } else {
            //ai: audit-only fixture — recreate the visible-rect case in front of
            //    the plain texture so the probe catches the queued-rect order.
            if (getenv("GX_GUI_TEXTURE_PROBE_RECT"))
                gX_Rectangle_Draw(element->rect, gX_WHITE);
            gX_I1 depthWas = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
            gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
            //ai: the rect fill above is a raylib-native queued draw. Flush it
            //    first or it lands after the immediate gX batch and paints over
            //    the texture (the blank plain-texture bug); then disable the
            //    2D-incompatible depth/cull tests for the quad.
            gX_Drawing_Flush();
            gX_RHI_Disable(gX_RHI_DEPTH_TEST);
            gX_RHI_Disable(gX_RHI_CULL_FACE);
            gX_Texture_Draw(*texture->texture,params);
            if (cullWas) gX_RHI_Enable(gX_RHI_CULL_FACE);
            if (depthWas) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        }
        
        if (matrixPushed) {
            gX_RHI_Matrix_Pop();
        }
    }
    
    
    
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR( Texture,texture)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Texture,texture)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Texture,texture,TEXTURE)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Texture,texture)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Texture,texture)
//gX_GUI_ELEMENT_MODULE_FUNCDEF_LOOP(  Texture,texture,Draw)

gX_I0 gX_GUI_Element_Draw_Textures(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw_Textures(gX_GUI_Element *element, gX_GUI_Canvas *canvas) { \
     \
    gX_Heap heapStruct = element->textures; \
     \
    if (heapStruct.amount > 0) { \
        gX_GUI_Element_Texture *heapPtr = heapStruct.data; \
         \
        for (gX_U32 i = 0; i < heapStruct.amount; i++) { \
            gX_GUI_Element_Texture *elementmodule = &heapPtr[i]; \
            gX_GUI_Element_Texture_Draw(elementmodule,element,canvas); \
        } \
    } \
}
