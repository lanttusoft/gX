/**********************************************************************************************
*
*   [gX: src/renderer/background/schema.c]
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


//opencode: offset-based schema for Background renderer (new system, coexists with old schema.c)
// Flat fields (state booleans) use the new offset-based schema system.
// Param-heavy sections (transform, special, palette, misc) use json-c bridge functions
// because the param system (8 oscillators × 9 sub-fields per param, 146+ params total)
// is too complex for the flat GX_SCHEMA_OBJECT approach.

#include "schema.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "../../common/schema.h"
#include "../../common/marshal/schema_json.h"
#include "../../common/log.h"
#include "../../common/macros.h"

//opencode: json-c types for manual param bridge serialization
#include "../../common/marshal/json_impl.h"

#include "../background.h"
#include "param.h"
#include "struct.h"

// ===========================================================================
// Helper: write a gX_F32 / gX_F32v2/v3/v4 as JSON value
// ===========================================================================

static void json_add_f32(struct json_object *obj, const gX_I8 *key, gX_F32 val)
{
    json_object_object_add(obj, key, json_object_new_double(val));
}

static void json_add_v2(struct json_object *obj, const gX_I8 *key, gX_F32v2 val)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(val.x));
    json_object_array_add(arr, json_object_new_double(val.y));
    json_object_object_add(obj, key, arr);
}

static void json_add_v3(struct json_object *obj, const gX_I8 *key, gX_F32v3 val)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(val.x));
    json_object_array_add(arr, json_object_new_double(val.y));
    json_object_array_add(arr, json_object_new_double(val.z));
    json_object_object_add(obj, key, arr);
}

static void json_add_v4(struct json_object *obj, const gX_I8 *key, gX_F32v4 val)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(val.x));
    json_object_array_add(arr, json_object_new_double(val.y));
    json_object_array_add(arr, json_object_new_double(val.z));
    json_object_array_add(arr, json_object_new_double(val.w));
    json_object_object_add(obj, key, arr);
}

// ===========================================================================
// Helper: read a gX_F32 / gX_F32v2/v3/v4 from JSON value (returns 0 on success)
// ===========================================================================

static gX_I32 json_get_f32(struct json_object *parent, const gX_I8 *key, gX_F32 *out)
{
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    *out = (gX_F32)json_object_get_double(jVal);
    return 0;
}

static gX_I32 json_get_v2(struct json_object *parent, const gX_I8 *key, gX_F32v2 *out)
{
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    if (json_object_array_length(jVal) < 2) return 1;
    out->x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
    out->y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
    return 0;
}

static gX_I32 json_get_v3(struct json_object *parent, const gX_I8 *key, gX_F32v3 *out)
{
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    if (json_object_array_length(jVal) < 3) return 1;
    out->x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
    out->y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
    out->z = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 2));
    return 0;
}

static gX_I32 json_get_v4(struct json_object *parent, const gX_I8 *key, gX_F32v4 *out)
{
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    if (json_object_array_length(jVal) < 4) return 1;
    out->x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
    out->y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
    out->z = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 2));
    out->w = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 3));
    return 0;
}

// ===========================================================================
// Param write/read helpers (generated via macros for all 4 type variants)
// Each writes/reads: { constant, defaultvalue, velocity, acceleration,
//                      oscillators: { osc[0]: {amp,freq,pha}, osc[1]: {amp,freq,pha} } }
// ===========================================================================

#define DEFINE_PARAM_WRITE(ptype, json_add_fn) \
static void write_##ptype##_param(struct json_object *parent, const gX_I8 *key, \
                                   const gX_RenBG_Param##ptype *p) \
{ \
    struct json_object *obj = json_object_new_object(); \
    json_add_fn(obj, "constant", p->constant); \
    json_add_fn(obj, "defaultvalue", p->defaultvalue); \
    json_add_fn(obj, "velocity", p->velocity); \
    json_add_fn(obj, "acceleration", p->acceleration); \
    { \
        struct json_object *oscs = json_object_new_object(); \
        { \
            struct json_object *osc = json_object_new_object(); \
            json_add_fn(osc, "amplitude", p->amplitude[0]); \
            json_add_fn(osc, "frequency", p->frequency[0]); \
            json_add_fn(osc, "phase", p->phase[0]); \
            json_object_object_add(oscs, "osc[0]", osc); \
        } \
        { \
            struct json_object *osc = json_object_new_object(); \
            json_add_fn(osc, "amplitude", p->amplitude[1]); \
            json_add_fn(osc, "frequency", p->frequency[1]); \
            json_add_fn(osc, "phase", p->phase[1]); \
            json_object_object_add(oscs, "osc[1]", osc); \
        } \
        json_object_object_add(obj, "oscillators", oscs); \
    } \
    json_object_object_add(parent, key, obj); \
}

DEFINE_PARAM_WRITE(Scalar, json_add_f32)
DEFINE_PARAM_WRITE(V2,     json_add_v2)
DEFINE_PARAM_WRITE(V3,     json_add_v3)
DEFINE_PARAM_WRITE(V4,     json_add_v4)

#define DEFINE_PARAM_READ(ptype, json_get_fn) \
static void read_##ptype##_param(struct json_object *parent, const gX_I8 *key, \
                                  gX_RenBG_Param##ptype *p) \
{ \
    struct json_object *obj = NULL; \
    if (!json_object_object_get_ex(parent, key, &obj)) return; \
    json_get_fn(obj, "constant", &p->constant); \
    json_get_fn(obj, "defaultvalue", &p->defaultvalue); \
    json_get_fn(obj, "velocity", &p->velocity); \
    json_get_fn(obj, "acceleration", &p->acceleration); \
    { \
        struct json_object *oscs = NULL; \
        if (json_object_object_get_ex(obj, "oscillators", &oscs)) \
        { \
            { \
                struct json_object *osc = NULL; \
                if (json_object_object_get_ex(oscs, "osc[0]", &osc)) \
                { \
                    json_get_fn(osc, "amplitude", &p->amplitude[0]); \
                    json_get_fn(osc, "frequency", &p->frequency[0]); \
                    json_get_fn(osc, "phase", &p->phase[0]); \
                } \
            } \
            { \
                struct json_object *osc = NULL; \
                if (json_object_object_get_ex(oscs, "osc[1]", &osc)) \
                { \
                    json_get_fn(osc, "amplitude", &p->amplitude[1]); \
                    json_get_fn(osc, "frequency", &p->frequency[1]); \
                    json_get_fn(osc, "phase", &p->phase[1]); \
                } \
            } \
        } \
    } \
}

DEFINE_PARAM_READ(Scalar, json_get_f32)
DEFINE_PARAM_READ(V2,     json_get_v2)
DEFINE_PARAM_READ(V3,     json_get_v3)
DEFINE_PARAM_READ(V4,     json_get_v4)

// ===========================================================================
// Section bridge: transform (7 params: 2×Scalar + 5×V2 + 1×V4)
// ===========================================================================

//opencode: helper to write gX_Matrix as JSON array of 16 floats
static gX_I0 json_add_matrix(struct json_object *parent, const gX_I8 *key, const gX_Matrix *m)
{
    struct json_object *arr = json_object_new_array();
    const gX_F32 *f = (const gX_F32*)m;
    for (gX_I32 i = 0; i < 16; i++)
        json_object_array_add(arr, json_object_new_double(f[i]));
    json_object_object_add(parent, key, arr);
}

//opencode: helper to read gX_Matrix from JSON array of 16 floats
static gX_I32 json_get_matrix(struct json_object *parent, const gX_I8 *key, gX_Matrix *out)
{
    struct json_object *arr = NULL;
    if (!json_object_object_get_ex(parent, key, &arr)) return 1;
    if (json_object_array_length(arr) < 16) return 1;
    gX_F32 *f = (gX_F32*)out;
    for (gX_I32 i = 0; i < 16; i++)
        f[i] = (gX_F32)json_object_get_double(json_object_array_get_idx(arr, i));
    return 0;
}

static gX_I0 bg_write_transform(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "transform", section);

    write_Scalar_param(section, "scale",        &bg->transform.scale);
    write_V2_param(section,    "scaleOrigin",   &bg->transform.scaleOrigin);
    write_Scalar_param(section, "rotate",        &bg->transform.rotate);
    write_V2_param(section,    "rotateOrigin",  &bg->transform.rotateOrigin);
    write_V4_param(section,    "stretch",       &bg->transform.stretch);
    write_V2_param(section,    "perspective",   &bg->transform.perspective);
    write_V2_param(section,    "translate",     &bg->transform.translate);
    write_V2_param(section,    "affine",        &bg->transform.affine);
    //opencode: matrix blend and direct matrices
    write_Scalar_param(section, "matrixBlend",  &bg->transform.matrixBlend);
    json_add_matrix(section, "matrixA", &bg->transform.matrixA);
    json_add_matrix(section, "matrixB", &bg->transform.matrixB);
}

static gX_I0 bg_read_transform(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "transform", &section)) return;

    read_Scalar_param(section, "scale",        &bg->transform.scale);
    read_V2_param(section,    "scaleOrigin",   &bg->transform.scaleOrigin);
    read_Scalar_param(section, "rotate",        &bg->transform.rotate);
    read_V2_param(section,    "rotateOrigin",  &bg->transform.rotateOrigin);
    read_V4_param(section,    "stretch",       &bg->transform.stretch);
    read_V2_param(section,    "perspective",   &bg->transform.perspective);
    read_V2_param(section,    "translate",     &bg->transform.translate);
    read_V2_param(section,    "affine",        &bg->transform.affine);
    //opencode: matrix blend and direct matrices
    read_Scalar_param(section, "matrixBlend",  &bg->transform.matrixBlend);
    json_get_matrix(section, "matrixA", &bg->transform.matrixA);
    json_get_matrix(section, "matrixB", &bg->transform.matrixB);
}

// ===========================================================================
// Sinusoid bridge helpers (each sinusoid has 12 params: 10×V4 + 1×Scalar + 1×V2)
// ===========================================================================

static gX_I0 bg_write_sinusoid(struct json_object *parent, const gX_RenBG_Sinusoid *sin)
{
    write_V4_param(parent,     "amplitude",            &sin->amplitude);
    write_V4_param(parent,     "frequency",            &sin->frequency);
    write_V4_param(parent,     "phase",                &sin->phase);
    write_V4_param(parent,     "speed",                &sin->speed);
    write_V4_param(parent,     "regularwave",          &sin->regularwave);
    write_V4_param(parent,     "interlaced",           &sin->interlaced);
    write_V4_param(parent,     "interlacedMultiplier", &sin->interlacedMultiplier);
    write_V4_param(parent,     "dcOffset",             &sin->dcOffset);
    write_V4_param(parent,     "linear",               &sin->linear);
    write_V4_param(parent,     "quadratic",            &sin->quadratic);
    write_Scalar_param(parent, "exponent",             &sin->exponent);
    write_Scalar_param(parent, "attenuation",          &sin->attenuation);
    write_V2_param(parent,     "center",               &sin->center);
}

//opencode: HDMA channel write/read
static gX_I0 bg_write_hdma_channel(struct json_object *parent, const gX_RenBG_HDMA_Channel *ch)
{
    write_Scalar_param(parent, "amplitude", &ch->amplitude);
    write_Scalar_param(parent, "frequency", &ch->frequency);
    write_Scalar_param(parent, "speed",     &ch->speed);
    write_Scalar_param(parent, "phase",     &ch->phase);
    write_V2_param(parent,     "offset",    &ch->offset);
}

static gX_I0 bg_read_hdma_channel(struct json_object *parent, gX_RenBG_HDMA_Channel *ch)
{
    read_Scalar_param(parent, "amplitude", &ch->amplitude);
    read_Scalar_param(parent, "frequency", &ch->frequency);
    read_Scalar_param(parent, "speed",     &ch->speed);
    read_Scalar_param(parent, "phase",     &ch->phase);
    read_V2_param(parent,     "offset",    &ch->offset);
}

static gX_I0 bg_read_sinusoid(struct json_object *parent, gX_RenBG_Sinusoid *sin)
{
    read_V4_param(parent,     "amplitude",            &sin->amplitude);
    read_V4_param(parent,     "frequency",            &sin->frequency);
    read_V4_param(parent,     "phase",                &sin->phase);
    read_V4_param(parent,     "speed",                &sin->speed);
    read_V4_param(parent,     "regularwave",          &sin->regularwave);
    read_V4_param(parent,     "interlaced",           &sin->interlaced);
    read_V4_param(parent,     "interlacedMultiplier", &sin->interlacedMultiplier);
    read_V4_param(parent,     "dcOffset",             &sin->dcOffset);
    read_V4_param(parent,     "linear",               &sin->linear);
    read_V4_param(parent,     "quadratic",            &sin->quadratic);
    read_Scalar_param(parent, "exponent",             &sin->exponent);
    read_Scalar_param(parent, "attenuation",          &sin->attenuation);
    read_V2_param(parent,     "center",               &sin->center);
}

// ===========================================================================
// Section bridge: special (9 params + 8-element sinusoid array)
// ===========================================================================

static gX_I0 bg_write_special(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "special", section);

    write_V3_param(section,    "warp",                  &bg->special.warp);
    write_Scalar_param(section, "warpExponent",          &bg->special.warpExponent);
    write_V4_param(section,    "waveAmplitude",          &bg->special.waveAmplitude);
    write_V4_param(section,    "waveFrequency",          &bg->special.waveFrequency);
    write_V4_param(section,    "waveRegularwave",        &bg->special.waveRegularwave);
    write_V4_param(section,    "waveInterlaced",         &bg->special.waveInterlaced);
    write_V4_param(section,    "waveInterlacedMultiplier",&bg->special.waveInterlacedMultiplier);
    write_V4_param(section,    "waveSpeed",              &bg->special.waveSpeed);
    write_V4_param(section,    "wavePhase",              &bg->special.wavePhase);

    // Sinusoids as a JSON array of 8 objects
    struct json_object *sinArr = json_object_new_array();
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++)
    {
        struct json_object *sinObj = json_object_new_object();
        bg_write_sinusoid(sinObj, &bg->special.sinusoids[i]);
        json_object_array_add(sinArr, sinObj);
    }
    json_object_object_add(section, "sinusoids", sinArr);

    //opencode: procedural noise params
    json_object_object_add(section, "noiseMode", json_object_new_int(bg->special.noiseMode));
    json_object_object_add(section, "noiseOctaves", json_object_new_int(bg->special.noiseOctaves));
    write_Scalar_param(section, "noiseScale", &bg->special.noiseScale);
    write_Scalar_param(section, "noiseSpeed", &bg->special.noiseSpeed);
    write_Scalar_param(section, "noiseLacunarity", &bg->special.noiseLacunarity);
    write_Scalar_param(section, "noiseGain", &bg->special.noiseGain);

    // HDMA channels as a JSON array of gX_BG_HDMA_CHANNEL_COUNT objects
    struct json_object *hdmaArr = json_object_new_array();
    for (gX_U32 i = 0; i < gX_BG_HDMA_CHANNEL_COUNT; i++)
    {
        struct json_object *hdmaObj = json_object_new_object();
        bg_write_hdma_channel(hdmaObj, &bg->special.hdma[i]);
        json_object_array_add(hdmaArr, hdmaObj);
    }
    json_object_object_add(section, "hdma", hdmaArr);
}

static gX_I0 bg_read_special(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "special", &section)) return;

    read_V3_param(section,    "warp",                  &bg->special.warp);
    read_Scalar_param(section, "warpExponent",          &bg->special.warpExponent);
    read_V4_param(section,    "waveAmplitude",          &bg->special.waveAmplitude);
    read_V4_param(section,    "waveFrequency",          &bg->special.waveFrequency);
    read_V4_param(section,    "waveRegularwave",        &bg->special.waveRegularwave);
    read_V4_param(section,    "waveInterlaced",         &bg->special.waveInterlaced);
    read_V4_param(section,    "waveInterlacedMultiplier",&bg->special.waveInterlacedMultiplier);
    read_V4_param(section,    "waveSpeed",              &bg->special.waveSpeed);
    read_V4_param(section,    "wavePhase",              &bg->special.wavePhase);

    //opencode: procedural noise params
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "noiseMode", &jVal))
            bg->special.noiseMode = json_object_get_int(jVal);
    }
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "noiseOctaves", &jVal))
            bg->special.noiseOctaves = json_object_get_int(jVal);
    }
    read_Scalar_param(section, "noiseScale", &bg->special.noiseScale);
    read_Scalar_param(section, "noiseSpeed", &bg->special.noiseSpeed);
    read_Scalar_param(section, "noiseLacunarity", &bg->special.noiseLacunarity);
    read_Scalar_param(section, "noiseGain", &bg->special.noiseGain);

    // Sinusoids array
    // HDMA channels
    struct json_object *hdmaArr = NULL;
    if (json_object_object_get_ex(section, "hdma", &hdmaArr))
    {
        gX_U32 count = (gX_U32)json_object_array_length(hdmaArr);
        if (count > gX_BG_HDMA_CHANNEL_COUNT) count = gX_BG_HDMA_CHANNEL_COUNT;
        for (gX_U32 i = 0; i < count; i++)
        {
            struct json_object *hdmaObj = json_object_array_get_idx(hdmaArr, i);
            if (hdmaObj)
                bg_read_hdma_channel(hdmaObj, &bg->special.hdma[i]);
        }
    }

    struct json_object *sinArr = NULL;
    if (json_object_object_get_ex(section, "sinusoids", &sinArr))
    {
        gX_U32 count = (gX_U32)json_object_array_length(sinArr);
        if (count > gX_BG_SINUSOID_COUNT)
            count = gX_BG_SINUSOID_COUNT;
        for (gX_U32 i = 0; i < count; i++)
        {
            struct json_object *sinObj = json_object_array_get_idx(sinArr, i);
            if (sinObj)
                bg_read_sinusoid(sinObj, &bg->special.sinusoids[i]);
        }
    }
}

// ===========================================================================
// Section bridge: palette (mode + 21 params + 16-element colors array)
// ===========================================================================

static gX_I0 bg_write_palette(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "palette", section);

    // Flat field: mode, rowCount, blendMode, colorMathMode
    json_object_object_add(section, "mode", json_object_new_int(bg->palette.mode));
    json_object_object_add(section, "rowCount", json_object_new_int(bg->palette.rowCount));
    json_object_object_add(section, "blendMode", json_object_new_int(bg->palette.blendMode));
    json_object_object_add(section, "colorMathMode", json_object_new_int(bg->palette.colorMathMode));

    //opencode: color math mask as vec3 array
    {
        struct json_object *cmArr = json_object_new_array();
        json_object_array_add(cmArr, json_object_new_double(bg->palette.colorMathMask.x));
        json_object_array_add(cmArr, json_object_new_double(bg->palette.colorMathMask.y));
        json_object_array_add(cmArr, json_object_new_double(bg->palette.colorMathMask.z));
        json_object_object_add(section, "colorMathMask", cmArr);
    }

    write_Scalar_param(section, "size",       &bg->palette.size);
    write_Scalar_param(section, "amount",     &bg->palette.amount);
    write_Scalar_param(section, "speed",      &bg->palette.speed);
    write_Scalar_param(section, "frequency",  &bg->palette.frequency);

    // Colors as a JSON array of gX_BG_PALETTE_COLORS_MAX paramV4 objects
    struct json_object *colArr = json_object_new_array();
    for (gX_U32 i = 0; i < gX_BG_PALETTE_COLORS_MAX; i++)
    {
        struct json_object *colObj = json_object_new_object();
        write_V4_param(colObj, "color", &bg->palette.colors[i]);
        json_object_array_add(colArr, colObj);
    }
    json_object_object_add(section, "colors", colArr);

    write_V4_param(section,    "color1",     &bg->palette.color1);
    write_V4_param(section,    "color2",     &bg->palette.color2);
    write_Scalar_param(section, "bgBoundary", &bg->palette.bgBoundary);
    write_V4_param(section,    "bgColor",    &bg->palette.bgColor);
    write_Scalar_param(section, "fgBoundary", &bg->palette.fgBoundary);
    write_V4_param(section,    "fgColor",    &bg->palette.fgColor);
}

static gX_I0 bg_read_palette(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "palette", &section)) return;

    // Flat field: mode, rowCount, blendMode, colorMathMode
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "mode", &jVal))
            bg->palette.mode = json_object_get_int(jVal);
    }
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "rowCount", &jVal))
            bg->palette.rowCount = json_object_get_int(jVal);
    }
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "blendMode", &jVal))
            bg->palette.blendMode = json_object_get_int(jVal);
    }
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "colorMathMode", &jVal))
            bg->palette.colorMathMode = json_object_get_int(jVal);
    }
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "colorMathMask", &jVal) && json_object_array_length(jVal) >= 3) {
            bg->palette.colorMathMask.x = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 0));
            bg->palette.colorMathMask.y = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 1));
            bg->palette.colorMathMask.z = (gX_F32)json_object_get_double(json_object_array_get_idx(jVal, 2));
        }
    }

    read_Scalar_param(section, "size",       &bg->palette.size);
    read_Scalar_param(section, "amount",     &bg->palette.amount);
    read_Scalar_param(section, "speed",      &bg->palette.speed);
    read_Scalar_param(section, "frequency",  &bg->palette.frequency);

    // Colors array
    struct json_object *colArr = NULL;
    if (json_object_object_get_ex(section, "colors", &colArr))
    {
        gX_U32 count = (gX_U32)json_object_array_length(colArr);
        if (count > gX_BG_PALETTE_COLORS_MAX) count = gX_BG_PALETTE_COLORS_MAX;
        for (gX_U32 i = 0; i < count; i++)
        {
            struct json_object *colObj = json_object_array_get_idx(colArr, i);
            if (colObj)
                read_V4_param(colObj, "color", &bg->palette.colors[i]);
        }
    }

    read_V4_param(section,    "color1",     &bg->palette.color1);
    read_V4_param(section,    "color2",     &bg->palette.color2);
    read_Scalar_param(section, "bgBoundary", &bg->palette.bgBoundary);
    read_V4_param(section,    "bgColor",    &bg->palette.bgColor);
    read_Scalar_param(section, "fgBoundary", &bg->palette.fgBoundary);
    read_V4_param(section,    "fgColor",    &bg->palette.fgColor);
}

// ===========================================================================
// Section bridge: misc (3 params: 1×V4 + 1×V2 + 1×V2)
// ===========================================================================

static gX_I0 bg_write_misc(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "misc", section);

    write_V4_param(section, "boundaryXY", &bg->misc.boundaryXY);
    write_V2_param(section, "boundaryZ",  &bg->misc.boundaryZ);
    write_V2_param(section, "modulo",     &bg->misc.modulo);
}

static gX_I0 bg_read_misc(struct gX_RendererBackground *bg, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "misc", &section)) return;

    read_V4_param(section, "boundaryXY", &bg->misc.boundaryXY);
    read_V2_param(section, "boundaryZ",  &bg->misc.boundaryZ);
    read_V2_param(section, "modulo",     &bg->misc.modulo);
}

// ===========================================================================
// Top-level param bridge: write/read all param-heavy sections to/from root
// ===========================================================================

static gX_I0 bg_params_to_json(struct gX_RendererBackground *bg, struct json_object *root)
{
    bg_write_transform(bg, root);
    bg_write_special(bg, root);
    bg_write_palette(bg, root);
    bg_write_misc(bg, root);
}

static gX_I0 bg_params_from_json(struct gX_RendererBackground *bg, struct json_object *root)
{
    bg_read_transform(bg, root);
    bg_read_special(bg, root);
    bg_read_palette(bg, root);
    bg_read_misc(bg, root);
}

// ===========================================================================
// New schema field tables — flat fields only (non-param)
// State: 9 booleans
// ===========================================================================

static gX_SchemaField bg_state_fields[] = {
    GX_SCHEMA_BOOL("paramEnabled",       gX_RendererBackground_State, paramEnabled),
    GX_SCHEMA_BOOL("warpEnabled",        gX_RendererBackground_State, warpEnabled),
    GX_SCHEMA_BOOL("waveEnabled",        gX_RendererBackground_State, waveEnabled),
    GX_SCHEMA_BOOL("paletteEnabled",     gX_RendererBackground_State, paletteEnabled),
    GX_SCHEMA_BOOL("boundaryEnabled",    gX_RendererBackground_State, boundaryEnabled),
    GX_SCHEMA_BOOL("boundaryXYEnabled",  gX_RendererBackground_State, boundaryXYEnabled),
    GX_SCHEMA_BOOL("boundaryZEnabled",   gX_RendererBackground_State, boundaryZEnabled),
    GX_SCHEMA_BOOL("moduloEnabled",      gX_RendererBackground_State, moduloEnabled),
    GX_SCHEMA_BOOL("useTransformMatrix", gX_RendererBackground_State, useTransformMatrix),
    GX_SCHEMA_BOOL("hdmaEnabled", gX_RendererBackground_State, hdmaEnabled),
};

// Root schema — state via offset-based schema, everything else via bridge
static gX_SchemaField bg_root_fields[] = {
    GX_SCHEMA_OBJECT("state", gX_RendererBackground, state,
                     bg_state_fields,
                     (gX_U32)(sizeof(bg_state_fields) / sizeof(bg_state_fields[0]))),
};

// ===========================================================================
// Preset Save — new API
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Save_New(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "RENDERER_BACKGROUND: Saving preset (new schema) to %s", path);

    struct json_object *root = json_object_new_object();

    // Meta fields
    json_object_object_add(root, "name", json_object_new_string(bg->name));
    json_object_object_add(root, "description", json_object_new_string(bg->description));

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    gX_I8 date[64], timeStr[64];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    json_object_object_add(root, "created_date", json_object_new_string(date));
    json_object_object_add(root, "created_time", json_object_new_string(timeStr));

    // Schema-driven flat fields (state booleans)
    gX_U32 fc = (gX_U32)(sizeof(bg_root_fields) / sizeof(bg_root_fields[0]));
    gX_Schema_JSON_SaveToObj(bg_root_fields, fc, (gX_I0*)bg, root);

    // Param sections via bridge
    bg_params_to_json(bg, root);

    // Write to file
    gX_I32 flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext(path, root, flags);
    json_object_put(root);

    if (ret == 0)
        gX_TraceLog_BACKGROUND(gX_LOG_INFO, "RENDERER_BACKGROUND: Save done");
    else
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR, "RENDERER_BACKGROUND: Save failed");
    return ret == 0 ? 0 : -1;
}

// ===========================================================================
// Preset Load — new API
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Load_New(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "RENDERER_BACKGROUND: Loading preset (new schema) from %s", path);

    struct json_object *root = json_object_from_file(path);
    if (!root)
    {
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR,
            "RENDERER_BACKGROUND: Failed to load preset from %s", path);
        return 1;
    }

    // Meta fields (just read back for info, no need to store)
    // Schema-driven flat fields (state booleans)
    gX_U32 fc = (gX_U32)(sizeof(bg_root_fields) / sizeof(bg_root_fields[0]));
    gX_Schema_JSON_LoadFromObj(bg_root_fields, fc, (gX_I0*)bg, root);

    // Param sections via bridge
    bg_params_from_json(bg, root);

    bg->state.paramsDirty = true; //opencode: params loaded from preset
    bg->state.colorsDirty = true; //opencode: colors loaded from preset

    json_object_put(root);

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "RENDERER_BACKGROUND: Load done");
    return 0;
}

// ===========================================================================
// YAML emitter helpers — write a param scalar/V2/V3/V4 as YAML inline
// ===========================================================================

static void yaml_write_param_scalar(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamScalar *p)
{
    fprintf(f, "%s%s:\n", indent, key);
    fprintf(f, "%s  constant: %f\n", indent, p->constant);
    fprintf(f, "%s  velocity: %f\n", indent, p->velocity);
    fprintf(f, "%s  acceleration: %f\n", indent, p->acceleration);
}

static void yaml_write_param_v2(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV2 *p)
{
    fprintf(f, "%s%s:\n", indent, key);
    fprintf(f, "%s  constant: [%f,%f]\n", indent, p->constant.x, p->constant.y);
    fprintf(f, "%s  velocity: [%f,%f]\n", indent, p->velocity.x, p->velocity.y);
    fprintf(f, "%s  acceleration: [%f,%f]\n", indent, p->acceleration.x, p->acceleration.y);
}

static void yaml_write_param_v3(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV3 *p)
{
    fprintf(f, "%s%s:\n", indent, key);
    fprintf(f, "%s  constant: [%f,%f,%f]\n", indent, p->constant.x, p->constant.y, p->constant.z);
    fprintf(f, "%s  velocity: [%f,%f,%f]\n", indent, p->velocity.x, p->velocity.y, p->velocity.z);
    fprintf(f, "%s  acceleration: [%f,%f,%f]\n", indent, p->acceleration.x, p->acceleration.y, p->acceleration.z);
}

static void yaml_write_param_v4(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV4 *p)
{
    fprintf(f, "%s%s:\n", indent, key);
    fprintf(f, "%s  constant: [%f,%f,%f,%f]\n", indent, p->constant.x, p->constant.y, p->constant.z, p->constant.w);
    fprintf(f, "%s  velocity: [%f,%f,%f,%f]\n", indent, p->velocity.x, p->velocity.y, p->velocity.z, p->velocity.w);
    fprintf(f, "%s  acceleration: [%f,%f,%f,%f]\n", indent, p->acceleration.x, p->acceleration.y, p->acceleration.z, p->acceleration.w);
}

// ===========================================================================
// Preset Save — YAML format (covers all param sections)
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Save_Yaml(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "RENDERER_BACKGROUND: Saving YAML preset to %s", path);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# background preset\n");
    fprintf(f, "name: \"%s\"\n", bg->name);
    fprintf(f, "description: \"%s\"\n", bg->description);

    // State booleans
    fprintf(f, "state:\n");
    #define YAML_BOOL(field) fprintf(f, "  %s: %s\n", #field, bg->state.field ? "true" : "false");
    YAML_BOOL(paramEnabled)
    YAML_BOOL(warpEnabled)
    YAML_BOOL(waveEnabled)
    YAML_BOOL(paletteEnabled)
    YAML_BOOL(boundaryEnabled)
    YAML_BOOL(boundaryXYEnabled)
    YAML_BOOL(boundaryZEnabled)
    YAML_BOOL(moduloEnabled)
    YAML_BOOL(useTransformMatrix)
    YAML_BOOL(useDirectMatrix)
    #undef YAML_BOOL
    fprintf(f, "  palette_mode: %d\n", bg->palette.mode);

    // Transform section
    fprintf(f, "transform:\n");
    yaml_write_param_scalar(f, "  ", "scale", &bg->transform.scale);
    yaml_write_param_v2(f, "  ", "scaleOrigin", &bg->transform.scaleOrigin);
    yaml_write_param_scalar(f, "  ", "rotate", &bg->transform.rotate);
    yaml_write_param_v2(f, "  ", "rotateOrigin", &bg->transform.rotateOrigin);
    yaml_write_param_v4(f, "  ", "stretch", &bg->transform.stretch);
    yaml_write_param_v2(f, "  ", "perspective", &bg->transform.perspective);
    yaml_write_param_v2(f, "  ", "translate", &bg->transform.translate);
    yaml_write_param_v2(f, "  ", "affine", &bg->transform.affine);

    // Special section
    fprintf(f, "special:\n");
    yaml_write_param_v3(f, "  ", "warp", &bg->special.warp);
    yaml_write_param_scalar(f, "  ", "warpExponent", &bg->special.warpExponent);
    yaml_write_param_v4(f, "  ", "waveAmplitude", &bg->special.waveAmplitude);
    yaml_write_param_v4(f, "  ", "waveFrequency", &bg->special.waveFrequency);
    yaml_write_param_v4(f, "  ", "waveRegularwave", &bg->special.waveRegularwave);
    yaml_write_param_v4(f, "  ", "waveInterlaced", &bg->special.waveInterlaced);
    yaml_write_param_v4(f, "  ", "waveInterlacedMultiplier", &bg->special.waveInterlacedMultiplier);
    yaml_write_param_v4(f, "  ", "waveSpeed", &bg->special.waveSpeed);
    yaml_write_param_v4(f, "  ", "wavePhase", &bg->special.wavePhase);

    // Sinusoids as YAML array (simplified — constant values only)
    fprintf(f, "sinusoids:\n");
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++) {
        const gX_RenBG_Sinusoid *s = &bg->special.sinusoids[i];
        fprintf(f, "  - index: %u\n", i);
        fprintf(f, "    amplitude: [%f,%f,%f,%f]\n",
            s->amplitude.constant.x, s->amplitude.constant.y,
            s->amplitude.constant.z, s->amplitude.constant.w);
        fprintf(f, "    frequency: [%f,%f,%f,%f]\n",
            s->frequency.constant.x, s->frequency.constant.y,
            s->frequency.constant.z, s->frequency.constant.w);
        fprintf(f, "    phase: [%f,%f,%f,%f]\n",
            s->phase.constant.x, s->phase.constant.y,
            s->phase.constant.z, s->phase.constant.w);
        fprintf(f, "    speed: [%f,%f,%f,%f]\n",
            s->speed.constant.x, s->speed.constant.y,
            s->speed.constant.z, s->speed.constant.w);
        fprintf(f, "    exponent: %f\n", s->exponent.constant);
        fprintf(f, "    attenuation: %f\n", s->attenuation.constant);
        fprintf(f, "    dcOffset: [%f,%f,%f,%f]\n",
            s->dcOffset.constant.x, s->dcOffset.constant.y,
            s->dcOffset.constant.z, s->dcOffset.constant.w);
        fprintf(f, "    center: [%f,%f]\n", s->center.constant.x, s->center.constant.y);
    }

    // Palette section
    fprintf(f, "palette:\n");
    fprintf(f, "  mode: %d\n", bg->palette.mode);
    yaml_write_param_scalar(f, "  ", "amount", &bg->palette.amount);
    yaml_write_param_scalar(f, "  ", "speed", &bg->palette.speed);
    yaml_write_param_scalar(f, "  ", "frequency", &bg->palette.frequency);
    fprintf(f, "  color1: [%f,%f,%f,%f]\n",
        bg->palette.color1.constant.x, bg->palette.color1.constant.y,
        bg->palette.color1.constant.z, bg->palette.color1.constant.w);
    fprintf(f, "  color2: [%f,%f,%f,%f]\n",
        bg->palette.color2.constant.x, bg->palette.color2.constant.y,
        bg->palette.color2.constant.z, bg->palette.color2.constant.w);

    // Misc section
    fprintf(f, "misc:\n");
    fprintf(f, "  boundaryXY: [%f,%f,%f,%f]\n",
        bg->misc.boundaryXY.constant.x, bg->misc.boundaryXY.constant.y,
        bg->misc.boundaryXY.constant.z, bg->misc.boundaryXY.constant.w);
    fprintf(f, "  boundaryZ: [%f,%f]\n",
        bg->misc.boundaryZ.constant.x, bg->misc.boundaryZ.constant.y);
    fprintf(f, "  modulo: [%f,%f]\n",
        bg->misc.modulo.constant.x, bg->misc.modulo.constant.y);

    fclose(f);
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "RENDERER_BACKGROUND: YAML save done");
    return 0;
}

// ===========================================================================
// Preset Load — YAML format (delegates to JSON loader)
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Load_Yaml(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_WARNING,
        "RENDERER_BACKGROUND: YAML load not directly supported, "
        "falling back to JSON load. Convert YAML to JSON or use .json extension.");
    // YAML load via conversion would go here; for now, try JSON as fallback
    return gX_RendererBG_Preset_Load_New(bg, path);
}

// ===========================================================================
// XML emitter — write a param's constant values as XML elements
// ===========================================================================

static void xml_write_param_scalar(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamScalar *p)
{
    fprintf(f, "%s<%s constant=\"%f\" velocity=\"%f\" acceleration=\"%f\" />\n",
        indent, key, p->constant, p->velocity, p->acceleration);
}

static void xml_write_param_v2(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV2 *p)
{
    fprintf(f, "%s<%s constant=\"%f,%f\" velocity=\"%f,%f\" acceleration=\"%f,%f\" />\n",
        indent, key, p->constant.x, p->constant.y,
        p->velocity.x, p->velocity.y, p->acceleration.x, p->acceleration.y);
}

static void xml_write_param_v3(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV3 *p)
{
    fprintf(f, "%s<%s constant=\"%f,%f,%f\" velocity=\"%f,%f,%f\" acceleration=\"%f,%f,%f\" />\n",
        indent, key, p->constant.x, p->constant.y, p->constant.z,
        p->velocity.x, p->velocity.y, p->velocity.z,
        p->acceleration.x, p->acceleration.y, p->acceleration.z);
}

static void xml_write_param_v4(FILE *f, const gX_I8 *indent,
    const gX_I8 *key, const gX_RenBG_ParamV4 *p)
{
    fprintf(f, "%s<%s constant=\"%f,%f,%f,%f\" velocity=\"%f,%f,%f,%f\" acceleration=\"%f,%f,%f,%f\" />\n",
        indent, key,
        p->constant.x, p->constant.y, p->constant.z, p->constant.w,
        p->velocity.x, p->velocity.y, p->velocity.z, p->velocity.w,
        p->acceleration.x, p->acceleration.y, p->acceleration.z, p->acceleration.w);
}

// ===========================================================================
// Preset Save — XML format (covers all param sections)
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Save_Xml(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "RENDERER_BACKGROUND: Saving XML preset to %s", path);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<background_preset name=\"%s\" description=\"%s\">\n",
        bg->name, bg->description);

    // State
    fprintf(f, "  <state>\n");
    #define XML_BOOL(field) fprintf(f, "    <%s>%s</%s>\n", #field, \
        bg->state.field ? "true" : "false", #field);
    XML_BOOL(paramEnabled)
    XML_BOOL(warpEnabled)
    XML_BOOL(waveEnabled)
    XML_BOOL(paletteEnabled)
    XML_BOOL(boundaryEnabled)
    XML_BOOL(boundaryXYEnabled)
    XML_BOOL(boundaryZEnabled)
    XML_BOOL(moduloEnabled)
    XML_BOOL(useTransformMatrix)
    XML_BOOL(useDirectMatrix)
    #undef XML_BOOL
    fprintf(f, "    <palette_mode>%d</palette_mode>\n", bg->palette.mode);
    fprintf(f, "  </state>\n");

    // Transform
    fprintf(f, "  <transform>\n");
    xml_write_param_scalar(f, "    ", "scale", &bg->transform.scale);
    xml_write_param_v2(f, "    ", "scaleOrigin", &bg->transform.scaleOrigin);
    xml_write_param_scalar(f, "    ", "rotate", &bg->transform.rotate);
    xml_write_param_v2(f, "    ", "rotateOrigin", &bg->transform.rotateOrigin);
    xml_write_param_v4(f, "    ", "stretch", &bg->transform.stretch);
    xml_write_param_v2(f, "    ", "perspective", &bg->transform.perspective);
    xml_write_param_v2(f, "    ", "translate", &bg->transform.translate);
    xml_write_param_v2(f, "    ", "affine", &bg->transform.affine);
    fprintf(f, "  </transform>\n");

    // Special
    fprintf(f, "  <special>\n");
    xml_write_param_v3(f, "    ", "warp", &bg->special.warp);
    xml_write_param_scalar(f, "    ", "warpExponent", &bg->special.warpExponent);
    xml_write_param_v4(f, "    ", "waveAmplitude", &bg->special.waveAmplitude);
    xml_write_param_v4(f, "    ", "waveFrequency", &bg->special.waveFrequency);
    xml_write_param_v4(f, "    ", "waveRegularwave", &bg->special.waveRegularwave);
    xml_write_param_v4(f, "    ", "waveInterlaced", &bg->special.waveInterlaced);
    xml_write_param_v4(f, "    ", "waveInterlacedMultiplier", &bg->special.waveInterlacedMultiplier);
    xml_write_param_v4(f, "    ", "waveSpeed", &bg->special.waveSpeed);
    xml_write_param_v4(f, "    ", "wavePhase", &bg->special.wavePhase);
    fprintf(f, "  </special>\n");

    // Sinusoids
    fprintf(f, "  <sinusoids>\n");
    for (gX_U32 i = 0; i < gX_BG_SINUSOID_COUNT; i++) {
        const gX_RenBG_Sinusoid *s = &bg->special.sinusoids[i];
        fprintf(f, "    <sinusoid index=\"%u\">\n", i);
        fprintf(f, "      <amplitude>%f,%f,%f,%f</amplitude>\n",
            s->amplitude.constant.x, s->amplitude.constant.y,
            s->amplitude.constant.z, s->amplitude.constant.w);
        fprintf(f, "      <frequency>%f,%f,%f,%f</frequency>\n",
            s->frequency.constant.x, s->frequency.constant.y,
            s->frequency.constant.z, s->frequency.constant.w);
        fprintf(f, "      <exponent>%f</exponent>\n", s->exponent.constant);
        fprintf(f, "      <attenuation>%f</attenuation>\n", s->attenuation.constant);
        fprintf(f, "      <dcOffset>%f,%f,%f,%f</dcOffset>\n",
            s->dcOffset.constant.x, s->dcOffset.constant.y,
            s->dcOffset.constant.z, s->dcOffset.constant.w);
        fprintf(f, "      <center>%f,%f</center>\n",
            s->center.constant.x, s->center.constant.y);
        fprintf(f, "    </sinusoid>\n");
    }
    fprintf(f, "  </sinusoids>\n");

    // Palette
    fprintf(f, "  <palette mode=\"%d\">\n", bg->palette.mode);
    xml_write_param_scalar(f, "    ", "amount", &bg->palette.amount);
    xml_write_param_scalar(f, "    ", "speed", &bg->palette.speed);
    xml_write_param_scalar(f, "    ", "frequency", &bg->palette.frequency);
    fprintf(f, "    <color1>%f,%f,%f,%f</color1>\n",
        bg->palette.color1.constant.x, bg->palette.color1.constant.y,
        bg->palette.color1.constant.z, bg->palette.color1.constant.w);
    fprintf(f, "    <color2>%f,%f,%f,%f</color2>\n",
        bg->palette.color2.constant.x, bg->palette.color2.constant.y,
        bg->palette.color2.constant.z, bg->palette.color2.constant.w);
    fprintf(f, "  </palette>\n");

    // Misc
    fprintf(f, "  <misc>\n");
    fprintf(f, "    <boundaryXY>%f,%f,%f,%f</boundaryXY>\n",
        bg->misc.boundaryXY.constant.x, bg->misc.boundaryXY.constant.y,
        bg->misc.boundaryXY.constant.z, bg->misc.boundaryXY.constant.w);
    fprintf(f, "    <boundaryZ>%f,%f</boundaryZ>\n",
        bg->misc.boundaryZ.constant.x, bg->misc.boundaryZ.constant.y);
    fprintf(f, "    <modulo>%f,%f</modulo>\n",
        bg->misc.modulo.constant.x, bg->misc.modulo.constant.y);
    fprintf(f, "  </misc>\n");

    fprintf(f, "</background_preset>\n");
    fclose(f);

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "RENDERER_BACKGROUND: XML save done");
    return 0;
}

// ===========================================================================
// Preset Load — XML format (delegates to JSON loader)
// ===========================================================================

gX_I32 gX_RendererBG_Preset_Load_Xml(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_WARNING,
        "RENDERER_BACKGROUND: XML load not directly supported, "
        "falling back to JSON load. Convert XML to JSON or use .json extension.");
    return gX_RendererBG_Preset_Load_New(bg, path);
}
