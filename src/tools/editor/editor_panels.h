/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_panels.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — bounded panel registry for editor workspaces. A panel declares
//   its identity, owner group and get/set callbacks; the host owns visibility
//   masks, menu generation, layout persistence and reset. No allocations.
#ifndef GX_TOOLS_EDITOR_PANELS_H
#define GX_TOOLS_EDITOR_PANELS_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_EDITOR_PANEL_MAX 24
#define GX_EDITOR_PANEL_ID 64
#define GX_EDITOR_PANEL_LABEL 96
#define GX_EDITOR_PANEL_GROUP 32

typedef struct gX_EditorPanel
{
    const gX_I8 *id;
    const gX_I8 *label;
    const gX_I8 *group;
    gX_I1 defaultVisible;
    gX_I1 (*get)(gX_I0 *user);
    gX_I0 (*set)(gX_I0 *user, gX_I1 visible);
    gX_I0 *user;
} gX_EditorPanel;

gX_I0 gX_EditorPanels_Clear(gX_I0);
//ai: register returns the stable slot index (bit position in the mask), or -1.
gX_I32 gX_EditorPanels_Register(const gX_EditorPanel *panel);
gX_I32 gX_EditorPanels_Count(gX_I0);
const gX_EditorPanel *gX_EditorPanels_At(gX_I32 index);
gX_I32 gX_EditorPanels_Find(const gX_I8 *id);
gX_I1 gX_EditorPanels_Visible(gX_I32 index);
gX_I0 gX_EditorPanels_SetVisible(gX_I32 index, gX_I1 visible);
gX_I0 gX_EditorPanels_Toggle(gX_I32 index);
gX_U32 gX_EditorPanels_Mask(gX_I0);
gX_I0 gX_EditorPanels_ApplyMask(gX_U32 mask);
gX_I0 gX_EditorPanels_Reset(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
