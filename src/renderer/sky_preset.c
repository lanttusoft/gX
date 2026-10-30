/**********************************************************************************************
*
*   [gX: src/renderer/sky_preset.c]
*   Sky renderer preset — look parameters + the ephemeris clock time
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


//opencode: sky presets capture what an author controls: the look parameters
//   (density/hue/glow/contrast/altitude/turbidity/disks/stars/moon/helpers)
//   plus the ephemeris clock time — the sun/moon/planet *directions* are
//   computed from gamedata.ephemeris, so the time is what moves the sky.
//   JSON via the engine's json_impl; saves refuse shipped preset paths under
//   the harness guard (GX_NO_SETTINGS_SAVE), like GFX presets.
#include "sky.h"

#include "../common/log.h"
#include "../common/marshal/json_impl.h"
#include "../game/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gX_I0 sky_json_add_f32(struct json_object *root, const char *key, gX_F32 value) {
    json_object_object_add(root, key, json_object_new_double((double)value));
}

static gX_I0 sky_json_get_f32(struct json_object *root, const char *key, gX_F32 *out) {
    struct json_object *val = NULL;
    if (json_object_object_get_ex(root, key, &val)) {
        *out = (gX_F32)json_object_get_double(val);
    }
}

gX_I32 gX_RendererSky_Preset_Save(gX_RendererSky *sky, const gX_I8 *path) {
    if (getenv("GX_NO_SETTINGS_SAVE") && path
        && strstr((const char *)path, "resources/tools/")) {
        gX_TraceLog_RENDERER(gX_LOG_ERROR,
            "Sky: refusing to overwrite a shipped preset under the harness guard: %s", path);
        return 1;
    }

    struct json_object *root = json_object_new_object();
    if (!root) return 1;
    json_object_object_add(root, "type", json_object_new_string("sky_preset"));
    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "timeSec",
        json_object_new_double((double)gamedata.ephemeris.timeSec));

    sky_json_add_f32(root, "skyDensity",       sky->skyDensity);
    sky_json_add_f32(root, "skyHue",           sky->skyHue);
    sky_json_add_f32(root, "horizonGlow",      sky->horizonGlow);
    sky_json_add_f32(root, "skyContrast",      sky->skyContrast);
    sky_json_add_f32(root, "altitude",         sky->altitude);
    sky_json_add_f32(root, "turbidity",        sky->turbidity);
    sky_json_add_f32(root, "sunDiskSize",      sky->sunDiskSize);
    sky_json_add_f32(root, "sunDiskIntensity", sky->sunDiskIntensity);
    sky_json_add_f32(root, "starDensity",      sky->starDensity);
    sky_json_add_f32(root, "starBrightness",   sky->starBrightness);
    sky_json_add_f32(root, "starTwinkleSpeed", sky->starTwinkleSpeed);
    sky_json_add_f32(root, "moonSize",         sky->moonSize);
    sky_json_add_f32(root, "moonBrightness",   sky->moonBrightness);
    json_object_object_add(root, "showHelpers", json_object_new_int(sky->showHelpers ? 1 : 0));

    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);
    if (ret == 0) {
        gX_TraceLog_RENDERER(gX_LOG_NOTICE, "Sky: saved preset to %s", path);
        return 0;
    }
    gX_TraceLog_RENDERER(gX_LOG_ERROR, "Sky: failed to save preset to %s", path);
    return 1;
}

gX_I32 gX_RendererSky_Preset_Load(gX_RendererSky *sky, const gX_I8 *path) {
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        gX_TraceLog_RENDERER(gX_LOG_WARNING, "Sky: failed to load preset from %s", path);
        return 1;
    }

    //opencode: missing fields keep the current values (partial presets load).
    sky_json_get_f32(root, "skyDensity",       &sky->skyDensity);
    sky_json_get_f32(root, "skyHue",           &sky->skyHue);
    sky_json_get_f32(root, "horizonGlow",      &sky->horizonGlow);
    sky_json_get_f32(root, "skyContrast",      &sky->skyContrast);
    sky_json_get_f32(root, "altitude",         &sky->altitude);
    sky_json_get_f32(root, "turbidity",        &sky->turbidity);
    sky_json_get_f32(root, "sunDiskSize",      &sky->sunDiskSize);
    sky_json_get_f32(root, "sunDiskIntensity", &sky->sunDiskIntensity);
    sky_json_get_f32(root, "starDensity",      &sky->starDensity);
    sky_json_get_f32(root, "starBrightness",   &sky->starBrightness);
    sky_json_get_f32(root, "starTwinkleSpeed", &sky->starTwinkleSpeed);
    sky_json_get_f32(root, "moonSize",         &sky->moonSize);
    sky_json_get_f32(root, "moonBrightness",   &sky->moonBrightness);
    struct json_object *val = NULL;
    if (json_object_object_get_ex(root, "showHelpers", &val)) {
        sky->showHelpers = (json_object_get_int(val) != 0) ? 1 : 0;
    }

    //opencode: the clock time is part of the preset; applying it re-derives
    //   the body directions immediately so the preview matches without
    //   waiting for a tick.
    struct json_object *timeVal = NULL;
    if (json_object_object_get_ex(root, "timeSec", &timeVal)) {
        gX_F64 t = (gX_F64)json_object_get_double(timeVal);
        gamedata.ephemeris.timeSec = t;
        gX_Ephemeris_Update(&gamedata.ephemeris, t);
    }

    json_object_put(root);
    gX_TraceLog_RENDERER(gX_LOG_NOTICE, "Sky: loaded preset from %s", path);
    return 0;
}
