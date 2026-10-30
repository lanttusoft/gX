/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_textwriter.cpp]
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


//ai: tour textwriter API — file/line control, reveal, fonts and choices for
//   the textwriter module (renderer.textwriter.data; the module draws itself
//   in SCENE_RENDERER_TEXTWRITER), plus a script-facing tagged-draw overlay:
//   TextWriter_DrawTagged queues tagged strings that a shell draw hook
//   replays between the renderer and the GUI (immediate draws during the
//   update would be erased by the renderer's canvas clear). Colour, size and
//   <font path> tags are honored; layout offsets/effects/matrices belong to
//   the module's own draw path. Choices are bound: lines with
//   <option line='N'>text</option> entries expose TextWriter_HasChoices /
//   GetChoiceCount / GetChoiceText, and SelectChoice jumps to the option's
//   target line (scripts draw the option list themselves, e.g. via
//   TextWriter_DrawTagged).
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through other includes would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "tour.h"

#include <angelscript.h>

#include <stdio.h>
#include <string>
#include <string.h>

#include "../../common/log.h"
#include "../../main.h"
#include "../../renderer/renderer.h"
#include "../../renderer/textwriter.h"
#include "../../wrappers/text.h"

// ===========================================================================
// Instance
// ===========================================================================

static gX_RendererTextWriter *TourTextWriter_Instance(void) {
    if (!renderer.textwriter.data || renderer.textwriter.amount == 0) return NULL;
    return (gX_RendererTextWriter *)renderer.textwriter.data;
}

static gX_Font TourTextWriter_ActiveFont(gX_RendererTextWriter *tw) {
    if (tw->hasCustomFont) return tw->customFont;
    return gX_Font_GetDefault();
}

// ===========================================================================
// Per-word font cache — resolves <font path='...'/> inside tagged strings
// ===========================================================================

#define TOUR_TW_FONT_CACHE 4

static struct {
    gX_I8   path[256];
    gX_Font font;
    gX_I1   loaded;
    gX_I1   failed;
} s_twFonts[TOUR_TW_FONT_CACHE];

static gX_Font TourTextWriter_FontForPath(const gX_I8 *path) {
    for (gX_I32 i = 0; i < TOUR_TW_FONT_CACHE; i++) {
        if (!s_twFonts[i].path[0]) continue;
        if (strcmp((const char *)s_twFonts[i].path, (const char *)path)) continue;
        if (s_twFonts[i].loaded) return s_twFonts[i].font;
        return gX_Font_GetDefault();
    }
    for (gX_I32 i = 0; i < TOUR_TW_FONT_CACHE; i++) {
        if (s_twFonts[i].path[0]) continue;
        snprintf((char *)s_twFonts[i].path, sizeof(s_twFonts[i].path), "%s", path);
        s_twFonts[i].font = gX_Font_Load(path);
        s_twFonts[i].loaded = gX_Font_IsReady(s_twFonts[i].font);
        if (!s_twFonts[i].loaded) {
            s_twFonts[i].failed = 1;
            gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "TextWriter: font load failed '%s'", path);
            return gX_Font_GetDefault();
        }
        return s_twFonts[i].font;
    }
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "TextWriter: font cache full, using default for '%s'", path);
    return gX_Font_GetDefault();
}

static void TourTextWriter_ClearFontCache(void) {
    for (gX_I32 i = 0; i < TOUR_TW_FONT_CACHE; i++) {
        if (s_twFonts[i].loaded) gX_Font_Unload(s_twFonts[i].font);
        s_twFonts[i].path[0] = '\0';
        s_twFonts[i].loaded = 0;
        s_twFonts[i].failed = 0;
    }
}

// ===========================================================================
// Tagged text helpers
// ===========================================================================

static gX_I32 TourTextWriter_WordToUtf8(const gX_TextWriter_ProcessedLine *out,
    const gX_TextWriter_Word *w, gX_I8 *buf, gX_I32 cap)
{
    gX_I32 len = 0;
    for (gX_U32 gi = w->glyphStart; gi < w->glyphStart + w->glyphCount; gi++) {
        if (gi >= out->glyphsAmount) break;
        gX_I32 cp = out->glyphs[gi].codepoint;
        if (cp < 0x80) {
            if (len + 1 >= cap) break;
            buf[len++] = (gX_I8)cp;
        } else if (cp < 0x800) {
            if (len + 2 >= cap) break;
            buf[len++] = (gX_I8)(0xC0 | (cp >> 6));
            buf[len++] = (gX_I8)(0x80 | (cp & 0x3F));
        } else {
            if (len + 3 >= cap) break;
            buf[len++] = (gX_I8)(0xE0 | (cp >> 12));
            buf[len++] = (gX_I8)(0x80 | ((cp >> 6) & 0x3F));
            buf[len++] = (gX_I8)(0x80 | (cp & 0x3F));
        }
    }
    buf[len] = '\0';
    return len;
}

