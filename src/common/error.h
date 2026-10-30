/**********************************************************************************************
*
*   [gX: src/common/error.h]
*   [common/error.h] — structured error codes + last-error slot
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

#ifndef GX_ERROR_H
#define GX_ERROR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: boundary error codes (docs/error_handling.md). Status functions
//   keep their 0/1 returns and set the last-error slot on failure; boundary
//   APIs may return gX_Error directly where callers branch on the reason.
typedef enum {
    GX_ERR_NONE = 0,
    GX_ERR_NOT_FOUND,
    GX_ERR_IO,
    GX_ERR_PARSE,
    GX_ERR_SCHEMA,
    GX_ERR_OOM,
    GX_ERR_INVALID,
    GX_ERR_UNSUPPORTED,
    GX_ERR_GL,
    GX_ERR_STATE,
    GX_ERR_ASSERT,
    GX_ERR_COUNT
} gX_Error;

typedef struct gX_ErrorInfo {
    gX_Error code;
    gX_I8 message[256];
    gX_I8 file[128];
    gX_I32 line;
} gX_ErrorInfo;

//opencode: store + log one failure. The trace line carries code= and the
//   enum name so gates can assert codes instead of prose.
gX_I0 gX_Error_Set(gX_Error code, const gX_I8 *file, gX_I32 line,
    const gX_I8 *format, ...);
gX_I0 gX_Error_Clear(gX_I0);
const gX_ErrorInfo *gX_Error_Get(gX_I0);
const gX_I8 *gX_Error_Name(gX_Error code);

#define GX_ERROR_SET(code_, ...) \
    gX_Error_Set((code_), __FILE__, __LINE__, __VA_ARGS__)

//opencode: GX_ASSERT — a development invariant, not a boundary check.
//   Failures log the expression/file/line/function, set the last-error slot
//   (code=assert) and, with GX_ASSERT_FATAL=1, abort so the crash handler
//   captures a full trace. Boundary failures should keep using GX_ERROR_SET
//   with a return (docs/error_handling.md).
gX_I0 gX_Assert_Fail(const gX_I8 *expression, const gX_I8 *file, gX_I32 line,
    const gX_I8 *function);

#define GX_ASSERT(cond_) \
    do { \
        if (!(cond_)) \
            gX_Assert_Fail(#cond_, __FILE__, __LINE__, __func__); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif //GX_ERROR_H
