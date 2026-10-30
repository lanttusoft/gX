/**********************************************************************************************
*
*   [gX: src/audio/opx_schema.c]
*   gX — OPX preset save/load
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


//ai: OPX preset JSON — the synth_schema.c conventions: json-c, versioned
//   root, parameter fields only (runtime phase/envelope/feedback history is
//   never persisted). Loading applies through the public setters, so every
//   range clamp the engine enforces also protects a hand-edited file.
#include "opx_schema.h"
#include "opx.h"

#include <string.h>
#include <stdlib.h>
#include <json-c/json.h>

#include "../common/log.h"

//ai: shipped-preset guard — gates set GX_NO_SETTINGS_SAVE; a stray save must
//   never rewrite a resource-tree preset (the gfx/synth precedent).
static gX_I1 gX_OPX_Preset_Refused(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    if (getenv("GX_NO_SETTINGS_SAVE") && strstr((const char *)path, "resources/")) {
        gX_TraceLog(gX_LOG_ERROR,
            "OPX_PRESET: refusing to write a resource-tree preset under the harness guard: %s",
            path);
        return 1;
    }
    return 0;
}

gX_I32 gX_OPX_Preset_Save(const gX_I8 *path) {
    if (gX_OPX_Preset_Refused(path)) return 1;

    struct json_object *root = json_object_new_object();
    if (!root) {
        gX_TraceLog(gX_LOG_ERROR, "OPX_PRESET: failed to create JSON root");
        return 1;
    }

    json_object_object_add(root, "type", json_object_new_string("opx_preset"));
    json_object_object_add(root, "version", json_object_new_int(1));

    struct json_object *global = json_object_new_object();
    json_object_object_add(global, "masterVolume",
        json_object_new_double(g_opx.masterVolume));
    json_object_object_add(global, "defaultAlgorithm",
        json_object_new_int(g_opx.defaultAlgorithm));
    json_object_object_add(root, "global", global);

    struct json_object *operators = json_object_new_array();
    for (gX_I32 op = 0; op < GX_OPX_OPERATORS; op++) {
        const gX_OPXOperator *o = &g_opx.defaults[op];
        struct json_object *jo = json_object_new_object();
        json_object_object_add(jo, "enabled", json_object_new_int(o->enabled));
        json_object_object_add(jo, "ratio", json_object_new_double(o->ratio));
        json_object_object_add(jo, "level", json_object_new_double(o->level));
        json_object_object_add(jo, "detuneCents", json_object_new_double(o->detuneCents));
        json_object_object_add(jo, "feedback", json_object_new_double(o->feedback));
        json_object_object_add(jo, "attackTime", json_object_new_double(o->attackTime));
        json_object_object_add(jo, "decayTime", json_object_new_double(o->decayTime));
        json_object_object_add(jo, "sustainLevel", json_object_new_double(o->sustainLevel));
        json_object_object_add(jo, "releaseTime", json_object_new_double(o->releaseTime));
        json_object_array_add(operators, jo);
    }
    json_object_object_add(root, "operators", operators);

    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);
    if (ret != 0) {
        gX_TraceLog(gX_LOG_ERROR, "OPX_PRESET: failed to save to %s", path);
        return 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "OPX_PRESET: saved to %s", path);
    return 0;
}

static gX_I32 gX_OPX_Preset_GetF32(struct json_object *parent, const gX_I8 *key,
    gX_F32 fallback, gX_F32 *out) {
    struct json_object *val = NULL;
    if (!json_object_object_get_ex(parent, key, &val)) {
        *out = fallback;
        return 1;
    }
    *out = (gX_F32)json_object_get_double(val);
    return 0;
}

static gX_I32 gX_OPX_Preset_GetI32(struct json_object *parent, const gX_I8 *key,
    gX_I32 fallback, gX_I32 *out) {
    struct json_object *val = NULL;
    if (!json_object_object_get_ex(parent, key, &val)) {
        *out = fallback;
        return 1;
    }
    *out = json_object_get_int(val);
    return 0;
}

gX_I32 gX_OPX_Preset_Load(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        gX_TraceLog(gX_LOG_ERROR, "OPX_PRESET: failed to load from %s", path);
        return 1;
    }

    struct json_object *global = NULL;
    if (json_object_object_get_ex(root, "global", &global)
        && json_object_get_type(global) == json_type_object) {
        gX_F32 volume = g_opx.masterVolume;
        gX_I32 algorithm = g_opx.defaultAlgorithm;
        gX_OPX_Preset_GetF32(global, "masterVolume", volume, &volume);
        gX_OPX_Preset_GetI32(global, "defaultAlgorithm", algorithm, &algorithm);
        gX_OPX_SetMasterVolume(volume);
        gX_OPX_SetDefaultAlgorithm(algorithm);
    }

    struct json_object *operators = NULL;
    if (json_object_object_get_ex(root, "operators", &operators)
        && json_object_get_type(operators) == json_type_array) {
        gX_U32 count = (gX_U32)json_object_array_length(operators);
        if (count > GX_OPX_OPERATORS) count = GX_OPX_OPERATORS;
        for (gX_U32 i = 0; i < count; i++) {
            struct json_object *jo = json_object_array_get_idx(operators, i);
            if (!jo || json_object_get_type(jo) != json_type_object) continue;
            gX_OPXOperator current = g_opx.defaults[i];
            gX_F32 ratio = current.ratio, level = current.level;
            gX_F32 detune = current.detuneCents, feedback = current.feedback;
            gX_F32 a = current.attackTime, d = current.decayTime;
            gX_F32 s = current.sustainLevel, r = current.releaseTime;
            gX_I32 enabled = current.enabled;
            gX_OPX_Preset_GetF32(jo, "ratio", ratio, &ratio);
            gX_OPX_Preset_GetF32(jo, "level", level, &level);
            gX_OPX_Preset_GetF32(jo, "detuneCents", detune, &detune);
            gX_OPX_Preset_GetF32(jo, "feedback", feedback, &feedback);
            gX_OPX_Preset_GetF32(jo, "attackTime", a, &a);
            gX_OPX_Preset_GetF32(jo, "decayTime", d, &d);
            gX_OPX_Preset_GetF32(jo, "sustainLevel", s, &s);
            gX_OPX_Preset_GetF32(jo, "releaseTime", r, &r);
            gX_OPX_Preset_GetI32(jo, "enabled", enabled, &enabled);

            gX_OPX_SetDefaultOperator((gX_I32)i, ratio, level);
            gX_OPX_SetDefaultOperatorEnabled((gX_I32)i, enabled ? 1 : 0);
            gX_OPX_SetDefaultOperatorDetune((gX_I32)i, detune);
            gX_OPX_SetDefaultOperatorFeedback((gX_I32)i, feedback);
            gX_OPX_SetDefaultOperatorADSR((gX_I32)i, a, d, s, r);
        }
    }

    json_object_put(root);
    gX_TraceLog(gX_LOG_NOTICE, "OPX_PRESET: loaded from %s", path);
    return 0;
}
