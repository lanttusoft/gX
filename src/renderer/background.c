/**********************************************************************************************
*
*   [gX: src/renderer/background.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


#include <math.h>
#include <stdlib.h>

#include "background.h"
#include "background/schema.h"

#include "renderer.h"

#include "../common/enum.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"

#include "../gxmath/gxmath.h"

#include "../common/path.h"
#include "../main.h"
#include "../camera.h"
#include "../wrappers/wrappers.h"
#include "../rhi/rhi.h"

gX_I0 gX_RendererBackground_Transform_Reset(gX_RendererBackground_Transform *tra) {
    gX_RenBG_ParamScalar_Reset(&(tra->scale));
    gX_RenBG_ParamV2_Reset(&(tra->scaleOrigin));
    gX_RenBG_ParamScalar_Reset(&(tra->rotate));
    gX_RenBG_ParamV2_Reset(&(tra->rotateOrigin));
    gX_RenBG_ParamV4_Reset(&(tra->stretch));
    gX_RenBG_ParamV2_Reset(&(tra->perspective));
    gX_RenBG_ParamV2_Reset(&(tra->translate));
    gX_RenBG_ParamV2_Reset(&(tra->affine));
    gX_RenBG_ParamScalar_Reset(&(tra->matrixBlend)); //opencode: reset matrix blend
    tra->matrixA = gX_Matrix_Identity();
    tra->matrixB = gX_Matrix_Identity();
}

//special
//  non-radial
//      scale:
//      translate: wave
//      rotate:
//  radial
//      scale: warp
//      translate: ripple
//      rotate: spiral
gX_I0 gX_RendererBackground_Special_Reset(gX_RendererBackground_Special *spe) {
    gX_RenBG_ParamV3_Reset(&(spe->warp));
    gX_RenBG_ParamScalar_Reset(&(spe->warpExponent));
    gX_RenBG_ParamV4_Reset(&(spe->waveAmplitude));
    gX_RenBG_ParamV4_Reset(&(spe->waveFrequency));
    gX_RenBG_ParamV4_Reset(&(spe->waveRegularwave));
    gX_RenBG_ParamV4_Reset(&(spe->waveInterlaced));
    gX_RenBG_ParamV4_Reset(&(spe->waveInterlacedMultiplier));
    gX_RenBG_ParamV4_Reset(&(spe->waveSpeed));
    gX_RenBG_ParamV4_Reset(&(spe->wavePhase));
    
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++) { gX_RenBG_Sinusoid_Reset(&spe->sinusoids[i]); }
    for (gX_U32 i = 0; i < gX_BG_HDMA_CHANNEL_COUNT; i++) { gX_RenBG_HDMA_Channel_Reset(&spe->hdma[i]); }
}

gX_I0 gX_RendererBackground_Palette_Reset(gX_RendererBackground_Palette *pal) {
    gX_RenBG_ParamScalar_Reset(&(pal->amount));
    gX_RenBG_ParamScalar_Reset(&(pal->speed));
    gX_RenBG_ParamScalar_Reset(&(pal->frequency));
    gX_RenBG_ParamV4_Reset(&(pal->color1));
    gX_RenBG_ParamV4_Reset(&(pal->color2));
    gX_RenBG_ParamScalar_Reset(&(pal->bgBoundary));
    gX_RenBG_ParamV4_Reset(&(pal->bgColor));
    gX_RenBG_ParamScalar_Reset(&(pal->fgBoundary));
    gX_RenBG_ParamV4_Reset(&(pal->fgColor));
}

//gX_I0 gX_RendererBackground_Misc_Reset(gX_RendererBackground_Misc *misc)

//ai: the value expression is passed in directly — scalar defaults come from
//   the polisher-visible runtime variables, vectors stay constants.
#define gX_BG_LOAD_PARAM(dt,modu,name,defval) \
    gX_RenBG_Param##dt##_Init(  &(bg->modu.name), defval);

inline static gX_I0 gX_RendererBackground_Load_Params(gX_RendererBackground *bg) {
    gX_BG_LOAD_PARAM(Scalar,transform,scale,        gX_Bg_ScaleDefval)
    gX_BG_LOAD_PARAM(V2,    transform,scaleOrigin,  gX_BG_SCALEORIGIN_DEFVAL)
    gX_BG_LOAD_PARAM(Scalar,transform,rotate,       gX_Bg_RotateDefval)
    gX_BG_LOAD_PARAM(V2,    transform,rotateOrigin, gX_BG_ROTATEORIGIN_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    transform,stretch,      gX_BG_STRETCH_DEFVAL)
    gX_BG_LOAD_PARAM(V2,    transform,perspective,  gX_BG_PERSPECTIVE_DEFVAL)
    gX_BG_LOAD_PARAM(V2,    transform,translate,    gX_BG_TRANSLATE_DEFVAL)
    gX_BG_LOAD_PARAM(V2,    transform,affine,       gX_BG_AFFINE_DEFVAL)
    gX_BG_LOAD_PARAM(Scalar,transform,matrixBlend,  gX_Bg_MatrixblendDefval)
    
    gX_BG_LOAD_PARAM(V3,    special,warp,                       gX_BG_WARP_DEFVAL)
    gX_BG_LOAD_PARAM(Scalar,special,warpExponent,               gX_Bg_WarpexpDefval)
    gX_BG_LOAD_PARAM(V4,    special,waveAmplitude,              gX_BG_WAVEAMP_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,waveFrequency,              gX_BG_WAVEFRE_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,waveRegularwave,            gX_BG_WAVERWE_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,waveInterlaced,             gX_BG_WAVEILE_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,waveInterlacedMultiplier,   gX_BG_WAVEILM_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,waveSpeed,                  gX_BG_WAVESPD_DEFVAL)
    gX_BG_LOAD_PARAM(V4,    special,wavePhase,                  gX_BG_WAVEPHA_DEFVAL)
    
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++) { gX_RenBG_Sinusoid_Init(&bg->special.sinusoids[i]); }
    for (gX_U32 i = 0; i < gX_BG_HDMA_CHANNEL_COUNT; i++) { gX_RenBG_HDMA_Channel_Init(&bg->special.hdma[i]); }
    
    gX_BG_LOAD_PARAM(Scalar,palette,amount,                     gX_Bg_PalamountDefval)
    gX_BG_LOAD_PARAM(Scalar,palette,speed,                      gX_Bg_PalspeedDefval)
    gX_BG_LOAD_PARAM(Scalar,palette,frequency,                  gX_Bg_PalfrequencyDefval)
    gX_BG_LOAD_PARAM(Scalar,palette,size,                       gX_BG_PALSIZE_DEFVAL)
    gX_RenBG_ParamV4_Init(      &(bg->palette.color1),                  (gX_F32v4){0.0f,1.0f,0.0f,1.0f});
    gX_RenBG_ParamV4_Init(      &(bg->palette.color2),                  (gX_F32v4){0.0f,0.0f,1.0f,1.0f});
    gX_RenBG_ParamScalar_Init(  &(bg->palette.bgBoundary),              gX_Bg_PalbgboundDefval);
    gX_RenBG_ParamV4_Init(      &(bg->palette.bgColor),                 (gX_F32v4){0.0f,0.0f,0.0f,1.0f});
    gX_RenBG_ParamScalar_Init(  &(bg->palette.fgBoundary),              gX_Bg_PalfgboundDefval);
    gX_RenBG_ParamV4_Init(      &(bg->palette.fgColor),                 (gX_F32v4){1.0f,1.0f,1.0f,1.0f});
    
    gX_RenBG_ParamV4_Init(      &(bg->misc.boundaryXY),                 (gX_F32v4){-0.5f,+0.5f,-0.5f,+0.5f});
    gX_RenBG_ParamV2_Init(      &(bg->misc.boundaryZ),                  (gX_F32v2){+0.025f,+10.0f});
    gX_RenBG_ParamV2_Init(      &(bg->misc.modulo),                     (gX_F32v2){1.0f,10.0f});
}

#define gX_BG_SHADER_GETLOC(name,locstring) \
    bg->shader.locations.location##name = gX_Shader_GetLocation(bg->shader.wrapper.shader,locstring); \

inline static gX_I0 gX_RendererBackground_Load_ShaderLocations(gX_RendererBackground *bg) {
    gX_BG_SHADER_GETLOC(Time,"time")
    gX_BG_SHADER_GETLOC(Res,"res")
    gX_BG_SHADER_GETLOC(BattleViewEnabled,"battleViewEnabled")
    gX_BG_SHADER_GETLOC(BattleCameraPosition,"battleCameraPosition")
    gX_BG_SHADER_GETLOC(BattleCameraTarget,"battleCameraTarget")
    gX_BG_SHADER_GETLOC(BattleCameraUp,"battleCameraUp")
    gX_BG_SHADER_GETLOC(BattleCameraFov,"battleCameraFov")
    
    gX_BG_SHADER_GETLOC(InputTexture,"tex0")
    gX_BG_SHADER_GETLOC(InputTexture2,"tex2")
    gX_BG_SHADER_GETLOC(InputPalette,"tex1")
    gX_BG_SHADER_GETLOC(InputTextureRes,"tex0res")
    gX_BG_SHADER_GETLOC(InputTexture2Res,"tex2res")
    gX_BG_SHADER_GETLOC(InputPaletteRes,"tex1res")
    
    gX_BG_SHADER_GETLOC(Scale,"scale")
    gX_BG_SHADER_GETLOC(ScaleOrigin,"scaleOrigin")
    gX_BG_SHADER_GETLOC(Rotate,"rotate")
    gX_BG_SHADER_GETLOC(RotateOrigin,"rotateOrigin")
    gX_BG_SHADER_GETLOC(Stretch,"stretch")
    gX_BG_SHADER_GETLOC(Perspective,"perspective")
    gX_BG_SHADER_GETLOC(Translate,"translate")
    gX_BG_SHADER_GETLOC(Affine,"affine")
    
    gX_BG_SHADER_GETLOC(Warp,"warp")
    gX_BG_SHADER_GETLOC(WarpExp,"warp_exp")
    gX_BG_SHADER_GETLOC(WaveAmp,"wave_amp")
    gX_BG_SHADER_GETLOC(WaveFre,"wave_fre")
    gX_BG_SHADER_GETLOC(WaveRwe,"wave_rwe")
    gX_BG_SHADER_GETLOC(WaveIle,"wave_ile")
    gX_BG_SHADER_GETLOC(WaveIlm,"wave_ilm")
    gX_BG_SHADER_GETLOC(WaveSpd,"wave_spd")
    gX_BG_SHADER_GETLOC(WavePha,"wave_pha")
    
    gX_BG_SHADER_GETLOC(PalMode,"palette_mode")
    gX_BG_SHADER_GETLOC(PalSize,"palette_size")
    gX_BG_SHADER_GETLOC(PalRowCount,"palette_rowCount")
    gX_BG_SHADER_GETLOC(PalColors,"colors")
    gX_BG_SHADER_GETLOC(PalAmo,"palette_amount")
    gX_BG_SHADER_GETLOC(PalSpd,"palette_speed")
    gX_BG_SHADER_GETLOC(PalFre,"palette_freq")
    gX_BG_SHADER_GETLOC(PalCol1,"color1")
    gX_BG_SHADER_GETLOC(PalCol2,"color2")
    gX_BG_SHADER_GETLOC(PalBgBound,"bg_boundary")
    gX_BG_SHADER_GETLOC(PalFgBound,"fg_boundary")
    gX_BG_SHADER_GETLOC(PalBgCol,"color_bg")
    gX_BG_SHADER_GETLOC(PalFgCol,"color_fg")
    
    bg->shader.locations.locationBlendMode       = gX_Shader_GetLocation(bg->shader.wrapper.shader,"blendMode");
    bg->shader.locations.locationColorMathMode   = gX_Shader_GetLocation(bg->shader.wrapper.shader,"colorMathMode");
    bg->shader.locations.locationColorMathMask   = gX_Shader_GetLocation(bg->shader.wrapper.shader,"colorMathMask");
    bg->shader.locations.locationLayer2Enabled   = gX_Shader_GetLocation(bg->shader.wrapper.shader,"layer2Enabled");
    bg->shader.locations.locationReactType       = gX_Shader_GetLocation(bg->shader.wrapper.shader,"reactType");
    bg->shader.locations.locationReactIntensity  = gX_Shader_GetLocation(bg->shader.wrapper.shader,"reactIntensity");
    
    bg->shader.locations.locationBoundaryXY     = gX_Shader_GetLocation(bg->shader.wrapper.shader,"boundaryXY");
    bg->shader.locations.locationBoundaryZ      = gX_Shader_GetLocation(bg->shader.wrapper.shader,"boundaryZ");
    bg->shader.locations.locationModulo         = gX_Shader_GetLocation(bg->shader.wrapper.shader,"modulo");
    
    bg->shader.locations.locationParamEnable        = gX_Shader_GetLocation(bg->shader.wrapper.shader,"param_enable");
    bg->shader.locations.locationWarpEnable         = gX_Shader_GetLocation(bg->shader.wrapper.shader,"warp_enable");
    bg->shader.locations.locationWaveEnable         = gX_Shader_GetLocation(bg->shader.wrapper.shader,"wave_enable");
    bg->shader.locations.locationPaletteEnable      = gX_Shader_GetLocation(bg->shader.wrapper.shader,"palette_enable");
    bg->shader.locations.locationBoundaryXYEnable   = gX_Shader_GetLocation(bg->shader.wrapper.shader,"boundaryXY_enable");
    bg->shader.locations.locationBoundaryZEnable    = gX_Shader_GetLocation(bg->shader.wrapper.shader,"boundaryZ_enable");
    bg->shader.locations.locationModuloEnable       = gX_Shader_GetLocation(bg->shader.wrapper.shader,"modulo_enable");
    
    //opencode: new shader uniform locations
    gX_BG_SHADER_GETLOC(TransformMatrix,"transform")
    gX_BG_SHADER_GETLOC(SinusoidAmp,"sinusoidAmp")
    gX_BG_SHADER_GETLOC(SinusoidFre,"sinusoidFre")
    gX_BG_SHADER_GETLOC(SinusoidPha,"sinusoidPha")
    gX_BG_SHADER_GETLOC(SinusoidSpd,"sinusoidSpd")
    gX_BG_SHADER_GETLOC(SinusoidRwe,"sinusoidRwe")
    gX_BG_SHADER_GETLOC(SinusoidIle,"sinusoidIle")
    gX_BG_SHADER_GETLOC(SinusoidIlm,"sinusoidIlm")
    gX_BG_SHADER_GETLOC(SinusoidExp,"sinusoidExp")
    gX_BG_SHADER_GETLOC(SinusoidCen,"sinusoidCen") //opencode: radial sinusoid center
    gX_BG_SHADER_GETLOC(SinusoidDcO,"sinusoidDcO") //opencode: sinusoid DC offset
    gX_BG_SHADER_GETLOC(SinusoidLin,"sinusoidLin") //opencode: sinusoid linear coefficient
    gX_BG_SHADER_GETLOC(SinusoidQuad,"sinusoidQuad") //opencode: sinusoid quadratic coefficient
    gX_BG_SHADER_GETLOC(SinusoidAtt,"sinusoidAtt") //opencode: sinusoid radial attenuation
    
    //opencode: HDMA uniform array locations
    gX_BG_SHADER_GETLOC(HdmaAmp,"hdmaAmp")
    gX_BG_SHADER_GETLOC(HdmaFre,"hdmaFre")
    gX_BG_SHADER_GETLOC(HdmaSpd,"hdmaSpd")
    gX_BG_SHADER_GETLOC(HdmaPha,"hdmaPha")
    gX_BG_SHADER_GETLOC(HdmaOff,"hdmaOff")
    bg->shader.locations.locationHdmaEnable = gX_Shader_GetLocation(bg->shader.wrapper.shader,"hdma_enable");
    
    //opencode: procedural noise uniform locations
    gX_BG_SHADER_GETLOC(NoiseMode,"noiseMode")
    gX_BG_SHADER_GETLOC(NoiseScale,"noiseScale")
    gX_BG_SHADER_GETLOC(NoiseSpeed,"noiseSpeed")
    gX_BG_SHADER_GETLOC(NoiseOctaves,"noiseOctaves")
    gX_BG_SHADER_GETLOC(NoiseLacunarity,"noiseLacunarity")
    gX_BG_SHADER_GETLOC(NoiseGain,"noiseGain")
    
    //opencode: mark sinusoids dirty after shader reload so values upload on first frame
    bg->state.sinusoidDirty = true;
}

gX_I0 gX_RendererBackground_Load(gX_RendererBackground *bg) {
    gX_TRACELOG_PROLOGUE("RendererBackground_Load","RENDERER_BACKGROUND: Loading background renderer")
    
    strcpy(bg->name,"Background");
    strcpy(bg->description,"battle background");
    //ai: register the Background polisher entries (idempotent).
    gX_Background_RegisterPolisher();
    gX_TraceLog_BACKGROUND(gX_LOG_GREEN,"name: %s",bg->name);
    gX_TraceLog_BACKGROUND(gX_LOG_GREEN,"desc: %s",bg->description);
    
    //gX_TraceLog_BACKGROUND(gX_LOG_INFO,"1");
    
    gX_RendererBackground_Load_Params(bg);
    
    bg->state.paramsDirty = true; //opencode: initial param load
    bg->state.colorsDirty = true; //opencode: initial colors upload
    bg->special.noiseMode = 0;
    gX_RenBG_ParamScalar_Init(&bg->special.noiseScale, 1.0f);
    gX_RenBG_ParamScalar_Init(&bg->special.noiseSpeed, 0.0f);
    bg->special.noiseOctaves = 4;
    gX_RenBG_ParamScalar_Init(&bg->special.noiseLacunarity, 2.0f);
    gX_RenBG_ParamScalar_Init(&bg->special.noiseGain, 0.5f);

    bg->state.paramEnabled = true;
    bg->state.warpEnabled = true;
    bg->state.waveEnabled = true;
    bg->state.boundaryEnabled = true;
    bg->state.moduloEnabled = false;
    bg->state.hdmaEnabled = false;
    bg->state.reactEnabled = false;
    bg->reactType = GX_BG_REACT_NONE;
    bg->reactIntensity = 0.0f;
    bg->reactTimer = 0.0f;
    bg->reactDuration = 0.0f;
    
    //opencode: new shader is default — old background.frag kept for backward compat
    bg->shader.useNewShader = true;
    
    bg->transform.transformation = gX_Matrix_Identity(); //opencode: replaced manual field init
    //bg->transform.scale.velocity = 0.01f;
    
    bg->palette.mode = 1;
    bg->palette.rowCount = 16;
    bg->palette.blendMode = 0;
    bg->palette.colorMathMode = 0;
    bg->palette.colorMathMask = (gX_F32v3){1,1,1};
    for (gX_U32 i = 0; i < gX_BG_PALETTE_COLORS_MAX; i++) {
        gX_F32 f = (gX_F32)i;
        bg->palette.colors[i].constant = (gX_F32v4){0.0f,1.0f-f/(gX_F32)gX_BG_PALETTE_COLORS_MAX,0.0f+f/(gX_F32)gX_BG_PALETTE_COLORS_MAX,1.0f};
    }
    
    bg->misc.modulo.constant = gX_F32v2_One();
    
    bg->shader.wrapper.loadingState.loadAsap = true;
    //opencode: select shader path based on useNewShader flag
    if (bg->shader.useNewShader) {
        strcpy(bg->shader.wrapper.pathFrag,GX_PATH("shader", "background_new.frag"));
    } else {
        strcpy(bg->shader.wrapper.pathFrag,GX_PATH("shader", "background.frag"));
    }
    bg->shader.wrapper.version = 330;
    bg->shader.wrapper.useDefaultVert = true;
    
    bg->texture.wrapper.loadingState.loadAsap = true;
    //opencode: gX ships the texture at resources/texture/texture.png (same file)
    strcpy(bg->texture.wrapper.path,GX_PATH("texture", "texture.png"));
    
    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Loading done")
}

gX_I0 gX_RendererBackground_Unload(gX_RendererBackground *bg) {
    gX_TRACELOG_PROLOGUE("RendererBackground_Unload","RENDERER_BACKGROUND: Unloading background renderer")
    
    gX_Shader_Unload(bg->shader.wrapper.shader);
    
    gX_Texture_Unload(bg->texture.wrapper.texture);
    
    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Unloading done")
}

gX_I0 gX_RendererBackground_LoadDefault(gX_RendererBackground *bg) {
    gX_TRACELOG_PROLOGUE("RendererBackground_LoadDefault","RENDERER_BACKGROUND: Loading default values")
    
    gX_RendererBackground_Transform_Reset(&bg->transform);
    gX_RendererBackground_Special_Reset(&bg->special);
    gX_RendererBackground_Palette_Reset(&bg->palette);
    bg->state.battleProjectionEnabled = false;
    
    //ai: the backdrop's default transform comes from the polisher-visible
    //   runtime defaults (identical values unless GX_POLISHER overrides them).
    bg->transform.scale.constant = gX_Bg_ScaleDefval;
    bg->transform.rotate.constant = gX_Bg_RotateDefval;
    bg->state.paramsDirty = true; //opencode: params reset
    
    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Loading done")
}

//ai: a pending --bg-palette-mode wins over built-in presets. Applied from the
//   per-frame update and from the preset path: a scene switch re-applies its
//   preset after the module's first update, which used to swallow the flag.
static gX_I0 bg_apply_cli_palette_mode(gX_RendererBackground *bg) {
    if (!arguments.hasBgPaletteMode) return;
    bg->palette.mode = arguments.bgPaletteMode;
    bg->state.paletteEnabled = true;
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_PALETTE: mode=%d enabled=1 (cli)",
        bg->palette.mode);
    arguments.hasBgPaletteMode = false;
}

//ai: preset name strings (declared in background.h; the enum coverage gate
//   wants every preset reachable by name).
#define gX_BG_ENUMNAME_CASE(name,stri) \
    case name: return stri; break;

const gX_I8 *gX_RendererBackground_Presets_String(gX_I32 preset) {
#define X(preset) gX_BG_ENUMNAME_CASE(gX_BG_PRESET_##preset,#preset);
    switch (preset) {
        gX_BG_PRESETS
        default: return "undefined";
    }
#undef X
}

gX_I0 gX_RendererBackground_Preset_BuiltIn(gX_RendererBackground *bg, gX_I32 index) {
    gX_TRACELOG_PROLOGUE("RendererBackground_Preset_BuiltIn","RENDERER_BACKGROUND: Loading builtin background preset")
    
    gX_RendererBackground_LoadDefault(bg);
    bg->state.battleProjectionEnabled = index == gX_BG_PRESET_BATTLE;
    
    switch (index) {
        case 0: {
            bg->state.boundaryEnabled = false;
            bg->transform.scale.constant = 1.0f;
            bg->transform.translate.velocity.x = -0.1f;
            bg->transform.translate.velocity.y = +0.2f;
            break;
        }
        //opencode: neon palette — deep purple/black with bright cyan/magenta
        case 1: {
            bg->state.boundaryEnabled = false;
            bg->palette.color1.constant = (gX_F32v4){0.05f,0.0f,0.12f,1.0f}; // deep purple
            bg->palette.color2.constant = (gX_F32v4){0.0f,0.8f,0.9f,1.0f};  // bright cyan
            bg->transform.translate.velocity.x = 0.05f;
            bg->transform.translate.velocity.y = -0.03f;
            break;
        }
        //ai: TITLE — Mother 2-style palette-cycled backdrop for the title
        //   scene: hue-cycling luminance bands (palette mode 3) with a slow
        //   drift and the dark/bright cutoffs reading as the neon palette.
        case 2: {
            gX_TraceLog_BACKGROUND(gX_LOG_INFO,
                "BG_PRESET: TITLE palette mode 3 (hue cycle)");
            bg->state.boundaryEnabled = false;
            bg->state.paletteEnabled = true;
            bg->palette.mode = 3;
            bg->palette.amount.constant = 24.0f;
            bg->palette.speed.constant = 0.08f;
            bg->palette.frequency.constant = 0.6f;
            bg->palette.bgBoundary.constant = 0.12f;
            bg->palette.fgBoundary.constant = 0.88f;
            bg->palette.bgColor.constant = (gX_F32v4){0.05f,0.0f,0.12f,1.0f}; // deep purple
            bg->palette.fgColor.constant = (gX_F32v4){1.0f,0.95f,0.75f,1.0f}; // warm highlight
            bg->transform.translate.velocity.x = 0.02f;
            bg->transform.translate.velocity.y = -0.015f;
            break;
        }
        //ai: BATTLE — Mother 2-style battle backdrop: palette mode 1
        //   posterized gradient (deep navy -> violet) breathing slowly, dark
        //   enough for the 2D HUD panels to read over it.
        case 3: {
            gX_TraceLog_BACKGROUND(gX_LOG_INFO,
                "BG_PRESET: BATTLE palette mode 1 (posterized gradient)");
            bg->state.boundaryEnabled = false;
            bg->state.paletteEnabled = true;
            bg->palette.mode = 1;
            bg->palette.color1.constant = (gX_F32v4){0.015f, 0.03f, 0.11f, 1.0f}; // deep navy
            bg->palette.color2.constant = (gX_F32v4){0.34f, 0.06f, 0.46f, 1.0f};  // violet
            bg->palette.amount.constant = 32.0f;
            bg->palette.speed.constant = 0.02f;
            bg->palette.frequency.constant = 1.2f;
            bg->palette.bgBoundary.constant = 0.08f;
            bg->palette.fgBoundary.constant = 0.92f;
            bg->palette.bgColor.constant = (gX_F32v4){0.01f, 0.01f, 0.04f, 1.0f};
            bg->palette.fgColor.constant = (gX_F32v4){0.62f, 0.28f, 0.72f, 1.0f};
            bg->transform.translate.velocity.x = 0.012f;
            bg->transform.translate.velocity.y = -0.009f;
            break;
        }
        default: break;
    }
    
    bg_apply_cli_palette_mode(bg);

    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Loading done")
}

gX_I0 gX_RendererBackground_Preset_Load(gX_RendererBackground *bg, const gX_I8 *path) {
    gX_TRACELOG_PROLOGUE("RendererBackground_Preset_Load","RENDERER_BACKGROUND: Loading background preset")
    gX_RendererBG_Preset_Load_New(bg, path);
    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Loading done")
}

gX_I0 gX_RendererBackground_Preset_Save(gX_RendererBackground *bg, const gX_I8 *path) {
    gX_TRACELOG_PROLOGUE("RendererBackground_Preset_Save","RENDERER_BACKGROUND: Saving background preset")
    gX_RendererBG_Preset_Save_New(bg, path);
    gX_TRACELOG_EPILOGUE("RENDERER_BACKGROUND: Saving done")
}

//opencode: scan if any param has motion (velocity/acceleration/oscillator amplitude)
static gX_I1 bg_has_param_motion(gX_RendererBackground *bg)
{
    #define BG_SCAN_MOTION_SCALAR(p) \
        if ((p).velocity != 0.0f || (p).acceleration != 0.0f) return 1; \
        for (gX_U32 _i = 0; _i < gX_BG_OSCAMOUNT; _i++) \
            if ((p).amplitude[_i] != 0.0f) return 1;
    #define BG_SCAN_MOTION_V2(p) \
        if ((p).velocity.x != 0.0f || (p).velocity.y != 0.0f || \
            (p).acceleration.x != 0.0f || (p).acceleration.y != 0.0f) return 1; \
        for (gX_U32 _i = 0; _i < gX_BG_OSCAMOUNT; _i++) \
            if ((p).amplitude[_i].x != 0.0f || (p).amplitude[_i].y != 0.0f) return 1;
    #define BG_SCAN_MOTION_V3(p) \
        if ((p).velocity.x != 0.0f || (p).velocity.y != 0.0f || (p).velocity.z != 0.0f || \
            (p).acceleration.x != 0.0f || (p).acceleration.y != 0.0f || (p).acceleration.z != 0.0f) return 1; \
        for (gX_U32 _i = 0; _i < gX_BG_OSCAMOUNT; _i++) \
            if ((p).amplitude[_i].x != 0.0f || (p).amplitude[_i].y != 0.0f || (p).amplitude[_i].z != 0.0f) return 1;
    #define BG_SCAN_MOTION_V4(p) \
        if ((p).velocity.x != 0.0f || (p).velocity.y != 0.0f || (p).velocity.z != 0.0f || (p).velocity.w != 0.0f || \
            (p).acceleration.x != 0.0f || (p).acceleration.y != 0.0f || (p).acceleration.z != 0.0f || (p).acceleration.w != 0.0f) return 1; \
        for (gX_U32 _i = 0; _i < gX_BG_OSCAMOUNT; _i++) \
            if ((p).amplitude[_i].x != 0.0f || (p).amplitude[_i].y != 0.0f || (p).amplitude[_i].z != 0.0f || (p).amplitude[_i].w != 0.0f) return 1;

    // Transform
    BG_SCAN_MOTION_SCALAR(bg->transform.scale)
    BG_SCAN_MOTION_V2(bg->transform.scaleOrigin)
    BG_SCAN_MOTION_SCALAR(bg->transform.rotate)
    BG_SCAN_MOTION_V2(bg->transform.rotateOrigin)
    BG_SCAN_MOTION_V4(bg->transform.stretch)
    BG_SCAN_MOTION_V2(bg->transform.perspective)
    BG_SCAN_MOTION_V2(bg->transform.translate)
    BG_SCAN_MOTION_V2(bg->transform.affine)
    BG_SCAN_MOTION_SCALAR(bg->transform.matrixBlend)

    // Special
    BG_SCAN_MOTION_V3(bg->special.warp)
    BG_SCAN_MOTION_SCALAR(bg->special.warpExponent)
    BG_SCAN_MOTION_V4(bg->special.waveAmplitude)
    BG_SCAN_MOTION_V4(bg->special.waveFrequency)
    BG_SCAN_MOTION_V4(bg->special.waveRegularwave)
    BG_SCAN_MOTION_V4(bg->special.waveInterlaced)
    BG_SCAN_MOTION_V4(bg->special.waveInterlacedMultiplier)
    BG_SCAN_MOTION_V4(bg->special.waveSpeed)
    BG_SCAN_MOTION_V4(bg->special.wavePhase)

    // Sinusoids (8 × 12 params)
    for (gX_U32 si = 0; si < gX_BG_SINUSOID_COUNT; si++) {
        gX_RenBG_Sinusoid *s = &bg->special.sinusoids[si];
        BG_SCAN_MOTION_V4(s->amplitude)
        BG_SCAN_MOTION_V4(s->frequency)
        BG_SCAN_MOTION_V4(s->phase)
        BG_SCAN_MOTION_V4(s->speed)
        BG_SCAN_MOTION_V4(s->regularwave)
        BG_SCAN_MOTION_V4(s->interlaced)
        BG_SCAN_MOTION_V4(s->interlacedMultiplier)
        BG_SCAN_MOTION_V4(s->dcOffset)
        BG_SCAN_MOTION_V4(s->linear)
        BG_SCAN_MOTION_V4(s->quadratic)
        BG_SCAN_MOTION_SCALAR(s->exponent)
        BG_SCAN_MOTION_SCALAR(s->attenuation)
        BG_SCAN_MOTION_V2(s->center)
    }

    // Palette
    BG_SCAN_MOTION_SCALAR(bg->palette.amount)
    BG_SCAN_MOTION_SCALAR(bg->palette.speed)
    BG_SCAN_MOTION_SCALAR(bg->palette.frequency)
    BG_SCAN_MOTION_V4(bg->palette.color1)
    BG_SCAN_MOTION_V4(bg->palette.color2)
    BG_SCAN_MOTION_SCALAR(bg->palette.bgBoundary)
    BG_SCAN_MOTION_V4(bg->palette.bgColor)
    BG_SCAN_MOTION_SCALAR(bg->palette.fgBoundary)
    BG_SCAN_MOTION_V4(bg->palette.fgColor)

    // Misc
    BG_SCAN_MOTION_V4(bg->misc.boundaryXY)
    BG_SCAN_MOTION_V2(bg->misc.boundaryZ)
    BG_SCAN_MOTION_V2(bg->misc.modulo)

    #undef BG_SCAN_MOTION_SCALAR
    #undef BG_SCAN_MOTION_V2
    #undef BG_SCAN_MOTION_V3
    #undef BG_SCAN_MOTION_V4
    return 0;
}

//opencode: trigger a game-event reaction with linear intensity decay over duration
gX_I0 gX_RendererBackground_React(gX_RendererBackground *bg, gX_I32 type, gX_F32 intensity, gX_F32 duration) {
    if (!bg) return;
    if (type < GX_BG_REACT_NONE || type > GX_BG_REACT_WARP) return;
    bg->reactType = type;
    bg->reactIntensity = intensity > 1.0f ? 1.0f : (intensity < 0.0f ? 0.0f : intensity);
    bg->reactTimer = duration > 0.0f ? duration : 0.0f;
    bg->reactDuration = bg->reactTimer;
    bg->state.reactEnabled = (bg->reactType != GX_BG_REACT_NONE && bg->reactTimer > 0.0f);
}

//opencode: advance reaction decay toward zero each frame
inline static gX_I0 gX_RendererBackground_Update_React(gX_RendererBackground *bg) {
    if (!bg->state.reactEnabled) return;
    if (bg->reactTimer > 0.0f) {
        bg->reactTimer -= (gX_F32)gX_FrameTime_Get();
        if (bg->reactTimer <= 0.0f) {
            bg->reactTimer = 0.0f;
            bg->reactType = GX_BG_REACT_NONE;
            bg->reactIntensity = 0.0f;
            bg->state.reactEnabled = false;
        } else if (bg->reactDuration > 0.0f) {
            bg->reactIntensity = (bg->reactTimer / bg->reactDuration);
        }
    }
}

inline static gX_I0 gX_RendererBackground_Update_State(gX_RendererBackground *bg) {
    if (bg->state.load) {
        gX_RendererBackground_Load(bg);
        bg->state.loaded = true;
        bg->state.load = false;
    }
    if (bg->state.unload) {
        gX_RendererBackground_Unload(bg);
        bg->state.loaded = false;
        bg->state.unload = false;
    }
}

inline static gX_I0 gX_RendererBackground_Update_Params(gX_RendererBackground *bg) {
    //opencode: skip param update if no params changed and no motion present
    if (!bg->state.paramsDirty && !bg_has_param_motion(bg)) {
        return;
    }
    bg->state.paramsDirty = false;
    
    gX_RenBG_ParamScalar_Update(&(bg->transform.scale));
    gX_RenBG_ParamV2_Update(    &(bg->transform.scaleOrigin));
    gX_RenBG_ParamScalar_Update(&(bg->transform.rotate));
    gX_RenBG_ParamV2_Update(    &(bg->transform.rotateOrigin));
    gX_RenBG_ParamV4_Update(    &(bg->transform.stretch));
    gX_RenBG_ParamV2_Update(    &(bg->transform.perspective));
    gX_RenBG_ParamV2_Update(    &(bg->transform.translate));
    gX_RenBG_ParamV2_Update(    &(bg->transform.affine));
    gX_RenBG_ParamScalar_Update(&(bg->transform.matrixBlend)); //opencode: matrix blend update
    
    //opencode: direct matrix mode — lerp between matrixA and matrixB
    if (bg->state.useDirectMatrix) {
        gX_F32 blend = bg->transform.matrixBlend.value;
        if (blend < 0.0f) blend = 0.0f;
        if (blend > 1.0f) blend = 1.0f;
        for (gX_I32 mi = 0; mi < 16; mi++) {
            ((gX_F32*)&bg->transform.transformation)[mi] =
                ((gX_F32*)&bg->transform.matrixA)[mi] * (1.0f - blend) +
                ((gX_F32*)&bg->transform.matrixB)[mi] * blend;
        }
    } else {
        //opencode: param-based matrix assembly (separate block avoids goto-jumps-over-init warning)
        gX_F32 ang = 2.0f*gX_PI*bg->transform.rotate.value;
        gX_F32 c = cosf(ang);
        gX_F32 s = sinf(ang);
        bg->transform.transformation.m0 = +c+bg->transform.stretch.value.x;
        bg->transform.transformation.m1 = -s+bg->transform.stretch.value.y;
        bg->transform.transformation.m2 = bg->transform.perspective.value.x;
        
        bg->transform.transformation.m4 = +s+bg->transform.stretch.value.z;
        bg->transform.transformation.m5 = +c+bg->transform.stretch.value.w;
        bg->transform.transformation.m6 = bg->transform.perspective.value.y;
        
        bg->transform.transformation.m8 = bg->transform.affine.value.x;
        bg->transform.transformation.m9 = bg->transform.affine.value.y;
        bg->transform.transformation.m10 = bg->transform.scale.value;
        
        bg->transform.transformation.m12 = 0.0f; //translate is applied post-divide in new shader
        bg->transform.transformation.m13 = 0.0f;
    }
    
    gX_RenBG_ParamV3_Update(    &(bg->special.warp));
    gX_RenBG_ParamScalar_Update(&(bg->special.warpExponent));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveAmplitude));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveFrequency));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveRegularwave));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveInterlaced));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveInterlacedMultiplier));
    gX_RenBG_ParamV4_Update(    &(bg->special.waveSpeed));
    gX_RenBG_ParamV4_Update(    &(bg->special.wavePhase));
    
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++) { gX_RenBG_Sinusoid_Update(&bg->special.sinusoids[i]); }
    for (gX_U32 i = 0; i < gX_BG_HDMA_CHANNEL_COUNT; i++) { gX_RenBG_HDMA_Channel_Update(&bg->special.hdma[i]); }
    
    gX_RenBG_ParamScalar_Update(&bg->special.noiseScale);
    gX_RenBG_ParamScalar_Update(&bg->special.noiseSpeed);
    gX_RenBG_ParamScalar_Update(&bg->special.noiseLacunarity);
    gX_RenBG_ParamScalar_Update(&bg->special.noiseGain);
    
    //opencode: detect animated sinusoids — if any have motion, re-upload every frame
    {
        gX_I1 anyAnimated = false;
        for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT && !anyAnimated; i++) {
            gX_RenBG_Sinusoid *sin = &bg->special.sinusoids[i];
            // Check scalar params (exponent, attenuation)
            if (sin->exponent.velocity != 0.0f || sin->exponent.acceleration != 0.0f ||
                sin->attenuation.velocity != 0.0f || sin->attenuation.acceleration != 0.0f)
                anyAnimated = true;
            // Check V4 params — compare first component as proxy for whole vector
            if (sin->amplitude.velocity.x != 0.0f || sin->amplitude.acceleration.x != 0.0f ||
                sin->frequency.velocity.x != 0.0f || sin->frequency.acceleration.x != 0.0f ||
                sin->phase.velocity.x != 0.0f || sin->phase.acceleration.x != 0.0f ||
                sin->speed.velocity.x != 0.0f || sin->speed.acceleration.x != 0.0f ||
                sin->regularwave.velocity.x != 0.0f || sin->regularwave.acceleration.x != 0.0f ||
                sin->interlaced.velocity.x != 0.0f || sin->interlaced.acceleration.x != 0.0f ||
                sin->interlacedMultiplier.velocity.x != 0.0f || sin->interlacedMultiplier.acceleration.x != 0.0f ||
                sin->dcOffset.velocity.x != 0.0f || sin->dcOffset.acceleration.x != 0.0f ||
                sin->linear.velocity.x != 0.0f || sin->linear.acceleration.x != 0.0f ||
                sin->quadratic.velocity.x != 0.0f || sin->quadratic.acceleration.x != 0.0f ||
                sin->center.velocity.x != 0.0f || sin->center.acceleration.x != 0.0f)
                anyAnimated = true;
            // Check oscillator amplitudes — first component as proxy
            for (gX_U32 o = 0; o < gX_BG_OSCAMOUNT && !anyAnimated; o++) {
                if (sin->amplitude.amplitude[o].x != 0.0f ||
                    sin->frequency.amplitude[o].x != 0.0f)
                    anyAnimated = true;
            }
        }
        if (anyAnimated) bg->state.sinusoidDirty = true;
    }
    
    gX_RenBG_ParamScalar_Update(&(bg->palette.amount));
    gX_RenBG_ParamScalar_Update(&(bg->palette.speed));
    gX_RenBG_ParamScalar_Update(&(bg->palette.frequency));
    gX_RenBG_ParamScalar_Update(&(bg->palette.size));
    gX_RenBG_ParamV4_Update(    &(bg->palette.color1));
    gX_RenBG_ParamV4_Update(    &(bg->palette.color2));
    gX_RenBG_ParamScalar_Update(&(bg->palette.bgBoundary));
    gX_RenBG_ParamV4_Update(    &(bg->palette.bgColor));
    gX_RenBG_ParamScalar_Update(&(bg->palette.fgBoundary));
    gX_RenBG_ParamV4_Update(    &(bg->palette.fgColor));
    
    gX_RenBG_ParamV4_Update(    &(bg->misc.boundaryXY));
    gX_RenBG_ParamV2_Update(    &(bg->misc.boundaryZ));
    gX_RenBG_ParamV2_Update(    &(bg->misc.modulo));
}

#define gX_BG_SHADER_GETVAL_V2(name,modu,att) \
    gX_F32 name[2] = {bg->modu.att.value.x,bg->modu.att.value.y};
    
#define gX_BG_SHADER_GETVAL_V3(name,modu,att) \
    gX_F32 name[3] = {bg->modu.att.value.x,bg->modu.att.value.y,bg->modu.att.value.z};
    
#define gX_BG_SHADER_GETVAL_V4(name,modu,att) \
    gX_F32 name[4] = {bg->modu.att.value.x,bg->modu.att.value.y,bg->modu.att.value.z,bg->modu.att.value.w};

#define gX_BG_SHADER_SETVAL(loc,val,dt) \
    gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.location##loc, val, gX_SHADER_UNIFORM_##dt);

inline static gX_I0 gX_RendererBackground_Update_ShaderValues(gX_RendererBackground *bg) {
    gX_F32 time = (gX_F32)maindata.timeElapsed;
    gX_F32 res[2] = {maindata.renderWidth,maindata.renderHeight};
    //opencode: tex0 source dimensions (static texture; video source dropped in gX)
    gX_F32 tex0res[2] = {(gX_F32)bg->texture.wrapper.texture.width,(gX_F32)bg->texture.wrapper.texture.height};
    gX_F32 tex2res[2] = {(gX_F32)bg->texture.wrapper2.texture.width,(gX_F32)bg->texture.wrapper2.texture.height};
    
    gX_F32 scale = bg->transform.scale.value;
    gX_BG_SHADER_GETVAL_V2(scaleorigin,transform,scaleOrigin)
    gX_BG_SHADER_GETVAL_V2(rotateorigin,transform,rotateOrigin)
    gX_BG_SHADER_GETVAL_V2(perspective,transform,perspective)
    gX_BG_SHADER_GETVAL_V2(translate,transform,translate)
    
    //opencode: old shader uniforms (rotate/stretch/affine/warp/wave) — gated to save CPU with new shader
    if (!bg->shader.useNewShader) {
        gX_F32 rotate = bg->transform.rotate.value;
        gX_BG_SHADER_GETVAL_V4(stretch,transform,stretch)
        gX_BG_SHADER_GETVAL_V2(affine,transform,affine)
        gX_BG_SHADER_GETVAL_V3(warp,special,warp)
        gX_F32 warpexp = bg->special.warpExponent.value;
        gX_BG_SHADER_GETVAL_V4(waveamp,special,waveAmplitude)
        gX_BG_SHADER_GETVAL_V4(wavefre,special,waveFrequency)
        gX_BG_SHADER_GETVAL_V4(waverwe,special,waveRegularwave)
        gX_BG_SHADER_GETVAL_V4(waveile,special,waveInterlaced)
        gX_BG_SHADER_GETVAL_V4(waveilm,special,waveInterlacedMultiplier)
        gX_BG_SHADER_GETVAL_V4(wavespd,special,waveSpeed)
        gX_BG_SHADER_GETVAL_V4(wavepha,special,wavePhase)
        
        gX_BG_SHADER_SETVAL(Rotate, &rotate, FLOAT)
        gX_BG_SHADER_SETVAL(Stretch, stretch, VEC4)
        gX_BG_SHADER_SETVAL(Affine, affine, VEC2)
        gX_BG_SHADER_SETVAL(Warp, warp, VEC3)
        gX_BG_SHADER_SETVAL(WarpExp, &warpexp, FLOAT)
        gX_BG_SHADER_SETVAL(WaveAmp, waveamp, VEC4)
        gX_BG_SHADER_SETVAL(WaveFre, wavefre, VEC4)
        gX_BG_SHADER_SETVAL(WaveRwe, waverwe, VEC4)
        gX_BG_SHADER_SETVAL(WaveIle, waveile, VEC4)
        gX_BG_SHADER_SETVAL(WaveIlm, waveilm, VEC4)
        gX_BG_SHADER_SETVAL(WaveSpd, wavespd, VEC4)
        gX_BG_SHADER_SETVAL(WavePha, wavepha, VEC4)
        
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationWarpEnable, bg->state.warpEnabled);
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationWaveEnable, bg->state.waveEnabled);
    }
    
    //ai: palette_amount is an int uniform in both background shaders — upload
    //   the rounded value. A glUniform1f on an int uniform is a GL type error
    //   and the value stays silently at its link-time default (16), so the
    //   quantization level could never be authored.
    gX_I32 paleamo = (gX_I32)(bg->palette.amount.value + 0.5f);
    if (paleamo < 1) paleamo = 1;
    gX_F32 palespd = bg->palette.speed.value;
    gX_F32 palefre = bg->palette.frequency.value;
    gX_BG_SHADER_GETVAL_V4(col1,palette,color1)
    gX_BG_SHADER_GETVAL_V4(col2,palette,color2)
    gX_F32 bgbound = bg->palette.bgBoundary.value;
    gX_BG_SHADER_GETVAL_V4(bgcol,palette,bgColor)
    gX_F32 fgbound = bg->palette.fgBoundary.value;
    gX_BG_SHADER_GETVAL_V4(fgcol,palette,fgColor)
    
    gX_BG_SHADER_GETVAL_V4(boundxy,misc,boundaryXY)
    gX_BG_SHADER_GETVAL_V2(boundz,misc,boundaryZ)
    gX_BG_SHADER_GETVAL_V2(modul,misc,modulo)
    
    gX_BG_SHADER_SETVAL(Time,           &time,          FLOAT)
    gX_BG_SHADER_SETVAL(Res,            res,            VEC2)
    gX_BG_SHADER_SETVAL(InputTextureRes,tex0res,        VEC2)
    gX_BG_SHADER_SETVAL(InputTexture2Res,tex2res,       VEC2)
    
    gX_BG_SHADER_SETVAL(Scale,          &scale,         FLOAT)
    gX_BG_SHADER_SETVAL(ScaleOrigin,    scaleorigin,    VEC2)
    gX_BG_SHADER_SETVAL(RotateOrigin,   rotateorigin,   VEC2)
    gX_BG_SHADER_SETVAL(Perspective,    perspective,    VEC2)
    gX_BG_SHADER_SETVAL(Translate,      translate,      VEC2)
    
    gX_BG_SHADER_SETVAL(PalMode,   &(bg->palette.mode), INT)
    gX_BG_SHADER_SETVAL(PalRowCount, &bg->palette.rowCount, INT)
    gX_BG_SHADER_SETVAL(PalAmo,        &paleamo,        INT)
    gX_BG_SHADER_SETVAL(PalSpd,        &palespd,        FLOAT)
    gX_BG_SHADER_SETVAL(PalFre,        &palefre,        FLOAT)
    gX_BG_SHADER_SETVAL(PalCol1,           col1,        VEC4)
    gX_BG_SHADER_SETVAL(PalCol2,           col2,        VEC4)
    gX_BG_SHADER_SETVAL(PalBgBound,    &bgbound,        FLOAT)
    gX_BG_SHADER_SETVAL(PalBgCol,         bgcol,        VEC4)
    gX_BG_SHADER_SETVAL(PalFgBound,    &fgbound,        FLOAT)
    gX_BG_SHADER_SETVAL(PalFgCol,         fgcol,        VEC4)

    //opencode: upload palette_size — was missing (mode 0 uses this for factor calculation)
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationPalSize, (gX_I32)bg->palette.size.value);

    //opencode: upload blend + color math uniforms
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationBlendMode, bg->palette.blendMode);
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationColorMathMode, bg->palette.colorMathMode);
    {
        gX_F32 mask[3] = {bg->palette.colorMathMask.x, bg->palette.colorMathMask.y, bg->palette.colorMathMask.z};
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationColorMathMask, mask, gX_SHADER_UNIFORM_VEC3);
    }
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationLayer2Enabled,
        (bg->texture.wrapper2.loadingState.loaded && bg->texture.wrapper2.texture.id > 0) ? 1 : 0);

    //opencode: upload colors[16] array — only when dirty (static by default)
    //   NOTE: reads .constant, not .value — ParamV4_Update is never called for colors[]
    if (bg->state.colorsDirty) {
        gX_F32 palColors[gX_BG_PALETTE_COLORS_MAX * 4];
        for (gX_I32 i = 0; i < gX_BG_PALETTE_COLORS_MAX; i++) {
            palColors[i*4 + 0] = bg->palette.colors[i].constant.x;
            palColors[i*4 + 1] = bg->palette.colors[i].constant.y;
            palColors[i*4 + 2] = bg->palette.colors[i].constant.z;
            palColors[i*4 + 3] = bg->palette.colors[i].constant.w;
        }
        gX_Shader_SetValue_Array(bg->shader.wrapper.shader,
            bg->shader.locations.locationPalColors,
            (gX_I0*)palColors, gX_SHADER_UNIFORM_VEC4, gX_BG_PALETTE_COLORS_MAX);
        bg->state.colorsDirty = false;
    }

    gX_BG_SHADER_SETVAL(BoundaryXY,       boundxy,      VEC4)
    gX_BG_SHADER_SETVAL(BoundaryZ,        boundz,       VEC2)
    gX_BG_SHADER_SETVAL(Modulo,           modul,        VEC2)
    
    //opencode: warn once when useNewShader=1 but warp/wave flags are set (they're ignored by new shader)
    if (bg->shader.useNewShader && !bg->state.warpWaveWarned &&
        (bg->state.warpEnabled || bg->state.waveEnabled)) {
        gX_TraceLog_BACKGROUND(gX_LOG_WARNING,
            "BG: warp/wave flags ignored when useNewShader=1 — "
            "configure sinusoids[0..7] instead (0-2=linear, 3-5=radial, 6-7=Z).");
        bg->state.warpWaveWarned = true;
    }

    gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationParamEnable, bg->state.paramEnabled);
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationPaletteEnable, bg->state.paletteEnabled);
    //opencode: boundaryEnabled master switch — overrides sub-flags when false
    {
        gX_I1 effBoundaryXY = bg->state.boundaryEnabled ? bg->state.boundaryXYEnabled : 0;
        gX_I1 effBoundaryZ  = bg->state.boundaryEnabled ? bg->state.boundaryZEnabled : 0;
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationBoundaryXYEnable, effBoundaryXY);
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationBoundaryZEnable, effBoundaryZ);
    }
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader,bg->shader.locations.locationModuloEnable, bg->state.moduloEnabled);
    
    //opencode: new shader uploads — matrix always uploaded, sinusoids only when dirty
    if (bg->shader.useNewShader) {
        gX_Shader_SetValue_Matrix(bg->shader.wrapper.shader,bg->shader.locations.locationTransformMatrix,bg->transform.transformation);
        
        //opencode: skip sinusoid upload when no values changed (static presets save 104 SetValue calls/frame)
        if (bg->state.sinusoidDirty) {
            for (gX_I32 i = 0; i < (gX_I32)gX_BG_SINUSOID_COUNT; i++) {
                gX_F32 amp[4] = {bg->special.sinusoids[i].amplitude.value.x,bg->special.sinusoids[i].amplitude.value.y,bg->special.sinusoids[i].amplitude.value.z,bg->special.sinusoids[i].amplitude.value.w};
                gX_F32 fre[4] = {bg->special.sinusoids[i].frequency.value.x,bg->special.sinusoids[i].frequency.value.y,bg->special.sinusoids[i].frequency.value.z,bg->special.sinusoids[i].frequency.value.w};
                gX_F32 pha[4] = {bg->special.sinusoids[i].phase.value.x,bg->special.sinusoids[i].phase.value.y,bg->special.sinusoids[i].phase.value.z,bg->special.sinusoids[i].phase.value.w};
                gX_F32 spd[4] = {bg->special.sinusoids[i].speed.value.x,bg->special.sinusoids[i].speed.value.y,bg->special.sinusoids[i].speed.value.z,bg->special.sinusoids[i].speed.value.w};
                gX_F32 rwe[4] = {bg->special.sinusoids[i].regularwave.value.x,bg->special.sinusoids[i].regularwave.value.y,bg->special.sinusoids[i].regularwave.value.z,bg->special.sinusoids[i].regularwave.value.w};
                gX_F32 ile[4] = {bg->special.sinusoids[i].interlaced.value.x,bg->special.sinusoids[i].interlaced.value.y,bg->special.sinusoids[i].interlaced.value.z,bg->special.sinusoids[i].interlaced.value.w};
                gX_F32 ilm[4] = {bg->special.sinusoids[i].interlacedMultiplier.value.x,bg->special.sinusoids[i].interlacedMultiplier.value.y,bg->special.sinusoids[i].interlacedMultiplier.value.z,bg->special.sinusoids[i].interlacedMultiplier.value.w};
                gX_F32 exp = bg->special.sinusoids[i].exponent.value;
                gX_F32 dco[4] = {bg->special.sinusoids[i].dcOffset.value.x,bg->special.sinusoids[i].dcOffset.value.y,bg->special.sinusoids[i].dcOffset.value.z,bg->special.sinusoids[i].dcOffset.value.w};
                gX_F32 lin[4] = {bg->special.sinusoids[i].linear.value.x,bg->special.sinusoids[i].linear.value.y,bg->special.sinusoids[i].linear.value.z,bg->special.sinusoids[i].linear.value.w};
                gX_F32 qua[4] = {bg->special.sinusoids[i].quadratic.value.x,bg->special.sinusoids[i].quadratic.value.y,bg->special.sinusoids[i].quadratic.value.z,bg->special.sinusoids[i].quadratic.value.w}; //opencode: quadratic
                gX_F32 cen[2] = {bg->special.sinusoids[i].center.value.x, bg->special.sinusoids[i].center.value.y}; //opencode: center from ParamV2
                gX_F32 att = bg->special.sinusoids[i].attenuation.value; //opencode: radial attenuation

                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidAmp+i,amp,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidFre+i,fre,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidPha+i,pha,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidSpd+i,spd,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidRwe+i,rwe,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidIle+i,ile,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidIlm+i,ilm,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidExp+i,&exp,gX_SHADER_UNIFORM_FLOAT);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidDcO+i,dco,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidLin+i,lin,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidQuad+i,qua,gX_SHADER_UNIFORM_VEC4);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidCen+i,cen,gX_SHADER_UNIFORM_VEC2);
                gX_Shader_SetValue(bg->shader.wrapper.shader,bg->shader.locations.locationSinusoidAtt+i,&att,gX_SHADER_UNIFORM_FLOAT);
            }
            bg->state.sinusoidDirty = false;
        }

        //opencode: HDMA channel upload
        for (gX_I32 i = 0; i < (gX_I32)gX_BG_HDMA_CHANNEL_COUNT; i++) {
            gX_F32 amp = bg->special.hdma[i].amplitude.value;
            gX_F32 fre = bg->special.hdma[i].frequency.value;
            gX_F32 spd = bg->special.hdma[i].speed.value;
            gX_F32 pha = bg->special.hdma[i].phase.value;
            gX_F32 off[2] = {bg->special.hdma[i].offset.value.x, bg->special.hdma[i].offset.value.y};
            gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaAmp+i, &amp, gX_SHADER_UNIFORM_FLOAT);
            gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaFre+i, &fre, gX_SHADER_UNIFORM_FLOAT);
            gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaSpd+i, &spd, gX_SHADER_UNIFORM_FLOAT);
            gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaPha+i, &pha, gX_SHADER_UNIFORM_FLOAT);
            gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaOff+i, off, gX_SHADER_UNIFORM_VEC2);
        }
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationHdmaEnable, bg->state.hdmaEnabled);
    }

    //opencode: react uniforms
    gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationReactType,
        bg->state.reactEnabled ? bg->reactType : 0);
    {
        gX_F32 ri = bg->state.reactEnabled ? bg->reactIntensity : 0.0f;
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationReactIntensity, &ri, gX_SHADER_UNIFORM_FLOAT);
    }

    //opencode: procedural noise upload
    {
        gX_F32 nScale = bg->special.noiseScale.value;
        gX_F32 nSpeed = bg->special.noiseSpeed.value;
        gX_F32 nLac = bg->special.noiseLacunarity.value;
        gX_F32 nGain = bg->special.noiseGain.value;
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseMode, bg->special.noiseMode);
        gX_Shader_SetValue_Int(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseOctaves, bg->special.noiseOctaves);
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseScale, &nScale, gX_SHADER_UNIFORM_FLOAT);
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseSpeed, &nSpeed, gX_SHADER_UNIFORM_FLOAT);
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseLacunarity, &nLac, gX_SHADER_UNIFORM_FLOAT);
        gX_Shader_SetValue(bg->shader.wrapper.shader, bg->shader.locations.locationNoiseGain, &nGain, gX_SHADER_UNIFORM_FLOAT);
    }
    
}

inline static gX_I0 gX_RendererBackground_Update_Shader(gX_RendererBackground *bg) {
    const gX_I8 *expectedPath = bg->shader.useNewShader
        ? GX_PATH("shader", "background_new.frag")
        : GX_PATH("shader", "background.frag");
    
    //opencode: runtime toggle — reloads when useNewShader changes
    if (bg->shader.wrapper.loadingState.loaded && strcmp(bg->shader.wrapper.pathFrag,expectedPath) != 0) {
        gX_Shader_Unload(bg->shader.wrapper.shader);
        bg->shader.wrapper.loadingState.loaded = false;
        strcpy(bg->shader.wrapper.pathFrag,expectedPath);
    }
    
    //opencode: forced shader reload (hot-reload flag from ImGui/scripting)
    if (bg->state.reloadShader && bg->shader.wrapper.loadingState.loaded) {
        gX_Shader_Unload(bg->shader.wrapper.shader);
        bg->shader.wrapper.loadingState.loaded = false;
        bg->state.reloadShader = false;
    }
    
    if (bg->shader.wrapper.loadingState.loadAsap && maindata.backendLoaded && !(bg->shader.wrapper.loadingState.loaded)) {
        bg->shader.wrapper.shader = gX_Shader_Load(0,gX_Text_Format(bg->shader.wrapper.pathFrag,bg->shader.wrapper.version));
        gX_RendererBackground_Load_ShaderLocations(bg);
        bg->shader.wrapper.loadingState.loaded = true;
    }
}

inline static gX_I0 gX_RendererBackground_Update_Texture(gX_RendererBackground *bg) {
    if (bg->texture.wrapper.loadingState.loadAsap && maindata.backendLoaded && !(bg->texture.wrapper.loadingState.loaded)) {
        bg->texture.wrapper.texture = gX_Texture_Load(bg->texture.wrapper.path);
        bg->texture.wrapper.loadingState.loaded = true;
    }
    if (bg->texture.wrapper.filterChanged && bg->texture.wrapper.loadingState.loaded) {
        gX_Texture_SetFilter(bg->texture.wrapper.texture,bg->texture.wrapper.filter);
        bg->texture.wrapper.filterChanged = false;
    }
    if (bg->texture.wrapper.wrapChanged && bg->texture.wrapper.loadingState.loaded) {
        gX_Texture_SetWrap(bg->texture.wrapper.texture,bg->texture.wrapper.wrap);
        bg->texture.wrapper.wrapChanged = false;
    }

    //opencode: load second layer texture (tex2) when path is set
    if (bg->texture.wrapper2.loadingState.loadAsap && maindata.backendLoaded && !(bg->texture.wrapper2.loadingState.loaded)) {
        bg->texture.wrapper2.texture = gX_Texture_Load(bg->texture.wrapper2.path);
        bg->texture.wrapper2.loadingState.loaded = true;
    }
    if (bg->texture.wrapper2.filterChanged && bg->texture.wrapper2.loadingState.loaded) {
        gX_Texture_SetFilter(bg->texture.wrapper2.texture, bg->texture.wrapper2.filter);
        bg->texture.wrapper2.filterChanged = false;
    }
    if (bg->texture.wrapper2.wrapChanged && bg->texture.wrapper2.loadingState.loaded) {
        gX_Texture_SetWrap(bg->texture.wrapper2.texture, bg->texture.wrapper2.wrap);
        bg->texture.wrapper2.wrapChanged = false;
    }
}

gX_I0 gX_RendererBackground_Update(gX_RendererBackground *bg) {
    
    gX_RendererBackground_Update_State(bg);
    gX_RendererBackground_Update_React(bg);
    gX_RendererBackground_Transition_Update(bg);
    gX_RendererBackground_Update_Params(bg);
    gX_RendererBackground_Update_ShaderValues(bg);
    gX_RendererBackground_Update_Shader(bg);
    gX_RendererBackground_Update_Texture(bg);
    
    //opencode: --bg-imgpro flag triggers ImageProcessor pipeline
    if (arguments.hasBgImgPro && !bg->imgpro.imageUnprocessed.data) {
        gX_RendererBackground_ImgPro_Run(bg, arguments.bgImgProPath);
        arguments.hasBgImgPro = false;
    }
    
    //opencode: --bg-preset flag loads a preset at startup
    if (arguments.hasBgPreset && bg->shader.wrapper.loadingState.loaded) {
        gX_RendererBG_Preset_Load_New(bg, arguments.bgPresetPath);
        bg->state.sinusoidDirty = true;
        bg->state.paramsDirty = true;
        bg->state.colorsDirty = true;
        arguments.hasBgPreset = false;
    }

    //opencode: --bg-palette-mode overrides palette mode (for testing mode 7 row cycling)
    bg_apply_cli_palette_mode(bg);

    //opencode: --bg-react triggers a one-shot reaction for testing
    if (arguments.hasBgReact) {
        gX_RendererBackground_React(bg, arguments.bgReactType, 1.0f, 2.0f);
        arguments.hasBgReact = false;
    }

    //opencode: --bg-transition morphs current bg to the target preset
    if (arguments.hasBgTransition && bg->shader.wrapper.loadingState.loaded) {
        gX_RendererBackground_Transition_Start(bg, arguments.bgTransitionPath,
            arguments.bgTransitionDuration, GX_BG_TRANS_EASE_IN_OUT);
        arguments.hasBgTransition = false;
    }

}

//ai: upload the active Battle camera before drawing the spherical space/lava background.
gX_I0 gX_RendererBackground_Draw(gX_RendererBackground *bg, gX_RenderTexture *output) {
    //opencode: pipeline-driven scenes (GFX, RAYMARCH, etc.) draw the background via
    //   gX_Renderer_Pipeline_Draw without going through gX_Renderer_Update, so the
    //   bg shader's loadingState.loadAsap -> Update path is never triggered. The
    //   previous rlEnableShader(0) silently no-op'd for the un-loaded shader; the
    //   new rlSetShader(0, NULL) properly flushes the batch and crashes on the next
    //   EndShader call. Self-load here so Draw is self-contained.
    gX_RendererBackground_Update(bg);
    
    if (output) {
        gX_Drawing_BeginTexture(*output);
    }
    else gX_Drawing_BeginTexture(renderer.canvas);
    
        gX_Drawing_ClearBG(gX_BLACK);
        
        gX_Drawing_BeginShader(bg->shader.wrapper.shader);

            if (bg->shader.useNewShader) {
                gX_I1 battleViewEnabled = maindata.masterIndex == gX_SCENE_BATTLE
                    && bg->state.battleProjectionEnabled;
                gX_Shader_SetValue_Int(bg->shader.wrapper.shader,
                    bg->shader.locations.locationBattleViewEnabled,
                    battleViewEnabled ? 1 : 0);
                if (battleViewEnabled) {
                    gX_F32 cameraPosition[3] = {
                        camera.camera3D.position.x,
                        camera.camera3D.position.y,
                        camera.camera3D.position.z};
                    gX_F32 cameraTarget[3] = {
                        camera.camera3D.target.x,
                        camera.camera3D.target.y,
                        camera.camera3D.target.z};
                    gX_F32 cameraUp[3] = {
                        camera.camera3D.up.x,
                        camera.camera3D.up.y,
                        camera.camera3D.up.z};
                    gX_F32 cameraFov = camera.camera3D.fovy;
                    gX_Shader_SetValue(bg->shader.wrapper.shader,
                        bg->shader.locations.locationBattleCameraPosition,
                        cameraPosition, gX_SHADER_UNIFORM_VEC3);
                    gX_Shader_SetValue(bg->shader.wrapper.shader,
                        bg->shader.locations.locationBattleCameraTarget,
                        cameraTarget, gX_SHADER_UNIFORM_VEC3);
                    gX_Shader_SetValue(bg->shader.wrapper.shader,
                        bg->shader.locations.locationBattleCameraUp,
                        cameraUp, gX_SHADER_UNIFORM_VEC3);
                    gX_Shader_SetValue(bg->shader.wrapper.shader,
                        bg->shader.locations.locationBattleCameraFov,
                        &cameraFov, gX_SHADER_UNIFORM_FLOAT);
                }
            }
        
            //opencode: use ImageProcessor index texture (tex0) when available.
            //   NOTE: use explicit texture slots (SetValue_TextureSlot) instead of
            //   SetValue_Texture — raylib's SetShaderValueTexture uses the shared
            //   RLGL.State.texUnitsCounter which collides with material map units
            //   and the batch's default texture unit, silently binding the wrong
            //   texture (seen as black tex0 while tex1 palette still works).
            {
                gX_Texture tex0 = bg->texture.wrapper.texture;
                if (bg->imgpro.textureUnprocessed.id > 0)
                    tex0 = bg->imgpro.textureUnprocessed;
                gX_Shader_SetValue_TextureSlot(bg->shader.wrapper.shader,
                    bg->shader.locations.locationInputTexture, tex0, 0);
            }
            //opencode: bind second layer texture (tex2) when loaded
            if (bg->texture.wrapper2.loadingState.loaded &&
                bg->texture.wrapper2.texture.id > 0 &&
                bg->shader.locations.locationInputTexture2 >= 0) {
                gX_Shader_SetValue_TextureSlot(bg->shader.wrapper.shader,
                    bg->shader.locations.locationInputTexture2,
                    bg->texture.wrapper2.texture, 2);
            }
            //opencode: bind palette texture (tex1) when palette mode 2 or 7 is active
            if ((bg->palette.mode == 2 || bg->palette.mode == 7) &&
                bg->shader.locations.locationInputPalette >= 0 &&
                bg->palette.paletteTexture.id > 0) {
                gX_Shader_SetValue_TextureSlot(bg->shader.wrapper.shader,
                    bg->shader.locations.locationInputPalette,
                    bg->palette.paletteTexture, 1);
            }
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
        
    gX_Drawing_EndTexture();
}
