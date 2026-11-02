/**********************************************************************************************
*
*   [gX: src/common/platform.h]
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
*
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/

//ai: portable process/filesystem primitives. The engine is Linux-first; these
//   inline shims cover the small POSIX/Win32 differences so the rest of the
//   code does not grow a platform #ifdef per call site.

#ifndef GX_COMMON_PLATFORM_H
#define GX_COMMON_PLATFORM_H

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(_WIN32)
    #include <direct.h>
    #include <io.h>
    #include <process.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

//ai: resolved executable path (UTF-8/ANSI on Windows), NUL-terminated.
static inline gX_I32 gX_Platform_ExecutablePath(gX_I8 *out, gX_U32 size)
{
#if defined(_WIN32)
    if (!out || size < 2 || !_pgmptr || !_pgmptr[0]) return -1;
    size_t length = strlen(_pgmptr);
    if (length >= (size_t)size) return -1;
    memcpy(out, _pgmptr, length + 1);
    return (gX_I32)length;
#else
    if (!out || size < 2) return -1;
    ssize_t written = readlink("/proc/self/exe", (char *)out, (size_t)size - 1);
    if (written <= 0) return -1;
    out[written] = '\0';
    return (gX_I32)written;
#endif
}

//ai: one directory level; the caller ignores "already exists" where needed.
static inline gX_I32 gX_Platform_MakeDirectory(const gX_I8 *path)
{
#if defined(_WIN32)
    return _mkdir((const char *)path);
#else
    return mkdir((const char *)path, 0755);
#endif
}

//ai: set or clear one environment variable; a NULL value unsets it.
static inline gX_I32 gX_Platform_SetEnv(const gX_I8 *name, const gX_I8 *value)
{
    if (!name || !name[0]) return -1;
#if defined(_WIN32)
    return _putenv_s((const char *)name, value ? (const char *)value : "");
#else
    if (!value) return unsetenv((const char *)name);
    return setenv((const char *)name, (const char *)value, 1);
#endif
}

static inline gX_I32 gX_Platform_UnsetEnv(const gX_I8 *name)
{
    return gX_Platform_SetEnv(name, NULL);
}

#if defined(_WIN32)
//ai: MinGW has no setenv/unsetenv; the test and parse paths use the POSIX
//   names, so provide the standard two wrappers in this translation unit.
static inline int setenv(const char *name, const char *value, int overwrite)
{
    (void)overwrite;
    return _putenv_s(name, value ? value : "");
}

static inline int unsetenv(const char *name)
{
    return _putenv_s(name, "");
}
#endif

#endif //GX_COMMON_PLATFORM_H
