/**********************************************************************************************
*
*   [gX: src/renderer/fractal/fractal_preset.c]
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


//opencode: fractal preset system — JSON save/load of all fractal state
#include "fractal_preset.h"
#include "../fractal.h"

#include "../../common/log.h"
#include "../../common/path.h"

#include "../../common/marshal/json_impl.h"

#include <string.h>
#include <time.h>

//opencode: helpers to pack/unpack vec2/vec3 fields as JSON arrays
static struct json_object *v2_to_json(gX_F32v2 v) {
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double((double)v.x));
    json_object_array_add(arr, json_object_new_double((double)v.y));
    return arr;
}

static struct json_object *v3_to_json(gX_F32v3 v) {
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double((double)v.x));
    json_object_array_add(arr, json_object_new_double((double)v.y));
    json_object_array_add(arr, json_object_new_double((double)v.z));
    return arr;
}

static gX_F32v2 v2_from_json(struct json_object *arr) {
    gX_F32v2 v = {0,0};
    if (arr && json_object_get_type(arr) == json_type_array) {
        int len = (int)json_object_array_length(arr);
        if (len > 0) v.x = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 0));
        if (len > 1) v.y = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 1));
    }
    return v;
}

static gX_F32v3 v3_from_json(struct json_object *arr) {
    gX_F32v3 v = {0,0,0};
    if (arr && json_object_get_type(arr) == json_type_array) {
        int len = (int)json_object_array_length(arr);
        if (len > 0) v.x = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 0));
        if (len > 1) v.y = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 1));
        if (len > 2) v.z = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, 2));
    }
    return v;
}

//opencode: helper to read an int field from a JSON object with default
static gX_I32 json_read_int(struct json_object *obj, const gX_I8 *key, gX_I32 def) {
    struct json_object *val = NULL;
    if (json_object_object_get_ex(obj, key, &val) && json_object_get_type(val) == json_type_int)
        return (gX_I32)json_object_get_int(val);
    return def;
}

static gX_F32 json_read_float(struct json_object *obj, const gX_I8 *key, gX_F32 def) {
    struct json_object *val = NULL;
    if (json_object_object_get_ex(obj, key, &val) &&
        (json_object_get_type(val) == json_type_double || json_object_get_type(val) == json_type_int))
        return (gX_F32)json_object_get_double(val);
    return def;
}

static gX_F32v2 json_read_v2(struct json_object *obj, const gX_I8 *key, gX_F32v2 def) {
    struct json_object *val = NULL;
    if (json_object_object_get_ex(obj, key, &val))
        return v2_from_json(val);
    return def;
}

static gX_F32v3 json_read_v3(struct json_object *obj, const gX_I8 *key, gX_F32v3 def) {
    struct json_object *val = NULL;
    if (json_object_object_get_ex(obj, key, &val))
        return v3_from_json(val);
    return def;
}

// ===========================================================================
// Preset Save
// ===========================================================================

gX_I32 gX_RendererFractal_Preset_Save(struct gX_RendererFractal *frac, const gX_I8 *path) {
    gX_TraceLog_RENDERER(gX_LOG_INFO, "FRACTAL: Saving preset to %s", path);

    struct json_object *root = json_object_new_object();

    // Meta
    json_object_object_add(root, "name", json_object_new_string(frac->name));
    json_object_object_add(root, "description", json_object_new_string(frac->description));
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    gX_I8 date[64], timeStr[64];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    json_object_object_add(root, "created_date", json_object_new_string(date));
    json_object_object_add(root, "created_time", json_object_new_string(timeStr));

    // gen section
    struct json_object *gen = json_object_new_object();
    json_object_object_add(gen, "iterations", json_object_new_int((int)frac->gen.iterations));
    json_object_object_add(gen, "seed", v2_to_json(frac->gen.seed));
    json_object_object_add(gen, "exponent", json_object_new_double((double)frac->gen.exponent));
    json_object_object_add(gen, "escape", json_object_new_double((double)frac->gen.escape));
    json_object_object_add(gen, "scale", json_object_new_double((double)frac->gen.scale));
    json_object_object_add(gen, "fractalType", json_object_new_int((int)frac->gen.fractalType));
    json_object_object_add(gen, "precisionMode", json_object_new_int((int)frac->gen.precisionMode));
    json_object_object_add(gen, "algorithm", json_object_new_int((int)frac->gen.algorithm));
    json_object_object_add(gen, "y_offset", v2_to_json(frac->gen.y_offset));
    json_object_object_add(gen, "paletteMode", json_object_new_int((int)frac->gen.paletteMode));
    json_object_object_add(gen, "morphSpeed", json_object_new_double((double)frac->gen.morphSpeed));
    json_object_object_add(gen, "morphPhase", json_object_new_double((double)frac->gen.morphPhase));
    json_object_object_add(root, "gen", gen);

    // cam section
    struct json_object *cam = json_object_new_object();
    json_object_object_add(cam, "pos", v2_to_json(frac->cam.pos));
    json_object_object_add(cam, "zoom", json_object_new_double((double)frac->cam.zoom));
    json_object_object_add(cam, "angle", json_object_new_double((double)frac->cam.angle));
    json_object_object_add(cam, "tilt", v2_to_json(frac->cam.tilt));
    json_object_object_add(cam, "pos_speed", v2_to_json(frac->cam.pos_speed));
    json_object_object_add(cam, "zoom_speed", json_object_new_double((double)frac->cam.zoom_speed));
    json_object_object_add(cam, "angle_speed", json_object_new_double((double)frac->cam.angle_speed));
    json_object_object_add(cam, "tilt_speed", v2_to_json(frac->cam.tilt_speed));
    json_object_object_add(root, "cam", cam);

    // pal section
    struct json_object *pal = json_object_new_object();
    json_object_object_add(pal, "palette_amount", json_object_new_int((int)frac->pal.palette_amount));
    json_object_object_add(pal, "palette_factor", v3_to_json(frac->pal.palette_factor));
    json_object_object_add(pal, "palette_speed", v3_to_json(frac->pal.palette_speed));
    json_object_object_add(pal, "palette_phase", v3_to_json(frac->pal.palette_phase));
    json_object_object_add(pal, "palette_amplitude", v3_to_json(frac->pal.palette_amplitude));
    json_object_object_add(root, "pal", pal);

    // trap section
    struct json_object *trap = json_object_new_object();
    json_object_object_add(trap, "trapMode", json_object_new_int((int)frac->trap.trapMode));
    json_object_object_add(trap, "trapPos", v2_to_json(frac->trap.trapPos));
    json_object_object_add(trap, "trapRadius", json_object_new_double((double)frac->trap.trapRadius));
    json_object_object_add(root, "trap", trap);

    // oscillators section
    struct json_object *oscs = json_object_new_array();
    for (gX_I32 i = 0; i < GX_FRACTAL_OSC_COUNT; i++) {
        struct json_object *o = json_object_new_object();
        json_object_object_add(o, "target", json_object_new_int((int)frac->oscillators[i].target));
        json_object_object_add(o, "speed", json_object_new_double((double)frac->oscillators[i].speed));
        json_object_object_add(o, "amplitude", json_object_new_double((double)frac->oscillators[i].amplitude));
        json_object_object_add(o, "phase", json_object_new_double((double)frac->oscillators[i].phase));
        json_object_object_add(o, "offset", json_object_new_double((double)frac->oscillators[i].offset));
        json_object_array_add(oscs, o);
    }
    json_object_object_add(root, "oscillators", oscs);
    
    // Write to file
    gX_I32 flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext(path, root, flags);
    json_object_put(root);

    if (ret == 0)
        gX_TraceLog_RENDERER(gX_LOG_INFO, "FRACTAL: Preset saved");
    else
        gX_TraceLog_RENDERER(gX_LOG_ERROR, "FRACTAL: Failed to save preset");
    return ret == 0 ? 0 : -1;
}

// ===========================================================================
// Preset Load
// ===========================================================================

gX_I32 gX_RendererFractal_Preset_Load(struct gX_RendererFractal *frac, const gX_I8 *path) {
    gX_TraceLog_RENDERER(gX_LOG_INFO, "FRACTAL: Loading preset from %s", path);

    struct json_object *root = json_object_from_file(path);
    if (!root) {
        gX_TraceLog_RENDERER(gX_LOG_ERROR, "FRACTAL: Failed to load preset from %s", path);
        return 1;
    }

    // gen section
    struct json_object *gen = NULL;
    if (json_object_object_get_ex(root, "gen", &gen)) {
        frac->gen.iterations     = json_read_int(gen, "iterations", frac->gen.iterations);
        frac->gen.seed           = json_read_v2(gen, "seed", frac->gen.seed);
        frac->gen.exponent       = json_read_float(gen, "exponent", frac->gen.exponent);
        frac->gen.escape         = json_read_float(gen, "escape", frac->gen.escape);
        frac->gen.scale          = json_read_float(gen, "scale", frac->gen.scale);
        frac->gen.fractalType    = json_read_int(gen, "fractalType", frac->gen.fractalType);
        frac->gen.precisionMode  = json_read_int(gen, "precisionMode", frac->gen.precisionMode);
        frac->gen.algorithm      = json_read_int(gen, "algorithm", frac->gen.algorithm);
        frac->gen.y_offset       = json_read_v2(gen, "y_offset", frac->gen.y_offset);
        frac->gen.paletteMode    = json_read_int(gen, "paletteMode", frac->gen.paletteMode);
        frac->gen.morphSpeed     = json_read_float(gen, "morphSpeed", frac->gen.morphSpeed);
        frac->gen.morphPhase     = json_read_float(gen, "morphPhase", frac->gen.morphPhase);
    }

    // cam section
    struct json_object *cam = NULL;
    if (json_object_object_get_ex(root, "cam", &cam)) {
        frac->cam.pos             = json_read_v2(cam, "pos", frac->cam.pos);
        frac->cam.zoom            = json_read_float(cam, "zoom", frac->cam.zoom);
        frac->cam.angle           = json_read_float(cam, "angle", frac->cam.angle);
        frac->cam.tilt            = json_read_v2(cam, "tilt", frac->cam.tilt);
        frac->cam.pos_speed       = json_read_v2(cam, "pos_speed", frac->cam.pos_speed);
        frac->cam.zoom_speed      = json_read_float(cam, "zoom_speed", frac->cam.zoom_speed);
        frac->cam.angle_speed     = json_read_float(cam, "angle_speed", frac->cam.angle_speed);
        frac->cam.tilt_speed      = json_read_v2(cam, "tilt_speed", frac->cam.tilt_speed);
    }

    // pal section
    struct json_object *pal = NULL;
    if (json_object_object_get_ex(root, "pal", &pal)) {
        frac->pal.palette_amount    = json_read_int(pal, "palette_amount", frac->pal.palette_amount);
        frac->pal.palette_factor    = json_read_v3(pal, "palette_factor", frac->pal.palette_factor);
        frac->pal.palette_speed     = json_read_v3(pal, "palette_speed", frac->pal.palette_speed);
        frac->pal.palette_phase     = json_read_v3(pal, "palette_phase", frac->pal.palette_phase);
        frac->pal.palette_amplitude = json_read_v3(pal, "palette_amplitude", frac->pal.palette_amplitude);
    }

    // trap section
    struct json_object *trap = NULL;
    if (json_object_object_get_ex(root, "trap", &trap)) {
        frac->trap.trapMode   = json_read_int(trap, "trapMode", frac->trap.trapMode);
        frac->trap.trapPos    = json_read_v2(trap, "trapPos", frac->trap.trapPos);
        frac->trap.trapRadius = json_read_float(trap, "trapRadius", frac->trap.trapRadius);
    }

    // oscillators section
    struct json_object *oscs = NULL;
    if (json_object_object_get_ex(root, "oscillators", &oscs) && json_object_get_type(oscs) == json_type_array) {
        gX_I32 count = (gX_I32)json_object_array_length(oscs);
        if (count > GX_FRACTAL_OSC_COUNT) count = GX_FRACTAL_OSC_COUNT;
        for (gX_I32 i = 0; i < count; i++) {
            struct json_object *o = json_object_array_get_idx(oscs, i);
            if (o) {
                frac->oscillators[i].target      = json_read_int(o, "target", -1);
                frac->oscillators[i].speed       = json_read_float(o, "speed", 0.0f);
                frac->oscillators[i].amplitude   = json_read_float(o, "amplitude", 0.0f);
                frac->oscillators[i].phase       = json_read_float(o, "phase", 0.0f);
                frac->oscillators[i].offset      = json_read_float(o, "offset", 0.0f);
                frac->oscillators[i].accumulator = 0.0f;
            }
        }
    }

    json_object_put(root);

    // Force shader reload if precision/algorithm changed
    frac->prevPrecisionMode = -1;
    frac->prevFractalType = -1;
    frac->prevAlgorithm = -1;

    gX_TraceLog_RENDERER(gX_LOG_INFO, "FRACTAL: Preset loaded");
    return 0;
}
