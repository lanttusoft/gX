/**********************************************************************************************
* 
*   [wrappers/text.h]
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
//lanttu: original wrapper for raylib text/font/string functions
#ifndef GX_WRAPPERS_TEXT_H
#define GX_WRAPPERS_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/types.h"
#include "../common/text.h"

//font
gX_Font gX_Font_GetDefault(gX_I0);
gX_Font gX_Font_Load(const gX_I8 *path);
gX_Font gX_Font_LoadSDF(const gX_I8 *path, gX_I32 baseSize); //opencode: v0.6.0
//opencode: soft-shadow controls for the SDF text shader (spread in distance
//   units, edge-band multiplier, alpha multiplier); defaults 0/1/1.
gX_I0 gX_RawString_SDF_SetSoft(gX_F32 spread, gX_F32 blur, gX_F32 alphaScale);
gX_Font gX_Font_LoadFromImage(gX_Image image);
gX_Font gX_Font_LoadFromMemory(gX_Image image);
gX_I1   gX_Font_IsReady(gX_Font font);
gX_I0   gX_Font_Unload(gX_Font font);
gX_I32  gX_Font_GetGlyphIndex(gX_Font font, gX_I32 codepoint);
gX_I32  gX_Font_GetGlyphInfo(gX_Font font, gX_I32 codepoint);
gX_I32  gX_Font_GetGlyphAtlasRec(gX_Font font, gX_I32 codepoint);
gX_Font gX_Font_LoadEx(const gX_I8 *path, gX_I32 fontSize, gX_I32 *codepoints, gX_I32 codepointCount); //opencode

//rawstring
const gX_I8 *gX_RawString_Format(   const gX_I8 *format, ...);
gX_I32       gX_RawString_Copy(     gX_I8 *dst, const gX_I8 *src);
gX_I1        gX_RawString_IsEqual(  const gX_I8 *str1, const gX_I8 *str2);
gX_U32       gX_RawString_Length(   const gX_I8 *string);
const gX_I8 *gX_RawString_SubString(const gX_I8 *string,       gX_I32 pos,           gX_U32  len);
gX_I8       *gX_RawString_Replace(        gX_I8 *string, const gX_I8 *replace, const gX_I8  *by);
gX_I8       *gX_RawString_Insert(   const gX_I8 *string, const gX_I8 *insert,        gX_I32  pos);
const gX_I8 *gX_RawString_Join(     const gX_I8 **stringList,  gX_I32 count,   const gX_I8  *delimiter);
gX_I8      **gX_RawString_Split(   const gX_I8 *string,       gX_I8  delimiter,     gX_I32 *count);
gX_I0        gX_RawString_Append(         gX_I8 *string, const gX_I8 *append,        gX_I32 *pos);
gX_I32       gX_RawString_FindIndex(const gX_I8 *string, const gX_I8 *find);
const gX_I8 *gX_RawString_ToUpper(  const gX_I8 *string);
const gX_I8 *gX_RawString_ToLower(  const gX_I8 *string);
const gX_I8 *gX_RawString_ToPascal( const gX_I8 *string);
gX_U32       gX_RawString_ToInteger(const gX_I8 *string);
#define gX_Text_Format gX_RawString_Format

gX_F32v2 gX_RawString_Measure(const gX_I8 *string, gX_Font font, gX_F32 fontsize, gX_F32 spacing);
gX_I0 gX_RawString_Draw_Lesser(const gX_I8 *string, gX_F32v2 position, gX_F32 fontsize, gX_Color tint);
gX_I0 gX_RawString_Draw(const gX_I8 *string, gX_Font font, gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize, gX_F32 spacing, gX_Color tint);

gX_I0 gX_RawString_Draw_Simple(const gX_I8 *string);
#define gX_Text_Draw_Simple gX_RawString_Draw_Simple

//string
gX_String gX_String_Format(const gX_I8 *format, ...);
gX_I32    gX_String_Copy(  gX_String dst, gX_String src);

//text
gX_F32v2 gX_Text_Measure(gX_Text *text);
gX_I0 gX_Text_Draw(gX_Text text);

#ifdef __cplusplus
}
#endif

#endif //GX_WRAPPERS_TEXT_H
