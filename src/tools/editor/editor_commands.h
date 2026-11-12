/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_commands.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — small C command registry shared by editor menus, buttons,
//   shortcuts and headless probes. Commands are copied; no allocations.
#ifndef GX_TOOLS_EDITOR_COMMANDS_H
#define GX_TOOLS_EDITOR_COMMANDS_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_EDITOR_COMMAND_MAX 32
#define GX_EDITOR_COMMAND_ID 96
#define GX_EDITOR_COMMAND_LABEL 128
#define GX_EDITOR_COMMAND_SHORTCUT 32

typedef struct gX_EditorCommand
{
    const gX_I8 *id;
    const gX_I8 *label;
    const gX_I8 *shortcut;
    gX_I1 (*enabled)(gX_I0 *user);
    gX_I0 (*run)(gX_I0 *user);
    gX_I0 *user;
} gX_EditorCommand;

//ai: register/replace a command by id; returns 1 on success.
gX_I32 gX_EditorCommands_Register(const gX_EditorCommand *command);
gX_I32 gX_EditorCommands_Unregister(const gX_I8 *id);
//ai: run returns 1 only when the command exists, is enabled and ran.
gX_I1 gX_EditorCommands_Run(const gX_I8 *id);
gX_I1 gX_EditorCommands_Enabled(const gX_I8 *id);
gX_I32 gX_EditorCommands_Count(gX_I0);
const gX_EditorCommand *gX_EditorCommands_At(gX_I32 index);

#ifdef __cplusplus
}
#endif

#endif
