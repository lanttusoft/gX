/**********************************************************************************************
*
*   [gX: src/common/error.c]
*   [common/error.c] — structured error codes + last-error slot
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

//opencode: single last-error slot (the engine is single-threaded apart from
//   asset loaders, which report through their own paths). Set stores and logs;
//   Get is read-only; the trace line is the gate-facing contract.
#include "error.h"
#include <stdlib.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

static gX_ErrorInfo gX_Error_Slot;

static const gX_I8 *gX_Error_Names[GX_ERR_COUNT] = {
    "none",
    "not-found",
    "io",
    "parse",
    "schema",
    "oom",
    "invalid",
    "unsupported",
    "gl",
    "state",
    "assert",
};

gX_I0 gX_Assert_Fail(const gX_I8 *expression, const gX_I8 *file, gX_I32 line,
    const gX_I8 *function) {
    gX_TraceLog(gX_LOG_ERROR, "GX_ASSERT: %s at %s:%d (%s)",
        expression ? expression : "?", file ? file : "?", line,
        function ? function : "?");
    gX_Error_Set(GX_ERR_ASSERT, file, line, "assert failed: %s",
        expression ? expression : "?");
    const char *fatal = getenv("GX_ASSERT_FATAL");
    if (fatal && fatal[0] == '1') abort();
}

gX_I0 gX_Error_Clear(gX_I0) {
    memset(&gX_Error_Slot, 0, sizeof(gX_Error_Slot));
}

const gX_I8 *gX_Error_Name(gX_Error code) {
    if (code < 0 || code >= GX_ERR_COUNT) return "unknown";
    return gX_Error_Names[code];
}

gX_I0 gX_Error_Set(gX_Error code, const gX_I8 *file, gX_I32 line,
    const gX_I8 *format, ...) {
    gX_Error_Slot.code = code;
    snprintf((char *)gX_Error_Slot.file, sizeof(gX_Error_Slot.file), "%s",
        file ? file : "?");
    gX_Error_Slot.line = line;
    va_list args;
    va_start(args, format);
    vsnprintf((char *)gX_Error_Slot.message, sizeof(gX_Error_Slot.message),
        format ? format : "", args);
    va_end(args);
    gX_TraceLog(gX_LOG_ERROR, "GX_ERROR: %s code=%d name=%s at %s:%d",
        (const char *)gX_Error_Slot.message, (gX_I32)code,
        (const char *)gX_Error_Name(code), (const char *)gX_Error_Slot.file,
        line);
}

const gX_ErrorInfo *gX_Error_Get(gX_I0) {
    return &gX_Error_Slot;
}
