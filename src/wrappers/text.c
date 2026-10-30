/**********************************************************************************************
* 
*   [wrappers/text.c]
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
#include "wrappers_header.h"
#include "wrappers.h"
#include "text.h"
#include "drawing.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_tex.h"
//opencode: hunt - implicit-decl sweep

#include "../common/config.h"
#include "../common/types.h"


#include <stdlib.h>

#include "../main.h"

//opencode: moderate font scale — reduces from full viewport-mapped size (2.5x)
//   to avoid oversized text on large windows while keeping it readable.
//   Applied consistently in both Draw and Measure functions so centering works.
#define GX_FONT_SCALE 0.75f

extern gX_DrawingInfo drawinfo;

//opencode: PS2 font side-table

//opencode: N64 font side-table (maps gX_Font to rdpq_font_t)

gX_Font gX_Font_GetDefault(gX_I0) {
    Font font = GetFontDefault();
    gX_Font result = gX_Font_ConvertFromBackend(font);
    return result;
}

gX_Font gX_Font_Load(const gX_I8 *path) {
    Font font = LoadFont(path);
    gX_Font result = gX_Font_ConvertFromBackend(font);
    return result;
}

//opencode: v0.6.0 — single-channel SDF font (raylib FONT_SDF atlas; the
//   atlas renders through the engine's sdf_text shader — this raylib
//   generates the distance field but carries no per-font shader).
//   baseSize should match the bitmap twin so metrics (advances) match.
static gX_Shader s_sdfTextShader = {0};
static gX_I1     s_sdfTextShaderLoaded = false;
static gX_I32    s_sdfTextColorLoc = -1;
//opencode: per-draw soft-shadow state (spread, blur, alpha multiplier).
static gX_F32    s_sdfSoft[3] = {0.0f, 1.0f, 1.0f};

gX_I0 gX_RawString_SDF_SetSoft(gX_F32 spread, gX_F32 blur, gX_F32 alphaScale) {
    s_sdfSoft[0] = spread;
    s_sdfSoft[1] = (blur > 0.001f) ? blur : 1.0f;
    s_sdfSoft[2] = (alphaScale >= 0.0f) ? alphaScale : 1.0f;
}

static gX_Shader gX_SDF_GetTextShader(gX_I0) {
    if (!s_sdfTextShaderLoaded) {
        s_sdfTextShader = gX_Shader_Load(NULL, "resources/shader/sdf_text.fs");
        if (s_sdfTextShader.id)
            s_sdfTextColorLoc = gX_RHI_GetUniformLocation(s_sdfTextShader.id, "textColor");
        s_sdfTextShaderLoaded = true;
    }
    return s_sdfTextShader;
}

gX_Font gX_Font_LoadSDF(const gX_I8 *path, gX_I32 baseSize) {
    //opencode: this raylib's LoadFontEx predates the FONT_SDF parameter —
    //   build the SDF font through the multi-step API (LoadFontData with
    //   FONT_SDF + GenImageFontAtlas + LoadTextureFromImage), mirroring the
    //   newer raylib internals.
    gX_Font result = {0};
    gX_I32 dataSize = 0;
    gX_U8 *fileData = LoadFileData(path, &dataSize);
    if (!fileData) return result;
    gX_I32 glyphCount = 0;
    GlyphInfo *glyphs = LoadFontData(fileData, dataSize, baseSize, NULL, 0, FONT_SDF, &glyphCount);
    free(fileData);
    if (!glyphs || glyphCount <= 0) return result;
    gX_I32 padding = 4; //opencode: FONT_SDF_CHAR_PADDING
    Rectangle *recs = NULL;
    Image atlas = GenImageFontAtlas(glyphs, &recs, glyphCount, baseSize, padding, 0);
    if (atlas.data == NULL) { free(glyphs); if (recs) free(recs); return result; }
    result.baseSize = baseSize;
    result.glyphCount = glyphCount;
    result.glyphPadding = padding;
    result.glyphs = (gX_GlyphInfo*)glyphs;
    result.recs = (gX_Rectangle*)recs;
    result.texture = gX_Texture_ConvertFromBackend(LoadTextureFromImage(atlas));
    UnloadImage(atlas);
    result.shader = gX_SDF_GetTextShader();
    return result;
}

//opencode: v0.6.0 — draw text with the SDF shader through the ENGINE's
//   direct-geometry path (gX_RHI_Shader_Use + DrawGeometry). raylib's
//   BeginShaderMode calls rlSetShader, which permanently changes
//   RLGL.State.currentShaderId: the rlgl immediate-quad batch (used by
//   DrawTextPro for the OTHER texts) then flushes with the SDF program and
//   the rest of the canvas text silently disappears. Rotation falls back
//   to the plain (blurry) DrawTextPro.
static gX_I0 gX_RawString_Draw_SDF(const gX_I8 *string, gX_Font font, gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize, gX_F32 spacing, gX_Color tint) {
    gX_F32 scaledSize = fontsize * GX_FONT_SCALE;
    Font backendFont = gX_Font_ConvertToBackend(font);
    gX_U32 sdfId = gX_SDF_GetTextShader().id;
    if (sdfId == 0 || rotation != 0.0f) {
        DrawTextPro(backendFont, string, (Vector2){position.x, position.y}, (Vector2){origin.x, origin.y}, rotation, scaledSize, spacing, (Color){tint.r, tint.g, tint.b, tint.a});
        return;
    }

    gX_I32 prevProg = 0;
    gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &prevProg);
    gX_RHI_Shader_Use(sdfId);
    gX_I32 texLoc = gX_RHI_GetUniformLocation(sdfId, "texture0");
    if (texLoc >= 0) gX_RHI_UniformInt(texLoc, 0);
    if (s_sdfTextColorLoc >= 0) gX_RHI_UniformVec4(s_sdfTextColorLoc, 1, (gX_F32[4]){tint.r/255.0f, tint.g/255.0f, tint.b/255.0f, tint.a/255.0f});
    gX_I32 softLoc = gX_RHI_GetUniformLocation(sdfId, "soft");
    if (softLoc >= 0) gX_RHI_UniformVec4(softLoc, 1, (gX_F32[4]){s_sdfSoft[0], s_sdfSoft[1], s_sdfSoft[2], 1.0f});
    gX_RHI_ActiveTexture(gX_RHI_TEXTURE0);
    gX_RHI_Tex_Bind(backendFont.texture.id, gX_RHI_TEXTURE_2D);

    gX_F32 scale = scaledSize / (gX_F32)backendFont.baseSize;
    gX_F32 offsetX = 0.0f;
    gX_F32 offsetY = 0.0f;
    gX_F32 lineH = scaledSize + spacing;
    const gX_I8 *p = string;
    while (*p) {
        gX_I32 size = 0;
        gX_I32 cp = GetCodepointNext(p, &size);
        if (cp == '\n') { offsetX = 0.0f; offsetY += lineH; p += size; continue; }
        gX_I32 idx = GetGlyphIndex(backendFont, cp);
        gX_F32 glyphW = 0.0f;
        if (idx >= 0 && idx < backendFont.glyphCount) {
            if (cp != ' ' && cp != '\t') {
                Rectangle srcRec = backendFont.recs[idx];
                Rectangle dstRec = {
                    position.x + offsetX + (gX_F32)backendFont.glyphs[idx].offsetX*scale - (gX_F32)backendFont.glyphPadding*scale,
                    position.y + offsetY + (gX_F32)backendFont.glyphs[idx].offsetY*scale - (gX_F32)backendFont.glyphPadding*scale,
                    (srcRec.width + 2.0f*(gX_F32)backendFont.glyphPadding)*scale,
                    (srcRec.height + 2.0f*(gX_F32)backendFont.glyphPadding)*scale
                };
                gX_F32 u0 = srcRec.x / (gX_F32)backendFont.texture.width;
                gX_F32 v0 = srcRec.y / (gX_F32)backendFont.texture.height;
                gX_F32 u1 = (srcRec.x + srcRec.width) / (gX_F32)backendFont.texture.width;
                gX_F32 v1 = (srcRec.y + srcRec.height) / (gX_F32)backendFont.texture.height;
                gX_F32 x0 = dstRec.x, y0 = dstRec.y;
                gX_F32 x1 = dstRec.x + dstRec.width, y1 = dstRec.y + dstRec.height;
                gX_F32 verts[24] = {
                    x0, y0, u0, v0, x0, y1, u0, v1, x1, y1, u1, v1,
                    x0, y0, u0, v0, x1, y1, u1, v1, x1, y0, u1, v0
                };
                gX_Drawing_DrawGeometry(verts, 6, gX_RHI_TRIANGLES);
                glyphW = srcRec.width;
            }
            gX_F32 adv = (gX_F32)backendFont.glyphs[idx].advanceX;
            if (adv == 0.0f) offsetX += glyphW*scale + spacing;
            else offsetX += adv*scale + spacing;
        }
        p += size;
    }
    gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D);
    if (prevProg) gX_RHI_Shader_Use((gX_U32)prevProg);
}

gX_I0 gX_Font_Unload(gX_Font font) {
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE,"Unloading font");
    Font fnt = gX_Font_ConvertToBackend(font);
    UnloadFont(fnt);
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE,"Font unloading done");
}

//opencode: v0.6.1 — real implementations (were no-op stubs). Pointer-returning
//   helpers share one static scratch buffer (the header's const-return
//   signatures have no allocation channel) — results are overwritten by the
//   next call; do not chain two of these in one expression.
static gX_I8 s_rwScratch[2048];

gX_Font gX_Font_LoadFromImage(gX_Image image) {
    Font fnt = LoadFontFromImage(gX_Image_ConvertToBackend(image), (Color){0, 0, 0, 0}, 32);
    return gX_Font_ConvertFromBackend(fnt);
}

gX_Font gX_Font_LoadFromMemory(gX_Image image) {
    return gX_Font_LoadFromImage(image);
}

gX_I32 gX_Font_GetGlyphAtlasRec(gX_Font font, gX_I32 codepoint) {
    return gX_Font_GetGlyphIndex(font, codepoint);
}

gX_I1 gX_RawString_IsEqual(const gX_I8 *str1, const gX_I8 *str2) {
    if (!str1 || !str2) return false;
    return strcmp(str1, str2) == 0;
}

gX_U32 gX_RawString_Length(const gX_I8 *string) {
    return string ? (gX_U32)strlen(string) : 0;
}

const gX_I8 *gX_RawString_SubString(const gX_I8 *string, gX_I32 pos, gX_U32 len) {
    if (!string) return "";
    gX_U32 slen = (gX_U32)strlen(string);
    if (pos < 0) pos = 0;
    if ((gX_U32)pos > slen) pos = (gX_I32)slen;
    gX_U32 avail = slen - (gX_U32)pos;
    if (len > avail) len = avail;
    memcpy(s_rwScratch, string + pos, len);
    s_rwScratch[len] = '\0';
    return s_rwScratch;
}

gX_I8 *gX_RawString_Replace(gX_I8 *string, const gX_I8 *replace, const gX_I8 *by) {
    if (!string || !replace || !*replace) return string;
    if (!by) by = "";
    gX_U32 slen = (gX_U32)strlen(string);
    gX_U32 rlen = (gX_U32)strlen(replace);
    gX_U32 blen = (gX_U32)strlen(by);
    gX_U32 out = 0;
    const gX_I8 *p = string, *end = string + slen;
    while (p < end) {
        const gX_I8 *hit = strstr(p, replace);
        if (!hit) {
            while (p < end && out + 1 < sizeof(s_rwScratch)) s_rwScratch[out++] = *p++;
            break;
        }
        gX_U32 seg = (gX_U32)(hit - p);
        if (out + seg + blen + 1 > sizeof(s_rwScratch)) break;
        memcpy(s_rwScratch + out, p, seg); out += seg;
        memcpy(s_rwScratch + out, by, blen); out += blen;
        p = hit + rlen;
    }
    s_rwScratch[out] = '\0';
    return s_rwScratch;
}

gX_I8 *gX_RawString_Insert(const gX_I8 *string, const gX_I8 *insert, gX_I32 pos) {
    if (!string) string = "";
    if (!insert) insert = "";
    gX_U32 slen = (gX_U32)strlen(string), ilen = (gX_U32)strlen(insert);
    if (pos < 0) pos = 0;
    if ((gX_U32)pos > slen) pos = (gX_I32)slen;
    gX_U32 total = slen + ilen;
    if (total + 1 > sizeof(s_rwScratch)) total = (gX_U32)sizeof(s_rwScratch) - 1;
    memcpy(s_rwScratch, string, (size_t)pos);
    gX_U32 il = ilen; if (pos + il > total) il = total - pos;
    memcpy(s_rwScratch + pos, insert, il);
    gX_U32 sl = slen; if (pos + il + sl > total) sl = total - pos - il;
    memcpy(s_rwScratch + pos + il, string + pos, sl);
    s_rwScratch[pos + il + sl] = '\0';
    return s_rwScratch;
}

const gX_I8 *gX_RawString_Join(const gX_I8 **stringList, gX_I32 count, const gX_I8 *delimiter) {
    gX_U32 out = 0;
    if (!delimiter) delimiter = "";
    gX_U32 dlen = (gX_U32)strlen(delimiter);
    for (gX_I32 i = 0; i < count; i++) {
        const gX_I8 *s = (stringList && stringList[i]) ? stringList[i] : "";
        if (i > 0 && dlen && out + dlen + 1 < sizeof(s_rwScratch)) {
            memcpy(s_rwScratch + out, delimiter, dlen); out += dlen;
        }
        gX_U32 slen = (gX_U32)strlen(s);
        if (out + slen + 1 > sizeof(s_rwScratch)) slen = (gX_U32)sizeof(s_rwScratch) - out - 1;
        memcpy(s_rwScratch + out, s, slen); out += slen;
    }
    s_rwScratch[out] = '\0';
    return s_rwScratch;
}

gX_I0 gX_RawString_Append(gX_I8 *string, const gX_I8 *append, gX_I32 *pos) {
    if (!string || !append) return;
    gX_U32 alen = (gX_U32)strlen(append);
    if (pos && *pos >= 0) {
        gX_U32 at = (gX_U32)*pos;
        gX_U32 slen = (gX_U32)strlen(string);
        if (at > slen) at = slen;
        memmove(string + at + alen, string + at, slen - at + 1);
        memcpy(string + at, append, alen);
        *pos = (gX_I32)(at + alen);
    } else {
        strcat(string, append);
        if (pos) *pos = (gX_I32)strlen(string);
    }
}

gX_I32 gX_RawString_FindIndex(const gX_I8 *string, const gX_I8 *find) {
    if (!string || !find) return -1;
    const gX_I8 *hit = strstr(string, find);
    return hit ? (gX_I32)(hit - string) : -1;
}

const gX_I8 *gX_RawString_ToUpper(const gX_I8 *string) {
    if (!string) return "";
    gX_U32 i = 0;
    for (; string[i] && i + 1 < sizeof(s_rwScratch); i++)
        s_rwScratch[i] = (string[i] >= 'a' && string[i] <= 'z') ? (gX_I8)(string[i] - 32) : string[i];
    s_rwScratch[i] = '\0';
    return s_rwScratch;
}

const gX_I8 *gX_RawString_ToLower(const gX_I8 *string) {
    if (!string) return "";
    gX_U32 i = 0;
    for (; string[i] && i + 1 < sizeof(s_rwScratch); i++)
        s_rwScratch[i] = (string[i] >= 'A' && string[i] <= 'Z') ? (gX_I8)(string[i] + 32) : string[i];
    s_rwScratch[i] = '\0';
    return s_rwScratch;
}

const gX_I8 *gX_RawString_ToPascal(const gX_I8 *string) {
    if (!string) return "";
    gX_U32 i = 0;
    gX_I1 newWord = true;
    for (; string[i] && i + 1 < sizeof(s_rwScratch); i++) {
        gX_I8 c = string[i];
        if (c == ' ' || c == '\t' || c == '\n') { s_rwScratch[i] = c; newWord = true; continue; }
        if (newWord) { s_rwScratch[i] = (c >= 'a' && c <= 'z') ? (gX_I8)(c - 32) : c; newWord = false; }
        else s_rwScratch[i] = (c >= 'A' && c <= 'Z') ? (gX_I8)(c + 32) : c;
    }
    s_rwScratch[i] = '\0';
    return s_rwScratch;
}

gX_U32 gX_RawString_ToInteger(const gX_I8 *string) {
    return string ? (gX_U32)strtoul(string, NULL, 10) : 0;
}

gX_String gX_String_Format(const gX_I8 *format, ...) {
    gX_String s = {0};
    if (!format) return s;
    va_list args;
    va_start(args, format);
    gX_I32 n = vsnprintf(s_rwScratch, sizeof(s_rwScratch), format, args);
    va_end(args);
    if (n < 0) n = 0;
    if ((gX_U32)n >= sizeof(s_rwScratch)) n = (gX_I32)sizeof(s_rwScratch) - 1;
    s.string = s_rwScratch;
    s.length = (gX_U32)n;
    return s;
}

gX_I32 gX_String_Copy(gX_String dst, gX_String src) {
    if (!dst.string || !src.string) return -1;
    gX_U32 n = src.length;
    if (dst.length && n > dst.length) n = dst.length;
    memcpy(dst.string, src.string, n);
    dst.string[n] = '\0';
    return (gX_I32)n;
}

gX_F32v2 gX_Text_Measure(gX_Text *text) {
    if (!text || !text->string.string) return (gX_F32v2){0, 0};
    Font fnt = gX_Font_ConvertToBackend(text->font);
    Vector2 m = MeasureTextEx(fnt, text->string.string, (gX_F32)text->fontSize * GX_FONT_SCALE, (gX_F32)text->spacing);
    return (gX_F32v2){m.x, m.y};
}

//opencode: font query implementations
gX_I1 gX_Font_IsReady(gX_Font font) {
    Font fnt = gX_Font_ConvertToBackend(font);
    return fnt.texture.id > 0;
}

gX_I32 gX_Font_GetGlyphIndex(gX_Font font, gX_I32 codepoint) {
    Font fnt = gX_Font_ConvertToBackend(font);
    return GetGlyphIndex(fnt, codepoint);
}

gX_I32 gX_Font_GetGlyphInfo(gX_Font font, gX_I32 codepoint) {
    //return glyph advanceX as simple metric
    gX_I32 idx = gX_Font_GetGlyphIndex(font, codepoint);
    if (idx >= 0 && idx < font.glyphCount) {
        return font.glyphs[idx].advanceX;
    }
    return 0;
}

//opencode: load font with custom codepoints
gX_Font gX_Font_LoadEx(const gX_I8 *path, gX_I32 fontSize, gX_I32 *codepoints, gX_I32 codepointCount) {
    Font fnt = LoadFontEx(path, fontSize, codepoints, codepointCount);
    return gX_Font_ConvertFromBackend(fnt);
}

const gX_I8 *gX_RawString_Format(const gX_I8 *string, ...) {
    va_list args;
    va_start(args, string);
    const gX_I8 *format = raylib_Text_Format(string, args);
    va_end(args);
    return format;
}

gX_I32 gX_RawString_Copy(gX_I8 *dst, const gX_I8 *src) {
    return TextCopy(dst,src);
}

gX_I8 **gX_RawString_Split(const gX_I8 *string, gX_I8 delimiter, gX_I32 *count) {
    return (gX_I8 **)TextSplit(string,delimiter,count);
}

gX_F32v2 gX_RawString_Measure(const gX_I8 *string, gX_Font font, gX_F32 fontsize, gX_F32 spacing) {
    gX_F32 scaledSize = fontsize * GX_FONT_SCALE;
    Font backendFont = gX_Font_ConvertToBackend(font);
    Vector2 backendV2 = MeasureTextEx(backendFont,string,scaledSize,spacing);
    gX_F32v2 result = gX_F32v2_ConvertFromBackend(backendV2);
    return result;
}

gX_I0 gX_RawString_Draw_Lesser(const gX_I8 *string, gX_F32v2 position, gX_F32 fontsize, gX_Color tint) {
    gX_F32 scaledSize = fontsize * GX_FONT_SCALE;
    Color tintcolor = (Color){0};
    tintcolor.r = tint.r;
    tintcolor.g = tint.g;
    tintcolor.b = tint.b;
    tintcolor.a = tint.a;
    DrawText(string,(int)position.x,(int)position.y,(int)scaledSize,tintcolor);
}

gX_I0 gX_RawString_Draw(const gX_I8 *string, gX_Font font, gX_F32v2 position, gX_F32v2 origin, gX_F32 rotation, gX_F32 fontsize, gX_F32 spacing, gX_Color tint) {
    gX_F32 scaledSize = fontsize * GX_FONT_SCALE;
    Font backendFont = gX_Font_ConvertToBackend(font);
    Vector2 positionv2 = (Vector2){0};
    positionv2.x = position.x;
    positionv2.y = position.y;
    Vector2 originv2 = (Vector2){0};
    originv2.x = origin.x;
    originv2.y = origin.y;
    //opencode: v0.6.0 — SDF fonts draw through the engine's SDF shader
    if (font.shader.id != 0) {
        gX_RawString_Draw_SDF(string, font, position, origin, rotation, fontsize, spacing, tint);
        return;
    }
    Color tintcolor = (Color){0};
    tintcolor.r = tint.r;
    tintcolor.g = tint.g;
    tintcolor.b = tint.b;
    tintcolor.a = tint.a;
    DrawTextPro(backendFont,string,positionv2,originv2,rotation,scaledSize,spacing,tintcolor);
}

gX_I0 gX_RawString_Draw_Simple(const gX_I8 *string) {
    gX_F32 scaledSize = 48.0f * GX_FONT_SCALE;
    DrawText(string,maindata.screenWidth/2,maindata.screenHeight/2,(int)scaledSize,BLACK);
}

gX_I0 gX_Text_Draw(gX_Text text) {
    gX_F32 scaledSize = (gX_F32)text.fontSize * GX_FONT_SCALE;
    Font font = GetFontDefault();
    if (text.font.glyphCount > 0) {
        font = gX_Font_ConvertToBackend(text.font);
    }
    Color color = gX_Color_ConvertToBackend(text.color);
    DrawTextPro(font,text.string.string,
                (Vector2){0.0f,0.0f},(Vector2){0.0f,0.0f},
                0.0f,scaledSize,(float)text.spacing,color);
}
