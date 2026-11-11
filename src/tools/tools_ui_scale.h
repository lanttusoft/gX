/**********************************************************************************************
*
*   [gX: src/tools/tools_ui_scale.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*   This file is part of gX. See LICENSE.
*
**********************************************************************************************/

//ai: GUI-M4 — floating tool panels must widen with the ImGui text scale. Their
//   fixed default widths clip labels and controls at 175% even though the font
//   and spacing scale. Height stays content-scrollable.
#ifndef GX_TOOLS_UI_SCALE_H
#define GX_TOOLS_UI_SCALE_H

#include "imgui/imgui.h"
#include "../common/types.h"

static inline gX_F32 gX_Tools_UI_Scale(void)
{
    gX_F32 scale = ImGui::GetStyle().FontScaleMain;
    return scale > 1.0f ? scale : 1.0f;
}

static inline gX_F32 gX_Tools_UI_ScaleWidth(gX_F32 base)
{
    gX_F32 width = base * gX_Tools_UI_Scale();
    gX_F32 maxWidth = ImGui::GetIO().DisplaySize.x - 32.0f;
    if (maxWidth > 100.0f && width > maxWidth) width = maxWidth;
    return width;
}

#endif
