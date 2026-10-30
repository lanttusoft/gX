/**********************************************************************************************
*
*   [gX: src/console/console.c]
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


//ai: ported from gameX src/common/console.c — ring log, history, built-in
//   command dispatch and the AngelScript one-liner fall-through. Built-in set
//   distilled to gX: demoscript commands dropped; /scripts and /state added;
//   gX directory listing, scene registry, msg box and KV store wired in.
#include "console.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/log.h"
#include "../common/msg.h"
#include "../common/enum.h"
#include "../wrappers/file.h"

#include "../main.h"
#include "../input.h"
#include "../scripting.h"

extern gX_I0 gX_GUI_Load_ImGui(gX_I0);

gX_Console g_console;

static gX_I8 g_consoleExecBatch[2048];
static gX_I1 g_consoleExecBatchUsed = 0;

gX_I0 gX_Console_Init(gX_I0) {
    memset(&g_console, 0, sizeof(g_console));
    snprintf(g_console.name, sizeof(g_console.name), "console");
    snprintf(g_console.description, sizeof(g_console.description),
        "Developer console — shell log, history and command dispatch");

    const gX_I8 *batch = getenv("GX_CONSOLE_EXEC");
    if (batch && batch[0]) {
        snprintf(g_consoleExecBatch, sizeof(g_consoleExecBatch), "%s", batch);
        g_console.execPending = 1;
    }
}

gX_I0 gX_Console_Uninit(gX_I0) {
    memset(&g_console, 0, sizeof(g_console));
    g_consoleExecBatchUsed = 0;
}

gX_I0 gX_Console_SetOpen(gX_I1 open) {
    if (open && !g_console.open) gX_GUI_Load_ImGui();
    g_console.open = open;
}

gX_I1 gX_Console_IsOpen(gX_I0) {
    return g_console.open;
}

gX_I0 gX_Console_Update(gX_I0) {
    if (input.keyboard.keys[input.consoleKey].state.PRESSED) {
        gX_Console_SetOpen(!g_console.open);
    }

    if (!g_console.execPending) return;
    g_console.execPending = 0;
    if (g_consoleExecBatchUsed) return;
    g_consoleExecBatchUsed = 1;

    gX_I8 line[GX_CONSOLE_INPUT_MAX];
    gX_U32 lineLen = 0;
    for (const gX_I8 *p = g_consoleExecBatch; ; p++) {
        if (*p == '\n' || *p == '\0') {
            line[lineLen] = '\0';
            if (lineLen > 0) gX_Console_Execute(line);
            lineLen = 0;
            if (*p == '\0') break;
            continue;
        }
        if (lineLen < sizeof(line) - 1) line[lineLen++] = *p;
    }
}

gX_I0 gX_Console_Clear(gX_I0) {
    memset(g_console.log, 0, sizeof(g_console.log));
    g_console.logCount = 0;
    g_console.logWrite = 0;
}

gX_I0 gX_Console_AddLog(const gX_I8 *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gX_I8 buf[GX_CONSOLE_LOG_LINE_MAX];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    gX_U32 pos = g_console.logWrite;
    snprintf(g_console.log[pos], GX_CONSOLE_LOG_LINE_MAX, "%s", buf);
    g_console.logWrite = (g_console.logWrite + 1) % GX_CONSOLE_LOG_MAX;
    if (g_console.logCount < GX_CONSOLE_LOG_MAX) g_console.logCount++;

    gX_TraceLog_COMMON(gX_LOG_INFO, "[CONSOLE] %s", buf);
    gX_Msg_Output_Push(buf);
}

gX_U32 gX_Console_GetLogCount(gX_I0) {
    return g_console.logCount;
}

gX_I32 gX_Console_GetLogLine(gX_U32 i, gX_I8 *out, gX_U32 outSize) {
    if (i >= g_console.logCount || !out) return -1;
    gX_U32 pos = (g_console.logCount < GX_CONSOLE_LOG_MAX)
        ? i
        : (g_console.logWrite + i) % GX_CONSOLE_LOG_MAX;
    snprintf(out, outSize, "%s", g_console.log[pos]);
    return 0;
}

gX_U32 gX_Console_GetHistoryCount(gX_I0) {
    return g_console.historyCount;
}

gX_I32 gX_Console_GetHistoryLine(gX_U32 i, gX_I8 *out, gX_U32 outSize) {
    if (i >= g_console.historyCount || !out) return -1;
    snprintf(out, outSize, "%s", g_console.history[i]);
    return 0;
}

static gX_I0 gX_Console_Help(gX_I0) {
    gX_Console_AddLog("=== Console help ===");
    gX_Console_AddLog("  /clear, /cls        Clear the console log");
    gX_Console_AddLog("  /help, /? [cmd]     Show this help, or details for a command");
    gX_Console_AddLog("  /history            Show command history");
    gX_Console_AddLog("  /scene <name>       Switch scene (TITLE/RENDERER_GFX/PHYSICS/...)");
    gX_Console_AddLog("  /scenes             List all available scenes");
    gX_Console_AddLog("  /msg <text>         Push a message to the on-screen msg box");
    gX_Console_AddLog("  /echo <text>        Echo text to the console log");
    gX_Console_AddLog("  /time               Show elapsed time and average FPS");
    gX_Console_AddLog("  /stats              Show engine runtime statistics");
    gX_Console_AddLog("  /ls <dir>           List files in a directory");
    gX_Console_AddLog("  /reload             Hot-reload all scripts");
    gX_Console_AddLog("  /exec <file.as>     Load and run an AngelScript file");
    gX_Console_AddLog("  /run <file.as>      Load and run (extension-checked)");
    gX_Console_AddLog("  /scripts            List loaded scripts + pause state");
    gX_Console_AddLog("  /state [save|load|clear]  Script KV: dump, persist, reset");
    gX_Console_AddLog("  Anything else runs as AngelScript (wrapped in void main()).");
    gX_Console_AddLog("  Up/Down arrows navigate command history. F10 toggles.");
}

static gX_I0 gX_Console_HelpCommand(const gX_I8 *cmd) {
    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        gX_Console_AddLog("/clear — Clear the console log buffer");
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        gX_Console_AddLog("/help [cmd] — Show help (with a command name for details)");
    } else if (strcmp(cmd, "history") == 0) {
        gX_Console_AddLog("/history — Show command history (Up/Down recalls)");
    } else if (strcmp(cmd, "scene") == 0) {
        gX_Console_AddLog("/scene <name> — Switch scene; see /scenes for names");
    } else if (strcmp(cmd, "scenes") == 0) {
        gX_Console_AddLog("/scenes — List every scene name in the registry");
    } else if (strcmp(cmd, "msg") == 0) {
        gX_Console_AddLog("/msg <text> — Push an on-screen message (top-left box)");
    } else if (strcmp(cmd, "echo") == 0) {
        gX_Console_AddLog("/echo <text> — Echo text back into the log");
    } else if (strcmp(cmd, "time") == 0) {
        gX_Console_AddLog("/time — Elapsed HH:MM:SS and average FPS");
    } else if (strcmp(cmd, "stats") == 0) {
        gX_Console_AddLog("/stats — FPS, frames, time, scene, backend, scripts");
    } else if (strcmp(cmd, "ls") == 0) {
        gX_Console_AddLog("/ls <dir> — List files (default '.'); no subdirs");
    } else if (strcmp(cmd, "reload") == 0) {
        gX_Console_AddLog("/reload — Reload, recompile and restart all scripts");
    } else if (strcmp(cmd, "exec") == 0) {
        gX_Console_AddLog("/exec <file.as> — Load and start a script file");
    } else if (strcmp(cmd, "run") == 0) {
        gX_Console_AddLog("/run <file.as> — Extension-checked /exec");
    } else if (strcmp(cmd, "scripts") == 0) {
        gX_Console_AddLog("/scripts — List loaded scripts (index, name, path, paused)");
    } else if (strcmp(cmd, "state") == 0) {
        gX_Console_AddLog("/state — Dump the script KV store");
        gX_Console_AddLog("  /state save  — persist to the state file (see docs/scripting.md)");
        gX_Console_AddLog("  /state load  — reload from the state file");
        gX_Console_AddLog("  /state clear — drop all entries (not the file)");
    } else {
        gX_Console_AddLog("Unknown command: %s  (try /help)", cmd);
    }
}

static gX_I0 gX_Console_History(gX_I0) {
    gX_Console_AddLog("Command history (%u entries):", g_console.historyCount);
    for (gX_U32 i = 0; i < g_console.historyCount; i++) {
        gX_Console_AddLog("  %3u: %s", i, g_console.history[i]);
    }
}

static gX_I0 gX_Console_CmdScene(const gX_I8 *name) {
    gX_I32 idx = -1;
    #define X(scene) if (!strcmp(name, #scene)) idx = gX_SCENE_##scene;
    gX_SCENES
    #undef X
    if (idx >= 0) {
        gX_MainData_Switch(idx);
        gX_Console_AddLog("Switched to scene '%s'", name);
    } else {
        gX_Console_AddLog("Unknown scene: '%s'  (try /scenes)", name);
    }
}

static gX_I0 gX_Console_CmdStats(gX_I0) {
    gX_F64 fps = (maindata.timeElapsed > 0.0)
        ? (gX_F64)maindata.frameCount / maindata.timeElapsed : 0.0;
    gX_Console_AddLog("=== Engine stats ===");
    gX_Console_AddLog("FPS avg:   %.1f", fps);
    gX_Console_AddLog("Frames:    %llu", (unsigned long long)maindata.frameCount);
    gX_Console_AddLog("Time:      %.2f s", maindata.timeElapsed);
    gX_Console_AddLog("Scene:     %d [%s]", maindata.masterIndex,
        gX_Scenes_String(maindata.masterIndex));
    gX_Console_AddLog("Backend:   raylib");
    gX_Console_AddLog("Scripts:   %u loaded", gX_Scripting_ScriptCount());
}

static gX_I0 gX_Console_CmdScripts(gX_I0) {
    gX_U32 count = gX_Scripting_ScriptCount();
    gX_Console_AddLog("=== Scripts (%u loaded) ===", count);
    for (gX_U32 i = 0; i < count; i++) {
        const gX_I8 *path = gX_Scripting_ScriptPath(i);
        if (!path[0]) continue;
        gX_Console_AddLog("  [%u] %s %s", i, gX_Scripting_ScriptName(i),
            gX_Scripting_Debug_IsPaused(i) ? "(paused)" : "");
    }
}

static gX_I0 gX_Console_CmdState(const gX_I8 *arg) {
    if (!arg || !arg[0]) {
        gX_U32 shown = 0;
        for (gX_U32 i = 0; i < GX_SCRIPT_STATE_MAX; i++) {
            if (!g_scripting.stateUsed[i]) continue;
            switch (g_scripting.stateTypes[i]) {
                case 'i':
                    gX_Console_AddLog("  %s = %d (int)", g_scripting.stateNames[i], g_scripting.stateInts[i]);
                    break;
                case 'f':
                    gX_Console_AddLog("  %s = %g (float)", g_scripting.stateNames[i], g_scripting.stateFloats[i]);
                    break;
                case 's':
                    gX_Console_AddLog("  %s = \"%s\" (string)", g_scripting.stateNames[i], g_scripting.stateStrings[i]);
                    break;
                default:
                    continue;
            }
            shown++;
        }
        gX_Console_AddLog("=== Script state: %u entr%s ===", shown, shown == 1 ? "y" : "ies");
    } else if (strcmp(arg, "save") == 0) {
        gX_Scripting_State_Save();
        gX_Console_AddLog("Script state saved (%u entries)", g_scripting.stateCount);
    } else if (strcmp(arg, "load") == 0) {
        gX_Scripting_State_Load();
        gX_Console_AddLog("Script state loaded (%u entries)", g_scripting.stateCount);
    } else if (strcmp(arg, "clear") == 0) {
        gX_Scripting_State_Clear();
        gX_Console_AddLog("Script state cleared");
    } else {
        gX_Console_AddLog("Usage: /state [save|load|clear]");
    }
}

static gX_I0 gX_Console_CmdLs(const gX_I8 *path) {
    if (!path || !path[0]) path = ".";
    gX_File_Directory listing;
    gX_I32 count = gX_File_Directory_List(path, &listing);
    if (count < 0) {
        gX_Console_AddLog("Cannot list '%s'", path);
        return;
    }
    gX_Console_AddLog("=== Directory: %s (%d entries) ===", path, count);
    for (gX_I32 i = 0; i < count; i++) {
        gX_Console_AddLog("  %s", listing.names[i]);
    }
}

static gX_I0 gX_Console_CmdReload(gX_I0) {
    gX_U32 count = 0;
    gX_U32 total = gX_Scripting_ScriptCount();
    for (gX_U32 i = 0; i < total; i++) {
        gX_Script *s = gX_Scripting_GetScript(i);
        if (!s) continue;
        if (gX_Script_Reload(s) == 0) count++;
    }
    gX_Console_AddLog("Reloaded %u script(s)", count);
}

static gX_I0 gX_Console_CmdExec(const gX_I8 *path) {
    if (!path || !path[0]) {
        gX_Console_AddLog("Usage: /exec <file.as>");
        return;
    }
    gX_I32 idx = gX_Scripting_AddScript(path);
    if (idx < 0) {
        gX_Console_AddLog("Failed to load '%s' (code %d)", path, idx);
        return;
    }
    gX_Console_AddLog("Script '%s' started (index %d)", path, idx);
}

gX_I0 gX_Console_Execute(const gX_I8 *line) {
    if (!line || !line[0]) return;

    gX_U32 hc = g_console.historyCount;
    if (hc < GX_CONSOLE_HISTORY_MAX) {
        snprintf(g_console.history[hc], GX_CONSOLE_INPUT_MAX, "%s", line);
        g_console.historyCount++;
    } else {
        for (gX_U32 i = 1; i < GX_CONSOLE_HISTORY_MAX; i++) {
            memcpy(g_console.history[i - 1], g_console.history[i], GX_CONSOLE_INPUT_MAX);
        }
        snprintf(g_console.history[GX_CONSOLE_HISTORY_MAX - 1],
            GX_CONSOLE_INPUT_MAX, "%s", line);
    }

    gX_Console_AddLog("> %s", line);

    if (line[0] == '/') {
        if (strcmp(line, "/clear") == 0 || strcmp(line, "/cls") == 0) {
            gX_Console_Clear();
            return;
        }
        if (strncmp(line, "/help ", 6) == 0) {
            const gX_I8 *cmd = line + 6;
            while (*cmd == ' ') cmd++;
            if (*cmd) { gX_Console_HelpCommand(cmd); return; }
        }
        if (strncmp(line, "/? ", 3) == 0) {
            const gX_I8 *cmd = line + 3;
            while (*cmd == ' ') cmd++;
            if (*cmd) { gX_Console_HelpCommand(cmd); return; }
        }
        if (strcmp(line, "/help") == 0 || strcmp(line, "/?") == 0) {
            gX_Console_Help();
            return;
        }
        if (strcmp(line, "/history") == 0) {
            gX_Console_History();
            return;
        }
        if (strcmp(line, "/scenes") == 0) {
            gX_Console_AddLog("=== Available scenes ===");
            #define X(scene) gX_Console_AddLog("  %s", #scene);
            gX_SCENES
            #undef X
            return;
        }
        if (strncmp(line, "/scene ", 7) == 0) {
            const gX_I8 *name = line + 7;
            while (*name == ' ') name++;
            if (*name) gX_Console_CmdScene(name);
            return;
        }
        if (strncmp(line, "/msg ", 5) == 0) {
            const gX_I8 *text = line + 5;
            while (*text == ' ') text++;
            if (*text) {
                gX_Msg_Output_Push(text);
                gX_Console_AddLog("Message: %s", text);
            }
            return;
        }
        if (strncmp(line, "/echo ", 6) == 0) {
            const gX_I8 *text = line + 6;
            while (*text == ' ') text++;
            if (*text) gX_Console_AddLog("%s", text);
            return;
        }
        if (strcmp(line, "/time") == 0) {
            gX_F64 elapsed = maindata.timeElapsed;
            gX_U32 h = (gX_U32)(elapsed / 3600.0);
            gX_U32 m = (gX_U32)((elapsed - h * 3600.0) / 60.0);
            gX_F64 s = elapsed - (gX_F64)(h * 3600 + m * 60);
            gX_F64 fps = (elapsed > 0.0) ? (gX_F64)maindata.frameCount / elapsed : 0.0;
            gX_Console_AddLog("Time: %02u:%02u:%05.2f  FPS avg: %.1f", h, m, s, fps);
            return;
        }
        if (strcmp(line, "/stats") == 0) {
            gX_Console_CmdStats();
            return;
        }
        if (strcmp(line, "/scripts") == 0) {
            gX_Console_CmdScripts();
            return;
        }
        if (strcmp(line, "/state") == 0) {
            gX_Console_CmdState("");
            return;
        }
        if (strncmp(line, "/state ", 7) == 0) {
            const gX_I8 *arg = line + 7;
            while (*arg == ' ') arg++;
            gX_Console_CmdState(arg);
            return;
        }
        if (strncmp(line, "/ls", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
            const gX_I8 *path = line + 3;
            while (*path == ' ') path++;
            gX_Console_CmdLs(path);
            return;
        }
        if (strcmp(line, "/reload") == 0) {
            gX_Console_CmdReload();
            return;
        }
        if (strncmp(line, "/exec ", 6) == 0) {
            const gX_I8 *path = line + 6;
            while (*path == ' ') path++;
            gX_Console_CmdExec(path);
            return;
        }
        if (strncmp(line, "/run ", 5) == 0) {
            const gX_I8 *path = line + 5;
            while (*path == ' ') path++;
            const gX_I8 *dot = path ? strrchr(path, '.') : NULL;
            if (dot && strcmp(dot, ".as") == 0) {
                gX_Console_CmdExec(path);
            } else if (dot) {
                gX_Console_AddLog("Unsupported extension '%s' (only .as)", dot);
            } else {
                gX_Console_AddLog("No extension in '%s' (try <name>.as)", path);
            }
            return;
        }

        gX_Console_AddLog("Unknown command: %s  (try /help)", line);
        return;
    }

    gX_I32 result = gX_Console_ExecScript(line);
    if (result != 0) {
        gX_Console_AddLog("Script error (code %d) — see the log for details", result);
    }
}
