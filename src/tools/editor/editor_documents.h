/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_documents.h]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — one active document per tool: path, dirty/read-only state,
//   save/load callbacks and an optional close guard. The caller owns storage.
#ifndef GX_TOOLS_EDITOR_DOCUMENTS_H
#define GX_TOOLS_EDITOR_DOCUMENTS_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_EDITOR_DOCUMENT_ID 64
#define GX_EDITOR_DOCUMENT_PATH 512

typedef struct gX_EditorDocument
{
    gX_I8 id[GX_EDITOR_DOCUMENT_ID];
    gX_I8 path[GX_EDITOR_DOCUMENT_PATH];
    gX_I1 dirty;
    gX_I1 readOnly;
    gX_I0 *context;
    gX_I32 (*save)(struct gX_EditorDocument *document);
    gX_I32 (*load)(struct gX_EditorDocument *document);
    gX_I1 (*closeGuard)(struct gX_EditorDocument *document);
} gX_EditorDocument;

gX_I0 gX_EditorDocument_Init(gX_EditorDocument *document,
    const gX_I8 *id, const gX_I8 *path,
    gX_I32 (*save)(gX_EditorDocument *),
    gX_I32 (*load)(gX_EditorDocument *),
    gX_I1 (*closeGuard)(gX_EditorDocument *),
    gX_I0 *context);
gX_I0 gX_EditorDocument_SetPath(gX_EditorDocument *document, const gX_I8 *path);
const gX_I8 *gX_EditorDocument_Path(const gX_EditorDocument *document);
gX_I0 gX_EditorDocument_MarkDirty(gX_EditorDocument *document);
gX_I0 gX_EditorDocument_ClearDirty(gX_EditorDocument *document);
gX_I1 gX_EditorDocument_Dirty(const gX_EditorDocument *document);
gX_I32 gX_EditorDocument_Save(gX_EditorDocument *document);
gX_I32 gX_EditorDocument_Load(gX_EditorDocument *document);
gX_I1 gX_EditorDocument_CanClose(gX_EditorDocument *document);

#ifdef __cplusplus
}
#endif

#endif
