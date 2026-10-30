/**********************************************************************************************
*
*   [gX: src/gui/element/text.c]
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


#include <string.h>
#include <stdlib.h>

#include "element.h"
#include "../gui.h"
#include "modules.h"

#include "../../wrappers/wrappers.h"
#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/utils.h"

#include "../../gxmath/color.h"
#include "../../common/config.h"
#include "../../main.h"

inline gX_I0 gX_GUI_Element_Text_Init(gX_GUI_Element_Text *text) {
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing text values");
    
    text->draw              = true;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Zeroing out text buffer");
    memset(text->text,'\0',gX_GUI_ELEMENT_TEXT_MAX);
    gX_TraceLog_GUI(gX_LOG_ALL,"Zeroing out text buffer done");
    
    text->fontSize          = gX_GUI_TEXT_DEFAULT_FONTSIZE;
    text->spacing           = gX_GUI_TEXT_DEFAULT_SPACING;
    
    text->color             = gX_BLACK;
    text->colorOriginal     = gX_BLACK;
    text->lineSpacing       = 0.0f;
    text->wrap              = false;
    text->sdf               = false;
    text->autoShrink        = false;
    
    gX_TraceLog_GUI(gX_LOG_ALL,"Initializing values done");
}

//opencode: v0.6.0 — word-wrap core (pure; measurement via callback so unit
//   tests run headless with a mock). Greedy per-word fill + minimal UAX #14:
//   break opportunities between CJK ideographs (each CJK char is its own
//   segment — ID/IN classes); ASCII words break at whitespace; hard '\n'
//   kept. A single segment wider than maxWidth emits as its own (overflowing)
//   line — the draw path's ellipsis handles it. Returns bytes written (excl.
//   NUL), 0 if the input is empty or the buffer is too small.
typedef gX_F32 (*gX_GUI_TextMeasureFn)(const gX_I8 *segment, gX_I0 *userData);

static gX_U32 gX_GUI_Utf8_Decode(const gX_I8 **pp) {
    const gX_U8 *p = (const gX_U8*)*pp;
    gX_U32 cp = 0;
    if ((p[0] & 0x80) == 0)      { cp = p[0]; *pp += 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { cp = ((gX_U32)(p[0] & 0x1F) << 6) | (gX_U32)(p[1] & 0x3F); *pp += 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { cp = ((gX_U32)(p[0] & 0x0F) << 12) | ((gX_U32)(p[1] & 0x3F) << 6) | (gX_U32)(p[2] & 0x3F); *pp += 3; }
    else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) { cp = ((gX_U32)(p[0] & 0x07) << 18) | ((gX_U32)(p[1] & 0x3F) << 12) | ((gX_U32)(p[2] & 0x3F) << 6) | (gX_U32)(p[3] & 0x3F); *pp += 4; }
    else { cp = p[0]; *pp += 1; }
    return cp;
}

static gX_I1 gX_GUI_Utf8_IsCJK(gX_U32 cp) {
    return (cp >= 0x2E80 && cp <= 0x2EFF) || (cp >= 0x3000 && cp <= 0x30FF) ||
           (cp >= 0x3100 && cp <= 0x312F) || (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

gX_I32 gX_GUI_Element_Text_Wrap(const gX_I8 *text, gX_F32 maxWidth,
                                gX_GUI_TextMeasureFn measure, gX_I0 *userData,
                                gX_I8 *out, gX_U32 outSize) {
    if (!text || !measure || !out || outSize == 0) return 0;
    out[0] = '\0';
    if (!*text) return 0;

    gX_I8 line[gX_GUI_ELEMENT_TEXT_MAX];
    gX_I8 tmp[gX_GUI_ELEMENT_TEXT_MAX];
    gX_U32 lineLen = 0;
    gX_U32 outLen = 0;
    const gX_I8 *src = text;
    const gX_I8 *ws = NULL;

    //opencode: emit the accumulated line (drops pending trailing whitespace)
    #define GX_WRAP_EMIT_LINE() do { \
        line[lineLen] = '\0'; \
        if (outLen > 0) { out[outLen++] = '\n'; if (outLen >= outSize - 1) goto gx_wrap_done; } \
        { gX_U32 cpl = lineLen; \
          if (cpl > outSize - outLen - 1) cpl = outSize - outLen - 1; \
          if (cpl > 0) memcpy(out + outLen, line, cpl); \
          outLen += cpl; \
          out[outLen] = '\0'; } \
        lineLen = 0; \
    } while (0)

    //opencode: append [begin, end) plus pending whitespace to the line
    #define GX_WRAP_APPEND(begin, end) do { \
        gX_U32 alen = (gX_U32)((end) - (begin)); \
        if (ws) { gX_U32 wlen = (gX_U32)((begin) - (ws)); \
                  if (lineLen + wlen < sizeof(line)) { memcpy(line + lineLen, ws, wlen); lineLen += wlen; } } \
        if (lineLen + alen < sizeof(line)) { memcpy(line + lineLen, (begin), alen); lineLen += alen; } \
        ws = NULL; \
    } while (0)

    while (*src) {
        if (*src == '\n') {
            GX_WRAP_EMIT_LINE();
            src++;
            continue;
        }
        if (*src == ' ' || *src == '\t') {
            if (!ws) ws = src;
            src++;
            continue;
        }

        //opencode: segment = one CJK char, or a run of non-space/non-CJK bytes
        const gX_I8 *segStart = src;
        if (gX_GUI_Utf8_IsCJK(gX_GUI_Utf8_Decode(&src))) {
            // src already advanced past the single CJK char
        } else {
            while (*src && *src != ' ' && *src != '\t' && *src != '\n') {
                const gX_I8 *chk = src;
                if (gX_GUI_Utf8_IsCJK(gX_GUI_Utf8_Decode(&chk))) break;
                src = chk;
            }
        }
        const gX_I8 *segEnd = src;

        //opencode: greedy fit — line + pending ws + segment
        gX_U32 tlen = 0;
        if (ws) tlen = (gX_U32)(segStart - ws);
        if (lineLen + tlen + (gX_U32)(segEnd - segStart) < sizeof(tmp)) {
            if (lineLen > 0) { memcpy(tmp, line, lineLen); tlen = lineLen; }
            else tlen = 0;
            if (ws) { memcpy(tmp + tlen, ws, (gX_U32)(segStart - ws)); tlen += (gX_U32)(segStart - ws); }
            memcpy(tmp + tlen, segStart, (gX_U32)(segEnd - segStart));
            tlen += (gX_U32)(segEnd - segStart);
            tmp[tlen] = '\0';
            if (measure(tmp, userData) <= maxWidth || lineLen == 0) {
                GX_WRAP_APPEND(segStart, segEnd);
                continue;
            }
            GX_WRAP_EMIT_LINE();
            ws = NULL; //opencode: pending ws belongs to the old line — drop it
            GX_WRAP_APPEND(segStart, segEnd);
            continue;
        }
        //opencode: line buffer nearly full — emit and start fresh
        GX_WRAP_EMIT_LINE();
        ws = NULL;
        GX_WRAP_APPEND(segStart, segEnd);
    }
    GX_WRAP_EMIT_LINE();
gx_wrap_done:
    return (gX_I32)outLen;
}

//opencode: v0.6.0 — font-backed wrap entry for the draw path
typedef struct gX_GUI_WrapMeasureCtx {
    gX_Font  font;
    gX_F32   fontsize;
    gX_F32   spacing;
} gX_GUI_WrapMeasureCtx;

static gX_F32 gX_GUI_Wrap_MeasureCb(const gX_I8 *segment, gX_I0 *userData) {
    gX_GUI_WrapMeasureCtx *ctx = (gX_GUI_WrapMeasureCtx*)userData;
    return gX_RawString_Measure(segment, ctx->font, ctx->fontsize, ctx->spacing).x;
}

gX_I32 gX_GUI_Element_Text_WrapText(const gX_I8 *text, gX_F32 maxWidth,
                                    gX_Font font, gX_F32 fontsize, gX_F32 spacing,
                                    gX_I8 *out, gX_U32 outSize) {
    gX_GUI_WrapMeasureCtx ctx = { font, fontsize, spacing };
    return gX_GUI_Element_Text_Wrap(text, maxWidth, gX_GUI_Wrap_MeasureCb, &ctx, out, outSize);
}

//opencode: shared draw-time preparation — font/scale resolution, opt-in
//   shrink-to-fit, wrap, auto-size, and truncation live in one place so the
//   draw path and the layout analyzer report exactly the same bounds.
typedef struct gX_GUI_Element_Text_Prepared {
    gX_Font      font;
    gX_F32       fontsize;
    gX_F32       spacing;
    gX_Rectangle rect;
    gX_I8        drawBuf[gX_GUI_ELEMENT_TEXT_MAX];
    gX_F32v2     measure;
    gX_F32v2     natural;
    gX_F32       shrinkScale;
    gX_F32v2     shadowOffset;  // themed offset, scaled like the font
    gX_Color     shadowColor;   // alpha 0 disables the shadow visually
    gX_F32       outlineWidth;  // themed 8-way outline width, scaled; 0 = off
    gX_Color     outlineColor;
    gX_F32       shadowSoftness; // themed soft-shadow ring radius, scaled; 0 = off
    gX_I1        textFromBuf;  // drawBuf holds wrapped/truncated text
    gX_I1        wrapped;
    gX_I1        truncated;
} gX_GUI_Element_Text_Prepared;

static gX_GUI_Element_Text_Prepared gX_GUI_Element_Text_Prepare(
    gX_GUI_Element_Text *text, const gX_GUI_Element *element, gX_I1 measureNatural) {
    gX_GUI_Element_Text_Prepared p;
    memset(&p, 0, sizeof(p));
    p.shrinkScale = 1.0f;
    p.rect = element->rect;

    //opencode: v0.2.0 — engine UI font (Kenney Mini Square Mono) once the
    //   GUI has loaded it; raylib's builtin remains the fallback
    if (gui.style.uiFontLoaded) p.font = gui.style.uiFont;
    else                        p.font = gX_Font_GetDefault();
    if (text->hasCustomFont && text->customFontPtr) {
        p.font = *(text->customFontPtr);
    }
    //opencode: v0.6.0 — SDF twin (only for the default uiFont; customFont wins)
    else if (text->sdf && gui.style.uiFontSDFLoaded) {
        p.font = gui.style.uiFontSDF;
    }

    //opencode: resolution-independent text scaling.
    //   fontsize and spacing are scaled by screenWidth / base resolution (1024)
    //   so text at any screenRes appears the same proportional size as at 1024x768.
    //opencode: explicit legacy math — identical bits to the old bare-macro
    //   division ((SW/1024)*mult), but context-free.
    gX_F32 resScale = (gX_F32)maindata.screenWidth / 1024.0f * gX_scale_mult;
    if (resScale < 0.5f) resScale = 0.5f;
    if (resScale > 4.0f) resScale = 4.0f;
    p.fontsize = (gX_F32)(text->fontSize) * resScale;
    p.spacing =  (gX_F32)(text->spacing) * resScale;
    if (element->stateRendering.TEXT_SIZE_RELATIVE) {
        p.fontsize *= p.rect.width;
    }
    //opencode: shadow tokens scale with the font like size and spacing, so a
    //   high-resolution run keeps the same relative depth.
    gX_I1 smallShadow = (text->fontSize > 0
        && (gX_F32)text->fontSize < gui.style.bodySize);
    p.shadowOffset = (gX_F32v2){
        (smallShadow ? gui.style.textShadow.offsetXSmall
                     : gui.style.textShadow.offsetX) * resScale,
        (smallShadow ? gui.style.textShadow.offsetYSmall
                     : gui.style.textShadow.offsetY) * resScale};
    p.shadowColor = gui.style.textShadow.color;
    p.outlineWidth = gui.style.textOutline.width * resScale;
    p.outlineColor = gui.style.textOutline.color;
    p.shadowSoftness = gui.style.textShadow.softness * resScale;

    const gX_I8 *drawText = text->text;
    if (strcmp(text->text, "\0")) {
        if (measureNatural || text->autoShrink) {
            p.natural = gX_RawString_Measure(text->text, p.font, p.fontsize, p.spacing);
        }
        //opencode: opt-in shrink-to-fit — reduce the font size until the
        //   single-line text fits (floor at the min scale), before wrap and
        //   truncation. A label that still does not fit at the floor keeps
        //   the honest truncation signal.
        if (text->autoShrink && !element->stateRendering.RECT_AUTO_WIDTH
            && !element->stateRendering.TEXT_SIZE_RELATIVE) {
            //opencode: pure fit-scale math (unit-tested without a font).
            gX_F32 scale = gX_GUI_Element_Text_FitScale(p.natural.x, p.rect.width);
            if (scale < 1.0f) {
                p.fontsize *= scale;
                p.spacing *= scale;
                p.shrinkScale = scale;
            }
        }
        //opencode: v0.6.0 — word-wrap to the rect width (opt-in; fixed width only —
        //   RECT_AUTO_WIDTH already sizes to the text, so wrapping is moot)
        if (text->wrap && !element->stateRendering.RECT_AUTO_WIDTH) {
            if (gX_GUI_Element_Text_WrapText(text->text, p.rect.width, p.font, p.fontsize, p.spacing,
                                             p.drawBuf, sizeof(p.drawBuf)) > 0) {
                drawText = p.drawBuf;
                p.wrapped = true;
            }
        }
        p.measure = gX_RawString_Measure(drawText, p.font, p.fontsize, p.spacing);
        //opencode: auto-size element to text content when RECT_AUTO_WIDTH/HEIGHT is set.
        //   Measured text size becomes the element rect size so the container
        //   tight-fits the text. This must run before centering calculations.
        if (element->stateRendering.RECT_AUTO_WIDTH) {
            p.rect.width = p.measure.x;
        }
        if (element->stateRendering.RECT_AUTO_HEIGHT) {
            p.rect.height = p.measure.y;
        }
        //opencode: overflow detection + truncation with "..."
        gX_I1 needsTruncation = (p.measure.x > p.rect.width);
        if (needsTruncation && !p.wrapped && !text->overflowLogged) {
            text->overflowLogged = true;
            gX_TraceLog(gX_LOG_NOTICE, "[GUI_OVERFLOW] text=\"%s\" exceeds rect (%.0fx%.0f) by %.0fpx",
                text->text, p.rect.width, p.rect.height, p.measure.x - p.rect.width);
        }
        if (needsTruncation && !p.wrapped) {
            gX_U32 len = (gX_U32)strlen(text->text);
            gX_F32 avgCharW = (len > 0) ? p.measure.x / (gX_F32)len : 1.0f;
            gX_I32 maxChars = (gX_I32)((p.rect.width - 8.0f) / avgCharW);
            if (maxChars < 1) maxChars = 1;
            if (maxChars > (gX_I32)len - 3) maxChars = (gX_I32)len - 3;
            if (maxChars < 0) maxChars = 0;
            if (maxChars > (gX_I32)sizeof(p.drawBuf) - 4) maxChars = (gX_I32)sizeof(p.drawBuf) - 4;
            strncpy(p.drawBuf, text->text, (gX_U32)maxChars);
            p.drawBuf[maxChars] = '\0';
            strcat(p.drawBuf, "...");
            drawText = p.drawBuf;
            p.truncated = true;
            //opencode: re-measure for centering
            p.measure = gX_RawString_Measure(drawText, p.font, p.fontsize, p.spacing);
        } else if (needsTruncation && p.wrapped) {
            //opencode: v0.6.0 — wrap fits every line except an overwide single
            //   segment; ellipsize that last line in place
            const gX_I8 *lastLf = strrchr(drawText, '\n');
            const gX_I8 *lastLine = lastLf ? lastLf + 1 : drawText;
            gX_F32v2 lastLen = gX_RawString_Measure(lastLine, p.font, p.fontsize, p.spacing);
            if (lastLen.x > p.rect.width) {
                gX_U32 llen = (gX_U32)strlen(lastLine);
                gX_F32 avgCharW = (llen > 0) ? lastLen.x / (gX_F32)llen : 1.0f;
                gX_I32 maxChars = (gX_I32)((p.rect.width - 8.0f) / avgCharW);
                if (maxChars < 1) maxChars = 1;
                if (maxChars > (gX_I32)llen - 3) maxChars = (gX_I32)llen - 3;
                if (maxChars < 0) maxChars = 0;
                gX_I8 *dst = (gX_I8*)lastLine;
                strncpy(dst, lastLine, (gX_U32)maxChars);
                dst[maxChars] = '\0';
                strcat(dst, "...");
                p.truncated = true;
                p.measure = gX_RawString_Measure(drawText, p.font, p.fontsize, p.spacing);
            }
        }
        p.textFromBuf = (drawText == p.drawBuf);
    }
    return p;
}

//opencode: pure fit-scale math — the font scale a label needs so its natural
//   single-line width fits the rect, floored at the readable minimum. Kept
//   font-free so headless tests pin the floor without a window.
gX_F32 gX_GUI_Element_Text_FitScale(gX_F32 naturalWidth, gX_F32 rectWidth) {
    if (naturalWidth <= 0.0f || rectWidth <= 0.0f || naturalWidth <= rectWidth)
        return 1.0f;
    gX_F32 scale = rectWidth / naturalWidth;
    if (scale < gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE)
        scale = gX_GUI_TEXT_AUTOSHRINK_MIN_SCALE;
    return scale;
}

//opencode: draw-truth layout verdict for the audit trace and headless fit
//   tests — same prepared path as the draw call, no drawing.
gX_GUI_Element_Text_Layout gX_GUI_Element_Text_Analyze(const gX_GUI_Element *element,
    gX_I32 textIndex) {
    gX_GUI_Element_Text_Layout layout;
    memset(&layout, 0, sizeof(layout));
    if (!element || textIndex < 0) return layout;
    gX_GUI_Element_Text *text = gX_GUI_Element_Text_Get((gX_GUI_Element*)element, (gX_U32)textIndex);
    if (!text || text->text[0] == '\0') return layout;
    gX_GUI_Element_Text_Prepared prepared = gX_GUI_Element_Text_Prepare(text, element, true);
    layout.hasText = true;
    layout.measure = prepared.measure;
    layout.natural = prepared.natural.x > 0.0f ? prepared.natural : prepared.measure;
    layout.rectWidth = prepared.rect.width;
    layout.rectHeight = prepared.rect.height;
    gX_F32 lineHeightMul = (text->lineSpacing != 0.0f)
        ? (1.0f + text->lineSpacing) : gui.style.a11y.typeLineHeight;
    if (lineHeightMul <= 0.0f) lineHeightMul = 1.0f;
    layout.lineHeight = prepared.fontsize * lineHeightMul;
    layout.shrinkScale = prepared.shrinkScale;
    layout.wraps = prepared.wrapped;
    layout.truncated = prepared.truncated;
    layout.overflowX = prepared.measure.x > prepared.rect.width + 0.5f;
    layout.overflowY = prepared.measure.y > prepared.rect.height + 0.5f;
    return layout;
}

//opencode: audit hook — GX_GUI_TEXT_SHADOW=1 forces the shadow path on every
//   text so the shadow gate can verify bitmap and SDF drawing without a
//   template opting in.
static gX_I1 gX_GUI_TextShadow_Forced(gX_I0) {
    static gX_I1 resolved = false;
    static gX_I1 forced = false;
    if (!resolved) {
        resolved = true;
        const gX_I8 *env = getenv("GX_GUI_TEXT_SHADOW");
        forced = env && env[0] && env[0] != '0';
    }
    return forced;
}

static gX_I0 gX_GUI_Element_Text_Draw(gX_GUI_Element_Text *text, gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    gX_GUI_Element_Text_Prepared prepared = gX_GUI_Element_Text_Prepare(text, element, false);
    gX_Font      font      = prepared.font;
    gX_F32       fontsize  = prepared.fontsize;
    gX_F32       spacing   = prepared.spacing;
    gX_Rectangle rect      = prepared.rect;
    //opencode: drawBuf lives in this frame's copy — never keep a pointer
    //   stored in the returned struct (return-by-value would dangle).
    const gX_I8 *drawText  = prepared.textFromBuf ? prepared.drawBuf : text->text;
    gX_F32v2     textlength = prepared.measure;
    gX_F32v2     offset = (gX_F32v2){0,0};
    gX_F32       angle  = 0.0f;
    gX_I1 shadow = (element->stateRendering.TEXT_SHADOW || gX_GUI_TextShadow_Forced())
        && prepared.shadowColor.a > 0;
    gX_I1 outline = prepared.outlineWidth > 0.01f && prepared.outlineColor.a > 0;
    gX_I1 softShadow = shadow && prepared.shadowSoftness > 0.01f;
    
    //opencode: text color uses only the text's own color (not multiplied by element).
    //   Element color was previously multiplied causing white text to appear tinted.
    gX_Color  textcolor = text->color;
    //opencode: button labels use the shared white control color and full alpha;
    //   text-only labels retain their theme color.
    if (element->type == gX_GUI_ELEMENT_TYPE_BUTTON && !element->stateRendering.RECT_NO_DRAW) {
        textcolor = gX_WHITE;
        //opencode: disabled buttons dim their label too — the forced white
        //   ignored DISABLED, so grayed buttons kept shouting (saves audit).
        if (element->state.DISABLED) {
            textcolor.r = (gX_U8)((gX_U32)textcolor.r * 11 / 20);
            textcolor.g = (gX_U8)((gX_U32)textcolor.g * 11 / 20);
            textcolor.b = (gX_U8)((gX_U32)textcolor.b * 11 / 20);
        }
        textcolor.a = (gX_U8)(((gX_U32)textcolor.a * canvas->canvasHue.a) / 255);
    } else {
        textcolor = gX_Color_Mul(textcolor, canvas->canvasHue);
    }
    
    //opencode: multi-line — count newlines for correct centering.
    //   a11y line-height token: an explicit lineSpacing (extra multiplier over
    //   1.0) wins; the default 0 falls back to the theme's typeLineHeight
    //   token so multi-line text honors WCAG 1.4.12 spacing.
    gX_F32 lineHeightMul = (text->lineSpacing != 0.0f) ? (1.0f + text->lineSpacing) : gui.style.a11y.typeLineHeight;
    if (lineHeightMul <= 0.0f) lineHeightMul = 1.0f;
    gX_F32 lineH = fontsize * lineHeightMul;
    gX_I1 isMultiLine = (strchr(drawText, '\n') != NULL);
    gX_F32 totalTextH = textlength.y;
    gX_U32 lineCount = 1;
    if (isMultiLine) {
        for (const gX_I8 *p = drawText; *p; p++) if (*p == '\n') lineCount++;
        totalTextH = (gX_F32)lineCount * lineH;
    }
    
    gX_F32v2 textbox = (gX_F32v2){0,0};
    textbox.x = rect.x+text->offset.x;
    textbox.y = rect.y+text->offset.y;
    
    //opencode: for multi-line, X centering is handled per-line below (each line centers itself)
    if (!isMultiLine && element->stateRendering.TEXT_CENTER_X) {
        textbox.x += 0.50f*(rect.width - textlength.x);
    }
    if (element->stateRendering.TEXT_CENTER_Y) { textbox.y += 0.50f*(rect.height - totalTextH); }
    
    if (isMultiLine) {
        gX_I8 lineBuf[gX_GUI_ELEMENT_TEXT_MAX];
        const gX_I8 *src = drawText;
        gX_F32 ly = textbox.y;
        gX_I1 firstLine = 1;
        while (*src) {
            gX_U32 lineLen = 0;
            while (src[lineLen] && src[lineLen] != '\n') lineLen++;
            strncpy(lineBuf, src, lineLen);
            lineBuf[lineLen] = '\0';
            gX_F32v2 ll = gX_RawString_Measure(lineBuf, font, fontsize, spacing);
            gX_F32 lx = rect.x + text->offset.x;
            if (element->stateRendering.TEXT_CENTER_X) lx += (rect.width - ll.x) * 0.5f;
            if (!firstLine) ly += lineH;
            firstLine = 0;
            if (outline) {
                gX_RawString_Draw_Outline(lineBuf, font, (gX_F32v2){lx, ly},
                    offset, angle, fontsize, spacing, prepared.outlineWidth,
                    prepared.outlineColor);
            }
            if (softShadow) {
                gX_RawString_Draw_ShadowSoft(lineBuf, font, (gX_F32v2){lx, ly},
                    offset, angle, fontsize, spacing, textcolor,
                    prepared.shadowOffset, prepared.shadowColor,
                    prepared.shadowSoftness);
            } else if (shadow) {
                gX_RawString_Draw_Shadow(lineBuf, font, (gX_F32v2){lx, ly}, offset, angle,
                    fontsize, spacing, textcolor, prepared.shadowOffset, prepared.shadowColor);
            } else {
                gX_RawString_Draw(lineBuf, font, (gX_F32v2){lx, ly}, offset, angle,
                    fontsize, spacing, textcolor);
            }
            src += lineLen;
            if (*src == '\n') src++;
        }
    } else {
        if (outline) {
            gX_RawString_Draw_Outline(drawText, font, textbox, offset, angle,
                fontsize, spacing, prepared.outlineWidth, prepared.outlineColor);
        }
        if (softShadow) {
            gX_RawString_Draw_ShadowSoft(drawText, font, textbox, offset, angle,
                fontsize, spacing, textcolor, prepared.shadowOffset,
                prepared.shadowColor, prepared.shadowSoftness);
        }
        else if (shadow) {
            gX_RawString_Draw_Shadow(drawText,font,textbox,offset,angle,
                                     fontsize,spacing,textcolor,prepared.shadowOffset,prepared.shadowColor);
        }
        else gX_RawString_Draw(drawText,font,textbox,offset,angle,fontsize,spacing,textcolor);
    }
    
}

gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEAR(      Text,text)
gX_GUI_ELEMENT_MODULE_FUNCDEF_GET(        Text,text)
gX_GUI_ELEMENT_MODULE_FUNCDEF_ADD(        Text,text,TEXT)
gX_GUI_ELEMENT_MODULE_FUNCDEF_REMOVE(     Text,text)
gX_GUI_ELEMENT_MODULE_FUNCDEF_CLEARMODULE(Text,text)
//opencode: inlined from LOOP_CANVAS macro for navigability
gX_I0 gX_GUI_Element_Draw_Texts(gX_GUI_Element *element, gX_GUI_Canvas *canvas);
gX_I0 gX_GUI_Element_Draw_Texts(gX_GUI_Element *element, gX_GUI_Canvas *canvas) {
    gX_Heap heapStruct = element->texts;
    if (heapStruct.amount > 0) {
        gX_GUI_Element_Text *heapPtr = heapStruct.data;
        for (gX_U32 i = 0; i < heapStruct.amount; i++) {
            gX_GUI_Element_Text *elementmodule = &heapPtr[i];
            gX_GUI_Element_Text_Draw(elementmodule,element,canvas);
        }
    }
}
