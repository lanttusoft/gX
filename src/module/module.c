/**********************************************************************************************
*
*   [gX: src/module/module.c]
*   Dynamic module host — Route A loader/registry
*
*   Copyright (c) 2026 lanttu, opencode
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


//opencode: dynamic modules — see docs/dynamic_modules.md. Route A: the exe
//   links with -Wl,--dynamic-list=modules/module_exports.txt and modules keep
//   undefined engine symbols; this file owns the dlopen/entry/handshake flow
//   and the kernel-side hook registry the engine calls unconditionally.
#include "module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/data.h"
#include "../common/log.h"
#include "../wrappers/file.h"
#include "../main.h"

#define GX_MODULE_MAX 8

typedef struct gX_Module_Slot {
    gX_I1 used;
    gX_I8 path[256];
    void *handle;

    gX_Module_FrameFn update;
    gX_Module_FrameFn draw;
    gX_Module_FrameFn sceneExit;
    gX_Module_FrameFn sceneEnter;
    gX_Module_ShutdownFn shutdown;
    gX_Module_ExitCodeFn exitCode;
    gX_Module_MarkupClickFn markupClick;
    gX_Module_EnsureScriptFn markupEnsure;
    gX_Module_DrawFn toolsDraw;
    gX_Module_HasFn overlayOpen;
    gX_Module_DrawFn overlayDraw;
} gX_Module_Slot;

static gX_Module_Slot s_slots[GX_MODULE_MAX];
static gX_U32 s_slotCount = 0;
static gX_I32 s_loadSlot = -1;

//=============================================================================
// registration (called by the module through the host table)
//=============================================================================

#define GX_MODULE_SLOT() \
    ((s_loadSlot >= 0 && s_loadSlot < GX_MODULE_MAX) ? &s_slots[s_loadSlot] : NULL)

static gX_I32 gX_Module_RegisterUpdate(gX_Module_FrameFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->update = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterDraw(gX_Module_FrameFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->draw = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterSceneExit(gX_Module_FrameFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->sceneExit = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterSceneEnter(gX_Module_FrameFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->sceneEnter = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterShutdown(gX_Module_ShutdownFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->shutdown = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterExitCode(gX_Module_ExitCodeFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->exitCode = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterMarkupClick(gX_Module_MarkupClickFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->markupClick = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterMarkupEnsure(gX_Module_EnsureScriptFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->markupEnsure = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterToolsDraw(gX_Module_DrawFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->toolsDraw = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterOverlayOpen(gX_Module_HasFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->overlayOpen = fn;
    return 0;
}

static gX_I32 gX_Module_RegisterOverlayDraw(gX_Module_DrawFn fn) {
    gX_Module_Slot *slot = GX_MODULE_SLOT();
    if (!slot || !fn) return 1;
    slot->overlayDraw = fn;
    return 0;
}

//=============================================================================
// loader
//=============================================================================

gX_I32 gX_Module_Load(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    if (s_slotCount >= GX_MODULE_MAX) {
        gX_TraceLog(gX_LOG_WARNING, "MODULE: slot table full, '%s' skipped", path);
        return 1;
    }

    //opencode: handles are never dlclose'd (static constructors/TLS make
    //   reload unsafe) — a failed load also keeps its handle.
    void *handle = dlopen((const char *)path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        gX_TraceLog(gX_LOG_ERROR, "MODULE: dlopen '%s' failed: %s", path, dlerror());
        return 1;
    }

    gX_Module_EntryFn entry = (gX_Module_EntryFn)dlsym(handle, "gX_Module_Entry");
    if (!entry) {
        gX_TraceLog(gX_LOG_ERROR, "MODULE: '%s' has no gX_Module_Entry", path);
        return 1;
    }

    //opencode: both directions of the handshake — the module declares its ABI
    //   major (optional symbol), the host passes version + struct size in the
    //   table and the entry refuses mismatch itself.
    gX_U32 (*moduleAbi)(gX_I0) = (gX_U32 (*)(gX_I0))dlsym(handle, "gX_Module_ABI");
    if (moduleAbi && moduleAbi() != GX_MODULE_ABI_VERSION) {
        gX_TraceLog(gX_LOG_ERROR, "MODULE: '%s' ABI %u != host %u",
            path, moduleAbi(), (gX_U32)GX_MODULE_ABI_VERSION);
        return 1;
    }

    gX_Module_Host host;
    memset(&host, 0, sizeof(host));
    host.abiVersion = GX_MODULE_ABI_VERSION;
    host.structSize = (gX_U32)sizeof(gX_Module_Host);
    host.engineName = "gX";
    host.engineVersion = gX_Data_String_gX_version;
    host.RegisterUpdate = gX_Module_RegisterUpdate;
    host.RegisterDraw = gX_Module_RegisterDraw;
    host.RegisterSceneExit = gX_Module_RegisterSceneExit;
    host.RegisterSceneEnter = gX_Module_RegisterSceneEnter;
    host.RegisterShutdown = gX_Module_RegisterShutdown;
    host.RegisterExitCode = gX_Module_RegisterExitCode;
    host.RegisterMarkupClick = gX_Module_RegisterMarkupClick;
    host.RegisterMarkupEnsure = gX_Module_RegisterMarkupEnsure;
    host.RegisterToolsDraw = gX_Module_RegisterToolsDraw;
    host.RegisterOverlayOpen = gX_Module_RegisterOverlayOpen;
    host.RegisterOverlayDraw = gX_Module_RegisterOverlayDraw;

    memset(&s_slots[s_slotCount], 0, sizeof(gX_Module_Slot));
    s_slots[s_slotCount].handle = handle;
    snprintf(s_slots[s_slotCount].path, sizeof(s_slots[s_slotCount].path), "%s", path);

    s_loadSlot = (gX_I32)s_slotCount;
    gX_I32 errors = entry(&host);
    s_loadSlot = -1;

    if (errors != 0) {
        gX_TraceLog(gX_LOG_ERROR, "MODULE: '%s' entry failed (%i)", path, errors);
        return errors;
    }

    s_slots[s_slotCount].used = 1;
    s_slotCount++;
    gX_TraceLog(gX_LOG_NOTICE, "MODULE: loaded '%s'", path);
    return 0;
}

//opencode: scan modules/*.so in sorted order (the scripting scan pattern).
static gX_I32 gX_Module_LoadDirectory(const gX_I8 *dir) {
    gX_File_Directory listing;
    if (gX_File_Directory_List(dir, &listing) <= 0) return 0;

    for (gX_I32 i = 1; i < listing.count; i++) {
        gX_TextLine256 key;
        memcpy(key, listing.names[i], sizeof(gX_TextLine256));
        gX_I32 j = i - 1;
        while (j >= 0 && strcmp(listing.names[j], key) > 0) {
            memcpy(listing.names[j + 1], listing.names[j], sizeof(gX_TextLine256));
            j--;
        }
        memcpy(listing.names[j + 1], key, sizeof(gX_TextLine256));
    }

    gX_I32 loaded = 0;
    for (gX_I32 i = 0; i < listing.count; i++) {
        const gX_I8 *name = listing.names[i];
        if (!name[0] || name[0] == '.') continue;
        const gX_I8 *dot = strrchr(name, '.');
        if (!dot || strcmp(dot, ".so") != 0) continue;

        gX_I8 fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, name);
        if (gX_Module_Load(fullPath) == 0) loaded++;
        //opencode: an unloadable auto-discovered module warns (loader logs)
        //   but does not fail the boot; explicit paths do.
    }
    return loaded;
}

gX_I32 gX_Module_LoadConfigured(gX_I0) {
    gX_I32 errors = 0;
    gX_I1 explicit = 0;

    if (arguments.hasModule && arguments.modulePath[0]) {
        explicit = 1;
        errors += gX_Module_Load(arguments.modulePath) != 0 ? 1 : 0;
    }
    else {
        const gX_I8 *env = getenv("GX_MODULE");
        if (env && env[0]) {
            explicit = 1;
            gX_I8 list[1024];
            snprintf(list, sizeof(list), "%s", env);
            gX_I8 *cursor = list;
            while (cursor && cursor[0]) {
                gX_I8 *sep = strchr(cursor, ':');
                if (sep) *sep = '\0';
                errors += gX_Module_Load(cursor) != 0 ? 1 : 0;
                cursor = sep ? sep + 1 : NULL;
            }
        }
    }

    if (!explicit) {
        gX_I32 loaded = gX_Module_LoadDirectory("modules");
        if (loaded > 0) {
            gX_TraceLog(gX_LOG_NOTICE, "MODULE: %i module(s) auto-loaded from modules/",
                loaded);
        }
    }
    return errors;
}

//=============================================================================
// frame/scene/lifecycle iterators (called unconditionally by the engine)
//=============================================================================

gX_I0 gX_Module_Update(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].update) s_slots[i].update();
    }
}

gX_I0 gX_Module_Draw(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].draw) s_slots[i].draw();
    }
}

gX_I0 gX_Module_SceneExit(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].sceneExit) s_slots[i].sceneExit();
    }
}

gX_I0 gX_Module_SceneEnter(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].sceneEnter) s_slots[i].sceneEnter();
    }
}

gX_I0 gX_Module_PreUnload(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].shutdown) s_slots[i].shutdown();
    }
}

gX_I0 gX_Module_Unload(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        s_slots[i].used = 0;
    }
}

gX_I32 gX_Module_ExitCode(gX_I0) {
    gX_I32 code = 0;
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].exitCode) {
            gX_I32 c = s_slots[i].exitCode();
            if (c > code) code = c;
        }
    }
    return code;
}

gX_I1 gX_Module_AnyLoaded(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used) return 1;
    }
    return 0;
}

//=============================================================================
// kernel-side interop
//=============================================================================

static gX_I1 gX_Module_HasToolsDraw(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].toolsDraw) return 1;
    }
    return 0;
}

gX_I0 gX_Module_MarkupClick(const gX_I8 *scriptPath, const gX_I8 *funcName) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].markupClick) {
            s_slots[i].markupClick(scriptPath, funcName);
            return;
        }
    }
    gX_TraceLog(gX_LOG_WARNING,
        "MARKUP: onclick '%s' — no module loaded (see docs/dynamic_modules.md)",
        funcName ? funcName : "?");
}

gX_I0 gX_Module_MarkupEnsure(const gX_I8 *scriptPath) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].markupEnsure) {
            s_slots[i].markupEnsure(scriptPath);
            return;
        }
    }
}

gX_I0 gX_Module_ToolsDraw(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].toolsDraw) {
            s_slots[i].toolsDraw();
            return;
        }
    }
}

gX_I1 gX_Module_OverlayOpen(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].overlayOpen) {
            if (s_slots[i].overlayOpen()) return 1;
        }
    }
    return 0;
}

gX_I0 gX_Module_OverlayDraw(gX_I0) {
    for (gX_U32 i = 0; i < s_slotCount; i++) {
        if (s_slots[i].used && s_slots[i].overlayDraw) {
            s_slots[i].overlayDraw();
        }
    }
}

//opencode: used by the tools hint path — true when a module provides the
//   scripting debugger draw (so the opener does not log "no module").
gX_I1 gX_Module_ToolsAvailable(gX_I0) {
    return gX_Module_HasToolsDraw();
}
