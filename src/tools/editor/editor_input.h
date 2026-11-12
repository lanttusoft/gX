/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_input.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — per-frame input owner. Modal chrome outranks ImGui, ImGui
//   outranks the native GUI canvas, and gameplay is the fallback. Tools set
//   owners by reason; call sites ask the capture predicates instead of
//   recombining probes.
#ifndef GX_TOOLS_EDITOR_INPUT_H
#define GX_TOOLS_EDITOR_INPUT_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gX_EditorInputOwner
{
    GX_EDITOR_INPUT_GAME = 0,
    GX_EDITOR_INPUT_NATIVE_GUI,
    GX_EDITOR_INPUT_IMGUI,
    GX_EDITOR_INPUT_MODAL
} gX_EditorInputOwner;

gX_I0 gX_EditorInput_BeginFrame(gX_I0);
gX_I0 gX_EditorInput_Set(gX_EditorInputOwner owner, const gX_I8 *reason);
gX_I0 gX_EditorInput_Update(gX_I0);
gX_EditorInputOwner gX_EditorInput_Owner(gX_I0);
const gX_I8 *gX_EditorInput_Reason(gX_I0);
gX_I1 gX_EditorInput_CapturesMouse(gX_I0);
gX_I1 gX_EditorInput_CapturesKeyboard(gX_I0);
gX_I1 gX_EditorInput_CapturesText(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
