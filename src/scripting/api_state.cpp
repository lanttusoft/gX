/**********************************************************************************************
*
*   [gX: src/scripting/api_state.cpp]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu, opencode
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


//ai: ported from gameX src/scripting/api_state.cpp — the KV store (fixed
//   slots, name-deduped, typed) and its ScriptSet*/ScriptGet* bindings. The
//   json-c serialization helpers stayed out (gX has no game-save system);
//   the state survives hot-reload in-process.
#include "api_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>
#include <string.h>

#include <angelscript.h>

#include "../common/log.h"
#include "../common/marshal/json_impl.h"
#include "../scripting.h"

#define AS_CHECK(r) do { if (r < 0) \
    gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "AS state registration failed (code %d)", r); \
} while(0)

// ========================================================================
// KV store implementation
// ========================================================================

gX_I0 gX_Scripting_State_Clear(gX_I0) {
    memset(g_scripting.stateUsed, 0, sizeof(g_scripting.stateUsed));
    memset(g_scripting.stateNames, 0, sizeof(g_scripting.stateNames));
    memset(g_scripting.stateTypes, 0, sizeof(g_scripting.stateTypes));
    g_scripting.stateCount = 0;
}

static gX_I32 gX_Scripting_State_Find(const gX_I8 *name) {
    if (!name || !name[0]) return -1;
    for (gX_U32 i = 0; i < GX_SCRIPT_STATE_MAX; i++) {
        if (g_scripting.stateUsed[i] && strcmp(g_scripting.stateNames[i], name) == 0)
            return (gX_I32)i;
    }
    return -1;
}

static gX_I32 gX_Scripting_State_Alloc(const gX_I8 *name) {
    gX_I32 idx = gX_Scripting_State_Find(name);
    if (idx >= 0) return idx;
    if (g_scripting.stateCount >= GX_SCRIPT_STATE_MAX) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "script state full (%d)", GX_SCRIPT_STATE_MAX);
        return -1;
    }
    for (gX_U32 i = 0; i < GX_SCRIPT_STATE_MAX; i++) {
        if (!g_scripting.stateUsed[i]) {
            g_scripting.stateUsed[i] = 1;
            strncpy(g_scripting.stateNames[i], name, sizeof(g_scripting.stateNames[i]) - 1);
            g_scripting.stateNames[i][sizeof(g_scripting.stateNames[i]) - 1] = '\0';
            g_scripting.stateCount++;
            return (gX_I32)i;
        }
    }
    return -1;
}

gX_I1 gX_Scripting_State_SetInt(const gX_I8 *name, gX_I32 value) {
    gX_I32 idx = gX_Scripting_State_Alloc(name);
    if (idx < 0) return 0;
    g_scripting.stateTypes[idx] = 'i';
    g_scripting.stateInts[idx] = value;
    return 1;
}

gX_I1 gX_Scripting_State_SetFloat(const gX_I8 *name, gX_F32 value) {
    gX_I32 idx = gX_Scripting_State_Alloc(name);
    if (idx < 0) return 0;
    g_scripting.stateTypes[idx] = 'f';
    g_scripting.stateFloats[idx] = value;
    return 1;
}

gX_I1 gX_Scripting_State_SetString(const gX_I8 *name, const gX_I8 *value) {
    gX_I32 idx = gX_Scripting_State_Alloc(name);
    if (idx < 0) return 0;
    g_scripting.stateTypes[idx] = 's';
    strncpy(g_scripting.stateStrings[idx], value ? value : "", sizeof(g_scripting.stateStrings[idx]) - 1);
    g_scripting.stateStrings[idx][sizeof(g_scripting.stateStrings[idx]) - 1] = '\0';
    return 1;
}

gX_I32 gX_Scripting_State_GetInt(const gX_I8 *name, gX_I32 def) {
    gX_I32 idx = gX_Scripting_State_Find(name);
    if (idx < 0) return def;
    return (g_scripting.stateTypes[idx] == 'i') ? g_scripting.stateInts[idx] : def;
}

