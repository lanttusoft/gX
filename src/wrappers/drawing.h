/**********************************************************************************************
* 
*   [wrappers/drawing.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib drawing/mode functions
#ifndef WRAPPERS_DRAWING_H
#define WRAPPERS_DRAWING_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"

//opencode: drawing state — viewport rect, GL state save, etc.
extern gX_DrawingInfo drawinfo;
    
gX_I0 gX_Drawing_Begin(         gX_I0);
gX_I0 gX_Drawing_End(           gX_I0);

gX_I0 gX_Drawing_ClearBG(       gX_Color color);
gX_I0 gX_Drawing_FPS(           gX_F32v2 pos);
//opencode: execute every queued draw path NOW. Required whenever a pass mixes
//   raylib-native shapes/text (deferred to raylib's queue flush) with gX batch
//   primitives (immediate): without an explicit flush the deferred native
//   geometry lands ON TOP of batch quads regardless of source order.
gX_I0 gX_Drawing_Flush(         gX_I0);

gX_I0 gX_Drawing_Begin2D(       gX_Camera2D cam2D);
gX_I0 gX_Drawing_Begin3D(       gX_Camera3D cam3D);
gX_I0 gX_Drawing_BeginTexture(  gX_RenderTexture rt);
gX_I0 gX_Drawing_BeginShader(   gX_Shader shader);
gX_I0 gX_Drawing_BeginBlend(                gX_I32 mode);
gX_I0 gX_Drawing_BeginBlendCustomSeparate(   gX_I32 srcRGB, gX_I32 dstRGB, gX_I32 srcAlpha, gX_I32 dstAlpha);
gX_I0 gX_Drawing_BeginScissor(              gX_Rectangle rect);

//opencode: blend factor constants for use with BeginBlendCustomSeparate (matching rlgl/OpenGL values)
#define gX_BLEND_ZERO                 0
#define gX_BLEND_ONE                  1
#define gX_BLEND_SRC_COLOR            0x0300
#define gX_BLEND_ONE_MINUS_SRC_COLOR  0x0301
#define gX_BLEND_SRC_ALPHA            0x0302
#define gX_BLEND_ONE_MINUS_SRC_ALPHA  0x0303
#define gX_BLEND_DST_COLOR            0x0306
#define gX_BLEND_ONE_MINUS_DST_COLOR  0x0307

gX_I0 gX_Drawing_DrawScreenQuad(gX_F32 x, gX_F32 y, gX_F32 w, gX_F32 h);
//opencode: generic dynamic-geometry draw with the CURRENT program (caller
//   enables it). verts: interleaved x,y,u,v (4 floats per vertex). Uploads
//   mvp from the RHI stack if the program has it. Used by shape shader draws.
gX_I0 gX_Drawing_DrawGeometry(const gX_F32 *verts, gX_U32 vertCount, gX_U32 primMode);
gX_I0 gX_Drawing_DisableCulling(gX_I0);
gX_I0 gX_Drawing_EnableCulling( gX_I0);

gX_I0 gX_Drawing_End2D(         gX_I0);
gX_I0 gX_Drawing_End3D(         gX_I0);
gX_I0 gX_Drawing_EndTexture(    gX_I0);
gX_I0 gX_Drawing_EndShader(     gX_I0);
gX_I0 gX_Drawing_EndBlend(      gX_I0);
gX_I0 gX_Drawing_EndScissor(    gX_I0);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_DRAWING_H
