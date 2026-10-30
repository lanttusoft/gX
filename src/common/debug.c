/**********************************************************************************************
*
*   [gX: src/common/debug.c]
*   [common/debug.c] — crash traces (registers + stack) and variable watches
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

//opencode: crash path is async-signal-safe where it matters (dprintf +
//   backtrace_symbols_fd; no malloc). Watches are a fixed registry sampled
//   per frame; changes trace as GX_WATCH lines.
#define _GNU_SOURCE
#include "debug.h"

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "log.h"
#include "error.h"
#include "../main.h"

#define GX_DEBUG_WATCH_MAX 16

typedef struct gX_Debug_Watch {
    gX_I8 name[48];
    const gX_I0 *addr;
    gX_Debug_WatchType type;
    gX_I8 last[96];
    gX_I1 used;
    //opencode: optional threshold condition (ADDR:TYPE:OP VALUE) — the watch
    //   then logs the rising edge only, not every change
    gX_I8 condOp[3];
    gX_F64 condValue;
    gX_I1 condWasTrue;
} gX_Debug_Watch;

static gX_Debug_Watch gX_Debug_Watches[GX_DEBUG_WATCH_MAX];
static gX_I32 gX_Debug_WatchCount;
static gX_I1 gX_Debug_Inited;
static gX_I1 gX_Debug_EnvParsed;
static gX_I1 gX_Debug_TestWatchDone;
static gX_I64 gX_Debug_CrashFrame = -1;
static gX_I64 gX_Debug_BacktraceFrame = -1;
static gX_I64 gX_Debug_WatchEvery;
static gX_I1 gX_Debug_BacktraceDone;

// ---------------------------------------------------------------------------
// Crash handler — registers, stack, addr2line recipe
// ---------------------------------------------------------------------------

static gX_I0 gX_Debug_CrashHandler(gX_I32 sig, siginfo_t *info, void *context) {
    (void)info;
    //opencode: no gX_TraceLog here — the FATAL level exits the process before
    //   the register/stack dump; dprintf is async-signal-safe.
    dprintf(STDERR_FILENO, "\nCaught signal %d — gX crashed!\n", sig);
    dprintf(STDERR_FILENO, "=== gX crash: signal %d (%s) ===\n", sig,
        strsignal(sig));
#if defined(__x86_64__)
    {
        ucontext_t *uc = (ucontext_t *)context;
        if (uc) {
            greg_t *g = uc->uc_mcontext.gregs;
            dprintf(STDERR_FILENO,
                "REGISTERS:\n"
                "  RIP=%016llx RSP=%016llx RBP=%016llx EFL=%016llx\n"
                "  RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n"
                "  RSI=%016llx RDI=%016llx R08=%016llx R09=%016llx\n"
                "  R10=%016llx R11=%016llx R12=%016llx R13=%016llx\n"
                "  R14=%016llx R15=%016llx\n",
                (unsigned long long)g[REG_RIP],
                (unsigned long long)g[REG_RSP],
                (unsigned long long)g[REG_RBP],
                (unsigned long long)g[REG_EFL],
                (unsigned long long)g[REG_RAX],
                (unsigned long long)g[REG_RBX],
                (unsigned long long)g[REG_RCX],
                (unsigned long long)g[REG_RDX],
                (unsigned long long)g[REG_RSI],
                (unsigned long long)g[REG_RDI],
                (unsigned long long)g[REG_R8],
                (unsigned long long)g[REG_R9],
                (unsigned long long)g[REG_R10],
                (unsigned long long)g[REG_R11],
                (unsigned long long)g[REG_R12],
                (unsigned long long)g[REG_R13],
                (unsigned long long)g[REG_R14],
                (unsigned long long)g[REG_R15]);
        }
    }
#endif
    //opencode: the recent log lines give the lead-up the register and stack
    //   dumps lack (best-effort: the ring may be partially written)
    {
        extern gX_I0 gX_Log_RingDump(gX_I0);
        gX_Log_RingDump();
    }
    {
        void *frames[64];
        gX_I32 count = backtrace(frames, 64);
        dprintf(STDERR_FILENO, "STACK (%d frames):\n", count);
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
    }
    dprintf(STDERR_FILENO,
        "HINT: resolve frames with: addr2line -e build/gX_build -f -C <addr>\n");
    _exit(128 + sig);
}

gX_I0 gX_Debug_Init(gX_I0) {
    if (gX_Debug_Inited) return;
    gX_Debug_Inited = 1;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = gX_Debug_CrashHandler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGFPE, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
}

gX_I0 gX_Debug_Backtrace(gX_I0) {
    void *frames[64];
    gX_I32 count = backtrace(frames, 64);
    dprintf(STDERR_FILENO, "STACK (%d frames):\n", count);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
}

// ---------------------------------------------------------------------------
// Variable watches
// ---------------------------------------------------------------------------

static gX_I0 gX_Debug_WatchFormat(const gX_Debug_Watch *watch, gX_I8 *out,
    gX_I32 outSize) {
    switch (watch->type) {
        case GX_DEBUG_WATCH_F32:
            snprintf((char *)out, outSize, "%.6f",
                (gX_F64)*(const gX_F32 *)watch->addr);
            break;
        case GX_DEBUG_WATCH_I32:
            snprintf((char *)out, outSize, "%d",
                (gX_I32)*(const gX_I32 *)watch->addr);
            break;
        case GX_DEBUG_WATCH_U32:
            snprintf((char *)out, outSize, "%u",
                (gX_U32)*(const gX_U32 *)watch->addr);
            break;
        case GX_DEBUG_WATCH_U64:
            snprintf((char *)out, outSize, "%llu",
                (unsigned long long)*(const gX_U64 *)watch->addr);
            break;
        case GX_DEBUG_WATCH_V3: {
            const gX_F32 *v = (const gX_F32 *)watch->addr;
            snprintf((char *)out, outSize, "(%.3f, %.3f, %.3f)",
                (gX_F64)v[0], (gX_F64)v[1], (gX_F64)v[2]);
            break;
        }
        case GX_DEBUG_WATCH_COLOR: {
            const gX_U8 *c = (const gX_U8 *)watch->addr;
            snprintf((char *)out, outSize, "(%u, %u, %u, %u)", c[0], c[1],
                c[2], c[3]);
            break;
        }
        default:
            snprintf((char *)out, outSize, "?");
            break;
    }
}

gX_I32 gX_Debug_Watch_Add(const gX_I8 *name, const gX_I0 *addr,
    gX_Debug_WatchType type) {
    if (!name || !name[0] || !addr) return -1;
    if (gX_Debug_WatchCount >= GX_DEBUG_WATCH_MAX) return -1;
    gX_Debug_Watch *watch = &gX_Debug_Watches[gX_Debug_WatchCount++];
    memset(watch, 0, sizeof(*watch));
    snprintf((char *)watch->name, sizeof(watch->name), "%s",
        (const char *)name);
    watch->addr = addr;
    watch->type = type;
    watch->used = 1;
    gX_Debug_WatchFormat(watch, watch->last, sizeof(watch->last));
    gX_TraceLog(gX_LOG_INFO, "GX_WATCH: %s registered (%s)", watch->name,
        (const char *)watch->last);
    return gX_Debug_WatchCount - 1;
}

gX_I0 gX_Debug_Watch_Clear(gX_I0) {
    memset(gX_Debug_Watches, 0, sizeof(gX_Debug_Watches));
    gX_Debug_WatchCount = 0;
}

gX_I32 gX_Debug_Watch_Count(gX_I0) {
    return gX_Debug_WatchCount;
}

const gX_I8 *gX_Debug_Watch_Name(gX_I32 index) {
    if (index < 0 || index >= gX_Debug_WatchCount) return NULL;
    return gX_Debug_Watches[index].name;
}

gX_I0 gX_Debug_Watch_Value(gX_I32 index, gX_I8 *out, gX_I32 outSize) {
    if (index < 0 || index >= gX_Debug_WatchCount || !out || outSize <= 0)
        return;
    gX_Debug_WatchFormat(&gX_Debug_Watches[index], out, outSize);
}

gX_I32 gX_Debug_Watch_AddFrameCount(gX_I0) {
    return gX_Debug_Watch_Add("frameCount", (const gX_I0 *)&maindata.frameCount,
        GX_DEBUG_WATCH_U64);
}

static gX_F64 gX_Debug_WatchNumeric(const gX_Debug_Watch *watch) {
    switch (watch->type) {
        case GX_DEBUG_WATCH_F32: return (gX_F64)*(const gX_F32 *)watch->addr;
        case GX_DEBUG_WATCH_I32: return (gX_F64)*(const gX_I32 *)watch->addr;
        case GX_DEBUG_WATCH_U32: return (gX_F64)*(const gX_U32 *)watch->addr;
        case GX_DEBUG_WATCH_U64: return (gX_F64)*(const gX_U64 *)watch->addr;
        default: return 0.0;
    }
}

static gX_I1 gX_Debug_WatchCondition(const gX_Debug_Watch *watch,
    gX_F64 value) {
    if (!watch->condOp[0]) return false;
    gX_F64 limit = watch->condValue;
    if (strcmp((const char *)watch->condOp, ">") == 0) return value > limit;
    if (strcmp((const char *)watch->condOp, ">=") == 0) return value >= limit;
    if (strcmp((const char *)watch->condOp, "<") == 0) return value < limit;
    if (strcmp((const char *)watch->condOp, "<=") == 0) return value <= limit;
    if (strcmp((const char *)watch->condOp, "==") == 0) return value == limit;
    if (strcmp((const char *)watch->condOp, "!=") == 0) return value != limit;
    return false;
}

static gX_I0 gX_Debug_WatchUpdate(gX_I0) {
    static gX_U64 tick;
    tick++;
    for (gX_I32 i = 0; i < gX_Debug_WatchCount; i++) {
        gX_Debug_Watch *watch = &gX_Debug_Watches[i];
        if (!watch->used) continue;
        gX_I8 value[96];
        gX_Debug_WatchFormat(watch, value, sizeof(value));
        gX_I1 changed = strcmp((const char *)value,
            (const char *)watch->last) != 0;
        gX_I1 sample = gX_Debug_WatchEvery > 0
            && (tick % (gX_U64)gX_Debug_WatchEvery) == 0;
        if (watch->condOp[0]) {
            //opencode: threshold watch — log the rising edge only
            gX_I1 cond = gX_Debug_WatchCondition(watch,
                gX_Debug_WatchNumeric(watch));
            if (cond && !watch->condWasTrue) {
                gX_TraceLog(gX_LOG_INFO, "GX_WATCH: %s=%s crossed %s%g",
                    watch->name, (const char *)value, watch->condOp,
                    watch->condValue);
                snprintf((char *)watch->last, sizeof(watch->last), "%s",
                    (const char *)value);
            }
            watch->condWasTrue = cond;
            continue;
        }
        if (changed || sample) {
            gX_TraceLog(gX_LOG_INFO, "GX_WATCH: %s=%s%s", watch->name,
                (const char *)value, changed ? " changed" : "");
            snprintf((char *)watch->last, sizeof(watch->last), "%s",
                (const char *)value);
        }
    }
}

// ---------------------------------------------------------------------------
// Headless hooks
// ---------------------------------------------------------------------------

static gX_I0 gX_Debug_ParseEnv(gX_I0) {
    gX_Debug_EnvParsed = 1;
    const char *every = getenv("GX_DEBUG_WATCH_FRAMES");
    if (every && every[0]) gX_Debug_WatchEvery = atoll(every);
    const char *crash = getenv("GX_DEBUG_CRASH_TEST");
    if (crash && crash[0]) gX_Debug_CrashFrame = atoll(crash);
    const char *backtrace = getenv("GX_DEBUG_BACKTRACE");
    if (backtrace && backtrace[0]) gX_Debug_BacktraceFrame = atoll(backtrace);
    const char *watch = getenv("GX_DEBUG_WATCH");
    if (!watch || !watch[0]) return;
    gX_I8 buffer[512];
    snprintf((char *)buffer, sizeof(buffer), "%s", watch);
    gX_I8 *save = NULL;
    for (gX_I8 *token = strtok_r((char *)buffer, ",", &save); token;
         token = strtok_r(NULL, ",", &save)) {
        gX_I8 *equals = (gX_I8 *)strchr((const char *)token, '=');
        if (!equals) continue;
        *equals = 0;
        //opencode: ADDR:TYPE[:OP VALUE] — the threshold part is optional
        gX_I8 *addrPart = equals + 1;
        gX_I8 *typeName = NULL;
        gX_I8 *condPart = NULL;
        gX_I8 *colon = (gX_I8 *)strchr((const char *)addrPart, ':');
        if (colon) {
            *colon = 0;
            typeName = colon + 1;
            gX_I8 *condColon = (gX_I8 *)strchr((const char *)typeName, ':');
            if (condColon) {
                *condColon = 0;
                condPart = condColon + 1;
            }
        }
        if (!typeName) continue;
        const gX_I0 *addr = (const gX_I0 *)(uintptr_t)strtoull(
            (const char *)addrPart, NULL, 16);
        gX_Debug_WatchType type = GX_DEBUG_WATCH_U32;
        if (strcmp((const char *)typeName, "f32") == 0)
            type = GX_DEBUG_WATCH_F32;
        else if (strcmp((const char *)typeName, "i32") == 0)
            type = GX_DEBUG_WATCH_I32;
        else if (strcmp((const char *)typeName, "u64") == 0)
            type = GX_DEBUG_WATCH_U64;
        else if (strcmp((const char *)typeName, "v3") == 0)
            type = GX_DEBUG_WATCH_V3;
        else if (strcmp((const char *)typeName, "color") == 0)
            type = GX_DEBUG_WATCH_COLOR;
        if (!addr) continue;
        gX_I32 index = gX_Debug_Watch_Add(token, addr, type);
        if (index < 0 || !condPart || !condPart[0]) continue;
        gX_Debug_Watch *added = &gX_Debug_Watches[index];
        gX_I32 oi = 0;
        while (condPart[oi] && oi < 2
            && (condPart[oi] == '>' || condPart[oi] == '<'
                || condPart[oi] == '=' || condPart[oi] == '!')) {
            added->condOp[oi] = condPart[oi];
            oi++;
        }
        if (oi > 0) added->condValue = atof(condPart + oi);
    }
}

gX_I0 gX_Debug_Update(gX_I0) {
    if (!gX_Debug_EnvParsed) gX_Debug_ParseEnv();
    const char *test = getenv("GX_DEBUG_WATCH_TEST");
    if (test && test[0] == '1' && !gX_Debug_TestWatchDone) {
        gX_I32 index = gX_Debug_Watch_AddFrameCount();
        gX_Debug_TestWatchDone = 1;
        //opencode: GX_DEBUG_WATCH_TEST_THRESHOLD=<N> arms the frameCount
        //   watch with a "> N" condition (gate coverage for thresholds)
        const char *threshold = getenv("GX_DEBUG_WATCH_TEST_THRESHOLD");
        if (index >= 0 && threshold && threshold[0]) {
            gX_Debug_Watch *watch = &gX_Debug_Watches[index];
            snprintf((char *)watch->condOp, sizeof(watch->condOp), ">");
            watch->condValue = atof(threshold);
        }
    }
    if (gX_Debug_BacktraceFrame >= 0 && !gX_Debug_BacktraceDone
        && (gX_I64)maindata.frameCount >= gX_Debug_BacktraceFrame) {
        gX_Debug_Backtrace();
        gX_Debug_BacktraceDone = 1;
    }
    if (gX_Debug_CrashFrame >= 0
        && (gX_I64)maindata.frameCount >= gX_Debug_CrashFrame) {
        volatile gX_I32 *nullPointer = NULL;
        *nullPointer = 1;
    }
    //opencode: GX_LOG_TEST_REPEAT=<N> emits N identical lines once (log
    //   dedup gate coverage; never set it in normal runs)
    {
        static gX_I1 repeatDone = 0;
        const char *repeat = getenv("GX_LOG_TEST_REPEAT");
        if (!repeatDone && repeat && repeat[0]
            && maindata.frameCount >= 5) {
            repeatDone = 1;
            gX_I32 count = atoi(repeat);
            for (gX_I32 i = 0; i < count; i++)
                gX_TraceLog(gX_LOG_INFO, "GX_LOG_TEST: repeat");
        }
    }
    //opencode: GX_DEBUG_ASSERT_TEST=<frame> fires one deliberate failing
    //   assert (gate coverage; never set it in normal runs)
    {
        static gX_I1 assertTestDone = 0;
        const char *assertTest = getenv("GX_DEBUG_ASSERT_TEST");
        if (!assertTestDone && assertTest && assertTest[0]
            && (gX_I64)maindata.frameCount >= atoll(assertTest)) {
            assertTestDone = 1;
            GX_ASSERT(1 == 2);
        }
    }
    gX_Debug_WatchUpdate();
}