gX_F32 gX_Scripting_State_GetFloat(const gX_I8 *name, gX_F32 def) {
    gX_I32 idx = gX_Scripting_State_Find(name);
    if (idx < 0) return def;
    return (g_scripting.stateTypes[idx] == 'f') ? g_scripting.stateFloats[idx] : def;
}

const gX_I8 *gX_Scripting_State_GetString(const gX_I8 *name) {
    gX_I32 idx = gX_Scripting_State_Find(name);
    if (idx < 0) return "";
    return (g_scripting.stateTypes[idx] == 's') ? g_scripting.stateStrings[idx] : "";
}

gX_I1 gX_Scripting_State_Has(const gX_I8 *name) {
    return gX_Scripting_State_Find(name) >= 0;
}

// ========================================================================
// File persistence — the KV store serialized as a JSON array of typed
//   entries (the gameX format). Default home: save/script_state.json
//   (GX_SCRIPT_STATE=<path> overrides, GX_SCRIPT_STATE=0 disables).
// ========================================================================

static const gX_I8 *gX_Scripting_State_Path(void) {
    const gX_I8 *env = getenv("GX_SCRIPT_STATE");
    if (env && env[0]) {
        if (strcmp(env, "0") == 0) return NULL;
        return env;
    }
    return "save/script_state.json";
}

static gX_I0 gX_Scripting_State_EnsureParentDir(const gX_I8 *path) {
    const gX_I8 *slash = strrchr(path, '/');
    if (!slash || slash == path) return;
    gX_I8 dir[512];
    size_t len = (size_t)(slash - path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, path, len);
    dir[len] = '\0';
    mkdir(dir, 0755);
}

struct json_object *gX_Scripting_State_ToJSON(gX_I0) {
    struct json_object *arr = json_object_new_array();
    if (!arr) return NULL;
    for (gX_U32 i = 0; i < GX_SCRIPT_STATE_MAX; i++) {
        if (!g_scripting.stateUsed[i]) continue;
        struct json_object *entry = json_object_new_object();
        if (!entry) continue;
        json_object_object_add(entry, "name", json_object_new_string(g_scripting.stateNames[i]));
        gX_I8 typeStr[2] = { g_scripting.stateTypes[i], '\0' };
        json_object_object_add(entry, "type", json_object_new_string(typeStr));
        switch (g_scripting.stateTypes[i]) {
            case 'i': json_object_object_add(entry, "value", json_object_new_int(g_scripting.stateInts[i])); break;
            case 'f': json_object_object_add(entry, "value", json_object_new_double(g_scripting.stateFloats[i])); break;
            case 's': json_object_object_add(entry, "value", json_object_new_string(g_scripting.stateStrings[i])); break;
            default: json_object_put(entry); continue;
        }
        json_object_array_add(arr, entry);
    }
    return arr;
}

gX_I0 gX_Scripting_State_FromJSON(struct json_object *obj) {
    if (!obj) return;
    gX_Scripting_State_Clear();
    size_t n = json_object_array_length(obj);
    for (size_t i = 0; i < n; i++) {
        struct json_object *entry = json_object_array_get_idx(obj, i);
        struct json_object *val;
        if (!entry || !json_object_object_get_ex(entry, "name", &val)) continue;
        const gX_I8 *name = json_object_get_string(val);
        if (!name || !name[0]) continue;
        const gX_I8 *type = "";
        if (json_object_object_get_ex(entry, "type", &val)) type = json_object_get_string(val);
        if (!json_object_object_get_ex(entry, "value", &val)) continue;
        if (type && type[0] == 'i') gX_Scripting_State_SetInt(name, json_object_get_int(val));
        else if (type && type[0] == 'f') gX_Scripting_State_SetFloat(name, (gX_F32)json_object_get_double(val));
        else if (type && type[0] == 's') gX_Scripting_State_SetString(name, json_object_get_string(val));
    }
}

