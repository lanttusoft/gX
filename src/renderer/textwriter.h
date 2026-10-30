/**********************************************************************************************
*
*   [gX: src/renderer/textwriter.h]
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


#ifndef RENDERER_TEXTWRITER_H
#define RENDERER_TEXTWRITER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"
#include "../common/heap.h"
#include "../common/file.h"

//opencode: tiered capacity — T3 (N64/PS1) shrinks the text buffers;
//   desktop (GX_TIER undefined) keeps the full sizes. Must stay consistent
//   with the tier tables in docs/tier-system.md + docs/n64_port_plan.md.
#if defined(GX_TIER) && GX_TIER == 3
  #define GX_TEXTWRITER_GLYPHS_MAX  128
  #define GX_TEXTWRITER_WORDS_MAX    32
  #define GX_TEXTWRITER_LINES_MAX    32
  #define GX_TEXTWRITER_LINE_LENGTH 256
#else
  #define GX_TEXTWRITER_GLYPHS_MAX  512
  #define GX_TEXTWRITER_WORDS_MAX   128
  #define GX_TEXTWRITER_LINES_MAX   256
  #define GX_TEXTWRITER_LINE_LENGTH 512
#endif
#define GX_TEXTWRITER_STATE_STACK 16
#define GX_TEXTWRITER_MAX_CHOICES 8
#define GX_TEXTWRITER_FONT_CACHE  4

//opencode: tag shorthand defaults
#define GX_TEXTWRITER_ITALIC_SHEAR  -0.3f
#define GX_TEXTWRITER_BOLD_SCALE     1.2f

//opencode: default render state
#define GX_TEXTWRITER_DEFAULT_SIZE   24.0f
#define GX_TEXTWRITER_DEFAULT_AMP     5.0f
#define GX_TEXTWRITER_DEFAULT_FREQ    0.1f
#define GX_TEXTWRITER_FONT_CJK_SIZE  48

//opencode: layout constants
#define GX_TEXTWRITER_MARGIN_X       64.0f
#define GX_TEXTWRITER_MARGIN_Y       64.0f
#define GX_TEXTWRITER_WRAP_RATIO      0.85f
#define GX_TEXTWRITER_LINE_HEIGHT     1.3f
#define GX_TEXTWRITER_ADVANCE_RATIO   0.6f
#define GX_TEXTWRITER_WORD_SPACING    3.0f

//opencode: effect tuning
#define GX_TEXTWRITER_RAINBOW_HUE_SPEED   0.15f
#define GX_TEXTWRITER_RAINBOW_TIME_SPEED   0.5f
#define GX_TEXTWRITER_SHAKE_TIME          10.0f
#define GX_TEXTWRITER_SHAKE_AMP            2.5f
#define GX_TEXTWRITER_DISINTEGRATE_SCALE   4.0f
#define GX_TEXTWRITER_WAVE_TIME_SPEED      3.0f

//opencode: ruby + underline layout
#define GX_TEXTWRITER_RUBY_SIZE_RATIO      0.5f
#define GX_TEXTWRITER_RUBY_OFFSET_RATIO    0.7f
#define GX_TEXTWRITER_UL_OFFSET_RATIO      0.15f
#define GX_TEXTWRITER_UL_THICKNESS         1.5f

//opencode: glyph — just a codepoint. Formatting lives on the parent Word.
typedef struct gX_TextWriter_Glyph {
    gX_I32      codepoint;
} gX_TextWriter_Glyph;

//opencode: 2D affine transform for word-level matrix effects.
// Applied as: x' = a*x + c*y + tx , y' = b*x + d*y + ty
// Default (identity): a=1, b=0, c=0, d=1, tx=0, ty=0
// Italic shear (top leans right): a=1, c=-0.3  (negative c = rightward lean)
// Rotate/scale: standard 2x2 matrix in a,b,c,d.
typedef struct gX_TextWriter_Transform {
    gX_F32 a, b, c, d;
    gX_F32 tx, ty;
} gX_TextWriter_Transform;

//opencode: effect flags — bitfield for per-word special effects
typedef enum gX_TextWriter_Effect {
    GX_TEXTWRITER_EFFECT_NONE         = 0,
    GX_TEXTWRITER_EFFECT_RAINBOW      = 1 << 0,
    GX_TEXTWRITER_EFFECT_SHAKE        = 1 << 1,
    GX_TEXTWRITER_EFFECT_DISINTEGRATE = 1 << 2,
    GX_TEXTWRITER_EFFECT_WAVE         = 1 << 3,
    GX_TEXTWRITER_EFFECT_GRADIENT     = 1 << 4,
} gX_TextWriter_Effect;

//opencode: word — range of glyphs sharing formatting + optional transform.
//   The reveal cursor tracks glyph index; a word is fully revealed when its
//   last glyph is past the cursor. Effects (color, size, transform) apply
//   uniformly across the whole word.
typedef struct gX_TextWriter_Word {
    gX_U32      glyphStart;
    gX_U32      glyphCount;
    gX_Color    color;
    gX_F32v2    offset;
    gX_F32      size;
    gX_F32      spacing;            //opencode: extra letter-spacing (0 = default)
    gX_TextWriter_Transform matrix; //opencode: identity = no transform
    gX_I8       rubyText[64];       //opencode: ruby annotation (empty = none)
    gX_I1       underline;          //opencode: draw underline
    gX_U32      effects;            //opencode: bitfield of gX_TextWriter_Effect
    gX_Color    gradientEnd;        //opencode: end color for gradient effect
    gX_F32      waveAmp;            //opencode: wave amplitude (pixels)
    gX_F32      waveFreq;           //opencode: wave frequency
    gX_I8       fontPath[256];      //opencode: per-word custom font path (empty = default)
} gX_TextWriter_Word;

//opencode: rendering state (pushed/popped by XML-like tags)
typedef struct gX_TextWriter_ParseState {
    gX_Color    color;
    gX_F32v2    offset;
    gX_F32      size;
    gX_F32      spacing;            //opencode: extra letter-spacing (0 = default)
    gX_I8       fontPath[256];
    gX_TextWriter_Transform matrix;
    gX_I8       rubyText[64];       //opencode: ruby annotation (empty = none)
    gX_I1       underline;          //opencode: draw underline under word
    gX_U32      effects;            //opencode: bitfield of gX_TextWriter_Effect
    gX_Color    gradientEnd;        //opencode: end color for gradient
    gX_F32      waveAmp;            //opencode: wave amplitude
    gX_F32      waveFreq;           //opencode: wave frequency
    gX_I32      jumpTarget;         //opencode: set by <jump line='N' />, -1 = none
} gX_TextWriter_ParseState;

//opencode: one line processed into glyphs + word segments
typedef struct gX_TextWriter_ProcessedLine {
    gX_U32                  glyphsAmount;
    gX_TextWriter_Glyph     glyphs[GX_TEXTWRITER_GLYPHS_MAX];

    gX_U32                  wordsAmount;
    gX_TextWriter_Word      words[GX_TEXTWRITER_WORDS_MAX];
} gX_TextWriter_ProcessedLine;

//opencode: line buffer from a loaded file
typedef struct gX_TextWriter_LineBuffer {
    gX_I8   path[256];
    gX_I1   loaded;

    gX_U32  linesAmount;
    gX_I8   lines[GX_TEXTWRITER_LINES_MAX][GX_TEXTWRITER_LINE_LENGTH];

    gX_U32  currentLine;

    gX_TextWriter_ProcessedLine processed;
} gX_TextWriter_LineBuffer;

//opencode: cached font entry for per-word font switching
typedef struct gX_TextWriter_FontCacheEntry {
    gX_I8   path[256];
    gX_Font font;
    gX_I1   loaded;
} gX_TextWriter_FontCacheEntry;

typedef struct gX_RendererTextWriter {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_U32 state;

    gX_TextWriter_ParseState    parseState;
    gX_TextWriter_ParseState    stateStack[GX_TEXTWRITER_STATE_STACK];
    gX_U32                      stateStackDepth;

    gX_TextWriter_LineBuffer    lineBuffer;

    gX_F32  revealCursor;
    gX_I32  selectedChoice;   //opencode: cursor index in choice list, -1 = none
    gX_F32  revealSpeed;

    gX_I1   loaded;
    gX_I1   textFileLoaded;
    gX_I1   textFileFailed;

    gX_Font     customFont;
    gX_I1       hasCustomFont;
    gX_I1       fontLoadFailed;

    //opencode: font cache for per-word font switching
    gX_TextWriter_FontCacheEntry fontCache[GX_TEXTWRITER_FONT_CACHE];
    gX_U32                       fontCacheCount;

    //opencode: choice system — options collected during parsing
    gX_I1   hasChoices;
    gX_U32  choiceCount;
    gX_I8   choiceText[GX_TEXTWRITER_MAX_CHOICES][64];
    gX_I32  choiceLines[GX_TEXTWRITER_MAX_CHOICES];  // target line, -1 = none

    //opencode: jump target set by <jump line='N' /> tag
    gX_I32  jumpTarget;  // -1 = no jump, >= 0 = line to jump to

    //opencode: animation timer for effects
    gX_F64  effectTime;

} gX_RendererTextWriter;

//opencode: file/line management
gX_I32 gX_TextWriter_LoadFile(       gX_RendererTextWriter *writer, const gX_I8 *path);
gX_I32 gX_TextWriter_LoadFileYAML(   gX_RendererTextWriter *writer, const gX_I8 *path);
gX_I32 gX_TextWriter_LoadFileTXT(    gX_RendererTextWriter *writer, const gX_I8 *path);
gX_I32 gX_TextWriter_SaveFileTXT(    gX_RendererTextWriter *writer, const gX_I8 *path); // 0 saved, 1 failed
gX_I0  gX_TextWriter_UnloadFile(     gX_RendererTextWriter *writer);
gX_I0  gX_TextWriter_NextLine(       gX_RendererTextWriter *writer);
gX_I0  gX_TextWriter_JumpLine(       gX_RendererTextWriter *writer, gX_U32 line);

//opencode: choice API
gX_I1   gX_TextWriter_HasChoices(    gX_RendererTextWriter *writer);
gX_U32  gX_TextWriter_GetChoiceCount(gX_RendererTextWriter *writer);
const gX_I8 *gX_TextWriter_GetChoiceText(gX_RendererTextWriter *writer, gX_U32 index);
gX_I32  gX_TextWriter_SelectChoice(  gX_RendererTextWriter *writer, gX_U32 index);

//opencode: inline parser — builds glyph + word arrays from tagged line
gX_I32 gX_TextWriter_ParseLine(const gX_I8 *line, gX_TextWriter_ProcessedLine *out,
                               gX_TextWriter_ParseState *initial, gX_TextWriter_ParseState *final);

gX_I0 gX_RendererTextWriter_Load(  gX_RendererTextWriter *writer);
gX_I0 gX_RendererTextWriter_Unload(gX_RendererTextWriter *writer);

gX_I0 gX_RendererTextWriter_Update(gX_RendererTextWriter *writer);
//opencode: DrawTo with explicit output RT (NULL = renderer.canvas). LIBDRAGON
//   backend ignores outputRT (draws to the attached rdpq surface directly).
gX_I0 gX_RendererTextWriter_DrawTo(gX_RendererTextWriter *writer, gX_RenderTexture *outputRT);
#define gX_RendererTextWriter_Draw(writer) gX_RendererTextWriter_DrawTo((writer), NULL)

#ifdef __cplusplus
}
#endif

#endif //RENDERER_TEXTWRITER_H
