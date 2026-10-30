/**********************************************************************************************
*
*   [gX: src/audio/pad_schema.c]
*   gX — PAD preset save/load
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


//ai: PAD preset JSON — the synth_schema.c conventions: json-c, versioned
//   root, parameter fields only. Loading applies through the public setters
//   so the engine's clamps also protect a hand-edited file.
#include "pad_schema.h"
#include "pad.h"

#include <string.h>
#include <stdlib.h>
#include <json-c/json.h>

#include "../common/log.h"

//ai: shipped-preset guard — gates set GX_NO_SETTINGS_SAVE; a stray save must
//   never rewrite a resource-tree preset (the gfx/synth precedent).
static gX_I1 gX_Pad_Preset_Refused(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    if (getenv("GX_NO_SETTINGS_SAVE") && strstr((const char *)path, "resources/")) {
        gX_TraceLog(gX_LOG_ERROR,
            "PAD_PRESET: refusing to write a resource-tree preset under the harness guard: %s",
            path);
        return 1;
    }
    return 0;
}

gX_I32 gX_Pad_Preset_Save(const gX_I8 *path) {
    if (gX_Pad_Preset_Refused(path)) return 1;

    struct json_object *root = json_object_new_object();
    if (!root) {
        gX_TraceLog(gX_LOG_ERROR, "PAD_PRESET: failed to create JSON root");
        return 1;
    }

    json_object_object_add(root, "type", json_object_new_string("pad_preset"));
    json_object_object_add(root, "version", json_object_new_int(1));

    struct json_object *global = json_object_new_object();
    json_object_object_add(global, "masterVolume", json_object_new_double(g_pad.masterVolume));
    json_object_object_add(global, "width", json_object_new_double(g_pad.width));
    json_object_object_add(global, "attackTime", json_object_new_double(g_pad.defaultAttack));
    json_object_object_add(global, "decayTime", json_object_new_double(g_pad.defaultDecay));
    json_object_object_add(global, "sustainLevel", json_object_new_double(g_pad.defaultSustain));
    json_object_object_add(global, "releaseTime", json_object_new_double(g_pad.defaultRelease));
    json_object_object_add(global, "filterMode", json_object_new_int(g_pad.defaultFilterMode));
    json_object_object_add(global, "filterCutoff", json_object_new_double(g_pad.defaultFilterCutoff));
    json_object_object_add(global, "filterRes", json_object_new_double(g_pad.defaultFilterRes));
    json_object_object_add(global, "filterLfoRate", json_object_new_double(g_pad.defaultFilterLfoRate));
    json_object_object_add(global, "filterLfoDepth", json_object_new_double(g_pad.defaultFilterLfoDepth));
    json_object_object_add(root, "global", global);

    struct json_object *chorus = json_object_new_object();
    json_object_object_add(chorus, "enabled", json_object_new_int(g_pad.chorusEnabled));
    json_object_object_add(chorus, "rate", json_object_new_double(g_pad.chorusRate));
    json_object_object_add(chorus, "depth", json_object_new_double(g_pad.chorusDepth));
    json_object_object_add(chorus, "mix", json_object_new_double(g_pad.chorusMix));
    json_object_object_add(chorus, "stereoPhase", json_object_new_double(g_pad.chorusStereoPhase));
    json_object_object_add(root, "chorus", chorus);

    struct json_object *layers = json_object_new_array();
    for (gX_I32 l = 0; l < GX_PAD_LAYERS; l++) {
        const gX_Pad_Layer *layer = &g_pad.defaultLayers[l];
        struct json_object *jl = json_object_new_object();
        json_object_object_add(jl, "waveform", json_object_new_int(layer->waveform));
        json_object_object_add(jl, "detuneCents", json_object_new_double(layer->detuneCents));
        json_object_object_add(jl, "level", json_object_new_double(layer->level));
        json_object_object_add(jl, "pan", json_object_new_double(layer->pan));
        json_object_object_add(jl, "driftRate", json_object_new_double(layer->driftRate));
        json_object_object_add(jl, "driftDepth", json_object_new_double(layer->driftDepth));
        json_object_array_add(layers, jl);
    }
    json_object_object_add(root, "layers", layers);

    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);
    if (ret != 0) {
        gX_TraceLog(gX_LOG_ERROR, "PAD_PRESET: failed to save to %s", path);
        return 1;
    }
    gX_TraceLog(gX_LOG_NOTICE, "PAD_PRESET: saved to %s", path);
    return 0;
}

static gX_I32 gX_Pad_Preset_GetF32(struct json_object *parent, const gX_I8 *key,
    gX_F32 fallback, gX_F32 *out) {
    struct json_object *val = NULL;
    if (!json_object_object_get_ex(parent, key, &val)) {
        *out = fallback;
        return 1;
    }
    *out = (gX_F32)json_object_get_double(val);
    return 0;
}

static gX_I32 gX_Pad_Preset_GetI32(struct json_object *parent, const gX_I8 *key,
    gX_I32 fallback, gX_I32 *out) {
    struct json_object *val = NULL;
    if (!json_object_object_get_ex(parent, key, &val)) {
        *out = fallback;
        return 1;
    }
    *out = json_object_get_int(val);
    return 0;
}

gX_I32 gX_Pad_Preset_Load(const gX_I8 *path) {
    if (!path || !path[0]) return 1;
    struct json_object *root = json_object_from_file((const char *)path);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        gX_TraceLog(gX_LOG_ERROR, "PAD_PRESET: failed to load from %s", path);
        return 1;
    }

    struct json_object *global = NULL;
    if (json_object_object_get_ex(root, "global", &global)
        && json_object_get_type(global) == json_type_object) {
        gX_F32 volume = g_pad.masterVolume, width = g_pad.width;
        gX_F32 a = g_pad.defaultAttack, d = g_pad.defaultDecay;
        gX_F32 s = g_pad.defaultSustain, r = g_pad.defaultRelease;
        gX_F32 cutoff = g_pad.defaultFilterCutoff, res = g_pad.defaultFilterRes;
        gX_F32 lfoRate = g_pad.defaultFilterLfoRate, lfoDepth = g_pad.defaultFilterLfoDepth;
        gX_I32 filterMode = g_pad.defaultFilterMode;
        gX_Pad_Preset_GetF32(global, "masterVolume", volume, &volume);
        gX_Pad_Preset_GetF32(global, "width", width, &width);
        gX_Pad_Preset_GetF32(global, "attackTime", a, &a);
        gX_Pad_Preset_GetF32(global, "decayTime", d, &d);
        gX_Pad_Preset_GetF32(global, "sustainLevel", s, &s);
        gX_Pad_Preset_GetF32(global, "releaseTime", r, &r);
        gX_Pad_Preset_GetI32(global, "filterMode", filterMode, &filterMode);
        gX_Pad_Preset_GetF32(global, "filterCutoff", cutoff, &cutoff);
        gX_Pad_Preset_GetF32(global, "filterRes", res, &res);
        gX_Pad_Preset_GetF32(global, "filterLfoRate", lfoRate, &lfoRate);
        gX_Pad_Preset_GetF32(global, "filterLfoDepth", lfoDepth, &lfoDepth);

        gX_Pad_SetMasterVolume(volume);
        gX_Pad_SetWidth(width);
        gX_Pad_SetDefaultADSR(a, d, s, r);
        gX_Pad_SetDefaultFilter(filterMode, cutoff, res);
        gX_Pad_SetDefaultFilterLFO(lfoRate, lfoDepth);
    }

    struct json_object *chorus = NULL;
    if (json_object_object_get_ex(root, "chorus", &chorus)
        && json_object_get_type(chorus) == json_type_object) {
        gX_I32 enabled = g_pad.chorusEnabled;
        gX_F32 rate = g_pad.chorusRate, depth = g_pad.chorusDepth;
        gX_F32 mix = g_pad.chorusMix, phase = g_pad.chorusStereoPhase;
        gX_Pad_Preset_GetI32(chorus, "enabled", enabled, &enabled);
        gX_Pad_Preset_GetF32(chorus, "rate", rate, &rate);
        gX_Pad_Preset_GetF32(chorus, "depth", depth, &depth);
        gX_Pad_Preset_GetF32(chorus, "mix", mix, &mix);
        gX_Pad_Preset_GetF32(chorus, "stereoPhase", phase, &phase);
        gX_Pad_SetChorus(enabled ? 1 : 0, rate, depth, mix, phase);
    }

    struct json_object *layers = NULL;
    if (json_object_object_get_ex(root, "layers", &layers)
        && json_object_get_type(layers) == json_type_array) {
        gX_U32 count = (gX_U32)json_object_array_length(layers);
        if (count > GX_PAD_LAYERS) count = GX_PAD_LAYERS;
        for (gX_U32 i = 0; i < count; i++) {
            struct json_object *jl = json_object_array_get_idx(layers, i);
            if (!jl || json_object_get_type(jl) != json_type_object) continue;
            gX_Pad_Layer layer = g_pad.defaultLayers[i];
            gX_I32 waveform = layer.waveform;
            gX_Pad_Preset_GetI32(jl, "waveform", waveform, &waveform);
            gX_Pad_Preset_GetF32(jl, "detuneCents", layer.detuneCents, &layer.detuneCents);
            gX_Pad_Preset_GetF32(jl, "level", layer.level, &layer.level);
            gX_Pad_Preset_GetF32(jl, "pan", layer.pan, &layer.pan);
            gX_Pad_Preset_GetF32(jl, "driftRate", layer.driftRate, &layer.driftRate);
            gX_Pad_Preset_GetF32(jl, "driftDepth", layer.driftDepth, &layer.driftDepth);
            layer.waveform = (waveform == GX_PAD_WAVE_TRI) ? GX_PAD_WAVE_TRI : GX_PAD_WAVE_SAW;
            gX_Pad_SetDefaultLayer((gX_I32)i, &layer);
        }
    }

    json_object_put(root);
    gX_TraceLog(gX_LOG_NOTICE, "PAD_PRESET: loaded from %s", path);
    return 0;
}
