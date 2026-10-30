/**********************************************************************************************
*
*   [gX: src/tools/textwriter/textwriter_editor.cpp]
*   Textwriter editor — author TXT lines/tags/choices with live scene preview
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


//opencode: engine-native surface for the textwriter module's live line buffer
//   (renderer.textwriter.data): per-line editing with live preview (▶ jumps
//   the scene), a tag palette, choice/jump snippets, a fixture list and
//   guarded save/load (gX_TextWriter_LoadFileTXT / SaveFileTXT — shipped text
//   paths are refused under GX_NO_SETTINGS_SAVE). Opened by
//   --textwriter-editor, GX_TOOLS_OPEN=textwriter, or the title EXTRAS menu.
//opencode: raylib headers FIRST with C++ linkage — wrapper headers reached
//   through renderer.h would give raymath's operator overloads C linkage
//   (see src/tools/gfx/gfx_editor.cpp).
#include "raylib.h"
#include "raymath.h"

#include "../imgui/imgui.h"

#include "../../renderer/renderer.h"
#include "../../renderer/textwriter.h"
#include "../../common/log.h"
#include "../../wrappers/file.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" gX_I1 *gX_Tools_RuntimeTextwriterFlag(gX_I0);

//opencode: kernel profile — the textwriter scene lives in the renderer
//   cluster, so the whole implementation is guarded and both entry points
//   become no-ops there (previously the editor's renderer references broke
//   `make check-kernel`; same shape as the physics editor).
#if gX_ENABLE_RENDERER == 1

#define GX_TW_EDITOR_PATH_MAX 256
#define GX_TW_EDITOR_FILE_MAX 16
#define GX_TW_EDITOR_DIR "resources/text"

static gX_I32 s_twSelected = 0;
static gX_I8  s_twPath[GX_TW_EDITOR_PATH_MAX] = "save/textwriter_edit.txt";
static gX_I8  s_twStatus[192] = "ready";
static gX_I8  s_twFiles[GX_TW_EDITOR_FILE_MAX][128];
static gX_I32 s_twFileCount = 0;

static gX_RendererTextWriter *TwEditor_Instance(void) {
    if (!renderer.textwriter.data || renderer.textwriter.amount == 0) return NULL;
    return (gX_RendererTextWriter *)renderer.textwriter.data;
}

static gX_I0 TwEditor_Status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)s_twStatus, sizeof(s_twStatus), fmt, args);
    va_end(args);
}

static gX_I0 TwEditor_RefreshFiles(void) {
    s_twFileCount = 0;
    gX_File_Directory listing;
    if (gX_File_Directory_List(GX_TW_EDITOR_DIR, &listing) > 0) {
        for (gX_I32 i = 0; i < listing.count && s_twFileCount < GX_TW_EDITOR_FILE_MAX; i++) {
            const gX_I8 *name = listing.names[i];
            const gX_I8 *dot = strrchr((const char *)name, '.');
            if (!dot || strcmp((const char *)dot, ".txt") != 0) continue;
            strncpy((char *)s_twFiles[s_twFileCount], (const char *)name, 127);
            s_twFiles[s_twFileCount][127] = '\0';
            s_twFileCount++;
        }
    }
}

//opencode: re-parse a line and reset reveal so the scene shows the edit.
static gX_I0 TwEditor_RefreshLine(gX_RendererTextWriter *tw, gX_U32 line) {
    if (!tw->lineBuffer.loaded || line >= tw->lineBuffer.linesAmount) return;
    gX_TextWriter_JumpLine(tw, line);
}

static gX_I0 TwEditor_LoadPath(gX_RendererTextWriter *tw, const gX_I8 *path) {
    gX_I32 rc = gX_TextWriter_LoadFileTXT(tw, path);
    if (rc == 0) {
        s_twSelected = 0;
        TwEditor_Status("loaded %s (%u lines)", path, tw->lineBuffer.linesAmount);
    } else {
        TwEditor_Status("LOAD FAILED <- %s", path);
    }
}

//opencode: palette insertion — snippet goes at the end of the selected line;
//   a live line re-parses immediately so the scene previews the result.
static gX_I0 TwEditor_AppendSnippet(const char *snippet) {
    gX_RendererTextWriter *tw = TwEditor_Instance();
    if (!tw || !tw->lineBuffer.loaded) {
        TwEditor_Status("no file loaded");
        return;
    }
    if (s_twSelected < 0 || s_twSelected >= (gX_I32)tw->lineBuffer.linesAmount) {
        TwEditor_Status("no line selected");
        return;
    }
    gX_I8 *line = tw->lineBuffer.lines[s_twSelected];
    size_t len = strlen((const char *)line);
    size_t add = strlen(snippet);
    if (len + add + 1 > GX_TEXTWRITER_LINE_LENGTH) {
        TwEditor_Status("line %d is full", s_twSelected);
        return;
    }
    memcpy(line + len, snippet, add + 1);
    if ((gX_U32)s_twSelected == tw->lineBuffer.currentLine) {
        TwEditor_RefreshLine(tw, (gX_U32)s_twSelected);
    }
    TwEditor_Status("inserted into line %d", s_twSelected);
}

extern "C" gX_I0 gX_Tools_Textwriter_Reset(gX_I0);
extern "C" gX_I0 gX_Tools_Textwriter_Reset(gX_I0) {
    gX_RendererTextWriter *tw = TwEditor_Instance();
    if (tw && !tw->lineBuffer.loaded) {
        gX_TextWriter_LoadFileTXT(tw, GX_TW_EDITOR_DIR "/textwriter.txt");
    }
    TwEditor_RefreshFiles();
    s_twSelected = 0;
    TwEditor_Status("ready — > previews a line on the textwriter scene");
}

extern "C" gX_I0 gX_Tools_Textwriter_Draw(gX_I0);
extern "C" gX_I0 gX_Tools_Textwriter_Draw(gX_I0) {
    gX_I1 open = *gX_Tools_RuntimeTextwriterFlag();
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(660, 660), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("gX Tools - Textwriter Editor", &open)) {
        gX_RendererTextWriter *tw = TwEditor_Instance();
        if (!tw) {
            ImGui::TextUnformatted("textwriter module unavailable");
        } else {
            gX_TextWriter_LineBuffer *buf = &tw->lineBuffer;

            // ---- file row ----
            ImGui::SetNextItemWidth(-215.0f);
            ImGui::InputText("##twpath", (char *)s_twPath, sizeof(s_twPath));
            ImGui::SameLine();
            if (ImGui::Button("Load .txt")) TwEditor_LoadPath(tw, s_twPath);
            ImGui::SameLine();
            if (ImGui::Button("Save .txt")) {
                gX_I32 rc = gX_TextWriter_SaveFileTXT(tw, s_twPath);
                if (rc == 0) {
                    TwEditor_Status("saved %u lines -> %s", buf->linesAmount, s_twPath);
                    TwEditor_RefreshFiles();
                } else {
                    TwEditor_Status("SAVE FAILED -> %s", s_twPath);
                }
            }

            ImGui::BeginChild("TwEditorFiles", ImVec2(0, 86), true);
            if (s_twFileCount == 0) ImGui::TextUnformatted("(no .txt fixtures found)");
            for (gX_I32 i = 0; i < s_twFileCount; i++) {
                if (ImGui::Selectable((const char *)s_twFiles[i])) {
                    snprintf((char *)s_twPath, sizeof(s_twPath), GX_TW_EDITOR_DIR "/%s",
                             (const char *)s_twFiles[i]);
                    TwEditor_LoadPath(tw, s_twPath);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) TwEditor_RefreshFiles();
            ImGui::SameLine();
            if (ImGui::Button("Reload from disk")) TwEditor_LoadPath(tw, s_twPath);

            // ---- buffer controls ----
            ImGui::Text("lines %u   current %u   selected %d",
                        buf->linesAmount, buf->currentLine, s_twSelected);
            ImGui::SameLine();
            if (ImGui::Button("Add line")) {
                if (buf->linesAmount < GX_TEXTWRITER_LINES_MAX) {
                    buf->lines[buf->linesAmount][0] = '\0';
                    buf->linesAmount++;
                    TwEditor_Status("line appended (%u total)", buf->linesAmount);
                } else {
                    TwEditor_Status("line limit (%d) reached", GX_TEXTWRITER_LINES_MAX);
                }
            }

            // ---- palette ----
            ImGui::SeparatorText("tag palette (inserts into the selected line)");
            struct { const char *label; const char *snippet; } colors[] = {
                { "red", "<red>text</>" }, { "green", "<green>text</>" },
                { "blue", "<blue>text</>" }, { "yellow", "<yellow>text</>" },
                { "cyan", "<cyan>text</>" }, { "magenta", "<magenta>text</>" },
                { "white", "<white>text</>" },
            };
            for (gX_I32 i = 0; i < (gX_I32)(sizeof(colors) / sizeof(colors[0])); i++) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(colors[i].label)) TwEditor_AppendSnippet(colors[i].snippet);
            }
            struct { const char *label; const char *snippet; } styles[] = {
                { "bold", "<b>text</>" }, { "italic", "<i>text</>" },
                { "underline", "<u>text</>" }, { "wave", "<wave>text</>" },
                { "rainbow", "<rainbow>text</>" }, { "shake", "<shake>text</>" },
                { "size", "<size v='32'>text</>" },
            };
            for (gX_I32 i = 0; i < (gX_I32)(sizeof(styles) / sizeof(styles[0])); i++) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(styles[i].label)) TwEditor_AppendSnippet(styles[i].snippet);
            }
            struct { const char *label; const char *snippet; } specials[] = {
                { "ruby", "text<ruby rt='note' />" },
                { "font", "<font path='resources/font/KenneyMiniSquareMono.ttf'>text</>" },
            };
            for (gX_I32 i = 0; i < (gX_I32)(sizeof(specials) / sizeof(specials[0])); i++) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(specials[i].label)) TwEditor_AppendSnippet(specials[i].snippet);
            }
            gX_I8 snippet[160];
            snprintf((char *)snippet, sizeof(snippet),
                     "<jump line='%d' />", (s_twSelected + 1 < (gX_I32)buf->linesAmount) ? s_twSelected + 1 : 0);
            if (ImGui::SmallButton("jump")) TwEditor_AppendSnippet(snippet);
            ImGui::SameLine();
            snprintf((char *)snippet, sizeof(snippet),
                     "<choice><option line='%d'>text</option></choice>",
                     (s_twSelected + 1 < (gX_I32)buf->linesAmount) ? s_twSelected + 1 : 0);
            if (ImGui::SmallButton("choice")) TwEditor_AppendSnippet(snippet);

            // ---- line list ----
            ImGui::SeparatorText("lines (> previews, click a field to select)");
            ImGui::BeginChild("TwEditorLines", ImVec2(0, 250), true);
            for (gX_U32 i = 0; i < buf->linesAmount; i++) {
                ImGui::PushID((gX_I32)i);
                if (ImGui::SmallButton(">")) {
                    s_twSelected = (gX_I32)i;
                    TwEditor_RefreshLine(tw, i);
                }
                ImGui::SameLine();
                ImGui::Text("%3u", i);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-46.0f);
                if (ImGui::InputText("##line", (char *)buf->lines[i], GX_TEXTWRITER_LINE_LENGTH)) {
                    if (i == buf->currentLine) TwEditor_RefreshLine(tw, i);
                }
                if (ImGui::IsItemActive()) s_twSelected = (gX_I32)i;
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    for (gX_U32 j = i; j + 1 < buf->linesAmount; j++) {
                        memcpy(buf->lines[j], buf->lines[j + 1], GX_TEXTWRITER_LINE_LENGTH);
                    }
                    buf->linesAmount--;
                    if (buf->linesAmount == 0) {
                        buf->loaded = false;
                        tw->textFileLoaded = false;
                    } else {
                        if (buf->currentLine >= buf->linesAmount) buf->currentLine = buf->linesAmount - 1;
                        if (s_twSelected >= (gX_I32)buf->linesAmount) s_twSelected = (gX_I32)buf->linesAmount - 1;
                        TwEditor_RefreshLine(tw, buf->currentLine);
                    }
                    ImGui::PopID();
                    break; // the list changed under the loop; redraw next frame
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::TextUnformatted((const char *)s_twStatus);
        }
    }
    ImGui::End();
    if (!open) *gX_Tools_RuntimeTextwriterFlag() = 0;
}

#else //!gX_ENABLE_RENDERER — no textwriter scene to edit (kernel profile)

extern "C" gX_I0 gX_Tools_Textwriter_Reset(gX_I0) { }
extern "C" gX_I0 gX_Tools_Textwriter_Draw(gX_I0) { }

#endif
