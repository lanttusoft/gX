/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 lanttu, opencode
 * This file is part of gX. See LICENSE.
 */

//ai: external GX-I024 M11F-02/M11F-03 lifecycle witness. It links the
//    sanitized engine archive and wraps malloc so the generated-model owner
//    path and its allocation-failure rollback run under ASan/LSan.
#include "src/renderer/gfx.h"
#include "src/common/log.h"
#include "src/main.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int remaining = 0;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if (remaining > 0 && --remaining == 0) return NULL;
    return __real_malloc(size);
}

gX_MainArguments arguments;
gX_MainData maindata;

int main(int argc, char **argv)
{
    gX_LogState.suppress = 1;
    int control = argc > 1 && strcmp(argv[1], "normal-owner-control") == 0;
    int failure = argc > 1 && strncmp(argv[1], "fail", 4) == 0 ? atoi(argv[1] + 4) : 0;
    gX_RendererGFX *gfx = calloc(1, sizeof(*gfx));
    if (!gfx) return 2;
    gfx->models.blockSize = sizeof(gX_RendererGFX_Model);
    gfx->models.max = 4;
    gfx->models.amount = 1;
    if (gX_Heap_Init(&gfx->models)) return 2;
    int iterations = failure ? 1 : 100;
    for (int index = 0; index < iterations; index++)
    {
        gX_Mesh mesh = {0};
        mesh.vertexCount = 3;
        mesh.triangleCount = 1;
        mesh.vertices = calloc(9, sizeof(float));
        if (!mesh.vertices) return 2;
        mesh.vertices[3] = 1;
        mesh.vertices[7] = 1;
        remaining = failure;
        gX_I32 slot = gX_RendererGFX_Model_AddGeneratedMesh(gfx, mesh);
        remaining = 0;
        if (slot < 0)
        {
            int used = 0;
            for (unsigned int item = 0; item < gfx->models.amount; item++)
                used += gfx->models.meta[item].USED;
            printf("ADD_FAILED allocation=%d used_slots_after_failure=%d\n",
                failure, used);
            free(mesh.vertices);
            break;
        }
        if (failure) return 3;
        gX_RendererGFX_Model *model = gX_Heap_SeekPtr_Index(&gfx->models, (gX_U32)slot);
        if (!model) return 4;
        if (control) model->ownsMesh = 0;
        if (gX_RendererGFX_Model_Remove(gfx, (gX_U32)slot)) return 4;
    }
    gX_Heap_DeInit(&gfx->models);
    free(gfx);
    puts("LIFECYCLE_COMPLETED");
    return 0;
}
