/**********************************************************************************************
*
*   [gX: src/common/path.c]
*   gX — a game engine
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


//opencode: multi-tier resource path resolution — search dirs, type aliases, fallback
#include "path.h"
#include "log.h"
#include "shader_compiler.h"
#include "../main.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define GX_PATH_MAX_DIRS    8
#define GX_PATH_MAX_ALIASES 16

//opencode: type alias entry — maps a type to a subdirectory name
typedef struct {
    gX_I8 type[64];     // e.g. "model"
    gX_I8 alias[64];    // e.g. "models"
} gX_PathAlias;

static struct {
    gX_I8  dirs[GX_PATH_MAX_DIRS][256];
    gX_I32 dirCount;
    gX_I1  initialized;
    gX_PathAlias aliases[GX_PATH_MAX_ALIASES];
    gX_I32 aliasCount;
} s_path;

gX_I0 gX_Path_Init(gX_I0)
{
    if (s_path.initialized) return;
    s_path.dirCount = 0;
    s_path.aliasCount = 0;
    s_path.initialized = 1;

    //opencode: register type aliases for common resource directories
    gX_Path_RegisterType("shader",  "shader");
    gX_Path_RegisterType("model",   "model");
    gX_Path_RegisterType("model",   "models");
    gX_Path_RegisterType("texture", "texture");
    gX_Path_RegisterType("texture", "textures");
    gX_Path_RegisterType("font",    "font");
    gX_Path_RegisterType("font",    "fonts");
    gX_Path_RegisterType("audio",   "audio");
    gX_Path_RegisterType("audio",   "sounds");
    gX_Path_RegisterType("music",   "music");
    gX_Path_RegisterType("music",   "songs");

    //opencode: default search dirs — priority order (first = highest)
    gX_Path_AddSearchDir("custom/");         // user overrides
    gX_Path_AddSearchDir("../resources/");   // external guest assets
    gX_Path_AddSearchDir("resources/");      // internal fallback

    gX_TraceLog(gX_LOG_INFO, "PATH: initialized with %d dirs and %d type aliases",
        s_path.dirCount, s_path.aliasCount);

    //opencode: register search dirs as shader include paths for #include resolution
    for (gX_I32 di = 0; di < s_path.dirCount; di++) {
        gX_I8 shaderDir[512];
        gX_I32 n = snprintf(shaderDir, sizeof(shaderDir), "%s%s", s_path.dirs[di], "shader");
        if (n > 0 && (gX_U32)n < sizeof(shaderDir))
            gX_Shader_Compiler_AddIncludePath(shaderDir);
    }
}

gX_I32 gX_Path_AddSearchDir(const gX_I8 *dir)
{
    if (!dir || !dir[0]) return -1;
    if (s_path.dirCount >= GX_PATH_MAX_DIRS) {
        gX_TraceLog(gX_LOG_WARNING, "PATH: max dirs (%d) reached, can't add '%s'",
            GX_PATH_MAX_DIRS, dir);
        return -1;
    }
    gX_I32 n = snprintf(s_path.dirs[s_path.dirCount], sizeof(s_path.dirs[0]), "%s", dir);
    if (n < 0 || (gX_U32)n >= sizeof(s_path.dirs[0])) {
        gX_TraceLog(gX_LOG_WARNING, "PATH: dir name too long '%s'", dir);
        return -1;
    }
    s_path.dirCount++;
    gX_TraceLog(gX_LOG_TRACE, "PATH: added search dir #%d = '%s'", s_path.dirCount - 1, dir);
    return s_path.dirCount - 1;
}

gX_I32 gX_Path_RegisterType(const gX_I8 *type, const gX_I8 *alias)
{
    if (!type || !type[0] || !alias || !alias[0]) return -1;
    if (s_path.aliasCount >= GX_PATH_MAX_ALIASES) {
        gX_TraceLog(gX_LOG_WARNING, "PATH: max aliases (%d) reached, can't add '%s'->'%s'",
            GX_PATH_MAX_ALIASES, type, alias);
        return -1;
    }
    gX_I32 idx = s_path.aliasCount;
    gX_I32 nt = snprintf(s_path.aliases[idx].type,  sizeof(s_path.aliases[idx].type),  "%s", type);
    gX_I32 na = snprintf(s_path.aliases[idx].alias, sizeof(s_path.aliases[idx].alias), "%s", alias);
    if (nt < 0 || na < 0 || (gX_U32)nt >= sizeof(s_path.aliases[idx].type) ||
        (gX_U32)na >= sizeof(s_path.aliases[idx].alias)) {
        gX_TraceLog(gX_LOG_WARNING, "PATH: type/alias too long '%s'->'%s'", type, alias);
        return -1;
    }
    s_path.aliasCount++;
    return idx;
}

static gX_I1 fileExists(const gX_I8 *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

//opencode: v0.3.8 - guest fallback (see path.h). Only reachable on load
//   failure at the call sites, so gX's own assets always win.
const gX_I8 *gX_Path_GuestFallback(const gX_I8 *type, const gX_I8 *fullPath)
{
    if (!type || !fullPath || !fullPath[0]) return NULL;
    gX_I8 seg[96];
    gX_I32 n = snprintf(seg, sizeof(seg), "/%s/", type);
    if (n < 0 || (gX_U32)n >= sizeof(seg)) return NULL;
    const gX_I8 *after = strstr(fullPath, seg);
    if (!after) return NULL;
    return gX_Path_Resolve(type, after + n - 1);
}

//opencode: resolve a resource path by searching registered dirs and type aliases.
//   For each registered search dir (priority order):
//     Phase 1 — try each type alias: dir + alias + "/" + subpath
//     Phase 2 — literal fallback:  dir + subpath
//   Returns a pointer to a static buffer (valid until next call) or NULL.
const gX_I8 *gX_Path_Resolve(const gX_I8 *type, const gX_I8 *subpath)
{
    if (!s_path.initialized) gX_Path_Init();
    if (!type || !subpath) return NULL;

    static gX_I8 result[1024];

    for (gX_I32 di = 0; di < s_path.dirCount; di++) {
        // Phase 1 — try each type alias
        for (gX_I32 ai = 0; ai < s_path.aliasCount; ai++) {
            if (strcmp(s_path.aliases[ai].type, type) != 0) continue;
            gX_I32 n = snprintf(result, sizeof(result), "%s%s/%s",
                s_path.dirs[di], s_path.aliases[ai].alias, subpath);
            if (n > 0 && (gX_U32)n < sizeof(result) && fileExists(result))
                return result;
        }
        // Phase 2 — literal fallback
        gX_I32 n = snprintf(result, sizeof(result), "%s%s",
            s_path.dirs[di], subpath);
        if (n > 0 && (gX_U32)n < sizeof(result) && fileExists(result))
            return result;
    }
    return NULL;
}
