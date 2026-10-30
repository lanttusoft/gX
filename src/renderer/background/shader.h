/**********************************************************************************************
*
*   [gX: src/renderer/background/shader.h]
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


#ifndef RENDERER_BACKGROUND_SHADER_H
#define RENDERER_BACKGROUND_SHADER_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../../common/types.h"
#include "../../common/text.h"
#include "../../wrappers/shader.h"
    
typedef struct gX_RendererBackground_Shader_Locations {
    gX_I32 locationTime;
    gX_I32 locationRes;
    //ai: camera inputs for the Battle spherical backdrop projection.
    gX_I32 locationBattleViewEnabled;
    gX_I32 locationBattleCameraPosition;
    gX_I32 locationBattleCameraTarget;
    gX_I32 locationBattleCameraUp;
    gX_I32 locationBattleCameraFov;
    
    gX_I32 locationInputTexture;
    gX_I32 locationInputTexture2; //opencode: second layer texture (tex2)
    gX_I32 locationInputPalette;
    gX_I32 locationInputTextureRes;
    gX_I32 locationInputTexture2Res; //opencode: second layer resolution (tex2res)
    gX_I32 locationInputPaletteRes;
    
    gX_I32 locationScale;
    gX_I32 locationScaleOrigin;
    gX_I32 locationRotate;
    gX_I32 locationRotateOrigin;
    gX_I32 locationStretch;
    gX_I32 locationPerspective;
    gX_I32 locationTranslate;
    gX_I32 locationAffine;
    gX_I32 locationTransformMatrix;
    
    gX_I32 locationWarp;
    gX_I32 locationWarpExp;
    gX_I32 locationWaveAmp;
    gX_I32 locationWaveFre;
    gX_I32 locationWaveRwe;
    gX_I32 locationWaveIle;
    gX_I32 locationWaveIlm;
    gX_I32 locationWaveSpd;
    gX_I32 locationWavePha;
    gX_I32 locationRipple;
    gX_I32 locationSpiral;
    
    //opencode: sinusoid uniform array base locations (indexed 0-5 in new shader)
    gX_I32 locationSinusoidAmp;
    gX_I32 locationSinusoidFre;
    gX_I32 locationSinusoidPha;
    gX_I32 locationSinusoidSpd;
    gX_I32 locationSinusoidRwe;
    gX_I32 locationSinusoidIle;
    gX_I32 locationSinusoidIlm;
    gX_I32 locationSinusoidExp;
    gX_I32 locationSinusoidCen; //opencode: radial sinusoid center point
    gX_I32 locationSinusoidDcO; //opencode: sinusoid DC offset
    gX_I32 locationSinusoidLin; //opencode: sinusoid linear coefficient
    gX_I32 locationSinusoidQuad; //opencode: sinusoid quadratic coefficient
    gX_I32 locationSinusoidAtt; //opencode: sinusoid radial attenuation
    
    //opencode: procedural noise uniforms
    gX_I32 locationNoiseMode;
    gX_I32 locationNoiseScale;
    gX_I32 locationNoiseSpeed;
    gX_I32 locationNoiseOctaves;
    gX_I32 locationNoiseLacunarity;
    gX_I32 locationNoiseGain;
    
    gX_I32 locationPalMode;
    gX_I32 locationPalSize;
    gX_I32 locationPalRowCount;
    gX_I32 locationPalColors;
    gX_I32 locationPalAmo;
    gX_I32 locationPalSpd;
    gX_I32 locationPalFre;
    gX_I32 locationPalCol1;
    gX_I32 locationPalCol2;
    gX_I32 locationPalBgBound;
    gX_I32 locationPalFgBound;
    gX_I32 locationPalBgCol;
    gX_I32 locationPalFgCol;
    
    //opencode: dual-layer blend + color math
    gX_I32 locationBlendMode;
    gX_I32 locationColorMathMode;
    gX_I32 locationColorMathMask;
    gX_I32 locationLayer2Enabled;
    gX_I32 locationReactType;
    gX_I32 locationReactIntensity;
    
    gX_I32 locationBoundaryXY;
    gX_I32 locationBoundaryZ;
    gX_I32 locationModulo;
    
    gX_I32 locationParamEnable;
    gX_I32 locationHdmaEnable;
    gX_I32 locationHdmaAmp;
    gX_I32 locationHdmaFre;
    gX_I32 locationHdmaSpd;
    gX_I32 locationHdmaPha;
    gX_I32 locationHdmaOff;
    gX_I32 locationWarpEnable;
    gX_I32 locationWaveEnable;
    gX_I32 locationPaletteEnable;
    gX_I32 locationBoundaryXYEnable;
    gX_I32 locationBoundaryZEnable;
    gX_I32 locationModuloEnable;
} gX_RendererBackground_Shader_Locations;

//opencode: consolidated — embeds gX_Shader_Wrapper
typedef struct gX_RendererBackground_Shader {
    gX_Shader_Wrapper wrapper;
    gX_I1       useNewShader; // toggle between old and new shader
    gX_RendererBackground_Shader_Locations locations;
} gX_RendererBackground_Shader;   

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_SHADER_H