gX_I0 gX_Scripting_State_Save(gX_I0) {
    const gX_I8 *path = gX_Scripting_State_Path();
    if (!path) return;

    struct json_object *arr = gX_Scripting_State_ToJSON();
    if (!arr) return;
    const gX_I8 *text = json_object_to_json_string_ext(arr, JSON_C_TO_STRING_PRETTY);
    if (!text) {
        json_object_put(arr);
        return;
    }

    gX_Scripting_State_EnsureParentDir(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        gX_TraceLog_SCRIPTING(gX_LOG_WARNING, "STATE: cannot write '%s'", path);
        json_object_put(arr);
        return;
    }
    fprintf(f, "%s\n", text);
    fclose(f);
    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "STATE: saved '%s' (%u entries)", path, g_scripting.stateCount);
    json_object_put(arr);
}

gX_I0 gX_Scripting_State_Load(gX_I0) {
    const gX_I8 *path = gX_Scripting_State_Path();
    if (!path) return;

    struct json_object *arr = json_object_from_file(path);
    if (!arr) return;
    gX_Scripting_State_FromJSON(arr);
    json_object_put(arr);
    gX_TraceLog_SCRIPTING(gX_LOG_INFO, "STATE: loaded '%s' (%u entries)", path, g_scripting.stateCount);
}

// ========================================================================
// Script-facing registration
// ========================================================================

static void Script_State_SetInt(const std::string &name, int value) {
    gX_Scripting_State_SetInt(name.c_str(), value);
}
static int Script_State_GetInt(const std::string &name, int def) {
    return gX_Scripting_State_GetInt(name.c_str(), def);
}
static void Script_State_SetFloat(const std::string &name, float value) {
    gX_Scripting_State_SetFloat(name.c_str(), value);
}
static float Script_State_GetFloat(const std::string &name, float def) {
    return gX_Scripting_State_GetFloat(name.c_str(), def);
}
static void Script_State_SetString(const std::string &name, const std::string &value) {
    gX_Scripting_State_SetString(name.c_str(), value.c_str());
}
static std::string Script_State_GetString(const std::string &name) {
    return std::string(gX_Scripting_State_GetString(name.c_str()));
}
static bool Script_State_Has(const std::string &name) {
    return gX_Scripting_State_Has(name.c_str()) != 0;
}
static void Script_State_Clear(void) {
    gX_Scripting_State_Clear();
}
static void Script_State_Save(void) {
    gX_Scripting_State_Save();
}
static void Script_State_Load(void) {
    gX_Scripting_State_Load();
}

gX_I32 gX_Scripting_RegisterStateAPI(asIScriptEngine *engine) {
    int r = 0;

    r = engine->RegisterGlobalFunction("void ScriptSetInt(const string &in, int)",
        asFUNCTION(Script_State_SetInt), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("int ScriptGetInt(const string &in, int)",
        asFUNCTION(Script_State_GetInt), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void ScriptSetFloat(const string &in, float)",
        asFUNCTION(Script_State_SetFloat), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("float ScriptGetFloat(const string &in, float)",
        asFUNCTION(Script_State_GetFloat), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void ScriptSetString(const string &in, const string &in)",
        asFUNCTION(Script_State_SetString), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("string ScriptGetString(const string &in)",
        asFUNCTION(Script_State_GetString), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("bool ScriptHas(const string &in)",
        asFUNCTION(Script_State_Has), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void ScriptClearState()",
        asFUNCTION(Script_State_Clear), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void ScriptSaveState()",
        asFUNCTION(Script_State_Save), asCALL_CDECL); AS_CHECK(r);
    r = engine->RegisterGlobalFunction("void ScriptLoadState()",
        asFUNCTION(Script_State_Load), asCALL_CDECL); AS_CHECK(r);

    gX_TraceLog_SCRIPTING(gX_LOG_DEBUG, "registered State API");
    return 0;
}
