/**********************************************************************************************
* 
*   [wrappers/image.c]
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
#include "wrappers_header.h"
#include "image.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//opencode: GX — minimal raw pixel helpers
static gX_I0 *gX_Image_AllocData(gX_I32 width, gX_I32 height) {
    return calloc(1, (size_t)(width * height * 4));
}

//opencode: gX_Image_Load — load raw RGBA data from file
gX_Image gX_Image_Load(const gX_I8 *path) {
    Image img = LoadImage(path);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadRaw — wraps LoadImageRaw
gX_Image gX_Image_LoadRaw(const gX_I8 *path, gX_I32 width, gX_I32 height, gX_I32 format, gX_I32 headerSize) {
    Image img = LoadImageRaw(path, width, height, format, headerSize);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadAnim — wraps LoadImageAnim
gX_Image gX_Image_LoadAnim(const gX_I8 *path, gX_I32 *frames) {
    Image img = LoadImageAnim(path, frames);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadAnimFromMemory — wraps LoadImageAnimFromMemory
gX_Image gX_Image_LoadAnimFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize, gX_I32 *frames) {
    Image img = LoadImageAnimFromMemory(fileType, (const unsigned char *)fileData, dataSize, frames);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadFromMemory — wraps LoadImageFromMemory
gX_Image gX_Image_LoadFromMemory(const gX_I8 *fileType, const gX_U8 *fileData, gX_I32 dataSize) {
    Image img = LoadImageFromMemory(fileType, (const unsigned char *)fileData, dataSize);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadFromScreen — wraps LoadImageFromScreen
gX_Image gX_Image_LoadFromScreen(gX_I0) {
    Image img = LoadImageFromScreen();
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_LoadFromTexture — wraps LoadImageFromTexture
gX_Image gX_Image_LoadFromTexture(gX_Texture texture) {
    Image img = LoadImageFromTexture(gX_Texture_ConvertToBackend(texture));
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_GenColor — generate solid color image
gX_Image gX_Image_GenColor(gX_I32 width, gX_I32 height, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    Image img = GenImageColor(width, height, col);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_Unload — free pixel data
gX_I0 gX_Image_Unload(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    UnloadImage(img);
}

//opencode: gX_Image_Export — write raw RGBA data
gX_I1 gX_Image_Export(gX_Image image, const gX_I8 *path) {
    Image img = gX_Image_ConvertToBackend(image);
    return (gX_I1)ExportImage(img, path);
}

//opencode: gX_Image_ExportAsCode — wraps ExportImageAsCode
gX_I1 gX_Image_ExportAsCode(gX_Image image, const gX_I8 *path) {
    Image img = gX_Image_ConvertToBackend(image);
    return (gX_I1)ExportImageAsCode(img, path);
}

//opencode: gX_Image_Copy — allocate and copy pixel data
gX_Image gX_Image_Copy(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    Image copy = ImageCopy(img);
    return gX_Image_ConvertFromBackend(copy);
}

//opencode: gX_Image_FromImage — extract sub-rectangle
gX_Image gX_Image_FromImage(gX_Image image, gX_Rectangle rect) {
    Image img = gX_Image_ConvertToBackend(image);
    Rectangle r = gX_Rectangle_ConvertToBackend(rect);
    Image sub = ImageFromImage(img, r);
    return gX_Image_ConvertFromBackend(sub);
}

//opencode: gX_Image_FromChannel — extract single channel
gX_Image gX_Image_FromChannel(gX_Image image, gX_I32 selectedChannel) {
    Image img = gX_Image_ConvertToBackend(image);
    Image channel = ImageFromChannel(img, selectedChannel);
    return gX_Image_ConvertFromBackend(channel);
}

//opencode: gX_Image_Text — wraps ImageText
gX_Image gX_Image_Text(const gX_I8 *text, gX_I32 fontSize, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    Image img = ImageText(text, fontSize, col);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_TextEx — wraps ImageTextEx
gX_Image gX_Image_TextEx(gX_Font font, const gX_I8 *text, gX_F32 fontSize, gX_F32 spacing, gX_Color tint) {
    Font fnt = gX_Font_ConvertToBackend(font);
    Color col = gX_Color_ConvertToBackend(tint);
    Image img = ImageTextEx(fnt, text, fontSize, spacing, col);
    return gX_Image_ConvertFromBackend(img);
}

//opencode: gX_Image_Crop — crop image data
gX_I0 gX_Image_Crop(gX_Image *image, gX_Rectangle crop) {
    Rectangle r = gX_Rectangle_ConvertToBackend(crop);
    ImageCrop((Image *)image, r);
}

//opencode: gX_Image_AlphaCrop — wraps ImageAlphaCrop
gX_I0 gX_Image_AlphaCrop(gX_Image *image, gX_F32 threshold) {
    ImageAlphaCrop((Image *)image, threshold);
}

//opencode: gX_Image_AlphaClear — wraps ImageAlphaClear
gX_I0 gX_Image_AlphaClear(gX_Image *image, gX_Color color, gX_F32 threshold) {
    Color col = gX_Color_ConvertToBackend(color);
    ImageAlphaClear((Image *)image, col, threshold);
}

//opencode: gX_Image_AlphaMask — wraps ImageAlphaMask
gX_I0 gX_Image_AlphaMask(gX_Image *image, gX_Image alphaMask) {
    Image mask = gX_Image_ConvertToBackend(alphaMask);
    ImageAlphaMask((Image *)image, mask);
}

//opencode: gX_Image_AlphaPremultiply — wraps ImageAlphaPremultiply
gX_I0 gX_Image_AlphaPremultiply(gX_Image *image) {
    ImageAlphaPremultiply((Image *)image);
}

//opencode: gX_Image_BlurGaussian — wraps ImageBlurGaussian
gX_I0 gX_Image_BlurGaussian(gX_Image *image, gX_I32 blurSize) {
    ImageBlurGaussian((Image *)image, blurSize);
}

//opencode: gX_Image_Resize — wraps ImageResize
gX_I0 gX_Image_Resize(gX_Image *image, gX_I32 newWidth, gX_I32 newHeight) {
    ImageResize((Image *)image, newWidth, newHeight);
}

//opencode: gX_Image_ResizeNN — wraps ImageResizeNN
gX_I0 gX_Image_ResizeNN(gX_Image *image, gX_I32 newWidth, gX_I32 newHeight) {
    ImageResizeNN((Image *)image, newWidth, newHeight);
}

//opencode: gX_Image_ResizeCanvas — wraps ImageResizeCanvas
gX_I0 gX_Image_ResizeCanvas(gX_Image *image, gX_I32 newWidth, gX_I32 newHeight, gX_I32 offsetX, gX_I32 offsetY, gX_Color fill) {
    Color col = gX_Color_ConvertToBackend(fill);
    ImageResizeCanvas((Image *)image, newWidth, newHeight, offsetX, offsetY, col);
}

//opencode: gX_Image_GenerateMipmaps — wraps ImageMipmaps
gX_I0 gX_Image_GenerateMipmaps(gX_Image *image) {
    ImageMipmaps((Image *)image);
}

//opencode: gX_Image_Dither — wraps ImageDither
gX_I0 gX_Image_Dither(gX_Image *image, gX_I32 rBpp, gX_I32 gBpp, gX_I32 bBpp, gX_I32 aBpp) {
    ImageDither((Image *)image, rBpp, gBpp, bBpp, aBpp);
}

//opencode: gX_Image_FlipVertical — flip Y
gX_I0 gX_Image_FlipVertical(gX_Image *image) {
    ImageFlipVertical((Image *)image);
}

//opencode: gX_Image_FlipHorizontal — flip X
gX_I0 gX_Image_FlipHorizontal(gX_Image *image) {
    ImageFlipHorizontal((Image *)image);
}

//opencode: gX_Image_Rotate — wraps ImageRotate
gX_I0 gX_Image_Rotate(gX_Image *image, gX_I32 degrees) {
    ImageRotate((Image *)image, degrees);
}

//opencode: gX_Image_RotateCW — rotate 90 CW
gX_I0 gX_Image_RotateCW(gX_Image *image) {
    ImageRotateCW((Image *)image);
}

//opencode: gX_Image_RotateCCW — rotate 90 CCW
gX_I0 gX_Image_RotateCCW(gX_Image *image) {
    ImageRotateCCW((Image *)image);
}

//opencode: gX_Image_ColorTint — multiply each pixel by tint
gX_I0 gX_Image_ColorTint(gX_Image *image, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    ImageColorTint((Image *)image, col);
}

//opencode: gX_Image_ColorInvert — invert colors
gX_I0 gX_Image_ColorInvert(gX_Image *image) {
    ImageColorInvert((Image *)image);
}

//opencode: gX_Image_ColorGrayscale — convert to grayscale
gX_I0 gX_Image_ColorGrayscale(gX_Image *image) {
    ImageColorGrayscale((Image *)image);
}

//opencode: gX_Image_ColorContrast — adjust contrast
gX_I0 gX_Image_ColorContrast(gX_Image *image, gX_F32 contrast) {
    ImageColorContrast((Image *)image, (gX_I32)contrast);
}

//opencode: gX_Image_ColorBrightness — adjust brightness
gX_I0 gX_Image_ColorBrightness(gX_Image *image, gX_I32 brightness) {
    ImageColorBrightness((Image *)image, brightness);
}

//opencode: gX_Image_ColorReplace — replace one color with another
gX_I0 gX_Image_ColorReplace(gX_Image *image, gX_Color color, gX_Color replace) {
    Color col = gX_Color_ConvertToBackend(color);
    Color rep = gX_Color_ConvertToBackend(replace);
    ImageColorReplace((Image *)image, col, rep);
}

//opencode: gX_Image_Format — wraps ImageFormat
gX_I0 gX_Image_Format(gX_Image *image, gX_I32 newFormat) {
    ImageFormat((Image *)image, newFormat);
}

//opencode: gX_Image_ToPOT — wraps ImageToPOT
gX_I0 gX_Image_ToPOT(gX_Image *image, gX_Color fill) {
    Color col = gX_Color_ConvertToBackend(fill);
    ImageToPOT((Image *)image, col);
}

//opencode: gX_Image_LoadColors — wrap pixel data as gX_Color array
gX_Color *gX_Image_LoadColors(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    Color *colors = LoadImageColors(img);
    return (gX_Color *)colors;
}

//opencode: gX_Image_LoadPalette — wraps LoadImagePalette
gX_Color *gX_Image_LoadPalette(gX_Image image, gX_I32 maxPaletteSize, gX_I32 *colorCount) {
    Image img = gX_Image_ConvertToBackend(image);
    Color *palette = LoadImagePalette(img, maxPaletteSize, colorCount);
    return (gX_Color *)palette;
}

//opencode: gX_Image_UnloadColors — free color array
gX_I0 gX_Image_UnloadColors(gX_Color *colors) {
    UnloadImageColors((Color *)colors);
}

//opencode: gX_Image_UnloadPalette — wraps UnloadImagePalette
gX_I0 gX_Image_UnloadPalette(gX_Color *colors) {
    UnloadImagePalette((Color *)colors);
}

//opencode: gX_Image_GetAlphaBorder — wraps GetImageAlphaBorder
gX_Rectangle gX_Image_GetAlphaBorder(gX_Image image, gX_F32 threshold) {
    Image img = gX_Image_ConvertToBackend(image);
    Rectangle rect = GetImageAlphaBorder(img, threshold);
    return gX_Rectangle_ConvertFromBackend(rect);
}

//opencode: gX_Image_Draw — blit src onto dst
gX_I0 gX_Image_Draw(gX_Image *dst, gX_Image src, gX_Rectangle srcRec, gX_Rectangle dstRec, gX_Color tint) {
    Image srcImg = gX_Image_ConvertToBackend(src);
    Rectangle srcR = gX_Rectangle_ConvertToBackend(srcRec);
    Rectangle dstR = gX_Rectangle_ConvertToBackend(dstRec);
    Color col = gX_Color_ConvertToBackend(tint);
    ImageDraw((Image *)dst, srcImg, srcR, dstR, col);
}

//opencode: gX_Image_DrawRectangle — fill rectangle on image
gX_I0 gX_Image_DrawRectangle(gX_Image *dst, gX_I32 posX, gX_I32 posY, gX_I32 width, gX_I32 height, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    ImageDrawRectangle((Image *)dst, posX, posY, width, height, col);
}

//opencode: gX_Image_DrawText — wraps ImageDrawText
gX_I0 gX_Image_DrawText(gX_Image *dst, const gX_I8 *text, gX_F32v2 position, gX_I32 fontSize, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    ImageDrawText((Image *)dst, text, (int)position.x, (int)position.y, fontSize, col);
}

//opencode: gX_Image_DrawTextEx — wraps ImageDrawTextEx
gX_I0 gX_Image_DrawTextEx(gX_Image *dst, gX_Font font, const gX_I8 *text, gX_F32v2 position, gX_F32 fontSize, gX_F32 spacing, gX_Color tint) {
    Font fnt = gX_Font_ConvertToBackend(font);
    Color col = gX_Color_ConvertToBackend(tint);
    Vector2 pos = gX_F32v2_ConvertToBackend(position);
    ImageDrawTextEx((Image *)dst, fnt, text, pos, fontSize, spacing, col);
}

//opencode: gX_Image_DrawPixel — set single pixel
gX_I0 gX_Image_DrawPixel(gX_Image *dst, gX_I32 x, gX_I32 y, gX_Color color) {
    Color col = gX_Color_ConvertToBackend(color);
    ImageDrawPixel((Image *)dst, x, y, col);
}

//opencode: gX_LoadTextureFromImage — wraps LoadTextureFromImage
gX_Texture gX_LoadTextureFromImage(gX_Image image) {
    Image img = gX_Image_ConvertToBackend(image);
    Texture tex = LoadTextureFromImage(img);
    return gX_Texture_ConvertFromBackend(tex);
}

//opencode: gX_UpdateTexture — wraps UpdateTexture
gX_I0 gX_UpdateTexture(gX_Texture texture, const gX_I0 *pixels) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    UpdateTexture(tex, pixels);
}

//opencode: gX_UpdateTextureRec — wraps UpdateTextureRec
gX_I0 gX_UpdateTextureRec(gX_Texture texture, gX_Rectangle rec, const gX_I0 *pixels) {
    Texture tex = gX_Texture_ConvertToBackend(texture);
    Rectangle r = gX_Rectangle_ConvertToBackend(rec);
    UpdateTextureRec(tex, r, pixels);
}
