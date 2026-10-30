/**********************************************************************************************
*
*   [gX: src/rhi/rhi_query.c]
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


//opencode: RHI Query — GPU query implementations.
#include "rhi_query.h"
#include "rhi_gl.h"
#include "../common/log.h"
#include <string.h>

// ─── Runtime availability ───
gX_I1 gX_RHI_Query_IsAvailable(gX_U32 target)
{
    switch (target) {
        case GL_TIMESTAMP:
        case GL_TIME_ELAPSED:
            return (gX_I1)(GLAD_GL_ARB_timer_query != 0);
        case GL_ANY_SAMPLES_PASSED:
        case GL_SAMPLES_PASSED:
            return (gX_I1)(GLAD_GL_VERSION_3_3 != 0 || GLAD_GL_ARB_occlusion_query2 != 0);
        default:
            return 0;
    }
}

// ─── Query lifecycle ───
gX_U32 gX_RHI_Query_Create(gX_U32 target)
{
    if (!gX_RHI_Query_IsAvailable(target)) return 0;
    GLuint id = 0;
    glGenQueries(1, &id);
    return (gX_U32)id;
}

gX_I0 gX_RHI_Query_Delete(gX_U32 queryId)
{
    if (queryId == 0) return;
    GLuint id = (GLuint)queryId;
    glDeleteQueries(1, &id);
}

// ─── Query execution ───
gX_I0 gX_RHI_Query_Begin(gX_U32 target, gX_U32 queryId)
{
    if (queryId == 0) return;
    glBeginQuery((GLenum)target, (GLuint)queryId);
}

gX_I0 gX_RHI_Query_End(gX_U32 target, gX_U32 queryId)
{
    if (queryId == 0) return;
    glEndQuery((GLenum)target);
}

// ─── Result polling ───
gX_I1 gX_RHI_Query_GetResult(gX_U32 queryId, gX_U64 *result)
{
    if (queryId == 0 || !result) return 0;
    GLint available = 0;
    glGetQueryObjectiv((GLuint)queryId, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return 0;
    GLuint64 val = 0;
    glGetQueryObjectui64v((GLuint)queryId, GL_QUERY_RESULT, &val);
    *result = (gX_U64)val;
    return 1;
}

// ─── Convenience: GL_TIMESTAMP pair timer ───
gX_I32 gX_RHI_Query_Timer_Init(gX_RHI_Query_Timer *timer, const gX_I8 *name)
{
    if (!timer || !name) return -1;
    if (!GLAD_GL_ARB_timer_query) return -2;
    glGenQueries(2, timer->queryIds);
    timer->started = 0;
    timer->active = 0;
    timer->available = 0;
    timer->elapsedMs = 0.0;
    strncpy(timer->name, name, GX_RHI_QUERY_TIMER_NAME - 1);
    timer->name[GX_RHI_QUERY_TIMER_NAME - 1] = '\0';
    return 0;
}

gX_I0 gX_RHI_Query_Timer_Begin(gX_RHI_Query_Timer *timer)
{
    if (!GLAD_GL_ARB_timer_query || !timer) return;
    //opencode: preserve a pending timestamp pair instead of overwriting it.
    if (timer->started && !timer->available) {
        timer->active = 0;
        return;
    }
    //opencode: mark started BEFORE the counter — a gated pass (e.g. bloom
    //   disabled) never calls Begin, and Get must not touch the never-started
    //   query: NVIDIA raises GL_INVALID_OPERATION "query object not found"
    //   on glGetQueryObjectiv for queries never used with glQueryCounter.
    timer->started = 1;
    timer->active = 1;
    timer->available = 0;
    glQueryCounter((GLuint)timer->queryIds[0], GL_TIMESTAMP);
}

gX_I0 gX_RHI_Query_Timer_End(gX_RHI_Query_Timer *timer)
{
    if (!GLAD_GL_ARB_timer_query || !timer) return;
    if (!timer->active) return;
    glQueryCounter((GLuint)timer->queryIds[1], GL_TIMESTAMP);
    timer->active = 0;
}

gX_I1 gX_RHI_Query_Timer_Get(gX_RHI_Query_Timer *timer)
{
    if (!GLAD_GL_ARB_timer_query || !timer) return 0;
    if (!timer->started) return 0;
    GLint available = 0;
    glGetQueryObjectiv((GLint)timer->queryIds[1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return 0;
    GLuint64 beginTime = 0, endTime = 0;
    glGetQueryObjectui64v((GLuint)timer->queryIds[0], GL_QUERY_RESULT, &beginTime);
    glGetQueryObjectui64v((GLuint)timer->queryIds[1], GL_QUERY_RESULT, &endTime);
    timer->elapsedMs = (gX_F64)(endTime - beginTime) / 1000000.0;
    timer->available = 1;
    return 1;
}

gX_F64 gX_RHI_Query_Timer_GetMs(gX_RHI_Query_Timer *timer)
{
    if (!GLAD_GL_ARB_timer_query || !timer) return 0.0;
    while (!gX_RHI_Query_Timer_Get(timer)) { }
    return timer->elapsedMs;
}

gX_I0 gX_RHI_Query_Timer_Deinit(gX_RHI_Query_Timer *timer)
{
    if (!timer) return;
    glDeleteQueries(2, timer->queryIds);
    timer->available = 0;
}
