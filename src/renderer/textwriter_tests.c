/**********************************************************************************************
*
*   [gX: src/renderer/textwriter_tests.c]
*   Textwriter parser tests — TXT loader, inline tags, segmentation, capacity
*
*   Copyright (c) 2026 lanttu, opencode
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


//opencode: headless pins for the textwriter parser (no GPU/window): word
//   segmentation, tag parsing (colors/attributes/matrix/effects/ruby/font),
//   the state stack reset, UTF-8 decoding, capacity guards, the TXT file
//   reader (line split/truncation/empty/missing) and the <jump> flow through
//   Update → NextLine.
#include "textwriter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/log.h"
#include "../common/path.h"

#define TW_TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { gX_TraceLog(gX_LOG_ERROR, "TW TEST FAIL: %s", msg); errors++; } \
} while (0)

static gX_I1 gX_Tests_TextWriter_Color(gX_Color c, gX_U8 r, gX_U8 g, gX_U8 b, gX_U8 a) {
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

gX_I32 gX_Tests_TextWriter(gX_I0);
//ai: the save-guard probe must not clear or truncate an inherited harness setting.
gX_I32 gX_Tests_TextWriter(gX_I0) {
    gX_I32 errors = 0;
    gX_TextWriter_ProcessedLine line;
    gX_TextWriter_ParseState final;

    //opencode: basic segmentation — two words, space belongs to the first.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("Hello world", &line, NULL, &final);
    TW_TEST_ASSERT(line.glyphsAmount == 11 && line.wordsAmount == 2,
        "basic glyph/word count");
    TW_TEST_ASSERT(line.words[0].glyphStart == 0 && line.words[0].glyphCount == 6
        && line.words[1].glyphStart == 6 && line.words[1].glyphCount == 5,
        "basic word ranges");
    TW_TEST_ASSERT(gX_Tests_TextWriter_Color(line.words[0].color, 255,255,255,255)
        && final.size == GX_TEXTWRITER_DEFAULT_SIZE && final.effects == 0
        && final.matrix.a == 1.0f && final.matrix.c == 0.0f,
        "default state");

    //opencode: shorthand color + </> reset (empty stack restores defaults).
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("a <red>b</> c", &line, NULL, &final);
    TW_TEST_ASSERT(line.wordsAmount == 3
        && line.words[0].glyphCount == 2
        && gX_Tests_TextWriter_Color(line.words[0].color, 255,255,255,255)
        && line.words[1].glyphCount == 1
        && gX_Tests_TextWriter_Color(line.words[1].color,
            255,0,0,255)
        && line.words[2].glyphCount == 2
        && gX_Tests_TextWriter_Color(line.words[2].color, 255,255,255,255),
        "color tag segmentation");
    TW_TEST_ASSERT(gX_Tests_TextWriter_Color(final.color, 255,255,255,255),
        "reset after empty-stack close");

    //opencode: nested tags — inner word keeps both, close restores outward.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("<red><b>x</></> y", &line, NULL, &final);
    TW_TEST_ASSERT(line.wordsAmount == 2 && line.words[0].glyphCount == 1
        && gX_Tests_TextWriter_Color(line.words[0].color, 255,0,0,255)
        && line.words[0].matrix.a == GX_TEXTWRITER_BOLD_SCALE,
        "nested tags on word");
    TW_TEST_ASSERT(gX_Tests_TextWriter_Color(final.color, 255,255,255,255)
        && final.matrix.a == 1.0f && final.matrix.c == 0.0f,
        "nested close restores defaults");

    //opencode: attribute tags fill the parse state (and the closed word).
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(
        "<color r='1' g='2' b='3' a='4' /><offset x='5' y='6' />"
        "<size v='33' /><spacing v='7' />x", &line, NULL, &final);
    TW_TEST_ASSERT(gX_Tests_TextWriter_Color(final.color, 1,2,3,4)
        && final.offset.x == 5.0f && final.offset.y == 6.0f
        && final.size == 33.0f && final.spacing == 7.0f,
        "attribute tag state");
    TW_TEST_ASSERT(line.wordsAmount == 1
        && gX_Tests_TextWriter_Color(line.words[0].color, 1,2,3,4)
        && line.words[0].size == 33.0f && line.words[0].spacing == 7.0f,
        "attribute tag word snapshot");

    //opencode: matrix tag + italic shorthand.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(
        "<matrix a='2' b='0' c='-0.3' d='1' tx='4' ty='5' />x", &line, NULL, &final);
    TW_TEST_ASSERT(final.matrix.a == 2.0f && final.matrix.b == 0.0f
        && final.matrix.c == -0.3f && final.matrix.d == 1.0f
        && final.matrix.tx == 4.0f && final.matrix.ty == 5.0f,
        "matrix tag state");
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("<i />x", &line, NULL, &final);
    TW_TEST_ASSERT(final.matrix.a == 1.0f
        && final.matrix.c == GX_TEXTWRITER_ITALIC_SHEAR,
        "italic shorthand");

    //opencode: effect tags — flags plus wave/gradient parameters.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(
        "<rainbow /><shake /><disintegrate /><wave amp='6' freq='0.15' />"
        "<gradient r='10' g='20' b='30' />x", &line, NULL, &final);
    TW_TEST_ASSERT(final.effects == (GX_TEXTWRITER_EFFECT_RAINBOW
            | GX_TEXTWRITER_EFFECT_SHAKE | GX_TEXTWRITER_EFFECT_DISINTEGRATE
            | GX_TEXTWRITER_EFFECT_WAVE | GX_TEXTWRITER_EFFECT_GRADIENT),
        "effect flags");
    TW_TEST_ASSERT(final.waveAmp == 6.0f && final.waveFreq == 0.15f
        && gX_Tests_TextWriter_Color(final.gradientEnd, 10,20,30,255),
        "effect parameters");
    TW_TEST_ASSERT(line.wordsAmount == 1
        && line.words[0].effects == final.effects
        && line.words[0].waveAmp == 6.0f,
        "effect word snapshot");

    //opencode: ruby/underline/font annotations travel into the word.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(
        "<ruby rt='furigana' /><u /><font path='dummy.ttf' />x", &line, NULL, &final);
    TW_TEST_ASSERT(final.underline == 1 && !strcmp(final.rubyText, "furigana")
        && !strcmp(final.fontPath, "dummy.ttf"),
        "annotation state");
    TW_TEST_ASSERT(line.wordsAmount == 1 && line.words[0].underline == 1
        && !strcmp(line.words[0].rubyText, "furigana")
        && !strcmp(line.words[0].fontPath, "dummy.ttf"),
        "annotation word snapshot");

    //opencode: jump tag target.
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("<jump line='7' />", &line, NULL, &final);
    TW_TEST_ASSERT(final.jumpTarget == 7, "jump target");

    //opencode: UTF-8 — 2-byte and 3-byte codepoints (escaped bytes).
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine("\xC3\xA4\xE2\x86\x92", &line, NULL, &final);
    TW_TEST_ASSERT(line.glyphsAmount == 2
        && line.glyphs[0].codepoint == 0xE4
        && line.glyphs[1].codepoint == 0x2192,
        "utf-8 decoding");

    //opencode: capacity guards — glyph and word arrays saturate, no overflow.
    static gX_I8 longLine[GX_TEXTWRITER_GLYPHS_MAX + 32];
    memset(longLine, 'a', sizeof(longLine) - 1);
    longLine[sizeof(longLine) - 1] = '\0';
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(longLine, &line, NULL, &final);
    TW_TEST_ASSERT(line.glyphsAmount == GX_TEXTWRITER_GLYPHS_MAX,
        "glyph capacity guard");

    static gX_I8 wordLine[(GX_TEXTWRITER_WORDS_MAX + 8) * 2];
    for (gX_U32 i = 0; i < sizeof(wordLine) - 1; i += 2) {
        wordLine[i] = 'a';
        wordLine[i + 1] = ' ';
    }
    wordLine[sizeof(wordLine) - 1] = '\0';
    memset(&line, 0, sizeof(line));
    gX_TextWriter_ParseLine(wordLine, &line, NULL, &final);
    TW_TEST_ASSERT(line.wordsAmount == GX_TEXTWRITER_WORDS_MAX,
        "word capacity guard");

    //opencode: shipped TXT file — line count, first-line parse, cursor moves.
    //   Load is field-init only (no GL objects), so the test mirrors the real
    //   init path including the -1 jump sentinels.
    gX_RendererTextWriter writer;
    memset(&writer, 0, sizeof(writer));
    gX_RendererTextWriter_Load(&writer);
    TW_TEST_ASSERT(writer.loaded && writer.jumpTarget == -1
        && writer.parseState.jumpTarget == -1,
        "writer load init");
    TW_TEST_ASSERT(gX_TextWriter_LoadFileTXT(&writer,
        GX_PATH("text", "textwriter.txt")) == 0
        && writer.lineBuffer.loaded && writer.textFileLoaded,
        "shipped txt load");
    TW_TEST_ASSERT(writer.lineBuffer.linesAmount == 25
        && writer.lineBuffer.processed.wordsAmount >= 2
        && writer.revealCursor == 0.0f,
        "shipped txt line parse");
    gX_TextWriter_NextLine(&writer);
    TW_TEST_ASSERT(writer.lineBuffer.currentLine == 1, "next line");
    gX_TextWriter_JumpLine(&writer, 9999);
    TW_TEST_ASSERT(writer.lineBuffer.currentLine == writer.lineBuffer.linesAmount - 1,
        "jump clamps to last line");
    gX_TextWriter_UnloadFile(&writer);
    TW_TEST_ASSERT(!writer.textFileLoaded && !writer.lineBuffer.loaded
        && writer.lineBuffer.linesAmount == 0, "txt unload clears state");

    //opencode: missing file fails loudly (LoadFile flags it).
    TW_TEST_ASSERT(gX_TextWriter_LoadFile(&writer,
        "/tmp/opencode/definitely_missing_textwriter.txt") == 1
        && writer.textFileFailed, "missing file rejected");

    //opencode: scratch files — empty input rejected, overlong lines split,
    //   and the <jump> flow honored (jump to line 0 from line 1; without the
    //   jump NextLine would have advanced to line 2).
    gX_I8 path[] = "/tmp/opencode/gx_tw_XXXXXX";
    FILE *scratch = NULL;
    gX_I32 fd = mkstemp(path);
    TW_TEST_ASSERT(fd >= 0, "scratch creation");
    if (fd >= 0) {
        close(fd);
        TW_TEST_ASSERT(gX_TextWriter_LoadFileTXT(&writer, path) == 1
            && !writer.textFileLoaded && !writer.lineBuffer.loaded,
            "empty file rejected");
        scratch = fopen(path, "w");
        if (scratch) {
            fputs("line zero\n<jump line='0' />\nlast\n", scratch);
            fclose(scratch);
        }
        TW_TEST_ASSERT(scratch != NULL
            && gX_TextWriter_LoadFileTXT(&writer, path) == 0
            && writer.lineBuffer.linesAmount == 3,
            "jump file load");
        gX_TextWriter_NextLine(&writer);
        TW_TEST_ASSERT(writer.lineBuffer.currentLine == 1, "jump line reached");
        gX_RendererTextWriter_Update(&writer);
        TW_TEST_ASSERT(writer.jumpTarget == 0 && writer.parseState.jumpTarget == -1,
            "update latches jump target");
        gX_TextWriter_NextLine(&writer);
        TW_TEST_ASSERT(writer.lineBuffer.currentLine == 0, "next line honors jump");
        scratch = fopen(path, "w");
        if (scratch) {
            static gX_I8 wide[GX_TEXTWRITER_LINE_LENGTH + 64];
            memset(wide, 'b', sizeof(wide) - 1);
            wide[sizeof(wide) - 1] = '\0';
            fputs(wide, scratch);
            fclose(scratch);
        }
        TW_TEST_ASSERT(scratch != NULL
            && gX_TextWriter_LoadFileTXT(&writer, path) == 0
            && writer.lineBuffer.linesAmount == 2,
            "overlong line splits into two");
        remove(path);
    }

    //opencode: choice collection — <option line='N'>text</option> entries
    //   fill the writer list on the current line; SelectChoice jumps to the
    //   option target and clears the list; lines without options stay empty.
    {
        static gX_I8 choicePath[] = "/tmp/opencode/gx_tw_choicesXXXXXX";
        gX_I32 cfd = mkstemp(choicePath);
        TW_TEST_ASSERT(cfd >= 0, "choice scratch creation");
        if (cfd >= 0) {
            close(cfd);
            FILE *cfile = fopen(choicePath, "w");
            if (cfile) {
                fputs("plain line\n"
                      "<choice><option line='3'>Go left</option>"
                      "<option line='2'>Go right</option></choice>\n"
                      "right target\n"
                      "left target\n"
                      "no options here\n"
                      "<option line='0'>12345678901234567890123456789012345678901234567890123456789012345678901234567890</option>\n",
                      cfile);
                fclose(cfile);
            }
            gX_RendererTextWriter_Load(&writer);
            TW_TEST_ASSERT(cfile != NULL
                && gX_TextWriter_LoadFileTXT(&writer, choicePath) == 0
                && writer.lineBuffer.linesAmount == 6,
                "choice file load");
            TW_TEST_ASSERT(!gX_TextWriter_HasChoices(&writer)
                && gX_TextWriter_GetChoiceCount(&writer) == 0,
                "line zero has no choices");
            gX_TextWriter_NextLine(&writer);
            TW_TEST_ASSERT(gX_TextWriter_HasChoices(&writer)
                && gX_TextWriter_GetChoiceCount(&writer) == 2,
                "choice count");
            TW_TEST_ASSERT(!strcmp((const char *)gX_TextWriter_GetChoiceText(&writer, 0), "Go left")
                && !strcmp((const char *)gX_TextWriter_GetChoiceText(&writer, 1), "Go right"),
                "choice texts");
            TW_TEST_ASSERT(gX_TextWriter_GetChoiceText(&writer, 2) == NULL
                && gX_TextWriter_SelectChoice(&writer, 2) == 1,
                "choice range guards");
            TW_TEST_ASSERT(gX_TextWriter_SelectChoice(&writer, 1) == 0
                && writer.lineBuffer.currentLine == 2
                && !gX_TextWriter_HasChoices(&writer),
                "select choice jumps and clears");
            gX_TextWriter_NextLine(&writer);
            TW_TEST_ASSERT(writer.lineBuffer.currentLine == 3
                && gX_TextWriter_GetChoiceCount(&writer) == 0,
                "next line clears choices");
            TW_TEST_ASSERT(gX_TextWriter_SelectChoice(&writer, 0) == 1,
                "select without choices fails");
            gX_TextWriter_JumpLine(&writer, 5);
            TW_TEST_ASSERT(gX_TextWriter_GetChoiceCount(&writer) == 1
                && strlen((const char *)gX_TextWriter_GetChoiceText(&writer, 0)) == 63,
                "choice text truncation");
            remove(choicePath);
        }
    }

    //opencode: TXT save round trip — edits persist, unloaded writers refuse,
    //   and shipped text paths are refused under the harness guard. The guard
    //   probe path is assembled at runtime: the deps audit scans src/ for
    //   literal resource-path tokens and this deliberate non-file must not
    //   read as a missing reference.
    {
        static gX_I8 savePath[] = "/tmp/opencode/gx_tw_saveXXXXXX";
        gX_I32 sfd = mkstemp(savePath);
        TW_TEST_ASSERT(sfd >= 0, "save scratch creation");
        if (sfd >= 0) {
            close(sfd);
            remove(savePath);

            gX_RendererTextWriter_Load(&writer);
            //opencode: Load keeps field state — UnloadFile clears the buffer
            //   so the no-file save path is the one under test.
            gX_TextWriter_UnloadFile(&writer);
            TW_TEST_ASSERT(gX_TextWriter_SaveFileTXT(&writer, savePath) == 1,
                "save without a loaded file fails");

            FILE *sf = fopen(savePath, "w");
            if (sf) {
                fputs("first line\n<red>second</>\nthird\n", sf);
                fclose(sf);
            }
            TW_TEST_ASSERT(sf != NULL
                && gX_TextWriter_LoadFileTXT(&writer, savePath) == 0
                && writer.lineBuffer.linesAmount == 3,
                "save probe load");
            strcpy((char *)writer.lineBuffer.lines[1], "edited <b>second</b>");
            TW_TEST_ASSERT(gX_TextWriter_SaveFileTXT(&writer, savePath) == 0,
                "lines saved");
            gX_RendererTextWriter_Load(&writer);
            TW_TEST_ASSERT(gX_TextWriter_LoadFileTXT(&writer, savePath) == 0
                && writer.lineBuffer.linesAmount == 3
                && !strcmp((const char *)writer.lineBuffer.lines[0], "first line")
                && !strcmp((const char *)writer.lineBuffer.lines[1], "edited <b>second</b>"),
                "saved lines reload identically");

            gX_I8 guardPath[128];
            snprintf((char *)guardPath, sizeof(guardPath),
                     "%s/text/should_not_write.txt", "resources");
            const gX_I8 *guardPrev = (const gX_I8 *)getenv("GX_NO_SETTINGS_SAVE");
            gX_I8 *guardSaved = guardPrev ? (gX_I8 *)strdup((const char *)guardPrev) : NULL;
            TW_TEST_ASSERT(!guardPrev || guardSaved, "harness guard snapshot allocated");
            if (!guardPrev || guardSaved)
            {
                gX_I32 guardSet = setenv("GX_NO_SETTINGS_SAVE", "1", 1);
                TW_TEST_ASSERT(guardSet == 0, "harness guard set");
                if (guardSet == 0)
                {
                    TW_TEST_ASSERT(gX_TextWriter_SaveFileTXT(&writer, guardPath) == 1,
                        "shipped text refused under the harness guard");
                }
                gX_I32 guardRestored = guardSaved
                    ? setenv("GX_NO_SETTINGS_SAVE", (const char *)guardSaved, 1)
                    : unsetenv("GX_NO_SETTINGS_SAVE");
                TW_TEST_ASSERT(guardRestored == 0, "inherited harness guard restored");
                free(guardSaved);
            }
            FILE *guardCheck = fopen((const char *)guardPath, "rb");
            TW_TEST_ASSERT(guardCheck == NULL, "refusal wrote nothing");
            if (guardCheck) fclose(guardCheck);
            remove(savePath);
        }
    }

    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GX_TESTS: textwriter parser — 0 errors");
    }
    return errors;
}
