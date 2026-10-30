/**********************************************************************************************
* 
*   [wrappers/image.h]
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
//opencode: wrapper for image functions
#ifndef WRAPPERS_IMAGE_H
#define WRAPPERS_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"
#include "../common/types.h"

//loading and unloading
gX_Image  gX_Image_Load(             const gX_I8 *path);
gX_Image  gX_Image_LoadRaw(          const gX_I8 *path, gX_I32 width, gX_I32 height, gX_I32 format, gX_I32 headerSize);
gX_Image  gX_Image_LoadAnim(         const gX_I8 *path, gX_I32 *frames);
gX_Image  gX_Image_LoadAnimFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize, gX_I32 *frames);
gX_Image  gX_Image_LoadFromMemory(   const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize);
gX_Image  gX_Image_LoadFromTexture(  gX_Texture texture);
gX_Image  gX_Image_LoadFromScreen(   gX_I0);
gX_Image  gX_Image_GenColor(         gX_I32 width, gX_I32 height, gX_Color color);
gX_I0     gX_Image_Unload(           gX_Image image);

//export
gX_I1     gX_Image_Export(           gX_Image image, const gX_I8 *path);
gX_I1     gX_Image_ExportAsCode(     gX_Image image, const gX_I8 *path);

//copy and sub-image
gX_Image  gX_Image_Copy(             gX_Image image);
gX_Image  gX_Image_FromImage(        gX_Image image, gX_Rectangle rect);
gX_Image  gX_Image_FromChannel(      gX_Image image, gX_I32 selectedChannel);

//text to image
gX_Image  gX_Image_Text(             const gX_I8 *text, gX_I32 fontSize, gX_Color color);
gX_Image  gX_Image_TextEx(           gX_Font font, const gX_I8 *text, gX_F32 fontSize, gX_F32 spacing, gX_Color tint);

//crop and alpha
gX_I0     gX_Image_Crop(             gX_Image *image, gX_Rectangle crop);
gX_I0     gX_Image_AlphaCrop(        gX_Image *image, gX_F32 threshold);
gX_I0     gX_Image_AlphaClear(       gX_Image *image, gX_Color color, gX_F32 threshold);
gX_I0     gX_Image_AlphaMask(        gX_Image *image, gX_Image alphaMask);
gX_I0     gX_Image_AlphaPremultiply( gX_Image *image);

//blur
gX_I0     gX_Image_BlurGaussian(     gX_Image *image, gX_I32 blurSize);

//resize
gX_I0     gX_Image_Resize(           gX_Image *image, gX_I32 newWidth, gX_I32 newHeight);
gX_I0     gX_Image_ResizeNN(         gX_Image *image, gX_I32 newWidth, gX_I32 newHeight);
gX_I0     gX_Image_ResizeCanvas(     gX_Image *image, gX_I32 newWidth, gX_I32 newHeight, gX_I32 offsetX, gX_I32 offsetY, gX_Color fill);

//mipmaps and dither
gX_I0     gX_Image_GenerateMipmaps(  gX_Image *image);
gX_I0     gX_Image_Dither(           gX_Image *image, gX_I32 rBpp, gX_I32 gBpp, gX_I32 bBpp, gX_I32 aBpp);

//flip and rotate
gX_I0     gX_Image_FlipVertical(     gX_Image *image);
gX_I0     gX_Image_FlipHorizontal(   gX_Image *image);
gX_I0     gX_Image_Rotate(           gX_Image *image, gX_I32 degrees);
gX_I0     gX_Image_RotateCW(         gX_Image *image);
gX_I0     gX_Image_RotateCCW(        gX_Image *image);

//color manipulation
gX_I0     gX_Image_ColorTint(        gX_Image *image, gX_Color color);
gX_I0     gX_Image_ColorInvert(      gX_Image *image);
gX_I0     gX_Image_ColorGrayscale(   gX_Image *image);
gX_I0     gX_Image_ColorContrast(    gX_Image *image, gX_F32 contrast);
gX_I0     gX_Image_ColorBrightness(  gX_Image *image, gX_I32 brightness);
gX_I0     gX_Image_ColorReplace(     gX_Image *image, gX_Color color, gX_Color replace);

//format
gX_I0     gX_Image_Format(           gX_Image *image, gX_I32 newFormat);
gX_I0     gX_Image_ToPOT(            gX_Image *image, gX_Color fill);

//pixel data
gX_Color *gX_Image_LoadColors(      gX_Image image);
gX_Color *gX_Image_LoadPalette(     gX_Image image, gX_I32 maxPaletteSize, gX_I32 *colorCount);
gX_I0     gX_Image_UnloadColors(    gX_Color *colors);
gX_I0     gX_Image_UnloadPalette(   gX_Color *colors);

gX_Rectangle gX_Image_GetAlphaBorder(gX_Image image, gX_F32 threshold);

//drawing into images
gX_I0     gX_Image_Draw(            gX_Image *dst, gX_Image src, gX_Rectangle srcRec, gX_Rectangle dstRec, gX_Color tint);
gX_I0     gX_Image_DrawRectangle(   gX_Image *dst, gX_I32 posX, gX_I32 posY, gX_I32 width, gX_I32 height, gX_Color color);
gX_I0     gX_Image_DrawText(        gX_Image *dst, const gX_I8 *text, gX_F32v2 position, gX_I32 fontSize, gX_Color color);
gX_I0     gX_Image_DrawTextEx(      gX_Image *dst, gX_Font font, const gX_I8 *text, gX_F32v2 position, gX_F32 fontSize, gX_F32 spacing, gX_Color tint);
gX_I0     gX_Image_DrawPixel(       gX_Image *dst, gX_I32 x, gX_I32 y, gX_Color color);

//texture from image and update
gX_Texture gX_LoadTextureFromImage( gX_Image image);
gX_I0      gX_UpdateTexture(        gX_Texture texture, const gX_I0 *pixels);
gX_I0      gX_UpdateTextureRec(     gX_Texture texture, gX_Rectangle rec, const gX_I0 *pixels);

#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_IMAGE_H
