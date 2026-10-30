/**********************************************************************************************
*
*   [gX: src/common/marshal/json_impl.h]
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


//opencode: self-contained JSON implementation replacing the vendored json-c
//   library (9,389 lines → ~1,000). Implements the exact subset of the json-c
//   API that gX's marshal layer uses (json.c, schema_json.c, synth_schema.c,
//   fractal_preset.c, postprocess/gfx schema.c). Drop-in — call sites unchanged.

#ifndef JSON_IMPL_H
#define JSON_IMPL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int json_bool;

typedef enum json_type {
    json_type_null,
    json_type_boolean,
    json_type_double,
    json_type_int,
    json_type_object,
    json_type_array,
    json_type_string
} json_type;

struct json_object;

#define JSON_C_TO_STRING_PLAIN 0
#define JSON_C_TO_STRING_SPACED (1 << 0)
#define JSON_C_TO_STRING_PRETTY (1 << 1)
#define JSON_C_TO_STRING_NOZERO (1 << 2)
#define JSON_C_TO_STRING_PRETTY_TAB (1 << 3)
#define JSON_C_TO_STRING_NOSLASHESCAPE (1 << 4)

//ai: file-backed parsing budgets; ordinary engine presets are far below these limits.
#define GX_JSON_MAX_FILE_BYTES (16u * 1024u * 1024u)
#define GX_JSON_MAX_DEPTH 64u

struct json_object *json_object_new_object(void);
struct json_object *json_object_new_array(void);
struct json_object *json_object_new_boolean(json_bool b);
struct json_object *json_object_new_int(int32_t i);
struct json_object *json_object_new_int64(int64_t i);
struct json_object *json_object_new_double(double d);
struct json_object *json_object_new_string(const char *s);

enum json_type json_object_get_type(const struct json_object *obj);
json_bool json_object_get_boolean(const struct json_object *obj);
int32_t json_object_get_int(const struct json_object *obj);
int64_t json_object_get_int64(const struct json_object *obj);
double json_object_get_double(const struct json_object *obj);
const char *json_object_get_string(struct json_object *obj);

size_t json_object_array_length(const struct json_object *obj);
struct json_object *json_object_array_get_idx(const struct json_object *obj, size_t idx);
int json_object_array_add(struct json_object *obj, struct json_object *val);
int json_object_array_put_idx(struct json_object *obj, size_t idx, struct json_object *val);

struct json_object *json_object_object_get(const struct json_object *obj, const char *key);
int json_object_object_get_ex(const struct json_object *obj, const char *key, struct json_object **value);
int json_object_object_add(struct json_object *obj, const char *key, struct json_object *val);

int json_object_put(struct json_object *obj);

struct json_object *json_object_from_file(const char *filename);
int json_object_to_file_ext(const char *filename, struct json_object *obj, int flags);
const char *json_object_to_json_string(struct json_object *obj);
const char *json_object_to_json_string_ext(struct json_object *obj, int flags);

#ifdef __cplusplus
}
#endif

#endif //JSON_IMPL_H
