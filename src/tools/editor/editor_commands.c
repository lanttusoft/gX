/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_commands.c]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — bounded C command registry. The editor kernel owns command
//   identity; tools own the callbacks and user pointer.
#include "editor_commands.h"
#include "../../common/log.h"

#include <string.h>

typedef struct gX_EditorCommandSlot
{
    gX_I1 used;
    gX_EditorCommand command;
    gX_I8 id[GX_EDITOR_COMMAND_ID];
    gX_I8 label[GX_EDITOR_COMMAND_LABEL];
    gX_I8 shortcut[GX_EDITOR_COMMAND_SHORTCUT];
} gX_EditorCommandSlot;

static gX_EditorCommandSlot s_commands[GX_EDITOR_COMMAND_MAX];

static gX_EditorCommandSlot *gX_EditorCommands_Find(const gX_I8 *id)
{
    if (!id || !id[0]) return NULL;
    for (gX_I32 i = 0; i < GX_EDITOR_COMMAND_MAX; i++)
    {
        if (s_commands[i].used
            && strcmp((const char *)s_commands[i].id, (const char *)id) == 0)
            return &s_commands[i];
    }
    return NULL;
}

gX_I32 gX_EditorCommands_Register(const gX_EditorCommand *command)
{
    if (!command || !command->id || !command->id[0]
        || !command->label || !command->label[0]) return 0;
    if (strlen((const char *)command->id) >= GX_EDITOR_COMMAND_ID
        || strlen((const char *)command->label) >= GX_EDITOR_COMMAND_LABEL
        || (command->shortcut
            && strlen((const char *)command->shortcut)
                >= GX_EDITOR_COMMAND_SHORTCUT)) return 0;
    gX_EditorCommandSlot *slot = gX_EditorCommands_Find(command->id);
    if (!slot)
    {
        for (gX_I32 i = 0; i < GX_EDITOR_COMMAND_MAX; i++)
        {
            if (!s_commands[i].used)
            {
                slot = &s_commands[i];
                break;
            }
        }
    }
    if (!slot) return 0;
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->command = *command;
    strncpy((char *)slot->id, (const char *)command->id,
        sizeof(slot->id) - 1);
    strncpy((char *)slot->label, (const char *)command->label,
        sizeof(slot->label) - 1);
    if (command->shortcut)
        strncpy((char *)slot->shortcut, (const char *)command->shortcut,
            sizeof(slot->shortcut) - 1);
    slot->command.id = slot->id;
    slot->command.label = slot->label;
    slot->command.shortcut = slot->shortcut[0] ? slot->shortcut : NULL;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_COMMAND: register %s", slot->id);
    return 1;
}

gX_I32 gX_EditorCommands_Unregister(const gX_I8 *id)
{
    gX_EditorCommandSlot *slot = gX_EditorCommands_Find(id);
    if (!slot) return 0;
    memset(slot, 0, sizeof(*slot));
    return 1;
}

gX_I1 gX_EditorCommands_Enabled(const gX_I8 *id)
{
    gX_EditorCommandSlot *slot = gX_EditorCommands_Find(id);
    if (!slot) return 0;
    if (!slot->command.enabled) return 1;
    return slot->command.enabled(slot->command.user) ? 1 : 0;
}

gX_I1 gX_EditorCommands_Run(const gX_I8 *id)
{
    gX_EditorCommandSlot *slot = gX_EditorCommands_Find(id);
    if (!slot || !slot->command.run) return 0;
    if (slot->command.enabled && !slot->command.enabled(slot->command.user))
        return 0;
    slot->command.run(slot->command.user);
    gX_TraceLog(gX_LOG_INFO, "EDITOR_COMMAND: run %s", slot->id);
    return 1;
}

gX_I32 gX_EditorCommands_Count(gX_I0)
{
    gX_I32 count = 0;
    for (gX_I32 i = 0; i < GX_EDITOR_COMMAND_MAX; i++)
        if (s_commands[i].used) count++;
    return count;
}

const gX_EditorCommand *gX_EditorCommands_At(gX_I32 index)
{
    if (index < 0) return NULL;
    for (gX_I32 i = 0; i < GX_EDITOR_COMMAND_MAX; i++)
    {
        if (!s_commands[i].used) continue;
        if (index == 0) return &s_commands[i].command;
        index--;
    }
    return NULL;
}