static gX_Font TourTextWriter_WordFont(gX_RendererTextWriter *tw, const gX_TextWriter_Word *w) {
    if (w->fontPath[0]) return TourTextWriter_FontForPath(w->fontPath);
    return TourTextWriter_ActiveFont(tw);
}

static gX_I32 TourTextWriter_Parse(const std::string &text, gX_RendererTextWriter *tw,
    gX_TextWriter_ProcessedLine *out)
{
    gX_TextWriter_ParseState final;
    gX_TextWriter_ParseState initial = tw->parseState;
    return gX_TextWriter_ParseLine(text.c_str(), out, &initial, &final);
}

// ===========================================================================
// File + line control
// ===========================================================================

static void TourTextWriter_LoadFile(const std::string &path) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) gX_TextWriter_LoadFile(tw, path.c_str());
}

static void TourTextWriter_LoadFileTXT(const std::string &path) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) gX_TextWriter_LoadFileTXT(tw, path.c_str());
}

static void TourTextWriter_LoadFileYAML(const std::string &path) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) gX_TextWriter_LoadFileYAML(tw, path.c_str());
}

static void TourTextWriter_UnloadFile(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) gX_TextWriter_UnloadFile(tw);
}

static gX_I1 TourTextWriter_IsLoaded(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? tw->textFileLoaded : 0;
}

static gX_I32 TourTextWriter_GetLine(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? (gX_I32)tw->lineBuffer.currentLine : 0;
}

static gX_I32 TourTextWriter_GetLineCount(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? (gX_I32)tw->lineBuffer.linesAmount : 0;
}

static std::string TourTextWriter_GetLineText(gX_I32 line) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw || line < 0 || line >= (gX_I32)tw->lineBuffer.linesAmount) return std::string();
    return std::string((const char *)tw->lineBuffer.lines[line]);
}

static void TourTextWriter_NextLine(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) gX_TextWriter_NextLine(tw);
}

static void TourTextWriter_JumpLine(gX_I32 line) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw && line >= 0) gX_TextWriter_JumpLine(tw, (gX_U32)line);
}

// ===========================================================================
// Choices
// ===========================================================================

static gX_I1 TourTextWriter_HasChoices(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? gX_TextWriter_HasChoices(tw) : 0;
}

static gX_I32 TourTextWriter_GetChoiceCount(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? (gX_I32)gX_TextWriter_GetChoiceCount(tw) : 0;
}

static std::string TourTextWriter_GetChoiceText(gX_I32 index) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    const gX_I8 *text = tw ? gX_TextWriter_GetChoiceText(tw, (gX_U32)index) : NULL;
    return text ? std::string((const char *)text) : std::string();
}

static gX_I32 TourTextWriter_SelectChoice(gX_I32 index) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? gX_TextWriter_SelectChoice(tw, (gX_U32)index) : 1;
}

// ===========================================================================
// Reveal
// ===========================================================================

static void TourTextWriter_SetReveal(gX_F32 v) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) tw->revealCursor = v;
}

static gX_F32 TourTextWriter_GetReveal(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? tw->revealCursor : 0.0f;
}

static void TourTextWriter_SetRevealSpeed(gX_F32 v) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) tw->revealSpeed = v;
}

static gX_F32 TourTextWriter_GetRevealSpeed(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? tw->revealSpeed : 0.0f;
}

static gX_I1 TourTextWriter_IsRevealComplete(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw) return 0;
    return (tw->revealCursor >= (gX_F32)tw->lineBuffer.processed.glyphsAmount) ? 1 : 0;
}

// ===========================================================================
// Fonts + colour
// ===========================================================================

static void TourTextWriter_SetFont(const std::string &path) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw) return;
    if (tw->hasCustomFont) gX_Font_Unload(tw->customFont);
    tw->customFont = (gX_Font){0};
    tw->hasCustomFont = 0;
    tw->fontLoadFailed = 0;
    snprintf((char *)tw->parseState.fontPath, sizeof(tw->parseState.fontPath), "%s", path.c_str());
    tw->customFont = gX_Font_Load(path.c_str());
    tw->hasCustomFont = gX_Font_IsReady(tw->customFont);
    if (!tw->hasCustomFont) {
        tw->fontLoadFailed = 1;
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "TextWriter_SetFont: font load failed '%s'", path.c_str());
    }
}

