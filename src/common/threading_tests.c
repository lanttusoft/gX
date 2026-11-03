/**********************************************************************************************
*
*   [gX: src/common/threading_tests.c]
*   gX — threading primitives and worker-pool tests (CPU only)
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


//ai: headless threading contract — atomic flags, queue order and, most
//   importantly, Wait() must not return while a worker is still processing
//   the last queued item. The old queue-count-only Wait returned as soon as
//   the queue drained, which is a real lost-work race for any caller that
//   destroys or reads results right after Wait.
#include "threading.h"
#include "log.h"

#define THREAD_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            gX_TraceLog(gX_LOG_ERROR, "THREADING_TEST FAIL: %s (line %d)", msg, __LINE__); \
            errors++; \
        } \
    } while (0)

typedef struct gX_ThreadTestJob {
    gX_AtomicI32 *counter;
    gX_I32 delayMs;
} gX_ThreadTestJob;

static gX_I0 thread_test_worker(gX_I0 *arg) {
    gX_ThreadTestJob *job = (gX_ThreadTestJob*)arg;
    if (job->delayMs > 0) gX_Thread_Sleep((gX_U32)job->delayMs);
    gX_AtomicI32_Add(job->counter, 1);
}

static gX_I32 thread_test_submit(gX_ThreadPool *pool, gX_I0 *item) {
    gX_I32 result = 0;
    gX_I32 tries = 0;
    do {
        result = gX_ThreadPool_Submit(pool, item);
        if (result == -2) {
            gX_Thread_Sleep(1);
            tries++;
        }
    } while (result == -2 && tries < 2000);
    return result;
}

static gX_I32 thread_test_pool_wait(gX_I32 errors) {
    gX_AtomicI32 counter;
    gX_ThreadTestJob jobs[8];
    gX_ThreadPool pool;

    gX_AtomicI32_Init(&counter, 0);
    if (gX_ThreadPool_Create(&pool, 2, thread_test_worker, 0) != 0) {
        THREAD_TEST_ASSERT(0, "pool create");
        return errors;
    }
    for (gX_I32 i = 0; i < 8; i++) {
        jobs[i].counter = &counter;
        jobs[i].delayMs = 5;
        THREAD_TEST_ASSERT(thread_test_submit(&pool, &jobs[i]) == 0, "pool submit");
    }
    gX_ThreadPool_Wait(&pool);
    THREAD_TEST_ASSERT(gX_AtomicI32_Load(&counter) == 8,
        "wait completes every submitted job");
    gX_ThreadPool_Destroy(&pool);

    //stress: fill/drain the bounded queue with short jobs
    gX_AtomicI32_Init(&counter, 0);
    if (gX_ThreadPool_Create(&pool, 4, thread_test_worker, 0) != 0) {
        THREAD_TEST_ASSERT(0, "stress pool create");
        return errors;
    }
    gX_ThreadTestJob stress[512];
    for (gX_I32 i = 0; i < 512; i++) {
        stress[i].counter = &counter;
        stress[i].delayMs = 0;
        THREAD_TEST_ASSERT(thread_test_submit(&pool, &stress[i]) == 0, "stress submit");
    }
    gX_ThreadPool_Wait(&pool);
    THREAD_TEST_ASSERT(gX_AtomicI32_Load(&counter) == 512,
        "stress wait completes every job");
    gX_ThreadPool_Destroy(&pool);

    THREAD_TEST_ASSERT(gX_ThreadPool_Create(0, 0, thread_test_worker, 0) != 0,
        "zero-worker pool rejected");
    return errors;
}

static gX_I32 thread_test_queue(gX_I32 errors) {
    gX_WorkQueue *queue = gX_WorkQueue_Create(4);
    gX_I32 values[3] = {11, 22, 33};
    THREAD_TEST_ASSERT(queue != 0, "queue create");
    if (!queue) return errors;
    for (gX_I32 i = 0; i < 3; i++)
        THREAD_TEST_ASSERT(gX_WorkQueue_Push(queue, &values[i]) == 0, "queue push");
    THREAD_TEST_ASSERT(gX_WorkQueue_Count(queue) == 3, "queue count");
    THREAD_TEST_ASSERT(gX_WorkQueue_Pop(queue) == &values[0], "queue FIFO 0");
    THREAD_TEST_ASSERT(gX_WorkQueue_Pop(queue) == &values[1], "queue FIFO 1");
    THREAD_TEST_ASSERT(gX_WorkQueue_Pop(queue) == &values[2], "queue FIFO 2");
    THREAD_TEST_ASSERT(gX_WorkQueue_Count(queue) == 0, "queue drained");
    gX_WorkQueue_Destroy(queue);
    return errors;
}

gX_I32 gX_Tests_Threading(gX_I0) {
    gX_I32 errors = 0;
    errors = thread_test_pool_wait(errors);
    errors = thread_test_queue(errors);
    gX_TraceLog(gX_LOG_INFO, "THREADING_TESTS: %d errors", errors);
    return errors;
}
