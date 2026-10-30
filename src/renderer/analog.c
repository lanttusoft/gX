/**********************************************************************************************
*
*   [gX: src/renderer/analog.c]
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


//opencode: analog CRT renderer — 3-system mode dispatcher
//   Modes: NTSC_PAL (CPU EMMIR), LEGACY (analog.frag GPU), VHS (tape/machine/format GPU)
#include "analog.h"

#include <stdlib.h>
#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"
#include "../common/path.h"
#include "../input.h"
#include "../main.h"
#include "../settings.h"
#include "../resource/resource.h"
#include "../wrappers/wrappers.h"

#include "renderer.h"
#include "../rhi/rhi.h"

gX_I32 gX_RendererAnalog_Load(gX_RendererAnalog *analog) {
    gX_TRACELOG_PROLOGUE("RendererAnalog_Load","RENDERER_ANALOG: Loading")
    strcpy(analog->name, "[gX] Analog");
    strcpy(analog->description, "analog CRT simulation — 3 modes: NTSC/PAL, analog.frag, VHS");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", analog->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", analog->description);

    //opencode: defaults
    analog->mode = GX_ANALOG_MODE_LEGACY; // start with GPU shader (fastest)
    analog->reducedMotion = settings.reduceMotion != 0;
    analog->hasInputTexture = 0;
    analog->inputTexture = (gX_Texture){0};
    analog->legacyShaderLoaded = 0;
    analog->vhsShaderLoaded = 0;

    //opencode: CPU-side input image for NTSC/PAL path and GPU path default
    analog->inputImg = gX_Image_Load(GX_PATH("texture", "misc/testcard.png"));

    //opencode: init NTSC/PAL sub-module
    analog->ntscPal = GX_ANALOG_NTSCPAL_DEFAULTS;

    //opencode: init legacy shader state
    analog->legacyShaderState = GX_ANALOG_LEGACY_SHADER_DEFAULTS;

    //opencode: init VHS sub-module
    analog->vhsState = GX_ANALOG_VHS_DEFAULTS;

    //opencode: deferred GPU loading
    analog->loadingstate = (gX_LoadingState){0};
    analog->loadingstate.loadAsap = true;

    //opencode: apply --analog-mode CLI argument if set
    if (arguments.hasAnalogMode) {
        gX_RendererAnalog_SetMode(analog, arguments.analogMode);
        gX_TraceLog_RENDERER(gX_LOG_GREEN, "ANALOG: mode set from CLI to %s", gX_RendererAnalog_ModeString(analog->mode));
    }

    gX_TRACELOG_EPILOGUE("RENDERER_ANALOG: Load done")
    return 0;
}

gX_I32 gX_RendererAnalog_Unload(gX_RendererAnalog *analog) {
    gX_TRACELOG_PROLOGUE("RendererAnalog_Unload","RENDERER_ANALOG: Unloading")

    //opencode: free NTSC/PAL CPU resources
    gX_Analog_NTSCPAL_Uninit(&analog->ntscPal);

    //opencode: unload shaders
    if (analog->legacyShaderLoaded && analog->legacyShader.id > 0) {
        gX_Shader_Unload(analog->legacyShader);
    }
    if (analog->vhsShaderLoaded && analog->vhsShader.id > 0) {
        gX_Shader_Unload(analog->vhsShader);
    }

    //opencode: free textures
    if (analog->hasInputTexture && analog->inputTexture.id > 0) {
        gX_Texture_Unload(analog->inputTexture);
    }
    gX_Image_Unload(analog->inputImg);

    analog->legacyShaderLoaded = 0;
    analog->vhsShaderLoaded = 0;
    analog->hasInputTexture = 0;

    gX_TRACELOG_EPILOGUE("RENDERER_ANALOG: Unload done")
    return 0;
}

//opencode: deferred GPU load — loads default input texture + shaders
static gX_I0 analog_gpu_load(gX_I0 *arg) {
    gX_RendererAnalog *analog = (gX_RendererAnalog*)arg;

    //opencode: load default input texture if no custom input and not yet loaded
    if (!analog->hasInputTexture && analog->inputTexture.id == 0) {
        { gX_Texture *_rsct = gX_Resource_LoadTexture("misc/testcard.png"); if (_rsct) analog->inputTexture = *_rsct; }
    }

    //opencode: load legacy shader (analog.frag)
    if (!analog->legacyShaderLoaded) {
        analog->legacyShader = gX_Shader_Load_Ex(
            NULL,
            GX_PATH("shader", "analog.frag"),
            NULL);
        if (analog->legacyShader.id > 0) {
            gX_Analog_LegacyShader_LoadLocations(&analog->legacyShaderState, analog->legacyShader);
            analog->legacyShaderLoaded = 1;
            gX_TraceLog_RENDERER(gX_LOG_GREEN, "ANALOG: loaded analog.frag (id=%u)", analog->legacyShader.id);
        } else {
            gX_TraceLog_RENDERER(gX_LOG_WARNING, "ANALOG: failed to load analog.frag");
        }
    }

    //opencode: load VHS shader (vhs.frag)
    if (!analog->vhsShaderLoaded) {
        analog->vhsShader = gX_Shader_Load_Ex(
            NULL,
            GX_PATH("shader", "vhs.frag"),
            NULL);
        if (analog->vhsShader.id > 0) {
            gX_Analog_VHS_LoadLocations(&analog->vhsState, analog->vhsShader);
            analog->vhsShaderLoaded = 1;
            gX_TraceLog_RENDERER(gX_LOG_GREEN, "ANALOG: loaded vhs.frag (id=%u)", analog->vhsShader.id);
        } else {
            gX_TraceLog_RENDERER(gX_LOG_WARNING, "ANALOG: failed to load vhs.frag (expected if not yet written)");
        }
    }

    analog->loadingstate.loaded = true;
}

gX_I0 gX_RendererAnalog_Update(gX_RendererAnalog *analog) {
    //opencode: deferred GPU load
    gX_LoadingState_Handler(&analog->loadingstate, analog_gpu_load, 0, analog);
    if (!analog->loadingstate.loaded) return;

    //ai: live accessibility preference — the settings UI writes the same
    //   field that drives gui.style.a11y.motionReduced.
    analog->reducedMotion = settings.reduceMotion != 0;

    //opencode: key bindings — T/G adjust noise in NTSC/PAL mode only
    if (analog->mode == GX_ANALOG_MODE_NTSC_PAL) {
        if (gX_Key_IsDown(gX_KEY_T)) analog->ntscPal.noiseNTSC++;
        if (gX_Key_IsDown(gX_KEY_G)) analog->ntscPal.noiseNTSC--;
        if (analog->ntscPal.noiseNTSC < 0) analog->ntscPal.noiseNTSC = 0;
        if (analog->ntscPal.noiseNTSC > 255) analog->ntscPal.noiseNTSC = 255;

        //opencode: process NTSC/PAL frame (synchronous, ~28ms at 640x480)
        if (analog->inputImg.data) {
            gX_Analog_NTSCPAL_Update(
                &analog->ntscPal,
                (const gX_U32*)analog->inputImg.data,
                analog->inputImg.width,
                analog->inputImg.height);
        }
    }

    //opencode: LEGACY and VHS modes have no CPU update work
}

gX_I0 gX_RendererAnalog_DrawTo(gX_RendererAnalog *analog, gX_RenderTexture *outputRT) {
    if (!analog->loadingstate.loaded) return;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG((gX_Color){0,0,0,255});

        if (analog->reducedMotion) {
            //ai: reduced effects — show the clean input instead of the
            //   time-driven CRT/VHS/static path (the glitch itself is removed,
            //   not merely slowed).
            gX_Texture_Draw(analog->inputTexture, (gX_Texture_Draw_Params){
                .dst = (gX_Rectangle){ 0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight },
                .scale = {1, 1},
                .tint = gX_WHITE
            });
            gX_Drawing_EndTexture();
            return;
        }

        switch (analog->mode) {
            case GX_ANALOG_MODE_NTSC_PAL: {
                //opencode: CPU NTSC/PAL path — draw processed output texture
                gX_Analog_NTSCPAL_Draw(&analog->ntscPal);
                break;
            }

            case GX_ANALOG_MODE_LEGACY: {
                //opencode: GPU analog.frag path
                if (!analog->legacyShaderLoaded || analog->legacyShader.id == 0) {
                    // fallback: draw raw input texture
                    gX_Texture_Draw(analog->inputTexture, (gX_Texture_Draw_Params){
                        .dst = (gX_Rectangle){ 0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight },
                        .scale = {1, 1},
                        .tint = gX_WHITE
                    });
                    break;
                }

                gX_Drawing_BeginShader(analog->legacyShader);

                    //opencode: bind input texture to tex0
                    if (analog->legacyShaderState.locations.inputTexture >= 0) {
                        gX_Shader_SetValue_Texture(analog->legacyShader,
                            analog->legacyShaderState.locations.inputTexture,
                            analog->inputTexture);
                    }

                    //opencode: upload uniforms
                    gX_F32 time = (gX_F32)maindata.timeElapsed;
                    gX_F32 res[2] = { (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight };
                    gX_F32 uniforms[10] = {0};
                    uniforms[0] = (gX_F32)analog->ntscPal.noiseNTSC;  // noise
                    uniforms[1] = analog->ntscPal.colorNTSC ? 1.0f : 0.0f;
                    gX_Analog_LegacyShader_UploadUniforms(
                        &analog->legacyShaderState,
                        analog->legacyShader, time, res, uniforms);

                    //opencode: fullscreen quad
                    gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
                gX_Drawing_EndShader();
                break;
            }

            case GX_ANALOG_MODE_VHS: {
                //opencode: GPU VHS path
                if (!analog->vhsShaderLoaded || analog->vhsShader.id == 0) {
                    // fallback: draw raw input texture
                    gX_Texture_Draw(analog->inputTexture, (gX_Texture_Draw_Params){
                        .dst = (gX_Rectangle){ 0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight },
                        .scale = {1, 1},
                        .tint = gX_WHITE
                    });
                    break;
                }

                gX_Drawing_BeginShader(analog->vhsShader);

                    //opencode: bind input texture to tex0
                    if (analog->vhsState.locations.inputTexture >= 0) {
                        gX_Shader_SetValue_Texture(analog->vhsShader,
                            analog->vhsState.locations.inputTexture,
                            analog->inputTexture);
                    }

                    //opencode: upload VHS uniforms
                    gX_F32 time = (gX_F32)maindata.timeElapsed;
                    gX_F32 res[2] = { (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight };
                    gX_Analog_VHS_UploadUniforms(&analog->vhsState, analog->vhsShader, time, res);

                    //opencode: fullscreen quad
                    gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
                gX_Drawing_EndShader();
                break;
            }

            default: {
                //opencode: unknown mode — draw raw testcard
                gX_Texture_Draw(analog->inputTexture, (gX_Texture_Draw_Params){
                    .dst = (gX_Rectangle){ 0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight },
                    .scale = {1, 1},
                    .tint = gX_WHITE
                });
                break;
            }
        }

    gX_Drawing_EndTexture();
}

gX_I0 gX_RendererAnalog_SetInput(gX_RendererAnalog *analog, gX_Texture texture) {
    if (texture.id == 0) {
        //opencode: reset to default testcard
        analog->hasInputTexture = 0;
        if (analog->inputTexture.id > 0) {
            gX_Texture_Unload(analog->inputTexture);
            analog->inputTexture = (gX_Texture){0};
        }
        return;
    }

    //opencode: replace existing input texture
    if (analog->hasInputTexture && analog->inputTexture.id > 0) {
        gX_Texture_Unload(analog->inputTexture);
    }
    analog->inputTexture = texture;
    analog->hasInputTexture = 1;
}

gX_I0 gX_RendererAnalog_SetMode(gX_RendererAnalog *analog, gX_I32 mode) {
    if (mode >= GX_ANALOG_MODE_NTSC_PAL && mode <= GX_ANALOG_MODE_VHS) {
        analog->mode = mode;
    }
}

const gX_I8 *gX_RendererAnalog_ModeString(gX_I32 mode) {
    switch (mode) {
        case GX_ANALOG_MODE_NTSC_PAL: return "NTSC/PAL (CPU)";
        case GX_ANALOG_MODE_LEGACY:   return "Legacy analog.frag (GPU)";
        case GX_ANALOG_MODE_VHS:      return "VHS (GPU)";
        default:                      return "Unknown";
    }
}