static void TourTextWriter_ResetFont(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw) return;
    if (tw->hasCustomFont) gX_Font_Unload(tw->customFont);
    tw->customFont = (gX_Font){0};
    tw->hasCustomFont = 0;
    tw->fontLoadFailed = 0;
    tw->parseState.fontPath[0] = '\0';
    TourTextWriter_ClearFontCache();
}

static gX_I1 TourTextWriter_HasFont(void) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    return tw ? tw->hasCustomFont : 0;
}

static void TourTextWriter_SetColor(gX_I32 r, gX_I32 g, gX_I32 b, gX_I32 a) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (tw) tw->parseState.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, (gX_U8)a};
}

// ===========================================================================
// Measures
// ===========================================================================

static gX_F32 TourTextWriter_MeasureText(const std::string &text, gX_F32 size) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw || text.empty()) return 0.0f;
    gX_F32v2 measure = gX_RawString_Measure(text.c_str(), TourTextWriter_ActiveFont(tw), size, 1.0f);
    return measure.x;
}

static gX_F32 TourTextWriter_MeasureTagged(const std::string &text, gX_F32 size) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw) return 0.0f;

    gX_TextWriter_ProcessedLine out;
    if (TourTextWriter_Parse(text, tw, &out) != 0) return 0.0f;

    gX_F32 total = 0.0f;
    gX_I8 wordBuf[512];
    for (gX_U32 wi = 0; wi < out.wordsAmount; wi++) {
        gX_TextWriter_Word *w = &out.words[wi];
        if (TourTextWriter_WordToUtf8(&out, w, wordBuf, (gX_I32)sizeof(wordBuf)) == 0) continue;
        gX_F32 wordSize = (w->size > 0.0f) ? w->size : size;
        total += gX_RawString_Measure(wordBuf, TourTextWriter_WordFont(tw, w), wordSize, 1.0f).x;
    }
    return total;
}

// ===========================================================================
// Tagged draw overlay (replayed from the shell draw hook)
// ===========================================================================

#define TOUR_TW_DRAWS_MAX 16

typedef struct TourTextWriter_Draw {
    std::string text;
    gX_F32 x;
    gX_F32 y;
    gX_F32 size;
    gX_Color color;
} TourTextWriter_Draw;

static TourTextWriter_Draw s_twDraws[TOUR_TW_DRAWS_MAX];
static gX_I32 s_twDrawCount = 0;

static void TourTextWriter_DrawTaggedAt(const std::string &text, gX_F32 x, gX_F32 y,
    gX_F32 size, gX_Color color)
{
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw || text.empty()) return;

    gX_TextWriter_ProcessedLine out;
    gX_TextWriter_ParseState final;
    gX_TextWriter_ParseState initial = tw->parseState;
    initial.color = color;
    if (gX_TextWriter_ParseLine(text.c_str(), &out, &initial, &final) != 0) return;

    gX_F32v2 pos = gX_F32v2_Make(x, y);
    gX_I8 wordBuf[512];
    for (gX_U32 wi = 0; wi < out.wordsAmount; wi++) {
        gX_TextWriter_Word *w = &out.words[wi];
        if (TourTextWriter_WordToUtf8(&out, w, wordBuf, (gX_I32)sizeof(wordBuf)) == 0) continue;
        gX_F32 wordSize = (w->size > 0.0f) ? w->size : size;
        gX_Font font = TourTextWriter_WordFont(tw, w);
        gX_RawString_Draw(wordBuf, font, pos, gX_F32v2_Make(0.0f, 0.0f), 0.0f,
            wordSize, 1.0f, w->color);
        pos.x += gX_RawString_Measure(wordBuf, font, wordSize, 1.0f).x;
    }
}

static gX_I0 TourTextWriter_Tick(gX_I0) {
    if (!TourTextWriter_Instance() || s_twDrawCount <= 0) return;
    for (gX_I32 i = 0; i < s_twDrawCount; i++) {
        TourTextWriter_DrawTaggedAt(s_twDraws[i].text, s_twDraws[i].x, s_twDraws[i].y,
            s_twDraws[i].size, s_twDraws[i].color);
    }
    return;
}

static void TourTextWriter_DrawTagged(const std::string &text, gX_F32 x, gX_F32 y, gX_F32 size) {
    gX_RendererTextWriter *tw = TourTextWriter_Instance();
    if (!tw || text.empty()) return;
    if (s_twDrawCount >= TOUR_TW_DRAWS_MAX) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "TextWriter_DrawTagged: list full (%d)", TOUR_TW_DRAWS_MAX);
        return;
    }
    TourTextWriter_Draw *d = &s_twDraws[s_twDrawCount++];
    d->text = text;
    d->x = x;
    d->y = y;
    d->size = size;
    d->color = tw->parseState.color;
}

