/**********************************************************************************************
*
*   [gX: src/console/console.h]
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


//ai: ported from gameX src/common/console.h — the shell's interactive
//   front-end: ring-buffer log, command history, built-in commands and a
//   one-liner AngelScript fall-through. Distilled: trimmed buffers, no
//   demoscript/stringtable couplings, gX-native command set (see console.c).
#ifndef GX_CONSOLE_H
#define GX_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

#define GX_CONSOLE_LOG_MAX      512
#define GX_CONSOLE_LOG_LINE_MAX 320
#define GX_CONSOLE_HISTORY_MAX  64
#define GX_CONSOLE_INPUT_MAX    512

typedef struct gX_Console {
    gX_I8  name[128];
    gX_I8  description[256];

    gX_I8  log[GX_CONSOLE_LOG_MAX][GX_CONSOLE_LOG_LINE_MAX];
    gX_U32 logCount;
    gX_U32 logWrite; //next slot to write (circular)

    gX_I8  history[GX_CONSOLE_HISTORY_MAX][GX_CONSOLE_INPUT_MAX];
    gX_U32 historyCount;

    gX_I8  input[GX_CONSOLE_INPUT_MAX];

    gX_I1  open;
    gX_I1  execPending; //GX_CONSOLE_EXEC batch runs on the first Update
} gX_Console;

extern gX_Console g_console;

gX_I0 gX_Console_Init(gX_I0);
gX_I0 gX_Console_Uninit(gX_I0);

//F10 toggle + the one-shot GX_CONSOLE_EXEC batch (called from the main loop).
gX_I0 gX_Console_Update(gX_I0);

//ImGui panel (drawn by the tools ImGui pass; no-op while closed).
gX_I0 gX_Console_Draw(gX_I0);

gX_I0 gX_Console_SetOpen(gX_I1 open);
gX_I1 gX_Console_IsOpen(gX_I0);

gX_I0 gX_Console_Clear(gX_I0);
gX_I0 gX_Console_AddLog(const gX_I8 *fmt, ...);
gX_I0 gX_Console_Execute(const gX_I8 *line);

gX_I32 gX_Console_GetLogLine(gX_U32 i, gX_I8 *out, gX_U32 outSize);
gX_U32 gX_Console_GetLogCount(gX_I0);
gX_I32 gX_Console_GetHistoryLine(gX_U32 i, gX_I8 *out, gX_U32 outSize);
gX_U32 gX_Console_GetHistoryCount(gX_I0);

//implemented in console_exec.cpp — compile + run one statement (or several)
//   as a throwaway module wrapped in void main(). 0 on success.
gX_I32 gX_Console_ExecScript(const gX_I8 *line);

#ifdef __cplusplus
}
#endif

#endif //GX_CONSOLE_H
