//ai: isolated native wrapper around real allocator/heap sources; no engine window or real OOM.
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned AllocationCall;
static unsigned FailAt;

static void *ProbeCalloc(size_t Count, size_t Size)
{
    if (++AllocationCall == FailAt) return NULL;
    return calloc(Count, Size);
}

static void *ProbeRealloc(void *Pointer, size_t Size)
{
    if (++AllocationCall == FailAt) return NULL;
    return realloc(Pointer, Size);
}

#define LOG_H
#define gX_TRACELOG_PROLOGUE_LEVEL(...)
#define gX_TRACELOG_EPILOGUE_LEVEL(...)
#define gX_TRACELOG_EPILOGUE_ERROR(...)
#define gX_TRACELOG_PROLOGUE_MODULE(...)
#define gX_TRACELOG_EPILOGUE_MODULE(...)
#define gX_TraceLog(...) ((void)0)
#define gX_TraceLog_HEAP(...) ((void)0)
#define gX_TraceLog_StackDecrement(...) ((void)0)
#define calloc ProbeCalloc
#define realloc ProbeRealloc
#include "../src/common/alloc.c"
#ifndef GX_HEAP_SOURCE
#define GX_HEAP_SOURCE "../src/common/heap.c"
#endif
#include GX_HEAP_SOURCE
#undef calloc
#undef realloc

static gX_I32 Errors;
static gX_U32 Checks;

static gX_I0 Check(gX_I1 Pass, const gX_I8 *Label)
{
    Checks++;
    if (!Pass)
    {
        fprintf(stderr, "HEAP_PROBE: failed %s\n", Label);
        Errors++;
    }
}

static gX_Heap Fixture(gX_U32 Amount)
{
    gX_Heap Heap = {0};
    Heap.amount = Amount;
    Heap.max = 8;
    Heap.blockSize = 8;
    Heap.state.ENABLE_FILENAME = true;
    Heap.state.ENABLE_FILENAME2 = true;
    return Heap;
}

static gX_I0 FailureCases(gX_I0)
{
    for (gX_U32 Position = 1; Position <= 4; Position++)
    {
        gX_Heap Heap = Fixture(1);
        gX_U64 Live = heaptracker.liveBytes;
        AllocationCall = 0;
        FailAt = Position;
        Check(gX_Heap_Init(&Heap) != 0, "initial allocation reports failure");
        Check(!Heap.state.INITIALIZED && Heap.amount == 0, "failed init has no valid count");
        Check(!Heap.data && !Heap.meta && !Heap.fileName && !Heap.fileName2, "initial failure retains no partial buffers");
        Check(heaptracker.liveBytes == Live, "initial failure frees prepared buffers");
        FailAt = 0;
        gX_Heap_DeInit(&Heap);
    }
    for (gX_U32 Method = 0; Method < 5; Method++)
    {
        for (gX_U32 Position = 1; Position <= 4; Position++)
        {
            gX_Heap Heap = Fixture(2);
            Check(gX_Heap_Init(&Heap) == 0, "growth fixture init");
            memset(Heap.data, 0x5a, 16);
            Heap.meta[0].USED = true;
            Heap.meta[1].USED = true;
            strcpy(Heap.fileName[0], "first");
            strcpy(Heap.fileName2[1], "second");
            gX_Heap Before = Heap;
            gX_U64 Live = heaptracker.liveBytes;
            gX_U64 Count = heaptracker.liveCount;
            gX_I32 Index = 123;
            AllocationCall = 0;
            FailAt = Position;
            if (Method == 0) Check(!gX_Heap_Add_Push_NoPtr(&Heap), "push rejects failed growth");
            if (Method == 1) Check(!gX_Heap_Add_FirstEmpty_NoPtr(&Heap), "first empty rejects failed growth");
            if (Method == 2)
            {
                Check(!gX_Heap_Add_FirstEmpty_NoPtr_GetIndex(&Heap, &Index), "indexed add rejects failed growth");
                Check(Index == -1, "failed indexed growth returns invalid index");
            }
            if (Method == 3) Check(gX_Heap_IncrementMany(&Heap, 3) != 0, "bulk growth rejects failure");
            if (Method == 4) Check(gX_Heap_Decrement(&Heap) != 0, "shrink reports failure");
            Check(memcmp(&Before, &Heap, sizeof(Heap)) == 0, "failed resize preserves pointers count flags refresh");
            Check(((gX_U8 *)Heap.data)[15] == 0x5a && Heap.meta[1].USED, "failed resize preserves data and metadata");
            Check(!strcmp(Heap.fileName[0], "first") && !strcmp(Heap.fileName2[1], "second"), "failed resize preserves filenames");
            Check(heaptracker.liveBytes == Live && heaptracker.liveCount == Count, "failed resize balances allocator tracking");
            FailAt = 0;
            gX_Heap_DeInit(&Heap);
        }
    }
}

static gX_I0 BoundaryCases(gX_I0)
{
    gX_Heap Heap = Fixture(1);
    Check(gX_Heap_Init(&Heap) == 0, "normal fixture init");
    memset(Heap.data, 0x6b, 8);
    Heap.meta[0].USED = true;
    strcpy(Heap.fileName[0], "keep");
    gX_I32 Refresh = Heap.refreshState;
    Check(gX_Heap_IncrementMany(&Heap, 2) == 0 && Heap.amount == 3, "normal bulk growth");
    Check(((gX_U8 *)Heap.data)[7] == 0x6b && Heap.meta[0].USED && !strcmp(Heap.fileName[0], "keep"), "normal growth preserves all prefixes");
    Check(((gX_U8 *)Heap.data)[23] == 0 && !Heap.meta[2].USED && !Heap.fileName[2][0] && !Heap.fileName2[2][0], "new tails are zeroed");
    Check(Heap.refreshState == Refresh + 1, "refresh tracks data relocation");
    Check(gX_Heap_IncrementMany(&Heap, 0) == 0 && Heap.amount == 3, "zero bulk growth");
    Heap.max = Heap.amount;
    gX_Heap Before = Heap;
    Check(!gX_Heap_Add_Push_NoPtr(&Heap) && !memcmp(&Before, &Heap, sizeof(Heap)), "max push preserves old last item");
    Check(gX_Heap_IncrementMany(&Heap, UINT32_MAX) != 0 && Heap.amount == 3, "count overflow rejected");
    gX_Heap_DeInit(&Heap);
    Heap = Fixture(0);
    Heap.blockSize = UINT32_MAX;
    Check(gX_Heap_IncrementMany(&Heap, 2) != 0 && Heap.amount == 0 && !Heap.data, "byte overflow rejected before allocation");
    Heap = Fixture(0);
    AllocationCall = 0;
    FailAt = 2;
    Check(!gX_Heap_Add_Push_NoPtr(&Heap) && Heap.amount == 0 && !Heap.data, "first push rolls back partial allocation");
    FailAt = 0;
    Check(gX_Heap_Add_Push_NoPtr(&Heap) != NULL && Heap.amount == 1, "retry after failure works");
    gX_Heap_DeInit(&Heap);
    Check(heaptracker.liveBytes == 0 && heaptracker.liveCount == 0, "all fixtures release tracked allocations");
}

int main(void)
{
    FailureCases();
    BoundaryCases();
    free(gX_Alloc_Sites);
    gX_Alloc_Sites = NULL;
    printf("HEAP_PROBE: %u checks, %d errors\n", Checks, Errors);
    return Errors != 0;
}
