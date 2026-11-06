/**********************************************************************************************
*
*   [gX: src/gui/console_state.c]
*   gX — a game engine
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

//ai: gX-240 save states — cartridge id, video profile and orbit camera in one
//   small JSON slot per cartridge. The scene itself is deterministic from the
//   cartridge profile, so no object graph is stored.

#include "console_state.h"
#include "console_playback.h"
#include "gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "../main.h"
#include "../settings.h"
#include "../renderer/renderer.h"
#include "../renderer/gfx.h"
#include "../renderer/gfx/showcase.h"
#include "../common/log.h"
#include "../common/marshal/json_impl.h"
#include "../wrappers/wrappers.h"

static const gX_I8 *console_state_dir(gX_I0) {
    const gX_I8 *hook = getenv("GX_CONSOLE_SAVE_DIR");
    return (hook && hook[0]) ? hook : (const gX_I8 *)"save";
}

static gX_I1 console_state_path(gX_I32 slot, gX_I8 *out, gX_I32 size) {
    if (!out || size <= 0 || slot < 1 || slot > GX_CONSOLE_SAVE_SLOTS)
        return 0;
    snprintf((char *)out, (size_t)size, "%s/console_slot_%d.json",
        (const char *)console_state_dir(), slot);
    return 1;
}

gX_I1 gX_ConsoleState_Exists(gX_I32 slot) {
    gX_I8 path[512];
    if (!console_state_path(slot, path, sizeof(path))) return 0;
    FILE *file = fopen((const char *)path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static const gX_I8 *console_state_cartridge(gX_I0) {
    if (arguments.cartridgeY2K) return (const gX_I8 *)"y2k";
    if (maindata.masterIndex == gX_SCENE_RENDERER_RAYMARCH)
        return (const gX_I8 *)"ray-1";
    if (maindata.masterIndex == gX_SCENE_PHYSICS)
        return (const gX_I8 *)"particle";
    return NULL;
}

static gX_I32 console_state_save(gX_I32 slot) {
    gX_I8 path[512];
    if (!console_state_path(slot, path, sizeof(path))) {
        gX_TraceLog(gX_LOG_ERROR, "CONSOLE_STATE: invalid slot %d", slot);
        return -1;
    }
    const gX_I8 *guard = getenv("GX_NO_SETTINGS_SAVE");
    if (guard && guard[0] == '1') {
        gX_TraceLog(gX_LOG_ERROR,
            "CONSOLE_STATE: refusing save under the harness guard: %s",
            (const char *)path);
        return -1;
    }
    const gX_I8 *cartridge = console_state_cartridge();
    if (!cartridge) {
        gX_TraceLog(gX_LOG_ERROR,
            "CONSOLE_STATE: save requires a running cartridge");
        return -1;
    }
    const gX_I8 *folder = console_state_dir();
    if (!DirectoryExists((const char *)folder)
        && MakeDirectory((const char *)folder) != 0) {
        gX_TraceLog(gX_LOG_ERROR, "CONSOLE_STATE: cannot create %s",
            (const char *)folder);
        return -1;
    }

    struct json_object *root = json_object_new_object();
    if (!root) return -1;
    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "slot", json_object_new_int(slot));
    json_object_object_add(root, "cartridge",
        json_object_new_string((const char *)cartridge));
    {
        char date[32] = "";
        char clock[32] = "";
        time_t now = time(NULL);
        struct tm local;
        if (localtime_r(&now, &local)) {
            strftime(date, sizeof(date), "%Y-%m-%d", &local);
            strftime(clock, sizeof(clock), "%H:%M", &local);
        }
        json_object_object_add(root, "created_date",
            json_object_new_string(date));
        json_object_object_add(root, "created_time",
            json_object_new_string(clock));
    }
    {
        struct json_object *video = json_object_new_object();
        json_object_object_add(video, "width",
            json_object_new_int((gX_I32)settings.screenWidth));
        json_object_object_add(video, "height",
            json_object_new_int((gX_I32)settings.screenHeight));
        json_object_object_add(video, "filter",
            json_object_new_int(settings.presentFilter));
        json_object_object_add(root, "video", video);
    }
    {
        struct json_object *camera = json_object_new_object();
        gX_Showcase_CameraState state;
        gX_Showcase_CameraGet(&state);
        json_object_object_add(camera, "yaw",
            json_object_new_double((gX_F64)state.yaw));
        json_object_object_add(camera, "pitch",
            json_object_new_double((gX_F64)state.pitch));
        json_object_object_add(camera, "dist",
            json_object_new_double((gX_F64)state.dist));
        struct json_object *target = json_object_new_array();
        json_object_array_add(target,
            json_object_new_double((gX_F64)state.target.x));
        json_object_array_add(target,
            json_object_new_double((gX_F64)state.target.y));
        json_object_array_add(target,
            json_object_new_double((gX_F64)state.target.z));
        json_object_object_add(camera, "target", target);
        json_object_object_add(root, "camera", camera);
    }
    gX_I32 result = json_object_to_file_ext((const char *)path, root,
        JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);
    if (result != 0) {
        gX_TraceLog(gX_LOG_ERROR, "CONSOLE_STATE: save failed %s",
            (const char *)path);
        return -1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_STATE: save slot=%d cartridge=%s path=%s",
        slot, (const char *)cartridge, (const char *)path);
    return 0;
}

static gX_F64 console_state_number(struct json_object *object,
    const gX_I8 *key, gX_F64 fallback) {
    struct json_object *value = NULL;
    if (!object || !json_object_object_get_ex(object, (const char *)key,
            &value)) return fallback;
    return json_object_get_double(value);
}

static gX_I32 console_state_load(gX_I32 slot) {
    gX_I8 path[512];
    if (!console_state_path(slot, path, sizeof(path))) return -1;
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root) {
        gX_TraceLog(gX_LOG_ERROR, "CONSOLE_STATE: missing slot %d (%s)",
            slot, (const char *)path);
        return -1;
    }
    struct json_object *value = NULL;
    gX_I8 cartridge[32] = "y2k";
    if (json_object_object_get_ex(root, "cartridge", &value)) {
        const char *name = json_object_get_string(value);
        if (name && name[0])
            snprintf((char *)cartridge, sizeof(cartridge), "%s", name);
    }

    if (json_object_object_get_ex(root, "video", &value)) {
        gX_U32 width = (gX_U32)console_state_number(value, "width", 320.0);
        gX_U32 height = (gX_U32)console_state_number(value, "height", 240.0);
        gX_F32 filter = (gX_F32)console_state_number(value, "filter", 1.0);
        if (width >= 64 && width <= 4096 && height >= 64 && height <= 4096) {
            settings.screenWidth = width;
            settings.screenHeight = height;
            settings.presentFilter = filter != 0.0f ? 1 : 0;
            gX_Settings_Apply_VirtualScreen();
            settings.dirtyFlag = 1;
        }
    }

    gX_Showcase_CameraState camera = {0};
    gX_I1 hasCamera = 0;
    if (strcmp((const char *)cartridge, "y2k") == 0
        && json_object_object_get_ex(root, "camera", &value)) {
        camera.yaw = (gX_F32)console_state_number(value, "yaw", 0.0);
        camera.pitch = (gX_F32)console_state_number(value, "pitch", 0.0);
        camera.dist = (gX_F32)console_state_number(value, "dist", 9.0);
        struct json_object *target = NULL;
        if (json_object_object_get_ex(value, "target", &target)
            && json_object_array_length(target) >= 3) {
            camera.target.x = (gX_F32)json_object_get_double(
                json_object_array_get_idx(target, 0));
            camera.target.y = (gX_F32)json_object_get_double(
                json_object_array_get_idx(target, 1));
            camera.target.z = (gX_F32)json_object_get_double(
                json_object_array_get_idx(target, 2));
        }
        hasCamera = 1;
    }
    json_object_put(root);

    arguments.cartridgeY2K = 0;
    arguments.showcaseY2K = 0;
    arguments.showcase = 0;
    gX_I32 scene = gX_SCENE_TITLE;
    if (strcmp((const char *)cartridge, "y2k") == 0) {
        arguments.showcase = 1;
        arguments.showcaseY2K = 1;
        arguments.cartridgeY2K = 1;
        scene = gX_SCENE_RENDERER_GFX;
        gX_Renderer_GFX_ColdBoot();
    } else if (strcmp((const char *)cartridge, "ray-1") == 0) {
        scene = gX_SCENE_RENDERER_RAYMARCH;
    } else if (strcmp((const char *)cartridge, "particle") == 0) {
        scene = gX_SCENE_PHYSICS;
    }
    if (hasCamera) gX_Showcase_CameraSet(&camera);
    gX_MainData_Switch(scene);
    if (hasCamera)
        gX_TraceLog(gX_LOG_NOTICE,
            "CONSOLE_STATE: load slot=%d cartridge=%s camera=%.1f/%.1f/%.1f",
            slot, (const char *)cartridge, camera.yaw, camera.pitch,
            camera.dist);
    else
        gX_TraceLog(gX_LOG_NOTICE, "CONSOLE_STATE: load slot=%d cartridge=%s",
            slot, (const char *)cartridge);
    return 0;
}

//ai: all save/load callers, including hotkeys, share success and failure OSD feedback.
gX_I32 gX_ConsoleState_Save(gX_I32 slot)
{
    gX_I32 Result = console_state_save(slot);
    gX_GUI_ConsoleOSD_Notify(slot, Result == 0 ? "SAVED" : "SAVE FAILED");
    return Result;
}

gX_I32 gX_ConsoleState_Load(gX_I32 slot)
{
    gX_I32 Result = console_state_load(slot);
    //ai: loading resumes real time; playback speed never persists across slots.
    gX_ConsolePlayback_Reset();
    gX_GUI_ConsoleOSD_Notify(slot, Result == 0 ? "LOADED" : "LOAD FAILED");
    return Result;
}

gX_I0 gX_ConsoleState_Describe(gX_I32 slot, gX_I8 *out, gX_I32 size) {
    if (!out || size <= 0) return;
    gX_I8 path[512];
    out[0] = '\0';
    if (!console_state_path(slot, path, sizeof(path))) return;
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root) return;
    struct json_object *value = NULL;
    const char *cartridge = "?";
    const char *timeText = "";
    if (json_object_object_get_ex(root, "cartridge", &value))
        cartridge = json_object_get_string(value);
    if (json_object_object_get_ex(root, "created_time", &value))
        timeText = json_object_get_string(value);
    const char *label = strcmp((const char *)cartridge, "y2k") == 0 ? "Y2K CGI"
        : (strcmp((const char *)cartridge, "ray-1") == 0 ? "RAY-1"
        : (strcmp((const char *)cartridge, "particle") == 0 ? "SANDBOX" : cartridge));
    snprintf((char *)out, (size_t)size, "%s  %s", label, timeText);
    json_object_put(root);
}