static void TourTextWriter_ClearDraws(void) {
    s_twDrawCount = 0;
}

static gX_I32 TourTextWriter_GetDrawCount(void) {
    return s_twDrawCount;
}

// ===========================================================================
// Registration
// ===========================================================================

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS tour-textwriter registration failed (code %d)", r); \
} while(0)

gX_I32 gX_TourTextWriter_Register(asIScriptEngine *engine) {
    static gX_I1 hooked = 0;
    if (!hooked) {
        if (gX_Scripting_RegisterDrawHook("tour_textwriter", TourTextWriter_Tick) == 0) {
            hooked = 1;
        }
    }

    int r = 0;

    // --- File + lines ---
    r = engine->RegisterGlobalFunction("void TextWriter_LoadFile(const string &in)",
        asFUNCTION(TourTextWriter_LoadFile), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_LoadFileTXT(const string &in)",
        asFUNCTION(TourTextWriter_LoadFileTXT), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_LoadFileYAML(const string &in)",
        asFUNCTION(TourTextWriter_LoadFileYAML), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_UnloadFile()",
        asFUNCTION(TourTextWriter_UnloadFile), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool TextWriter_IsLoaded()",
        asFUNCTION(TourTextWriter_IsLoaded), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int TextWriter_GetLine()",
        asFUNCTION(TourTextWriter_GetLine), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int TextWriter_GetLineCount()",
        asFUNCTION(TourTextWriter_GetLineCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string TextWriter_GetLineText(int)",
        asFUNCTION(TourTextWriter_GetLineText), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_NextLine()",
        asFUNCTION(TourTextWriter_NextLine), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_JumpLine(int)",
        asFUNCTION(TourTextWriter_JumpLine), asCALL_CDECL); AS_CHECK(r);

    // --- Choices (<option line='N'>text</option> entries on the line) ---
    r = engine->RegisterGlobalFunction("bool TextWriter_HasChoices()",
        asFUNCTION(TourTextWriter_HasChoices), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int TextWriter_GetChoiceCount()",
        asFUNCTION(TourTextWriter_GetChoiceCount), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string TextWriter_GetChoiceText(int)",
        asFUNCTION(TourTextWriter_GetChoiceText), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int TextWriter_SelectChoice(int)",
        asFUNCTION(TourTextWriter_SelectChoice), asCALL_CDECL); AS_CHECK(r);

    // --- Reveal ---
    r = engine->RegisterGlobalFunction("void TextWriter_SetReveal(float)",
        asFUNCTION(TourTextWriter_SetReveal), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float TextWriter_GetReveal()",
        asFUNCTION(TourTextWriter_GetReveal), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_SetRevealSpeed(float)",
        asFUNCTION(TourTextWriter_SetRevealSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float TextWriter_GetRevealSpeed()",
        asFUNCTION(TourTextWriter_GetRevealSpeed), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool TextWriter_IsRevealComplete()",
        asFUNCTION(TourTextWriter_IsRevealComplete), asCALL_CDECL); AS_CHECK(r);

    // --- Fonts + colour ---
    r = engine->RegisterGlobalFunction("void TextWriter_SetFont(const string &in)",
        asFUNCTION(TourTextWriter_SetFont), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_ResetFont()",
        asFUNCTION(TourTextWriter_ResetFont), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool TextWriter_HasFont()",
        asFUNCTION(TourTextWriter_HasFont), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_SetColor(int, int, int, int)",
        asFUNCTION(TourTextWriter_SetColor), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float TextWriter_MeasureText(const string &in, float)",
        asFUNCTION(TourTextWriter_MeasureText), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float TextWriter_MeasureTagged(const string &in, float)",
        asFUNCTION(TourTextWriter_MeasureTagged), asCALL_CDECL); AS_CHECK(r);

    // --- Tagged draw overlay ---
    r = engine->RegisterGlobalFunction("void TextWriter_DrawTagged(const string &in, float, float, float)",
        asFUNCTION(TourTextWriter_DrawTagged), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void TextWriter_ClearDraws()",
        asFUNCTION(TourTextWriter_ClearDraws), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int TextWriter_GetDrawCount()",
        asFUNCTION(TourTextWriter_GetDrawCount), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered tour textwriter API (files, reveal, fonts, tagged draw)");
    return 0;
}
