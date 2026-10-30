/**********************************************************************************************
*
*   [gX: src/gui/element/shader.c]
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

#include "../../main.h"

#include "../../wrappers/wrappers.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep (float-arg promotion hazard)
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"
#include "../../common/shader_compiler.h"

#include "../../gxmath/color.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/vector2.h"
#include "../../rhi/rhi.h"
#include "../../rhi/batch.h"


inline gX_I0 gX_GUI_Element_Shader_Init(gX_GUI_Element_Shader *shader) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing shader values");
    
    shader->loaded = false;
    shader->drawShader = true;
    shader->loops = 1;
    
    for (gX_U32 m = 0; m < 4; m++) {
        shader->uniforms[m].initial = (gX_F32v4){0.0f,0.0f,0.0f,0.0f};
        shader->uniforms[m].delta = (gX_F32v4){0.0f,0.0f,0.0f,0.0f};
    }
    
    //opencode: removed non-zero initial/delta defaults — they corrupt builder-set values.
    // The builder sets value + initial via Shader_Uniform(). Delta should remain zero
    // so that Uniforms_Set doesn't drift the value each frame.
    // Auto-animation via delta is opt-in (builder sets delta explicitly if needed).
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader);
static gX_I0 gX_GUI_Element_Shader_OnReload(gX_Shader *unused, gX_I0 *userData);
//opencode: NULL guard shader->shader before dereference
gX_I0 gX_GUI_Element_Shader_Load(gX_GUI_Element_Shader *shader) {
    if (!shader->shader) return;
    shader->locs.timeLoc =      gX_Shader_GetLocation(*(shader->shader),"time");
    shader->locs.resLoc =       gX_Shader_GetLocation(*(shader->shader),"res");
    shader->locs.rectLoc =      gX_Shader_GetLocation_Multi(*(shader->shader),2,"box","rect");
    shader->locs.colorLoc =     gX_Shader_GetLocation_Multi(*(shader->shader),2,"albedoColor","col");
    shader->locs.scaleLoc =     gX_Shader_GetLocation(*(shader->shader),"scale");
    //opencode: search "input" BEFORE "texture0"! raylib's default vertex shader
    //   always has a "texture0" sampler. Looking up "texture0" first would
    //   find the default vertex shader's sampler, not the custom fragment
    //   shader's input texture. raylib's batch flush then resets texture0
    //   to unit 0 (with the shapes texture), breaking the input binding.
    //   "inputTex" is the desktop-GLSL130 dialect rename of `input`.
    shader->locs.inputTexLoc =  gX_Shader_GetLocation_Multi(*(shader->shader),4,"input","inputTex","tex0","texture0");
    shader->locs.uniformLoc =   gX_Shader_GetLocation(*(shader->shader),"uniforms");
    
    //opencode: query the declared size of the `uniforms` array — uploading
    //   more vec4s than the shader declares is gX_RHI_INVALID_OPERATION.
    //   Scan active uniforms for the "uniforms" name; store size clamped 0..4.
    shader->uniformCount = 0;
    if (shader->locs.uniformLoc >= 0) {
        gX_I32 activeUniforms = 0;
        gX_RHI_GetProgramiv((gX_U32)shader->shader->id, gX_RHI_ACTIVE_UNIFORMS, &activeUniforms);
        for (gX_I32 ui = 0; ui < activeUniforms; ui++) {
            gX_I8 uname[64] = {0};
            gX_I32 usize = 0; gX_U32 utype = 0;
            gX_RHI_GetActiveUniform((gX_U32)shader->shader->id, (gX_U32)ui,
                uname, (gX_U32)sizeof(uname), &usize, &utype);
            if (strncmp(uname, "uniforms", 8) == 0) {
                shader->uniformCount = (gX_U32)(usize > 4 ? 4 : (usize > 0 ? usize : 1));
                break;
            }
        }
    }
    
    shader->locs.camPosLoc =    gX_Shader_GetLocation(*(shader->shader),"cameraPos");
    shader->locs.camTargetLoc = gX_Shader_GetLocation(*(shader->shader),"cameraTarget");
    shader->locs.camUpLoc =     gX_Shader_GetLocation(*(shader->shader),"cameraUp");
    shader->locs.camFovLoc =    gX_Shader_GetLocation(*(shader->shader),"cameraFov");
    
    shader->locs.ambientFactorLoc =  gX_Shader_GetLocation(*(shader->shader),"ambientFactor");
    shader->locs.colorMultiplierLoc =  gX_Shader_GetLocation(*(shader->shader),"colorMultiplier");
    shader->locs.lightDirLoc =  gX_Shader_GetLocation(*(shader->shader),"lightDir");
    
    shader->locs.backsideLoc =  gX_Shader_GetLocation(*(shader->shader),"backside");
    shader->locs.refIndexLoc =  gX_Shader_GetLocation(*(shader->shader),"refIndex");
    
    //opencode: matrix locs for 3D model draws with this element shader
    shader->locs.mvpLoc =       gX_Shader_GetLocation(*(shader->shader),"mvp");
    shader->locs.modelLoc =     gX_Shader_GetLocation(*(shader->shader),"matModel");
    shader->locs.normalLoc =    gX_Shader_GetLocation(*(shader->shader),"matNormal");
    shader->locs.viewLoc =      gX_Shader_GetLocation(*(shader->shader),"matView");
    shader->locs.projLoc =      gX_Shader_GetLocation(*(shader->shader),"matProjection");

    //opencode: reload ownership — the element does NOT own the resource shader
    //   watch. Calling Watch_Ex with NULL paths reused the owner's program slot
    //   and wiped its paths/mtimes/callback (audit gui_audit_2026-09 finding
    //   #5), and the transient module pointer had no matching unwatch. Instead:
    //   subscribe additively (deduped, lifetime-aware via Clear->Unsubscribe).
    //   The RM resource heap reloads shaders in place with a new program id
    //   that bypasses the watch table entirely — Draw's loadedProgramId check
    //   covers that path (self-healing), so a 0 return here is not an error.
    shader->loadedProgramId = shader->shader->id;
    if (shader->shader->id != 0) {
        gX_Shader_Compiler_Subscribe(*(shader->shader),
            gX_GUI_Element_Shader_OnReload,
            (gX_I0 *)shader);
    }
}

//opencode: hot-reload observer — re-query uniform locations after recompile
static gX_I0 gX_GUI_Element_Shader_OnReload(gX_Shader *unused, gX_I0 *userData) {
    (void)unused;
    gX_GUI_Element_Shader *shader = (gX_GUI_Element_Shader *)userData;
    gX_GUI_Element_Shader_Load(shader);
}

gX_I0 gX_GUI_Element_Shader_Uniforms_Init(gX_GUI_Element_Shader *shader) {
    for (gX_U32 m = 0; m < 4; m++) {
        shader->uniforms[m].value.x = shader->uniforms[m].initial.x;
        shader->uniforms[m].value.y = shader->uniforms[m].initial.y;
        shader->uniforms[m].value.z = shader->uniforms[m].initial.z;
        shader->uniforms[m].value.w = shader->uniforms[m].initial.w;
    }
}

//opencode: public — the model element uploads element uniforms through this
gX_I0 gX_GUI_Element_Shader_Uniforms_Set(gX_GUI_Element_Shader *shader) {
    if (!shader->shader) return;
    gX_Shader *sha = shader->shader;
    gX_F32v4 unif[4] = { 0 };
    gX_U32 count = (shader->uniformCount > 0) ? shader->uniformCount : 4;
    if (count > 4) count = 4;
    for (gX_U32 m = 0; m < count; m++) {
        shader->uniforms[m].value.x += shader->uniforms[m].delta.x;
        shader->uniforms[m].value.y += shader->uniforms[m].delta.y;
        shader->uniforms[m].value.z += shader->uniforms[m].delta.z;
        shader->uniforms[m].value.w += shader->uniforms[m].delta.w;
        unif[m] = shader->uniforms[m].value;
    }
    gX_Shader_SetValue_Array(*sha, shader->locs.uniformLoc, unif, gX_SHADER_UNIFORM_VEC4, count);
}

//opencode: public uniform upload — shared by the shader element's rect draw,
//   the shape element's geometry draw, and the texture element's quad draw.
//   Caller must have the shader enabled (BeginShader).
gX_I0 gX_GUI_Element_Shader_UploadUniforms(gX_GUI_Element_Shader *shader, gX_Rectangle box,
    gX_F32 time, gX_F32v2 res, gX_Color col, gX_F32v2 scale, gX_F32 ambientFactor) {
    gX_Shader *sha = shader->shader;
    if (!sha) return;
    gX_Shader_SetValue_Float(*sha, shader->locs.timeLoc, time);
    gX_Shader_SetValue_V2(*sha, shader->locs.resLoc, res);
    gX_Shader_SetValue_Rectangle(*sha, shader->locs.rectLoc, box);
    gX_Shader_SetValue_Color(*sha, shader->locs.colorLoc, col);
    gX_Shader_SetValue_V2(*sha, shader->locs.scaleLoc, scale);
    gX_Shader_SetValue_Float(*sha, shader->locs.ambientFactorLoc, ambientFactor);
    gX_GUI_Element_Shader_Uniforms_Set(shader);
}

//opencode: ALL uniforms are uploaded AFTER BeginShader so they target the correct GL program.
//   Fix: moved time, res, rect, color, scale, ambientFactor, and uniforms[] into this function.
static gX_I0 gX_GUI_Element_Shader_Draw_Shader(gX_GUI_Element_Shader *shader, gX_Rectangle box,
    gX_U32 iterations, gX_F32 time, gX_F32v2 res, gX_Color col, gX_F32v2 scale, gX_F32 ambientFactor) {
    gX_Shader *sha = shader->shader;
    if (!sha) return;

    //opencode: v0.2.0 — flush pending rlgl batch FIRST: the raw DrawScreenQuad
    //   below bypasses batching, so queued geometry would submit after the
    //   quad and paint completely over it.
    gX_Batch_Flush();
    gX_Drawing_BeginShader(*sha);
        gX_GUI_Element_Shader_UploadUniforms(shader, box, time, res, col, scale, ambientFactor);
        //opencode: EscapeMenu canvas reads gui.ping which is accumulated with
        //   all previously drawn canvases' content. For scenes that draw via
        //   GUI canvases (title, title2, etc.), this is the actual scene content
        //   visible behind the EscapeMenu.
        gX_Texture tex = (iterations % 2 == 0) ? gui.ping.rt.texture : gui.pong.rt.texture;
        //opencode: bind to dedicated texture slot 24 (was 7 — collides with
        //   material map unit 7 / cubemap).
        gX_Shader_SetValue_TextureSlot(*sha, shader->locs.inputTexLoc, tex, 24);
        //opencode: element shaders are 2D passes. A preceding renderer pass
        //   can leave culling, depth, or scissor enabled; that makes the quad
        //   silently disappear in buffered canvases even though the program
        //   compiled and all uniforms uploaded.
        gX_I1 cullWas = gX_RHI_IsEnabled(gX_RHI_CULL_FACE);
        gX_I1 depthWas = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
        gX_I1 scissorWas = gX_RHI_IsEnabled(gX_RHI_SCISSOR_TEST);
        gX_RHI_Disable(gX_RHI_CULL_FACE);
        gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        gX_RHI_Disable(gX_RHI_SCISSOR_TEST);
        {
            extern gX_I0 gX_RHI_Trace_ContextDump(const gX_I8 *);
            gX_RHI_Trace_ContextDump("pre-DrawScreenQuad");
        }
        gX_Drawing_DrawScreenQuad(box.x, box.y, box.width, box.height);
        if (scissorWas) gX_RHI_Enable(gX_RHI_SCISSOR_TEST);
        if (depthWas) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        if (cullWas) gX_RHI_Enable(gX_RHI_CULL_FACE);
    gX_Drawing_EndShader();
}

//opencode: fixed uniform upload ordering — shader must be enabled before any SetValue calls.
//   Moved all gX_Shader_SetValue* calls into Draw_Shader (which calls BeginShader first).
//   Resolves lens (uniforms defaulting to 0) and blur (radius defaulting to 0) bugs.
static gX_I0 gX_GUI_Element_Shader_Draw(gX_GUI_Element_Shader *shader, gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    
    //opencode: NULL guard shader->shader before dereference
    //opencode: reload ownership — the RM resource heap reloads shaders in
    //   place with a new program id (resource.c mtime reload bypasses the
    //   watch table), so also re-Load when the live program changed since the
    //   locs were queried. Uniforms are NOT re-initialized on reload — only
    //   the first load resets value=initial (preserves delta animation).
    if (shader->shader && (!shader->loaded || shader->loadedProgramId != shader->shader->id)) {
        gX_GUI_Element_Shader_Load(shader);
        if (!shader->loaded) gX_GUI_Element_Shader_Uniforms_Init(shader);
        shader->loaded = true;
    }
    
    if (!shader->shader) return;
    gX_Rectangle box = element->rect;
    gX_Color col = gX_Color_Mul(element->color,canvas->canvasHue);
    
    //opencode: Uniforms_Init moved into the Load path — calling it every Draw
    //   reset value=initial each frame, so delta-based auto-animation never
    //   accumulated (Uniforms_Set's value += delta had no lasting effect).
    
    //opencode: resolve resolution BEFORE the loop (doesn't change per-iteration).
    //   When the canvas has a buffer, all draws go to that RT → use buffer size.
    //   When the canvas has NO buffer, elements draw directly to the current FB
    //   (which may be the window at 2560×1920, not the render size 1024×768).
    //   Using screenWidth/Height here would make gl_FragCoord.xy/res UV > 1.0,
    //   causing the "low-left corner stretched" bug. Query the actual FB size
    //   via rlgl for the bufferless case.
    gX_F32 resW, resH;
    if (canvas->buffer.loadingState.loaded) {
        resW = (gX_F32)canvas->buffer.width;
        resH = (gX_F32)canvas->buffer.height;
    } else {
        //opencode: query the ACTUAL framebuffer size via gX_RHI_VIEWPORT — the
        //   old code used maindata.renderWidth even though no-buffer elements
        //   draw at window size (gl_FragCoord/res UV > 1.0 → stretched).
        gX_I32 vp[4] = {0, 0, 0, 0};
        gX_RHI_GetIntegerv(gX_RHI_VIEWPORT, vp);
        if (vp[2] > 0 && vp[3] > 0) {
            resW = (gX_F32)vp[2];
            resH = (gX_F32)vp[3];
        } else {
            resW = (gX_F32)maindata.renderWidth;
            resH = (gX_F32)maindata.renderHeight;
        }
    }
    gX_F32v2 fsRes = {resW, resH};
    gX_F32 time = (gX_F32)(maindata.timeElapsed);
    gX_F32v2 scale = element->params.scale;
    gX_F32 ambientFactor = canvas->camera.ambientFactor;
    
    //opencode: ping-pong loop — alternate writes with FBO save/restore for nesting
    //opencode: restore canvas buffer state (FBO + viewport + ortho) after each
    //   intermediate ping-pong write. A raw rlEnableFramebuffer alone leaves the
    //   viewport and ortho from EndTexture's screen restore, causing subsequent
    //   element draws to render at screen coordinates into the canvas buffer.
    //opencode: guard — without ping/pong RTs the feedback loop can't run;
    //   fall back to a single pass drawing straight to the canvas/screen.
    gX_U32 loops = shader->loops;
    if (loops > 1 && (!gui.ping.loadingState.loaded || !gui.pong.loadingState.loaded)) {
        loops = 1;
    }
    gX_U32 iterations = 0;
    for (gX_U32 k = 0; k < loops; k++) {
        if (!shader->drawShader) continue;
        
        if (k < loops - 1) {
            if (k % 2 == 0) gX_Drawing_BeginTexture(gui.pong.rt);
            else            gX_Drawing_BeginTexture(gui.ping.rt);
        }
        
        gX_GUI_Element_Shader_Draw_Shader(shader, box, iterations,
            time, fsRes, col, scale, ambientFactor);
        
        if (k < loops - 1) {
            gX_Drawing_EndTexture();
            //opencode: restore canvas buffer (FBO, viewport, ortho) via BeginTexture
            //   instead of raw rlEnableFramebuffer. BeginTexture sets the correct
            //   viewport and ortho projection for the canvas buffer dimensions,
            //   which EndTexture's screen restore may have overwritten.
            if (canvas->buffer.loadingState.loaded) {
                gX_Drawing_BeginTexture(canvas->buffer.rt);
            }
        }
        
        iterations++;
    }
    
}

//opencode: lifetime-aware clear — drop the reload observer BEFORE zeroing so
//   the watch table never retains a pointer into reused module memory. The
//   REMOVE/CLEARMODULE macros funnel every removal through this function.
gX_I0 gX_GUI_Element_Shader_Clear(gX_GUI_Element_Shader *shader) {
    if (shader && shader->shader && shader->shader->id != 0) {
        gX_Shader_Compiler_Unsubscribe(*(shader->shader),
            gX_GUI_Element_Shader_OnReload,
            (gX_I0 *)shader);
    }
    *shader = (gX_GUI_Element_Shader){ 0 };
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(   Shader,shader)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(   Shader,shader,SHADER)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(Shader,shader)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE( Shader,shader)
//opencode: inlined from LOOP_CANVAS macro for navigability
gX_I0 gX_GUI_Element_Draw_Shaders(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw_Shaders(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    gX_Heap heapStruct = element->shaders;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Shader *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Shader *elementmodule = &heapPtr[i];
            gX_GUI_Element_Shader_Draw(elementmodule,element,canvas);
        }
    }
}
