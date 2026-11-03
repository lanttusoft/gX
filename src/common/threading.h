/**********************************************************************************************
*
*   [gX: src/common/threading.h]
*   gX — a game engine
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


//opencode: threading abstraction (pthread-based)
#ifndef GX_THREADING_H
#define GX_THREADING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

typedef gX_I0* (*gX_Thread_Func)(gX_I0 *arg);

typedef struct gX_Thread {
    gX_I0 *handle;
} gX_Thread;

typedef struct gX_Mutex {
    gX_I0 *handle;
} gX_Mutex;

gX_I32 gX_Thread_Create(gX_Thread *thread, gX_Thread_Func func, gX_I0 *arg);
gX_I32 gX_Thread_Join(gX_Thread thread);
gX_I32 gX_Thread_Self(gX_Thread *thread);
gX_I32 gX_Thread_Sleep(gX_U32 milliseconds);

gX_I32 gX_Mutex_Init(  gX_Mutex *mutex);
gX_I32 gX_Mutex_Lock(  gX_Mutex *mutex);
gX_I32 gX_Mutex_Unlock(gX_Mutex *mutex);
gX_I32 gX_Mutex_Destroy(gX_Mutex *mutex);

// ─── Atomics ───
// Plain 32-bit atomic word for cross-thread completion flags and counters.
// Implemented with GCC/Clang __atomic builtins (GCC and winpthreads builds);
// the fallback is a plain word for compilers without them.
typedef gX_I32 gX_AtomicI32;

gX_I0  gX_AtomicI32_Init(gX_AtomicI32 *value, gX_I32 initial);
gX_I32 gX_AtomicI32_Load(const gX_AtomicI32 *value);
gX_I0  gX_AtomicI32_Store(gX_AtomicI32 *value, gX_I32 next);
gX_I32 gX_AtomicI32_Add(gX_AtomicI32 *value, gX_I32 delta);

// ─── Work Queue (thread-safe producer-consumer queue) ───
// Items are pushed by producers, popped by consumers (workers).
// The queue blocks on pop when empty.

typedef struct gX_WorkQueue gX_WorkQueue;

gX_WorkQueue *gX_WorkQueue_Create(gX_U32 maxItems);
gX_I0        gX_WorkQueue_Destroy(gX_WorkQueue *q);
gX_I32       gX_WorkQueue_Push(gX_WorkQueue *q, gX_I0 *item);
gX_I0       *gX_WorkQueue_Pop(gX_WorkQueue *q);       // blocks until item available
gX_I32       gX_WorkQueue_Count(gX_WorkQueue *q);      // number of items pending

// ─── Thread Pool ───
// A set of N worker threads that process items from a work queue.
// Each worker calls a user-provided function for each item.
// Wait() blocks until every submitted item has completed (queue empty AND
// no worker still processing); Destroy() must be called after Wait().

typedef gX_I0 (*gX_ThreadPool_WorkerFunc)(gX_I0 *item);

typedef struct gX_ThreadPool {
    gX_Thread     *threads;
    gX_WorkQueue  *queue;
    gX_U32         numThreads;
    gX_AtomicI32   running;
    gX_AtomicI32   pending;   // submitted but not yet completed
    gX_ThreadPool_WorkerFunc workerFunc;
    gX_I0         *userData;
} gX_ThreadPool;

gX_I32  gX_ThreadPool_Create(gX_ThreadPool *pool, gX_U32 numThreads,
                             gX_ThreadPool_WorkerFunc func, gX_I0 *userData);
gX_I0   gX_ThreadPool_Destroy(gX_ThreadPool *pool);
gX_I32  gX_ThreadPool_Submit(gX_ThreadPool *pool, gX_I0 *item);
gX_I0   gX_ThreadPool_Wait(gX_ThreadPool *pool);  // blocks until all submitted items complete

#ifdef __cplusplus
}
#endif

#endif //GX_THREADING_H
