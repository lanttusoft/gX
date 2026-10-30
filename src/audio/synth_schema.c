/**********************************************************************************************
*
*   [gX: src/audio/synth_schema.c]
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


//opencode: synth engine preset save/load — full state (global + patches + sequencer)
#include "synth_schema.h"
#include "synth.h"

#include <string.h>
#include <time.h>
#include <json-c/json.h>

#include "../common/log.h"
#include "../common/path.h"
#include "../gxmath/gxmath.h"

// ===========================================================================
// Helpers
// ===========================================================================

static void json_add_f32(struct json_object *obj, const gX_I8 *key, gX_F32 val) {
    json_object_object_add(obj, key, json_object_new_double(val));
}

static void json_add_i32(struct json_object *obj, const gX_I8 *key, gX_I32 val) {
    json_object_object_add(obj, key, json_object_new_int(val));
}

static gX_I32 json_get_f32(struct json_object *parent, const gX_I8 *key, gX_F32 *out) {
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    *out = (gX_F32)json_object_get_double(jVal);
    return 0;
}

static gX_I32 json_get_i32(struct json_object *parent, const gX_I8 *key, gX_I32 *out) {
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    *out = json_object_get_int(jVal);
    return 0;
}

static void patch_to_json(struct json_object *obj, gX_Synth_Patch *p) {
    json_object_object_add(obj, "name",         json_object_new_string(p->name));
    json_add_i32(obj, "waveform",     p->waveform);
    json_add_f32(obj, "attackTime",   p->attackTime);
    json_add_f32(obj, "decayTime",    p->decayTime);
    json_add_f32(obj, "sustainLevel", p->sustainLevel);
    json_add_f32(obj, "releaseTime",  p->releaseTime);
    json_add_i32(obj, "filterMode",   p->filterMode);
    json_add_f32(obj, "filterCutoff", p->filterCutoff);
    json_add_f32(obj, "filterRes",    p->filterRes);
    json_add_i32(obj, "lfoTarget",    p->lfoTarget);
    json_add_f32(obj, "lfoFreq",      p->lfoFreq);
    json_add_f32(obj, "lfoDepth",     p->lfoDepth);
    json_add_i32(obj, "lfoWaveform",  p->lfoWaveform);
    json_add_f32(obj, "pulseWidth",   p->pulseWidth);
    json_add_f32(obj, "portamentoTime", p->portamentoTime);
    json_add_i32(obj, "fmAlgorithm",  p->fm.algorithm);
    json_add_f32(obj, "fmOp1Ratio",   p->fm.op1Ratio);
    json_add_f32(obj, "fmOp1Level",   p->fm.op1Level);
    json_add_f32(obj, "fmOp2Ratio",   p->fm.op2Ratio);
    json_add_f32(obj, "fmOp2Level",   p->fm.op2Level);
    json_add_f32(obj, "fmFeedback",   p->fm.feedback);
}

static void json_to_patch(struct json_object *obj, gX_Synth_Patch *p) {
    struct json_object *val;

    if (json_object_object_get_ex(obj, "name", &val))
        strncpy(p->name, json_object_get_string(val), GX_SYNTH_PATCH_NAME_MAX - 1);
    json_get_i32(obj, "waveform",     &p->waveform);
    json_get_f32(obj, "attackTime",   &p->attackTime);
    json_get_f32(obj, "decayTime",    &p->decayTime);
    json_get_f32(obj, "sustainLevel", &p->sustainLevel);
    json_get_f32(obj, "releaseTime",  &p->releaseTime);
    json_get_i32(obj, "filterMode",   &p->filterMode);
    json_get_f32(obj, "filterCutoff", &p->filterCutoff);
    json_get_f32(obj, "filterRes",    &p->filterRes);
    json_get_i32(obj, "lfoTarget",    &p->lfoTarget);
    json_get_f32(obj, "lfoFreq",      &p->lfoFreq);
    json_get_f32(obj, "lfoDepth",     &p->lfoDepth);
    json_get_i32(obj, "lfoWaveform",  &p->lfoWaveform);
    json_get_f32(obj, "pulseWidth",   &p->pulseWidth);
    json_get_f32(obj, "portamentoTime", &p->portamentoTime);
    json_get_i32(obj, "fmAlgorithm",  &p->fm.algorithm);
    json_get_f32(obj, "fmOp1Ratio",   &p->fm.op1Ratio);
    json_get_f32(obj, "fmOp1Level",   &p->fm.op1Level);
    json_get_f32(obj, "fmOp2Ratio",   &p->fm.op2Ratio);
    json_get_f32(obj, "fmOp2Level",   &p->fm.op2Level);
    json_get_f32(obj, "fmFeedback",   &p->fm.feedback);
}

// ===========================================================================
// Save
// ===========================================================================

gX_I32 gX_Synth_Preset_Save(const gX_I8 *path) {
    struct json_object *root = json_object_new_object();
    if (!root) { gX_TraceLog(gX_LOG_ERROR, "SYNTH_PRESET: failed to create JSON root"); return 1; }

    // Meta
    json_object_object_add(root, "type", json_object_new_string("synth_preset"));
    json_object_object_add(root, "version", json_object_new_int(1));

    // Global state
    struct json_object *global = json_object_new_object();
    json_add_f32(global, "masterVolume",          g_synth.masterVolume);
    json_add_f32(global, "defaultAttackTime",     g_synth.defaultAttackTime);
    json_add_f32(global, "defaultDecayTime",      g_synth.defaultDecayTime);
    json_add_f32(global, "defaultSustainLevel",   g_synth.defaultSustainLevel);
    json_add_f32(global, "defaultReleaseTime",    g_synth.defaultReleaseTime);
    json_add_i32(global, "defaultFilterMode",     g_synth.defaultFilterMode);
    json_add_f32(global, "defaultFilterCutoff",   g_synth.defaultFilterCutoff);
    json_add_f32(global, "defaultFilterRes",      g_synth.defaultFilterRes);
    json_add_f32(global, "defaultPortamentoTime", g_synth.defaultPortamentoTime);
    //ai: oscillator/LFO/FM defaults complete the global state — the original
    //   schema omitted them, so a round-trip silently dropped the sound.
    json_add_i32(global, "defaultWaveform",       g_synth.defaultWaveform);
    json_add_f32(global, "defaultPulseWidth",     g_synth.defaultPulseWidth);
    json_add_i32(global, "defaultWavetableIndex", g_synth.defaultWavetableIndex);
    json_add_f32(global, "defaultLfoFreq",        g_synth.defaultLfoFreq);
    json_add_f32(global, "defaultLfoDepth",       g_synth.defaultLfoDepth);
    json_add_i32(global, "defaultLfoWaveform",    g_synth.defaultLfoWaveform);
    json_add_i32(global, "defaultLfoTarget",      g_synth.defaultLfoTarget);
    json_add_i32(global, "fmAlgorithm",           g_synth.defaultFM.algorithm);
    json_add_f32(global, "fmOp1Ratio",            g_synth.defaultFM.op1Ratio);
    json_add_f32(global, "fmOp1Level",            g_synth.defaultFM.op1Level);
    json_add_f32(global, "fmOp2Ratio",            g_synth.defaultFM.op2Ratio);
    json_add_f32(global, "fmOp2Level",            g_synth.defaultFM.op2Level);
    json_add_f32(global, "fmFeedback",            g_synth.defaultFM.feedback);

    // Delay
    struct json_object *delay = json_object_new_object();
    json_add_i32(delay, "enabled",      g_synth.delay.enabled);
    json_add_f32(delay, "delayTime",    g_synth.delay.delayTime);
    json_add_f32(delay, "feedback",     g_synth.delay.feedback);
    json_add_f32(delay, "mix",          g_synth.delay.mix);
    json_add_f32(delay, "stereoSpread", g_synth.delay.stereoSpread);
    json_object_object_add(global, "delay", delay);

    // Arpeggiator
    struct json_object *arp = json_object_new_object();
    json_add_i32(arp, "mode", g_synth.arp.mode);
    json_add_f32(arp, "rate", g_synth.arp.rate);
    json_add_f32(arp, "gate", g_synth.arp.gate);
    json_object_object_add(global, "arp", arp);

    json_object_object_add(root, "global", global);

    // Patch bank
    struct json_object *patches = json_object_new_array();
    for (gX_U32 i = 0; i < GX_SYNTH_MAX_VOICES; i++) {
        struct json_object *jp = json_object_new_object();
        patch_to_json(jp, &g_synth.patchBank[i]);
        json_object_array_add(patches, jp);
    }
    json_object_object_add(root, "patchBank", patches);

    // Sequencer patterns
    struct json_object *seq = json_object_new_object();
    json_add_f32(seq, "bpm",          g_synth.seq.bpm);
    json_add_i32(seq, "stepsPerBeat", g_synth.seq.stepsPerBeat);
    json_add_i32(seq, "currentPattern", g_synth.seq.currentPattern);

    struct json_object *patterns = json_object_new_array();
    for (gX_U32 pat = 0; pat < GX_SYNTH_SEQ_MAX_PATTERNS; pat++) {
        struct json_object *jp = json_object_new_object();
        json_add_i32(jp, "length", g_synth.seq.patternLength[pat]);
        struct json_object *steps = json_object_new_array();
        for (gX_U32 s = 0; s < GX_SYNTH_SEQ_STEPS; s++) {
            struct json_object *js = json_object_new_object();
            json_add_f32(js, "freq",     g_synth.seq.patterns[pat][s].freq);
            json_add_f32(js, "velocity", g_synth.seq.patterns[pat][s].velocity);
            json_add_f32(js, "gate",     g_synth.seq.patterns[pat][s].gate);
            json_object_array_add(steps, js);
        }
        json_object_object_add(jp, "steps", steps);
        json_object_array_add(patterns, jp);
    }
    json_object_object_add(seq, "patterns", patterns);
    json_object_object_add(root, "sequencer", seq);

    // Write file
    gX_I32 ret = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY_TAB);
    json_object_put(root);

    if (ret == 0) {
        gX_TraceLog(gX_LOG_NOTICE, "SYNTH_PRESET: saved to %s", path);
        return 0;
    }
    gX_TraceLog(gX_LOG_ERROR, "SYNTH_PRESET: failed to save to %s", path);
    return 1;
}

// ===========================================================================
// Load
// ===========================================================================

gX_I32 gX_Synth_Preset_Load(const gX_I8 *path) {
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        gX_TraceLog(gX_LOG_ERROR, "SYNTH_PRESET: failed to load from %s", path);
        return 1;
    }

    // Global state
    struct json_object *global = NULL;
    if (json_object_object_get_ex(root, "global", &global)) {
        json_get_f32(global, "masterVolume",          &g_synth.masterVolume);
        json_get_f32(global, "defaultAttackTime",     &g_synth.defaultAttackTime);
        json_get_f32(global, "defaultDecayTime",      &g_synth.defaultDecayTime);
        json_get_f32(global, "defaultSustainLevel",   &g_synth.defaultSustainLevel);
        json_get_f32(global, "defaultReleaseTime",    &g_synth.defaultReleaseTime);
        json_get_i32(global, "defaultFilterMode",     &g_synth.defaultFilterMode);
        json_get_f32(global, "defaultFilterCutoff",   &g_synth.defaultFilterCutoff);
        json_get_f32(global, "defaultFilterRes",      &g_synth.defaultFilterRes);
        json_get_f32(global, "defaultPortamentoTime", &g_synth.defaultPortamentoTime);
        json_get_i32(global, "defaultWaveform",       &g_synth.defaultWaveform);
        json_get_f32(global, "defaultPulseWidth",     &g_synth.defaultPulseWidth);
        json_get_i32(global, "defaultWavetableIndex", &g_synth.defaultWavetableIndex);
        json_get_f32(global, "defaultLfoFreq",        &g_synth.defaultLfoFreq);
        json_get_f32(global, "defaultLfoDepth",       &g_synth.defaultLfoDepth);
        json_get_i32(global, "defaultLfoWaveform",    &g_synth.defaultLfoWaveform);
        json_get_i32(global, "defaultLfoTarget",      &g_synth.defaultLfoTarget);
        json_get_i32(global, "fmAlgorithm",           &g_synth.defaultFM.algorithm);
        json_get_f32(global, "fmOp1Ratio",            &g_synth.defaultFM.op1Ratio);
        json_get_f32(global, "fmOp1Level",            &g_synth.defaultFM.op1Level);
        json_get_f32(global, "fmOp2Ratio",            &g_synth.defaultFM.op2Ratio);
        json_get_f32(global, "fmOp2Level",            &g_synth.defaultFM.op2Level);
        json_get_f32(global, "fmFeedback",            &g_synth.defaultFM.feedback);

        struct json_object *delay = NULL;
        if (json_object_object_get_ex(global, "delay", &delay)) {
            gX_I32 de; if (!json_get_i32(delay, "enabled", &de)) g_synth.delay.enabled = de;
            json_get_f32(delay, "delayTime",    &g_synth.delay.delayTime);
            json_get_f32(delay, "feedback",     &g_synth.delay.feedback);
            json_get_f32(delay, "mix",          &g_synth.delay.mix);
            json_get_f32(delay, "stereoSpread", &g_synth.delay.stereoSpread);
        }

        struct json_object *arp = NULL;
        if (json_object_object_get_ex(global, "arp", &arp)) {
            json_get_i32(arp, "mode", &g_synth.arp.mode);
            json_get_f32(arp, "rate", &g_synth.arp.rate);
            json_get_f32(arp, "gate", &g_synth.arp.gate);
            g_synth.arp.enabled = (g_synth.arp.mode > 0) ? 1 : 0;
        }
    }

    // Patch bank
    struct json_object *patches = NULL;
    if (json_object_object_get_ex(root, "patchBank", &patches)) {
        gX_U32 count = (gX_U32)json_object_array_length(patches);
        if (count > GX_SYNTH_MAX_VOICES) count = GX_SYNTH_MAX_VOICES;
        for (gX_U32 i = 0; i < count; i++) {
            struct json_object *jp = json_object_array_get_idx(patches, i);
            if (jp) {
                gX_Synth_Patch_Init(&g_synth.patchBank[i]);
                json_to_patch(jp, &g_synth.patchBank[i]);
            }
        }
        g_synth.patchBankCount = count;
    }

    // Sequencer
    struct json_object *seq = NULL;
    if (json_object_object_get_ex(root, "sequencer", &seq)) {
        json_get_f32(seq, "bpm",          &g_synth.seq.bpm);
        json_get_i32(seq, "stepsPerBeat", &g_synth.seq.stepsPerBeat);
        json_get_i32(seq, "currentPattern", &g_synth.seq.currentPattern);

        struct json_object *patterns = NULL;
        if (json_object_object_get_ex(seq, "patterns", &patterns)) {
            gX_U32 pcount = (gX_U32)json_object_array_length(patterns);
            if (pcount > GX_SYNTH_SEQ_MAX_PATTERNS) pcount = GX_SYNTH_SEQ_MAX_PATTERNS;
            for (gX_U32 pat = 0; pat < pcount; pat++) {
                struct json_object *jp = json_object_array_get_idx(patterns, pat);
                if (!jp) continue;
                json_get_i32(jp, "length", &g_synth.seq.patternLength[pat]);
                struct json_object *steps = NULL;
                if (json_object_object_get_ex(jp, "steps", &steps)) {
                    gX_U32 scount = (gX_U32)json_object_array_length(steps);
                    if (scount > GX_SYNTH_SEQ_STEPS) scount = GX_SYNTH_SEQ_STEPS;
                    for (gX_U32 s = 0; s < scount; s++) {
                        struct json_object *js = json_object_array_get_idx(steps, s);
                        if (!js) continue;
                        json_get_f32(js, "freq",     &g_synth.seq.patterns[pat][s].freq);
                        json_get_f32(js, "velocity", &g_synth.seq.patterns[pat][s].velocity);
                        json_get_f32(js, "gate",     &g_synth.seq.patterns[pat][s].gate);
                    }
                }
            }
        }
    }

    json_object_put(root);
    gX_TraceLog(gX_LOG_NOTICE, "SYNTH_PRESET: loaded from %s", path);
    return 0;
}
