/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_picker.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — shared ImGui picker for presets, assets, themes and files.
//   Acceptance is a callback; the picker never writes or loads by itself.
#ifndef GX_TOOLS_EDITOR_PICKER_H
#define GX_TOOLS_EDITOR_PICKER_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_EDITOR_PICKER_MAX_ITEMS 256
#define GX_EDITOR_PICKER_PATH 256
#define GX_EDITOR_PICKER_LABEL 128
#define GX_EDITOR_PICKER_ID 64
#define GX_EDITOR_PICKER_TITLE 128
#define GX_EDITOR_PICKER_HINT 128
#define GX_EDITOR_PICKER_SEARCH 128

typedef struct gX_EditorPickerItem
{
    const gX_I8 *path;
    const gX_I8 *label;
} gX_EditorPickerItem;

typedef struct gX_EditorPickerRequest
{
    const gX_I8 *id;
    const gX_I8 *title;
    const gX_I8 *searchHint;
    const gX_EditorPickerItem *items;
    gX_I32 count;
    const gX_I8 *selected;
    gX_I0 (*accept)(const gX_I8 *path, gX_I0 *user);
    gX_I0 *user;
} gX_EditorPickerRequest;

gX_I0 gX_EditorPicker_Open(const gX_EditorPickerRequest *request);
gX_I0 gX_EditorPicker_Draw(gX_I0);
gX_I1 gX_EditorPicker_IsOpen(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
