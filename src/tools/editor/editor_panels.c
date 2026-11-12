/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_panels.c]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — panel registry. Registration is dense and never unregisters, so
//   a slot index is also its bit position in a workspace mask.
#include "editor_panels.h"
#include "../../common/log.h"

#include <string.h>

typedef struct gX_EditorPanelSlot
{
    gX_I1 used;
    gX_EditorPanel panel;
    gX_I8 id[GX_EDITOR_PANEL_ID];
    gX_I8 label[GX_EDITOR_PANEL_LABEL];
    gX_I8 group[GX_EDITOR_PANEL_GROUP];
} gX_EditorPanelSlot;

static gX_EditorPanelSlot s_panels[GX_EDITOR_PANEL_MAX];

static gX_EditorPanelSlot *editor_panels_find(const gX_I8 *id)
{
    if (!id || !id[0]) return NULL;
    for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX; i++)
    {
        if (s_panels[i].used
            && strcmp((const char *)s_panels[i].id, (const char *)id) == 0)
            return &s_panels[i];
    }
    return NULL;
}

gX_I0 gX_EditorPanels_Clear(gX_I0)
{
    memset(s_panels, 0, sizeof(s_panels));
}

gX_I32 gX_EditorPanels_Register(const gX_EditorPanel *panel)
{
    if (!panel || !panel->id || !panel->id[0]
        || !panel->label || !panel->label[0]
        || !panel->group || !panel->group[0]
        || !panel->get || !panel->set) return -1;
    if (strlen((const char *)panel->id) >= GX_EDITOR_PANEL_ID
        || strlen((const char *)panel->label) >= GX_EDITOR_PANEL_LABEL
        || strlen((const char *)panel->group) >= GX_EDITOR_PANEL_GROUP)
        return -1;
    gX_EditorPanelSlot *slot = editor_panels_find(panel->id);
    if (!slot)
    {
        for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX; i++)
        {
            if (!s_panels[i].used)
            {
                slot = &s_panels[i];
                break;
            }
        }
    }
    if (!slot) return -1;
    gX_I32 index = (gX_I32)(slot - s_panels);
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->panel = *panel;
    strncpy((char *)slot->id, (const char *)panel->id, sizeof(slot->id) - 1);
    strncpy((char *)slot->label, (const char *)panel->label,
        sizeof(slot->label) - 1);
    strncpy((char *)slot->group, (const char *)panel->group,
        sizeof(slot->group) - 1);
    slot->panel.id = slot->id;
    slot->panel.label = slot->label;
    slot->panel.group = slot->group;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_PANEL: register %s", slot->id);
    return index;
}

gX_I32 gX_EditorPanels_Count(gX_I0)
{
    gX_I32 count = 0;
    for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX; i++)
        if (s_panels[i].used) count++;
    return count;
}

const gX_EditorPanel *gX_EditorPanels_At(gX_I32 index)
{
    if (index < 0 || index >= GX_EDITOR_PANEL_MAX || !s_panels[index].used)
        return NULL;
    return &s_panels[index].panel;
}

gX_I32 gX_EditorPanels_Find(const gX_I8 *id)
{
    gX_EditorPanelSlot *slot = editor_panels_find(id);
    return slot ? (gX_I32)(slot - s_panels) : -1;
}

gX_I1 gX_EditorPanels_Visible(gX_I32 index)
{
    if (index < 0 || index >= GX_EDITOR_PANEL_MAX || !s_panels[index].used)
        return 0;
    return s_panels[index].panel.get(s_panels[index].panel.user) ? 1 : 0;
}

gX_I0 gX_EditorPanels_SetVisible(gX_I32 index, gX_I1 visible)
{
    if (index < 0 || index >= GX_EDITOR_PANEL_MAX || !s_panels[index].used)
        return;
    s_panels[index].panel.set(s_panels[index].panel.user, visible ? 1 : 0);
}

gX_I0 gX_EditorPanels_Toggle(gX_I32 index)
{
    gX_EditorPanels_SetVisible(index, gX_EditorPanels_Visible(index) ? 0 : 1);
}

gX_U32 gX_EditorPanels_Mask(gX_I0)
{
    gX_U32 mask = 0;
    for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX && i < 32; i++)
    {
        if (s_panels[i].used && gX_EditorPanels_Visible(i)) mask |= 1u << i;
    }
    return mask;
}

gX_I0 gX_EditorPanels_ApplyMask(gX_U32 mask)
{
    for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX && i < 32; i++)
    {
        if (!s_panels[i].used) continue;
        gX_EditorPanels_SetVisible(i, (mask & (1u << i)) ? 1 : 0);
    }
}

gX_I0 gX_EditorPanels_Reset(gX_I0)
{
    for (gX_I32 i = 0; i < GX_EDITOR_PANEL_MAX; i++)
    {
        if (!s_panels[i].used) continue;
        gX_EditorPanels_SetVisible(i, s_panels[i].panel.defaultVisible);
    }
}
