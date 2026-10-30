/**********************************************************************************************
*
*   [gX: src/renderer/gfx/profile.c]
*
*   Copyright (c) 2026 lanttu, AI contributors
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


//ai: complete asynchronous query sets retain their source frame/draw; no rendering state, flush or blocking readback changes.
#include "profile.h"
#include "../../rhi/rhi_query.h"
#include "../../rhi/rhi_enums.h"
#include "../../common/alloc.h"
#include "../../common/log.h"
#include <stdio.h>
#include <stdlib.h>


typedef struct gX_GFX_ProfileSlot
{
    gX_I8 name[128];
    gX_I8 description[256];
    gX_RHI_Query_Timer timers[GX_GFX_PROFILE_STAGES];
    gX_U64 frame;
    gX_U64 draw;
    gX_I1 pending;
} gX_GFX_ProfileSlot;

struct gX_RendererGFX_Profile
{
    gX_I8 name[128];
    gX_I8 description[256];
    gX_GFX_ProfileSlot slots[GX_GFX_PROFILE_RING];
    FILE *file;
    gX_U64 nextDraw;
    gX_U64 dropped;
    gX_U32 initialized;
    gX_I32 current;
    gX_I1 noFlush;
    gX_I1 fileFailed;
};

static const gX_I8 *gX_GFX_ProfileNames[GX_GFX_PROFILE_STAGES] =
{
    "shadow", "reflections", "opaque", "pre_ssr", "ssr_inputs", "ssr",
    "pre_bloom", "bloom", "post", "total"
};

//ai: nonblocking collection never publishes a partial set or re-labels cached results as another frame.
static gX_I0 gX_RendererGFX_Profile_Collect(gX_RendererGFX_Profile *Profile, gX_U64 ReadbackFrame)
{
    if (!Profile || Profile->fileFailed) return;
    for (gX_U32 Index = 0; Index < GX_GFX_PROFILE_RING; Index++)
    {
        gX_GFX_ProfileSlot *Slot = &Profile->slots[Index];
        if (!Slot->pending) continue;
        gX_I1 Ready = 1;
        for (gX_U32 Stage = 0; Stage < GX_GFX_PROFILE_STAGES; Stage++)
        {
            if (!gX_RHI_Query_Timer_Get(&Slot->timers[Stage])) Ready = 0;
        }
        if (!Ready) continue;
        gX_I1 Written = fprintf(Profile->file, "%llu,%llu,%llu",
            (unsigned long long)Slot->frame, (unsigned long long)Slot->draw,
            (unsigned long long)ReadbackFrame) >= 0;
        for (gX_U32 Stage = 0; Stage < GX_GFX_PROFILE_STAGES; Stage++)
        {
            if (fprintf(Profile->file, ",%.6f", Slot->timers[Stage].elapsedMs) < 0) Written = 0;
        }
        if (fputc('\n', Profile->file) == EOF) Written = 0;
        if (!Profile->noFlush && fflush(Profile->file) != 0) Written = 0;
        if (!Written)
        {
            Profile->fileFailed = 1;
            gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: CSV write failed");
            return;
        }
        Slot->pending = 0;
    }
}

//ai: initialize only on explicit --gfx-profile; rollback every initialized query on failure.
gX_RendererGFX_Profile *gX_RendererGFX_Profile_Init(const gX_I8 *Path)
{
    if (!Path || !Path[0] || !gX_RHI_Query_IsAvailable(gX_RHI_TIMESTAMP))
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: missing path or unavailable timestamp queries");
        return NULL;
    }
    gX_RendererGFX_Profile *Profile = gX_Calloc(sizeof(*Profile));
    if (!Profile)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: allocation failed");
        return NULL;
    }
    Profile->current = -1;
    Profile->noFlush = getenv("GX_BENCHMARK_NO_FLUSH") != NULL;
    Profile->file = fopen(Path, "w");
    if (!Profile->file)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: cannot open %s", Path);
        gX_Free(Profile);
        return NULL;
    }
    for (gX_U32 Index = 0; Index < GX_GFX_PROFILE_RING; Index++)
    {
        for (gX_U32 Stage = 0; Stage < GX_GFX_PROFILE_STAGES; Stage++)
        {
            if (gX_RHI_Query_Timer_Init(&Profile->slots[Index].timers[Stage], gX_GFX_ProfileNames[Stage]) < 0)
            {
                gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: query initialization failed");
                gX_RendererGFX_Profile_Deinit(Profile, 0);
                return NULL;
            }
            Profile->initialized++;
        }
    }
    if (fprintf(Profile->file, "frame,draw,readback_frame,shadow_ms,reflections_ms,opaque_ms,pre_ssr_ms,ssr_inputs_ms,ssr_ms,pre_bloom_ms,bloom_ms,post_ms,total_ms\n") < 0)
    {
        gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: CSV header write failed");
        gX_RendererGFX_Profile_Deinit(Profile, 0);
        return NULL;
    }
    return Profile;
}

//ai: pending pairs are never reused; overflow is an explicit missing draw, not a stale timing row.
gX_I0 gX_RendererGFX_Profile_Begin(gX_RendererGFX_Profile *Profile, gX_U64 Frame)
{
    if (!Profile || Profile->fileFailed) return;
    gX_RendererGFX_Profile_Collect(Profile, Frame);
    if (Profile->fileFailed) return;
    gX_U64 Draw = Profile->nextDraw++;
    gX_U32 Index = (gX_U32)(Draw % GX_GFX_PROFILE_RING);
    if (Profile->slots[Index].pending)
    {
        Profile->dropped++;
        if (Profile->dropped == 1)
            gX_TraceLog_GFX(gX_LOG_WARNING, "GFX_PROFILE: query ring full; missing frame %llu draw %llu",
                (unsigned long long)Frame, (unsigned long long)Draw);
        Profile->current = -1;
        return;
    }
    Profile->current = (gX_I32)Index;
    Profile->slots[Index].frame = Frame;
    Profile->slots[Index].draw = Draw;
    gX_RendererGFX_Profile_StageBegin(Profile, GX_GFX_PROFILE_TOTAL);
}

//ai: use the RHI timestamp pair helper without additional state changes or submission barriers.
gX_I0 gX_RendererGFX_Profile_StageBegin(gX_RendererGFX_Profile *Profile, gX_U32 Stage)
{
    if (!Profile || Profile->current < 0 || Stage >= GX_GFX_PROFILE_STAGES) return;
    gX_RHI_Query_Timer_Begin(&Profile->slots[Profile->current].timers[Stage]);
}

gX_I0 gX_RendererGFX_Profile_StageEnd(gX_RendererGFX_Profile *Profile, gX_U32 Stage)
{
    if (!Profile || Profile->current < 0 || Stage >= GX_GFX_PROFILE_STAGES) return;
    gX_RHI_Query_Timer_End(&Profile->slots[Profile->current].timers[Stage]);
}

//ai: publication is deferred until every interval from this draw has completed.
gX_I0 gX_RendererGFX_Profile_End(gX_RendererGFX_Profile *Profile)
{
    if (!Profile || Profile->current < 0) return;
    gX_RendererGFX_Profile_StageEnd(Profile, GX_GFX_PROFILE_TOTAL);
    Profile->slots[Profile->current].pending = 1;
    Profile->current = -1;
}

//ai: renderer teardown owns file/query lifetime; incomplete tail draws stay missing rather than forcing GPU completion.
gX_I0 gX_RendererGFX_Profile_Deinit(gX_RendererGFX_Profile *Profile, gX_U64 ReadbackFrame)
{
    if (!Profile) return;
    gX_RendererGFX_Profile_Collect(Profile, ReadbackFrame);
    gX_U32 Pending = 0;
    for (gX_U32 Index = 0; Index < GX_GFX_PROFILE_RING; Index++) Pending += Profile->slots[Index].pending;
    for (gX_U32 Index = 0; Index < Profile->initialized; Index++)
        gX_RHI_Query_Timer_Deinit(&Profile->slots[Index / GX_GFX_PROFILE_STAGES].timers[Index % GX_GFX_PROFILE_STAGES]);
    if (Profile->file && fclose(Profile->file) != 0)
        gX_TraceLog_GFX(gX_LOG_ERROR, "GFX_PROFILE: CSV close failed");
    if (Profile->dropped || Pending)
        gX_TraceLog_GFX(gX_LOG_WARNING, "GFX_PROFILE: missing draws=%llu pending tail=%u",
            (unsigned long long)Profile->dropped, Pending);
    gX_Free(Profile);
}
