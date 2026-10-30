/**********************************************************************************************
*
*   [gX: src/renderer/postprocess/schema.c]
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


//opencode: offset-based schema for PostProcess renderer (new system, coexists with old schema.c)
// State booleans use the new offset-based schema system via GX_SCHEMA_OBJECT.
// Inject, shader, and texture sections use json-c bridge functions because they contain
// fixed-size arrays (coords[8], scale[8], uniforms[8], etc.) and Background param types
// that don't map cleanly to the flat schema approach.

#include "schema.h"

#include <string.h>
#include <time.h>

#include "../../common/schema.h"
#include "../../common/marshal/schema_json.h"
#include "../../common/log.h"

//opencode: json-c types for manual bridge serialization
#include "../../common/marshal/json_impl.h"

#include "../postprocess.h"
#include "../background/param.h"
#include "../../resource/resource.h"

// ===========================================================================
// Helpers: add/get gX_F32 / gX_F32v2 / gX_F32v3 / gX_F32v4 / gX_Color to JSON
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

static void json_add_v4(struct json_object *obj, const gX_I8 *key, gX_F32v4 val)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_double(val.x));
    json_object_array_add(arr, json_object_new_double(val.y));
    json_object_array_add(arr, json_object_new_double(val.z));
    json_object_array_add(arr, json_object_new_double(val.w));
    json_object_object_add(obj, key, arr);
}

static void json_add_color(struct json_object *obj, const gX_I8 *key, gX_Color val)
{
    struct json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_int(val.r));
    json_object_array_add(arr, json_object_new_int(val.g));
    json_object_array_add(arr, json_object_new_int(val.b));
    json_object_array_add(arr, json_object_new_int(val.a));
    json_object_object_add(obj, key, arr);
}

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

static gX_I32 json_get_color(struct json_object *parent, const gX_I8 *key, gX_Color *out)
{
    struct json_object *jVal = NULL;
    if (!json_object_object_get_ex(parent, key, &jVal)) return 1;
    if (json_object_array_length(jVal) < 4) return 1;
    out->r = (gX_U8)json_object_get_int(json_object_array_get_idx(jVal, 0));
    out->g = (gX_U8)json_object_get_int(json_object_array_get_idx(jVal, 1));
    out->b = (gX_U8)json_object_get_int(json_object_array_get_idx(jVal, 2));
    out->a = (gX_U8)json_object_get_int(json_object_array_get_idx(jVal, 3));
    return 0;
}

// ===========================================================================
// Background param write/read helpers (same pattern as Background schema2)
// ===========================================================================

#define PP_PARAM_WRITE(ptype, json_add_fn) \
static void pp_write_##ptype##_param(struct json_object *parent, const gX_I8 *key, \
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

PP_PARAM_WRITE(Scalar, json_add_f32)
PP_PARAM_WRITE(V2,     json_add_v2)
PP_PARAM_WRITE(V4,     json_add_v4)

#define PP_PARAM_READ(ptype, json_get_fn) \
static void pp_read_##ptype##_param(struct json_object *parent, const gX_I8 *key, \
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

PP_PARAM_READ(Scalar, json_get_f32)
PP_PARAM_READ(V2,     json_get_v2)
PP_PARAM_READ(V4,     json_get_v4)

// ===========================================================================
// Bridge: inject section — scalar fields, array slots, and params
// ===========================================================================

static gX_I0 pp_write_inject(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "inject", section);

    // Scalar fields
    json_object_object_add(section, "useCustomTexture",
        json_object_new_boolean(pp->inject.useCustomTexture ? 1 : 0));
    json_object_object_add(section, "useParam",
        json_object_new_boolean(pp->inject.useParam ? 1 : 0));
    json_object_object_add(section, "connectScale",
        json_object_new_boolean(pp->inject.connectScale ? 1 : 0));
    json_object_object_add(section, "amount",
        json_object_new_int64((gX_I64)pp->inject.amount));

    // Array slots: coords[8], scale[8], rotate[8], color[8], blendMode[8]
    {
        struct json_object *coordsArr = json_object_new_array();
        for (gX_U32 i = 0; i < 8; i++)
        {
            struct json_object *elem = json_object_new_object();
            json_add_v2(elem, "coords", pp->inject.coords[i]);
            json_add_v2(elem, "scale",  pp->inject.scale[i]);
            json_add_f32(elem, "rotate", pp->inject.rotate[i]);
            json_add_color(elem, "color", pp->inject.color[i]);
            json_object_object_add(elem, "blendMode",
                json_object_new_int(pp->inject.blendMode[i]));
            //opencode: per-slot texture path for serialization
            if (pp->inject.texturePaths[i][0] != '\0') {
                json_object_object_add(elem, "texturePath",
                    json_object_new_string(pp->inject.texturePaths[i]));
            }
            json_object_array_add(coordsArr, elem);
        }
        json_object_object_add(section, "slots", coordsArr);
    }

    // Param fields
    pp_write_V2_param(section,     "coordsParam", &pp->inject.coordsParam);
    pp_write_V2_param(section,     "scaleParam",  &pp->inject.scaleParam);
    pp_write_Scalar_param(section, "rotateParam", &pp->inject.rotateParam);
    pp_write_V4_param(section,     "colorParam",  &pp->inject.colorParam);
}

static gX_I0 pp_read_inject(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "inject", &section)) return;

    // Scalar fields
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "useCustomTexture", &jVal))
            pp->inject.useCustomTexture = json_object_get_boolean(jVal) ? 1 : 0;
        if (json_object_object_get_ex(section, "useParam", &jVal))
            pp->inject.useParam = json_object_get_boolean(jVal) ? 1 : 0;
        if (json_object_object_get_ex(section, "connectScale", &jVal))
            pp->inject.connectScale = json_object_get_boolean(jVal) ? 1 : 0;
        if (json_object_object_get_ex(section, "amount", &jVal))
            pp->inject.amount = (gX_U32)json_object_get_int64(jVal);
    }

    // Array slots: coords[8], scale[8], rotate[8], color[8], blendMode[8]
    {
        struct json_object *slotsArr = NULL;
        if (json_object_object_get_ex(section, "slots", &slotsArr))
        {
            gX_U32 count = (gX_U32)json_object_array_length(slotsArr);
            if (count > 8) count = 8;
            for (gX_U32 i = 0; i < count; i++)
            {
                struct json_object *elem = json_object_array_get_idx(slotsArr, i);
                if (!elem) continue;
                json_get_v2(elem, "coords", &pp->inject.coords[i]);
                json_get_v2(elem, "scale",  &pp->inject.scale[i]);
                json_get_f32(elem, "rotate", &pp->inject.rotate[i]);
                json_get_color(elem, "color", &pp->inject.color[i]);
                {
                    struct json_object *jBm = NULL;
                    if (json_object_object_get_ex(elem, "blendMode", &jBm))
                        pp->inject.blendMode[i] = json_object_get_int(jBm);
                }
                //opencode: load per-slot texture from path
                {
                    struct json_object *jTp = NULL;
                    if (json_object_object_get_ex(elem, "texturePath", &jTp))
                    {
                        const gX_I8 *tp = json_object_get_string(jTp);
                        if (tp && tp[0] != '\0')
                        {
                            strncpy(pp->inject.texturePaths[i], tp, 255);
                            pp->inject.texturePaths[i][255] = '\0';
                            gX_Texture *tex = gX_Resource_LoadTexture(tp);
                            if (tex) pp->inject.textures[i] = tex;
                        }
                    }
                }
            }
        }
    }
    
    // Param fields
    pp_read_V2_param(section,     "coordsParam", &pp->inject.coordsParam);
    pp_read_V2_param(section,     "scaleParam",  &pp->inject.scaleParam);
    pp_read_Scalar_param(section, "rotateParam", &pp->inject.rotateParam);
    pp_read_V4_param(section,     "colorParam",  &pp->inject.colorParam);
}

// ===========================================================================
// Bridge: shaders section — writes shaders[shadersAmount] as a JSON array
// ===========================================================================

static void pp_write_uniform(struct json_object *parent, const gX_RendererPostProcess_ShaderUniform *u)
{
    struct json_object *obj = json_object_new_object();
    json_add_v4(obj, "value",    u->value);
    json_add_v4(obj, "deltaAdd", u->deltaAdd);
    json_add_v4(obj, "deltaMul", u->deltaMul);
    json_object_object_add(parent, "uniform", obj);
}

static void pp_read_uniform(struct json_object *parent, gX_RendererPostProcess_ShaderUniform *u)
{
    struct json_object *obj = NULL;
    if (!json_object_object_get_ex(parent, "uniform", &obj)) return;
    json_get_v4(obj, "value",    &u->value);
    json_get_v4(obj, "deltaAdd", &u->deltaAdd);
    json_get_v4(obj, "deltaMul", &u->deltaMul);
}

static void pp_write_shader(struct json_object *parent, const gX_RendererPostProcess_Shader *s)
{
    struct json_object *obj = json_object_new_object();

    // Scalar + string fields
    json_object_object_add(obj, "name",
        json_object_new_string(s->name));
    json_object_object_add(obj, "path",
        json_object_new_string(s->path));
    json_object_object_add(obj, "type",
        json_object_new_int(s->type));
    json_object_object_add(obj, "version",
        json_object_new_int(s->version));
    json_object_object_add(obj, "loops",
        json_object_new_int64((gX_I64)s->loops));
    json_object_object_add(obj, "blendMode",
        json_object_new_int(s->blendMode));
    json_object_object_add(obj, "filterMode",
        json_object_new_int(s->filterMode));
    json_object_object_add(obj, "wrapMode",
        json_object_new_int(s->wrapMode));
    json_object_object_add(obj, "uniformsAmount",
        json_object_new_int64((gX_I64)s->uniformsAmount));

    // Uniforms array (up to GX_PP_UNIFORM_AMOUNT)
    struct json_object *unifArr = json_object_new_array();
    gX_U32 unifCount = s->uniformsAmount < GX_PP_UNIFORM_AMOUNT
                       ? s->uniformsAmount : GX_PP_UNIFORM_AMOUNT;
    for (gX_U32 i = 0; i < unifCount; i++)
    {
        struct json_object *unifObj = json_object_new_object();
        pp_write_uniform(unifObj, &s->uniforms[i]);
        json_object_array_add(unifArr, unifObj);
    }
    json_object_object_add(obj, "uniforms", unifArr);

    //ai: parent is the chain ARRAY — json_object_object_add on an array is a
    //   no-op, so the saved preset silently had an empty chain.
    json_object_array_add(parent, obj);
}

static void pp_read_shader(struct json_object *parent, gX_RendererPostProcess_Shader *s)
{
    //ai: the chain array holds bare entry objects (the writer adds them with
    //   json_object_array_add), so the element IS the entry — the old
    //   "shader" wrapper lookup made every load a silent no-op.
    struct json_object *obj = parent;
    if (!obj) return;

    // Scalar + string fields
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(obj, "name", &jVal))
        {
            strncpy(s->name, json_object_get_string(jVal), 127);
            s->name[127] = '\0';
        }
        if (json_object_object_get_ex(obj, "path", &jVal))
        {
            strncpy(s->path, json_object_get_string(jVal), 127);
            s->path[127] = '\0';
        }
        if (json_object_object_get_ex(obj, "type", &jVal))
            s->type = json_object_get_int(jVal);
        if (json_object_object_get_ex(obj, "version", &jVal))
            s->version = json_object_get_int(jVal);
        if (json_object_object_get_ex(obj, "loops", &jVal))
            s->loops = (gX_U32)json_object_get_int64(jVal);
        if (json_object_object_get_ex(obj, "blendMode", &jVal))
            s->blendMode = json_object_get_int(jVal);
        if (json_object_object_get_ex(obj, "filterMode", &jVal))
            s->filterMode = json_object_get_int(jVal);
        if (json_object_object_get_ex(obj, "wrapMode", &jVal))
            s->wrapMode = json_object_get_int(jVal);
        if (json_object_object_get_ex(obj, "uniformsAmount", &jVal))
        {
            gX_U32 amt = (gX_U32)json_object_get_int64(jVal);
            s->uniformsAmount = amt < GX_PP_UNIFORM_AMOUNT ? amt : GX_PP_UNIFORM_AMOUNT;
        }
    }

    // Uniforms array
    struct json_object *unifArr = NULL;
    if (json_object_object_get_ex(obj, "uniforms", &unifArr))
    {
        gX_U32 count = (gX_U32)json_object_array_length(unifArr);
        if (count > GX_PP_UNIFORM_AMOUNT)
            count = GX_PP_UNIFORM_AMOUNT;
        for (gX_U32 i = 0; i < count; i++)
        {
            struct json_object *unifObj = json_object_array_get_idx(unifArr, i);
            if (unifObj)
                pp_read_uniform(unifObj, &s->uniforms[i]);
        }
    }
}

static gX_I0 pp_write_shaders(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "shaders", section);

    // shadersAmount as a flat scalar
    json_object_object_add(section, "shadersAmount",
        json_object_new_int64((gX_I64)pp->shadersAmount));

    // Shader array: serialize only the active shaders (up to shadersAmount)
    struct json_object *arr = json_object_new_array();
    gX_U32 count = pp->shadersAmount < 8 ? pp->shadersAmount : 8;
    for (gX_U32 i = 0; i < count; i++)
    {
        pp_write_shader(arr, &pp->shaders[i]);
    }
    json_object_object_add(section, "shader", arr);
}

static gX_I0 pp_read_shaders(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "shaders", &section)) return;

    // Read shadersAmount
    {
        struct json_object *jVal = NULL;
        if (json_object_object_get_ex(section, "shadersAmount", &jVal))
            pp->shadersAmount = (gX_U32)json_object_get_int64(jVal);
        if (pp->shadersAmount > 8)
            pp->shadersAmount = 8;
    }

    // Shader array
    struct json_object *arr = NULL;
    if (!json_object_object_get_ex(section, "shader", &arr)) return;

    gX_U32 count = (gX_U32)json_object_array_length(arr);
    if (count > pp->shadersAmount)
        count = pp->shadersAmount;
    for (gX_U32 i = 0; i < count; i++)
    {
        struct json_object *elem = json_object_array_get_idx(arr, i);
        if (elem)
            pp_read_shader(elem, &pp->shaders[i]);
    }
}

// ===========================================================================
// Bridge: texture section — mode fields (skip gX_Texture/gX_RenderTexture)
// ===========================================================================

static gX_I0 pp_write_texture(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = json_object_new_object();
    json_object_object_add(root, "texture", section);

    json_object_object_add(section, "startBlendMode",
        json_object_new_int(pp->texture.startBlendMode));
    json_object_object_add(section, "startFilterMode",
        json_object_new_int(pp->texture.startFilterMode));
    json_object_object_add(section, "startWrapMode",
        json_object_new_int(pp->texture.startWrapMode));
    //opencode: renderScale for resolution scaling
    json_add_f32(section, "renderScale", pp->texture.renderScale);
}

static gX_I0 pp_read_texture(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    struct json_object *section = NULL;
    if (!json_object_object_get_ex(root, "texture", &section)) return;

    struct json_object *jVal = NULL;
    if (json_object_object_get_ex(section, "startBlendMode", &jVal))
        pp->texture.startBlendMode = json_object_get_int(jVal);
    if (json_object_object_get_ex(section, "startFilterMode", &jVal))
        pp->texture.startFilterMode = json_object_get_int(jVal);
    if (json_object_object_get_ex(section, "startWrapMode", &jVal))
        pp->texture.startWrapMode = json_object_get_int(jVal);
    //opencode: renderScale — triggers RT recreation on next Update
    if (json_get_f32(section, "renderScale", &pp->texture.renderScale) == 0) {
        if (pp->texture.renderScale < 0.25f) pp->texture.renderScale = 0.25f;
        if (pp->texture.renderScale > 1.0f) pp->texture.renderScale = 1.0f;
        pp->texture.renderScaleChanged = 1;
    }
}

// ===========================================================================
// Section bridges: write/read all non-schema sections to/from root JSON
// ===========================================================================

static gX_I0 pp_sections_to_json(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    pp_write_inject(pp, root);
    pp_write_shaders(pp, root);
    pp_write_texture(pp, root);
}

static gX_I0 pp_sections_from_json(struct gX_RendererPostProcess *pp, struct json_object *root)
{
    pp_read_inject(pp, root);
    pp_read_shaders(pp, root);
    pp_read_texture(pp, root);
}

// ===========================================================================
// New schema field tables — flat fields only (state booleans + enums)
// ===========================================================================

static gX_SchemaField pp_state_fields[] = {
    // Load lifecycle booleans
    GX_SCHEMA_BOOL("load",         gX_RendererPostProcess_State, load),
    GX_SCHEMA_BOOL("unload",       gX_RendererPostProcess_State, unload),
    GX_SCHEMA_BOOL("reload",       gX_RendererPostProcess_State, reload),
    GX_SCHEMA_BOOL("loaded",       gX_RendererPostProcess_State, loaded),
    GX_SCHEMA_BOOL("unloadAfter",  gX_RendererPostProcess_State, unloadAfter),

    // Enable toggle
    GX_SCHEMA_BOOL("enabled",      gX_RendererPostProcess_State, enabled),

    // Shader stack control booleans
    GX_SCHEMA_BOOL("pushShader",   gX_RendererPostProcess_State, pushShader),
    GX_SCHEMA_BOOL("popShader",    gX_RendererPostProcess_State, popShader),
    GX_SCHEMA_BOOL("reloadShader", gX_RendererPostProcess_State, reloadShader),

    // Shader stack tokens
    GX_SCHEMA_I32("typeToken",     gX_RendererPostProcess_State, typeToken),
    GX_SCHEMA_I32("indexToken",    gX_RendererPostProcess_State, indexToken),

    // Composite flags
    GX_SCHEMA_BOOL("feedback",     gX_RendererPostProcess_State, feedback),
    GX_SCHEMA_BOOL("inject",       gX_RendererPostProcess_State, inject),
    GX_SCHEMA_BOOL("injectCanvas", gX_RendererPostProcess_State, injectCanvas),

    //opencode: feedback decay + blend
    GX_SCHEMA_F32("feedbackDecay",     gX_RendererPostProcess_State, feedbackDecay),
    GX_SCHEMA_I32("feedbackBlendMode", gX_RendererPostProcess_State, feedbackBlendMode),

    //opencode: injectCanvas blend + tint
    GX_SCHEMA_I32("injectCanvasBlendMode", gX_RendererPostProcess_State, injectCanvasBlendMode),
    GX_SCHEMA_U32("injectCanvasTintR",     gX_RendererPostProcess_State, injectCanvasTint.r),
    GX_SCHEMA_U32("injectCanvasTintG",     gX_RendererPostProcess_State, injectCanvasTint.g),
    GX_SCHEMA_U32("injectCanvasTintB",     gX_RendererPostProcess_State, injectCanvasTint.b),
    GX_SCHEMA_U32("injectCanvasTintA",     gX_RendererPostProcess_State, injectCanvasTint.a),
};

// Root schema — state via offset-based schema, everything else via bridge
static gX_SchemaField pp_root_fields[] = {
    GX_SCHEMA_OBJECT("state", gX_RendererPostProcess, state,
                     pp_state_fields,
                     (gX_U32)(sizeof(pp_state_fields) / sizeof(pp_state_fields[0]))),
};

// ===========================================================================
// Preset Save — new API
// ===========================================================================

gX_I32 gX_RendererPostProcess_Preset_Save_New(gX_RendererPostProcess *pp, const gX_I8 *path)
{
    gX_TraceLog_POSTPROCESS(gX_LOG_INFO,
        "RENDERER_PP: Saving preset (new schema) to %s", path);

    struct json_object *root = json_object_new_object();

    // Meta fields
    json_object_object_add(root, "name",
        json_object_new_string(pp->name));
    json_object_object_add(root, "description",
        json_object_new_string(pp->description));

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    gX_I8 date[64], timeStr[64];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm);
    json_object_object_add(root, "created_date",
        json_object_new_string(date));
    json_object_object_add(root, "created_time",
        json_object_new_string(timeStr));

    // Schema-driven flat fields (state booleans)
    gX_U32 fc = (gX_U32)(sizeof(pp_root_fields) / sizeof(pp_root_fields[0]));
    gX_Schema_JSON_SaveToObj(pp_root_fields, fc, (gX_I0*)pp, root);

    // Bridge sections (inject, shaders, texture)
    pp_sections_to_json(pp, root);

    // Write to file
    gX_I32 flags = JSON_C_TO_STRING_SPACED |
                   JSON_C_TO_STRING_PRETTY |
                   JSON_C_TO_STRING_PRETTY_TAB;
    gX_I32 ret = json_object_to_file_ext(path, root, flags);
    json_object_put(root);

    if (ret == 0)
        gX_TraceLog_POSTPROCESS(gX_LOG_INFO, "RENDERER_PP: Save done");
    else
        gX_TraceLog_POSTPROCESS(gX_LOG_ERROR, "RENDERER_PP: Save failed");
    return ret == 0 ? 0 : -1;
}

// ===========================================================================
// Preset Load — new API
// ===========================================================================

gX_I32 gX_RendererPostProcess_Preset_Load_New(gX_RendererPostProcess *pp, const gX_I8 *path)
{
    gX_TraceLog_POSTPROCESS(gX_LOG_INFO,
        "RENDERER_PP: Loading preset (new schema) from %s", path);

    struct json_object *root = json_object_from_file(path);
    if (!root)
    {
        gX_TraceLog_POSTPROCESS(gX_LOG_ERROR,
            "RENDERER_PP: Failed to load preset from %s", path);
        return 1;
    }

    // Schema-driven flat fields (state booleans)
    gX_U32 fc = (gX_U32)(sizeof(pp_root_fields) / sizeof(pp_root_fields[0]));
    gX_Schema_JSON_LoadFromObj(pp_root_fields, fc, (gX_I0*)pp, root);

    // Bridge sections (inject, shaders, texture)
    pp_sections_from_json(pp, root);

    json_object_put(root);

    gX_TraceLog_POSTPROCESS(gX_LOG_INFO, "RENDERER_PP: Load done");
    return 0;
}
