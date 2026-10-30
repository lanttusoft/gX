/**********************************************************************************************
*
*   [gX: src/common/threading.c]
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


//opencode: threading abstraction implementation (POSIX pthreads)
#include "threading.h"

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

//opencode: registry of Self()-allocated handles for cleanup at process exit.
// Linked list keyed by pthread_t value, freed by atexit handler.
typedef struct gX_Thread_SelfHandle {
    pthread_t pt;
    struct gX_Thread_SelfHandle *next;
} gX_Thread_SelfHandle;

static gX_Thread_SelfHandle *gX_Thread_selfHandles = 0;
static pthread_mutex_t gX_Thread_selfRegistryMtx = PTHREAD_MUTEX_INITIALIZER;

static gX_I0 gX_Thread_FreeSelfHandles(gX_I0) {
    pthread_mutex_lock(&gX_Thread_selfRegistryMtx);
    gX_Thread_SelfHandle *h = gX_Thread_selfHandles;
    while (h) {
        gX_Thread_SelfHandle *next = h->next;
        free(h);
        h = next;
    }
    gX_Thread_selfHandles = 0;
    pthread_mutex_unlock(&gX_Thread_selfRegistryMtx);
}

gX_I32 gX_Thread_Create(gX_Thread *thread, gX_Thread_Func func, gX_I0 *arg) {
    if (!thread || !func) return -1;
    pthread_t *pt = (pthread_t*)malloc(sizeof(pthread_t));
    if (!pt) return -1;
    int r = pthread_create(pt, 0, func, arg);
    if (r != 0) { free(pt); return -1; }
    thread->handle = (gX_I0*)pt;
    return 0;
}

gX_I32 gX_Thread_Join(gX_Thread thread) {
    if (!thread.handle) return -1;
    pthread_t *pt = (pthread_t*)thread.handle;
    int r = pthread_join(*pt, 0);
    free(pt);
    return (r == 0) ? 0 : -1;
}

gX_I32 gX_Thread_Self(gX_Thread *thread) {
    if (!thread) return -1;
    //opencode: stash pthread_t in a heap struct that the caller can read but
    // do NOT free (pthread_self handles cannot be joined/freed). The struct
    // is registered globally and released at process exit.
    static gX_I1 atexitRegistered = false;
    if (!atexitRegistered) {
        atexit(gX_Thread_FreeSelfHandles);
        atexitRegistered = true;
    }
    gX_Thread_SelfHandle *h = (gX_Thread_SelfHandle*)malloc(sizeof(gX_Thread_SelfHandle));
    if (!h) return -1;
    h->pt = pthread_self();
    pthread_mutex_lock(&gX_Thread_selfRegistryMtx);
    h->next = gX_Thread_selfHandles;
    gX_Thread_selfHandles = h;
    pthread_mutex_unlock(&gX_Thread_selfRegistryMtx);
    thread->handle = (gX_I0*)&h->pt;
    return 0;
}

gX_I32 gX_Thread_Sleep(gX_U32 milliseconds) {
    usleep((useconds_t)milliseconds * 1000);
    return 0;
}

gX_I32 gX_Mutex_Init(gX_Mutex *mutex) {
    if (!mutex) return -1;
    pthread_mutex_t *mtx = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!mtx) return -1;
    int r = pthread_mutex_init(mtx, 0);
    if (r != 0) { free(mtx); return -1; }
    mutex->handle = (gX_I0*)mtx;
    return 0;
}

gX_I32 gX_Mutex_Lock(gX_Mutex *mutex) {
    if (!mutex || !mutex->handle) return -1;
    return pthread_mutex_lock((pthread_mutex_t*)mutex->handle) == 0 ? 0 : -1;
}

gX_I32 gX_Mutex_Unlock(gX_Mutex *mutex) {
    if (!mutex || !mutex->handle) return -1;
    return pthread_mutex_unlock((pthread_mutex_t*)mutex->handle) == 0 ? 0 : -1;
}

gX_I32 gX_Mutex_Destroy(gX_Mutex *mutex) {
    if (!mutex || !mutex->handle) return -1;
    pthread_mutex_t *mtx = (pthread_mutex_t*)mutex->handle;
    int r = pthread_mutex_destroy(mtx);
    free(mtx);
    mutex->handle = 0;
    return (r == 0) ? 0 : -1;
}

// ─── Work Queue ───

struct gX_WorkQueue {
    gX_I0  **items;
    gX_U32   maxItems;
    gX_U32   head;
    gX_U32   tail;
    gX_U32   count;
    pthread_mutex_t mtx;
    pthread_cond_t  cond;  // signaled when items are added
};

gX_WorkQueue *gX_WorkQueue_Create(gX_U32 maxItems)
{
    gX_WorkQueue *q = (gX_WorkQueue*)calloc(1, sizeof(gX_WorkQueue));
    if (!q) return 0;
    q->items = (gX_I0**)calloc(maxItems, sizeof(gX_I0*));
    if (!q->items) { free(q); return 0; }
    q->maxItems = maxItems;
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->mtx, 0);
    pthread_cond_init(&q->cond, 0);
    return q;
}

gX_I0 gX_WorkQueue_Destroy(gX_WorkQueue *q)
{
    if (!q) return;
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cond);
    free(q->items);
    free(q);
}

gX_I32 gX_WorkQueue_Push(gX_WorkQueue *q, gX_I0 *item)
{
    if (!q) return -1;
    pthread_mutex_lock(&q->mtx);
    if (q->count >= q->maxItems) { pthread_mutex_unlock(&q->mtx); return -2; }
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->maxItems;
    q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

gX_I0 *gX_WorkQueue_Pop(gX_WorkQueue *q)
{
    if (!q) return 0;
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0)
        pthread_cond_wait(&q->cond, &q->mtx);
    gX_I0 *item = q->items[q->head];
    q->head = (q->head + 1) % q->maxItems;
    q->count--;
    pthread_mutex_unlock(&q->mtx);
    return item;
}

gX_I32 gX_WorkQueue_Count(gX_WorkQueue *q)
{
    if (!q) return 0;
    pthread_mutex_lock(&q->mtx);
    gX_U32 c = q->count;
    pthread_mutex_unlock(&q->mtx);
    return (gX_I32)c;
}

// ─── Thread Pool ───

// Internal worker thread: pops items from the queue and processes them
static gX_I0 *gX_ThreadPool_Worker(gX_I0 *arg)
{
    gX_ThreadPool *pool = (gX_ThreadPool*)arg;
    while (pool->running)
    {
        gX_I0 *item = gX_WorkQueue_Pop(pool->queue);
        if (!item) continue;
        pool->workerFunc(item);
    }
    return 0;
}

gX_I32 gX_ThreadPool_Create(gX_ThreadPool *pool, gX_U32 numThreads,
                             gX_ThreadPool_WorkerFunc func, gX_I0 *userData)
{
    if (!pool || !func) return -1;
    pool->queue = gX_WorkQueue_Create(256);
    if (!pool->queue) return -1;
    pool->numThreads = numThreads;
    pool->running = 1;
    pool->workerFunc = func;
    pool->userData = userData;
    pool->threads = (gX_Thread*)calloc(numThreads, sizeof(gX_Thread));
    if (!pool->threads) { gX_WorkQueue_Destroy(pool->queue); return -1; }
    for (gX_U32 i = 0; i < numThreads; i++)
    {
        if (gX_Thread_Create(&pool->threads[i], gX_ThreadPool_Worker, pool) != 0)
        {
            pool->numThreads = i;
            gX_ThreadPool_Destroy(pool);
            return -1;
        }
    }
    return 0;
}

gX_I0 gX_ThreadPool_Destroy(gX_ThreadPool *pool)
{
    if (!pool) return;
    pool->running = 0;
    // Wake all workers so they see running=0 and exit
    for (gX_U32 i = 0; i < pool->numThreads; i++)
        gX_WorkQueue_Push(pool->queue, 0);
    for (gX_U32 i = 0; i < pool->numThreads; i++)
        gX_Thread_Join(pool->threads[i]);
    free(pool->threads);
    gX_WorkQueue_Destroy(pool->queue);
    pool->queue = 0;
    pool->threads = 0;
    pool->numThreads = 0;
}

gX_I32 gX_ThreadPool_Submit(gX_ThreadPool *pool, gX_I0 *item)
{
    if (!pool || !pool->queue) return -1;
    return gX_WorkQueue_Push(pool->queue, item);
}

gX_I0 gX_ThreadPool_Wait(gX_ThreadPool *pool)
{
    if (!pool) return;
    // Spin until queue is empty and all work is done
    while (gX_WorkQueue_Count(pool->queue) > 0)
        gX_Thread_Sleep(1);
}
