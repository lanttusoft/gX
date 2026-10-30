/**********************************************************************************************
*
*   [gX: src/common/utils.h]
*
*   Copyright (c) 2025 lanttu
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

#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include <stddef.h>
    
#include "types.h"
#include "text.h"
    
#include "utils/pointercast.h"
#include "utils/shader.h"
    
#include "config.h" //opencode: gX_ENABLE_GAME guard below
#if gX_ENABLE_GAME == 1
    #include "../game/character.h"
#endif
    
#include "marshal/json_struct.h"
  
size_t gX_Struct_GetSize(gX_I32 id);

gX_I0 gX_Rectangle_MultiGradient(gX_Rectangle rect, gX_ColorGradientArray array);

//opencode: soft shadow — a 4-tap ring plus the center copy, each at a
//   fraction of the shadow alpha, then the text.
gX_I0 gX_RawString_Draw_ShadowSoft(const gX_I8 *string, gX_Font font,
    gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize,
    gX_F32 spacing, gX_Color tint, gX_F32v2 shadowOffset,
    gX_Color shadowColor, gX_F32 softness);
//opencode: 8-way 1px outline copies under the glyphs (theme-gated; the text
//   itself is drawn by the caller afterwards).
gX_I0 gX_RawString_Draw_Outline(const gX_I8 *string, gX_Font font,
    gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize,
    gX_F32 spacing, gX_F32 outlineWidth, gX_Color outlineColor);
gX_I0 gX_RawString_Draw_Shadow(const gX_I8 *string, gX_Font font,
                               gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation,
                               gX_F32 fontsize, gX_F32 spacing, gX_Color tint,
                               gX_F32v2 shadowOffset, gX_Color shadowColor);

gX_I0 gX_Text_Draw_Shadow(gX_Text text, gX_F32v2 shadowOffset, gX_Color shadowColor);

gX_I0 gX_Model_AttachShader(gX_Model *model, gX_Shader shader);
gX_I0 gX_Model_AttachTexture(gX_Model *model, gX_Texture texture);


gX_Texture gX_Texture_Load_Parameters(const gX_I8 *path, gX_I32 filter, gX_I32 wrap);
gX_RenderTexture gX_RenderTexture_Load_Parameters(gX_U32 width, gX_U32 height, gX_I32 filter, gX_I32 wrap);

gX_I0 gX_PathGen(gX_I8 *target, gX_I8 *filename, gX_U32 filetype);

gX_Color gX_Color_Random(gX_I0);

#if gX_ENABLE_GAME == 1
gX_I0 gX_DrawBillboard_CharacterTextures(gX_Character chara, gX_Camera3D cam);
#endif

gX_I32 gX_LoadingState_Handler(gX_LoadingState *state, gX_I0 (*loadf)(), gX_I0 (*unloadf)(), gX_I0 *wra);

gX_I8 *gX_Text_Split_GetLast(const gX_I8 *string, gX_I8 delimiter);

gX_I0 gX_Text_Screenshot(gX_I8 *text);

gX_I32 gX_String_JSON_Object(gX_I8 *bfr, gX_JSON_Object *obj);

#ifdef __cplusplus
}
#endif

#endif //UTILS_H
