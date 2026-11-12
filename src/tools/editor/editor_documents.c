/**********************************************************************************************
*
*   [gX: src/tools/editor/editor_documents.c]
*
*   Copyright (c) 2025-2026 lanttu
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*
**********************************************************************************************/
//ai: GX-I043 — document/session state. Dirty transitions are traceable for
//   headless editor checks; save/load callbacks own the actual I/O.
#include "editor_documents.h"
#include "../../common/log.h"

#include <string.h>

static gX_I0 gX_EditorDocument_Copy(gX_I8 *dst, gX_U32 size,
    const gX_I8 *src)
{
    if (!dst || size == 0) return;
    if (!src) src = (const gX_I8 *)"";
    strncpy((char *)dst, (const char *)src, (size_t)size - 1);
    dst[size - 1] = 0;
}

gX_I0 gX_EditorDocument_Init(gX_EditorDocument *document,
    const gX_I8 *id, const gX_I8 *path,
    gX_I32 (*save)(gX_EditorDocument *),
    gX_I32 (*load)(gX_EditorDocument *),
    gX_I1 (*closeGuard)(gX_EditorDocument *),
    gX_I0 *context)
{
    if (!document) return;
    memset(document, 0, sizeof(*document));
    gX_EditorDocument_Copy(document->id, sizeof(document->id), id);
    gX_EditorDocument_Copy(document->path, sizeof(document->path), path);
    document->save = save;
    document->load = load;
    document->closeGuard = closeGuard;
    document->context = context;
}

gX_I0 gX_EditorDocument_SetPath(gX_EditorDocument *document, const gX_I8 *path)
{
    if (!document) return;
    gX_EditorDocument_Copy(document->path, sizeof(document->path), path);
}

const gX_I8 *gX_EditorDocument_Path(const gX_EditorDocument *document)
{
    return document ? document->path : (const gX_I8 *)"";
}

gX_I0 gX_EditorDocument_MarkDirty(gX_EditorDocument *document)
{
    if (!document || document->dirty) return;
    document->dirty = 1;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_DOCUMENT: dirty id=%s path=%s",
        document->id, document->path);
}

gX_I0 gX_EditorDocument_ClearDirty(gX_EditorDocument *document)
{
    if (!document || !document->dirty) return;
    document->dirty = 0;
    gX_TraceLog(gX_LOG_INFO, "EDITOR_DOCUMENT: clean id=%s path=%s",
        document->id, document->path);
}

gX_I1 gX_EditorDocument_Dirty(const gX_EditorDocument *document)
{
    return document && document->dirty ? 1 : 0;
}

gX_I32 gX_EditorDocument_Save(gX_EditorDocument *document)
{
    if (!document || document->readOnly || !document->save) return 0;
    gX_I32 result = document->save(document);
    if (result) gX_EditorDocument_ClearDirty(document);
    return result;
}

gX_I32 gX_EditorDocument_Load(gX_EditorDocument *document)
{
    if (!document || !document->load) return 0;
    gX_I32 result = document->load(document);
    if (result) gX_EditorDocument_ClearDirty(document);
    return result;
}

gX_I1 gX_EditorDocument_CanClose(gX_EditorDocument *document)
{
    if (!document || !document->dirty) return 1;
    if (!document->closeGuard) return 0;
    return document->closeGuard(document) ? 1 : 0;
}
