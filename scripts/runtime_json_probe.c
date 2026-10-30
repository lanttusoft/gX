//ai: native parser wrapper for synthetic boundary cases and read-only shipped-preset controls.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static size_t AllocationCall;
static size_t FailAt;

static void *ProbeMalloc(size_t Size)
{
    if (++AllocationCall == FailAt) return NULL;
    return malloc(Size);
}

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

static char *ProbeStrdup(const char *Text)
{
    size_t Size = strlen(Text) + 1;
    char *Copy = ProbeMalloc(Size);
    if (Copy) memcpy(Copy, Text, Size);
    return Copy;
}

#define malloc ProbeMalloc
#define calloc ProbeCalloc
#define realloc ProbeRealloc
#define strdup ProbeStrdup
#ifndef GX_JSON_SOURCE
#define GX_JSON_SOURCE "../src/common/marshal/json_impl.c"
#endif
#include GX_JSON_SOURCE
#undef malloc
#undef calloc
#undef realloc
#undef strdup

int main(int Count, char **Args)
{
    if (Count != 3) return 2;
    if (!strcmp(Args[1], "oom"))
    {
        struct json_object *Control = json_object_from_file(Args[2]);
        if (!Control) return 2;
        size_t Calls = AllocationCall;
        json_object_put(Control);
        for (size_t Position = 1; Position <= Calls; Position++)
        {
            AllocationCall = 0;
            FailAt = Position;
            struct json_object *Value = json_object_from_file(Args[2]);
            if (Value)
            {
                json_object_put(Value);
                fprintf(stderr, "JSON_PROBE: allocation failure %zu returned a partial document\n", Position);
                return 1;
            }
        }
        printf("JSON_PROBE: %zu allocation failures rejected\n", Calls);
        return 0;
    }
    struct json_object *Root = json_object_from_file(Args[2]);
    int Expected = atoi(Args[1]);
    int Accepted = Root != NULL;
    struct json_object *Number = json_object_object_get(Root, "n");
    int Finite = !Number || isfinite(json_object_get_double(Number));
    json_object_put(Root);
    if (Accepted != Expected || !Finite)
    {
        fprintf(stderr, "JSON_PROBE: expected=%d accepted=%d finite=%d\n", Expected, Accepted, Finite);
        return 1;
    }
    return 0;
}
