/**********************************************************************************************
*
*   [gX: src/renderer/ws2d.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: world-space 2D module — managed render textures for GFX model materials
#include "ws2d.h"

#include <string.h>

#include "../common/log.h"
#include "../common/macros.h"
#include "../main.h"
#include "../renderer/renderer.h"
#include "../wrappers/wrappers.h"

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------
gX_I32 gX_RendererWS2D_Load(gX_RendererWS2D *ws2d) {
    gX_TRACELOG_PROLOGUE("RendererWS2D_Load","RENDERER_WS2D: Loading")
    strcpy(ws2d->name, "[gX] WS2D");
    strcpy(ws2d->description, "World-space 2D render texture manager");
    ws2d->loadingstate = (gX_LoadingState){0};
    ws2d->loadingstate.loadAsap = true;
    ws2d->entryCount = 0;
    for (gX_U32 i = 0; i < GX_WS2D_MAX_ENTRIES; i++) {
        ws2d->entries[i].active = 0;
    }
    gX_TRACELOG_EPILOGUE("RENDERER_WS2D: Load done")
    return 0;
}

gX_I32 gX_RendererWS2D_Unload(gX_RendererWS2D *ws2d) {
    gX_TRACELOG_PROLOGUE("RendererWS2D_Unload","RENDERER_WS2D: Unloading")
    for (gX_U32 i = 0; i < ws2d->entryCount; i++) {
        if (ws2d->entries[i].active && ws2d->entries[i].rt.id > 0) {
            gX_RenderTexture_Unload(ws2d->entries[i].rt);
            ws2d->entries[i].active = 0;
        }
    }
    ws2d->entryCount = 0;
    ws2d->loadingstate.loaded = false;
    gX_TRACELOG_EPILOGUE("RENDERER_WS2D: Unload done")
    return 0;
}

gX_I0 gX_RendererWS2D_Update(gX_RendererWS2D *ws2d) {
    // No deferred loading needed for WS2D — RTs are created on demand
    (void)ws2d;
}

gX_I0 gX_RendererWS2D_DrawTo(gX_RendererWS2D *ws2d, gX_RenderTexture *outputRT) {
    // WS2D doesn't draw to canvas — its textures are used by GFX model materials
    (void)ws2d;
    (void)outputRT;
}

// ---------------------------------------------------------------------------
// Internal: find a free entry slot
// ---------------------------------------------------------------------------
static gX_I32 ws2d_find_free(gX_RendererWS2D *ws2d) {
    for (gX_U32 i = 0; i < GX_WS2D_MAX_ENTRIES; i++) {
        if (!ws2d->entries[i].active) return (gX_I32)i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
gX_I32 gX_WS2D_Create(const gX_I8 *name, gX_U16 w, gX_U16 h) {
    if (!name || w == 0 || h == 0) return -1;
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d) return -1;

    gX_I32 slot = ws2d_find_free(ws2d);
    if (slot < 0) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING, "WS2D: no free slots (max %i)", GX_WS2D_MAX_ENTRIES);
        return -1;
    }

    gX_WS2D_Entry *entry = &ws2d->entries[slot];
    strncpy(entry->name, name, GX_WS2D_NAME_MAX - 1);
    entry->name[GX_WS2D_NAME_MAX - 1] = '\0';
    entry->width = w;
    entry->height = h;

    // Create the render texture
    entry->rt = gX_RenderTexture_Load(w, h);
    if (entry->rt.id == 0) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING, "WS2D: failed to create RT '%s' (%ix%i)", name, w, h);
        entry->active = 0;
        return -1;
    }

    entry->active = 1;
    if ((gX_U32)slot + 1 > ws2d->entryCount) {
        ws2d->entryCount = (gX_U32)slot + 1;
    }

    gX_TraceLog_RENDERER(gX_LOG_INFO, "WS2D: created '%s' (%ix%i) handle=%i", name, w, h, slot);
    return slot;
}

gX_I0 gX_WS2D_Destroy(gX_I32 handle) {
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d) return;
    if (handle < 0 || (gX_U32)handle >= GX_WS2D_MAX_ENTRIES) return;
    gX_WS2D_Entry *entry = &ws2d->entries[handle];
    if (!entry->active) return;

    if (entry->rt.id > 0) {
        gX_RenderTexture_Unload(entry->rt);
    }
    entry->active = 0;
    entry->rt = (gX_RenderTexture){0};
    gX_TraceLog_RENDERER(gX_LOG_INFO, "WS2D: destroyed handle=%i '%s'", handle, entry->name);
}

gX_Texture gX_WS2D_GetTexture(gX_I32 handle) {
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d) return (gX_Texture){0};
    if (handle < 0 || (gX_U32)handle >= GX_WS2D_MAX_ENTRIES) return (gX_Texture){0};
    if (!ws2d->entries[handle].active) return (gX_Texture){0};
    return ws2d->entries[handle].rt.texture;
}

gX_I0 gX_WS2D_Begin(gX_I32 handle) {
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d) return;
    if (handle < 0 || (gX_U32)handle >= GX_WS2D_MAX_ENTRIES) return;
    if (!ws2d->entries[handle].active) return;

    gX_Drawing_BeginTexture(ws2d->entries[handle].rt);
}

gX_I0 gX_WS2D_End(gX_I32 handle) {
    (void)handle;
    gX_Drawing_EndTexture();
}

gX_I0 gX_WS2D_Clear(gX_I32 handle, gX_Color color) {
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d) return;
    if (handle < 0 || (gX_U32)handle >= GX_WS2D_MAX_ENTRIES) return;
    if (!ws2d->entries[handle].active) return;

    gX_Drawing_BeginTexture(ws2d->entries[handle].rt);
        gX_Drawing_ClearBG(color);
    gX_Drawing_EndTexture();
}

gX_I32 gX_WS2D_Find(const gX_I8 *name) {
    gX_RendererWS2D *ws2d = (renderer.ws2d.amount > 0 && renderer.ws2d.data)
        ? (gX_RendererWS2D*)renderer.ws2d.data : NULL;
    if (!ws2d || !name) return -1;
    for (gX_U32 i = 0; i < ws2d->entryCount; i++) {
        if (ws2d->entries[i].active && strcmp(ws2d->entries[i].name, name) == 0) {
            return (gX_I32)i;
        }
    }
    return -1;
}
