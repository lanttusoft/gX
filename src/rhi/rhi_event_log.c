/**********************************************************************************************
*
*   [gX: src/rhi/rhi_event_log.c]
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


//opencode: RHI Event Log — ring buffer implementation.
//   Timestamp is wall-clock seconds via clock_gettime (no maindata dependency —
//   RHI must not depend on main.h).
#include "rhi_event_log.h"
#include <string.h>
#include <time.h>

static gX_RHI_Event g_events[GX_RHI_EVENT_LOG_SIZE];
static gX_U32 g_eventHead = 0;
static gX_U32 g_eventCount = 0;
static gX_U32 g_eventDropped = 0;
static gX_U32 g_eventFrame = 0;

static gX_F64 now_seconds(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_F64)ts.tv_sec + (gX_F64)ts.tv_nsec / 1e9;
}

gX_I0 gX_RHI_EventLog_Push(gX_RHI_EventType type,
    gX_U32 id0, gX_U32 id1, gX_U32 id2, const gX_I8 *label)
{
    gX_U32 idx = g_eventHead;
    g_events[idx].timestamp = now_seconds();
    g_events[idx].frameNumber = g_eventFrame;
    g_events[idx].type = type;
    g_events[idx].id0 = id0;
    g_events[idx].id1 = id1;
    g_events[idx].id2 = id2;
    if (label) {
        strncpy(g_events[idx].label, label, GX_RHI_EVENT_LABEL_LEN - 1);
        g_events[idx].label[GX_RHI_EVENT_LABEL_LEN - 1] = '\0';
    } else {
        g_events[idx].label[0] = '\0';
    }
    g_eventHead = (g_eventHead + 1) % GX_RHI_EVENT_LOG_SIZE;
    if (g_eventCount < GX_RHI_EVENT_LOG_SIZE) g_eventCount++;
    else g_eventDropped++;
}

gX_I0 gX_RHI_EventLog_NextFrame(gX_I0) { g_eventFrame++; }

gX_I0 gX_RHI_EventLog_Clear(gX_I0)
{
    g_eventHead = 0;
    g_eventCount = 0;
    g_eventDropped = 0;
}

const gX_RHI_Event *gX_RHI_EventLog_Get(gX_U32 *outCount)
{
    if (outCount) *outCount = g_eventCount;
    return g_events;
}

//ai: RC-L4 — logical accessors: At(0) is the oldest retained event, regardless
//   of where the raw ring wrapped. Get() remains for source compatibility.
gX_U32 gX_RHI_EventLog_Count(gX_I0)
{
    return g_eventCount;
}

const gX_RHI_Event *gX_RHI_EventLog_At(gX_U32 logicalIndex)
{
    if (logicalIndex >= g_eventCount) return 0;
    gX_U32 start = (g_eventHead + GX_RHI_EVENT_LOG_SIZE - g_eventCount)
        % GX_RHI_EVENT_LOG_SIZE;
    return &g_events[(start + logicalIndex) % GX_RHI_EVENT_LOG_SIZE];
}

gX_U32 gX_RHI_EventLog_Dropped(gX_I0)
{
    return g_eventDropped;
}

//ai: RC-L4 regression — 300 pushes retain 256 events in chronological order
//   (44..299), expose the 44 dropped events and reject out-of-range access.
gX_I32 gX_RHI_EventLog_Tests(gX_I0)
{
    gX_I32 errors = 0;
    gX_RHI_EventLog_Clear();
    for (gX_U32 index = 0; index < 300; index++)
    {
        gX_RHI_EventLog_Push(GX_RHI_EVENT_ERROR, index, 0, 0, "sequence");
    }
    if (gX_RHI_EventLog_Count() != GX_RHI_EVENT_LOG_SIZE) errors++;
    if (gX_RHI_EventLog_Dropped() != 44) errors++;
    {
        const gX_RHI_Event *first = gX_RHI_EventLog_At(0);
        const gX_RHI_Event *last = gX_RHI_EventLog_At(255);
        if (!first || first->id0 != 44) errors++;
        if (!last || last->id0 != 299) errors++;
        if (gX_RHI_EventLog_At(256) != 0) errors++;
    }
    gX_RHI_EventLog_Clear();
    if (gX_RHI_EventLog_Count() != 0) errors++;
    if (gX_RHI_EventLog_Dropped() != 0) errors++;
    return errors;
}
