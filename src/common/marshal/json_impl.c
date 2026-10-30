/**********************************************************************************************
*
*   [gX: src/common/marshal/json_impl.c]
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


//opencode: self-contained JSON implementation replacing vendored json-c.
//   Matches json-c semantics for the subset gX uses:
//   - refcounted objects (new_* → 1, put → free at 0)
//   - array_add/put_idx/object_add take ownership (no incref)
//   - get_idx/object_get return borrowed refs
//   - ints stored as int64; get_int/get_int64/get_double convert
//   - doubles serialize with %.17g + ".0" when integral (round-trips as double)
//   - strings escape ", \, /, control chars (\b\f\n\r\t + \u00XX)
//   - duplicate object keys: last-wins, insertion order preserved
//   - null → NULL pointer (json_type_null only for NULL)
//   - parse errors → NULL (settings falls back to defaults)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "json_impl.h"

typedef struct json_object_entry {
    char *key;
    struct json_object *value;
} json_object_entry;

struct json_object {
    enum json_type o_type;
    int o_refcount;
    union {
        int c_boolean;
        int64_t c_int64;
        double c_double;
        struct { char *str; int len; } c_string;
        struct { struct json_object **elements; size_t size, capacity; } c_array;
        struct { json_object_entry *entries; size_t size, capacity; } c_object;
    } o;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static struct json_object *json_object_new(enum json_type type) {
    struct json_object *obj = (struct json_object *)calloc(1, sizeof(*obj));
    if (!obj) return NULL;
    obj->o_type = type;
    obj->o_refcount = 1;
    return obj;
}

static void json_object_free(struct json_object *obj) {
    switch (obj->o_type) {
        case json_type_string:
            free(obj->o.c_string.str);
            break;
        case json_type_array:
            for (size_t i = 0; i < obj->o.c_array.size; i++)
                json_object_put(obj->o.c_array.elements[i]);
            free(obj->o.c_array.elements);
            break;
        case json_type_object:
            for (size_t i = 0; i < obj->o.c_object.size; i++) {
                free(obj->o.c_object.entries[i].key);
                json_object_put(obj->o.c_object.entries[i].value);
            }
            free(obj->o.c_object.entries);
            break;
        default:
            break;
    }
    free(obj);
}

int json_object_put(struct json_object *obj) {
    if (!obj) return 0;
    if (--obj->o_refcount <= 0) {
        json_object_free(obj);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

struct json_object *json_object_new_object(void) { return json_object_new(json_type_object); }
struct json_object *json_object_new_array(void)  { return json_object_new(json_type_array); }

struct json_object *json_object_new_boolean(json_bool b) {
    struct json_object *o = json_object_new(json_type_boolean);
    if (o) o->o.c_boolean = b ? 1 : 0;
    return o;
}

struct json_object *json_object_new_int(int32_t i) {
    struct json_object *o = json_object_new(json_type_int);
    if (o) o->o.c_int64 = i;
    return o;
}

struct json_object *json_object_new_int64(int64_t i) {
    struct json_object *o = json_object_new(json_type_int);
    if (o) o->o.c_int64 = i;
    return o;
}

struct json_object *json_object_new_double(double d) {
    struct json_object *o = json_object_new(json_type_double);
    if (o) o->o.c_double = d;
    return o;
}

struct json_object *json_object_new_string(const char *s) {
    if (!s) s = "";
    struct json_object *o = json_object_new(json_type_string);
    if (!o) return NULL;
    o->o.c_string.len = (int)strlen(s);
    o->o.c_string.str = (char *)malloc((size_t)o->o.c_string.len + 1);
    if (!o->o.c_string.str) { free(o); return NULL; }
    memcpy(o->o.c_string.str, s, (size_t)o->o.c_string.len + 1);
    return o;
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

enum json_type json_object_get_type(const struct json_object *obj) {
    return obj ? obj->o_type : json_type_null;
}

json_bool json_object_get_boolean(const struct json_object *obj) {
    if (!obj) return 0;
    switch (obj->o_type) {
        case json_type_boolean: return obj->o.c_boolean;
        case json_type_int:     return obj->o.c_int64 != 0;
        case json_type_double:  return obj->o.c_double != 0.0;
        default:                return 0;
    }
}

int32_t json_object_get_int(const struct json_object *obj) {
    if (!obj) return 0;
    switch (obj->o_type) {
        case json_type_int:     return (int32_t)obj->o.c_int64;
        case json_type_double:  return (int32_t)obj->o.c_double;
        case json_type_boolean: return obj->o.c_boolean;
        default:                return 0;
    }
}

int64_t json_object_get_int64(const struct json_object *obj) {
    if (!obj) return 0;
    switch (obj->o_type) {
        case json_type_int:     return obj->o.c_int64;
        case json_type_double:  return (int64_t)obj->o.c_double;
        case json_type_boolean: return obj->o.c_boolean;
        default:                return 0;
    }
}

double json_object_get_double(const struct json_object *obj) {
    if (!obj) return 0.0;
    switch (obj->o_type) {
        case json_type_int:     return (double)obj->o.c_int64;
        case json_type_double:  return obj->o.c_double;
        case json_type_boolean: return obj->o.c_boolean;
        default:                return 0.0;
    }
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

size_t json_object_array_length(const struct json_object *obj) {
    return (obj && obj->o_type == json_type_array) ? obj->o.c_array.size : 0;
}

struct json_object *json_object_array_get_idx(const struct json_object *obj, size_t idx) {
    if (!obj || obj->o_type != json_type_array || idx >= obj->o.c_array.size) return NULL;
    return obj->o.c_array.elements[idx];
}

static int array_reserve(struct json_object *obj, size_t need) {
    if (need <= obj->o.c_array.capacity) return 0;
    size_t cap = obj->o.c_array.capacity ? obj->o.c_array.capacity : 4;
    while (cap < need) cap *= 2;
    struct json_object **ne = (struct json_object **)realloc(obj->o.c_array.elements, cap * sizeof(*ne));
    if (!ne) return -1;
    obj->o.c_array.elements = ne;
    obj->o.c_array.capacity = cap;
    return 0;
}

int json_object_array_add(struct json_object *obj, struct json_object *val) {
    if (!obj || obj->o_type != json_type_array) return -1;
    if (array_reserve(obj, obj->o.c_array.size + 1) != 0) return -1;
    obj->o.c_array.elements[obj->o.c_array.size++] = val;
    return 0;
}

int json_object_array_put_idx(struct json_object *obj, size_t idx, struct json_object *val) {
    if (!obj || obj->o_type != json_type_array) return -1;
    if (idx == obj->o.c_array.size) return json_object_array_add(obj, val);
    if (idx > obj->o.c_array.size) return -1;
    json_object_put(obj->o.c_array.elements[idx]);
    obj->o.c_array.elements[idx] = val;
    return 0;
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

struct json_object *json_object_object_get(const struct json_object *obj, const char *key) {
    if (!obj || obj->o_type != json_type_object || !key) return NULL;
    for (size_t i = 0; i < obj->o.c_object.size; i++) {
        if (strcmp(obj->o.c_object.entries[i].key, key) == 0)
            return obj->o.c_object.entries[i].value;
    }
    return NULL;
}

int json_object_object_get_ex(const struct json_object *obj, const char *key, struct json_object **value) {
    struct json_object *v = json_object_object_get(obj, key);
    if (v) { if (value) *value = v; return 1; }
    return 0;
}

int json_object_object_add(struct json_object *obj, const char *key, struct json_object *val) {
    if (!obj || obj->o_type != json_type_object || !key) return -1;
    for (size_t i = 0; i < obj->o.c_object.size; i++) {
        if (strcmp(obj->o.c_object.entries[i].key, key) == 0) {
            json_object_put(obj->o.c_object.entries[i].value);
            obj->o.c_object.entries[i].value = val;
            return 0;
        }
    }
    if (obj->o.c_object.size >= obj->o.c_object.capacity) {
        size_t cap = obj->o.c_object.capacity ? obj->o.c_object.capacity * 2 : 8;
        json_object_entry *ne = (json_object_entry *)realloc(obj->o.c_object.entries, cap * sizeof(*ne));
        if (!ne) return -1;
        obj->o.c_object.entries = ne;
        obj->o.c_object.capacity = cap;
    }
    char *k = (char *)malloc(strlen(key) + 1);
    if (!k) return -1;
    strcpy(k, key);
    obj->o.c_object.entries[obj->o.c_object.size].key = k;
    obj->o.c_object.entries[obj->o.c_object.size].value = val;
    obj->o.c_object.size++;
    return 0;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sbuf;

static int sbuf_reserve(sbuf *s, size_t extra) {
    if (s->len + extra + 1 <= s->cap) return 0;
    size_t cap = s->cap ? s->cap : 64;
    while (cap < s->len + extra + 1) cap *= 2;
    char *nb = (char *)realloc(s->buf, cap);
    if (!nb) return -1;
    s->buf = nb;
    s->cap = cap;
    return 0;
}

static int sbuf_append(sbuf *s, const char *data, size_t n) {
    if (sbuf_reserve(s, n) != 0) return -1;
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return 0;
}

static int sbuf_appendc(sbuf *s, char c) { return sbuf_append(s, &c, 1); }

static const char json_hex_chars[] = "0123456789abcdef";

static int sbuf_escape(sbuf *s, const char *str, size_t len, int flags) {
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        const char *esc = NULL;
        char sbuf7[7];
        switch (c) {
            case '\b': esc = "\\b"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            case '\f': esc = "\\f"; break;
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '/':
                if (flags & JSON_C_TO_STRING_NOSLASHESCAPE) continue;
                esc = "\\/"; break;
            default:
                if (c < ' ') {
                    snprintf(sbuf7, sizeof(sbuf7), "\\u00%c%c",
                             json_hex_chars[c >> 4], json_hex_chars[c & 0xf]);
                    esc = sbuf7;
                } else {
                    continue;
                }
        }
        if (i > start) sbuf_append(s, str + start, i - start);
        sbuf_append(s, esc, strlen(esc));
        start = i + 1;
    }
    if (start < len) sbuf_append(s, str + start, len - start);
    return 0;
}

static int sbuf_double(sbuf *s, double d) {
    char buf[64];
    if (isnan(d)) return sbuf_append(s, "NaN", 3);
    if (isinf(d)) return sbuf_append(s, d > 0 ? "Infinity" : "-Infinity", d > 0 ? 8 : 9);
    int n = snprintf(buf, sizeof(buf), "%.17g", d);
    if (n < 0) return -1;
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
        if (sbuf_append(s, buf, (size_t)n) != 0) return -1;
        return sbuf_append(s, ".0", 2);
    }
    return sbuf_append(s, buf, (size_t)n);
}

static int json_serialize(sbuf *s, const struct json_object *obj, int level, int flags) {
    if (!obj) return sbuf_append(s, "null", 4);
    int pretty = (flags & JSON_C_TO_STRING_PRETTY) != 0;
    int spaced = (flags & JSON_C_TO_STRING_SPACED) != 0;
    const char *indent = (flags & JSON_C_TO_STRING_PRETTY_TAB) ? "\t" : "  ";
    switch (obj->o_type) {
        case json_type_null:
            return sbuf_append(s, "null", 4);
        case json_type_boolean:
            return sbuf_append(s, obj->o.c_boolean ? "true" : "false", obj->o.c_boolean ? 4 : 5);
        case json_type_int: {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld", (long long)obj->o.c_int64);
            return sbuf_append(s, buf, (size_t)n);
        }
        case json_type_double:
            return sbuf_double(s, obj->o.c_double);
        case json_type_string:
            if (sbuf_appendc(s, '"') != 0) return -1;
            if (sbuf_escape(s, obj->o.c_string.str, (size_t)obj->o.c_string.len, flags) != 0) return -1;
            return sbuf_appendc(s, '"');
        case json_type_array: {
            if (obj->o.c_array.size == 0) return sbuf_append(s, "[]", 2);
            if (sbuf_appendc(s, '[') != 0) return -1;
            for (size_t i = 0; i < obj->o.c_array.size; i++) {
                if (i > 0 && sbuf_appendc(s, ',') != 0) return -1;
                if (pretty) {
                    if (sbuf_appendc(s, '\n') != 0) return -1;
                    for (int l = 0; l <= level; l++)
                        if (sbuf_append(s, indent, strlen(indent)) != 0) return -1;
                } else if (spaced) {
                    if (sbuf_appendc(s, ' ') != 0) return -1;
                }
                if (json_serialize(s, obj->o.c_array.elements[i], level + 1, flags) != 0) return -1;
            }
            if (pretty) {
                if (sbuf_appendc(s, '\n') != 0) return -1;
                for (int l = 0; l < level; l++)
                    if (sbuf_append(s, indent, strlen(indent)) != 0) return -1;
            } else if (spaced) {
                if (sbuf_appendc(s, ' ') != 0) return -1;
            }
            return sbuf_appendc(s, ']');
        }
        case json_type_object: {
            if (obj->o.c_object.size == 0) return sbuf_append(s, "{}", 2);
            if (sbuf_appendc(s, '{') != 0) return -1;
            for (size_t i = 0; i < obj->o.c_object.size; i++) {
                if (i > 0 && sbuf_appendc(s, ',') != 0) return -1;
                if (pretty) {
                    if (sbuf_appendc(s, '\n') != 0) return -1;
                    for (int l = 0; l <= level; l++)
                        if (sbuf_append(s, indent, strlen(indent)) != 0) return -1;
                } else if (spaced) {
                    if (sbuf_appendc(s, ' ') != 0) return -1;
                }
                if (sbuf_appendc(s, '"') != 0) return -1;
                if (sbuf_escape(s, obj->o.c_object.entries[i].key,
                                strlen(obj->o.c_object.entries[i].key), flags) != 0) return -1;
                if (sbuf_appendc(s, '"') != 0) return -1;
                if (sbuf_appendc(s, ':') != 0) return -1;
                if (spaced && sbuf_appendc(s, ' ') != 0) return -1;
                if (json_serialize(s, obj->o.c_object.entries[i].value, level + 1, flags) != 0) return -1;
            }
            if (pretty) {
                if (sbuf_appendc(s, '\n') != 0) return -1;
                for (int l = 0; l < level; l++)
                    if (sbuf_append(s, indent, strlen(indent)) != 0) return -1;
            } else if (spaced) {
                if (sbuf_appendc(s, ' ') != 0) return -1;
            }
            return sbuf_appendc(s, '}');
        }
    }
    return -1;
}

const char *json_object_to_json_string_ext(struct json_object *obj, int flags) {
    static sbuf s = {0};
    s.len = 0;
    if (json_serialize(&s, obj, 0, flags) != 0) return "null";
    return s.buf;
}

const char *json_object_to_json_string(struct json_object *obj) {
    return json_object_to_json_string_ext(obj, JSON_C_TO_STRING_SPACED);
}

const char *json_object_get_string(struct json_object *obj) {
    if (!obj) return "null";
    if (obj->o_type == json_type_string) return obj->o.c_string.str;
    return json_object_to_json_string(obj);
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

typedef struct {
    const char *p;
    const char *end;
    size_t depth;
} parser;

static void skip_ws(parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
        ps->p++;
}

static struct json_object *parse_value(parser *ps);

//ai: reserve a whole decoded character before appending so OOM cannot truncate a parsed string.
static struct json_object *parse_string(parser *ps) {
    ps->p++;
    sbuf s = {0};
    int closed = 0;
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == '"') { ps->p++; closed = 1; break; }
        if (sbuf_reserve(&s, 4) != 0) { free(s.buf); return NULL; }
        if (c == '\\') {
            ps->p++;
            if (ps->p >= ps->end) break;
            char e = *ps->p++;
            switch (e) {
                case '"':  sbuf_appendc(&s, '"');  break;
                case '\\': sbuf_appendc(&s, '\\'); break;
                case '/':  sbuf_appendc(&s, '/');  break;
                case 'b':  sbuf_appendc(&s, '\b'); break;
                case 'f':  sbuf_appendc(&s, '\f'); break;
                case 'n':  sbuf_appendc(&s, '\n'); break;
                case 'r':  sbuf_appendc(&s, '\r'); break;
                case 't':  sbuf_appendc(&s, '\t'); break;
                case 'u': {
                    if (ps->p + 4 > ps->end) { free(s.buf); return NULL; }
                    unsigned cp = 0;
                    int ok = 1;
                    for (int i = 0; i < 4; i++) {
                        char h = ps->p[i];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else { ok = 0; break; }
                    }
                    if (!ok) { free(s.buf); return NULL; }
                    ps->p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && ps->p + 6 <= ps->end &&
                        ps->p[0] == '\\' && ps->p[1] == 'u') {
                        unsigned lo = 0;
                        int lok = 1;
                        for (int i = 0; i < 4; i++) {
                            char h = ps->p[2 + i];
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                            else { lok = 0; break; }
                        }
                        if (lok && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            ps->p += 6;
                        }
                    }
                    if (cp < 0x80) {
                        sbuf_appendc(&s, (char)cp);
                    } else if (cp < 0x800) {
                        sbuf_appendc(&s, (char)(0xC0 | (cp >> 6)));
                        sbuf_appendc(&s, (char)(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        sbuf_appendc(&s, (char)(0xE0 | (cp >> 12)));
                        sbuf_appendc(&s, (char)(0x80 | ((cp >> 6) & 0x3F)));
                        sbuf_appendc(&s, (char)(0x80 | (cp & 0x3F)));
                    } else {
                        sbuf_appendc(&s, (char)(0xF0 | (cp >> 18)));
                        sbuf_appendc(&s, (char)(0x80 | ((cp >> 12) & 0x3F)));
                        sbuf_appendc(&s, (char)(0x80 | ((cp >> 6) & 0x3F)));
                        sbuf_appendc(&s, (char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default:
                    free(s.buf);
                    return NULL;
            }
        } else {
            sbuf_appendc(&s, c);
            ps->p++;
        }
    }
    if (!closed) { free(s.buf); return NULL; }
    struct json_object *o = json_object_new(json_type_string);
    if (!o) { free(s.buf); return NULL; }
    o->o.c_string.str = s.buf ? s.buf : strdup("");
    if (!o->o.c_string.str) { json_object_put(o); return NULL; }
    o->o.c_string.len = (int)s.len;
    return o;
}

//ai: enforce JSON number grammar and reject overflow to non-finite double values.
static struct json_object *parse_number(parser *ps) {
    const char *start = ps->p;
    int is_double = 0;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') return NULL;
    if (*ps->p == '0') ps->p++;
    else while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    if (ps->p < ps->end && *ps->p == '.') {
        is_double = 1;
        ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') return NULL;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        is_double = 1;
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') return NULL;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    if (ps->p == start) return NULL;
    size_t len = (size_t)(ps->p - start);
    char buf[64];
    if (len >= sizeof(buf)) return NULL;
    memcpy(buf, start, len);
    buf[len] = '\0';
    if (is_double)
    {
        double value = strtod(buf, NULL);
        return isfinite(value) ? json_object_new_double(value) : NULL;
    }
    errno = 0;
    long long v = strtoll(buf, NULL, 10);
    if (errno == ERANGE)
    {
        double value = strtod(buf, NULL);
        return isfinite(value) ? json_object_new_double(value) : NULL;
    }
    return json_object_new_int64((int64_t)v);
}

//ai: insertion failure must reject the file and release the unowned parsed value.
static struct json_object *parse_array(parser *ps) {
    ps->p++;
    struct json_object *arr = json_object_new_array();
    if (!arr) return NULL;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    while (1) {
        skip_ws(ps);
        struct json_object *v;
        //opencode: length-guard the memcmp — the old p<end check allowed a
        //   4-byte read with 1..3 bytes left (ASan heap-buffer-overflow).
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
            ps->p += 4;
            v = NULL;
        } else {
            v = parse_value(ps);
            if (!v) { json_object_put(arr); return NULL; }
        }
        if (json_object_array_add(arr, v) != 0)
        {
            json_object_put(v);
            json_object_put(arr);
            return NULL;
        }
        skip_ws(ps);
        if (ps->p >= ps->end) { json_object_put(arr); return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; return arr; }
        json_object_put(arr);
        return NULL;
    }
}

//ai: propagate failed object insertions instead of returning a partial configuration.
static struct json_object *parse_object(parser *ps) {
    ps->p++;
    struct json_object *obj = json_object_new_object();
    if (!obj) return NULL;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    while (1) {
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') { json_object_put(obj); return NULL; }
        struct json_object *keyo = parse_string(ps);
        if (!keyo) { json_object_put(obj); return NULL; }
        const char *key = json_object_get_string(keyo);
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') { json_object_put(keyo); json_object_put(obj); return NULL; }
        ps->p++;
        skip_ws(ps);
        struct json_object *v;
        //opencode: length-guard the memcmp — the old p<end check allowed a
        //   4-byte read with 1..3 bytes left (ASan heap-buffer-overflow).
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
            ps->p += 4;
            v = NULL;
        } else {
            v = parse_value(ps);
            if (!v) { json_object_put(keyo); json_object_put(obj); return NULL; }
        }
        if (json_object_object_add(obj, key, v) != 0)
        {
            json_object_put(v);
            json_object_put(keyo);
            json_object_put(obj);
            return NULL;
        }
        json_object_put(keyo);
        skip_ws(ps);
        if (ps->p >= ps->end) { json_object_put(obj); return NULL; }
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return obj; }
        json_object_put(obj);
        return NULL;
    }
}

//ai: bound container recursion before descending, including mixed object/array nesting.
static struct json_object *parse_value(parser *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) return NULL;
    char c = *ps->p;
    if (c == '{' || c == '[')
    {
        if (ps->depth >= GX_JSON_MAX_DEPTH) return NULL;
        ps->depth++;
        struct json_object *value = c == '{' ? parse_object(ps) : parse_array(ps);
        ps->depth--;
        return value;
    }
    if (c == '"') return parse_string(ps);
    if (c == 't') {
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) { ps->p += 4; return json_object_new_boolean(1); }
        return NULL;
    }
    if (c == 'f') {
        if (ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) { ps->p += 5; return json_object_new_boolean(0); }
        return NULL;
    }
    if (c == 'n') {
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) { ps->p += 4; return NULL; }
        return NULL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(ps);
    return NULL;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

//ai: cap allocation before reading, require a complete read and consume the entire document.
struct json_object *json_object_from_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > GX_JSON_MAX_FILE_BYTES) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    int read_failed = ferror(f) || rd != (size_t)sz;
    fclose(f);
    if (read_failed) { free(buf); return NULL; }
    buf[rd] = '\0';
    parser ps = { buf, buf + rd, 0 };
    struct json_object *root = parse_value(&ps);
    skip_ws(&ps);
    if (ps.p != ps.end)
    {
        json_object_put(root);
        root = NULL;
    }
    free(buf);
    return root;
}

int json_object_to_file_ext(const char *filename, struct json_object *obj, int flags) {
    const char *str = json_object_to_json_string_ext(obj, flags);
    if (!str) return -1;
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    size_t len = strlen(str);
    size_t w = fwrite(str, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}
