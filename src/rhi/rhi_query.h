/**********************************************************************************************
*
*   [gX: src/rhi/rhi_query.h]
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


//opencode: RHI Query — GPU query wrappers for timers and occlusion.
//   Provides centralized access to GL_ARB_timer_query and GL_ARB_occlusion_query.
//   All functions gracefully no-op when the required extension is unavailable.
#ifndef RHI_QUERY_H
#define RHI_QUERY_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_RHI_QUERY_TIMER_NAME 64

// ─── Query object lifecycle ───
// Create a query object for the given target (gX_RHI_TIME_ELAPSED, gX_RHI_ANY_SAMPLES_PASSED,
//   gX_RHI_TIMESTAMP). Returns 0 on failure/unsupported.
gX_U32 gX_RHI_Query_Create(gX_U32 target);
gX_I0  gX_RHI_Query_Delete(gX_U32 queryId);

// ─── Query execution ───
gX_I0 gX_RHI_Query_Begin(gX_U32 target, gX_U32 queryId);
gX_I0 gX_RHI_Query_End(gX_U32 target, gX_U32 queryId);

// ─── Result polling ───
// Non-blocking: returns 1 when result is available, 0 if still pending.
// *result is only valid when return value is 1.
gX_I1 gX_RHI_Query_GetResult(gX_U32 queryId, gX_U64 *result);

// ─── Runtime availability ───
// Returns 1 if the given query target is supported by the current GL driver.
gX_I1 gX_RHI_Query_IsAvailable(gX_U32 target);

// ─── Convenience: gX_RHI_TIMESTAMP pair timer ───
// Uses glQueryCounter(gX_RHI_TIMESTAMP) for accurate GPU timing. Requires
// GL_ARB_timer_query. The timer measures the interval between Begin and End calls.
typedef struct {
    gX_U32 queryIds[2];  // 0 = begin timestamp, 1 = end timestamp
    gX_I1  started;      // set by Begin — Get returns 0 without GL calls when never started
    gX_I1  active;       // Begin issued this frame; End must not overwrite a pending pair
    gX_I1  available;    // set to 1 after successful Get/GetMs
    gX_F64 elapsedMs;    // result in milliseconds
    gX_I8  name[GX_RHI_QUERY_TIMER_NAME];
} gX_RHI_Query_Timer;

gX_I32 gX_RHI_Query_Timer_Init(gX_RHI_Query_Timer *timer, const gX_I8 *name);
gX_I0  gX_RHI_Query_Timer_Begin(gX_RHI_Query_Timer *timer);
gX_I0  gX_RHI_Query_Timer_End(gX_RHI_Query_Timer *timer);
gX_I1  gX_RHI_Query_Timer_Get(gX_RHI_Query_Timer *timer);    // non-blocking poll
gX_F64 gX_RHI_Query_Timer_GetMs(gX_RHI_Query_Timer *timer);   // blocks until ready
gX_I0  gX_RHI_Query_Timer_Deinit(gX_RHI_Query_Timer *timer);

#ifdef __cplusplus
}
#endif

#endif
