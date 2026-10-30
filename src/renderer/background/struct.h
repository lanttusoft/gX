/**********************************************************************************************
*
*   [gX: src/renderer/background/struct.h]
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


#ifndef RENDERER_BACKGROUND_STRUCT_H
#define RENDERER_BACKGROUND_STRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../common/text.h"
    
#include "shader.h"
#include "transition.h"
#include "../../wrappers/texture.h"

typedef struct gX_RendererBackground_State {
    gX_I1 load;
    gX_I1 unload;
    gX_I1 loaded;
    gX_I1 unloadafter;
    
    gX_I1 paramEnabled;
    gX_I1 warpEnabled;
    gX_I1 waveEnabled;
    gX_I1 paletteEnabled;
    gX_I1 boundaryEnabled;
    gX_I1 boundaryXYEnabled;
    gX_I1 boundaryZEnabled;
    gX_I1 moduloEnabled;
    
    gX_I1 useTransformMatrix;
    gX_I1 useDirectMatrix; //opencode: bypass param system, use matrixA/B with blend
    gX_I1 sinusoidDirty; //opencode: skip sinusoid upload when no values changed
    gX_I1 paramsDirty; //opencode: skip param Update when no motion detected
    gX_I1 colorsDirty; //opencode: skip colors[16] upload when unchanged
    gX_I1 reloadShader; //opencode: force shader reload next frame
    gX_I1 warpWaveWarned; //opencode: one-time warning about warp/wave vs sinusoids
    gX_I1 hdmaEnabled; //opencode: enable HDMA scanline wave distortion
    gX_I1 reactEnabled; //opencode: reaction system active (any react in progress)
    //ai: Battle-only spherical projection and procedural lava background mode.
    gX_I1 battleProjectionEnabled;
} gX_RendererBackground_State;

gX_I0 gX_RendererBackground_State_Reset(gX_RendererBackground_State *sta);

typedef struct gX_RendererBackground_Transform {
    gX_RenBG_ParamScalar    scale;
    gX_RenBG_ParamV2        scaleOrigin;
    gX_RenBG_ParamScalar    rotate;
    gX_RenBG_ParamV2        rotateOrigin;
    gX_RenBG_ParamV4        stretch;
    gX_RenBG_ParamV2        perspective;
    gX_RenBG_ParamV2        translate;
    gX_RenBG_ParamV2        affine;
    gX_RenBG_ParamScalar    matrixBlend; //opencode: blend 0..1 between matrixA and matrixB
    gX_Matrix               matrixA; //opencode: direct matrix A
    gX_Matrix               matrixB; //opencode: direct matrix B
    gX_Matrix               transformation;
} gX_RendererBackground_Transform;

gX_I0 gX_RendererBackground_Transform_Reset(gX_RendererBackground_Transform *tra);

typedef struct gX_RendererBackground_Special {
    gX_RenBG_ParamV3        warp; //linearscale (linear sinusoids need center lines for x,y)  + radialscale dcoffset
    gX_RenBG_ParamScalar    warpExponent;
    gX_RenBG_ParamV4        waveAmplitude; //lineartranslate
    gX_RenBG_ParamV4        waveFrequency;
    gX_RenBG_ParamV4        waveRegularwave;
    gX_RenBG_ParamV4        waveInterlaced;
    gX_RenBG_ParamV4        waveInterlacedMultiplier;
    gX_RenBG_ParamV4        waveSpeed;
    gX_RenBG_ParamV4        wavePhase;
    
    //opencode: 8 sinusoids for new shader (linear/radial × translate/scale/rotate + linearZ/radialZ)
    gX_RenBG_Sinusoid       sinusoids[gX_BG_SINUSOID_COUNT];
    
    //opencode: 8 HDMA channels — per-scanline wave distortion (SNES HDMA-style)
    gX_RenBG_HDMA_Channel   hdma[gX_BG_HDMA_CHANNEL_COUNT];
    
    //opencode: procedural noise (0=off 1=simplex 2=fractal 3=voronoi 4=turbulence)
    gX_I32                  noiseMode;
    gX_RenBG_ParamScalar    noiseScale;
    gX_RenBG_ParamScalar    noiseSpeed;
    gX_I32                  noiseOctaves;
    gX_RenBG_ParamScalar    noiseLacunarity;
    gX_RenBG_ParamScalar    noiseGain;
    
} gX_RendererBackground_Special;

gX_I0 gX_RendererBackground_Special_Reset(gX_RendererBackground_Special *spe);

typedef struct gX_RendererBackground_Palette {
    gX_I32                  mode;
    gX_I32                  rowCount; //opencode: rows for mode 7 (SNES row cycling)
    //mode 0: use a color array
    gX_RenBG_ParamScalar    size;
    gX_RenBG_ParamV4        colors[gX_BG_PALETTE_COLORS_MAX];
    //mode 1: use two colors and create a gradient in between
    gX_RenBG_ParamScalar    amount;
    gX_RenBG_ParamScalar    speed;
    gX_RenBG_ParamScalar    frequency;
    gX_RenBG_ParamV4        color1;
    gX_RenBG_ParamV4        color2;
    gX_RenBG_ParamScalar    bgBoundary;
    gX_RenBG_ParamV4        bgColor;
    gX_RenBG_ParamScalar    fgBoundary;
    gX_RenBG_ParamV4        fgColor;
    //mode 2: use a second texture for palette sampling
    gX_Texture              paletteTexture;
    
    //opencode: dual-layer blend mode (0=alpha 1=add 2=sub 3=mul 4=screen 5=diff 6=overlay 7=dodge)
    gX_I32                  blendMode;
    //opencode: SNES-style color math (0=off 1=add 2=subtract 3=half_add)
    gX_I32                  colorMathMode;
    gX_F32v3                colorMathMask; // per-channel enable
} gX_RendererBackground_Palette;

gX_I0 gX_RendererBackground_Palette_Reset(gX_RendererBackground_Palette *pal);

typedef struct gX_RendererBackground_Misc {
    gX_RenBG_ParamV4     boundaryXY;
    gX_RenBG_ParamV2     boundaryZ;
    gX_RenBG_ParamV2     modulo;
} gX_RendererBackground_Misc;

gX_I0 gX_RendererBackground_Misc_Reset(gX_RendererBackground_Misc *misc);

//opencode: background transition — morphs all params from current state to a
//   target preset over duration seconds. Snapshot source, load target preset,
//   then lerp every param field between the two snapshots each frame.
typedef struct gX_RendererBackground_Transition {
    gX_I1  active;
    gX_I1  loadedTarget;
    gX_F32 duration;
    gX_F32 elapsed;
    gX_BG_Transition_Ease ease;

    gX_RendererBackground_Transform srcTransform;
    gX_RendererBackground_Special   srcSpecial;
    gX_RendererBackground_Palette   srcPalette;
    gX_RendererBackground_Misc      srcMisc;

    gX_RendererBackground_Transform dstTransform;
    gX_RendererBackground_Special   dstSpecial;
    gX_RendererBackground_Palette   dstPalette;
    gX_RendererBackground_Misc      dstMisc;
} gX_RendererBackground_Transition;

//opencode: consolidated — embeds gX_Texture_Wrapper for testTexture; keeps input/output RTs separate
typedef struct gX_RendererBackground_Texture {
    gX_Texture_Wrapper  wrapper;
    gX_Texture_Wrapper  wrapper2; //opencode: second layer texture (tex2)
    gX_F32v2            inputRes;
    gX_RenderTexture   *input;
    gX_F32v2            outputRes;
    gX_RenderTexture    output;
} gX_RendererBackground_Texture;

//image -> (palette index texture [b/w], sorted palette according to index)
//use mode 2
typedef struct gX_RendererBackground_ImageProcessor {
    gX_Image            imageUnprocessed;
    gX_Image            imageResult;
    gX_Texture          textureUnprocessed;
    gX_Texture          textureResult;
    
    gX_Color            palettePivot1;
    gX_Color            palettePivot2;
    
    gX_U32              paletteSize;
    gX_U32              paletteMax;
    gX_Color           *paletteUnsorted;
    gX_Color           *paletteSorted;
    gX_F32v4           *paletteResult;
} gX_RendererBackground_ImageProcessor;

//typedef struct gX_RendererBackground_Pipe;

typedef struct gX_RendererBackground {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_RendererBackground_State     state;
    
    gX_RendererBackground_Transform transform;
    gX_RendererBackground_Special   special;
    gX_RendererBackground_Palette   palette;
    gX_RendererBackground_Misc      misc;
    
    gX_RendererBackground_Shader    shader;
    gX_RendererBackground_Texture   texture;
    
    gX_RendererBackground_ImageProcessor imgpro;


    //opencode: game-event reaction system (0=none 1=pulse 2=shake 3=flash 4=warp)
    gX_I32                  reactType;
    gX_F32                  reactIntensity; // 0..1, decays over reactDuration
    gX_F32                  reactTimer;
    gX_F32                  reactDuration;

    //opencode: preset morph transition
    gX_RendererBackground_Transition transition;
    
} gX_RendererBackground;

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_STRUCT_H
