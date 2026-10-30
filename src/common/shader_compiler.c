/**********************************************************************************************
*
*   [gX: src/common/shader_compiler.c]
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


//opencode: custom shader preprocessor/compiler

#include "shader_compiler.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "path.h"

#include "../rhi/rhi.h"

#include "log.h"
#include "msg.h"
#include "macros.h"

//opencode: growing buffer for building processed source
typedef struct {
    gX_I8  *data;
    gX_U64  capacity;
    gX_U64  length;
} gX_Shader_Compiler_Buffer;

static gX_I0 bufferAppend(gX_Shader_Compiler_Buffer *buf, const gX_I8 *str, gX_U64 len) {
    if (buf->length + len > buf->capacity) {
        gX_U64 newCap = buf->capacity * 2 + len + 4096;
        gX_I8 *newData = (gX_I8*)realloc(buf->data, (size_t)newCap);
        if (!newData) return;
        buf->data = newData;
        buf->capacity = newCap;
    }
    memcpy(buf->data + buf->length, str, (size_t)len);
    buf->length += len;
}

//opencode: read entire file into allocated buffer
static gX_I8 *readFile(const gX_I8 *path, gX_U64 *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: cannot open '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    gX_I8 *buf = (gX_I8*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t readLen = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[readLen] = '\0';
    if (outLen) *outLen = (gX_U64)readLen + 1;
    return buf;
}

//opencode: extract directory path from file path (mutates input, returns path up to last '/' or '\\')
static gX_I0 getDir(const gX_I8 *path, gX_I8 *dir, gX_U32 maxLen) {
    dir[0] = '\0';
    const gX_I8 *slash = strrchr(path, '/');
    const gX_I8 *bslash = strrchr(path, '\\');
    const gX_I8 *last = (slash > bslash) ? slash : bslash;
    if (!last) return;
    gX_U64 len = (gX_U64)(last - path);
    if (len >= maxLen) len = maxLen - 1;
    memcpy(dir, path, (size_t)len);
    dir[len] = '\0';
}

//opencode: static include paths
static gX_I8 *gX_Shader_Compiler_searchPaths[GX_SHADER_COMPILER_MAX_SEARCH_PATHS];
static gX_I32 gX_Shader_Compiler_searchPathCount = 0;
static gX_I8 *gX_Shader_Compiler_marker = NULL;

//opencode: forward decls — identifier/whitespace helpers defined below
static gX_I1 isIdentStart(gX_I8 c);
static gX_I1 isIdentCont(gX_I8 c);
static const gX_I8 *skipWs(const gX_I8 *p);

//opencode: GLES mode flag — set via gX_Shader_Compiler_SetESMode() at backend
//   init when the context is OpenGL ES (--gles3). Drives the GLSL dialect
//   rewrite in the preprocessor (see version-line handling below).
static gX_I1 gX_Shader_Compiler_esMode = 0;

gX_I0 gX_Shader_Compiler_SetESMode(gX_I1 on) {
    gX_Shader_Compiler_esMode = on;
}

//opencode: desktop legacy GLSL (130/140/150) predates parts of the 330
// dialect — 130 lacks explicit locations, interface blocks, instancing and
// inverse(); 140 adds instancing/inverse; 150 adds interface blocks and
// geometry. 0 = production 330. Kept independent from the selected GFX
// policy level: policy inspection still uses production GLSL 330.
static gX_I32 gX_Shader_Compiler_desktopLegacyGLSL = 0;

gX_I0 gX_Shader_Compiler_SetDesktopLegacyGLSL(gX_I32 version)
{
    gX_Shader_Compiler_desktopLegacyGLSL = version;
}

gX_I32 gX_Shader_Compiler_DesktopLegacyGLSL(gX_I0)
{
    return gX_Shader_Compiler_desktopLegacyGLSL;
}

//opencode: Vulkan mode — the RHI (vk_rhi.c) sets this so the preprocessor
//   emits `#version 450` (Vulkan needs 450, not 330) and the post-process
//   pass gX_Shader_Compiler_ToVulkanDialect() rewrites the rest of the
//   dialect (locations, uniform block, sampler bindings). Mirrors esMode.
static gX_I1 gX_Shader_Compiler_vulkanMode = 0;

gX_I0 gX_Shader_Compiler_SetVulkanMode(gX_I1 on) {
    gX_Shader_Compiler_vulkanMode = on;
}

gX_I1 gX_Shader_Compiler_IsVulkanMode(gX_I0) {
    return gX_Shader_Compiler_vulkanMode;
}

//opencode: detect GLSL interface blocks (`out VS_OUT { ... };` / `in VS_OUT { ... };`).
//   Interface blocks are ES 3.2 (GL_OES_shader_io_blocks) — a shader containing
//   them must be compiled as `#version 320 es`, not 300 es. NVIDIA's ES driver
//   tolerates blocks at 300 es, but llvmpipe/Mesa rejects with
//   "GL_OES_shader_io_blocks or #version 320 required". Distinguishes from a
//   plain `in vec3 vertexPosition;` (no `{`) and from `in vec3 count;` (word
//   boundary). Returns 1 if an `out`/`in` block declaration `{` is found.
static gX_I1 sourceHasInterfaceBlock(const gX_I8 *src) {
    const gX_I8 *p = src;
    while (p && *p) {
        const gX_I8 *k = NULL;
        if (p[0] == 'o' && p[1] == 'u' && p[2] == 't' && !isIdentCont(p[3])) k = p;
        else if (p[0] == 'i' && p[1] == 'n' && !isIdentCont(p[2])) k = p;
        if (k) {
            // keyword must be preceded by non-identifier (start, ws, `)`, `;`, `{`)
            gX_I1 prevOk = (k == src) || !isIdentCont(k[-1]);
            if (prevOk) {
                const gX_I8 *q = skipWs(k + (p[0] == 'o' ? 3 : 2));
                if (isIdentStart(*q)) {
                    while (isIdentCont(*q)) q++;
                    q = skipWs(q);
                    if (*q == '{') return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

//opencode: remove GLSL 330 explicit-layout qualifiers. Legacy GLSL assigns
// attributes/fragment outputs through the program linker instead. Only
// location/binding/storage layouts are stripped — geometry primitive layouts
// (triangles, triangle_strip) are left intact so a misconfigured geometry
// load fails loudly instead of degrading into invalid `in;`/`out;`.
static gX_I0 desktop130StripLayouts(gX_Shader_Compiler_Buffer *out,
    const gX_I8 *src, gX_U64 len)
{
    const gX_I8 *copyStart = src;
    const gX_I8 *search = src;
    const gX_I8 *end = src + len;
    while (search < end) {
        const gX_I8 *found = strstr(search, "layout");
        if (!found || found >= end) break;
        if ((found > src && isIdentCont(found[-1])) || isIdentCont(found[6])) {
            search = found + 6;
            continue;
        }
        const gX_I8 *open = skipWs(found + 6);
        if (open >= end || *open != '(') {
            search = found + 6;
            continue;
        }
        const gX_I8 *close = open + 1;
        gX_I32 depth = 1;
        while (close < end && depth > 0) {
            if (*close == '(') depth++;
            else if (*close == ')') depth--;
            close++;
        }
        if (depth != 0) {
            search = found + 6;
            continue;
        }
        {
            gX_I8 inner[256];
            gX_U64 innerLen = (gX_U64)((close - 1) - (open + 1));
            gX_I1 strippable = 0;
            if (innerLen >= sizeof(inner)) innerLen = sizeof(inner) - 1;
            memcpy(inner, open + 1, (size_t)innerLen);
            inner[innerLen] = '\0';
            if (strstr(inner, "location") != NULL) strippable = 1;
            else if (strstr(inner, "binding") != NULL) strippable = 1;
            else if (strstr(inner, "std430") != NULL) strippable = 1;
            else if (strstr(inner, "std140") != NULL) strippable = 1;
            else if (strstr(inner, "local_size") != NULL) strippable = 1;
            if (!strippable) {
                search = found + 6;
                continue;
            }
        }
        bufferAppend(out, copyStart, (gX_U64)(found - copyStart));
        copyStart = close;
        search = close;
    }
    if (copyStart < end) bufferAppend(out, copyStart, (gX_U64)(end - copyStart));
}

//opencode: replace a desktop-GLSL-330 construct that has no GLSL-130
// equivalent after GFX policy has disabled the feature that depended on it.
static gX_I0 desktop130ReplaceText(gX_Shader_Compiler_Buffer *source,
    const gX_I8 *from, const gX_I8 *to)
{
    gX_Shader_Compiler_Buffer replacement = {0};
    const gX_I8 *p = source->data;
    const gX_I8 *found = NULL;
    while ((found = strstr(p, from)) != NULL) {
        bufferAppend(&replacement, p, (gX_U64)(found - p));
        bufferAppend(&replacement, to, strlen(to));
        p = found + strlen(from);
    }
    if (p == source->data) return;
    bufferAppend(&replacement, p, strlen(p) + 1);
    free(source->data);
    *source = replacement;
}

//opencode: replace one whole-word GLSL identifier (identifier-boundary
// matched, so `inputTex` or `myinput` are untouched). Used for the legacy
// rename of the `input` sampler below.
static gX_I0 desktop130ReplaceWord(gX_Shader_Compiler_Buffer *source,
    const gX_I8 *from, const gX_I8 *to)
{
    gX_U64 fromLen = strlen(from);
    gX_Shader_Compiler_Buffer replacement = {0};
    const gX_I8 *p = source->data;
    const gX_I8 *found = NULL;
    if (fromLen == 0) return;
    while (*p) {
        const gX_I8 *at = strstr(p, from);
        if (!at) break;
        if ((at > source->data && isIdentCont(at[-1])) || isIdentCont(at[fromLen])) {
            bufferAppend(&replacement, p, (gX_U64)(at + 1 - p));
            p = at + 1;
            continue;
        }
        found = at;
        bufferAppend(&replacement, p, (gX_U64)(at - p));
        bufferAppend(&replacement, to, strlen(to));
        p = at + fromLen;
    }
    if (!found) {
        free(replacement.data);
        return;
    }
    bufferAppend(&replacement, p, strlen(p) + 1);
    free(source->data);
    *source = replacement;
}

//opencode: replace a vertex/fragment interface-block instance prefix after
// its fields become legacy standalone varyings.
static gX_I0 desktop130ReplaceMemberPrefix(gX_Shader_Compiler_Buffer *source,
    const gX_I8 *instance)
{
    gX_I8 prefix[96];
    gX_Shader_Compiler_Buffer replacement = {0};
    const gX_I8 *p = source->data;
    const gX_I8 *found = NULL;
    snprintf(prefix, sizeof(prefix), "%s.", instance);
    while ((found = strstr(p, prefix)) != NULL) {
        bufferAppend(&replacement, p, (gX_U64)(found - p));
        p = found + strlen(prefix);
    }
    if (p == source->data) return;
    bufferAppend(&replacement, p, strlen(p) + 1);
    free(source->data);
    *source = replacement;
}

//opencode: expand one non-array GLSL interface block into matching plain
// `in`/`out` declarations. GL 3.0's GLSL 1.30 has no interface-block syntax.
static gX_I1 desktop130ExpandOneInterfaceBlock(gX_Shader_Compiler_Buffer *source)
{
    const gX_I8 *src = source->data;
    const gX_I8 *p = src;
    while (p && *p) {
        const gX_I8 *start = NULL;
        gX_U64 directionLen = 0;
        if (strncmp(p, "out", 3) == 0 && !isIdentCont(p[3])) {
            start = p;
            directionLen = 3;
        } else if (strncmp(p, "in", 2) == 0 && !isIdentCont(p[2])) {
            start = p;
            directionLen = 2;
        }
        if (!start || (start > src && isIdentCont(start[-1]))) {
            p++;
            continue;
        }
        const gX_I8 *type = skipWs(start + directionLen);
        if (!isIdentStart(*type)) {
            p = start + directionLen;
            continue;
        }
        const gX_I8 *afterType = type;
        while (isIdentCont(*afterType)) afterType++;
        const gX_I8 *open = skipWs(afterType);
        if (*open != '{') {
            p = afterType;
            continue;
        }
        const gX_I8 *close = open + 1;
        while (*close && *close != '}') close++;
        if (*close != '}') return 0;
        const gX_I8 *instanceStart = skipWs(close + 1);
        if (!isIdentStart(*instanceStart)) return 0;
        const gX_I8 *instanceEnd = instanceStart;
        while (isIdentCont(*instanceEnd)) instanceEnd++;
        if (*skipWs(instanceEnd) == '[') {
            p = instanceEnd;
            continue;
        }
        const gX_I8 *semicolon = skipWs(instanceEnd);
        if (*semicolon != ';') return 0;
        gX_I8 instance[64];
        gX_U64 instanceLen = (gX_U64)(instanceEnd - instanceStart);
        if (instanceLen >= sizeof(instance)) return 0;
        memcpy(instance, instanceStart, instanceLen);
        instance[instanceLen] = '\0';
        gX_Shader_Compiler_Buffer replacement = {0};
        bufferAppend(&replacement, src, (gX_U64)(start - src));
        const gX_I8 *field = open + 1;
        while (field < close) {
            const gX_I8 *fieldEnd = field;
            while (fieldEnd < close && *fieldEnd != ';') fieldEnd++;
            const gX_I8 *first = skipWs(field);
            const gX_I8 *last = fieldEnd;
            while (last > first && (last[-1] == ' ' || last[-1] == '\t' || last[-1] == '\r' || last[-1] == '\n')) last--;
            if (last > first) {
                bufferAppend(&replacement, start, directionLen);
                bufferAppend(&replacement, " ", 1);
                bufferAppend(&replacement, first, (gX_U64)(last - first));
                bufferAppend(&replacement, ";\n", 2);
            }
            field = fieldEnd < close ? fieldEnd + 1 : close;
        }
        bufferAppend(&replacement, semicolon + 1, strlen(semicolon + 1) + 1);
        if (!replacement.data) return 0;
        free(source->data);
        *source = replacement;
        desktop130ReplaceMemberPrefix(source, instance);
        return 1;
    }
    return 0;
}

//opencode: apply the desktop legacy-GLSL source rewrites after include
// expansion so interface fields and explicit locations are handled
// consistently. Higher legacy rungs keep what they support: 140 keeps
// instancing/inverse, 150 additionally keeps interface blocks and geometry.
static gX_I0 desktopLegacyRewriteDialect(gX_Shader_Compiler_Buffer *source, gX_I32 version)
{
    gX_Shader_Compiler_Buffer withoutLayouts = {0};
    desktop130StripLayouts(&withoutLayouts, source->data, strlen(source->data));
    bufferAppend(&withoutLayouts, "", 1);
    free(source->data);
    *source = withoutLayouts;
    if (version < 150) {
        while (desktop130ExpandOneInterfaceBlock(source)) { }
    }
    if (version < 140) {
        desktop130ReplaceText(source, "gl_InstanceID", "0");
        desktop130ReplaceText(source, "transpose(inverse(mat3(modelMat)))", "mat3(matNormal)");
    }
    desktop130ReplaceWord(source, "input", "inputTex");
}

gX_I0 gX_Shader_Compiler_AddIncludePath(const gX_I8 *path) {
    if (gX_Shader_Compiler_searchPathCount >= GX_SHADER_COMPILER_MAX_SEARCH_PATHS) return;
    gX_Shader_Compiler_searchPaths[gX_Shader_Compiler_searchPathCount++] = strdup(path);
}

gX_I0 gX_Shader_Compiler_ClearIncludePaths(gX_I0) {
    for (gX_I32 i = 0; i < gX_Shader_Compiler_searchPathCount; i++) {
        free(gX_Shader_Compiler_searchPaths[i]);
        gX_Shader_Compiler_searchPaths[i] = NULL;
    }
    gX_Shader_Compiler_searchPathCount = 0;
}

//opencode: test if a path exists; returns allocated path or NULL
static gX_I8 *testPath(const gX_I8 *path) {
    if (!path) return NULL;
    FILE *test = fopen(path, "rb");
    if (test) { fclose(test); return strdup(path); }
    return NULL;
}

//opencode: simple path resolution cache to avoid repeated fopen lookups
#define GX_SHADER_COMPILER_PATH_CACHE_SIZE 16
typedef struct {
    gX_I8 name[256];
    gX_I8 resolved[512];
    gX_I1 valid;
} gX_Shader_Compiler_PathCacheEntry;

static gX_Shader_Compiler_PathCacheEntry gX_pathCache[GX_SHADER_COMPILER_PATH_CACHE_SIZE];
static gX_U32 gX_pathCacheNext = 0;

static gX_I0 gX_Shader_Compiler_ClearPathCache(gX_I0) {
    for (gX_U32 i = 0; i < GX_SHADER_COMPILER_PATH_CACHE_SIZE; i++)
        gX_pathCache[i].valid = 0;
    gX_pathCacheNext = 0;
}

static const gX_I8 *gX_Shader_Compiler_LookupPathCache(const gX_I8 *name) {
    if (!name) return NULL;
    for (gX_U32 i = 0; i < GX_SHADER_COMPILER_PATH_CACHE_SIZE; i++) {
        if (gX_pathCache[i].valid && strcmp(gX_pathCache[i].name, name) == 0)
            return gX_pathCache[i].resolved;
    }
    return NULL;
}

static gX_I0 gX_Shader_Compiler_StorePathCache(const gX_I8 *name, const gX_I8 *resolved) {
    if (!name || !resolved) return;
    gX_U32 idx = gX_pathCacheNext;
    gX_pathCacheNext = (gX_pathCacheNext + 1) % GX_SHADER_COMPILER_PATH_CACHE_SIZE;
    strncpy(gX_pathCache[idx].name, name, sizeof(gX_pathCache[idx].name) - 1);
    gX_pathCache[idx].name[sizeof(gX_pathCache[idx].name) - 1] = '\0';
    strncpy(gX_pathCache[idx].resolved, resolved, sizeof(gX_pathCache[idx].resolved) - 1);
    gX_pathCache[idx].resolved[sizeof(gX_pathCache[idx].resolved) - 1] = '\0';
    gX_pathCache[idx].valid = 1;
}

//opencode: resolve include path, returns allocated path or NULL
//   Checks the path cache first; stores result in cache on first resolution.
static gX_I8 *resolveIncludePath(const gX_I8 *baseDir, const gX_I8 *includeName) {
    //opencode: check cache
    {
        const gX_I8 *cached = gX_Shader_Compiler_LookupPathCache(includeName);
        if (cached) {
            gX_I8 *dup = strdup(cached);
            if (dup) return dup;
        }
    }

    gX_I8 *res = NULL;
    size_t nlen = strlen(includeName);
    // try baseDir first
    if (baseDir && baseDir[0]) {
        size_t blen = strlen(baseDir);
        gX_I8 *full = (gX_I8*)malloc(blen + nlen + 2);
        if (full) {
            sprintf(full, "%s/%s", baseDir, includeName);
            res = testPath(full);
            free(full);
            if (res) { gX_Shader_Compiler_StorePathCache(includeName, res); return res; }
        }
    }
    // try registered search paths
    for (gX_I32 i = 0; i < gX_Shader_Compiler_searchPathCount; i++) {
        size_t plen = strlen(gX_Shader_Compiler_searchPaths[i]);
        gX_I8 *full = (gX_I8*)malloc(plen + nlen + 2);
        if (full) {
            sprintf(full, "%s/%s", gX_Shader_Compiler_searchPaths[i], includeName);
            res = testPath(full);
            free(full);
            if (res) { gX_Shader_Compiler_StorePathCache(includeName, res); return res; }
        }
    }
    // try resources/shader/ (prefix already has trailing slash via GX_PATH)
    {
        gX_I8 prefix[] = GX_PATH("shader", "");
        size_t plen = sizeof(prefix) - 1;
        gX_I8 *full = (gX_I8*)malloc(plen + nlen + 1);
        if (full) {
            memcpy(full, prefix, plen);
            memcpy(full + plen, includeName, nlen + 1);
            res = testPath(full);
            free(full);
            if (res) { gX_Shader_Compiler_StorePathCache(includeName, res); return res; }
        }
    }
    // try raw includeName as path
    res = testPath(includeName);
    if (res) gX_Shader_Compiler_StorePathCache(includeName, res);
    return res;
}

//opencode: standard uniform block text — individual uniforms.
//   WAS: layout(std140) uniform gX_Std { ... } UBO. The UBO block index
//   returned INVALID_INDEX on raylib/NVIDIA GL config (block stripped by
//   loader or driver). Individual uniforms with bare names are always
//   accessible via glGetUniformLocation, so the C code's SetValue calls
//   for "viewPosition", "lightPosition", "ambientColor", "ambientFactor"
//   actually reach the shader variables. See also model.c:297 comment.
#define STANDARD_UNIFORM_TEXT \
    "//gX: injected standard uniforms (individual)\n" \
    "uniform vec3  viewPosition;\n" \
    "uniform vec3  lightPosition;\n" \
    "uniform vec4  ambientColor;\n" \
    "uniform float ambientFactor;\n" \
    "uniform mat4 mvp;\n" \
    "uniform mat4 matModel;\n" \
    "uniform mat4 matNormal;\n" \
    "uniform mat4 matLightSpace;\n" \
    "uniform float nearPlane;\n" \
    "uniform float farPlane;\n"

gX_I0 gX_Shader_Compiler_SetMarker(const gX_I8 *marker) {
    if (gX_Shader_Compiler_marker) free(gX_Shader_Compiler_marker);
    gX_Shader_Compiler_marker = marker ? strdup(marker) : NULL;
}

//opencode: evaluate a simple #if condition against a define table
//   Supports: SYMBOL, defined(SYMBOL), !defined(SYMBOL), NUMBER, !SYMBOL,
//             ||, && operators
typedef struct { gX_I8 name[64]; gX_I32 val; } gX_IfDefine;
static gX_I1 gX_Shader_Compiler_EvalIfCond(const gX_I8 *expr, const gX_IfDefine *tbl, gX_U32 tcnt) {
    while (*expr == ' ' || *expr == '\t') expr++;
    //opencode: split on || (lowest precedence, short-circuit on true)
    if (strchr(expr, '|') != NULL) {        const gX_I8 *orPos = strstr(expr, "||");
        if (orPos) {
            gX_U32 len = (gX_U32)(orPos - expr);
            gX_I8 *left = (gX_I8*)malloc(len + 1);
            gX_I1 result = 0;
            if (left) {
                size_t trim = len;
                while (trim > 0 && (expr[trim-1] == ' ' || expr[trim-1] == '\t')) trim--;
                memcpy(left, expr, trim);
                left[trim] = '\0';
                result = gX_Shader_Compiler_EvalIfCond(left, tbl, tcnt);
                free(left);
                if (result) return 1;
            }
            const gX_I8 *right = orPos + 2;
            while (*right == ' ' || *right == '\t') right++;
            return gX_Shader_Compiler_EvalIfCond(right, tbl, tcnt);
        }
    }
    //opencode: split on && (higher precedence than ||, short-circuit on false)
    {
        const gX_I8 *andPos = strstr(expr, "&&");
        if (andPos) {
            gX_U32 len = (gX_U32)(andPos - expr);
            gX_I8 *left = (gX_I8*)malloc(len + 1);
            gX_I1 result = 1;
            if (left) {
                size_t trim = len;
                while (trim > 0 && (expr[trim-1] == ' ' || expr[trim-1] == '\t')) trim--;
                memcpy(left, expr, trim);
                left[trim] = '\0';
                result = gX_Shader_Compiler_EvalIfCond(left, tbl, tcnt);
                free(left);
                if (!result) return 0;
            }
            const gX_I8 *right = andPos + 2;
            while (*right == ' ' || *right == '\t') right++;
            return gX_Shader_Compiler_EvalIfCond(right, tbl, tcnt);
        }
    }
    if (strncmp(expr, "defined(", 8) == 0) {
        const gX_I8 *s = expr + 8;
        gX_I8 name[64]; gX_U32 ni = 0;
        while (*s && *s != ')' && ni < 63) name[ni++] = *s++;
        name[ni] = '\0';
        for (gX_U32 i = 0; i < tcnt; i++)
            if (strcmp(tbl[i].name, name) == 0) return 1;
        return 0;
    }
    if (*expr == '!') return !gX_Shader_Compiler_EvalIfCond(expr + 1, tbl, tcnt);
    if (*expr >= '0' && *expr <= '9') return (gX_I32)strtol(expr, NULL, 10) != 0;
    if ((*expr >= 'A' && *expr <= 'Z') || (*expr >= 'a' && *expr <= 'z') || *expr == '_') {
        gX_I8 name[64]; gX_U32 ni = 0;
        while ((*expr >= 'A' && *expr <= 'Z') || (*expr >= 'a' && *expr <= 'z') ||
               (*expr >= '0' && *expr <= '9') || *expr == '_') {
            if (ni < 63) name[ni++] = *expr;
            expr++;
        }
        name[ni] = '\0';
        for (gX_U32 i = 0; i < tcnt; i++)
            if (strcmp(tbl[i].name, name) == 0) return tbl[i].val != 0;
        return 0;
    }
    return 1;
}

//opencode: forward declaration for recursive preprocess
static gX_I1 preprocessFile(
    const gX_I8 *path,
    const gX_I8 *defines,
    gX_I32 depth,
    gX_Shader_Compiler_Buffer *out
);

//opencode: forward declaration for processInclude (needed by preprocessFile)

//opencode: process a single #include line, recursing.
//   parentSourceLine is the line number of the #include directive in the parent file.
//   parentPath is the parent file path (for #line restore).
static gX_I1 processInclude(
    const gX_I8 *baseDir,
    const gX_I8 *includeName,
    const gX_I8 *defines,
    gX_I32 depth,
    gX_U64 parentSourceLine,
    const gX_I8 *parentPath,
    gX_Shader_Compiler_Buffer *out
) {
    if (depth >= GX_SHADER_COMPILER_MAX_INCLUDE_DEPTH) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: max include depth (%d) exceeded at '%s'",
            GX_SHADER_COMPILER_MAX_INCLUDE_DEPTH, includeName);
        return 0;
    }
    gX_I8 *resolved = resolveIncludePath(baseDir, includeName);
    if (!resolved) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: include '%s' not found", includeName);
        return 0;
    }
    //opencode: emit #line pointing to included file's line 1
    {
        gX_I8 buf[512];
        gX_I32 n = snprintf(buf, sizeof(buf), "#line 1\n");
        if (n > 0) bufferAppend(out, buf, (gX_U64)n);
    }
    gX_I1 result = preprocessFile(resolved, defines, depth + 1, out);
    //opencode: emit #line back to parent file (next line after #include)
    if (parentSourceLine > 0 && parentPath) {
        gX_I8 buf[512];
        gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
            (unsigned long long)(parentSourceLine + 1));
        if (n > 0) bufferAppend(out, buf, (gX_U64)n);
    } else if (parentSourceLine > 0) {
        gX_I8 buf[512];
        gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
            (unsigned long long)(parentSourceLine + 1));
        if (n > 0) bufferAppend(out, buf, (gX_U64)n);
    }
    free(resolved);
    return result;
}

//opencode: GLES mode — strip uniform initializers. GLSL ES 3.00 forbids
//   `uniform float x = 0.5;` declarations (initializers are desktop 4.20+).
//   Handles comma-separated declarators: `uniform vec4 a = A, b = B, c;`
//   becomes `uniform vec4 a, b, c;`. The uniforms are still declared and set
//   by C code, so dropping the `= <value>` parts is safe.
static gX_I0 esStripUniformInitializers(gX_Shader_Compiler_Buffer *out,
    const gX_I8 *cursor, gX_U64 lineLen)
{
    const gX_I8 *p = cursor;
    const gX_I8 *end = cursor + lineLen;
    while (p < end) {
        const gX_I8 *kw = NULL;
        for (const gX_I8 *q = p; q + 7 <= end; q++) {
            if (strncmp(q, "uniform", 7) == 0) {
                gX_I8 before = (q > cursor) ? q[-1] : ' ';
                gX_I8 after = (q + 7 < end) ? q[7] : ' ';
                gX_I1 wordBefore = (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') || (before >= '0' && before <= '9') || before == '_';
                gX_I1 wordAfter = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') || (after >= '0' && after <= '9') || after == '_';
                if (!wordBefore && !wordAfter) { kw = q; break; }
            }
        }
        if (!kw) {
            bufferAppend(out, p, (gX_U64)(end - p));
            return;
        }
        //opencode: emit everything up to the uniform keyword
        bufferAppend(out, p, (gX_U64)(kw - p));
        //opencode: walk the declarator list, stripping each `= <value>` part.
        //   `,` and `;` at depth 0 delimit declarators; `=` at depth 0 starts
        //   an initializer that runs to the next depth-0 `,` or `;`.
        {
            const gX_I8 *q = kw;
            gX_I32 depth = 0;
            const gX_I8 *segStart = q;
            const gX_I8 *eqPos = NULL;
            while (q < end) {
                gX_I8 ch = *q;
                if (ch == '{' || ch == '(') depth++;
                else if (ch == '}' || ch == ')') { if (depth > 0) depth--; }
                if (depth == 0 && (ch == ',' || ch == ';' || ch == '\n')) {
                    //opencode: declarator segment = [segStart, q). Strip at eqPos if present.
                    if (eqPos) {
                        bufferAppend(out, segStart, (gX_U64)(eqPos - segStart));
                    } else {
                        bufferAppend(out, segStart, (gX_U64)(q - segStart));
                    }
                    bufferAppend(out, &ch, 1);
                    segStart = q + 1;
                    eqPos = NULL;
                    if (ch == ';' || ch == '\n') { q++; break; }
                } else if (depth == 0 && ch == '=' && !eqPos) {
                    eqPos = q;
                }
                q++;
            }
            if (q >= end && segStart < end) {
                //opencode: no trailing ';' — emit remaining bytes
                if (eqPos) bufferAppend(out, segStart, (gX_U64)(eqPos - segStart));
                else bufferAppend(out, segStart, (gX_U64)(end - segStart));
            }
            p = q >= end ? end : q;
        }
    }
}

//opencode: preprocess a single file (recursive for includes)
static gX_I1 preprocessFile(
    const gX_I8 *path,
    const gX_I8 *defines,
    gX_I32 depth,
    gX_Shader_Compiler_Buffer *out
) {
    //opencode: emit initial #line for this file so GLSL compiler tracks correct file origin.
    //   Emitted at line 0 so the first real line (usually #version) is line 1.
    //   We use line 0 since the first emitted line will increment the counter to 1.
    //   Placed after #version to avoid C0204 errors.
    gX_U64 fileLen = 0;
    gX_I8 *src = readFile(path, &fileLen);
    if (!src) return 0;

    gX_I8 dir[256];
    getDir(path, dir, sizeof(dir));

    //opencode: parse defines into a name→value lookup for #if evaluation
    #define GX_IF_MAX_DEFINES 64
    gX_IfDefine ifDefines[GX_IF_MAX_DEFINES];
    gX_U32 ifDefineCount = 0;
    if (defines) {
        const gX_I8 *d = defines;
        while (*d && ifDefineCount < GX_IF_MAX_DEFINES) {
            if (strncmp(d, "#define ", 8) == 0) {
                d += 8;
                gX_I8 name[64]; gX_U32 ni = 0;
                while (*d && *d != ' ' && *d != '\n' && ni < 63) name[ni++] = *d++;
                name[ni] = '\0';
                while (*d == ' ') d++;
                gX_I32 val = (gX_I32)strtol(d, NULL, 10);
                strcpy(ifDefines[ifDefineCount].name, name);
                ifDefines[ifDefineCount].val = val;
                ifDefineCount++;
            }
            while (*d && *d++ != '\n') {}
        }
    }
    //ai: the legacy dialect injects this define after #version; mirror it in
    //    the #if table so shaders can guard 330-only code before that point.
    if (gX_Shader_Compiler_desktopLegacyGLSL
        && ifDefineCount < GX_IF_MAX_DEFINES)
    {
        strcpy(ifDefines[ifDefineCount].name, "GX_GLSL_LEGACY");
        ifDefines[ifDefineCount].val = 1;
        ifDefineCount++;
    }
    #define GX_IF_STACK_MAX 32
    gX_I1 ifActive[GX_IF_STACK_MAX];
    gX_I1 elseTaken[GX_IF_STACK_MAX];
    gX_I32 ifDepth = 0;
    ifActive[0] = 1; // outermost level always active

    //opencode: process line by line, tracking source line numbers for #line directives
    gX_U64 sourceLine = 1;
    gX_I8 *cursor = src;
    gX_I1 versionLineFound = 0;
    gX_I1 definesInjected = 0;
    const gX_I8 *marker = gX_Shader_Compiler_marker;
    if (!marker) marker = "//gX_STANDARD_UNIFORMS";

    while (*cursor) {
        gX_I8 *newline = strchr(cursor, '\n');
        gX_U64 lineLen = newline ? (gX_U64)(newline - cursor) + 1 : strlen(cursor) + 1;

    //opencode: track #if/#else/#endif to skip includes in inactive blocks.
    //   Expressions are evaluated LINE-BOUNDED (copied up to the newline) —
    //   previously the raw rest-of-buffer was passed, so a multiline body
    //   containing || or && got recursively split and a fragment starting
    //   with '\n' hit the catch-all "return 1", flipping #ifndef guards
    //   (e.g. shadow.glsl's guard evaluated true -> entire body stripped ->
    //   standard.frag failed to compile -> default shader fallback).
    if (strncmp(cursor, "#ifdef ", 7) == 0) {
        gX_I8 expr[256];
        gX_U32 exprLen = (gX_U32)(newline ? (gX_U64)(newline - (cursor + 7)) : strlen(cursor + 7));
        if (exprLen >= sizeof(expr)) exprLen = (gX_U32)sizeof(expr) - 1;
        memcpy(expr, cursor + 7, exprLen);
        expr[exprLen] = '\0';
        gX_I1 cond = gX_Shader_Compiler_EvalIfCond(expr, ifDefines, ifDefineCount);
        if (ifDepth < GX_IF_STACK_MAX - 1) ifDepth++;
        ifActive[ifDepth] = ifActive[ifDepth - 1] && cond;
        elseTaken[ifDepth] = 0;
        goto nextLine;
    }
    if (strncmp(cursor, "#ifndef ", 8) == 0) {
        gX_I8 expr[256];
        gX_U32 exprLen = (gX_U32)(newline ? (gX_U64)(newline - (cursor + 8)) : strlen(cursor + 8));
        if (exprLen >= sizeof(expr)) exprLen = (gX_U32)sizeof(expr) - 1;
        memcpy(expr, cursor + 8, exprLen);
        expr[exprLen] = '\0';
        gX_I1 cond = gX_Shader_Compiler_EvalIfCond(expr, ifDefines, ifDefineCount);
        if (ifDepth < GX_IF_STACK_MAX - 1) ifDepth++;
        ifActive[ifDepth] = ifActive[ifDepth - 1] && !cond;
        elseTaken[ifDepth] = 0;
        goto nextLine;
    }
    if (strncmp(cursor, "#if ", 4) == 0 || strncmp(cursor, "#if\n", 4) == 0) {
        gX_I1 cond = 1;
        if (cursor[3] == ' ') {
            gX_I8 expr[256];
            gX_U32 exprLen = (gX_U32)(newline ? (gX_U64)(newline - (cursor + 4)) : strlen(cursor + 4));
            if (exprLen >= sizeof(expr)) exprLen = (gX_U32)sizeof(expr) - 1;
            memcpy(expr, cursor + 4, exprLen);
            expr[exprLen] = '\0';
            cond = gX_Shader_Compiler_EvalIfCond(expr, ifDefines, ifDefineCount);
        }
        if (ifDepth < GX_IF_STACK_MAX - 1) ifDepth++;
        ifActive[ifDepth] = ifActive[ifDepth - 1] && cond;
        elseTaken[ifDepth] = 0;
        goto nextLine;
    }
        if (strncmp(cursor, "#else", 5) == 0) {
            if (ifDepth > 0) {
                ifActive[ifDepth] = ifActive[ifDepth - 1] && !elseTaken[ifDepth];
                elseTaken[ifDepth] = 1;
            }
            goto nextLine;
        }
        if (strncmp(cursor, "#endif", 6) == 0) {
            if (ifDepth > 0) ifDepth--;
            goto nextLine;
        }

        //opencode: skip lines inside inactive #if blocks (include is resolved here,
        //   not by the GLSL compiler, so we must skip #include at the preprocessor level)
        if (!ifActive[ifDepth]) {
            cursor = newline ? newline + 1 : cursor + lineLen;
            sourceLine++;
            if (!newline) break;
            continue;
        }

        //opencode: check for #include "..." or #include <...>
        if (strncmp(cursor, "#include", 8) == 0) {
            gX_I8 openChar = 0, closeChar = 0;
            gX_I8 *nameStart = NULL;
            //opencode: try double-quote form first
            gX_I8 *quot1 = strchr(cursor, '"');
            if (quot1 && quot1 < (newline ? newline : cursor + lineLen)) {
                openChar = '"'; closeChar = '"';
                nameStart = quot1 + 1;
            } else {
                //opencode: try angle-bracket form
                gX_I8 *ang1 = strchr(cursor, '<');
                if (ang1 && ang1 < (newline ? newline : cursor + lineLen)) {
                    openChar = '<'; closeChar = '>';
                    nameStart = ang1 + 1;
                }
            }
            if (openChar) {
                gX_I8 *nameEnd = strchr(nameStart, closeChar);
                if (nameEnd && nameEnd < (newline ? newline : cursor + lineLen)) {
                    gX_U64 nameLen = (gX_U64)(nameEnd - nameStart);
                    gX_I8 includeName[256];
                    if (nameLen >= sizeof(includeName)) nameLen = sizeof(includeName) - 1;
                    memcpy(includeName, nameStart, nameLen);
                    includeName[nameLen] = '\0';

                    gX_TraceLog(gX_LOG_TRACE, "SHADER_COMPILER: resolving include '%s' from '%s'",
                        includeName, path);
                    if (!processInclude(dir, includeName, defines, depth, sourceLine, path, out)) {
                        // include not found, emit original #include as comment
                        bufferAppend(out, "// [missing: ", 13);
                        bufferAppend(out, includeName, nameLen);
                        bufferAppend(out, "]\n", 2);
                    }
                    //opencode: restore #line to parent file (processInclude emits
                    //   #line N without filename, so GLSL compiler thinks we're
                    //   still in the included file). Without this, all subsequent
                    //   errors reference the included file with wrong line numbers.
                    {
                        gX_I8 gx_line_buf[512];
                        gX_I32 gx_n = snprintf(gx_line_buf, sizeof(gx_line_buf),
                            "#line %llu\n",
                            (unsigned long long)(sourceLine + 1));
                        if (gx_n > 0) bufferAppend(out, gx_line_buf, (gX_U64)gx_n);
                    }
                    cursor = newline ? newline + 1 : cursor + lineLen;
                    sourceLine++;
                    if (!newline) break;
                    continue;
                }
            }
        }

        //opencode: check for //gX_STANDARD_UNIFORMS marker (top-level only, not in includes)
        if (depth == 0) {
            const gX_I8 *found = strstr(cursor, marker);
            if (found && found < (newline ? newline : cursor + lineLen)) {
                bufferAppend(out, STANDARD_UNIFORM_TEXT, sizeof(STANDARD_UNIFORM_TEXT) - 1);
                //opencode: emit #line back to the line after the marker in original source
                {
                    gX_I8 buf[64];
                    gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
                        (unsigned long long)(sourceLine + 1));
                    if (n > 0) bufferAppend(out, buf, (gX_U64)n);
                }
                bufferAppend(out, "// marker injected\n", 19);
                cursor = newline ? newline + 1 : cursor + lineLen;
                sourceLine++;
                if (!newline) break;
                continue;
            }
        }

        //opencode: strip #version from included files
        if (depth > 0 && strncmp(cursor, "#version", 8) == 0) {
            bufferAppend(out, "// [stripped: ", 14);
            bufferAppend(out, cursor, lineLen - 1);
            bufferAppend(out, "]\n", 2);
            cursor = newline ? newline + 1 : cursor + lineLen;
            sourceLine++;
            if (!newline) break;
            continue;
        }

        //opencode: GLES mode — strip desktop-only #extension directives
        //   (e.g. standard.vert's GL_ARB_shader_storage_buffer_object, which
        //   does not exist in ES — SSBOs are core in ES 3.1+). Unknown
        //   extensions in an ES shader are a compile error, so comment them out.
        if (gX_Shader_Compiler_esMode && strncmp(cursor, "#extension", 10) == 0) {
            bufferAppend(out, "// [es-stripped: ", 17);
            bufferAppend(out, cursor, lineLen - 1);
            bufferAppend(out, "]\n", 2);
            cursor = newline ? newline + 1 : cursor + lineLen;
            sourceLine++;
            if (!newline) break;
            continue;
        }

        //opencode: inject defines after #version line (top-level only)
        if (depth == 0 && !versionLineFound && strncmp(cursor, "#version", 8) == 0) {
            versionLineFound = 1;
            if (gX_Shader_Compiler_vulkanMode) {
                //opencode: Vulkan dialect — SPIR-V needs #version 450 (330 is
                //   desktop-GL-only). The rest of the dialect (locations, UBO,
                //   sampler bindings) is applied by ToVulkanDialect().
                bufferAppend(out, "#version 450\n", 13);
                //opencode: emit #line to the line after #version to keep error
                //   line numbers aligned with the original source
                {
                    gX_I8 buf[64];
                    gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
                        (unsigned long long)(sourceLine + 1));
                    if (n > 0) bufferAppend(out, buf, (gX_U64)n);
                }
            } else if (gX_Shader_Compiler_esMode) {
                //opencode: GLES dialect rewrite — desktop `#version 330` has no
                //   precision or ES syntax. Pick the ES version from the file
                //   extension: .geom → 320 es (geometry shaders need ES 3.2),
                //   .glsl → 310 es (compute shaders need ES 3.1), else 300 es.
                //   Fragments need explicit default precision in ES.
                //   Interface blocks (`out VS_OUT {`) are ES 3.2 — a .vert/.frag
                //   that declares them must be 320 es or llvmpipe rejects with
                //   "GL_OES_shader_io_blocks or #version 320 required".
                const gX_I8 *dot = NULL;
                for (const gX_I8 *p = path; *p; p++) if (*p == '.') dot = p;
                gX_I8 esVer[32];
                if (dot && strcmp(dot, ".geom") == 0) strcpy(esVer, "#version 320 es\n");
                else if (dot && strcmp(dot, ".glsl") == 0) strcpy(esVer, "#version 310 es\n");
                else if (sourceHasInterfaceBlock(src)) strcpy(esVer, "#version 320 es\n");
                else strcpy(esVer, "#version 300 es\n");
                bufferAppend(out, esVer, strlen(esVer));
                //opencode: ES fragment/compute shaders require explicit default
                //   precision (desktop 330 defaulted to highp implicitly).
                //   sampler2D/samplerCube have implicit lowp defaults in ES, but
                //   sampler2DArray/sampler3D/sampler*Shadow have NONE — llvmpipe
                //   rejects "No precision specified for type `sampler2DArray'".
                static const gX_I8 kESPrecision[] =
                    "precision highp float;\n"
                    "precision highp int;\n"
                    "precision highp sampler2DArray;\n"
                    "precision highp sampler3D;\n"
                    "precision highp sampler2DShadow;\n"
                    "precision highp samplerCubeShadow;\n"
                    "precision highp sampler2DArrayShadow;\n";
                bufferAppend(out, kESPrecision, sizeof(kESPrecision) - 1);
                //opencode: emit #line to the line after #version to keep error
                //   line numbers aligned with the original source
                {
                    gX_I8 buf[64];
                    gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
                        (unsigned long long)(sourceLine + 1));
                    if (n > 0) bufferAppend(out, buf, (gX_U64)n);
                }
            } else if (gX_Shader_Compiler_desktopLegacyGLSL) {
                gX_I8 ver[32];
                snprintf(ver, sizeof(ver), "#version %d\n", (int)gX_Shader_Compiler_desktopLegacyGLSL);
                bufferAppend(out, ver, strlen(ver));
                bufferAppend(out, "#define GX_GLSL_LEGACY 1\n", 25);
                {
                    gX_I8 buf[64];
                    gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
                        (unsigned long long)(sourceLine + 1));
                    if (n > 0) bufferAppend(out, buf, (gX_U64)n);
                }
            } else {
                //opencode: v0.5.0 — GX_COMPUTE_SSBO: SSBO access in fragment
                //   shaders needs #version 430 (GL 4.3); the file keeps 330 so
                //   the default GL 3.3 build compiles unchanged.
                if (defines && strstr(defines, "GX_COMPUTE_SSBO")) {
                    bufferAppend(out, "#version 430\n", 13);
                } else {
                    bufferAppend(out, cursor, lineLen);
                }
            }
            if (defines && defines[0] && !definesInjected) {
                definesInjected = 1;
                bufferAppend(out, "//gX: compile-time defines\n", 27);
                bufferAppend(out, defines, strlen(defines));
                if (defines[strlen(defines) - 1] != '\n')
                    bufferAppend(out, "\n", 1);
                //opencode: emit #line back to the line after #version
                {
                    gX_I8 buf[64];
                    gX_I32 n = snprintf(buf, sizeof(buf), "#line %llu\n",
                        (unsigned long long)(sourceLine + 1));
                    if (n > 0) bufferAppend(out, buf, (gX_U64)n);
                }
            }
            cursor = newline ? newline + 1 : cursor + lineLen;
            sourceLine++;
            if (!newline) break;
            continue;
        }

        //opencode: emit line as-is
        nextLine:
        if (gX_Shader_Compiler_esMode) {
            esStripUniformInitializers(out, cursor, lineLen);
        } else if (gX_Shader_Compiler_desktopLegacyGLSL) {
            desktop130StripLayouts(out, cursor, lineLen);
        } else {
            bufferAppend(out, cursor, lineLen);
        }
        if (!newline) break; //opencode: end of file, no trailing newline — stop to avoid reading past buffer
        cursor = newline + 1;
        sourceLine++;
    }

    //opencode: if no #version line found, inject defines at top anyway
    if (depth == 0 && defines && defines[0] && !definesInjected) {
        gX_I8 tmp[256];
        gX_I32 n = sprintf(tmp, "//gX: compile-time defines\n");
        bufferAppend(out, tmp, (gX_U64)n);
        bufferAppend(out, defines, strlen(defines));
        if (defines[strlen(defines) - 1] != '\n')
            bufferAppend(out, "\n", 1);
    }

    //opencode: Vulkan mode without a #version line (edge-case shaders like
    //   vhs_*.frag) — glslang infers a desktop/ES default and SPIR-V compile
    //   fails. Emit #version 450 at the top for a deterministic Vulkan dialect.
    if (depth == 0 && gX_Shader_Compiler_vulkanMode && !versionLineFound) {
        gX_Shader_Compiler_Buffer vbuf = {0};
        bufferAppend(&vbuf, "#version 450\n", 13);
        if (out->length) bufferAppend(&vbuf, out->data, out->length);
        free(out->data);
        *out = vbuf;
        versionLineFound = 1;
    }

    free(src);
    return 1;
}

static gX_I8 *Preprocess_Internal(
    const gX_I8 *path,
    const gX_I8 *defines,
    gX_U64 *outLen
) {
    if (!path) return NULL;

    //opencode: clear path resolution cache for this compilation
    gX_Shader_Compiler_ClearPathCache();

    gX_Shader_Compiler_Buffer buf = {0};
    if (!preprocessFile(path, defines, 0, &buf)) {
        free(buf.data);
        return NULL;
    }

    //opencode: null-terminate
    bufferAppend(&buf, "", 1);
    if (gX_Shader_Compiler_desktopLegacyGLSL)
    {
        desktopLegacyRewriteDialect(&buf, gX_Shader_Compiler_desktopLegacyGLSL);
    }
    else
    {
        //ai: `input` is reserved from GLSL 1.30 on; rename it on every modern
        //    dialect so Mesa accepts the postprocess/glass family (lookups
        //    already fall back to inputTex).
        desktop130ReplaceWord(&buf, "input", "inputTex");
    }
    if (outLen) *outLen = buf.length;
    return buf.data;
}

//opencode: v0.3.2 HOTFIX — thread-safe entry point. Preprocess touches shared
//   statics (search paths, marker) and the async preprocess pool races the
//   main thread, corrupting output (stray NULs / swallowed braces -> moving
//   "unexpected {" compile failures).
gX_I8 *gX_Shader_Compiler_Preprocess(
    const gX_I8 *path,
    const gX_I8 *defines,
    gX_U64 *outLen
) {
    static pthread_mutex_t s_ppMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&s_ppMutex);
    gX_I8 *result = Preprocess_Internal(path, defines, outLen);
    pthread_mutex_unlock(&s_ppMutex);
    return result;
}

gX_I0 gX_Shader_Compiler_FreeSource(gX_I8 *source) {
    free(source);
}

//opencode: check if a character is a valid GLSL identifier start
static gX_I1 isIdentStart(gX_I8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static gX_I1 isIdentCont(gX_I8 c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

//opencode: skip whitespace, return pointer to first non-ws char
static const gX_I8 *skipWs(const gX_I8 *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

//opencode: extract identifier at *p into out, return pointer after identifier
static const gX_I8 *extractIdent(const gX_I8 *p, gX_I8 *out, gX_U32 maxLen) {
    gX_U32 i = 0;
    while (*p && isIdentCont(*p) && i < maxLen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p;
}

gX_I0 gX_Shader_Compiler_ParseUniforms(
    const gX_I8 *source,
    gX_U64 sourceLen,
    gX_Shader_Compiler_Uniform *uniforms,
    gX_U32 *count,
    gX_U32 maxUniforms
) {
    (void)sourceLen;
    *count = 0;
    if (!source) return;

    //opencode: tokenize line by line
    const gX_I8 *p = source;
    gX_I32 lineNum = 1;

    while (*p) {
        //opencode: skip to next line
        const gX_I8 *nl = strchr(p, '\n');
        const gX_U64 lineLen = nl ? (gX_U64)(nl - p) : strlen(p);

        //opencode: look for 'uniform' keyword
        const gX_I8 *scan = p;
        const gX_I8 *end = p + lineLen;

        //opencode: handle layout(std140) uniform BlockName { ... };
        if (strncmp(scan, "layout", 6) == 0) {
            const gX_I8 *brace = strchr(scan, '{');
            const gX_I8 *blockUniform = strstr(scan, "uniform");
            if (blockUniform && blockUniform < end && brace && blockUniform < brace) {
                //opencode: layout block with braces — register block name, skip line
                const gX_I8 *uniPos = strstr(scan, "uniform");
                const gX_I8 *nameStart = skipWs(uniPos + 7);
                if (nameStart < brace) {
                    gX_I8 bname[128];
                    extractIdent(nameStart, bname, sizeof(bname));
                    if (bname[0] && *count < maxUniforms) {
                        gX_I32 idx = (gX_I32)(*count);
                        uniforms[idx].line = lineNum;
                        uniforms[idx].location = -1;
                        strcpy(uniforms[idx].type, "layout_block");
                        strcpy(uniforms[idx].name, bname);
                        (*count)++;
                    }
                }
                //opencode: skip layout block line (e.g., "layout(std140) uniform BlockName {")
                if (nl) { p = nl + 1; lineNum++; } else break;
                continue;
            }
            //opencode: layout qualifier on a simple uniform (e.g., "layout(binding=7) uniform sampler2D input;")
            //   No braces → not a block → pass through to the normal uniform parser below.
            //   Do NOT skip the line (the uniform declaration must reach the compiler).
        }

        //opencode: look for 'uniform <type> <name>;'
        {
            const gX_I8 *uni = strstr(scan, "uniform");
            if (uni && uni < end) {
                const gX_I8 *afterUni = skipWs(uni + 7);
                if (afterUni < end && strncmp(afterUni, "layout", 6) != 0) {
                    gX_I8 typeBuf[64];
                    gX_I8 nameBuf[128];

                    const gX_I8 *afterType = extractIdent(afterUni, typeBuf, sizeof(typeBuf));
                    if (typeBuf[0]) {
                        //opencode: handle array suffix [N]
                        afterType = skipWs(afterType);
                        gX_I1 isArray = 0;
                        if (*afterType == '[') {
                            isArray = 1;
                            while (*afterType && *afterType != ']') afterType++;
                            if (*afterType == ']') afterType++;
                        }

                        //opencode: find identifier after type (before '=' or ';')
                        const gX_I8 *nameStart = skipWs(afterType);
                        const gX_I8 *nameEnd = nameStart;
                        while (*nameEnd && isIdentCont(*nameEnd)) nameEnd++;

                        gX_U64 nlen = (gX_U64)(nameEnd - nameStart);
                        if (nlen > 0 && nlen < sizeof(nameBuf)) {
                            memcpy(nameBuf, nameStart, nlen);
                            nameBuf[nlen] = '\0';

                            if (*count < maxUniforms) {
                                gX_I32 idx = (gX_I32)(*count);
                                uniforms[idx].line = lineNum;
                                uniforms[idx].location = -1;
                                strcpy(uniforms[idx].type, typeBuf);
                                if (isArray) strcat(uniforms[idx].type, "[]");
                                strcpy(uniforms[idx].name, nameBuf);
                                (*count)++;
                            }
                        }
                    }
                }
            }
        }

        //opencode: advance to next line
        if (nl) {
            p = nl + 1;
            lineNum++;
        } else {
            break;
        }
    }
}

//opencode: hot-reload — watched shader tracking grows with the active shader set
#define GX_SHADER_COMPILER_INITIAL_WATCHED 16
//opencode: compression P5 — was 16 (~48K bss); include closure is single-level
//   and the deepest chain in gX shaders = 3 includes (vhs.frag), most have 0-2.
#define GX_SHADER_COMPILER_MAX_INCLUDES 4
//opencode: additive reload observers per watched program. The owner (who
//   registered paths) owns the entry; short-lived consumers (GUI elements)
//   subscribe and get notified on reload WITHOUT clobbering the owner.
#define GX_SHADER_COMPILER_MAX_OBSERVERS 4

typedef struct {
    gX_U32  programId;
    //opencode: deferred swap — a freshly compiled program waits here when a
    //   render pass is active (deleting the old program mid-frame is UB);
    //   FlushDeferredSwaps applies the swap at the next frame start.
    gX_U32  pendingProgramId;
    gX_I32 *pendingLocs;
    gX_Shader *shaderPtr;
    gX_I8   vertPath[256];
    gX_I8   fragPath[256];
    gX_I8   geoPath[256];
    gX_I8   defines[2048];
    gX_I64  vertMtime;
    gX_I64  fragMtime;
    gX_I64  geoMtime;
    gX_I64  lastReloadTimeMs;
    gX_Shader_ReloadCallback reloadCallback;
    gX_I0  *reloadUserData;
    //opencode: additive reload observers — notified after the owner callback
    //   on CheckReload/FlushDeferredSwaps; managed via Subscribe/Unsubscribe.
    gX_Shader_ReloadCallback observers[GX_SHADER_COMPILER_MAX_OBSERVERS];
    gX_I0  *observerData[GX_SHADER_COMPILER_MAX_OBSERVERS];
    gX_I1   observerActive[GX_SHADER_COMPILER_MAX_OBSERVERS];
    gX_I8   errorLog[1024];
    gX_I1   active;
    //opencode: include dependency tracking — paths and mtimes of #include'd files
    gX_I8   incPaths[GX_SHADER_COMPILER_MAX_INCLUDES][256];
    gX_I64  incMtimes[GX_SHADER_COMPILER_MAX_INCLUDES];
    gX_I32  incCount;
} gX_Shader_Compiler_WatchedEntry;

static gX_Shader_Compiler_WatchedEntry *gX_Shader_Compiler_watched = NULL;
static gX_U32 gX_Shader_Compiler_watchedCapacity = 0;

//opencode: grow the watcher table without imposing a fixed shader-count ceiling
static gX_I1 gX_Shader_Compiler_EnsureWatchCapacity(gX_U32 required) {
    if (required <= gX_Shader_Compiler_watchedCapacity) return 1;
    gX_U32 newCapacity = gX_Shader_Compiler_watchedCapacity
        ? gX_Shader_Compiler_watchedCapacity * 2
        : GX_SHADER_COMPILER_INITIAL_WATCHED;
    if (newCapacity < required) newCapacity = required;
    gX_Shader_Compiler_WatchedEntry *newEntries =
        (gX_Shader_Compiler_WatchedEntry *)calloc(newCapacity, sizeof(*newEntries));
    if (!newEntries) {
        gX_TraceLog(gX_LOG_ERROR, "SHADER_COMPILER: unable to grow watch table to %u entries", required);
        return 0;
    }
    if (gX_Shader_Compiler_watched) {
        memcpy(newEntries, gX_Shader_Compiler_watched,
            (size_t)gX_Shader_Compiler_watchedCapacity * sizeof(*newEntries));
        free(gX_Shader_Compiler_watched);
    }
    gX_Shader_Compiler_watched = newEntries;
    gX_Shader_Compiler_watchedCapacity = newCapacity;
    return 1;
}

//opencode: when set, CheckReload defers the program swap instead of deleting
//   the old program immediately (which can delete the currently-bound program
//   when a reload triggers mid-render-pass)
static gX_I1 gX_Shader_Compiler_deferSwaps = 0;

gX_I0 gX_Shader_Compiler_SetDeferSwaps(gX_I1 defer) {
    gX_Shader_Compiler_deferSwaps = defer;
}

//opencode: forward decl — FlushDeferredSwaps and CheckReload notify observers
//   (defined below) after the owner callback
static gX_I0 gX_Shader_Compiler_NotifyObservers(gX_Shader_Compiler_WatchedEntry *e, gX_Shader *shader);

gX_I0 gX_Shader_Compiler_FlushDeferredSwaps(gX_I0) {
    if (gX_Shader_Compiler_deferSwaps) return;
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        gX_Shader_Compiler_WatchedEntry *e = &gX_Shader_Compiler_watched[i];
        if (!e->active || e->pendingProgramId == 0) continue;
        gX_Shader *shader = e->shaderPtr;
        if (!shader) continue;
        gX_U32 oldId = shader->id;
        gX_I32 *oldLocs = shader->locs;
        //opencode: keep the raw-program dispatch registry aligned with deferred swaps
        gX_I1 wasRaw = gX_GL_IsRawProgram(oldId);
        if (wasRaw) gX_GL_RawUnregister(oldId);
        shader->id = e->pendingProgramId;
        shader->locs = e->pendingLocs;
        e->programId = e->pendingProgramId;
        e->pendingProgramId = 0;
        e->pendingLocs = NULL;
        gX_RHI_Shader_Delete(oldId);
        free(oldLocs);
        if (wasRaw) gX_GL_RawRegister(shader->id);
        if (e->reloadCallback) {
            e->reloadCallback(shader, e->reloadUserData);
        }
        //opencode: additive observers (GUI elements etc.) — notified after
        //   the owner callback, with the swapped program already live
        gX_Shader_Compiler_NotifyObservers(e, shader);
    }
}

static gX_I64 gX_Shader_Compiler_GetMtime(const gX_I8 *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (gX_I64)st.st_mtime;
}

gX_I0 gX_Shader_Compiler_Watch_Ex(
    gX_Shader shader,
    const gX_I8 *vertPath, const gX_I8 *fragPath, const gX_I8 *geoPath,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
) {
    if (shader.id == 0) return;
    //opencode: refuse a watch with NO source paths — a path-less entry can
    //   never detect file changes, yet it still reuses the program slot and
    //   wipes the owner's paths/mtimes/callback. Consumers that only want
    //   reload notification must use gX_Shader_Compiler_Subscribe instead.
    if ((!vertPath || !vertPath[0]) &&
        (!fragPath || !fragPath[0]) &&
        (!geoPath  || !geoPath[0])) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: refusing path-less watch (program %u) — "
            "use Subscribe for reload observers", shader.id);
        return;
    }
    gX_I32 slot = -1;
    if (gX_Shader_Compiler_watchedCapacity == 0
        && !gX_Shader_Compiler_EnsureWatchCapacity(GX_SHADER_COMPILER_INITIAL_WATCHED)) return;
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active &&
            gX_Shader_Compiler_watched[i].programId == shader.id) {
            slot = i; break;
        }
        if (slot == -1 && !gX_Shader_Compiler_watched[i].active)
            slot = i;
    }
    if (slot == -1) {
        gX_U32 oldCapacity = gX_Shader_Compiler_watchedCapacity;
        if (!gX_Shader_Compiler_EnsureWatchCapacity(oldCapacity + 1)) return;
        slot = (gX_I32)oldCapacity;
    }

    gX_Shader_Compiler_WatchedEntry *e = &gX_Shader_Compiler_watched[slot];
    //opencode: freshly-reused slot — clear any observers left over from a
    //   previous entry life (the entry's program is changing). A RE-watch of
    //   the SAME live program keeps its observers (they subscribe to this id).
    if (!e->active) {
        for (gX_I32 oi = 0; oi < GX_SHADER_COMPILER_MAX_OBSERVERS; oi++) {
            e->observerActive[oi] = 0;
            e->observers[oi] = NULL;
            e->observerData[oi] = NULL;
        }
    }
    e->programId = shader.id;
    if (vertPath) { strcpy(e->vertPath, vertPath); } else { e->vertPath[0] = '\0'; }
    if (fragPath) { strcpy(e->fragPath, fragPath); } else { e->fragPath[0] = '\0'; }
    if (geoPath)  { strcpy(e->geoPath, geoPath);   } else { e->geoPath[0] = '\0'; }
    e->defines[0] = '\0';
    e->vertMtime = gX_Shader_Compiler_GetMtime(vertPath);
    e->fragMtime = gX_Shader_Compiler_GetMtime(fragPath);
    e->geoMtime  = gX_Shader_Compiler_GetMtime(geoPath);
    e->lastReloadTimeMs = 0;
    e->reloadCallback = callback;
    e->reloadUserData = userData;
    e->active = 1;

    //opencode: extract include dependencies by scanning source files for #include
    e->incCount = 0;
    const gX_I8 *scanPaths[] = {vertPath, fragPath, geoPath, NULL};
    for (gX_I32 pi = 0; scanPaths[pi] && e->incCount < GX_SHADER_COMPILER_MAX_INCLUDES; pi++) {
        const gX_I8 *fp = scanPaths[pi];
        if (!fp || !fp[0]) continue;
        FILE *f = fopen(fp, "r");
        if (!f) continue;
        gX_I8 line[512];
        while (fgets(line, sizeof(line), f) && e->incCount < GX_SHADER_COMPILER_MAX_INCLUDES) {
            //opencode: match #include "..." or #include <...>
            gX_I8 *inc = strstr(line, "#include");
            if (!inc) continue;
            gX_I8 *q1 = strchr(inc, '"');
            gX_I8 *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            gX_I8 *a1 = !q1 ? strchr(inc, '<') : NULL;
            gX_I8 *a2 = a1 ? strchr(a1 + 1, '>') : NULL;
            gX_I8 *nameStart = q1 ? q1 + 1 : (a1 ? a1 + 1 : NULL);
            gX_I8 *nameEnd = q2 ? q2 : (a2 ? a2 : NULL);
            if (!nameStart || !nameEnd) continue;
            gX_I32 plen = (gX_I32)(nameEnd - nameStart);
            if (plen <= 0 || plen >= 256) continue;
            gX_I8 incName[256];
            memcpy(incName, nameStart, (size_t)plen);
            incName[plen] = '\0';
            // resolve include path using the same logic as preprocessor
            gX_I8 dir[256];
            getDir(fp, dir, sizeof(dir));
            gX_I8 *resolved = resolveIncludePath(dir[0] ? dir : NULL, incName);
            if (resolved) {
                strncpy(e->incPaths[e->incCount], resolved, 255);
                e->incPaths[e->incCount][255] = '\0';
                e->incMtimes[e->incCount] = gX_Shader_Compiler_GetMtime(e->incPaths[e->incCount]);
                if (e->incMtimes[e->incCount] > 0) e->incCount++;
                free(resolved);
            }
        }
        fclose(f);
    }
    if (e->incCount > 0) {
        gX_TraceLog(gX_LOG_INFO, "SHADER_COMPILER: watching %d include deps for '%s'",
            e->incCount, fragPath ? fragPath : (vertPath ? vertPath : "?"));
        for (gX_I32 di = 0; di < e->incCount && di < 3; di++)
            gX_TraceLog(gX_LOG_DEBUG, "  dep[%d]: %s", di, e->incPaths[di]);
    }
}

gX_I0 gX_Shader_Compiler_Watch(gX_Shader shader, const gX_I8 *vertPath, const gX_I8 *fragPath) {
    gX_Shader_Compiler_Watch_Ex(shader, vertPath, fragPath, NULL, NULL, NULL);
}

gX_I0 gX_Shader_Compiler_Unwatch(gX_Shader shader) {
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active &&
            gX_Shader_Compiler_watched[i].programId == shader.id) {
            gX_Shader_Compiler_watched[i].active = 0;
            return;
        }
    }
}

//opencode: find the active watch entry for a program id (NULL if unwatched)
static gX_Shader_Compiler_WatchedEntry *gX_Shader_Compiler_FindWatch(gX_U32 programId) {
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active &&
            gX_Shader_Compiler_watched[i].programId == programId) {
            return &gX_Shader_Compiler_watched[i];
        }
    }
    return NULL;
}

gX_I1 gX_Shader_Compiler_Subscribe(
    gX_Shader shader,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
) {
    if (shader.id == 0 || !callback) return 0;
    gX_Shader_Compiler_WatchedEntry *e = gX_Shader_Compiler_FindWatch(shader.id);
    if (!e) return 0;
    //opencode: dedupe identical (callback,userData) subscriptions — re-Load
    //   on the same module must not grow the table
    for (gX_I32 i = 0; i < GX_SHADER_COMPILER_MAX_OBSERVERS; i++) {
        if (e->observerActive[i] && e->observers[i] == callback && e->observerData[i] == userData)
            return 1;
    }
    for (gX_I32 i = 0; i < GX_SHADER_COMPILER_MAX_OBSERVERS; i++) {
        if (!e->observerActive[i]) {
            e->observers[i] = callback;
            e->observerData[i] = userData;
            e->observerActive[i] = 1;
            return 1;
        }
    }
    return 0;
}

gX_I0 gX_Shader_Compiler_Unsubscribe(
    gX_Shader shader,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
) {
    if (shader.id == 0 || !callback) return;
    gX_Shader_Compiler_WatchedEntry *e = gX_Shader_Compiler_FindWatch(shader.id);
    if (!e) return;
    for (gX_I32 i = 0; i < GX_SHADER_COMPILER_MAX_OBSERVERS; i++) {
        if (e->observerActive[i] && e->observers[i] == callback && e->observerData[i] == userData) {
            e->observerActive[i] = 0;
            e->observers[i] = NULL;
            e->observerData[i] = NULL;
            return;
        }
    }
}

gX_I32 gX_Shader_Compiler_GetObserverCount(gX_Shader shader) {
    gX_Shader_Compiler_WatchedEntry *e = gX_Shader_Compiler_FindWatch(shader.id);
    if (!e) return 0;
    gX_I32 count = 0;
    for (gX_I32 i = 0; i < GX_SHADER_COMPILER_MAX_OBSERVERS; i++)
        if (e->observerActive[i]) count++;
    return count;
}

//opencode: invoke all observers after a successful reload (owner callback first)
static gX_I0 gX_Shader_Compiler_NotifyObservers(gX_Shader_Compiler_WatchedEntry *e, gX_Shader *shader) {
    for (gX_I32 i = 0; i < GX_SHADER_COMPILER_MAX_OBSERVERS; i++) {
        if (e->observerActive[i] && e->observers[i]) {
            e->observers[i](shader, e->observerData[i]);
        }
    }
}

//opencode: store defines for hot-reload preprocessing
gX_I0 gX_Shader_Compiler_SetDefines(gX_Shader shader, const gX_I8 *defines) {
    if (shader.id == 0) return;
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active &&
            gX_Shader_Compiler_watched[i].programId == shader.id) {
            if (defines) {
                strncpy(gX_Shader_Compiler_watched[i].defines, defines, sizeof(gX_Shader_Compiler_watched[i].defines) - 1);
                gX_Shader_Compiler_watched[i].defines[sizeof(gX_Shader_Compiler_watched[i].defines) - 1] = '\0';
            } else {
                gX_Shader_Compiler_watched[i].defines[0] = '\0';
            }
            return;
        }
    }
}

//opencode: get current monotonic time in milliseconds
static gX_I64 gX_Shader_Compiler_NowMs(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_I64)ts.tv_sec * 1000 + (gX_I64)ts.tv_nsec / 1000000;
}

//opencode: compile V+F or V+G+F using raw OpenGL.
//   processedGeo may be NULL for V+F only.
//   Captures glGetShaderInfoLog per stage + link error into errorLog (up to errorLogSize).
//   Returns program id, or 0 on failure.
gX_I1 gX_Shader_Compiler_CheckReload(gX_Shader *shader) {
    if (!shader || shader->id == 0) return 0;

    //opencode: find watched entry matching current program id
    gX_Shader_Compiler_WatchedEntry *e = NULL;
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active &&
            gX_Shader_Compiler_watched[i].programId == shader->id) {
            e = &gX_Shader_Compiler_watched[i];
            break;
        }
    }
    if (!e) return 0;
    e->shaderPtr = shader;

    //opencode: rate-limiting — skip if less than interval since last reload
    {
        gX_I64 now = gX_Shader_Compiler_NowMs();
        if (e->lastReloadTimeMs > 0 &&
            now - e->lastReloadTimeMs < GX_SHADER_COMPILER_MIN_RELOAD_INTERVAL_MS)
            return 0;
    }

    gX_I64 newVertMtime = gX_Shader_Compiler_GetMtime(
        e->vertPath[0] ? e->vertPath : NULL);
    gX_I64 newFragMtime = gX_Shader_Compiler_GetMtime(
        e->fragPath[0] ? e->fragPath : NULL);
    gX_I64 newGeoMtime  = gX_Shader_Compiler_GetMtime(
        e->geoPath[0]  ? e->geoPath  : NULL);

    if (newVertMtime == e->vertMtime && newFragMtime == e->fragMtime && newGeoMtime == e->geoMtime) {
        //opencode: check include dependency mtimes too
        gX_I1 incChanged = 0;
        for (gX_I32 di = 0; di < e->incCount; di++) {
            gX_I64 newIncMtime = gX_Shader_Compiler_GetMtime(e->incPaths[di]);
            if (newIncMtime != e->incMtimes[di]) { incChanged = 1; break; }
        }
        if (!incChanged) return 0;
    }

    gX_TraceLog(gX_LOG_INFO, "SHADER_COMPILER: hot-reload triggered for '%s'/'%s'",
        e->vertPath, e->fragPath);

    //opencode: read + preprocess new source
    gX_U64 vertLen = 0, fragLen = 0, geoLen = 0;
    gX_I8 *processedVert = NULL;
    gX_I8 *processedFrag = NULL;
    gX_I8 *processedGeo = NULL;
    gX_I8 *vPath = e->vertPath[0] ? e->vertPath : NULL;
    gX_I8 *fPath = e->fragPath[0] ? e->fragPath : NULL;
    gX_I8 *gPath = e->geoPath[0]  ? e->geoPath  : NULL;
    const gX_I8 *defs = e->defines[0] ? e->defines : NULL;

    if (vPath) processedVert = gX_Shader_Compiler_Preprocess(vPath, defs, &vertLen);
    if (fPath) processedFrag = gX_Shader_Compiler_Preprocess(fPath, defs, &fragLen);
    if (gPath) processedGeo  = gX_Shader_Compiler_Preprocess(gPath, defs, &geoLen);

    //opencode: on preprocess failure, keep old mtime so next save retries
    if ((vPath && !processedVert) || (fPath && !processedFrag) || (gPath && !processedGeo)) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: hot-reload preprocess failed, keeping old program");
        gX_Shader_Compiler_FreeSource(processedVert);
        gX_Shader_Compiler_FreeSource(processedFrag);
        gX_Shader_Compiler_FreeSource(processedGeo);
        return 0;
    }

    //opencode: compile new program via raw OpenGL (captures error log)
    gX_U32 newProgramId = 0;
    gX_I32 *newLocs = NULL;
    gX_I8 compileError[1024] = {0};

    newProgramId = gX_RHI_Shader_Compile(
        processedVert, gPath ? processedGeo : NULL, processedFrag,
        NULL, compileError, sizeof(compileError)
    );

    gX_Shader_Compiler_FreeSource(processedVert);
    gX_Shader_Compiler_FreeSource(processedFrag);
    gX_Shader_Compiler_FreeSource(processedGeo);

    //opencode: on compile failure, keep old mtime so next edit retries
    if (newProgramId == 0) {
        if (compileError[0]) {
            gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: hot-reload failed (%s), keeping old program", compileError);
            gX_Msg_Output_Push_Type(compileError, gX_LOG_ERROR);
            snprintf(e->errorLog, sizeof(e->errorLog), "%s", compileError);
            e->errorLog[sizeof(e->errorLog) - 1] = '\0';
        } else {
            gX_TraceLog(gX_LOG_WARNING, "SHADER_COMPILER: hot-reload compile failed, keeping old program");
            e->errorLog[0] = '\0';
        }
        return 0;
    }

    //opencode: clear error log on success
    e->errorLog[0] = '\0';

    //opencode: allocate locs array (both V+F and geo paths use raw GL now)
    {
        //opencode: RL_MAX_SHADER_LOCATIONS (32) — rlgl constant, replaced with
        //   a local bound so this file needs no GL/rlgl headers.
        newLocs = (gX_I32 *)calloc(32, sizeof(gX_I32));
        if (newLocs) {
            for (gX_I32 i = 0; i < 32; i++) newLocs[i] = -1;
        }
    }

    //opencode: defer the swap when a render pass is active — deleting the
    //   currently-bound program mid-frame is undefined; FlushDeferredSwaps
    //   applies it at the next frame start
    if (gX_Shader_Compiler_deferSwaps) {
        e->pendingProgramId = newProgramId;
        e->pendingLocs = newLocs;
        e->vertMtime = newVertMtime;
        e->fragMtime = newFragMtime;
        e->geoMtime  = newGeoMtime;
        for (gX_I32 di = 0; di < e->incCount; di++)
            e->incMtimes[di] = gX_Shader_Compiler_GetMtime(e->incPaths[di]);
        e->lastReloadTimeMs = gX_Shader_Compiler_NowMs();
        gX_TraceLog(gX_LOG_INFO, "SHADER_COMPILER: hot-reload deferred (mid-frame) — %u -> %u",
            shader->id, newProgramId);
        return 1;
    }

    //opencode: swap atomically — replace id + locs in caller's struct
    gX_U32 oldId = shader->id;
    gX_I32 *oldLocs = shader->locs;
    shader->id = newProgramId;
    shader->locs = newLocs;

    //opencode: update watched entry with new program id + mtimes
    e->programId = newProgramId;
    e->vertMtime = newVertMtime;
    e->fragMtime = newFragMtime;
    e->geoMtime  = newGeoMtime;
    //opencode: update include dependency mtimes
    for (gX_I32 di = 0; di < e->incCount; di++)
        e->incMtimes[di] = gX_Shader_Compiler_GetMtime(e->incPaths[di]);
    e->lastReloadTimeMs = gX_Shader_Compiler_NowMs();

    gX_RHI_Shader_Delete(oldId);
    free(oldLocs);

    gX_TraceLog(gX_LOG_INFO, "SHADER_COMPILER: hot-reload success — %u -> %u",
        oldId, newProgramId);

    //opencode: invoke reload callback (e.g. re-query locations, recreate UBOs)
    if (e->reloadCallback) {
        e->reloadCallback(shader, e->reloadUserData);
    }
    //opencode: additive observers (GUI elements etc.) — notified after the
    //   owner callback, with the swapped program already live
    gX_Shader_Compiler_NotifyObservers(e, shader);

    return 1;
}

//opencode: query last compile error for a watched program (returns NULL if no error / not found)
const gX_I8 *gX_Shader_Compiler_GetLastError(gX_U32 programId) {
    for (gX_I32 i = 0; i < (gX_I32)gX_Shader_Compiler_watchedCapacity; i++) {
        if (gX_Shader_Compiler_watched[i].active && gX_Shader_Compiler_watched[i].programId == programId) {
            return gX_Shader_Compiler_watched[i].errorLog[0] ? gX_Shader_Compiler_watched[i].errorLog : NULL;
        }
    }
    return NULL;
}

// ============================================================================
// Vulkan dialect post-process (Stage 1C)
//
// Takes already-preprocessed GLSL (version 450 emitted by the preprocessor when
// SetVulkanMode(1)) and rewrites what SPIR-V compilation requires:
//   1. vertex inputs → explicit layout(location = N)  (fixed RHI attr order)
//   2. fragment output color → layout(location = 0)
//   3. interface blocks → layout(location = 6) (member locations sequential)
//   4. flat in/out after the block → next free location
//   5. non-opaque uniforms → one layout(set=0, binding=0) uniform UBO { ... };
//      named block WITHOUT instance → members stay at global scope, so shader
//      bodies referencing them by bare name keep working unchanged
//   6. sampler uniforms → layout(set=0, binding=N) per sampler
//   7. reserved words (`input`/`output`/`buffer` as identifiers) → renamed
//
// Single pass over preprocessed line-based source. The UBO members are buffered
// and emitted inline at the position of the first non-opaque uniform, so any
// #define above it stays visible.
// ============================================================================

//opencode: standard vertex attribute → fixed location map (matches the RHI's
//   glBindAttribLocation convention in gl_draw.c: position=0..texcoord2=5).
static const gX_I8 *gX_VK_ATTR_NAMES[6] = {
    "vertexPosition", "vertexTexCoord", "vertexNormal",
    "vertexColor",    "vertexTangent",  "vertexTexCoord2"
};

//opencode: locations consumed by an interface-block member type (matN = N).
static gX_U32 vkTypeLocationCount(const gX_I8 *type) {
    if (strncmp(type, "mat", 3) == 0 && type[3] >= '1' && type[3] <= '4')
        return (gX_U32)(type[3] - '0');
    return 1;
}

static gX_I1 vkIsSamplerType(const gX_I8 *type) {
    if (strncmp(type, "sampler", 7) == 0) return 1;
    if (strncmp(type, "image", 5) == 0) return 1;
    return 0;
}

//opencode: rename reserved identifiers (input/output/buffer) on a declaration
//   line so they don't collide with Vulkan GLSL reserved words.
static gX_I8 *vkRenameReserved(const gX_I8 *line) {
    static const struct { const gX_I8 *from; const gX_I8 *to; } kRes[4] = {
        { "input", "gXin" }, { "output", "gXout" },
        { "gl_InstanceID", "gl_InstanceIndex" }, { "gl_VertexID", "gl_VertexIndex" }
    };
    gX_U64 cap = strlen(line) + 64;
    gX_I8 *out = (gX_I8*)malloc((size_t)cap);
    if (!out) return NULL;
    gX_U64 d = 0;
    const gX_I8 *s = line;
    while (*s) {
        if (isIdentStart(*s)) {
            const gX_I8 *e = s + 1;
            while (isIdentCont(*e)) e++;
            gX_U64 len = (gX_U64)(e - s);
            gX_U64 i;
            for (i = 0; i < 4; i++) {
                if (len == strlen(kRes[i].from) && strncmp(s, kRes[i].from, len) == 0) {
                    gX_U64 tl = strlen(kRes[i].to);
                    memcpy(out + d, kRes[i].to, (size_t)tl);
                    d += tl;
                    break;
                }
            }
            if (i == 4) { memcpy(out + d, s, (size_t)len); d += len; }
            s = e;
        } else {
            out[d++] = *s++;
        }
    }
    out[d] = '\0';
    return out;
}

//opencode: append a non-opaque uniform as a UBO member (strip `uniform ` and
//   any `= initializer` — UBO members can't have default values). Handles
//   multi-declarator statements (`uniform vec4 a, b;`).
//opencode: append a non-opaque uniform as UBO member(s). Strips `uniform ` and
//   each declarator's `= initializer` (UBO members can't have defaults). Handles
//   multi-declarator statements: `uniform vec4 a = v(1), b = v(2), c;` becomes
//   `vec4 a, b, c;`. Tracks paren/bracket depth so commas inside initializers
//   (vec4(1,2,3,4)) don't split declarators.
static gX_I0 vkAppendUBOMember(gX_Shader_Compiler_Buffer *ubo, const gX_I8 *decl) {
    const gX_I8 *p = decl;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "uniform", 7) == 0) p += 7;
    while (*p == ' ' || *p == '\t') p++;

    //opencode: find statement end (top-level ';')
    const gX_I8 *semi = NULL;
    gX_I32 depth = 0;
    for (const gX_I8 *scan = p; *scan; scan++) {
        if (*scan == '(' || *scan == '[' || *scan == '{') depth++;
        else if (*scan == ')' || *scan == ']' || *scan == '}') depth--;
        else if (*scan == ';' && depth <= 0) { semi = scan; break; }
    }
    const gX_I8 *end = semi ? semi : p + strlen(p);

    //opencode: type token (up to first space or comma)
    const gX_I8 *typeStart = p;
    const gX_I8 *typeEnd = typeStart;
    while (*typeEnd && *typeEnd != ' ' && *typeEnd != '\t' && *typeEnd != ',') typeEnd++;

    //opencode: split declarators at top-level commas; strip each `= expr`.
    //   First declarator starts at typeEnd; later ones after a top-level comma.
    const gX_I8 *segStart = typeEnd;
    for (const gX_I8 *scan = typeEnd; scan <= end; scan++) {
        if (scan >= end) {
            //opencode: final declarator — emit trimmed (before its '=')
            const gX_I8 *s2 = segStart;
            while (s2 < end && (*s2 == ' ' || *s2 == '\t' || *s2 == ',')) s2++;
            const gX_I8 *initEq = NULL;
            for (const gX_I8 *q = s2; q < end; q++) {
                if (*q == '=') { initEq = q; break; }
            }
            const gX_I8 *memberEnd = initEq ? initEq : end;
            while (memberEnd > s2 && (memberEnd[-1] == ' ' || memberEnd[-1] == '\t' || memberEnd[-1] == ','))
                memberEnd--;
            if (memberEnd > s2) {
                bufferAppend(ubo, typeStart, (gX_U64)(typeEnd - typeStart));
                bufferAppend(ubo, " ", 1);
                bufferAppend(ubo, s2, (gX_U64)(memberEnd - s2));
                bufferAppend(ubo, ";\n", 2);
            }
            break;
        }
        if (*scan == '(' || *scan == '[' || *scan == '{') depth++;
        else if (*scan == ')' || *scan == ']' || *scan == '}') depth--;
        else if (*scan == ',' && depth == 0) {
            //opencode: end of one declarator — emit trimmed (before its '=')
            const gX_I8 *s2 = segStart;
            while (s2 < scan && (*s2 == ' ' || *s2 == '\t' || *s2 == ',')) s2++;
            const gX_I8 *initEq = NULL;
            for (const gX_I8 *q = s2; q < scan; q++) {
                if (*q == '=') { initEq = q; break; }
            }
            const gX_I8 *memberEnd = initEq ? initEq : scan;
            while (memberEnd > s2 && (memberEnd[-1] == ' ' || memberEnd[-1] == '\t')) memberEnd--;
            if (memberEnd > s2) {
                bufferAppend(ubo, typeStart, (gX_U64)(typeEnd - typeStart));
                bufferAppend(ubo, " ", 1);
                bufferAppend(ubo, s2, (gX_U64)(memberEnd - s2));
                bufferAppend(ubo, ";\n", 2);
            }
            segStart = scan + 1;
        }
    }
}

//opencode: count interface-block member locations from the block body text.
//   Each `type name;` (or comma list) consumes vkTypeLocationCount() locations;
//   array suffixes multiply. Used to place flat vars after the block.
//opencode: skip whitespace including newlines (block bodies are multi-line)
static const gX_I8 *vkSkipAllWs(const gX_I8 *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static gX_U32 vkCountBlockLocations(const gX_I8 *body) {
    gX_U32 total = 0;
    const gX_I8 *seg = body;
    while (seg && *seg) {
        const gX_I8 *e = seg;
        gX_I32 d2 = 0;
        while (*e && !(*e == ';' && d2 == 0)) {
            if (*e == '{' || *e == '(' || *e == '[') d2++;
            else if (*e == '}' || *e == ')' || *e == ']') d2--;
            e++;
        }
        //opencode: skip ws INCLUDING newlines (block body is multi-line)
        const gX_I8 *t = vkSkipAllWs(seg);
        if (isIdentStart(*t)) {
            gX_I8 type[64];
            const gX_I8 *te = extractIdent(t, type, sizeof(type));
            if (strcmp(type, "flat") == 0 || strcmp(type, "centroid") == 0 ||
                strcmp(type, "smooth") == 0 || strcmp(type, "noperspective") == 0) {
                t = vkSkipAllWs(te);
                if (isIdentStart(*t)) te = extractIdent(t, type, sizeof(type));
            }
            gX_U32 count = vkTypeLocationCount(type);
            //opencode: walk declarators — count each `[N]` array multiply
            const gX_I8 *walk = te;
            while (walk && *walk && walk < e) {
                if (*walk == '[') {
                    const gX_I8 *cl = strchr(walk, ']');
                    if (cl && cl < e) {
                        gX_I32 n = atoi(walk + 1);
                        if (n > 1) count *= (gX_U32)n;
                    }
                    walk = cl ? cl + 1 : NULL;
                } else if (*walk == ',') {
                    //opencode: extra declarator in same statement — type stays,
                    //   but a per-declarator array suffix must re-apply
                    const gX_I8 *nx = vkSkipAllWs(walk + 1);
                    if (isIdentStart(*nx)) {
                        const gX_I8 *nn = nx;
                        while (isIdentCont(*nn)) nn++;
                        count += vkTypeLocationCount(type);
                        //opencode: undo the earlier *count if it had an array —
                        //   this is approximate; corpus has no block arrays.
                        const gX_I8 *aa = nn;
                        if (*aa == '[') {
                            const gX_I8 *cl = strchr(aa, ']');
                            if (cl && cl < e) {
                                gX_I32 n = atoi(aa + 1);
                                if (n > 1) count += (gX_U32)(n - 1) * vkTypeLocationCount(type);
                            }
                        }
                    }
                }
                walk++;
            }
            total += count;
        }
        if (!*e) break;
        seg = e + 1;
    }
    return total;
}

//opencode: the Vulkan dialect post-processor.
//   src: preprocessed GLSL (already #version 450 via SetVulkanMode).
//   isVertex: 1 = vertex stage (attrs 0-5), 0 = fragment stage.
//   Returns an allocated buffer (free() it), or NULL.

//opencode: helper — flush the buffered UBO block into out (header was NOT
//   emitted yet; emit header + members + close in one shot at the first
//   non-uniform boundary so #defines above it stay in scope).
//   set: 0 = vertex stage, 1 = fragment stage — per-stage descriptor sets so
//   V and F UBOs (which have different members) don't collide at set 0.
static gX_I0 vkFlushUBO(gX_Shader_Compiler_Buffer *out, gX_Shader_Compiler_Buffer *ubo, gX_I1 *done, gX_U32 set) {
    if (*done) return;
    if (set == 0) bufferAppend(out, "layout(set = 0, binding = 0) uniform UBO_V {\n", 44);
    else          bufferAppend(out, "layout(set = 1, binding = 0) uniform UBO_F {\n", 44);
    if (ubo->length) bufferAppend(out, ubo->data, ubo->length);
    bufferAppend(out, "};\n", 3);
    ubo->length = 0;
    *done = 1;
}

//opencode: helper — is this line a function definition (has `(` then `{`,
//   but not a `layout(...)` block)?
static gX_I1 isFuncLine(const gX_I8 *line) {
    const gX_I8 *t = line;
    while (*t == ' ' || *t == '\t') t++;
    if (strncmp(t, "void ", 5) == 0) return 1;
    if (strncmp(t, "layout(", 7) == 0) return 0;
    if (strchr(t, '(') && strchr(t, '{')) return 1;
    return 0;
}

//opencode: helper — is this line part of an interface block declaration?
static gX_I1 in_interface_block_scan(const gX_I8 *line) {
    const gX_I8 *t = line;
    while (*t == ' ' || *t == '\t') t++;
    if ((strncmp(t, "out ", 4) == 0 || strncmp(t, "in ", 3) == 0) &&
        (strstr(t, "VS_OUT") || strstr(t, "GS_OUT")))
        return 1;
    return 0;
}

//opencode: helper — emit a declaration line with per-declarator location
//   prefixes (handles `in vec2 a, b;` where each declarator gets its own
//   location). typeName is the base type, e.g. "vec3". locFor() returns the
//   location for each declarator name (may be sequential or name-based).
//   Emits: `layout(location = N) <type> <name>;` per declarator.
static gX_I0 vkEmitDeclarators(gX_Shader_Compiler_Buffer *out, const gX_I8 *decl,
    const gX_I8 *typeName, gX_U32 (*locFor)(const gX_I8 *name, gX_U32 seq)) {
    //opencode: find type end then split declarators at top-level commas
    const gX_I8 *semi = strchr(decl, ';');
    const gX_I8 *end = semi ? semi : decl + strlen(decl);
    //opencode: capture the storage qualifier (`in` or `out`)
    gX_I8 qual[8] = {0};
    {
        const gX_I8 *q = decl;
        gX_U32 i = 0;
        while (*q && *q != ' ' && *q != '\t' && i < sizeof(qual) - 1) qual[i++] = *q++;
        qual[i] = '\0';
    }
    //opencode: locate type token (first ident after 'in'/'out')
    const gX_I8 *ty = decl;
    while (*ty && *ty != ' ' && *ty != '\t') ty++;   //opencode: past 'in'/'out'
    while (*ty == ' ' || *ty == '\t') ty++;
    const gX_I8 *typeEnd = ty;
    while (*typeEnd && *typeEnd != ' ' && *typeEnd != '\t' && *typeEnd != ',') typeEnd++;
    gX_U32 seq = 0;
    const gX_I8 *seg = typeEnd;
    while (seg && seg < end) {
        while (seg < end && (*seg == ' ' || *seg == '\t' || *seg == ',')) seg++;
        if (seg >= end || !isIdentStart(*seg)) break;
        gX_I8 name[128];
        const gX_I8 *ne = extractIdent(seg, name, sizeof(name));
        while (ne < end && (*ne == ' ' || *ne == '\t')) ne++;
        //opencode: skip array suffix
        if (ne < end && *ne == '[') {
            const gX_I8 *cl = strchr(ne, ']');
            if (cl && cl < end) ne = cl + 1;
        }
        gX_U32 loc = locFor ? locFor(name, seq) : seq;
        gX_I8 pre[96];
        gX_I32 n = snprintf(pre, sizeof(pre), "layout(location = %u) %s %s %s;\n",
            loc, qual, typeName, name);
        if (n > 0) bufferAppend(out, pre, (gX_U64)n);
        seq++;
        while (seg < end && *seg != ',' && *seg != ';') seg++;
        if (seg < end && *seg == ',') seg++;
    }
}

//opencode: fixed vertex-attribute location by name (matches RHI glBindAttribLocation)
static gX_U32 vkAttrLoc(const gX_I8 *name, gX_U32 seq) {
    (void)seq;
    for (gX_U32 i = 0; i < 6; i++)
        if (strcmp(name, gX_VK_ATTR_NAMES[i]) == 0) return i;
    return 6;
}

//opencode: sequential location (vertex outs / fragment ins) — shared counter
//   so multi-declarator lines continue where the previous line stopped.
static gX_U32 s_vkSeqLoc = 0;
static const gX_I8 *s_vkSeqType = "vec4";   //opencode: current declaration type (matN consumes N)
static gX_U32 vkSeqLoc(const gX_I8 *name, gX_U32 seq) {
    (void)name; (void)seq;
    gX_U32 loc = s_vkSeqLoc;
    s_vkSeqLoc += vkTypeLocationCount(s_vkSeqType ? s_vkSeqType : "vec4");
    return loc;
}

//opencode: statement-splitting pre-pass. The line-based dialect pass assumes
//   one statement per line; engine 2D shaders (batch.c, glfw_draw2d.c) are
//   written as a single condensed line. Insert '\n' after every top-level
//   `;`, `{`, `}` (paren/brace depth 0, outside comments and backslash-
//   continued preprocessor lines) so each statement lands on its own line.
//   For already-formatted sources this is a no-op (nothing inserted before
//   '\n'/'\\r'). Returns a malloc'd copy, or NULL if source has no statement
//   terminators at all (caller then uses the original).
static gX_I8 *vkSplitStatements(const gX_I8 *src) {
    const gX_U64 cap = strlen(src) * 2 + 16;
    gX_I8 *out = (gX_I8*)malloc((size_t)cap);
    if (!out) return NULL;
    gX_U64 d = 0;
    gX_I32 paren = 0, brace = 0;
    const gX_I8 *s = src;
    while (*s) {
        if (*s == '/' && s[1] == '/') {
            while (*s && *s != '\n') out[d++] = *s++;
            continue;
        }
        if (*s == '/' && s[1] == '*') {
            out[d++] = *s++; out[d++] = *s++;
            while (*s && !(*s == '*' && s[1] == '/')) out[d++] = *s++;
            if (*s) out[d++] = *s++;
            if (*s) out[d++] = *s++;
            continue;
        }
        if (*s == '#') {
            //opencode: preprocessor line(s) — copy verbatim, no splitting
            for (;;) {
                const gX_I8 *e = s;
                while (*e && *e != '\n') e++;
                const gX_I1 cont = (e > s && e[-1] == '\\');
                while (s < e) out[d++] = *s++;
                if (*s == '\n') out[d++] = *s++;
                if (!cont) break;
            }
            continue;
        }
        if (*s == '(' || *s == '[') { paren++; out[d++] = *s++; continue; }
        if (*s == ')' || *s == ']') { if (paren > 0) paren--; out[d++] = *s++; continue; }
        if (*s == '{') {
            const gX_I1 top = (paren == 0 && brace == 0);
            out[d++] = *s++;
            brace++;
            if (top && *s != '\0' && *s != '\n' && *s != '\r') out[d++] = '\n';
            continue;
        }
        if (*s == '}') {
            const gX_I1 top = (paren == 0 && brace == 1);
            if (brace > 0) brace--;
            out[d++] = *s++;
            if (top) {
                //opencode: keep interface-block INSTANCE names attached —
                //   `} fs_in;` must stay on one line. Only split the `}` from
                //   following constructs (else/while/comma) or a bare end.
                const gX_I8 *q = s;
                while (*q == ' ' || *q == '\t' || *q == '\r') q++;
                gX_I1 isInstance = 0;
                if (*q == ';') {
                    isInstance = 1;
                } else if (isIdentStart(*q)) {
                    const gX_I8 *e = q + 1;
                    while (isIdentCont(*e)) e++;
                    while (*e == ' ' || *e == '\t') e++;
                    if (*e == '[') {
                        while (*e && *e != ']') e++;
                        if (*e == ']') e++;
                        while (*e == ' ' || *e == '\t') e++;
                    }
                    if (*e == ';') isInstance = 1;
                }
                if (!isInstance && *s != '\0' && *s != '\n' && *s != '\r') out[d++] = '\n';
            }
            continue;
        }
        if (*s == ';' && paren == 0 && brace == 0) {
            out[d++] = *s++;
            if (*s != '\0' && *s != '\n' && *s != '\r') out[d++] = '\n';
            continue;
        }
        out[d++] = *s++;
    }
    out[d] = '\0';
    return out;
}

//opencode: the Vulkan dialect post-processor.
gX_I8 *gX_Shader_Compiler_ToVulkanDialect(const gX_I8 *src, gX_U64 srcLen, gX_I1 isVertex) {
    if (!src) return NULL;
    (void)srcLen;

    //opencode: split condensed single-line shaders into one statement per line
    gX_I8 *splitBuf = vkSplitStatements(src);
    const gX_I8 *in = splitBuf ? splitBuf : src;

    //opencode: PASS 1 — collect all non-opaque uniforms (UBO members), sampler
    //   declarations, and find the flush point. The UBO must precede any use:
    //   global-scope initializers (`float asp = res.x/res.y;`) and functions.
    //   But #defines (e.g. MAX_INSTANCES) that uniforms reference must stay
    //   above the UBO. So flush at the FIRST non-uniform/sampler/attr global
    //   AFTER the last #define that appears before it.
    gX_Shader_Compiler_Buffer ubo = {0};
    gX_U32 uboCount = 0;
    gX_I1 haveUniform = 0;
    gX_U64 flushLine = 0;
    gX_U64 lastDefineLine = 0;
    gX_U64 firstGlobalLine = 0;
    gX_U64 firstFuncLine = 0;
    gX_U64 lineIdx = 1;
    gX_I32 pass1BlockDepth = 0;   //opencode: track interface-block member lines in pass 1
    {
        const gX_I8 *p = in;
        while (p && *p) {
            const gX_I8 *nl = strchr(p, '\n');
            const gX_U64 lineLen = nl ? (gX_U64)(nl - p) : strlen(p);
            gX_I8 *line = (gX_I8*)malloc((size_t)lineLen + 1);
            if (!line) break;
            memcpy(line, p, (size_t)lineLen);
            line[lineLen] = '\0';
            gX_I8 *t = line;
            while (*t == ' ' || *t == '\t') t++;

            if (in_interface_block_scan(line)) {
                //opencode: block open — track braces through its member lines
                pass1BlockDepth = 0;
                for (const gX_I8 *c = t; *c; c++) {
                    if (*c == '{') pass1BlockDepth++;
                    else if (*c == '}') pass1BlockDepth--;
                }
                free(line);
                if (!nl) break;
                p = nl + 1;
                lineIdx++;
                continue;
            }
            //opencode: SSBO block (`layout(...) buffer NAME {` or `buffer NAME {`)
            //   or any top-level `{`-block that isn't a function (its members
            //   are not uniforms/in/out). Track depth to skip members.
            if (!pass1BlockDepth && !isFuncLine(line) &&
                (strstr(t, "buffer ") || strstr(t, "sampler")) && strchr(t, '{')) {
                pass1BlockDepth = 0;
                for (const gX_I8 *c = t; *c; c++) {
                    if (*c == '{') pass1BlockDepth++;
                    else if (*c == '}') pass1BlockDepth--;
                }
                free(line);
                if (!nl) break;
                p = nl + 1;
                lineIdx++;
                continue;
            }
            if (pass1BlockDepth > 0) {
                //opencode: inside a block — only track braces to find the close
                for (const gX_I8 *c = t; *c; c++) {
                    if (*c == '{') pass1BlockDepth++;
                    else if (*c == '}') pass1BlockDepth--;
                }
                free(line);
                if (!nl) break;
                p = nl + 1;
                lineIdx++;
                continue;
            }

            if (strncmp(t, "uniform ", 8) == 0 && strncmp(t, "layout", 6) != 0) {
                gX_I8 type[64];
                const gX_I8 *ty = skipWs(t + 8);
                extractIdent(ty, type, sizeof(type));
                if (!vkIsSamplerType(type)) {
                    haveUniform = 1;
                    vkAppendUBOMember(&ubo, line);
                    uboCount++;
                }
            } else if (strncmp(t, "#define", 7) == 0) {
                lastDefineLine = lineIdx;
            } else if (!in_interface_block_scan(line)) {
                //opencode: first top-level global that is NOT uniform/sampler/
                //   comment/#line/#if/blank — this is where the UBO must appear
                //   before (functions, global initializers, attributes).
                gX_I1 isSkippable = 0;
                if (strncmp(t, "#", 1) == 0) isSkippable = 1;
                else if (strncmp(t, "//", 2) == 0) isSkippable = 1;
                else if (*t == '\0') isSkippable = 1;
                else if (strncmp(t, "in ", 3) == 0 || strncmp(t, "in\t", 3) == 0) isSkippable = 1;
                else if (strncmp(t, "out ", 4) == 0) isSkippable = 1;
                else if (strncmp(t, "flat ", 5) == 0) isSkippable = 1;
                else if (strncmp(t, "layout(", 7) == 0) isSkippable = 1;
                if (!isSkippable) {
                    if (!firstGlobalLine) {
                        firstGlobalLine = lineIdx;
                        if (getenv("GX_VK_TRACE")) {
                            FILE *tf = fopen("/tmp/gx_vk_firstglobal.txt", "w");
                            if (tf) { fprintf(tf, "line %llu: [%s]", (unsigned long long)lineIdx, line); fclose(tf); }
                        }
                    }
                    if (!firstFuncLine && (strncmp(t, "void ", 5) == 0 || strchr(t, '(')))
                        firstFuncLine = lineIdx;
                }
            }
            free(line);
            if (!nl) break;
            p = nl + 1;
            lineIdx++;
        }
    }

    //opencode: flush at the first use-site. Global initializers (background.frag)
    //   come before functions; if none, flush at the first function. #defines
    //   referenced by uniforms (MAX_INSTANCES) must stay above the UBO.
    if (haveUniform) {
        gX_U64 target = firstGlobalLine ? firstGlobalLine : firstFuncLine;
        if (target) {
            flushLine = target;
            if (lastDefineLine && lastDefineLine >= target)
                flushLine = lastDefineLine + 1;
            if (firstFuncLine && flushLine > firstFuncLine)
                flushLine = firstFuncLine;
        }
        if (getenv("GX_VK_TRACE")) {
            FILE *tf = fopen("/tmp/gx_vk_flush.txt", "w");
            if (tf) { fprintf(tf, "firstGlobal=%llu lastDefine=%llu firstFunc=%llu flush=%llu",
                (unsigned long long)firstGlobalLine, (unsigned long long)lastDefineLine,
                (unsigned long long)firstFuncLine, (unsigned long long)flushLine); fclose(tf); }
        }
    }

    //opencode: if uniforms exist but no non-preprocessor top-level line was
    //   found before them, flush right before the first function (`void`/main).
    if (haveUniform && flushLine == 0) {
        const gX_I8 *p = in;
        gX_U64 li = 1;
        while (p && *p) {
            const gX_I8 *nl = strchr(p, '\n');
            const gX_U64 lineLen = nl ? (gX_U64)(nl - p) : strlen(p);
            gX_I8 *line = (gX_I8*)malloc((size_t)lineLen + 1);
            if (!line) break;
            memcpy(line, p, (size_t)lineLen);
            line[lineLen] = '\0';
            gX_I8 *t = line;
            while (*t == ' ' || *t == '\t') t++;
            if (strncmp(t, "void ", 5) == 0 || strncmp(t, "int ", 4) == 0 ||
                strncmp(t, "float ", 6) == 0 || strncmp(t, "vec", 3) == 0 ||
                strncmp(t, "bool ", 5) == 0) {
                //opencode: only if it's a function (has '(' and not a uniform)
                if (strchr(t, '(') && strncmp(t, "uniform", 7) != 0) {
                    flushLine = li;
                    break;
                }
            }
            free(line);
            if (!nl) break;
            p = nl + 1;
            li++;
        }
    }

    //opencode: PASS 2 — emit with the UBO block spliced before flushLine.
    gX_Shader_Compiler_Buffer out = {0};
    gX_Shader_Compiler_Buffer flat = {0};
    gX_Shader_Compiler_Buffer blockBody = {0};
    gX_U32 nextFreeLoc = 6;
    gX_U32 samplerBinding = 1;   //opencode: UBO occupies binding 0 per stage
    gX_I1 inInterfaceBlock = 0;
    gX_I32 blockDepth = 0;
    gX_I1 blockClosed = 0;   //opencode: interface block fully processed — flats emit next
    gX_U32 blockMemberLocs = 0;
    gX_U32 blockBase = 6;
    gX_U32 descSet = isVertex ? 0 : 1;   //opencode: per-stage descriptor set

    //opencode: vertex attrs always occupy locations 0-5 (the RHI binds all
    //   six regardless of how many a shader declares — SSBO-based shaders
    //   declare none). Interface blocks and plain outs/ins start at 6.
    if (isVertex) {
        blockBase = 6;
        nextFreeLoc = 6;
    }
    s_vkSeqLoc = 6;   //opencode: vertex outs / frag ins start after the attrs
    //opencode: emit pass
    gX_U64 curLine = 1;
    const gX_I8 *p = in;
    while (p && *p) {
        const gX_I8 *nl = strchr(p, '\n');
        const gX_U64 lineLen = nl ? (gX_U64)(nl - p) : strlen(p);
        gX_I8 *line = (gX_I8*)malloc((size_t)lineLen + 1);
        if (!line) break;
        memcpy(line, p, (size_t)lineLen);
        line[lineLen] = '\0';
        gX_I8 *t = line;
        while (*t == ' ' || *t == '\t') t++;

        //opencode: Stage 1K — Vulkan SPIR-V requires #version 450. Sources that
        //   bypass the preprocessor (the wrapper's default vertex shader) still
        //   carry #version 330; the dialect injects layout(set=..., binding=...)
        //   qualifiers that shaderc rejects below 450, so bump any version line.
        if (strncmp(t, "#version", 8) == 0) {
            bufferAppend(&out, "#version 450\n", 13);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: flush UBO + buffered flat vars at the recorded position
        if (flushLine && curLine == flushLine && haveUniform && uboCount) {
            if (descSet == 0) bufferAppend(&out, "layout(set = 0, binding = 0) uniform UBO_V {\n", 44);
            else              bufferAppend(&out, "layout(set = 1, binding = 0) uniform UBO_F {\n", 44);
            bufferAppend(&out, ubo.data, ubo.length);
            bufferAppend(&out, "};\n", 3);
            ubo.length = 0;
            uboCount = 0;
        }
        //opencode: flush buffered flat vars at the flush line too (shaders with
        //   no interface block — gfx_particle emits `flat out` before main)
        if (flushLine && curLine == flushLine && flat.length) {
            const gX_I8 *fp = flat.data;
            while (fp && *fp) {
                const gX_I8 *nl2 = strchr(fp, '\n');
                const gX_U64 fl = nl2 ? (gX_U64)(nl2 - fp) : strlen(fp);
                gX_I8 pre2[64];
                gX_I32 n2 = snprintf(pre2, sizeof(pre2), "layout(location = %u) ", s_vkSeqLoc);
                bufferAppend(&out, pre2, (gX_U64)n2);
                bufferAppend(&out, fp, fl);
                bufferAppend(&out, "\n", 1);
                s_vkSeqLoc += vkTypeLocationCount(s_vkSeqType ? s_vkSeqType : "vec4");
                if (!nl2) break;
                fp = nl2 + 1;
            }
            flat.length = 0;
        }

        //opencode: non-opaque uniform → skip (already in UBO)
        if (strncmp(t, "uniform ", 8) == 0 && strncmp(t, "layout", 6) != 0) {
            gX_I8 type[64];
            const gX_I8 *ty = skipWs(t + 8);
            extractIdent(ty, type, sizeof(type));
            if (!vkIsSamplerType(type)) {
                free(line);
                p = nl ? nl + 1 : p + lineLen;
                curLine++;
                continue;
            }
        }

        //opencode: sampler → layout(set=0, binding=N)
        if (strncmp(t, "uniform ", 8) == 0 && strncmp(t, "layout", 6) != 0) {
            gX_I8 type[64];
            const gX_I8 *ty = skipWs(t + 8);
            extractIdent(ty, type, sizeof(type));
            if (vkIsSamplerType(type)) {
                const gX_I8 *semi = strchr(line, ';');
                const gX_I8 *rest = ty + strlen(type);
                while (rest && *rest && rest < semi) {
                    rest = skipWs(rest);
                    if (*rest == ',') { rest++; rest = skipWs(rest); }
                    if (!isIdentStart(*rest)) { rest++; continue; }
                    gX_I8 name[128];
                    const gX_I8 *ne = extractIdent(rest, name, sizeof(name));
                    while (*ne == ' ' || *ne == '\t') ne++;
                    gX_I8 suffix[32] = {0};
                    if (*ne == '[') {
                        const gX_I8 *cl = strchr(ne, ']');
                        if (cl) {
                            gX_U64 sl = (gX_U64)(cl - ne + 1);
                            if (sl < sizeof(suffix)) { memcpy(suffix, ne, (size_t)sl); suffix[sl] = '\0'; }
                            ne = cl + 1;
                        }
                    }
                    if (descSet == 0) bufferAppend(&out, "layout(set = 0, binding = ", 26);
                    else              bufferAppend(&out, "layout(set = 1, binding = ", 26);
                    gX_I8 nb[16];
                    gX_I32 n2 = snprintf(nb, sizeof(nb), "%u", samplerBinding);
                    if (n2 > 0) bufferAppend(&out, nb, (gX_U64)n2);
                    bufferAppend(&out, ") uniform ", 10);
                    bufferAppend(&out, type, strlen(type));
                    bufferAppend(&out, " ", 1);
                    //opencode: rename reserved identifiers in the sampler name
                    //   (analog.frag uses `uniform sampler2D input;`)
                    if (strcmp(name, "input") == 0) bufferAppend(&out, "gXin", 4);
                    else if (strcmp(name, "output") == 0) bufferAppend(&out, "gXout", 5);
                    else if (strcmp(name, "buffer") == 0) bufferAppend(&out, "gXbuf", 5);
                    else bufferAppend(&out, name, strlen(name));
                    if (suffix[0]) bufferAppend(&out, suffix, strlen(suffix));
                    bufferAppend(&out, ";\n", 2);
                    samplerBinding++;
                    rest = ne;
                }
                free(line);
                p = nl ? nl + 1 : p + lineLen;
                curLine++;
                continue;
            }
        }

        //opencode: interface block open (`out NAME {` / `in NAME {`)
        if (!inInterfaceBlock &&
            ((strncmp(t, "out ", 4) == 0) || (strncmp(t, "in ", 3) == 0)) &&
            (strstr(t, "VS_OUT") || strstr(t, "GS_OUT")) && strstr(t, "{")) {
            if (strstr(t, "layout(")) {
                bufferAppend(&out, line, lineLen);
            } else {
                gX_I8 pre[64];
                gX_I32 n = snprintf(pre, sizeof(pre), "layout(location = %u) ", blockBase);
                bufferAppend(&out, pre, (gX_U64)n);
                bufferAppend(&out, line, lineLen);
            }
            bufferAppend(&out, "\n", 1);
            blockBody.length = 0;
            {
                const gX_I8 *brace = strchr(t, '{');
                if (brace) {
                    const gX_I8 *closer = strstr(brace, "}");
                    if (closer && closer > brace) {
                        const gX_U64 blen = (gX_U64)(closer - brace - 1);
                        gX_I8 *body = (gX_I8*)malloc((size_t)blen + 1);
                        if (body) {
                            memcpy(body, brace + 1, (size_t)blen);
                            body[blen] = '\0';
                            blockMemberLocs = vkCountBlockLocations(body);
                            free(body);
                        }
                        nextFreeLoc = blockBase + blockMemberLocs;
                        blockDepth = 0;
                        blockClosed = 1;
                    } else {
                        const gX_I8 *tail = brace + 1;
                        const gX_U64 tlen = lineLen - (gX_U64)(tail - line);
                        if (tlen) bufferAppend(&blockBody, tail, tlen);
                        blockDepth = 1;
                        blockClosed = 0;
                    }
                }
            }
            inInterfaceBlock = (blockDepth > 0);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: inside interface block — accumulate body, copy verbatim
        if (inInterfaceBlock) {
            bufferAppend(&out, line, lineLen);
            bufferAppend(&out, "\n", 1);
            bufferAppend(&blockBody, line, lineLen);
            bufferAppend(&blockBody, "\n", 1);
            for (const gX_I8 *c = line; *c; c++) {
                if (*c == '{') blockDepth++;
                else if (*c == '}') blockDepth--;
            }
            if (blockDepth <= 0) {
                inInterfaceBlock = 0;
                blockClosed = 1;
                if (blockBody.length) {
                    bufferAppend(&blockBody, "\n\0", 2);   //opencode: NUL-terminate for the C-string count
                    blockMemberLocs = vkCountBlockLocations(blockBody.data);
                    nextFreeLoc = blockBase + blockMemberLocs;
                }
                //opencode: flush any flats buffered before the block (vert stage
                //   declares flatNormal before `out VS_OUT`)
                if (flat.length) {
                    s_vkSeqLoc = nextFreeLoc;   //opencode: sync flat loc with block end
                    const gX_I8 *fp = flat.data;
                    while (fp && *fp) {
                        const gX_I8 *nl2 = strchr(fp, '\n');
                        const gX_U64 fl = nl2 ? (gX_U64)(nl2 - fp) : strlen(fp);
                        gX_I8 pre2[64];
                        gX_I32 n2 = snprintf(pre2, sizeof(pre2), "layout(location = %u) ", s_vkSeqLoc);
                        bufferAppend(&out, pre2, (gX_U64)n2);
                        bufferAppend(&out, fp, fl);
                        bufferAppend(&out, "\n", 1);
                        s_vkSeqLoc += vkTypeLocationCount(s_vkSeqType ? s_vkSeqType : "vec4");
                        if (!nl2) break;
                        fp = nl2 + 1;
                    }
                    flat.length = 0;
                }
            }
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: flat in/out → emit at source position if block already
        //   closed (fragment: flat after block), else buffer (vertex: flat
        //   before block, emitted after block close)
        if (strncmp(t, "flat ", 5) == 0) {
            if (blockClosed) {
                gX_I8 pre2[64];
                gX_I32 n2 = snprintf(pre2, sizeof(pre2), "layout(location = %u) flat ", nextFreeLoc);
                bufferAppend(&out, pre2, (gX_U64)n2);
                bufferAppend(&out, skipWs(t + 5), strlen(skipWs(t + 5)));
                bufferAppend(&out, "\n", 1);
                nextFreeLoc++;
                s_vkSeqLoc = nextFreeLoc;
            } else {
                bufferAppend(&flat, skipWs(t + 5), strlen(skipWs(t + 5)));
                bufferAppend(&flat, "\n\0", 2);
            }
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

//opencode: vertex attribute → fixed location 0-5 (per declarator)
        if (isVertex && (strncmp(t, "in ", 3) == 0 || strncmp(t, "in\t", 3) == 0) &&
            strncmp(t, "layout", 6) != 0 && strncmp(t, "flat ", 5) != 0) {
            const gX_I8 *ty = skipWs(t + 2);
            gX_I8 type[64];
            extractIdent(ty, type, sizeof(type));
            const gX_I8 *semi = strchr(ty, ';');
            const gX_I8 *brace = strchr(ty, '{');
            if (strcmp(type, "VS_OUT") != 0 && strcmp(type, "GS_OUT") != 0 &&
                semi && (!brace || semi < brace)) {
                s_vkSeqType = type; vkEmitDeclarators(&out, t, type, vkAttrLoc);
                free(line);
                p = nl ? nl + 1 : p + lineLen;
                curLine++;
                continue;
            }
            bufferAppend(&out, line, lineLen);
            bufferAppend(&out, "\n", 1);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: fragment input (plain `in` matching the vertex's `out`) →
        //   sequential location starting at blockBase (vertex attrs 0-5, then
        //   vertex outs 6+, fragment ins mirror them)
        if (!isVertex && (strncmp(t, "in ", 3) == 0 || strncmp(t, "in\t", 3) == 0) &&
            strncmp(t, "layout", 6) != 0 && strncmp(t, "flat ", 5) != 0) {
            const gX_I8 *ty = skipWs(t + 2);
            gX_I8 type[64];
            extractIdent(ty, type, sizeof(type));
            const gX_I8 *semi = strchr(ty, ';');
            const gX_I8 *brace = strchr(ty, '{');
            if (strcmp(type, "VS_OUT") != 0 && strcmp(type, "GS_OUT") != 0 &&
                !vkIsSamplerType(type) && semi && (!brace || semi < brace)) {
                s_vkSeqType = type; vkEmitDeclarators(&out, t, type, vkSeqLoc);
                free(line);
                p = nl ? nl + 1 : p + lineLen;
                curLine++;
                continue;
            }
            bufferAppend(&out, line, lineLen);
            bufferAppend(&out, "\n", 1);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: vertex output (plain `out` for fullscreen-quad/postprocess
        //   shaders, no interface block) → sequential location at blockBase
        if (isVertex && (strncmp(t, "out ", 4) == 0) && strncmp(t, "layout", 6) != 0 &&
            strncmp(t, "flat ", 5) != 0 && !in_interface_block_scan(line)) {
            const gX_I8 *ty = skipWs(t + 4);
            gX_I8 type[64];
            extractIdent(ty, type, sizeof(type));
            const gX_I8 *semi = strchr(ty, ';');
            const gX_I8 *brace = strchr(ty, '{');
            if (strcmp(type, "VS_OUT") != 0 && strcmp(type, "GS_OUT") != 0 &&
                semi && (!brace || semi < brace)) {
                s_vkSeqType = type; vkEmitDeclarators(&out, t, type, vkSeqLoc);
                free(line);
                p = nl ? nl + 1 : p + lineLen;
                curLine++;
                continue;
            }
            bufferAppend(&out, line, lineLen);
            bufferAppend(&out, "\n", 1);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: fragment output color → location 0
        if (!isVertex && strncmp(t, "out ", 4) == 0 && strncmp(t, "layout", 6) != 0 &&
            strncmp(t, "flat ", 5) != 0) {
            bufferAppend(&out, "layout(location = 0) ", 21);
            bufferAppend(&out, line, lineLen);
            bufferAppend(&out, "\n", 1);
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: reserved-word rename
        if (strstr(line, "input") || strstr(line, "output") ||
            strstr(line, "gl_InstanceID") || strstr(line, "gl_VertexID")) {
            gX_I8 *renamed = vkRenameReserved(line);
            if (renamed) {
                bufferAppend(&out, renamed, strlen(renamed));
                bufferAppend(&out, "\n", 1);
                free(renamed);
            } else {
                bufferAppend(&out, line, lineLen);
                bufferAppend(&out, "\n", 1);
            }
            free(line);
            p = nl ? nl + 1 : p + lineLen;
            curLine++;
            continue;
        }

        //opencode: default — copy verbatim
        bufferAppend(&out, line, lineLen);
        bufferAppend(&out, "\n", 1);
        free(line);
        p = nl ? nl + 1 : p + lineLen;
        curLine++;
    }

    //opencode: flush UBO if never spliced (no function/const found)
    if (haveUniform && uboCount) {
        if (descSet == 0) bufferAppend(&out, "layout(set = 0, binding = 0) uniform UBO_V {\n", 44);
        else              bufferAppend(&out, "layout(set = 1, binding = 0) uniform UBO_F {\n", 44);
        bufferAppend(&out, ubo.data, ubo.length);
        bufferAppend(&out, "};\n", 3);
        ubo.length = 0;
    }
    //opencode: flush any buffered flat vars
    if (flat.length) {
        const gX_I8 *fp = flat.data;
        while (fp && *fp) {
            const gX_I8 *nl2 = strchr(fp, '\n');
            const gX_U64 fl = nl2 ? (gX_U64)(nl2 - fp) : strlen(fp);
            gX_I8 pre2[64];
            gX_I32 n2 = snprintf(pre2, sizeof(pre2), "layout(location = %u) ", s_vkSeqLoc);
            bufferAppend(&out, pre2, (gX_U64)n2);
            bufferAppend(&out, fp, fl);
            bufferAppend(&out, "\n", 1);
            s_vkSeqLoc += vkTypeLocationCount(s_vkSeqType ? s_vkSeqType : "vec4");
            if (!nl2) break;
            fp = nl2 + 1;
        }
        flat.length = 0;
    }

    free(ubo.data);
    free(flat.data);
    free(blockBody.data);
    free(splitBuf);
    if (out.length == 0) { free(out.data); return NULL; }
    bufferAppend(&out, "", 1);
    //opencode: TEMP DEBUG 1I
    if (getenv("GX_VK_TRACE")) {
        FILE *tf = fopen(isVertex ? "/tmp/gx_vk_dialect_vert.txt" : "/tmp/gx_vk_dialect_frag.txt", "a");
        if (tf) { fprintf(tf, "%s", out.data); fprintf(tf, "\n// ---- stage end ----\n"); fclose(tf); }
    }
    return out.data;
}
