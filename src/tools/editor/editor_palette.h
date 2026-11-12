/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_palette.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — Ctrl+P command palette built on the command registry and the
//   shared picker. Commands are contributed by the host and active tools.
#ifndef GX_TOOLS_EDITOR_PALETTE_H
#define GX_TOOLS_EDITOR_PALETTE_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

gX_I0 gX_EditorPalette_Open(gX_I0);
gX_I0 gX_EditorPalette_OpenGlobal(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
