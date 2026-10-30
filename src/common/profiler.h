/**********************************************************************************************
*
*   [gX: src/common/profiler.h]
*   [common/profiler.h] — frame profiler (checkpoint phases + nested zones)
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

#ifndef GX_PROFILER_H
#define GX_PROFILER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: checkpoint phases (main loop) and nested zones (render passes)
//   share one entry table. Reset() snapshots the previous frame; Check() and
//   ZoneBegin/End() accumulate into the current frame. GX_PROFILE_DUMP=<N>
//   traces the last snapshot every N frames (1 = every frame).

typedef struct gX_ProfilerEntry {
    const gX_I8 *name;
    gX_U64 lastNs;
    gX_U32 lastCalls;
} gX_ProfilerEntry;

gX_I0 gX_Profiler_Reset(gX_I0);
gX_I0 gX_Profiler_Check(const gX_I8 *name);
gX_I0 gX_Profiler_ZoneBegin(const gX_I8 *name);
gX_I0 gX_Profiler_ZoneEnd(const gX_I8 *name);
gX_I0 gX_Profiler_Dump(gX_I0);

//opencode: GX_PROFILE_TRACE=<path> records every zone instance into a heap
//   ring and writes a Chrome trace JSON (Perfetto / speedscope) at shutdown.
//   GX_PROFILE_TRACE_EVENTS overrides the ring capacity (default 65536).
gX_I0 gX_Profiler_TraceWrite(gX_I0);

gX_U32 gX_Profiler_EntryCount(gX_I0);
const gX_ProfilerEntry *gX_Profiler_Entry(gX_U32 index);
gX_F64 gX_Profiler_FrameMs(gX_I0);

//opencode: frame-time history ring (chronological copy for plots/dumps).
gX_U32 gX_Profiler_HistoryCopy(gX_F32 *out, gX_U32 max);

//opencode: the historical macro names keep their call sites (53 zones).
#define GX_PROFILE_ZONE_BEGIN(name) gX_Profiler_ZoneBegin(name)
#define GX_PROFILE_ZONE_END(name)   gX_Profiler_ZoneEnd(name)

#ifdef __cplusplus
}
#endif

#endif //GX_PROFILER_H
