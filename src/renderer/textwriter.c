/**********************************************************************************************
*
*   [gX: src/renderer/textwriter.c]
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
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

#include "textwriter.h"

#include "../common/enum.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/text.h"
#include "../common/data.h"

#include "../common/path.h"
#include "../wrappers/wrappers.h"
#include "../rhi/batch.h"

//opencode: N64 backend — replace engine deps, override log macros
  #include "renderer.h"
  #include "../main.h"
#include "../rhi/rhi.h"
  //opencode: forward-declare raylib alloc macros (avoid full raylib.h due to Matrix conflict)
  extern void *malloc(size_t size);
  extern void free(void *ptr);

//opencode: identity transform — no scaling, rotation, shear, or translation
static gX_TextWriter_Transform gX_TextWriter_Transform_Identity(gX_I0) {
    gX_TextWriter_Transform t;
    t.a = 1.0f; t.b = 0.0f;
    t.c = 0.0f; t.d = 1.0f;
    t.tx = 0.0f; t.ty = 0.0f;
    return t;
}

//opencode: check if a transform is identity (for fast-path in drawing)
static gX_I1 gX_TextWriter_Transform_IsIdentity(const gX_TextWriter_Transform *t) {
    return (t->a == 1.0f && t->b == 0.0f && t->c == 0.0f
         && t->d == 1.0f && t->tx == 0.0f && t->ty == 0.0f);
}

//opencode: apply a 2D affine transform to a position (in-place)
static gX_I0 gX_TextWriter_Transform_Apply(gX_TextWriter_Transform *t, gX_F32v2 *pos) {
    gX_F32 x = pos->x;
    gX_F32 y = pos->y;
    pos->x = t->a * x + t->c * y + t->tx;
    pos->y = t->b * x + t->d * y + t->ty;
}

//opencode: parse a key="value" pair from a tag buffer starting at pos
static gX_U32 gX_TextWriter_ParseAttr(const gX_I8 *tag, gX_U32 pos, gX_I8 *key,
                                       gX_U32 keyMax, gX_I8 *val, gX_U32 valMax) {
    gX_U32 kpos = 0;
    gX_U32 vpos = 0;
    while (tag[pos] && (tag[pos] == ' ' || tag[pos] == '\t')) pos++;
    if (!tag[pos] || tag[pos] == '>' || tag[pos] == '/') return pos;
    while (tag[pos] && tag[pos] != '=' && tag[pos] != ' ' && tag[pos] != '\t'
           && tag[pos] != '>' && tag[pos] != '/') {
        if (kpos < keyMax - 1) key[kpos++] = tag[pos];
        pos++;
    }
    key[kpos] = '\0';
    if (tag[pos] != '=') return pos;
    pos++;
    while (tag[pos] && (tag[pos] == ' ' || tag[pos] == '\t')) pos++;
    gX_I8 quote = tag[pos];
    if (quote != '"' && quote != '\'') return pos;
    pos++;
    while (tag[pos] && tag[pos] != quote) {
        if (vpos < valMax - 1) val[vpos++] = tag[pos];
        pos++;
    }
    val[vpos] = '\0';
    if (tag[pos] == quote) pos++;
    return pos;
}

//opencode: choice collection — <option line='N'>text</option> entries on the
//   current line fill the writer-level list. The line parser has no writer
//   handle (it writes a ProcessedLine), so this scanner reads the raw line
//   text instead. <choice> is a marker only; a line offers choices when at
//   least one option was collected. Option text is copied verbatim (plain
//   text preferred; nested tags would show up in the returned string).
static gX_I0 gX_TextWriter_ScanChoices(gX_RendererTextWriter *writer, const gX_I8 *line) {
    writer->hasChoices = false;
    writer->choiceCount = 0;
    writer->selectedChoice = -1;
    if (!line) return;

    const gX_I8 *p = line;
    while ((p = strstr(p, "<option")) != NULL) {
        const gX_I8 nxt = p[7];
        if (nxt != ' ' && nxt != '\t' && nxt != '>' && nxt != '/') {
            p += 7;
            continue;
        }
        if (writer->choiceCount >= GX_TEXTWRITER_MAX_CHOICES) break;

        gX_I32 target = -1;
        gX_I8 key[32] = {0};
        gX_I8 val[32] = {0};
        gX_U32 pos = 7; // after "<option"
        while (p[pos] && p[pos] != '>') {
            pos = gX_TextWriter_ParseAttr(p, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if (!strcmp(key, "line")) target = (gX_I32)atoi(val);
            key[0] = '\0';
        }
        while (p[pos] && p[pos] != '>') pos++;
        if (p[pos] == '>') pos++;

        const gX_I8 *textStart = p + pos;
        const gX_I8 *textEnd = strstr(textStart, "</option>");
        const gX_I8 *next;
        if (textEnd) {
            next = textEnd + 9; // after "</option>"
        } else {
            textEnd = textStart + strlen(textStart); // unterminated: take the rest
            next = textEnd;
        }

        gX_U32 textLen = (gX_U32)(textEnd - textStart);
        if (textLen >= sizeof(writer->choiceText[0])) {
            textLen = sizeof(writer->choiceText[0]) - 1;
        }
        memcpy(writer->choiceText[writer->choiceCount], textStart, textLen);
        writer->choiceText[writer->choiceCount][textLen] = '\0';
        writer->choiceLines[writer->choiceCount] = target;
        writer->choiceCount++;
        p = next;
    }

    if (writer->choiceCount > 0) {
        writer->hasChoices = true;
        writer->selectedChoice = 0;
    }
}

//opencode: apply a tag to the parse state
//tagType: 0=opening, 1=self-closing
static gX_I0 gX_TextWriter_ApplyTag(gX_TextWriter_ParseState *state,
                                     const gX_I8 *tag, gX_I32 tagType) {
    (void)tagType;
    gX_I8 key[64], val[64];
    gX_U32 pos = 0;

    gX_I8 name[64];
    gX_U32 npos = 0;
    while (tag[pos] && (tag[pos] == '<' || tag[pos] == ' ' || tag[pos] == '\t')) pos++;
    while (tag[pos] && tag[pos] != ' ' && tag[pos] != '\t' && tag[pos] != '>' && tag[pos] != '/') {
        if (npos < 63) name[npos++] = tag[pos];
        pos++;
    }
    name[npos] = '\0';

    //shorthand color names
    if (!strcmp(name, "red"))    { state->color = gX_RED;    return; }
    if (!strcmp(name, "green"))  { state->color = gX_GREEN;  return; }
    if (!strcmp(name, "blue"))   { state->color = gX_BLUE;   return; }
    if (!strcmp(name, "white"))  { state->color = gX_WHITE;  return; }
    if (!strcmp(name, "yellow")) { state->color = gX_YELLOW; return; }
    if (!strcmp(name, "cyan"))   { state->color = gX_CYAN;   return; }
    if (!strcmp(name, "magenta")){ state->color = gX_MAGENTA; return; }

    //opencode: italic shorthand — negative c for right-leaning shear
    if (!strcmp(name, "i") || !strcmp(name, "italic")) {
        state->matrix = gX_TextWriter_Transform_Identity();
        state->matrix.c = GX_TEXTWRITER_ITALIC_SHEAR;
        return;
    }

    //opencode: bold shorthand — wider glyphs via horizontal scale
    if (!strcmp(name, "b") || !strcmp(name, "bold")) {
        state->matrix = gX_TextWriter_Transform_Identity();
        state->matrix.a = GX_TEXTWRITER_BOLD_SCALE;
        return;
    }

    //opencode: underline shorthand
    if (!strcmp(name, "u") || !strcmp(name, "underline")) {
        state->underline = true;
        return;
    }

    //opencode: ruby annotation — <ruby rt='text' />
    if (!strcmp(name, "ruby")) {
        while (tag[pos]) {
            pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if (!strcmp(key, "rt")) {
                strncpy(state->rubyText, val, sizeof(state->rubyText) - 1); state->rubyText[sizeof(state->rubyText) - 1] = '\0';
            }
            key[0] = '\0';
        }
        return;
    }

    //opencode: special effects — set effect flags on the word
    if (!strcmp(name, "rainbow")) {
        state->effects |= GX_TEXTWRITER_EFFECT_RAINBOW;
        return;
    }
    if (!strcmp(name, "shake")) {
        state->effects |= GX_TEXTWRITER_EFFECT_SHAKE;
        return;
    }
    if (!strcmp(name, "disintegrate")) {
        state->effects |= GX_TEXTWRITER_EFFECT_DISINTEGRATE;
        return;
    }
    if (!strcmp(name, "wave")) {
        state->effects |= GX_TEXTWRITER_EFFECT_WAVE;
        while (tag[pos]) {
            pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if      (!strcmp(key,"amp"))  state->waveAmp  = (gX_F32)atof(val);
            else if (!strcmp(key,"freq")) state->waveFreq = (gX_F32)atof(val);
            key[0] = '\0';
        }
        return;
    }
    if (!strcmp(name, "gradient")) {
        state->effects |= GX_TEXTWRITER_EFFECT_GRADIENT;
        while (tag[pos]) {
            pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if      (!strcmp(key,"r")) state->gradientEnd.r = (gX_U8)atoi(val);
            else if (!strcmp(key,"g")) state->gradientEnd.g = (gX_U8)atoi(val);
            else if (!strcmp(key,"b")) state->gradientEnd.b = (gX_U8)atoi(val);
            else if (!strcmp(key,"a")) state->gradientEnd.a = (gX_U8)atoi(val);
            key[0] = '\0';
        }
        return;
    }

    //opencode: choice system — <choice> is a marker; the <option line='N'>
    //   entries are collected at the writer level (gX_TextWriter_ScanChoices)
    if (!strcmp(name, "choice")) {
        return;
    }

    //opencode: jump tag — <jump line='N' /> sets a line jump target
    if (!strcmp(name, "jump")) {
        while (tag[pos]) {
            pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if (!strcmp(key, "line")) state->jumpTarget = (gX_I32)atoi(val);
            key[0] = '\0';
        }
        return;
    }

    //opencode: matrix tag — set affine transform components.
    // <matrix a="" b="" c="" d="" tx="" ty="" />  (defaults=identity)
    if (!strcmp(name, "matrix")) {
        gX_TextWriter_Transform t = gX_TextWriter_Transform_Identity();
        while (tag[pos]) {
            pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
            if (!key[0]) break;
            if      (!strcmp(key,"a"))  t.a  = (gX_F32)atof(val);
            else if (!strcmp(key,"b"))  t.b  = (gX_F32)atof(val);
            else if (!strcmp(key,"c"))  t.c  = (gX_F32)atof(val);
            else if (!strcmp(key,"d"))  t.d  = (gX_F32)atof(val);
            else if (!strcmp(key,"tx")) t.tx = (gX_F32)atof(val);
            else if (!strcmp(key,"ty")) t.ty = (gX_F32)atof(val);
            key[0] = '\0';
        }
        state->matrix = t;
        return;
    }

    //parse attributes for color/offset/size/font tags
    while (tag[pos]) {
        pos = gX_TextWriter_ParseAttr(tag, pos, key, sizeof(key), val, sizeof(val));
        if (!key[0]) break;

        if (!strcmp(name, "color")) {
            gX_U32 r = state->color.r, g = state->color.g, b = state->color.b, a = state->color.a;
            if (!strcmp(key,"r")) r = (gX_U32)atoi(val);
            if (!strcmp(key,"g")) g = (gX_U32)atoi(val);
            if (!strcmp(key,"b")) b = (gX_U32)atoi(val);
            if (!strcmp(key,"a")) a = (gX_U32)atoi(val);
            state->color = (gX_Color){(gX_U8)r,(gX_U8)g,(gX_U8)b,(gX_U8)a};
        }
        else if (!strcmp(name, "offset")) {
            if (!strcmp(key,"x")) state->offset.x = (gX_F32)atof(val);
            if (!strcmp(key,"y")) state->offset.y = (gX_F32)atof(val);
        }
        else if (!strcmp(name, "size")) {
            if (!strcmp(key,"v")) state->size = (gX_F32)atof(val);
        }
        else if (!strcmp(name, "spacing")) {
            if (!strcmp(key,"v")) state->spacing = (gX_F32)atof(val);
        }
        else if (!strcmp(name, "font")) {
            if (!strcmp(key,"path")) {
                strncpy(state->fontPath, val, sizeof(state->fontPath) - 1);
            }
        }
        key[0] = '\0';
    }
}

//opencode: helper — snapshot current parse state into a word
static gX_I0 gX_TextWriter_CloseWord(gX_TextWriter_ProcessedLine *out,
                                      gX_U32 wordStart, gX_U32 wordEnd,
                                      gX_TextWriter_ParseState *ps) {
    if (wordEnd <= wordStart) return;
    if (out->wordsAmount >= GX_TEXTWRITER_WORDS_MAX) return;
    gX_TextWriter_Word *w = &out->words[out->wordsAmount++];
    w->glyphStart = wordStart;
    w->glyphCount = wordEnd - wordStart;
    w->color      = ps->color;
    w->offset     = ps->offset;
    w->size       = ps->size;
    w->spacing    = ps->spacing;
    w->matrix     = ps->matrix;
    w->underline  = ps->underline;
    w->effects    = ps->effects;
    w->gradientEnd = ps->gradientEnd;
    w->waveAmp    = ps->waveAmp;
    w->waveFreq   = ps->waveFreq;
    snprintf(w->rubyText, sizeof(w->rubyText), "%s", ps->rubyText);
    snprintf(w->fontPath, sizeof(w->fontPath), "%s", ps->fontPath);
}

//opencode: close the current word if one is open, returning the next
//   glyph index as the candidate word start.
static gX_U32 gX_TextWriter_MaybeCloseWord(gX_TextWriter_ProcessedLine *out,
                                             gX_U32 wordStart, gX_U32 gi,
                                             gX_TextWriter_ParseState *ps) {
    if (gi > wordStart) {
        gX_TextWriter_CloseWord(out, wordStart, gi, ps);
    }
    return gi;
}

//opencode: decode one UTF-8 codepoint from line[ci], return codepoint and
//   advance ci by the byte count.
static gX_I32 gX_TextWriter_DecodeUTF8(const gX_I8 *line, gX_U32 len,
                                         gX_U32 *ci) {
    gX_I32 codepoint = (gX_I32)(gX_U8)line[*ci];
    gX_U32 bytes = 1;
    if ((codepoint & 0xE0) == 0xC0) { bytes = 2; codepoint &= 0x1F; }
    else if ((codepoint & 0xF0) == 0xE0) { bytes = 3; codepoint &= 0x0F; }
    else if ((codepoint & 0xF8) == 0xF0) { bytes = 4; codepoint &= 0x07; }
    for (gX_U32 b = 1; b < bytes && *ci + b < len; b++) {
        codepoint = (codepoint << 6) | (line[*ci + b] & 0x3F);
    }
    *ci += bytes;
    return codepoint;
}

//opencode: encode one codepoint to UTF-8 bytes in ch[8]
static gX_I0 gX_TextWriter_EncodeUTF8(gX_I32 codepoint, gX_I8 ch[8]) {
    gX_U32 c = (gX_U32)codepoint;
    if (c < 0x80) {
        ch[0] = (gX_I8)c;
        ch[1] = '\0';
    } else if (c < 0x800) {
        ch[0] = (gX_I8)(0xC0 | (c >> 6));
        ch[1] = (gX_I8)(0x80 | (c & 0x3F));
        ch[2] = '\0';
    } else {
        ch[0] = (gX_I8)(0xE0 | (c >> 12));
        ch[1] = (gX_I8)(0x80 | ((c >> 6) & 0x3F));
        ch[2] = (gX_I8)(0x80 | (c & 0x3F));
        ch[3] = '\0';
    }
}

//opencode: parse one text line with inline XML-like tags into glyph + word
//   arrays. Words split at formatting changes (tags) and at spaces.
//   Spaces become part of the preceding word.
//   Supports: <color>, <size>, <offset>, <font>, <matrix>, color shorthands
gX_I32 gX_TextWriter_ParseLine(const gX_I8 *line, gX_TextWriter_ProcessedLine *out,
                                gX_TextWriter_ParseState *initial,
                                gX_TextWriter_ParseState *final) {
    gX_TextWriter_ParseState state;
    if (initial) state = *initial;
    else {
        state.color    = (gX_Color){255,255,255,255};
        state.offset   = (gX_F32v2){0,0};
        state.size     = GX_TEXTWRITER_DEFAULT_SIZE;
        state.spacing  = 0.0f;
        state.fontPath[0] = '\0';
        state.matrix   = gX_TextWriter_Transform_Identity();
        state.rubyText[0] = '\0';
        state.underline = false;
        state.effects  = 0;
        state.gradientEnd = (gX_Color){255,255,255,255};
        state.waveAmp  = GX_TEXTWRITER_DEFAULT_AMP;
        state.waveFreq = GX_TEXTWRITER_DEFAULT_FREQ;
        state.jumpTarget = -1;
    }

    gX_TextWriter_ParseState stateStack[GX_TEXTWRITER_STATE_STACK];
    gX_U32 stackDepth = 0;

    out->glyphsAmount = 0;
    out->wordsAmount  = 0;
    gX_U32 gi = 0;
    gX_U32 wordStart = 0; //glyph index where the current word began

    gX_U32 len = (gX_U32)strlen(line);
    gX_U32 ci = 0;
    while (ci < len && gi < GX_TEXTWRITER_GLYPHS_MAX) {
        if (line[ci] == '<') {
            //collect tag
            gX_I8 tag[256];
            gX_U32 tpos = 0;
            tag[tpos++] = line[ci++];
            while (ci < len && line[ci] != '>' && tpos < 254) {
                tag[tpos++] = line[ci++];
            }
            if (ci < len && line[ci] == '>') {
                tag[tpos++] = line[ci++];
            }
            tag[tpos] = '\0';

            //save snapshot BEFORE applying the tag — this is the formatting
            //for the glyphs since the last word boundary
            gX_TextWriter_ParseState preState = state;

            if (tpos >= 2 && tag[1] == '/') {
                //closing tag: </tagname> or </>
                if (stackDepth > 0) {
                    stackDepth--;
                    state = stateStack[stackDepth];
                } else {
                    state.color    = (gX_Color){255,255,255,255};
                    state.offset   = (gX_F32v2){0,0};
                    state.size     = GX_TEXTWRITER_DEFAULT_SIZE;
                    state.spacing  = 0.0f;
                    state.matrix   = gX_TextWriter_Transform_Identity();
                    state.rubyText[0] = '\0';
                    state.underline = false;
                    state.effects  = 0;
                    state.gradientEnd = (gX_Color){255,255,255,255};
                    state.waveAmp  = GX_TEXTWRITER_DEFAULT_AMP;
                    state.waveFreq = GX_TEXTWRITER_DEFAULT_FREQ;
                    state.jumpTarget = -1;
                }
            }
            else if (tpos >= 2 && tag[tpos-2] == '/') {
                gX_TextWriter_ApplyTag(&state, tag, 1);
            }
            else {
                if (stackDepth < GX_TEXTWRITER_STATE_STACK) {
                    stateStack[stackDepth++] = state;
                }
                gX_TextWriter_ApplyTag(&state, tag, 0);
            }

            //opencode: if formatting changed, close current word and start new one
            gX_I1 fmtChanged =
                (preState.color.r != state.color.r ||
                 preState.color.g != state.color.g ||
                 preState.color.b != state.color.b ||
                 preState.color.a != state.color.a) ||
                (preState.offset.x != state.offset.x ||
                 preState.offset.y != state.offset.y) ||
                (preState.size != state.size) ||
                (preState.spacing != state.spacing) ||
                (preState.matrix.a != state.matrix.a ||
                 preState.matrix.b != state.matrix.b ||
                 preState.matrix.c != state.matrix.c ||
                 preState.matrix.d != state.matrix.d ||
                 preState.matrix.tx != state.matrix.tx ||
                 preState.matrix.ty != state.matrix.ty) ||
                (preState.underline != state.underline) ||
                (preState.effects != state.effects) ||
                (strcmp(preState.rubyText, state.rubyText) != 0) ||
                (strcmp(preState.fontPath, state.fontPath) != 0);
            if (fmtChanged && gi > wordStart) {
                gX_TextWriter_CloseWord(out, wordStart, gi, &preState);
                wordStart = gi;
            }
        }
        else {
            //regular character
            gX_I32 codepoint = gX_TextWriter_DecodeUTF8(line, len, &ci);
            gX_I1 isSpace = (codepoint == ' ' || codepoint == '\t');

            if (isSpace && gi > wordStart) {
                //space ends the current word (space belongs to preceding word)
                out->glyphs[gi].codepoint = codepoint;
                gi++;
                gX_TextWriter_CloseWord(out, wordStart, gi, &state);
                wordStart = gi;
            }
            else {
                out->glyphs[gi].codepoint = codepoint;
                gi++;
            }
        }
    }

    //close final word
    if (gi > wordStart) {
        gX_TextWriter_CloseWord(out, wordStart, gi, &state);
    }

    out->glyphsAmount = gi;
    if (final) *final = state;
    return 0;
}

//opencode: parse a line into the writer and refresh its choice list (used by
//   the loaders and by every line change so <option> entries stay in sync).
static gX_I0 gX_TextWriter_ParseLineInto(gX_RendererTextWriter *writer, const gX_I8 *line) {
    gX_TextWriter_ParseLine(line, &writer->lineBuffer.processed,
                            &writer->parseState, &writer->parseState);
    gX_TextWriter_ScanChoices(writer, line);
}

//opencode: load a plain text file into the line buffer
gX_I32 gX_TextWriter_LoadFileTXT(gX_RendererTextWriter *writer, const gX_I8 *path) {
    gX_TextWriter_LineBuffer *buf = &writer->lineBuffer;
    FILE *fh = fopen(path, "rb");
    if (!fh) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: failed to open %s", path);
        return 1;
    }

    strncpy(buf->path, path, sizeof(buf->path) - 1);
    buf->linesAmount = 0;

    gX_I8 line[GX_TEXTWRITER_LINE_LENGTH];
    while (fgets(line, sizeof(line), fh) && buf->linesAmount < GX_TEXTWRITER_LINES_MAX) {
        gX_U32 l = (gX_U32)strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        memcpy(buf->lines[buf->linesAmount], line, l + 1);
        buf->linesAmount++;
    }
    fclose(fh);

    buf->currentLine = 0;
    buf->loaded = true;
    writer->textFileLoaded = true;
    writer->selectedChoice = 0;

    if (buf->linesAmount > 0) {
        gX_TextWriter_ParseLineInto(writer, buf->lines[0]);
        writer->revealCursor = 0;
    } else {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: empty file %s", path);
        writer->textFileFailed = true;
        buf->loaded = false;
        writer->textFileLoaded = false;
        return 1;
    }

    gX_TraceLog_RENDERER(gX_LOG_INFO,"TextWriter: loaded %i lines from %s",
                         buf->linesAmount, path);
    return 0;
}

//opencode: 0 when the line buffer saved, 1 otherwise — plain text, one line
//   per newline, the format gX_TextWriter_LoadFileTXT reads back. Shipped
//   text paths are refused under the harness guard (GX_NO_SETTINGS_SAVE), so
//   a gate-launched editor can never rewrite the fixtures it verifies.
gX_I32 gX_TextWriter_SaveFileTXT(gX_RendererTextWriter *writer, const gX_I8 *path) {
    gX_TextWriter_LineBuffer *buf = &writer->lineBuffer;
    if (!buf->loaded || buf->linesAmount == 0) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: nothing to save");
        return 1;
    }
    if (getenv("GX_NO_SETTINGS_SAVE") && path
        && strstr((const char *)path, "resources/text/")) {
        gX_TraceLog_RENDERER(gX_LOG_ERROR,
            "TextWriter: refusing to overwrite a shipped text file under the harness guard: %s",
            path);
        return 1;
    }

    FILE *fh = fopen(path, "wb");
    if (!fh) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: failed to open %s for write", path);
        return 1;
    }
    for (gX_U32 i = 0; i < buf->linesAmount; i++) {
        fputs((const char *)buf->lines[i], fh);
        fputc('\n', fh);
    }
    fclose(fh);
    gX_TraceLog_RENDERER(gX_LOG_INFO,"TextWriter: saved %i lines to %s",
                         buf->linesAmount, path);
    return 0;
}

//opencode: load a YAML dialogue file into the line buffer
gX_I32 gX_TextWriter_LoadFileYAML(gX_RendererTextWriter *writer, const gX_I8 *path) {
#if defined(GX_USE_YAML)
    gX_TextWriter_LineBuffer *buf = &writer->lineBuffer;

    if (gX_YAML_LoadTree(path) || !yamldata.root) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: YAML load failed %s", path);
        return 1;
    }

    strncpy(buf->path, path, sizeof(buf->path) - 1);
    buf->linesAmount = 0;

    gX_YAML_KeyValue *linesNode = gX_YAML_FindKey(yamldata.root, "lines");
    if (linesNode && linesNode->type == 1) {
        for (gX_U32 i = 0; i < linesNode->childCount && buf->linesAmount < GX_TEXTWRITER_LINES_MAX; i++) {
            gX_YAML_KeyValue *child = linesNode->children[i];
            if (child->value[0]) {
                strncpy(buf->lines[buf->linesAmount], child->value, GX_TEXTWRITER_LINE_LENGTH - 1);
                buf->linesAmount++;
            }
        }
    }
    else {
        for (gX_U32 i = 0; i < yamldata.root->childCount && buf->linesAmount < GX_TEXTWRITER_LINES_MAX; i++) {
            gX_YAML_KeyValue *child = yamldata.root->children[i];
            if (child->type == 0 && child->value[0]) {
                strncpy(buf->lines[buf->linesAmount], child->value, GX_TEXTWRITER_LINE_LENGTH - 1);
                buf->linesAmount++;
            }
        }
    }

    gX_YAML_Deinit();

    if (buf->linesAmount == 0) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: no lines found in YAML");
        return 1;
    }

    buf->currentLine = 0;
    buf->loaded = true;
    writer->textFileLoaded = true;
    writer->selectedChoice = 0;

    gX_TextWriter_ParseLineInto(writer, buf->lines[0]);
    writer->revealCursor = 0;

    gX_TraceLog_RENDERER(gX_LOG_INFO,"TextWriter: loaded %i lines from YAML %s",
                         buf->linesAmount, path);
    return 0;
#else
    (void)writer;
    (void)path;
    gX_TraceLog_RENDERER(gX_LOG_WARNING,"TextWriter: YAML not available");
    return 1;
#endif
}

//opencode: load a file, auto-detect YAML by extension
gX_I32 gX_TextWriter_LoadFile(gX_RendererTextWriter *writer, const gX_I8 *path) {
    if (writer->textFileLoaded) gX_TextWriter_UnloadFile(writer);
    writer->textFileFailed = false;

    gX_I32 result;
    gX_U32 plen = (gX_U32)strlen(path);
    if (plen >= 5 && (!strcmp(path + plen - 5, ".yaml") || !strcmp(path + plen - 4, ".yml"))) {
        result = gX_TextWriter_LoadFileYAML(writer, path);
    }
    else {
        result = gX_TextWriter_LoadFileTXT(writer, path);
    }

    if (result) {
        writer->textFileFailed = true;
    }
    return result;
}

gX_I0 gX_TextWriter_UnloadFile(gX_RendererTextWriter *writer) {
    writer->lineBuffer.loaded = false;
    writer->textFileLoaded = false;
    writer->lineBuffer.linesAmount = 0;
    writer->lineBuffer.currentLine = 0;
    writer->lineBuffer.processed.glyphsAmount = 0;
    writer->lineBuffer.processed.wordsAmount = 0;
    writer->revealCursor = 0;
}

gX_I0 gX_TextWriter_NextLine(gX_RendererTextWriter *writer) {
    gX_TextWriter_LineBuffer *buf = &writer->lineBuffer;
    if (!buf->loaded) return;
    //opencode: check for jump target set by <jump line='N' /> tag
    if (writer->jumpTarget >= 0) {
        gX_U32 target = (gX_U32)writer->jumpTarget;
        writer->jumpTarget = -1;
        if (target < buf->linesAmount) {
            buf->currentLine = target;
            gX_TextWriter_ParseLineInto(writer, buf->lines[target]);
            writer->revealCursor = 0;
            return;
        }
    }
    if (buf->currentLine + 1 >= buf->linesAmount) {
        buf->currentLine = 0;
    }
    else {
        buf->currentLine++;
    }
    gX_TextWriter_ParseLineInto(writer, buf->lines[buf->currentLine]);
    writer->revealCursor = 0;
}

gX_I0 gX_TextWriter_JumpLine(gX_RendererTextWriter *writer, gX_U32 line) {
    gX_TextWriter_LineBuffer *buf = &writer->lineBuffer;
    if (!buf->loaded) return;
    if (line >= buf->linesAmount) line = buf->linesAmount - 1;
    buf->currentLine = line;
    gX_TextWriter_ParseLineInto(writer, buf->lines[line]);
    writer->revealCursor = 0;
}

//opencode: choice API
gX_I1 gX_TextWriter_HasChoices(gX_RendererTextWriter *writer) {
    return writer->hasChoices;
}

gX_U32 gX_TextWriter_GetChoiceCount(gX_RendererTextWriter *writer) {
    return writer->choiceCount;
}

const gX_I8 *gX_TextWriter_GetChoiceText(gX_RendererTextWriter *writer, gX_U32 index) {
    if (index >= writer->choiceCount) return NULL;
    return writer->choiceText[index];
}

gX_I32 gX_TextWriter_SelectChoice(gX_RendererTextWriter *writer, gX_U32 index) {
    if (index >= writer->choiceCount) return 1;
    gX_I32 line = writer->choiceLines[index];
    writer->hasChoices = false;
    writer->choiceCount = 0;
    if (line >= 0) {
        gX_TextWriter_JumpLine(writer, (gX_U32)line);
    }
    return 0;
}

//opencode: build a combined codepoint array from data strings for CJK font loading.
//   Concatenates ASCII + kana + kanji + common punctuation into a single array.
//   Caller must RL_FREE the result. Returns NULL on failure.
static gX_I32 *gX_TextWriter_BuildCJKCodepoints(gX_I32 *outCount) {
    //opencode: collect all character source strings
    const gX_I8 *sources[] = {
        gX_Data_String_uppercase,
        gX_Data_String_uppercase_nordic,
        gX_Data_String_lowercase,
        gX_Data_String_lowercase_nordic,
        gX_Data_String_numbers,
        gX_Data_String_hiragana,
        gX_Data_String_katakana,
        gX_Data_String_kanji,
        ".,!?;:()[]{}<>\"'@#$%^&*+-=~`|/\\ _"
    };
    gX_U32 numSources = sizeof(sources) / sizeof(sources[0]);

    //opencode: first pass — count total codepoints
    gX_I32 total = 0;
    for (gX_U32 s = 0; s < numSources; s++) {
        const gX_I8 *str = sources[s];
        if (!str) continue;
        gX_U32 ci = 0;
        gX_U32 len = (gX_U32)strlen(str);
        while (ci < len) {
            gX_I32 cp = gX_TextWriter_DecodeUTF8(str, len, &ci);
            if (cp > 0) total++;
        }
    }

    //opencode: allocate and fill
    gX_I32 *codepoints = (gX_I32 *)malloc((size_t)total * sizeof(gX_I32));
    if (!codepoints) { *outCount = 0; return NULL; }
    gX_I32 idx = 0;
    for (gX_U32 s = 0; s < numSources; s++) {
        const gX_I8 *str = sources[s];
        if (!str) continue;
        gX_U32 ci = 0;
        gX_U32 len = (gX_U32)strlen(str);
        while (ci < len && idx < total) {
            gX_I32 cp = gX_TextWriter_DecodeUTF8(str, len, &ci);
            if (cp > 0) codepoints[idx++] = cp;
        }
    }
    *outCount = idx;
    return codepoints;
}

//opencode: font cache — find or load a font by path, with CJK glyph support
static gX_Font gX_TextWriter_GetFont(gX_RendererTextWriter *writer, const gX_I8 *path) {
    if (!path || !path[0]) {
        return writer->hasCustomFont ? writer->customFont : gX_Font_GetDefault();
    }
    //opencode: search cache
    for (gX_U32 i = 0; i < writer->fontCacheCount; i++) {
        if (!strcmp(writer->fontCache[i].path, path)) {
            return writer->fontCache[i].font;
        }
    }
    //opencode: load new font with full CJK codepoint set
    if (writer->fontCacheCount < GX_TEXTWRITER_FONT_CACHE) {
        gX_TextWriter_FontCacheEntry *e = &writer->fontCache[writer->fontCacheCount++];
        strncpy(e->path, path, sizeof(e->path) - 1);
        gX_I32 cpCount = 0;
        gX_I32 *codepoints = gX_TextWriter_BuildCJKCodepoints(&cpCount);
        if (codepoints && cpCount > 0) {
            e->font = gX_Font_LoadEx(path, GX_TEXTWRITER_FONT_CJK_SIZE, codepoints, cpCount);
            free(codepoints);
        } else {
            e->font = gX_Font_Load(path);
        }
        e->loaded = gX_Font_IsReady(e->font);
        if (e->loaded) return e->font;
    }
    return writer->hasCustomFont ? writer->customFont : gX_Font_GetDefault();
}

//ai: one-shot test hook state; reset on renderer load so a suite process gets
//    the requested fixture line exactly once, not every frame.
static gX_I1 s_textWriterHookLineApplied = 0;

gX_I0 gX_RendererTextWriter_Load(gX_RendererTextWriter *writer) {
    gX_TRACELOG_PROLOGUE("RendererTextWriter_Load","RENDERER_TEXTWRITER: Loading textwriter")
    s_textWriterHookLineApplied = 0;

    strcpy(writer->name,"Renderer_TextWriter");
    strcpy(writer->description,"writes text with inline XML-like scripts");

    writer->parseState.color    = (gX_Color){255,255,255,255};
    writer->parseState.offset   = (gX_F32v2){0,0};
    writer->parseState.size     = GX_TEXTWRITER_DEFAULT_SIZE;
    writer->parseState.spacing  = 0.0f;
    writer->parseState.fontPath[0] = '\0';
    writer->parseState.matrix   = gX_TextWriter_Transform_Identity();
    writer->parseState.rubyText[0] = '\0';
    writer->parseState.underline = false;
    writer->parseState.effects  = 0;
    writer->parseState.gradientEnd = (gX_Color){255,255,255,255};
    writer->parseState.waveAmp  = GX_TEXTWRITER_DEFAULT_AMP;
    writer->parseState.waveFreq = GX_TEXTWRITER_DEFAULT_FREQ;
    writer->parseState.jumpTarget = -1;
    writer->stateStackDepth = 0;

    writer->customFont = (gX_Font){0};
    writer->hasCustomFont = false;
    writer->fontLoadFailed = false;

    writer->revealCursor = 0;
    writer->revealSpeed = 0.3f;

    writer->loaded = true;
    writer->textFileLoaded = false;
    writer->textFileFailed = false;

    writer->fontCacheCount = 0;
    writer->hasChoices = false;
    writer->choiceCount = 0;
    writer->jumpTarget = -1;
    writer->effectTime = 0.0;

    gX_TRACELOG_EPILOGUE("RENDERER_TEXTWRITER: Loading done")
}

gX_I0 gX_RendererTextWriter_Unload(gX_RendererTextWriter *writer) {
    gX_TRACELOG_PROLOGUE("RendererTextWriter_Unload","RENDERER_TEXTWRITER: Unloading textwriter")

    if (writer->textFileLoaded) gX_TextWriter_UnloadFile(writer);
    writer->loaded = false;

    gX_TRACELOG_EPILOGUE("RENDERER_TEXTWRITER: Unloading done")
}

gX_I0 gX_RendererTextWriter_Update(gX_RendererTextWriter *writer) {
    if (!writer->loaded) return;

    //ai: test-only fixture path/line overrides; unset behavior is the shipped
    //    auto-load path and the file's first line exactly as before.
    const gX_I8 *fileHook = getenv("GX_TEXTWRITER_FILE");
    if (maindata.backendLoaded && !writer->textFileLoaded && !writer->textFileFailed) {
        const gX_I8 *path = (fileHook && fileHook[0])
            ? fileHook : GX_PATH("text", "textwriter.txt");
        gX_TextWriter_LoadFile(writer, path);
    }
    const gX_I8 *lineHook = getenv("GX_TEXTWRITER_LINE");
    if (!s_textWriterHookLineApplied && lineHook && lineHook[0]
        && writer->textFileLoaded) {
        gX_I32 line = atoi((const char *)lineHook);
        if (line < 0) line = 0;
        gX_TextWriter_JumpLine(writer, (gX_U32)line);
        s_textWriterHookLineApplied = 1;
    }

    //opencode: lazy-load custom font from parse state font path.
    // N64 doesn't use the backendLoaded guard (maindata is a stub there).
    gX_TextWriter_ParseState *ps = &writer->parseState;
    if (ps->fontPath[0] && !writer->hasCustomFont && !writer->fontLoadFailed) {
        if (maindata.backendLoaded) {
            writer->customFont = gX_Font_Load(ps->fontPath);
            writer->hasCustomFont = gX_Font_IsReady(writer->customFont);
            if (!writer->hasCustomFont) writer->fontLoadFailed = true;
        }
    }

    //opencode: advance effect animation timer
    writer->effectTime += gX_FrameTime_Get();

    //opencode: check for jump target from parse state
    if (writer->parseState.jumpTarget >= 0) {
        writer->jumpTarget = writer->parseState.jumpTarget;
        writer->parseState.jumpTarget = -1;
    }

    //advance typewriter reveal (per-glyph)
    if (writer->revealCursor < (gX_F32)writer->lineBuffer.processed.glyphsAmount) {
        writer->revealCursor += writer->revealSpeed;
    }
}

gX_I0 gX_RendererTextWriter_DrawTo(gX_RendererTextWriter *writer, gX_RenderTexture *outputRT) {
    if (!writer->loaded || !writer->lineBuffer.loaded) return;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
    gX_Drawing_ClearBG(gX_BLACK);

    gX_TextWriter_ProcessedLine *pl = &writer->lineBuffer.processed;
    gX_F32 reveal = writer->revealCursor;
    if (reveal > (gX_F32)pl->glyphsAmount) reveal = (gX_F32)pl->glyphsAmount;

    gX_F32v2 pos = (gX_F32v2){GX_TEXTWRITER_MARGIN_X, GX_TEXTWRITER_MARGIN_Y};
    gX_F32 startX = pos.x;
    gX_F32 wrapX = (gX_F32)maindata.renderWidth * GX_TEXTWRITER_WRAP_RATIO;

    //opencode: pick default font
    gX_Font defaultFont = writer->hasCustomFont ? writer->customFont : gX_Font_GetDefault();
    gX_Font font = defaultFont;
    gX_F32 baseSize = (gX_F32)font.baseSize;
    if (baseSize < 1.0f) baseSize = GX_TEXTWRITER_DEFAULT_SIZE;

    //opencode: animation time for effects
    gX_F64 effectTime = writer->effectTime;

    //opencode: iterate over words, drawing glyphs with per-word formatting
    for (gX_U32 wi = 0; wi < pl->wordsAmount; wi++) {
        gX_TextWriter_Word *word = &pl->words[wi];
        gX_U32 wEnd = word->glyphStart + word->glyphCount;

        //skip words entirely past the reveal cursor
        if (word->glyphStart >= (gX_U32)reveal) continue;

        //determine how many glyphs of this word to draw
        gX_U32 drawEnd = wEnd;
        if ((gX_F32)wEnd > reveal) {
            drawEnd = (gX_U32)reveal;
        }

        //opencode: font size for this word
        gX_F32 fs = word->size > 1.0f ? word->size : GX_TEXTWRITER_DEFAULT_SIZE;

        //opencode: per-word font switching
        gX_Font wordFont = defaultFont;
        gX_F32 wordBaseSize = baseSize;
        if (word->fontPath[0]) {
            wordFont = gX_TextWriter_GetFont(writer, word->fontPath);
            wordBaseSize = (gX_F32)wordFont.baseSize;
            if (wordBaseSize < 1.0f) wordBaseSize = GX_TEXTWRITER_DEFAULT_SIZE;
        }
        font = wordFont;
        baseSize = wordBaseSize;

        //opencode: track word horizontal extent for ruby/underline
        gX_F32 wordStartX = pos.x;

        //opencode: iterate glyphs within the visible portion of this word
        for (gX_U32 gi = word->glyphStart; gi < drawEnd; gi++) {
            gX_TextWriter_Glyph *glyph = &pl->glyphs[gi];

            if (glyph->codepoint == '\n') {
                pos.x = startX;
                pos.y += fs * GX_TEXTWRITER_LINE_HEIGHT;
                continue;
            }

            //opencode: base position = line cursor + word offset
            gX_F32v2 coord = (gX_F32v2){pos.x + word->offset.x,
                                         pos.y + word->offset.y};

            //opencode: local draw color (may be modified by effects)
            gX_Color drawColor = word->color;

            //opencode: apply per-glyph effects
            gX_U32 giRel = gi - word->glyphStart;
            gX_U32 gc = word->glyphCount > 0 ? word->glyphCount : 1;

            //opencode: rainbow — cycling hue per glyph
            if (word->effects & GX_TEXTWRITER_EFFECT_RAINBOW) {
                gX_F32 hue = fmodf((gX_F32)giRel * GX_TEXTWRITER_RAINBOW_HUE_SPEED + (gX_F32)effectTime * GX_TEXTWRITER_RAINBOW_TIME_SPEED, 1.0f);
                //opencode: HSV-to-RGB (hue in 0..1)
                gX_F32 h6 = hue * 6.0f;
                gX_I32 hi = (gX_I32)h6;
                gX_F32 f = h6 - (gX_F32)hi;
                switch (hi % 6) {
                    case 0: drawColor.r = 255; drawColor.g = (gX_U8)(f * 255.0f); drawColor.b = 0; break;
                    case 1: drawColor.r = (gX_U8)((1.0f - f) * 255.0f); drawColor.g = 255; drawColor.b = 0; break;
                    case 2: drawColor.r = 0; drawColor.g = 255; drawColor.b = (gX_U8)(f * 255.0f); break;
                    case 3: drawColor.r = 0; drawColor.g = (gX_U8)((1.0f - f) * 255.0f); drawColor.b = 255; break;
                    case 4: drawColor.r = (gX_U8)(f * 255.0f); drawColor.g = 0; drawColor.b = 255; break;
                    case 5: drawColor.r = 255; drawColor.g = 0; drawColor.b = (gX_U8)((1.0f - f) * 255.0f); break;
                }
                drawColor.a = word->color.a;
            }

            //opencode: shake — time-based oscillation per glyph (sin/cos with different phases)
            if (word->effects & GX_TEXTWRITER_EFFECT_SHAKE) {
                gX_F32 t = (gX_F32)effectTime * GX_TEXTWRITER_SHAKE_TIME;
                gX_F32 sx = sinf(t + (gX_F32)(giRel * 7)) * GX_TEXTWRITER_SHAKE_AMP;
                gX_F32 sy = cosf(t + (gX_F32)(giRel * 11)) * GX_TEXTWRITER_SHAKE_AMP;
                coord.x += sx;
                coord.y += sy;
            }

            //opencode: disintegrate — glyphs drift apart as the word is revealed.
            //   Uses reveal cursor as time base so displacement starts at 0 when
            //   the word first appears and ramps up as more glyphs are revealed.
            if (word->effects & GX_TEXTWRITER_EFFECT_DISINTEGRATE) {
                gX_F32 t = (reveal - (gX_F32)word->glyphStart) / (gX_F32)(gc > 0 ? gc : 1);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                gX_F32 dx = (gX_F32)((giRel * 13 + 7) % 21 - 10) * t * GX_TEXTWRITER_DISINTEGRATE_SCALE;
                gX_F32 dy = (gX_F32)((giRel * 17 + 3) % 21 - 10) * t * GX_TEXTWRITER_DISINTEGRATE_SCALE;
                coord.x += dx;
                coord.y += dy;
            }

            //opencode: wave — sinusoidal Y offset
            if (word->effects & GX_TEXTWRITER_EFFECT_WAVE) {
                gX_F32 wave = sinf(coord.x * word->waveFreq + (gX_F32)effectTime * GX_TEXTWRITER_WAVE_TIME_SPEED) * word->waveAmp;
                coord.y += wave;
            }

            //opencode: gradient — interpolated color across word
            if (word->effects & GX_TEXTWRITER_EFFECT_GRADIENT) {
                gX_F32 t = gc > 1 ? (gX_F32)giRel / (gX_F32)(gc - 1) : 0.0f;
                drawColor.r = (gX_U8)((gX_F32)word->color.r * (1.0f - t) + (gX_F32)word->gradientEnd.r * t);
                drawColor.g = (gX_U8)((gX_F32)word->color.g * (1.0f - t) + (gX_F32)word->gradientEnd.g * t);
                drawColor.b = (gX_U8)((gX_F32)word->color.b * (1.0f - t) + (gX_F32)word->gradientEnd.b * t);
                drawColor.a = word->color.a;
            }

            //opencode: get glyph index and advance for cursor tracking
            gX_I32 glyphIdx = gX_Font_GetGlyphIndex(font, (gX_I32)glyph->codepoint);
            gX_F32 scaleFactor = fs / baseSize;
            gX_F32 advance = fs * GX_TEXTWRITER_ADVANCE_RATIO;
            if (glyphIdx >= 0 && glyphIdx < font.glyphCount) {
                if (font.glyphs[glyphIdx].advanceX == 0) {
                    advance = (gX_F32)font.recs[glyphIdx].width * scaleFactor;
                } else {
                    advance = (gX_F32)font.glyphs[glyphIdx].advanceX * scaleFactor;
                }
                if (advance < 1.0f) advance = fs * GX_TEXTWRITER_ADVANCE_RATIO;
            }

            //opencode: fast path — identity matrix, draw with wrapper call
            if (gX_TextWriter_Transform_IsIdentity(&word->matrix)) {
                gX_I8 ch[8];
                gX_TextWriter_EncodeUTF8(glyph->codepoint, ch);
                gX_RawString_Draw(ch, font, coord, (gX_F32v2){0,0},
                                  0.0f, fs, 1.0f, drawColor);
            }
            //opencode: transformed path — deform glyph quad with matrix
            else {
                gX_F32 ox = 0.0f, oy = 0.0f;
                gX_F32 gw = fs * GX_TEXTWRITER_ADVANCE_RATIO, gh = fs;
                if (glyphIdx >= 0 && glyphIdx < font.glyphCount) {
                    gX_Rectangle rec = font.recs[glyphIdx];
                    ox = (gX_F32)font.glyphs[glyphIdx].offsetX * scaleFactor;
                    oy = (gX_F32)font.glyphs[glyphIdx].offsetY * scaleFactor;
                    gw = (gX_F32)rec.width * scaleFactor;
                    gh = (gX_F32)rec.height * scaleFactor;
                }

                //opencode: compute UV corners from font atlas
                gX_F32 tx = (gX_F32)font.texture.width;
                gX_F32 ty = (gX_F32)font.texture.height;
                gX_F32 uvL=0, uvR=1, uvT=0, uvB=1;
                if (glyphIdx >= 0 && glyphIdx < font.glyphCount) {
                    gX_Rectangle rec = font.recs[glyphIdx];
                    uvL = rec.x / tx;
                    uvR = (rec.x + rec.width) / tx;
                    uvT = rec.y / ty;
                    uvB = (rec.y + rec.height) / ty;
                }

                //opencode: quad corners in LOCAL space (relative to coord),
                //   so shear/scale only affects glyph shape, not world position.
                gX_F32v2 local[4];
                local[0].x = ox;       local[0].y = oy;         // top-left
                local[1].x = ox + gw;  local[1].y = oy;         // top-right
                local[2].x = ox + gw;  local[2].y = oy + gh;    // bottom-right
                local[3].x = ox;       local[3].y = oy + gh;    // bottom-left

                //opencode: apply matrix to local corners
                for (gX_I32 ci = 0; ci < 4; ci++) {
                    gX_TextWriter_Transform_Apply(&word->matrix, &local[ci]);
                }

                //opencode: world position = coord + local
                gX_F32v2 qc[4];
                for (gX_I32 ci = 0; ci < 4; ci++) {
                    qc[ci].x = coord.x + local[ci].x;
                    qc[ci].y = coord.y + local[ci].y;
                }

                //opencode: draw transformed quad via gX_Batch
                gX_Batch_SetTexture(font.texture.id);
                gX_Batch_Begin(GX_BATCH_QUADS);
                gX_Batch_Color4ub(drawColor.r, drawColor.g, drawColor.b, drawColor.a);
                gX_Batch_TexCoord2f(uvL, uvB); gX_Batch_Vertex2f(qc[3].x, qc[3].y);
                gX_Batch_TexCoord2f(uvR, uvB); gX_Batch_Vertex2f(qc[2].x, qc[2].y);
                gX_Batch_TexCoord2f(uvR, uvT); gX_Batch_Vertex2f(qc[1].x, qc[1].y);
                gX_Batch_TexCoord2f(uvL, uvT); gX_Batch_Vertex2f(qc[0].x, qc[0].y);
                gX_Batch_End();
            }

            //opencode: cursor advance includes word-level spacing
            pos.x += advance + GX_TEXTWRITER_WORD_SPACING + word->spacing;

            //simple word wrap
            if (pos.x >= wrapX) {
                pos.x = startX;
                pos.y += fs * GX_TEXTWRITER_LINE_HEIGHT;
            }
        }

        //opencode: post-word effects — ruby annotation above, underline below
        if (drawEnd > word->glyphStart) {
            gX_F32 wordEndX = pos.x;

            //opencode: ruby — draw annotation text centered above the word
            if (word->rubyText[0]) {
                gX_F32 rubySize = fs * GX_TEXTWRITER_RUBY_SIZE_RATIO;
                gX_F32 rubyY = pos.y + word->offset.y - fs * GX_TEXTWRITER_RUBY_OFFSET_RATIO;
                gX_F32 rubyX = (wordStartX + wordEndX) * 0.5f;
                gX_RawString_Draw(word->rubyText, font,
                    (gX_F32v2){rubyX, rubyY}, (gX_F32v2){0,0},
                    0.0f, rubySize, 1.0f, word->color);
            }

            //opencode: underline — draw a thin bar below the word
            if (word->underline) {
                gX_F32 ulY = pos.y + word->offset.y + fs * GX_TEXTWRITER_UL_OFFSET_RATIO;
                gX_Batch_Begin(GX_BATCH_QUADS);
                gX_Batch_Color4ub(word->color.r, word->color.g, word->color.b, word->color.a);
                gX_Batch_Vertex2f(wordStartX, ulY);
                gX_Batch_Vertex2f(wordEndX, ulY);
                gX_Batch_Vertex2f(wordEndX, ulY + GX_TEXTWRITER_UL_THICKNESS);
                gX_Batch_Vertex2f(wordStartX, ulY + GX_TEXTWRITER_UL_THICKNESS);
                gX_Batch_End();
            }
        }
    }

    gX_Drawing_EndTexture();
}
