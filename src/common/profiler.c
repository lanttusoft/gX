/**********************************************************************************************
*
*   [gX: src/common/profiler.c]
*   [common/profiler.c] — frame profiler (checkpoint phases + nested zones)
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

//opencode: monotonic clock, fixed entry/zone caps, no allocation. The dump is
//   one trace line per entry so gates can parse it.
#include "profiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "alloc.h"

#define GX_PROFILER_MAX_ENTRIES 96
#define GX_PROFILER_MAX_DEPTH   32
#define GX_PROFILER_HISTORY     120

typedef struct gX_ProfilerSlot {
    gX_I8 name[64];
    gX_U64 frameNs;
    gX_U64 lastNs;
    gX_U32 frameCalls;
    gX_U32 lastCalls;
    gX_I1 used;
} gX_ProfilerSlot;

typedef struct gX_ProfilerZone {
    const gX_I8 *name;
    gX_U64 startNs;
    gX_I32 traceIndex;   //opencode: slot in the trace ring, -1 when off
} gX_ProfilerZone;

//opencode: GX_PROFILE_TRACE ring — one entry per zone instance. Heap-backed
//   (a static ring would blow the lean gate's bss budget) and overwrites the
//   oldest events when full, so long runs keep the tail.
typedef struct gX_ProfilerTraceEvent {
    const gX_I8 *name;
    gX_U64 startNs;
    gX_U64 endNs;
} gX_ProfilerTraceEvent;

#define GX_PROFILER_TRACE_DEFAULT_EVENTS 65536

static gX_ProfilerTraceEvent *gX_Profiler_TraceEvents;
static gX_U32 gX_Profiler_TraceCapacity;
static gX_U32 gX_Profiler_TraceHead;
static gX_U64 gX_Profiler_TraceTotal;
static gX_I8 gX_Profiler_TracePath[512];

//opencode: GX_FRAME_WATCHDOG_MS=<ms> flags frames over the budget with a
//   zone dump + the log tail; GX_FRAME_WATCHDOG_ABORT=1 aborts on the first
//   overrun so the crash handler captures everything.
static gX_F64 gX_Profiler_WatchdogMs = -1.0;
static gX_U64 gX_Profiler_WatchdogHits;
static gX_U64 gX_Profiler_WatchdogLastDump;

static gX_ProfilerSlot gX_Profiler_Slots[GX_PROFILER_MAX_ENTRIES];
static gX_U32 gX_Profiler_SlotCount;
static gX_ProfilerZone gX_Profiler_Zones[GX_PROFILER_MAX_DEPTH];
static gX_I32 gX_Profiler_ZoneDepth;
static gX_U64 gX_Profiler_LastCheckNs;
static gX_U64 gX_Profiler_FrameStartNs;
static gX_U64 gX_Profiler_LastFrameNs;
static gX_U64 gX_Profiler_FrameIndex;
static gX_I32 gX_Profiler_DumpInterval;
static gX_F32 gX_Profiler_History[GX_PROFILER_HISTORY];
static gX_U32 gX_Profiler_HistoryCount;
static gX_U32 gX_Profiler_HistoryHead;

static gX_U64 gX_Profiler_Now(gX_I0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (gX_U64)now.tv_sec * 1000000000ull + (gX_U64)now.tv_nsec;
}

static gX_ProfilerSlot *gX_Profiler_Find(const gX_I8 *name, gX_I1 create) {
    for (gX_U32 i = 0; i < gX_Profiler_SlotCount; i++) {
        if (strcmp((const char *)gX_Profiler_Slots[i].name,
                (const char *)name) == 0)
            return &gX_Profiler_Slots[i];
    }
    if (!create || gX_Profiler_SlotCount >= GX_PROFILER_MAX_ENTRIES)
        return NULL;
    gX_ProfilerSlot *slot = &gX_Profiler_Slots[gX_Profiler_SlotCount++];
    memset(slot, 0, sizeof(*slot));
    snprintf((char *)slot->name, sizeof(slot->name), "%s",
        (const char *)name);
    slot->used = 1;
    return slot;
}

//opencode: lazy trace setup — read once on the first frame reset.
static gX_I0 gX_Profiler_TraceInit(gX_I0) {
    static gX_I1 checked = 0;
    if (checked) return;
    checked = 1;
    const char *path = getenv("GX_PROFILE_TRACE");
    if (!path || !path[0]) return;
    snprintf((char *)gX_Profiler_TracePath, sizeof(gX_Profiler_TracePath),
        "%s", path);
    const char *capEnv = getenv("GX_PROFILE_TRACE_EVENTS");
    gX_U32 capacity = (capEnv && capEnv[0])
        ? (gX_U32)strtoul(capEnv, NULL, 10)
        : GX_PROFILER_TRACE_DEFAULT_EVENTS;
    if (capacity < 64) capacity = 64;
    gX_Profiler_TraceEvents = (gX_ProfilerTraceEvent *)gX_Malloc(
        capacity * (gX_U32)sizeof(gX_ProfilerTraceEvent));
    if (!gX_Profiler_TraceEvents) {
        gX_Profiler_TracePath[0] = '\0';
        gX_TraceLog(gX_LOG_WARNING, "GX_PROFILE_TRACE: ring allocation failed");
        return;
    }
    gX_Profiler_TraceCapacity = capacity;
    gX_TraceLog(gX_LOG_INFO, "GX_PROFILE_TRACE: recording %u events -> %s",
        capacity, gX_Profiler_TracePath);
}

gX_I0 gX_Profiler_Reset(gX_I0) {
    gX_Profiler_TraceInit();
    if (gX_Profiler_WatchdogMs < 0.0) {
        const char *wd = getenv("GX_FRAME_WATCHDOG_MS");
        gX_Profiler_WatchdogMs = (wd && wd[0]) ? atof(wd) : 0.0;
    }
    gX_U64 now = gX_Profiler_Now();
    gX_Profiler_LastFrameNs = (gX_Profiler_FrameStartNs > 0)
        ? now - gX_Profiler_FrameStartNs : 0;
    gX_Profiler_FrameStartNs = now;
    if (gX_Profiler_LastFrameNs > 0) {
        gX_Profiler_History[gX_Profiler_HistoryHead] =
            (gX_F32)((gX_F64)gX_Profiler_LastFrameNs / 1000000.0);
        gX_Profiler_HistoryHead = (gX_Profiler_HistoryHead + 1)
            % GX_PROFILER_HISTORY;
        if (gX_Profiler_HistoryCount < GX_PROFILER_HISTORY)
            gX_Profiler_HistoryCount++;
    }
    //opencode: frame watchdog — the previous frame's duration is known here
    if (gX_Profiler_WatchdogMs > 0.0 && gX_Profiler_LastFrameNs > 0) {
        gX_F64 frameMs = (gX_F64)gX_Profiler_LastFrameNs / 1000000.0;
        if (frameMs > gX_Profiler_WatchdogMs) {
            gX_Profiler_WatchdogHits++;
            const char *abortEnv = getenv("GX_FRAME_WATCHDOG_ABORT");
            if (abortEnv && abortEnv[0] == '1') {
                gX_TraceLog(gX_LOG_ERROR,
                    "GX_WATCHDOG: frame %llu took %.2f ms (budget %.2f ms) "
                    "— aborting",
                    (unsigned long long)gX_Profiler_FrameIndex, frameMs,
                    gX_Profiler_WatchdogMs);
                abort();
            }
            if (gX_Profiler_WatchdogLastDump == 0
                || gX_Profiler_FrameIndex - gX_Profiler_WatchdogLastDump >= 60) {
                gX_Profiler_WatchdogLastDump = gX_Profiler_FrameIndex;
                gX_TraceLog(gX_LOG_WARNING,
                    "GX_WATCHDOG: frame %llu took %.2f ms (budget %.2f ms, "
                    "%llu overruns) — zone dump follows",
                    (unsigned long long)gX_Profiler_FrameIndex, frameMs,
                    gX_Profiler_WatchdogMs,
                    (unsigned long long)gX_Profiler_WatchdogHits);
                gX_Profiler_Dump();
                extern gX_I0 gX_Log_RingDump(gX_I0);
                gX_Log_RingDump();
            }
        }
    }
    gX_Profiler_LastCheckNs = now;
    gX_Profiler_FrameIndex++;
    for (gX_U32 i = 0; i < gX_Profiler_SlotCount; i++) {
        gX_Profiler_Slots[i].lastNs = gX_Profiler_Slots[i].frameNs;
        gX_Profiler_Slots[i].lastCalls = gX_Profiler_Slots[i].frameCalls;
        gX_Profiler_Slots[i].frameNs = 0;
        gX_Profiler_Slots[i].frameCalls = 0;
    }
    if (gX_Profiler_DumpInterval <= 0) {
        const char *interval = getenv("GX_PROFILE_DUMP");
        gX_Profiler_DumpInterval = (interval && interval[0])
            ? atoi(interval) : 0;
    }
    if (gX_Profiler_DumpInterval > 0
        && (gX_Profiler_FrameIndex % (gX_U64)gX_Profiler_DumpInterval) == 0)
        gX_Profiler_Dump();
}

gX_I0 gX_Profiler_Check(const gX_I8 *name) {
    gX_U64 now = gX_Profiler_Now();
    gX_ProfilerSlot *slot = gX_Profiler_Find(name, 1);
    if (slot) {
        slot->frameNs += now - gX_Profiler_LastCheckNs;
        slot->frameCalls++;
    }
    gX_Profiler_LastCheckNs = now;
}

gX_I0 gX_Profiler_ZoneBegin(const gX_I8 *name) {
    if (gX_Profiler_ZoneDepth >= GX_PROFILER_MAX_DEPTH) return;
    gX_ProfilerZone *zone = &gX_Profiler_Zones[gX_Profiler_ZoneDepth++];
    zone->name = name;
    zone->startNs = gX_Profiler_Now();
    zone->traceIndex = -1;
    if (gX_Profiler_TraceEvents) {
        gX_U32 index = gX_Profiler_TraceHead;
        gX_ProfilerTraceEvent *event = &gX_Profiler_TraceEvents[index];
        event->name = name;
        event->startNs = zone->startNs;
        event->endNs = 0;
        gX_Profiler_TraceHead = (gX_Profiler_TraceHead + 1)
            % gX_Profiler_TraceCapacity;
        gX_Profiler_TraceTotal++;
        zone->traceIndex = (gX_I32)index;
    }
}

gX_I0 gX_Profiler_ZoneEnd(const gX_I8 *name) {
    if (gX_Profiler_ZoneDepth <= 0) return;
    gX_ProfilerZone *zone = &gX_Profiler_Zones[--gX_Profiler_ZoneDepth];
    gX_U64 endNs = gX_Profiler_Now();
    gX_U64 delta = endNs - zone->startNs;
    if (zone->traceIndex >= 0 && gX_Profiler_TraceEvents)
        gX_Profiler_TraceEvents[zone->traceIndex].endNs = endNs;
    gX_ProfilerSlot *slot = gX_Profiler_Find(zone->name, 1);
    if (slot) {
        slot->frameNs += delta;
        slot->frameCalls++;
    }
    (void)name;
}

gX_I0 gX_Profiler_TraceWrite(gX_I0) {
    if (!gX_Profiler_TraceEvents || !gX_Profiler_TracePath[0]) return;
    FILE *file = fopen((const char *)gX_Profiler_TracePath, "w");
    if (!file) {
        gX_TraceLog(gX_LOG_WARNING, "GX_PROFILE_TRACE: cannot write '%s'",
            gX_Profiler_TracePath);
        return;
    }
    gX_U32 capacity = gX_Profiler_TraceCapacity;
    gX_U32 count = (gX_Profiler_TraceTotal < capacity)
        ? (gX_U32)gX_Profiler_TraceTotal : capacity;
    gX_U32 first = (gX_Profiler_TraceTotal < capacity)
        ? 0 : gX_Profiler_TraceHead;
    gX_U64 baseNs = 0;
    for (gX_U32 i = 0; i < count; i++) {
        const gX_ProfilerTraceEvent *event =
            &gX_Profiler_TraceEvents[(first + i) % capacity];
        if (event->startNs && (baseNs == 0 || event->startNs < baseNs))
            baseNs = event->startNs;
    }
    fprintf(file, "{\"traceEvents\":[");
    gX_U32 written = 0;
    for (gX_U32 i = 0; i < count; i++) {
        const gX_ProfilerTraceEvent *event =
            &gX_Profiler_TraceEvents[(first + i) % capacity];
        if (!event->name || event->endNs < event->startNs) continue;
        fprintf(file, "%s{\"name\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,"
            "\"dur\":%.3f,\"pid\":1,\"tid\":1}",
            written ? "," : "", event->name,
            (gX_F64)(event->startNs - baseNs) / 1000.0,
            (gX_F64)(event->endNs - event->startNs) / 1000.0);
        written++;
    }
    fprintf(file, "]}\n");
    fclose(file);
    gX_TraceLog(gX_LOG_INFO, "GX_PROFILE_TRACE: %u events -> %s",
        written, gX_Profiler_TracePath);
}

gX_I0 gX_Profiler_Dump(gX_I0) {
    gX_F64 frameMs = (gX_F64)gX_Profiler_LastFrameNs / 1000000.0;
    gX_F32 minMs = 0.0f, maxMs = 0.0f;
    gX_F64 avgMs = 0.0;
    if (gX_Profiler_HistoryCount > 0) {
        minMs = gX_Profiler_History[0];
        for (gX_U32 hi = 0; hi < gX_Profiler_HistoryCount; hi++) {
            gX_F32 sample = gX_Profiler_History[hi];
            if (sample < minMs) minMs = sample;
            if (sample > maxMs) maxMs = sample;
            avgMs += (gX_F64)sample;
        }
        avgMs /= (gX_F64)gX_Profiler_HistoryCount;
    }
    gX_TraceLog(gX_LOG_INFO,
        "GX_PROFILE: frame=%llu total_ms=%.2f min_ms=%.2f avg_ms=%.2f "
        "max_ms=%.2f history=%u entries=%u",
        (unsigned long long)gX_Profiler_FrameIndex, frameMs,
        (gX_F64)minMs, avgMs, (gX_F64)maxMs, gX_Profiler_HistoryCount,
        gX_Profiler_SlotCount);
    for (gX_U32 i = 0; i < gX_Profiler_SlotCount; i++) {
        gX_ProfilerSlot *slot = &gX_Profiler_Slots[i];
        if (!slot->used || slot->lastCalls == 0) continue;
        gX_F64 ms = (gX_F64)slot->lastNs / 1000000.0;
        gX_F64 pct = (gX_Profiler_LastFrameNs > 0)
            ? 100.0 * (gX_F64)slot->lastNs
                / (gX_F64)gX_Profiler_LastFrameNs : 0.0;
        gX_TraceLog(gX_LOG_INFO,
            "GX_PROFILE:   name=%s calls=%u ms=%.3f pct=%.1f",
            (const char *)slot->name, slot->lastCalls, ms, pct);
    }
}

gX_U32 gX_Profiler_EntryCount(gX_I0) {
    return gX_Profiler_SlotCount;
}

const gX_ProfilerEntry *gX_Profiler_Entry(gX_U32 index) {
    static gX_ProfilerEntry entry;
    if (index >= gX_Profiler_SlotCount) return NULL;
    entry.name = gX_Profiler_Slots[index].name;
    entry.lastNs = gX_Profiler_Slots[index].lastNs;
    entry.lastCalls = gX_Profiler_Slots[index].lastCalls;
    return &entry;
}

gX_F64 gX_Profiler_FrameMs(gX_I0) {
    return (gX_F64)gX_Profiler_LastFrameNs / 1000000.0;
}

gX_U32 gX_Profiler_HistoryCopy(gX_F32 *out, gX_U32 max) {
    if (!out || max == 0) return 0;
    gX_U32 count = gX_Profiler_HistoryCount < max
        ? gX_Profiler_HistoryCount : max;
    gX_U32 start = (gX_Profiler_HistoryHead + GX_PROFILER_HISTORY
        - gX_Profiler_HistoryCount) % GX_PROFILER_HISTORY;
    for (gX_U32 hi = 0; hi < count; hi++)
        out[hi] = gX_Profiler_History[(start + hi) % GX_PROFILER_HISTORY];
    return count;
}
