/**********************************************************************************************
*
*   [gX: src/renderer/gfx/scene.c]
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


//opencode: extracted from gfx.c — scene management and model picking
#include <stdlib.h>
#include <string.h>

#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../gfx.h"
#include "../../main.h"
#include <math.h>
#include "../../camera.h"
#include "../../wrappers/wrappers.h"
#include "../../common/path.h"
#include "scene.h"
#include "model.h"
#include "schema.h"
#include "light.h"
#include "boilerplate.h"
#include "texture.h"
#include "decal.h"

//opencode: isolate two editable cubes for the nearest-model picker regression.
static gX_I0 gX_RendererGFX_Scene_PickerOverlap(gX_RendererGFX *gfx)
{
    const gX_I8 *enabled = getenv("GX_GFX_PICKER_OVERLAP");
    if (!enabled || !enabled[0] || enabled[0] == '0') return;
    if (!gfx || !gfx->models.data || !gfx->models.meta || gfx->models.amount < 9
        || !gfx->models.meta[1].USED || !gfx->models.meta[4].USED) return;
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model *)gfx->models.data;
    gX_F32v3 backPosition = {-4.5f, 1.075f, -0.5f};
    gX_F32v3 cameraDirection = gX_F32v3_Normalize(gX_F32v3_Sub(
        camera.camera3D.position, camera.camera3D.target));
    gX_F32v3 frontOffset = gX_F32v3_Scale(cameraDirection, 0.8f);
    models[1].params.pos = backPosition;
    models[1].params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
    models[1].params.rotation = (gX_F32v3){0.0f, 0.0f, 0.0f};
    models[4].params.pos = (gX_F32v3){100.0f, 100.0f, 100.0f};
    gX_RendererGFX_Model *frontModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
    if (!frontModel) return;
    gX_I8 path[256];
    snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
    gX_RendererGFX_Model_Init(frontModel, path);
    frontModel->params.pos = gX_F32v3_Add(backPosition, frontOffset);
    frontModel->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
    frontModel->params.rotation = (gX_F32v3){0.0f, 0.0f, 0.0f};
    models = (gX_RendererGFX_Model *)gfx->models.data;
    gX_I32 frontIndex = (gX_I32)(frontModel - models);
    gX_TraceLog(gX_LOG_INFO, "GFX_PICKER_OVERLAP: back=1 front=%d", frontIndex);
}

//opencode: raycast model picking — game-viewport pixels, returns model index or -1
gX_I32 gX_RendererGFX_Model_Pick(gX_RendererGFX *gfx, gX_F32v2 screenPos, gX_F32 *outDistance)
{
    gX_Ray ray = gX_Mouse_GetRay(screenPos, camera.camera3D);
    gX_I32 closestIdx = -1;
    gX_F32 closestDist = 999999.0f;

    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
        if (!model->loaded) continue;

        gX_Model m = model->model;
        //opencode: include rotation in picking transform (R * T * S, matches model draw)
        gX_Matrix matScale = gX_Matrix_Scale(model->params.scale.x, model->params.scale.y, model->params.scale.z);
        gX_Matrix matRot = gX_Matrix_RotateXYZ((gX_F32v3){
            model->params.rotation.x * gX_DEG2RAD,
            model->params.rotation.y * gX_DEG2RAD,
            model->params.rotation.z * gX_DEG2RAD
        });
        gX_Matrix matTranslation = gX_Matrix_Translate(model->params.pos.x, model->params.pos.y, model->params.pos.z);
        gX_Matrix trs = gX_Matrix_Multiply(matScale, matRot);   // S * R
        trs = gX_Matrix_Multiply(trs, matTranslation);           // S * R * T
        //opencode: Model_Draw replaces m.transform with trs, so picking uses
        //   that exact draw transform rather than composing imported state.
        gX_Matrix transform = trs;

        for (gX_I32 meshIdx = 0; meshIdx < m.meshCount; meshIdx++)
        {
            gX_RayCollision rc = gX_RayCollision_Mesh(ray, m.meshes[meshIdx], transform);
            if (rc.hit && rc.distance < closestDist)
            {
                closestDist = rc.distance;
                closestIdx = (gX_I32)(model - (gX_RendererGFX_Model*)gfx->models.data);
            }
        }
    }

    if (outDistance) *outDistance = closestDist;
    return closestIdx;
}

//opencode: clear all scene data (load/unload models, lights, textures for scene switching)
gX_I32 gX_RendererGFX_ClearScene(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Model_RemoveAll(gfx);
    gX_RendererGFX_Light_RemoveAll(gfx);
    gX_RendererGFX_Texture_RemoveAll(gfx);
    gX_RendererGFX_Decal_RemoveAll(gfx);
    //opencode: keep maps[0] (shadow map) alive, just clear the rest
    for (gX_U32 i = 1; i < gfx->maps.amount; i++)
    {
        if (gfx->maps.meta[i].USED) gfx->maps.meta[i].USED = false;
    }
    return 0;
}

//opencode: Debug-only opt-in two-light shader regression fixture.
#ifndef NDEBUG
static gX_I0 gX_RendererGFX_Scene_LayoutLightTest(gX_RendererGFX *gfx)
{
    const gX_I8 *mode = getenv("GX_LAYOUT_LIGHT_TEST");
    if (!mode || !mode[0]) return;
    const gX_I8 *config = getenv("GX_LAYOUT_LIGHT_CONFIG");
    gX_I1 backface = config && strcmp(config, "backface") == 0;
    gfx->sun.brightness = 0.0f;
    gfx->sun.diffuseFactor = 0.0f;
    gfx->sun.specularFactor = 0.0f;
    gfx->state.ENABLE_LIGHTING_AMBIENT = 0;
    gfx->state.ENABLE_LIGHTING_DIFFUSE = 1;
    gfx->state.ENABLE_LIGHTING_SPECULAR = 0;
    gfx->state.ENABLE_PHONG = 1;
    gfx->state.ENABLE_MAP_ALBEDO = 0;
    gfx->state.ENABLE_MAP_METALNESS = 0;
    gfx->state.ENABLE_MAP_NORMAL = 0;
    gfx->state.ENABLE_MAP_PARALLAX = 0;
    gfx->state.ENABLE_DETAIL = 0;
    gfx->state.ENABLE_FOG = 0;
    gfx->state.ENABLE_SHADOWS = 0;
    gfx->state.ENABLE_REFLECTIONS = 0;
    gfx->state.ENABLE_PLANAR_REFLECTIONS = 0;
    gfx->state.ENABLE_BLOOM = 0;
    gfx->state.ENABLE_SSR = 0;
    gfx->state.ENABLE_SSR_AUXVIEW = 0;
    gfx->state.ENABLE_MOTION_BLUR = 0;
    gfx->state.ENABLE_DOF = 0;
    gfx->state.ENABLE_TAA = 0;
    gfx->state.ENABLE_SSAO = 0;
    gfx->state.ENABLE_RIM = 0;
    gfx->state.ENABLE_ENV_AMBIENT = 0;
    gfx->state.ENABLE_GODRAYS = 0;
    gfx->state.ENABLE_PARTICLES = 0;
    gfx->state.ENABLE_VOLUME_FOG = 0;
    gfx->state.ENABLE_LENS_FLARE = 0;
    gfx->state.ENABLE_POINT_SHADOWS = 0;
    gfx->state.ENABLE_COOKIES = 0;
    gfx->state.ENABLE_PROCEDURAL_SKY = 0;
    gfx->state.ENABLE_SKYBOX = 0;
    gfx->state.ENABLE_OCCLUSION_CULLING = 0;
    {
        gX_I1 usePBR = strcmp(getenv("GX_LAYOUT_LIGHT_MODEL")
            ? getenv("GX_LAYOUT_LIGHT_MODEL") : "pbr", "pbr") == 0;
        gX_RendererGFX_Model *models = gfx->models.data;
        static const gX_F32v3 modelPositions[] = {
            {0.0f, -100.0f, 0.0f}, {-1.5f, 0.7f, 0.0f},
            {-0.5f, 0.7f, 0.0f}, {0.5f, 0.7f, 0.0f}, {1.5f, 0.7f, 0.0f},
        };
        static const gX_F32v3 backfacePositions[] = {
            {0.0f, 0.0f, 0.0f}, {0.0f, -100.0f, 0.0f},
            {0.0f, -100.0f, 0.0f}, {0.0f, -100.0f, 0.0f},
            {0.0f, -100.0f, 0.0f},
        };
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
            gX_U32 modelIndex = (gX_U32)(model - models);
            model->params.usePBR = usePBR;
            model->params.metalness = 0.0f;
            model->params.roughness = 0.7f;
            model->params.emissiveFactor = 0.0f;
            model->params.reflectionSource = 0;
            model->params.rotation = (gX_F32v3){0.0f, 0.0f, 0.0f};
            if (modelIndex < sizeof(modelPositions) / sizeof(modelPositions[0])) {
                model->params.pos = backface
                    ? backfacePositions[modelIndex] : modelPositions[modelIndex];
                model->params.scale = backface
                    ? (modelIndex == 0 ? (gX_F32v3){4.0f, 0.1f, 4.0f}
                        : (gX_F32v3){0.001f, 0.001f, 0.001f})
                    : (modelIndex == 0 ? (gX_F32v3){0.001f, 0.001f, 0.001f}
                        : (gX_F32v3){0.8f, 0.8f, 0.8f});
            }
        }
    }
    {
        gX_RendererGFX_Light *lightBase = gfx->lights.data;
        if (gfx->lights.amount < 2 || !gfx->lights.meta[0].USED
            || !gfx->lights.meta[1].USED) {
            gX_TraceLog(gX_LOG_ERROR, "LAYOUT_LIGHT_TEST: expected two live lights");
            return;
        }
        for (gX_U32 li = 0; li < 2; li++) {
            lightBase[li].type = 1;
            lightBase[li].pos = (gX_F32v3){0.0f,
                backface && li == 1 ? -4.0f : 4.0f, backface ? 0.0f : 3.0f};
            lightBase[li].brightness = 1.2f;
            lightBase[li].diffuseFactor = 1.0f;
            lightBase[li].specularFactor = 0.0f;
            lightBase[li].attenConst = 1.0f;
            lightBase[li].attenLinear = 0.0f;
            lightBase[li].attenQuad = 0.0f;
        }
        lightBase[0].color = (gX_Color){255, 40, 40, 255};
        lightBase[1].color = (gX_Color){40, 80, 255, 255};
        lightBase[0].diffuseTint = lightBase[0].color;
        lightBase[1].diffuseTint = lightBase[1].color;
        lightBase[0].specularTint = lightBase[0].color;
        lightBase[1].specularTint = lightBase[1].color;
        gX_I1 removeBoth = strcmp(mode, "removeBoth") == 0;
        gX_I1 removeLight = removeBoth || strcmp(mode, "remove0") == 0
            || strcmp(mode, "remove1") == 0 || strcmp(mode, "reuse0") == 0
            || strcmp(mode, "reuse1") == 0;
        gX_U32 removedIndex = strcmp(mode, "remove1") == 0
            || strcmp(mode, "reuse1") == 0 ? 1 : 0;
        if (removeLight) {
            gX_RendererGFX_Light savedLight = lightBase[removedIndex];
            if (gX_RendererGFX_Light_Remove(gfx, removedIndex) != 0) {
                gX_TraceLog(gX_LOG_ERROR, "LAYOUT_LIGHT_TEST: remove failed");
                return;
            }
            if (removeBoth && gX_RendererGFX_Light_Remove(gfx, 1) != 0) {
                gX_TraceLog(gX_LOG_ERROR, "LAYOUT_LIGHT_TEST: second remove failed");
                return;
            }
            if (strcmp(mode, "reuse0") == 0 || strcmp(mode, "reuse1") == 0) {
                gX_I32 reusedIndex = gX_RendererGFX_Light_Add(gfx);
                if (reusedIndex != (gX_I32)removedIndex) {
                    gX_TraceLog(gX_LOG_ERROR,
                        "LAYOUT_LIGHT_TEST: expected reuse slot %u, got %d",
                        removedIndex, reusedIndex);
                    return;
                }
                lightBase = gfx->lights.data;
                lightBase[reusedIndex] = savedLight;
            }
        }
    }
    gX_U32 usedCount = 0;
    for (gX_U32 li = 0; li < gfx->lights.amount; li++)
        if (gfx->lights.meta[li].USED) usedCount++;
    gX_TraceLog(gX_LOG_INFO,
        "LAYOUT_LIGHT_TEST: mode=%s model=%s quality=%d used=%u span=%u",
        mode, getenv("GX_LAYOUT_LIGHT_MODEL") ? getenv("GX_LAYOUT_LIGHT_MODEL") : "pbr",
        gfx->state.shaderQuality, usedCount, gfx->lights.amount);
}
#endif

//opencode: populate the default GFX demo scene (--gfx / --showcase). Loads a
//   curated set of models from resources/model with per-model materials plus a
//   few coloured point lights, so the scene has visible content (it was empty
//   — only the white sphere marker — which made the default view wash out).
//ai: shipped project defaults retain demo slots; new normal-map fixtures do not require quarantine files.
gX_I32 gX_RendererGFX_Scene_AddDemoContent(gX_RendererGFX *gfx)
{
    if (gfx->common.demoSceneAdded) return 0;
    gfx->common.demoSceneAdded = true;

    extern gX_MainArguments arguments;

    //opencode: hunt - headless harness hooks live HERE (not at the end of
    //   this function): the verification scenes (layout-test, reflectrow,
    //   minimal/planar) return early and never reached the old spot, so
    //   GX_SSR_DEBUG / GX_T_* silently did nothing exactly where the
    //   verification harnesses needed them (SSR diagnostic modes rendered
    //   beauty instead). State-only hooks; EXTRA LIGHTS stay at the end.
    if (getenv("GX_T_SSR")) gfx->state.ENABLE_SSR = (gX_I1)atoi(getenv("GX_T_SSR"));
    if (getenv("GX_T_REFL")) gfx->state.ENABLE_REFLECTIONS = (gX_I1)atoi(getenv("GX_T_REFL"));
    if (getenv("GX_T_SSRAUX")) gfx->state.ENABLE_SSR_AUXVIEW = (gX_I1)atoi(getenv("GX_T_SSRAUX"));
    if (getenv("GX_SSR_DEBUG")) gfx->state.ssrDebugMode = atoi(getenv("GX_SSR_DEBUG"));
    //opencode: oracle - occlusion-culling force hook (blank-main diagnosis)
    if (getenv("GX_T_OCCL")) gfx->state.ENABLE_OCCLUSION_CULLING = (gX_I1)atoi(getenv("GX_T_OCCL"));
    //opencode: oracle - tonemap force hook (numeric debug echoes must reach
    //   the capture un-remapped: ACES/AgX/gamma would corrupt absolute UVs)
    if (getenv("GX_T_TONEMAP")) {
        gX_I1 t = (gX_I1)atoi(getenv("GX_T_TONEMAP"));
        gfx->state.ENABLE_ACES_TONEMAP = t;
        gfx->state.ENABLE_AGX_TONEMAP = t;
        gfx->state.ENABLE_GAMMA_CORRECTION = t;
    }
    //opencode: hunt - RHI per-draw validation (FBO completeness, program,
    //   viewport, NaN uniforms) for headless driver-bug triage
    if (getenv("GX_VALIDATE")) {
        extern gX_I1 g_rhiValidateEnabled;
        g_rhiValidateEnabled = (gX_I1)atoi(getenv("GX_VALIDATE"));
    }

    //ai: depth-oracle fixture contains one axis-aligned unit-half-extent raster cube.
    if (getenv("GX_RAYMARCH_DEPTH_TEST"))
    {
        gX_RendererGFX_Model *model = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (!model) return 1;
        gX_RendererGFX_Model_Init(model,GX_RESOURCE_DIR "/model/primitive/cube.glb");
        model->params.pos = (gX_F32v3){0};
        model->params.scale = (gX_F32v3){1,1,1};
        model->params.rotation = (gX_F32v3){0};
        //ai: a directly visible green target supplies an attributable hybrid SSR reflection.
        const gX_I8 *reflectionProbe = getenv("GX_RAYMARCH_DEPTH_TEST");
        if (!strncmp(reflectionProbe,"reflect",7))
        {
            model->params.pos = (gX_F32v3){!strcmp(reflectionProbe,"reflect-empty") ? 100.0f : -1.8f,0,4};
            model->params.scale = (gX_F32v3){0.45f,0.7f,0.45f};
            model->params.usePBR = 1;
            model->params.metalness = 0;
            model->params.roughness = 0.6f;
            model->params.emissiveColor = (gX_Color){0,255,0,255};
            model->params.emissiveFactor = 1.0f;
        }
        return 0;
    }

    //opencode: v0.1.24 — --showcase-minimal / --showcase-planar — reflection
    //   verification scene. Exactly two models: a near-mirror floor slab and
    //   ONE floating cube, so every reflection pixel is attributable and the
    //   "exactly one reflection" property is checkable by probe scripts
    //   (scripts/probe_reflect_verify.py). The planar variant swaps the
    //   floor's SSR for the exact planar-reflection RT path, giving the
    //   verification harness a mathematically-correct oracle to diff against.
    //   Camera defaults are the standard orbit (yaw 45 pitch 25 dist 6).
    //opencode: v0.3.4 - --reflectrow reflection testbed (plane + ball row).
    //   One planar mirror floor + spheres sweeping the material space, so
    //   every SSR/planar/auxview behaviour is attributable to one parameter.
    //   A small tilted FLOATING cube behind the row acts as the disocclusion
    //   probe: from the default camera its underside is off-view, and the
    //   mirror tries to reflect exactly that hidden face (the aux target).
    //opencode: --layout-test — deliberately broken scene for the auto-seat
    //   pass: one sunk cube (through the slab), two overlapping cubes, one
    //   floater (must stay). --layout-check verifies before>0 / after==0.
    if (arguments.layoutTest) {
        gX_RendererGFX_Model *floorModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (floorModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(floorModel, path);
            floorModel->params.pos = (gX_F32v3){0.0f, -0.075f, 0.0f};
            floorModel->params.scale = (gX_F32v3){8.0f, 0.15f, 8.0f};
            floorModel->params.usePBR = 1;
            floorModel->params.metalness = 1.0f;
            floorModel->params.shininess = 256.0f;
            floorModel->params.roughness = 0.02f;
            floorModel->params.reflectionSource = 1;
            floorModel->params.reflectionPlaneHeight = 0.075f;
            floorModel->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
        }
        {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model *sunk = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (sunk) {
                gX_RendererGFX_Model_Init(sunk, path);
                sunk->params.pos = (gX_F32v3){-2.0f, -0.5f, 0.0f};
                sunk->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
                sunk->params.usePBR = 1;
                sunk->params.metalness = 0.1f;
                sunk->params.shininess = 32.0f;
                sunk->params.roughness = 0.7f;
            }
            gX_RendererGFX_Model *ovA = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (ovA) {
                gX_RendererGFX_Model_Init(ovA, path);
                ovA->params.pos = (gX_F32v3){2.0f, 0.575f, 0.0f};
                ovA->params.scale = (gX_F32v3){0.5f, 0.5f, 0.5f};
                ovA->params.usePBR = 1;
                ovA->params.metalness = 0.1f;
                ovA->params.shininess = 32.0f;
                ovA->params.roughness = 0.7f;
            }
            gX_RendererGFX_Model *ovB = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (ovB) {
                gX_RendererGFX_Model_Init(ovB, path);
                ovB->params.pos = (gX_F32v3){2.2f, 0.575f, 0.0f};
                ovB->params.scale = (gX_F32v3){0.5f, 0.5f, 0.5f};
                ovB->params.usePBR = 1;
                ovB->params.metalness = 0.1f;
                ovB->params.shininess = 32.0f;
                ovB->params.roughness = 0.7f;
            }
        }
        {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
            gX_RendererGFX_Model *floater = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (floater) {
                gX_RendererGFX_Model_Init(floater, path);
                floater->params.pos = (gX_F32v3){-2.0f, 2.0f, -2.0f};
                floater->params.scale = (gX_F32v3){0.5f, 0.5f, 0.5f};
                floater->params.usePBR = 1;
                floater->params.metalness = 1.0f;
                floater->params.shininess = 256.0f;
                floater->params.roughness = 0.02f;
            }
        }
        static const gX_F32v3 ltLightPos[] = {{2.2f, 2.6f, 1.8f}, {-2.2f, 2.2f, -1.4f}};
        static const gX_Color ltLightCol[] = {{255,120,90,255}, {100,180,255,255}};
        for (gX_U32 li = 0; li < 2; li++) {
            gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
            if (!l) continue;
            gX_RendererGFX_Light_Init(l);
            l->type = 1;
            l->pos = ltLightPos[li];
            l->color = ltLightCol[li];
            l->diffuseTint = ltLightCol[li];
            l->specularTint = ltLightCol[li];
            l->brightness = 4.0f;
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.08f;
        }
#ifndef NDEBUG
        gX_RendererGFX_Scene_LayoutLightTest(gfx);
#endif
        gX_TraceLog(gX_LOG_NOTICE, "LAYOUT-TEST: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }
    if (arguments.showcaseReflectRow) {
        //opencode: Paper 1 M2 parity fixture — remove mirror/specular and
        //   emissive response so the raster HDR pass can be compared to the
        //   CPU diffuse reference without production-material shortcuts.
        gX_I1 oracleGpu = getenv("GX_T_ORACLE_GPU")
            && getenv("GX_T_ORACLE_GPU")[0] != '0'
            && gX_RendererGFX_OracleGPU_Enabled();
        gX_I1 oracleReflection = oracleGpu
            && getenv("GX_T_ORACLE_GPU_REFLECTION")
            && getenv("GX_T_ORACLE_GPU_REFLECTION")[0] != '0';
        gX_I1 oracleMatte = (getenv("GX_T_ORACLE_MATTE")
            && getenv("GX_T_ORACLE_MATTE")[0] != '0')
            || (oracleGpu && !oracleReflection);
        gX_I1 oracleFixture = oracleMatte || oracleReflection;
        gX_RendererGFX_Model *floorModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (floorModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(floorModel, path);
            floorModel->params.pos = (gX_F32v3){0.0f, -0.075f, 0.0f};
            floorModel->params.scale = (gX_F32v3){14.0f, 0.15f, 8.0f};
            floorModel->params.usePBR = 1;
            floorModel->params.metalness = oracleMatte ? 0.0f : 1.0f;
            floorModel->params.shininess = 256.0f;
            floorModel->params.roughness = 0.02f;
            //opencode: v0.3.4 - slab owns the mirror VIEW (fill excludes it
            //   via reflectionSource) while staying pure-SSR shaded here, so
            //   standard.frag never consumes reflTex on the slab itself
            floorModel->params.reflectionSource = 1;
        }

        typedef struct gX_BallSpec { float m; float r; float sh; int tr,tg,tb; } gX_BallSpec;
        static const gX_BallSpec balls[] = {
            {1.00f, 0.02f, 256.0f, 255,255,255},
            {1.00f, 0.15f, 200.0f, 255,220,180},
            {1.00f, 0.40f, 120.0f, 255,190,120},
            {0.00f, 0.05f, 200.0f, 255,255,255},
            {0.00f, 0.80f, 16.0f,  255,140,140},
            {0.30f, 0.08f, 160.0f, 140,230,255},
        };
        const gX_I32 nBalls = (gX_I32)(sizeof(balls)/sizeof(balls[0]));
        for (gX_I32 i = 0; i < nBalls; i++) {
            gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (!m) continue;
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
            gX_RendererGFX_Model_Init(m, path);
            m->params.pos = (gX_F32v3){-5.0f + 2.0f * i, 0.42f, 0.0f};
            m->params.scale = (gX_F32v3){0.7f, 0.7f, 0.7f};
            m->params.usePBR = 1;
            m->params.metalness = oracleMatte ? 0.0f : balls[i].m;
            m->params.roughness = balls[i].r;
            m->params.shininess = balls[i].sh;
            m->params.emissiveColor = (gX_Color){balls[i].tr, balls[i].tg, balls[i].tb, 255};
            m->params.emissiveFactor = oracleFixture ? 0.0f : 0.25f;
        }

        gX_RendererGFX_Model *probeModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (probeModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(probeModel, path);
            //opencode: v0.3.5 - floating probe: lifted clear of the slab so
            //   its UNDERSIDE never enters the main view (camera looks down);
            //   only the mirrored-camera aux target can supply that face.
            //   Cyan emissive marks the whole body so reflection content is
            //   attributable at a glance.
            //opencode: v0.3.5b - pushed right-and-back so its reflection
            //   lands on open floor past the ball row (occlusion test)
            //opencode: v0.3.5c - legibility config: diffuse cyan body (no
            //   metal env-masking), closer and lower so the mirrored image
            //   stays large and unforeshortened
            probeModel->params.pos = (gX_F32v3){3.1f, 1.65f, -3.4f};
            probeModel->params.scale = (gX_F32v3){0.8f, 0.8f, 0.8f};
            probeModel->params.rotation = (gX_F32v3){22.0f, 25.0f, 8.0f};
            probeModel->params.usePBR = 1;
            probeModel->params.metalness = 0.0f;
            probeModel->params.roughness = 0.5f;
            probeModel->params.shininess = 16.0f;
            probeModel->params.emissiveColor = (gX_Color){40,225,255,255};
            probeModel->params.emissiveFactor = oracleFixture ? 0.0f : 1.45f;
        }
        if (oracleFixture) {
            gfx->state.ENABLE_LIGHTING_AMBIENT = 0;
            gfx->state.ENABLE_LIGHTING_SPECULAR = 0;
        }

        static const gX_F32v3 rrLightPos[] = {{3.2f, 2.6f, 2.2f}, {-3.2f, 2.2f, -1.8f}};
        static const gX_Color rrLightCol[] = {{255,130,90,255}, {110,190,255,255}};
        for (int li = 0; li < 2; li++) {
            gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
            if (!l) continue;
            gX_RendererGFX_Light_Init(l);
            l->type = 1;
            l->pos = rrLightPos[li];
            l->color = rrLightCol[li];
            l->diffuseTint = rrLightCol[li];
            l->specularTint = rrLightCol[li];
            l->brightness = 4.0f;
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.08f;
            //opencode: Paper 1 M2 parity A/B — isolate the directional and
            //   point-light terms without changing the production fixture.
            if (getenv("GX_T_ORACLE_POINTS") && getenv("GX_T_ORACLE_POINTS")[0] == '0')
                l->brightness = 0.0f;
        }
        gX_TraceLog(gX_LOG_NOTICE, "REFLECTROW: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }

    //opencode: --showcase-y2k — native visual direction study using only
    //   shipped primitives: stepped abstract terrain, glass, brushed metal,
    //   neon accents, twilight sky, and a restrained realtime post stack.
    if (arguments.showcaseY2K)
    {
        typedef struct gX_Y2KModelSpec {
            const gX_I8 *file;
            gX_F32v3 pos;
            gX_F32v3 scale;
            gX_F32v3 rotation;
            gX_Color tint;
            gX_F32 metalness;
            gX_F32 roughness;
            gX_F32 emissionFactor;
            gX_Color emissionColor;
            gX_I1 glass;
            gX_I1 mirror;
        } gX_Y2KModelSpec;
        //opencode: compact composition keeps the stepped forms, violet spire,
        //   cyan ring, and glass sphere as the foreground focal group.
        static const gX_Y2KModelSpec specs[] = {
            //opencode: floor lifted out of black-crush (was 18,24,64 / 0.78 /
            //   0.18: dark + metallic under a 0.08 sun reads near-black).
            {"primitive/cube.glb", {0.0f, -0.075f, 0.0f}, {14.0f, 0.15f, 10.0f}, {0,0,0}, {42,58,118,255}, 0.62f, 0.30f, 0.0f, {0,0,0,255}, 0, 1},
            {"primitive/cube.glb", {-4.0f, 0.30f, -1.7f}, {2.8f, 0.35f, 1.2f}, {0,-18,0}, {55,140,200,255}, 0.45f, 0.32f, 0.0f, {0,0,0,255}, 0, 0},
            {"primitive/cube.glb", {-1.2f, 0.72f, -1.0f}, {1.7f, 0.72f, 1.0f}, {0,14,0}, {65,225,210,255}, 0.30f, 0.30f, 0.0f, {0,0,0,255}, 0, 0},
            {"primitive/cone.glb", {2.5f, 1.05f, -1.8f}, {1.2f, 2.1f, 1.2f}, {0,20,0}, {150,60,220,255}, 0.25f, 0.35f, 0.35f, {190,70,255,255}, 0, 0},
            {"primitive/torus.glb", {1.1f, 0.65f, 2.5f}, {1.35f, 1.35f, 1.35f}, {70,0,0}, {60,180,255,255}, 0.70f, 0.18f, 0.70f, {30,210,255,255}, 0, 0},
            {"primitive/sphere.glb", {4.0f, 1.0f, 1.1f}, {0.9f, 0.9f, 0.9f}, {0,0,0}, {180,240,255,255}, 0.10f, 0.12f, 0.0f, {0,0,0,255}, 1, 0},
        };
        static const gX_F32v3 lightPos[] = {{4.0f, 4.0f, 2.0f}, {-4.0f, 3.0f, -1.0f}, {0.0f, 5.0f, 4.0f}, {0.0f, 2.0f, -4.0f}};
        static const gX_Color lightCol[] = {{60,220,255,255}, {255,50,210,255}, {255,170,80,255}, {120,100,255,255}};
        static const gX_F32 lightBri[] = {7.0f, 6.0f, 3.5f, 3.0f};
        for (gX_U32 i = 0; i < (gX_U32)(sizeof(specs) / sizeof(specs[0])); i++)
        {
            const gX_Y2KModelSpec *s = &specs[i];
            gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (!m) continue;
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/%s", s->file);
            gX_RendererGFX_Model_Init(m, path);
            m->params.pos = s->pos;
            m->params.scale = s->scale;
            m->params.rotation = s->rotation;
            m->params.usePBR = 1;
            m->params.albedoTint = s->tint;
            m->params.metalness = s->metalness;
            m->params.roughness = s->roughness;
            m->params.emissiveColor = s->emissionColor;
            m->params.emissiveFactor = s->emissionFactor;
            if (s->mirror)
            {
                m->params.reflectionSource = 1;
                m->params.reflectionPlaneHeight = 0.075f;
                m->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
            }
            if (s->glass)
            {
                m->params.enableBlending = 1;
                m->params.blendOpacity = 0.34f;
                m->params.enableRefraction = 1;
                m->params.refractiveIndex = 1.5f;
                m->params.enableReflections = 1;
            }
        }
        for (gX_U32 i = 0; i < (gX_U32)(sizeof(lightPos) / sizeof(lightPos[0])); i++)
        {
            gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
            if (!l) continue;
            gX_RendererGFX_Light_Init(l);
            l->type = 1;
            l->pos = lightPos[i];
            l->color = lightCol[i];
            l->diffuseTint = lightCol[i];
            l->specularTint = lightCol[i];
            l->brightness = lightBri[i];
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.06f;
        }
        gfx->sun.brightness = 0.12f;
        gfx->sun.color = (gX_Color){150, 170, 255, 255};
        gfx->sun.diffuseTint = gfx->sun.color;
        gfx->sun.specularTint = gfx->sun.color;
        //opencode: keep the procedural sun below the horizon; point lights
        //   own the surfaces while the sky supplies the midnight-blue field.
        gfx->sun.dir = (gX_F32v3){0.25f, 0.90f, 0.35f};
        gfx->sky.color = (gX_Color){25, 35, 95, 255};
        gfx->sky.sunIntensity = 0.12f;
        gfx->sky.density = 1.8f;
        //opencode: sky_new.frag consumes hue in degrees; deep blue keeps the
        //   twilight backdrop in the Y2K palette instead of drifting red.
        gfx->sky.hue = 220.0f;
        gfx->sky.horizon = 0.65f;
        gfx->sky.contrast = 1.35f;
        gfx->sky.starDensity = 0.35f;
        gfx->sky.starBrightness = 0.90f;
        gfx->fogVolume.density = 0.012f;
        gfx->fogVolume.heightFalloff = 1.4f;
        gfx->fogVolume.scattering = 0.45f;
        gfx->state.SHOW_LIGHT_MARKERS = 0;
        gX_TraceLog(gX_LOG_NOTICE, "SHOWCASE-Y2K: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }

    //opencode: --ledrow night LED testbed. Moonlit mirror slab (owns the
    //   mirror view like reflectrow) + non-emissive PBR teapot centerpiece
    //   (orbiting colors must MODEL on it) + metalness-sweep ball row +
    //   8-LED rainbow ring (emissiveFactor ~3 clears the 0.85 bloom extract)
    //   + 4-light rig in fixed order: 2 orbiters, 1 hue-cycler, 1 strobe.
    //   Static Y rests every base on the slab top (+0.075); pristine scene
    //   (no auto-seat). Animation lives in gX_Showcase_Update by heap index.
    if (arguments.showcaseLedRow) {
        //opencode: opt-in material-normal reflection fixture. The production
        //   LED row stays untouched; the hook injects the shipped project normal
        //   map into colored models so SSR's rasterized normal path can be
        //   inspected independently from the water normal-map path.
        gX_I32 ledNormalMapID = -1;
        gX_I32 ledHeightMapID = -1;
        gX_I1 materialNormalOn = 0;
        gX_I1 materialParallaxOn = 0;
        gX_I32 materialParallaxMode = 2;
        {
            const gX_I8 *normalHook = getenv("GX_T_SSR_NORMALMAP");
            const gX_I8 *materialHook = getenv("GX_T_MATERIAL_MAPS");
            const gX_I8 *parallaxHook = getenv("GX_T_PARALLAX_MODE");
            materialNormalOn = (normalHook && normalHook[0] != '0')
                || (materialHook
                    && (strcmp((const char *)materialHook, "normal") == 0
                        || strcmp((const char *)materialHook, "parallax") == 0));
            materialParallaxOn = materialHook
                && strcmp((const char *)materialHook, "parallax") == 0;
            if (parallaxHook && parallaxHook[0]) {
                materialParallaxMode = atoi((const char *)parallaxHook);
                if (materialParallaxMode < 0) materialParallaxMode = 0;
                if (materialParallaxMode > 2) materialParallaxMode = 2;
            }
            if (materialNormalOn) {
                gX_I8 normalPath[256];
                snprintf(normalPath, sizeof(normalPath), GX_RESOURCE_DIR
                    "/texture/texture_normal.png");
                ledNormalMapID = gX_RendererGFX_Texture_Add(gfx, normalPath);
            }
            if (materialParallaxOn) {
                gX_I8 heightPath[256];
                snprintf(heightPath, sizeof(heightPath), GX_RESOURCE_DIR
                    "/texture/texture_displace.png");
                ledHeightMapID = gX_RendererGFX_Texture_Add(gfx, heightPath);
                gfx->state.ENABLE_MAP_PARALLAX = 1;
            }
        }
        gX_RendererGFX_Model *floorModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (floorModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(floorModel, path);
            floorModel->params.pos = (gX_F32v3){0.0f, -0.075f, 0.0f};
            floorModel->params.scale = (gX_F32v3){14.0f, 0.15f, 8.0f};
            floorModel->params.usePBR = 1;
            floorModel->params.metalness = 1.0f;
            floorModel->params.shininess = 256.0f;
            floorModel->params.roughness = 0.03f;
            floorModel->params.reflectionSource = 1;
            floorModel->params.reflectionPlaneHeight = 0.075f;
            floorModel->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
        }
        gX_RendererGFX_Model *potModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (potModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/teapot.glb");
            gX_RendererGFX_Model_Init(potModel, path);
            potModel->params.pos = (gX_F32v3){0.0f, 0.675f, 0.0f};
            potModel->params.scale = (gX_F32v3){0.30f, 0.30f, 0.30f};
            potModel->params.usePBR = 1;
            potModel->params.metalness = 0.85f;
            potModel->params.shininess = 96.0f;
            potModel->params.roughness = 0.30f;
            potModel->params.injectedNormalMapID = ledNormalMapID;
            if (ledHeightMapID >= 0) {
                potModel->textureID[0] = ledHeightMapID;
                potModel->texturePurpose[0] = gX_MATERIAL_MAP_HEIGHT;
                potModel->params.heightMapFactor = 0.3f;
                potModel->params.parallaxMode = materialParallaxMode;
            }
        }
        static const gX_F32 sweepMetal[] = {0.0f, 0.5f, 1.0f};
        static const gX_F32 sweepX[] = {-2.6f, 0.0f, 2.6f};
        for (gX_I32 bi = 0; bi < 3; bi++) {
            gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (!m) continue;
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
            gX_RendererGFX_Model_Init(m, path);
            //opencode: back row (was front): front balls occluded the LED
            //   ring from the pinned camera (only 5 of 8 visible).
            m->params.pos = (gX_F32v3){sweepX[bi], 0.42f, -3.6f};
            m->params.scale = (gX_F32v3){0.7f, 0.7f, 0.7f};
            m->params.usePBR = 1;
            m->params.metalness = sweepMetal[bi];
            m->params.roughness = 0.25f;
            m->params.shininess = 128.0f;
            m->params.injectedNormalMapID = ledNormalMapID;
            if (ledHeightMapID >= 0) {
                m->textureID[0] = ledHeightMapID;
                m->texturePurpose[0] = gX_MATERIAL_MAP_HEIGHT;
                m->params.heightMapFactor = 0.3f;
                m->params.parallaxMode = materialParallaxMode;
            }
        }
        for (gX_I32 li = 0; li < 8; li++) {
            gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (!m) continue;
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
            gX_RendererGFX_Model_Init(m, path);
            gX_F32 a = (gX_F32)li * 0.78539816f;
            m->params.pos = (gX_F32v3){cosf(a) * 2.6f, 0.32f, sinf(a) * 2.6f};
            m->params.scale = (gX_F32v3){0.22f, 0.22f, 0.22f};
            m->params.usePBR = 1;
            m->params.metalness = 0.0f;
            m->params.roughness = 0.4f;
            m->params.shininess = 16.0f;
            m->params.emissiveColor = gX_Color_FromHSV((gX_F32)li * 45.0f, 1.0f, 1.0f);
            m->params.emissiveFactor = 3.4f;
            m->params.injectedNormalMapID = ledNormalMapID;
            if (ledHeightMapID >= 0) {
                m->textureID[0] = ledHeightMapID;
                m->texturePurpose[0] = gX_MATERIAL_MAP_HEIGHT;
                m->params.heightMapFactor = 0.3f;
                m->params.parallaxMode = materialParallaxMode;
            }
        }
        static const gX_F32v3 ledLightPos[] = {{2.6f, 1.8f, 0.0f}, {-3.2f, 1.2f, 0.0f}, {0.0f, 3.2f, 0.0f}, {0.0f, 1.8f, 3.6f}};
        static const gX_Color ledLightCol[] = {{255,190,140,255}, {120,220,255,255}, {255,255,255,255}, {255,255,255,255}};
        static const gX_F32 ledLightBri[] = {5.0f, 5.0f, 4.0f, 8.0f};
        for (int li = 0; li < 4; li++) {
            gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
            if (!l) continue;
            gX_RendererGFX_Light_Init(l);
            l->type = 1;
            l->pos = ledLightPos[li];
            l->color = ledLightCol[li];
            l->diffuseTint = ledLightCol[li];
            l->specularTint = ledLightCol[li];
            l->brightness = ledLightBri[li];
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.08f;
        }
        gfx->sun.brightness = 0.12f;
        gfx->sun.color = (gX_Color){150, 180, 255, 255};
        gfx->sun.diffuseTint = gfx->sun.color;
        gfx->sun.specularTint = gfx->sun.color;
        //opencode: true night - sun BELOW the horizon (TO-sun.y < -0.14
        //   drives nightBlend->0 and dayFactor->~0.07: dark sky + stars +
        //   dark ground in reflections). sun.dir is light-travel direction
        //   (sky gets -dir), so below-horizon sun = +Y dir = scene up-light
        //   at 0.12 (negligible) with inverted CSM (ditto); the sky moon
        //   disk is fixed above and decorative only. Point rig owns shading.
        gfx->sun.dir = (gX_F32v3){0.35f, 1.0f, 0.45f};
        gfx->sky.color = (gX_Color){6, 8, 20, 255};
        gfx->sky.sunIntensity = 0.15f;
        //opencode: starfield fixed 2026-09-11 (was DEAD: gnomonic cells went
        //   subpixel in the near-horizon sliver + twinkle pulsed in unison;
        //   now yaw/pitch cells + decorrelated twinkle - hundreds of stars).
        //   Neutral 1.0s; GX density/brightness hooks live in schema.
        gfx->sky.starDensity = 1.0f;
        gfx->sky.starBrightness = 1.0f;
        //opencode: light-marker billboards would pollute the glow read
        //   (white blobs at every orbiter); markers stay a key toggle.
        gfx->state.SHOW_LIGHT_MARKERS = 0;
        //opencode: gta3 grade (GX_GTA3=1): neon bleeds into murk. Fog was
        //   effectively off here (default 0.0005); 0.03 walls the 14x8u
        //   slab edges while the 4-light rig (5/5/4/8) feeds point
        //   inscatter automatically (fogVolume.enablePointLights default
        //   on). Emissive wave + bloom defaults carry the glow; this knob
        //   is only the air. Default ledrow unchanged when unset.
        {
            const gX_I8 *gta = getenv("GX_GTA3");
            if (gta && gta[0] == '1') {
                gfx->fogVolume.density = 0.05f;
                //opencode: halo probe verdict (threshold 0.85 vs 0.45 A/B):
                //   orb halos fatten into visible color pools with no frame
                //   wash (mean +3, frac>200 identical). 0.45 ships in-grade.
                gfx->bloom.threshold = 0.45f;
                gfx->fogVolume.scattering = 1.0f;
                gfx->fogVolume.heightFalloff = 1.2f;
            }
        }
        gX_TraceLog(gX_LOG_NOTICE, "LEDROW: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }

    //opencode: --forest dead-forest testbed. Matte ground (no mirror: 110
    //   trees through the planar pass would triple an already heavy frame)
    //   + gameX tree.glb instances (guest fallback via GX_ASSETS, NOT
    //   vendored) scattered by a local LCG (fixed seed, hermetic).
    //   SPACING IS LOAD-BEARING: canopies span +-4..8u (scale-dependent),
    //   so 7u Poisson made a solid thicket with tips at the lens (mush
    //   frames). 18u spacing in r10..62 gives an open stand (~36 trees:
    //   the 12MB GLB parses ~236ms per instance, so 100+ cost 25s of
    //   load); thick fog melts everything past ~40u. Overcast pale sun.
    //   Batch read first: per-model draws + shadow pass.
    if (arguments.showcaseForest) {
        gX_RendererGFX_Model *floorModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (floorModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(floorModel, path);
            floorModel->params.pos = (gX_F32v3){0.0f, -0.075f, 0.0f};
            floorModel->params.scale = (gX_F32v3){280.0f, 0.15f, 280.0f};
            floorModel->params.usePBR = 1;
            floorModel->params.metalness = 0.0f;
            floorModel->params.shininess = 8.0f;
            floorModel->params.roughness = 1.0f;
        }
        {
            gX_U32 seed = 0xF0E57u;
            gX_F32 px[40], pz[40], ps[40], pr[40];
            gX_I32 placed = 0, guard = 0;
            while (placed < 36 && guard++ < 8000) {
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r1 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r2 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r3 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r4 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                gX_F32 rad = 10.0f + 52.0f * sqrtf(r1);
                gX_F32 ang = r2 * 6.2831853f;
                gX_F32 x = cosf(ang) * rad;
                gX_F32 z = sinf(ang) * rad;
                //opencode: camera pocket clearing matches the showcase pin
                //   (yaw 20 pitch 6 dist 20 -> ~(6.8,18.7)): r12 keeps both
                //   trunks AND the +-8u canopy overhang out of the lens.
                {
                    gX_F32 dx = x - 6.8f;
                    gX_F32 dz = z - 18.7f;
                    if (dx * dx + dz * dz < 144.0f) continue;
                }
                {
                    gX_I1 ok = 1;
                    for (gX_I32 pi = 0; pi < placed; pi++) {
                        gX_F32 dx = x - px[pi];
                        gX_F32 dz = z - pz[pi];
                        //opencode: 18u Poisson (324u^2): canopy diameters run
                        //   9..16u, so anything tighter is solid thicket.
                        if (dx * dx + dz * dz < 324.0f) { ok = 0; break; }
                    }
                    if (!ok) continue;
                }
                px[placed] = x;
                pz[placed] = z;
                ps[placed] = 0.75f + 0.6f * r3;
                pr[placed] = r4 * 360.0f;
                placed++;
            }
            //opencode: 3 hand-placed foreground heroes (the LCG pocket keeps
            //   the lens clear, which left the foreground empty). Small
            //   scales so canopy tips stay 4u+ from the camera; LCG overlap
            //   is harmless (real stands interleave).
            //   Silent-hill: bigger + 2 extra mid-frame flankers (~16u,
            //   just inside the fog wall) so the mid-frame is never empty.
            if (placed + 3 <= 40) {
                gX_I1 shHero = 0;
                {
                    const gX_I8 *shEnv = getenv("GX_SILENTHILL");
                    if (shEnv && shEnv[0] == '1') shHero = 1;
                }
                if (shHero) {
                    px[placed] = -0.1f; pz[placed] = 11.6f; ps[placed] = 1.15f; pr[placed] = 30.0f; placed++;
                    px[placed] = 7.7f; pz[placed] = 6.7f; ps[placed] = 1.25f; pr[placed] = 140.0f; placed++;
                    px[placed] = 1.1f; pz[placed] = 5.9f; ps[placed] = 1.05f; pr[placed] = 250.0f; placed++;
                    if (placed + 2 <= 40) {
                        px[placed] = -2.5f; pz[placed] = 6.0f; ps[placed] = 1.35f; pr[placed] = 200.0f; placed++;
                        px[placed] = 6.0f; pz[placed] = 3.0f; ps[placed] = 1.45f; pr[placed] = 90.0f; placed++;
                    }
                } else {
                    px[placed] = -0.1f; pz[placed] = 11.6f; ps[placed] = 0.60f; pr[placed] = 30.0f; placed++;
                    px[placed] = 7.7f; pz[placed] = 6.7f; ps[placed] = 0.65f; pr[placed] = 140.0f; placed++;
                    px[placed] = 1.1f; pz[placed] = 5.9f; ps[placed] = 0.55f; pr[placed] = 250.0f; placed++;
                }
            }
            for (gX_I32 ti = 0; ti < placed; ti++) {
                gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!m) continue;
                gX_I8 path[256];
                //opencode: silent-hill fix — tree.glb is a guest asset
                //   (absent in-tree: loads failed, empty fog slab).
                //   Fall back to in-tree cone so forest/lake work hermetic.
                //   Cones read tiny at tree scale: stretch tall when the
                //   fallback is active (dead-trunk silhouette, not a rock).
                gX_I1 useFallback = 0;
                {
                    FILE *probe = fopen(GX_RESOURCE_DIR "/model/tree.glb", "rb");
                    if (probe) {
                        fclose(probe);
                        snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/tree.glb");
                    } else {
                        snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cone.glb");
                        useFallback = 1;
                    }
                }
                gX_RendererGFX_Model_Init(m, path);
                //opencode: base sunk 5cm under the slab top: piercing the
                //   ground reads natural at any distance (and hides the
                //   uncapped tube mouth below the surface).
                m->params.pos = (gX_F32v3){px[ti], 0.02f, pz[ti]};
                if (useFallback) {
                    m->params.scale = (gX_F32v3){ps[ti] * 1.6f, ps[ti] * 3.4f, ps[ti] * 1.6f};
                } else {
                    m->params.scale = (gX_F32v3){ps[ti], ps[ti], ps[ti]};
                }
                m->params.rotation = (gX_F32v3){0.0f, pr[ti], 0.0f};
                m->params.usePBR = 1;
                m->params.metalness = 0.0f;
                m->params.shininess = 24.0f;
                m->params.roughness = 0.9f;
            }
            gX_TraceLog(gX_LOG_NOTICE, "FOREST: placed=%d (want 36)", placed);
        }
        gfx->sun.brightness = 0.45f;
        gfx->sun.color = (gX_Color){170, 175, 190, 255};
        gfx->sun.diffuseTint = gfx->sun.color;
        gfx->sun.specularTint = gfx->sun.color;
        //opencode: exposure 0.65 (default 0.80): fog-white ground + pale
        //   bark clipped to 255 across the frame; the mood is overcast,
        //   not bleached.
        gfx->state.exposure = 0.65f;
        gfx->sky.turbidity = 8.0f;
        //opencode: gentle bloom (0.6): thin dark trunks on fog-white ground
        //   are extreme local contrast and the bloom upsample rings white
        //   quads around trunk bases at full intensity (proven by
        //   GX_T_BLOOM=0 A/B). Proper fix = upsample investigation (backlog).
        gfx->bloom.intensity = 0.6f;
        //opencode: density 0.05 (was 0.03): the scaled-down stand needs a
        //   thicker melt past ~40u; 0.03 left the far edge readable.
        //   GX_FOG_DENSITY tunes.
        gfx->fogVolume.density = 0.05f;
        //opencode: silent-hill grade (GX_SILENTHILL=1): full-daylight
        //   whiteout — the classic foggy town at noon, not night. Thick
        //   white fog wall, bright sun (fog inscatter shares it), sky
        //   disabled with the clear color tone-matched to the wall.
        //   Default forest unchanged when unset.
        {
            const gX_I8 *sh = getenv("GX_SILENTHILL");
            if (sh && sh[0] == '1') {
                gfx->sun.brightness = 0.9f;
                gfx->sun.color = (gX_Color){255, 252, 245, 255};
                gfx->sun.diffuseTint = gfx->sun.color;
                gfx->sun.specularTint = gfx->sun.color;
                gfx->state.exposure = 0.75f;
                gfx->sky.turbidity = 1.5f;
                //opencode: no sky — the horizon band is unreachable from
                //   the fog side (clear-depth early-out), so remove the sky
                //   and tone-match the clear color to the white fog wall.
                //   First daylight guess {195,198,205} (slightly cool to
                //   counter the composite warmth); iterated vs captures.
                gfx->state.ENABLE_PROCEDURAL_SKY = 0;
                gfx->state.ENABLE_SKYBOX = 0;
                gfx->sky.color = (gX_Color){195, 198, 205, 255};
                gfx->sky.sunIntensity = 0.15f;
                gfx->sky.horizon = 0.35f;
                gfx->sky.density = 1.6f;
                //opencode: whiteout fog — dense enough to wall off past
                //   ~25u, full scattering so the sun lights the murk white.
                //   ACES, not AgX: band-source forensics proved AgX skews
                //   neutral greys mauve ([163,122,124] computed on the clear
                //   color, xAE 0.8 = measured [130,98,100]); ACES is neutral.
                gfx->state.ENABLE_AGX_TONEMAP = 0;
                gfx->fogVolume.heightFalloff = 0.6f;
                gfx->fogVolume.density = 0.05f;
                gfx->fogVolume.scattering = 0.5f;
                //opencode: softer daylight edges (night crush was 0.75).
                gfx->bloom.vignette = 0.6f;
                gfx->bloom.intensity = 0.6f;
                //opencode: slab breakup — 10 half-sunk rubble mounds ring
                //   the camera pocket (6..30u) so the noon sun rakes across
                //   geometry instead of a clean slab. In-tree cube, matte
                //   PBR; LCG fixed seed keeps captures deterministic.
                {
                    gX_U32 rseed = 0x5A117u;
                    for (gX_I32 ri = 0; ri < 10; ri++) {
                        rseed = rseed * 1664525u + 1013904223u;
                        gX_F32 rr1 = (gX_F32)(rseed >> 8) * (1.0f / 16777216.0f);
                        rseed = rseed * 1664525u + 1013904223u;
                        gX_F32 rr2 = (gX_F32)(rseed >> 8) * (1.0f / 16777216.0f);
                        rseed = rseed * 1664525u + 1013904223u;
                        gX_F32 rr3 = (gX_F32)(rseed >> 8) * (1.0f / 16777216.0f);
                        gX_F32 rang = rr1 * 6.2831853f;
                        gX_F32 rrad = 6.0f + 24.0f * rr2;
                        gX_RendererGFX_Model *rb = gX_Heap_Add_Push_NoPtr(&gfx->models);
                        if (!rb) continue;
                        gX_I8 rpath[256];
                        snprintf(rpath, sizeof(rpath), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                        gX_RendererGFX_Model_Init(rb, rpath);
                        rb->params.pos = (gX_F32v3){6.8f + cosf(rang) * rrad, -0.05f, 18.7f + sinf(rang) * rrad * 0.6f};
                        rb->params.scale = (gX_F32v3){1.5f + rr3 * 2.0f, 0.25f + rr1 * 0.45f, 1.5f + rr2 * 2.0f};
                        rb->params.rotation = (gX_F32v3){0.0f, rr1 * 360.0f, 0.0f};
                        rb->params.usePBR = 1;
                        rb->params.metalness = 0.0f;
                        rb->params.shininess = 8.0f;
                        rb->params.roughness = 1.0f;
                    }
                }
            }
        }
        gX_TraceLog(gX_LOG_NOTICE, "FOREST: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }

    if (arguments.showcaseLake) {
        //opencode: --lake - forest-ringed lake. The water plane is fixed
        //   at y=0 (40x40): lakebed slab below it, four bank boxes above
        //   it outside +-20, dead trees ringing the banks, camera on the
        //   south bank looking north across the water (fog + reflections
        //   carry the shot). Grade reuses the forest overcast.
        {
            gX_RendererGFX_Model *bed = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (bed) {
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(bed, path);
                bed->params.pos = (gX_F32v3){0.0f, -0.525f, 0.0f};
                bed->params.scale = (gX_F32v3){130.0f, 0.15f, 130.0f};
                bed->params.usePBR = 1;
                bed->params.metalness = 0.0f;
                bed->params.shininess = 8.0f;
                bed->params.roughness = 1.0f;
            }
        }
        {
            //opencode: bank boxes - tops at +0.15 (low shoreline walls read
            //   as land, not ledges), bottoms sunk past the lakebed top
            //   (-0.45) so no seam shows. N/S span full width; E/W fill
            //   between them (corners covered by N/S overhang). Inner
            //   edges at +-19: the water plane (+-20) tucks 1u UNDER the
            //   banks, hiding the alpha-faded mesh edge + the shore gap
            //   strip (that grazing-incidence mosaic salted white).
            const gX_F32 bx[4] = {0.0f, 0.0f, 42.0f, -42.0f};
            const gX_F32 bz[4] = {-42.0f, 42.0f, 0.0f, 0.0f};
            const gX_F32 sx[4] = {130.0f, 130.0f, 46.0f, 46.0f};
            const gX_F32 sz[4] = {46.0f, 46.0f, 42.0f, 42.0f};
            for (gX_I32 bi = 0; bi < 4; bi++) {
                gX_RendererGFX_Model *bank = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!bank) continue;
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(bank, path);
                bank->params.pos = (gX_F32v3){bx[bi], -0.35f, bz[bi]};
                bank->params.scale = (gX_F32v3){sx[bi], 1.0f, sz[bi]};
                bank->params.usePBR = 1;
                bank->params.metalness = 0.0f;
                bank->params.shininess = 8.0f;
                bank->params.roughness = 1.0f;
            }
        }
        {
            gX_U32 seed = 0x1A4E5u;
            gX_F32 px[32], pz[32], ps[32], pr[32];
            gX_I32 placed = 0, guard = 0;
            while (placed < 24 && guard++ < 8000) {
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r1 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r2 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r3 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                seed = seed * 1664525u + 1013904223u;
                gX_F32 r4 = (gX_F32)(seed >> 8) * (1.0f / 16777216.0f);
                gX_F32 rad = 24.0f + 34.0f * sqrtf(r1);
                gX_F32 ang = r2 * 6.2831853f;
                gX_F32 x = cosf(ang) * rad;
                gX_F32 z = sinf(ang) * rad;
                //opencode: banks only - the water square is +-20, so trees
                //   need |x|>21.5 or |z|>21.5 (1.5u margin off the edge).
                if (fabsf(x) < 21.5f && fabsf(z) < 21.5f) continue;
                {
                    gX_I1 ok = 1;
                    for (gX_I32 pi = 0; pi < placed; pi++) {
                        gX_F32 dx = x - px[pi];
                        gX_F32 dz = z - pz[pi];
                        //opencode: 16u Poisson (256u^2): tighter lakeside
                        //   stand than the forest's 18u.
                        if (dx * dx + dz * dz < 256.0f) { ok = 0; break; }
                    }
                    if (!ok) continue;
                }
                px[placed] = x;
                pz[placed] = z;
                ps[placed] = 0.55f + 0.55f * r3;
                pr[placed] = r4 * 360.0f;
                placed++;
            }
            //opencode: 2 near-shore heroes flanking the mid-view water
            //   (camera sits over the water at z~14 looking north: these
            //   stand on the east/west banks inside the frame). Small
            //   scales keep canopies out of the lens.
            if (placed + 2 <= 32) {
                px[placed] = -22.0f; pz[placed] = 8.0f; ps[placed] = 0.50f; pr[placed] = 30.0f; placed++;
                px[placed] = 22.5f; pz[placed] = 7.0f; ps[placed] = 0.55f; pr[placed] = 140.0f; placed++;
            }
            for (gX_I32 ti = 0; ti < placed; ti++) {
                gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!m) continue;
                gX_I8 path[256];
                //opencode: silent-hill fix — tree.glb is a guest asset
                //   (absent in-tree: loads failed, empty fog slab).
                //   Fall back to in-tree cone so forest/lake work hermetic.
                {
                    FILE *probe = fopen(GX_RESOURCE_DIR "/model/tree.glb", "rb");
                    if (probe) {
                        fclose(probe);
                        snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/tree.glb");
                    } else {
                        snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cone.glb");
                    }
                }
                gX_RendererGFX_Model_Init(m, path);
                //opencode: bases sunk 5cm into the bank tops (+0.15), same
                //   piercing recipe as the forest slab (hides the open
                //   tube mouth below the surface).
                m->params.pos = (gX_F32v3){px[ti], 0.17f, pz[ti]};
                m->params.scale = (gX_F32v3){ps[ti], ps[ti], ps[ti]};
                m->params.rotation = (gX_F32v3){0.0f, pr[ti], 0.0f};
                m->params.usePBR = 1;
                m->params.metalness = 0.0f;
                m->params.shininess = 24.0f;
                m->params.roughness = 0.9f;
            }
            gX_TraceLog(gX_LOG_NOTICE, "LAKE: placed=%d (want 24)", placed);
        }
        gfx->state.ENABLE_WATER = 1;
        //opencode: still lake - wave amps x0.15 (uploads read the struct
        //   per frame) + ripple rain off via calmWater: a lake mirrors,
        //   and chop facets sparkle white on ripple flanks (proven by
        //   sun=0 A/B: identical speckle energy, single-pixel impulses).
        gfx->water.calmWater = 1;
        for (gX_I32 wi = 0; wi < GX_WATER_MAX_WAVES; wi++)
            gfx->water.waveAmplitudes[wi] *= 0.15f;
        gfx->sun.brightness = 0.45f;
        gfx->sun.color = (gX_Color){170, 175, 190, 255};
        gfx->sun.diffuseTint = gfx->sun.color;
        gfx->sun.specularTint = gfx->sun.color;
        gfx->state.exposure = 0.65f;
        gfx->sky.turbidity = 8.0f;
        gfx->bloom.intensity = 0.6f;
        //opencode: density 0.03: the seam zone sat at ~169 LDR base with
        //   four legitimate adds (ssr+ssgi+bloom+particles) tipping peaks
        //   over the clip edge (proven: SUNFOG max=169 vs FULL 255, no
        //   single adder load-bearing). Thinner equilibrium + softer
        //   extinction pull peaks off the edge. GX_FOG_DENSITY tunes.
        gfx->fogVolume.density = 0.03f;
        gX_TraceLog(gX_LOG_NOTICE, "LAKE: models=%u lights=%u",
            gfx->models.amount, gfx->lights.amount);
        return 0;
    }

    if (arguments.showcaseMinimal) {
        const gX_I1 planarOracle = arguments.showcasePlanar;

        // near-mirror plane — slab top surface at y=+0.075 (pos.y=-0.075,
        // mesh spans [-1,1] scaled by 0.15 in Y -> half-extent 0.15)
        gX_RendererGFX_Model *floorModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (floorModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(floorModel, path);
            floorModel->params.pos = (gX_F32v3){0.0f, -0.075f, 0.0f};
            floorModel->params.scale = (gX_F32v3){8.0f, 0.15f, 8.0f};
            floorModel->params.usePBR = 1;
            floorModel->params.metalness = 1.0f;
            floorModel->params.shininess = 256.0f;
            floorModel->params.roughness = 0.02f; //opencode: near-mirror gloss
            //opencode: receiver-routing fixture; default scene behavior stays on.
            {
                const gX_I8 *receiverHook = getenv("GX_T_SSR_FLOOR_RECEIVER");
                if (receiverHook)
                    floorModel->params.receiveSSR = (gX_I1)atoi((const char *)receiverHook);
            }
            if (planarOracle) {
                floorModel->params.planarReflection = 1;
                //opencode: mirror across the slab TOP (+0.075), not world zero —
                //   a wrong plane height displaces every reflected pixel
                floorModel->params.reflectionPlaneHeight = 0.075f;
            }
            //opencode: cubemap gate fixture — the large slab makes the direct
            //   consumer A/B measurable without changing production materials.
            if (getenv("GX_CUBEMAP_TEST"))
                floorModel->params.enableReflections = 1;
        }

        // the single floating cube — 1u above the plane, slight tilt so its
        // reflection silhouette is unambiguous from any orbit angle
        gX_RendererGFX_Model *cubeModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (cubeModel) {
            gX_I8 path[256];
            snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
            gX_RendererGFX_Model_Init(cubeModel, path);
            cubeModel->params.pos = (gX_F32v3){0.35f, 1.0f, -0.25f};
            cubeModel->params.scale = (gX_F32v3){0.5f, 0.5f, 0.5f};
            //opencode: v0.1.35 — GX_CUBE_ROT=0 diagnostic: convention-free silhouette
            {
                const gX_I8 *cr = getenv("GX_CUBE_ROT");
                if (cr && cr[0]=='0')
                    cubeModel->params.rotation = (gX_F32v3){0,0,0};
                else if (cr && cr[0]=='Y')
                    cubeModel->params.rotation = (gX_F32v3){0,30,0};
                else if (cr && cr[0]=='Z')
                    cubeModel->params.rotation = (gX_F32v3){0,0,30};
                else
                    cubeModel->params.rotation = (gX_F32v3){10.0f, 30.0f, 5.0f};
            }
            cubeModel->params.usePBR = 1;
            //opencode: v0.1.27 — metalness 0.85 -> 0.6: pure metals have no
            //   diffuse response; under dim night lighting the cube read as
            //   black. 0.6 keeps strong reflections but lets the coloured
            //   point lights actually shade the faces.
            cubeModel->params.metalness = 0.6f;
            cubeModel->params.shininess = 128.0f;
            cubeModel->params.roughness = 0.15f;
            //opencode: v0.1.24 — harness hook: GX_DIAG_CUBE renders the cube
            //   flat magenta so scripts can locate it and its mirror image
            //   unambiguously in captured frames (calibration aid)
            if (getenv("GX_DIAG_CUBE")) {
                cubeModel->params.emissiveColor = (gX_Color){255, 0, 255, 255};
                //opencode: 8.0 clipped through bloom/tonemap (cube + mirror
                //   both blew to white, defeating the colorimetric gate).
                //   1.0 + near-black albedo: faces read pure emissive
                //   magenta (diffuse wash desaturates toward pink and fails
                //   the g-ratio test); stays under the bloom threshold too.
                cubeModel->params.emissiveFactor = 1.0f;
                cubeModel->params.albedo = 0.02f;
                //opencode: flat diag shading — metal/specular faces catch
                //   white highlights that dilute the magenta zone score;
                //   the gate measures colorimetry, not material response.
                cubeModel->params.metalness = 0.0f;
                cubeModel->params.shininess = 8.0f;
                cubeModel->params.roughness = 0.9f;
                const char *dc = getenv("GX_DIAG_COLOR");
                if (dc && dc[0]=='G') cubeModel->params.emissiveColor = (gX_Color){0, 255, 0, 255};
            }
            //opencode: M8 - emission-mapped bloom probe. Params here (persist);
            //   the map assignment is lazy in gX_Showcase_Update (model
            //   meshes/materials load deferred on first update). orb.png:
            //   bright core on black, max 1.0 - with factor headroom over
            //   the 0.85 extract threshold this is a deterministic,
            //   sun-independent bloom source for the harness (the sky-driven
            //   bloom row flakes on cloud drift). Default off.
            if (getenv("GX_T_EMISSIVEMAP")) {
                cubeModel->params.hasEmissiveMap = 1;
                cubeModel->params.emissiveMapFactor = 1.0f;
                cubeModel->params.emissiveColor = (gX_Color){255, 0, 255, 255};
                //opencode: factor 6 (not 3): magenta luminance is only
                //   0.28/1.0, so 3.0 lands exactly on the 0.85 extract
                //   threshold knee and extracts ~nothing. 6.0 -> lum ~1.7.
                cubeModel->params.emissiveFactor = 6.0f;
                cubeModel->params.albedo = 0.02f;
            }
            //opencode: cubemap gate fixture — make the minimal floating cube
            //   an explicit direct cubemap consumer without changing the
            //   production showcase material setup.
            if (getenv("GX_CUBEMAP_TEST")) {
                cubeModel->params.enableReflections = 1;
                cubeModel->params.metalness = 1.0f;
                cubeModel->params.roughness = 0.08f;
            }
        }

        //opencode: deterministic DOF focus-rack fixture — a foreground thin
        //   silhouette, transparent panel, and distant emissive spheres.
        const gX_I8 *dofFixture = getenv("GX_DOF_FIXTURE");
        if (dofFixture && dofFixture[0] == '1') {
            gX_RendererGFX_Model *thinModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (thinModel) {
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(thinModel, path);
                thinModel->params.pos = (gX_F32v3){-1.35f, 0.95f, 1.2f};
                thinModel->params.scale = (gX_F32v3){0.045f, 1.15f, 0.045f};
                thinModel->params.rotation = (gX_F32v3){0.0f, 0.0f, 8.0f};
                thinModel->params.usePBR = 1;
                thinModel->params.albedoTint = (gX_Color){255, 75, 35, 255};
                thinModel->params.metalness = 0.05f;
                thinModel->params.roughness = 0.35f;
            }

            gX_RendererGFX_Model *glassModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (glassModel) {
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(glassModel, path);
                glassModel->params.pos = (gX_F32v3){0.0f, 0.95f, 0.75f};
                glassModel->params.scale = (gX_F32v3){1.15f, 0.8f, 0.035f};
                glassModel->params.usePBR = 1;
                glassModel->params.albedoTint = (gX_Color){55, 175, 255, 255};
                glassModel->params.metalness = 0.1f;
                glassModel->params.roughness = 0.2f;
                glassModel->params.enableBlending = 1;
                glassModel->params.blendOpacity = 0.35f;
                glassModel->params.enableRefraction = 1;
                glassModel->params.refractiveIndex = 1.5f;
                glassModel->params.enableReflections = 1;
            }

            const gX_F32v3 bokehPositions[] = {
                {1.45f, 1.15f, -2.8f}, {-1.65f, 1.0f, -3.35f},
            };
            const gX_Color bokehColors[] = {
                {255, 170, 55, 255}, {80, 190, 255, 255},
            };
            for (gX_U32 i = 0; i < 2; i++) {
                gX_RendererGFX_Model *bokehModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!bokehModel) continue;
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
                gX_RendererGFX_Model_Init(bokehModel, path);
                bokehModel->params.pos = bokehPositions[i];
                bokehModel->params.scale = (gX_F32v3){0.18f, 0.18f, 0.18f};
                bokehModel->params.usePBR = 1;
                bokehModel->params.albedo = 0.05f;
                bokehModel->params.metalness = 0.0f;
                bokehModel->params.roughness = 0.25f;
                bokehModel->params.emissiveColor = bokehColors[i];
                bokehModel->params.emissiveFactor = 4.0f;
            }
        }

        //opencode: material override sidecar fixture — a probe cube with a
        //   sidecar (albedo tint, roughness, metalness) beside an identical
        //   control cube with no sidecar. Both get the same scene params
        //   (blue tint, 0.6/0.15) so the pair isolates the override response
        //   and proves the sidecar is per-file (control stays bit-identical).
        const gX_I8 *matProbe = getenv("GX_MATPROBE");
        if (matProbe && matProbe[0] == '1') {
            const gX_I8 *probePaths[2] = {
                GX_RESOURCE_DIR "/model/probe/matprobe.glb",
                GX_RESOURCE_DIR "/model/primitive/cube.glb",
            };
            for (gX_U32 pi = 0; pi < 2; pi++) {
                gX_RendererGFX_Model *probeModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!probeModel) continue;
                gX_RendererGFX_Model_Init(probeModel, probePaths[pi]);
                probeModel->params.pos = (gX_F32v3){1.35f + (gX_F32)pi * 0.9f, 0.475f, 0.6f};
                probeModel->params.scale = (gX_F32v3){0.4f, 0.4f, 0.4f};
                probeModel->params.usePBR = 1;
                probeModel->params.metalness = 0.6f;
                probeModel->params.roughness = 0.15f;
                probeModel->params.albedoTint = (gX_Color){90, 140, 255, 255};
            }
        }

        //opencode: material map override fixture — a cube whose sidecar
        //   replaces its albedo texture (SMPTE testcard) and adds an emissive
        //   orb map, beside an identical sidecar-less control. Proves the
        //   per-mesh texture rebind path (maps resolve at load, bind only on
        //   change).
        const gX_I8 *mapProbe = getenv("GX_MAPPROBE");
        if (mapProbe && mapProbe[0] == '1') {
            const gX_I8 *mapPaths[2] = {
                GX_RESOURCE_DIR "/model/probe/mapprobe.glb",
                GX_RESOURCE_DIR "/model/primitive/cube.glb",
            };
            for (gX_U32 pi = 0; pi < 2; pi++) {
                gX_RendererGFX_Model *mapModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!mapModel) continue;
                gX_RendererGFX_Model_Init(mapModel, mapPaths[pi]);
                mapModel->params.pos = (gX_F32v3){1.35f + (gX_F32)pi * 0.9f, 0.475f, -0.9f};
                mapModel->params.scale = (gX_F32v3){0.4f, 0.4f, 0.4f};
                mapModel->params.usePBR = 1;
                mapModel->params.metalness = 0.1f;
                mapModel->params.roughness = 0.6f;
            }
        }

        //opencode: two-mirror regress fixture — facing vertical slabs plus
        //   an emissive seed bar between them. Each slab owns one plane and
        //   samples the other's history bounce (see mirror.c budget/rules).
        const gX_I8 *mirrorPair = getenv("GX_MIRROR_PAIR");
        if (mirrorPair && mirrorPair[0] == '1') {
            struct { gX_F32v3 pos; gX_F32v3 normal; gX_F32 dist; gX_I32 tag; } pairSlabs[2] = {
                {{-2.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, -2.0f, 1},
                {{2.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, -2.0f, 2},
            };
            for (gX_U32 si = 0; si < 2; si++) {
                gX_RendererGFX_Model *slabModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!slabModel) continue;
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(slabModel, path);
                slabModel->params.pos = pairSlabs[si].pos;
                slabModel->params.scale = (gX_F32v3){0.06f, 1.6f, 2.4f};
                slabModel->params.usePBR = 1;
                slabModel->params.metalness = 1.0f;
                slabModel->params.shininess = 256.0f;
                slabModel->params.roughness = 0.02f;
                slabModel->params.planarReflection = 1;
                slabModel->params.mirrorPair = pairSlabs[si].tag;
                slabModel->params.reflectionPlaneHeight = pairSlabs[si].dist;
                slabModel->params.reflectionNormal = pairSlabs[si].normal;
            }
            gX_RendererGFX_Model *seedModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (seedModel) {
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(seedModel, path);
                seedModel->params.pos = (gX_F32v3){0.0f, 1.0f, 0.0f};
                seedModel->params.scale = (gX_F32v3){0.12f, 0.5f, 0.12f};
                seedModel->params.usePBR = 1;
                seedModel->params.albedo = 0.05f;
                seedModel->params.metalness = 0.0f;
                seedModel->params.roughness = 0.5f;
                seedModel->params.emissiveColor = (gX_Color){255, 170, 55, 255};
                seedModel->params.emissiveFactor = 2.5f;
            }
            gX_TraceLog(gX_LOG_NOTICE, "MIRROR-PAIR: slabs=2 seed=1");
        }

        // minimal lighting — one warm + one cool point light flanking the
        // cube (distinct hues make the mirrored image visually attributable).
        //opencode: v0.1.27 — brightness 1.5 -> 4.0 + tighter falloff: at
        //   metalness 0.85 under a night sky the cube read near-black with
        //   only a specular rim (user report), making the verification scene
        //   hard to eyeball. The lights must actually MODEL the faces.
        //opencode: diag-mode exposure — 4.0 clips the emissive cube faces
        //   (and the floor around them) to white, defeating the magenta
        //   colorimetric gate. 2.0 keeps faces modeled without clipping.
        static const gX_F32v3 minLightPos[] = {{2.2f, 2.6f, 1.8f}, {-2.2f, 2.2f, -1.4f}};
        static const gX_Color minLightCol[] = {{255,120,90,255}, {100,180,255,255}};
        for (gX_U32 i = 0; i < 2; i++) {
            gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
            if (!l) continue;
            gX_RendererGFX_Light_Init(l);
            l->type = 1;
            l->pos = minLightPos[i];
            l->color = minLightCol[i];
            l->diffuseTint = minLightCol[i];
            l->specularTint = minLightCol[i];
            l->brightness = 2.0f;
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.08f;
        }
        //opencode: non-planar analytic fixture - chrome sphere sampling the
        //   floor's planar RT through the global mirror-plane intersect
        //   (GTA4/5 style). Env-gated, planar scenes only (needs the RT).
        if (getenv("GX_NONPLANAR_TEST") && planarOracle) {
            gX_RendererGFX_Model *npModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
            if (npModel) {
                gX_I8 npath[256];
                snprintf(npath, sizeof(npath), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
                gX_RendererGFX_Model_Init(npModel, npath);
                npModel->params.pos = (gX_F32v3){-1.1f, 0.45f, 0.7f};
                npModel->params.scale = (gX_F32v3){0.35f, 0.35f, 0.35f};
                npModel->params.usePBR = 1;
                npModel->params.metalness = 1.0f;
                npModel->params.shininess = 256.0f;
                npModel->params.roughness = 0.02f;
                npModel->params.planarReflection = 1;
                npModel->params.nonPlanarReflection = 1;
                //opencode: match the fixture's floor slab top and oracle plane.
                npModel->params.reflectionPlaneHeight = 0.075f;
                npModel->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
            }
        }
        //opencode: probe-blend sweep fixture — chrome sphere gliding across
        //   the probe handoff at z=0 (probes sit at x/z ±3). GX_PROBE_SWEEP=z
        //   sets its position; capture a sweep with GX_PROBE_BLEND=0 vs 1 to
        //   score the handoff step. Minimal-mode only (needs SSR-off for a
        //   pure probe-direct measurement).
        {
            const gX_I8 *sw = getenv("GX_PROBE_SWEEP");
            if (sw && !planarOracle) {
                gX_RendererGFX_Model *swModel = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (swModel) {
                    gX_I8 spath[256];
                    snprintf(spath, sizeof(spath), GX_RESOURCE_DIR "/model/primitive/sphere.glb");
                    gX_RendererGFX_Model_Init(swModel, spath);
                    gX_F32 z = (gX_F32)atof((const char *)sw);
                    swModel->params.pos = (gX_F32v3){2.0f, 0.5f, z};
                    swModel->params.scale = (gX_F32v3){0.3f, 0.3f, 0.3f};
                    swModel->params.usePBR = 1;
                    swModel->params.metalness = 1.0f;
                    swModel->params.shininess = 256.0f;
                    swModel->params.roughness = 0.02f;
                    swModel->params.enableReflections = 1;
                }
            }
        }
        gX_TraceLog(gX_LOG_NOTICE, "MINIMAL-SCENE: models=%u lights=%u planar=%d",
            gfx->models.amount, gfx->lights.amount, (gX_I32)planarOracle);
        return 0;
    }

    typedef struct gX_DemoModelSpec {
        const gX_I8 *file;
        gX_F32v3 pos;
        gX_F32v3 scale;
        gX_F32v3 rotation;
        gX_I1   usePBR;
        gX_F32  metalness;
        gX_F32  shininess;
        gX_F32  roughness;      //opencode: per-model SSR roughness (0=mirror, 1=matte)
        gX_I1   enableRefl;     //opencode: audit — env-cubemap sampling (the
                                //   reflections toggle gates this per model)
    } gX_DemoModelSpec;

    static const gX_DemoModelSpec specs[] = {
        //opencode: layout pass - every Y rests its AABB base on the slab top
        //   (+0.075) and XZ footprints are separated with >=0.12 clearance
        //   (rotated footprints measured from the engine AABB dump; the
        //   teapot used to sink 0.675 into the slab and poke through).
        {"teapot.glb",        {-2.5f, 0.675f, 1.5f}, {0.30f, 0.30f, 0.30f}, {0, 0, 0}, 1, 0.9f, 96.0f, 0.6f, 0},
        {"primitive/cube.glb",{ 2.5f, 1.075f, -1.5f}, {1.0f, 1.0f, 1.0f}, {0, 30, 0}, 0, 0.1f, 32.0f, 0.7f, 0},
        {"primitive/cone.glb",{ -3.0f, 1.075f, -3.0f}, {1.0f, 1.0f, 1.0f}, {0,-30, 0}, 0, 0.1f, 16.0f, 0.7f, 0},
        {"primitive/torus.glb",{ 4.5f, 0.3f, 2.5f}, {0.9f, 0.9f, 0.9f}, {0, 0, 0}, 1, 0.2f, 64.0f, 0.4f, 0},
        {"primitive/icosahedron.glb", {-4.5f, 1.075f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0}, 1, 0.6f, 48.0f, 0.5f, 0},
        {"primitive/cylinder.glb", {1.5f, 0.975f, 4.7f}, {0.45f, 0.9f, 0.45f}, {0, 0, 0}, 0, 0.0f, 24.0f, 0.9f, 0},
        {"button.glb",        { 0.5f, 0.095f, 2.8f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0}, 0, 0.0f, 24.0f, 0.9f, 0},
        //opencode: glossy floor platform — the demo had no reflective surface
        //   (the green ground is the sky shader's below-horizon colour, not a
        //   mesh), so SSR/reflections had nothing to demonstrate on.
        {"primitive/cube.glb",{ 0.0f, -0.075f, 0.0f}, {8.0f, 0.15f, 8.0f}, {0, 0, 0}, 0, 0.1f, 128.0f, 0.08f, 1},
        //opencode: v0.1.12 — mirror ball + polished cube. The demo had one
        //   reflective surface (the floor); the 6-face env cubemap path was
        //   only verifiable via debug views. The ball reflects the cluster
        //   AND the env cubemap (behind-camera content); the cube adds a
        //   second bounce target with a rougher surface.
        {"primitive/sphere.glb", {-5.5f, 0.875f, 2.5f}, {0.8f, 0.8f, 0.8f}, {0, 0, 0}, 1, 1.0f, 256.0f, 0.02f, 0},
        {"primitive/cube.glb",  { 0.2f, 0.575f, -3.5f}, {0.5f, 0.5f, 0.5f}, {0, 20, 0}, 1, 1.0f, 200.0f, 0.12f, 0},
    };

    //opencode: --showcase-water — float the demo cluster above the
    //   water plane (y=0): raise all content by 2 units so the water surface
    //   sits below the platform top (clean y-separation for the SSR water mask).
    //opencode: v0.1.17 — the 8×8 platform slab does NOT lift with the cluster:
    //   lifted, it becomes a wall of glossy white filling the background from
    //   the orbit camera (flat gray field where the sky should be — worse now
    //   that the cubemap feeds it real sky reflections). The WATER is the
    //   reflective floor in this layout.
    extern gX_MainArguments arguments;
    const gX_F32 waterLift = arguments.showcaseWater ? 2.0f : 0.0f;
    const gX_I1 isWaterLayout = arguments.showcaseWater;

    for (gX_U32 i = 0; i < sizeof(specs)/sizeof(specs[0]); i++) {
        const gX_DemoModelSpec *s = &specs[i];
        if (isWaterLayout && s->scale.x == 8.0f && s->pos.y == -0.075f) continue; //opencode: skip the platform slab
        gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
        if (!m) continue;
        gX_I8 path[256];
        snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/%s", s->file);
        gX_RendererGFX_Model_Init(m, path);
        m->params.pos = s->pos;
        m->params.pos.y += waterLift;
        m->params.scale = s->scale;
        m->params.rotation = s->rotation;
        m->params.usePBR = s->usePBR;
        m->params.metalness = s->metalness;
        m->params.shininess = s->shininess;
        m->params.roughness = s->roughness;
        //opencode: audit — env-cubemap sampling per model. The slab mirrors
        //   the scene without SSR now, so the reflections toggle alone does
        //   something visible (was filling an RT nobody read).
        m->params.enableReflections = s->enableRefl;
        //opencode: v0.3.7 - the platform slab owns the mirror view so the
        //   v0.3.6 seam/wrong-side replacement triggers engage on the demo
        //   cluster (torus foot / disc rim residue, audit #2)
        if (!isWaterLayout && s->scale.x == 8.0f && s->pos.y == -0.075f) {
            m->params.reflectionSource = 1;
            m->params.reflectionPlaneHeight = 0.075f;
            m->params.reflectionNormal = (gX_F32v3){0.0f, 1.0f, 0.0f};
        }
    }
    if (!isWaterLayout) gX_RendererGFX_Scene_PickerOverlap(gfx);
    //ai: the editor opens the composed showcase unless an explicit preset or
    //   the audit-friendly GX_EDITOR_DEFAULT_SCENE=0 opt-out is present.
    const char *editorPreset = getenv("GX_GFX_PRESET_LOAD");
    const char *editorDefault = getenv("GX_EDITOR_DEFAULT_SCENE");
    if (!isWaterLayout && arguments.hasGfxEditor
        && (!editorDefault || strcmp(editorDefault, "0") != 0)
        && (!editorPreset || !editorPreset[0])
        && gX_RendererGFX_Preset_Load_New(gfx,
            "resources/tools/gfx_showcase.json") == 0)
        return 0;

    //opencode: instancing matrix fixture — the OFF arm draws one ordinary
    //   cube and the ON arm draws the same cube as a deterministic 4x4 grid.
    //   Keeping the model in both arms proves the hardware-instanced branch
    //   rather than merely measuring an extra model load.
    {
        const gX_I8 *inst = getenv("GX_T_INSTANCING");
        if (inst) {
            gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
            gX_U32 count = atoi(inst) != 0 ? 16u : 0u;
            if (m) {
                gX_I8 path[256];
                snprintf(path, sizeof(path), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                gX_RendererGFX_Model_Init(m, path);
                m->params.pos = (gX_F32v3){0.0f, 0.575f + waterLift, -5.0f};
                m->params.scale = (gX_F32v3){0.30f, 0.30f, 0.30f};
                m->params.usePBR = 1;
                m->params.metalness = 0.15f;
                m->params.roughness = 0.45f;
                m->params.instanceCount = count;
            }
            gX_TraceLog_GFX(gX_LOG_INFO, "INSTANCING: enabled=%d count=%u",
                count > 0 ? 1 : 0, count);
        }
    }

    //opencode: v0.3.8 - guest model ring (--guest-model subpath[:count] /
    //   GX_GUEST_MODEL): additive foreign-content injection for gfx perf
    //   testing. Instances resolve through the guest-root fallback chain
    //   (GX_ASSETS dirs); a ring keeps the demo cluster as the visual
    //   baseline so captures A/B cleanly. Verification scenes return
    //   before this point and stay pristine.
    if (arguments.guestModel[0]) {
        gX_I8 gpath[256];
        gX_I32 n = snprintf(gpath, sizeof(gpath), GX_RESOURCE_DIR "/%s",
            arguments.guestModel);
        gX_I32 count = arguments.guestCount > 0 ? arguments.guestCount : 1;
        if (n > 0 && (gX_U32)n < sizeof(gpath)) {
            //opencode: guest models load at native scale; normalize via the
            //   vertex AABB so foreign content (teapots, terrain dumps) reads
            //   at demo scale instead of dwarfing or vanishing in the scene
            gX_Model probe = gX_Model_Load(gpath);
            gX_F32 maxExtent = 0.0f;
            gX_U64 guestVerts = 0;
            for (gX_I32 mi = 0; mi < probe.meshCount; mi++) {
                const gX_Mesh *mesh = &probe.meshes[mi];
                guestVerts += mesh->vertexCount;
                for (gX_I32 vi = 0; vi < mesh->vertexCount; vi++) {
                    gX_F32 x = mesh->vertices[vi * 3 + 0];
                    gX_F32 y = mesh->vertices[vi * 3 + 1];
                    gX_F32 z = mesh->vertices[vi * 3 + 2];
                    gX_F32 ax = x < 0 ? -x : x;
                    gX_F32 ay = y < 0 ? -y : y;
                    gX_F32 az = z < 0 ? -z : z;
                    if (ax > maxExtent) maxExtent = ax;
                    if (ay > maxExtent) maxExtent = ay;
                    if (az > maxExtent) maxExtent = az;
                }
            }
            gX_F32 gscale = maxExtent > 0.0001f ? 1.2f / maxExtent : 1.0f;
            gX_Model_Unload(probe);
            const gX_F32 ringR = 3.8f;
            for (gX_I32 gi = 0; gi < count; gi++) {
                gX_F32 ang = (gX_F32)gi / (gX_F32)count * 2.0f * 3.14159265f
                    + 0.35f;
                gX_RendererGFX_Model *m = gX_Heap_Add_Push_NoPtr(&gfx->models);
                if (!m) break;
                gX_RendererGFX_Model_Init(m, gpath);
                m->params.pos = (gX_F32v3){ cosf(ang) * ringR, 0.6f,
                                            sinf(ang) * ringR };
                m->params.scale = (gX_F32v3){gscale, gscale, gscale};
                m->params.usePBR = 1;
                m->params.metalness = 0.35f;
                m->params.roughness = 0.4f;
            }
            gX_TraceLog_GFX(gX_LOG_INFO,
                "GUEST: spawned %d x '%s' (verts %llu, scale %.4f)",
                count, gpath, (unsigned long long)guestVerts, gscale);
        }
    }

    //opencode: decal harness — GX_T_DECALS=1 enables the pass and spawns
    //   three deterministic floor quads. The renderer existed with zero
    //   producers (Decal_Add had no callers), so the matrix row passed
    //   vacuously. Persistent (lifetime -1); rand() rotation is
    //   process-deterministic (default seed). y=0.09 floats above the
    //   slab top (0.075) to avoid z-fighting.
    if (getenv("GX_T_DECALS")) gfx->state.ENABLE_DECALS = (gX_I1)atoi(getenv("GX_T_DECALS"));
    if (gfx->state.ENABLE_DECALS) {
        gX_F32v3 flatN = {0.0f, 1.0f, 0.0f};
        //opencode: fixed spins (Decal_Add randomizes via rand(), whose
        //   global state is polluted by timing-dependent consumers, so
        //   harness quads would rotate run-to-run and break determinism).
        gX_F32v3 hpos[3] = {{0.5f, 0.09f, 0.5f}, {-1.5f, 0.09f, 1.0f}, {-3.0f, 0.09f, -1.0f}};
        gX_F32 hsize[3] = {0.8f, 0.5f, 0.6f};
        gX_F32 hspin[3] = {0.3f, 1.1f, 2.0f};
        gX_F32 halpha[3] = {1.0f, 0.5f, 1.0f};
        for (gX_I32 hi = 0; hi < 3; hi++) {
            gX_I32 di = gX_RendererGFX_Decal_Add(gfx, hpos[hi], flatN, hsize[hi], -1, -1.0);
            if (di >= 0) {
                gX_RendererGFX_Decal *dd = gX_Heap_SeekPtr_Index(&gfx->decals, (gX_U32)di);
                if (dd) { dd->rotation = hspin[hi]; dd->alpha = halpha[hi]; }
            }
        }
        //opencode: textured harness quad — textureIndex resolves under the
        //   resource dir (Texture_Add queues the load; the per-frame
        //   Texture_Update finishes it before capture; the decal draw
        //   falls back to matte while unloaded, so ordering is safe).
        {
            gX_I8 dtexPath[256];
            snprintf(dtexPath, sizeof(dtexPath), GX_RESOURCE_DIR "/texture/misc/testcard.png");
            gX_I32 dtexIdx = gX_RendererGFX_Texture_Add(gfx, dtexPath);
            gX_F32v3 dtexPos = {2.0f, 0.09f, -0.5f};
            gX_I32 di = gX_RendererGFX_Decal_Add(gfx, dtexPos, flatN, 2.0f, dtexIdx, -1.0);
            if (di >= 0) {
                gX_RendererGFX_Decal *dd = gX_Heap_SeekPtr_Index(&gfx->decals, (gX_U32)di);
                if (dd) { dd->rotation = 0.9f; dd->alpha = 1.0f; }
            }
        }
    }

    //opencode: deterministic bounded snow fixture — projected white patches
    //   exercise coverage, melt, height, floor edges, sloped-facing response,
    //   and the existing shadow receiver without adding a weather system.
    {
        const gX_I8 *snowHook = getenv("GX_T_SNOW");
        if (snowHook && snowHook[0] == '1') {
            const gX_I8 *coverageHook = getenv("GX_SNOW_COVERAGE");
            const gX_I8 *meltHook = getenv("GX_SNOW_MELT");
            const gX_I8 *heightHook = getenv("GX_SNOW_HEIGHT");
            const gX_I8 *slopeHook = getenv("GX_SNOW_SLOPE");
            gX_F32 coverage = coverageHook ? (gX_F32)atof(coverageHook) : 0.75f;
            gX_F32 melt = meltHook ? (gX_F32)atof(meltHook) : 0.0f;
            gX_F32 height = heightHook ? (gX_F32)atof(heightHook) : 0.015f;
            gX_I1 slopes = !slopeHook || atoi(slopeHook) != 0;
            if (coverage < 0.0f) coverage = 0.0f;
            if (coverage > 1.0f) coverage = 1.0f;
            if (melt < 0.0f) melt = 0.0f;
            if (melt > 1.0f) melt = 1.0f;
            if (height < 0.0f) height = 0.0f;
            if (height > 0.08f) height = 0.08f;
            static const gX_F32v3 snowPositions[] = {
                {0.5f, 0.0f, 0.5f}, {-1.5f, 0.0f, 1.0f}, {-3.0f, 0.0f, -1.0f},
                {2.5f, 0.0f, 1.8f}, {-4.0f, 0.0f, 2.5f}, {3.5f, 0.0f, -2.5f},
            };
            static const gX_F32 snowSizes[] = {0.8f, 0.5f, 0.6f, 0.75f, 0.45f, 0.9f};
            static const gX_F32 snowMeltWeights[] = {0.2f, 0.5f, 0.8f, 0.35f, 0.65f, 1.0f};
            static const gX_F32 snowRotations[] = {0.3f, 1.1f, 2.0f, 0.7f, 2.6f, 1.8f};
            static const gX_F32v3 snowNormals[] = {
                {0.0f, 1.0f, 0.0f}, {0.35f, 0.93675f, 0.0f},
                {-0.25f, 0.96825f, 0.0f}, {0.0f, 1.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.2f, 0.9799f, 0.0f},
            };
            gX_U32 added = 0;
            gfx->state.ENABLE_DECALS = 1;
            for (gX_U32 si = 0; si < sizeof(snowSizes) / sizeof(snowSizes[0]); si++) {
                gX_F32 amount = coverage * (1.0f - melt * snowMeltWeights[si]);
                if (amount <= 0.02f) continue;
                gX_I32 di = gX_RendererGFX_Decal_Add(gfx,
                    (gX_F32v3){snowPositions[si].x, 0.075f + height, snowPositions[si].z},
                    slopes ? snowNormals[si] : (gX_F32v3){0.0f, 1.0f, 0.0f},
                    snowSizes[si] * (0.35f + amount * 0.65f), -1, -1.0);
                if (di < 0) continue;
                gX_RendererGFX_Decal *snow = gX_Heap_SeekPtr_Index(&gfx->decals, (gX_U32)di);
                if (!snow) continue;
                snow->rotation = snowRotations[si];
                snow->alpha = 0.82f + amount * 0.17f;
                snow->snowLayer = 1;
                added++;
            }
            gX_TraceLog(gX_LOG_NOTICE,
                "SNOW-FIXTURE: patches=%u coverage=%.2f melt=%.2f height=%.3f slopes=%d",
                added, (double)coverage, (double)melt, (double)height, (int)slopes);
        }
    }

    //opencode: coloured point lights around the cluster (type 1 = point)
    static const gX_F32v3 lightPositions[] = {
        { 2.5f, 2.0f,  2.5f},
        {-2.5f, 2.0f,  2.5f},
        { 0.0f, 3.0f, -2.5f},
    };
    static const gX_Color lightColors[] = {
        {255, 90, 90, 255},
        {90, 190, 255, 255},
        {255, 210, 90, 255},
    };
    for (gX_U32 i = 0; i < 3; i++) {
        gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
        if (!l) continue;
        gX_RendererGFX_Light_Init(l);
        l->type = 1;
        l->pos = lightPositions[i];
        l->pos.y += waterLift;
        l->color = lightColors[i];
        l->diffuseTint = lightColors[i];
        l->specularTint = lightColors[i];
        l->brightness = 1.4f;
        l->attenConst = 1.0f;
        l->attenLinear = 0.0f;
        l->attenQuad = 0.35f;
    }

    //opencode: spotlight with a projected cookie — exercises the cookie atlas
    //   path (the only other writer was the deleted AI harness). A windowed
    //   "gobo" light above the cluster, testcard = clear window pattern.
    {
        gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
        if (l) {
            gX_RendererGFX_Light_Init(l);
            l->type = 2;
            l->pos = (gX_F32v3){0.0f, 4.0f, -0.5f};
            l->pos.y += waterLift;
            l->dir = (gX_F32v3){0.0f, -1.0f, 0.0f};
            l->fov = 45.0f;
            l->color = (gX_Color){255, 255, 220, 255};
            l->diffuseTint = l->color;
            l->specularTint = l->color;
            l->brightness = 2.0f;
            l->attenConst = 1.0f;
            l->attenLinear = 0.0f;
            l->attenQuad = 0.15f;
            l->castShadows = 0; //opencode: spot light doesn't cast point shadows
            gX_RendererGFX_Light_SetCookie(gfx, l, GX_RESOURCE_DIR "/texture/cookie_window.png");
        }
    }

    //opencode: v0.5.0 — GX_EXTRA_LIGHTS=N: ring of N extra point lights for
    //   ceiling-lift verification (SSBO path handles >32 total lights; the
    //   uniform path clamps at 32). Ring at radius 7 sits outside the demo
    //   cluster so per-tile masks stay under the 32-slot cap.
    //   GX_EXTRA_SHADOW_LIGHTS=N: same ring, shadow-casting (point shadows
    //   beyond the old 8-light cap / 32-light uniform clamp).
    //opencode: v0.5.2 — GX_EXTRA_COOKIE_LIGHTS=N: ring of spot lights
    //   (type 2) aimed at the demo cluster with the window cookie, for
    //   >32-light cookie verification (uniform path clamps at 32).
    const gX_I8 *ec = getenv("GX_EXTRA_COOKIE_LIGHTS");
    const gX_I8 *esp = ec ? NULL : getenv("GX_EXTRA_SPOT_LIGHTS");
    {
        const gX_I8 *es = (ec || esp) ? NULL : getenv("GX_EXTRA_SHADOW_LIGHTS");
        const gX_I8 *el = es ? es : ec ? ec : esp ? esp : getenv("GX_EXTRA_LIGHTS");
        if (el) {
            gX_I32 n = atoi(el);
            if (n < 1) n = 1;
            if (n > 64) n = 64;
            for (gX_I32 li = 0; li < n; li++) {
                gX_RendererGFX_Light *l = gX_Heap_Add_Push_NoPtr(&gfx->lights);
                if (!l) continue;
                gX_RendererGFX_Light_Init(l);
                gX_F32 ang = (gX_F32)li / (gX_F32)n * 2.0f * 3.14159265f;
                l->pos = (gX_F32v3){ cosf(ang) * 7.0f, 2.2f, sinf(ang) * 7.0f };
                l->type = (ec || esp) ? 2 : 1;
                l->dir = gX_F32v3_Normalize(gX_F32v3_Sub((gX_F32v3){0.0f, 0.6f, 0.0f}, l->pos));
                l->fov = (ec || esp) ? 40.0f : 45.0f;
                l->color = (gX_Color){255, 120 + (li % 4) * 30, 90, 255};
                l->diffuseTint = l->color;
                l->specularTint = l->color;
                l->brightness = 2.5f;
                l->attenConst = 1.0f;
                l->attenLinear = 0.0f;
                l->attenQuad = 0.06f;
                if (es) {
                    l->castShadows = true;
                    l->shadowRange = 6.0f;
                }
                if (ec)
                    gX_RendererGFX_Light_SetCookie(gfx, l, GX_RESOURCE_DIR "/texture/cookie_window.png");
            }
            gX_TraceLog(gX_LOG_NOTICE, "EXTRA LIGHTS: added %d (total %u)",
                n, gfx->lights.amount);
        }
    }

    //opencode: audit — headless harness hooks for the demo scene moved to
    //   the top of this function (hunt): verification scenes return early
    //   and never reached this spot. EXTRA LIGHTS stay here.

    //opencode: noon-haze grade (GX_NOON=1 on --showcase): bright flat day,
    //   cheap LINEAR distance fog (standard.frag gX_Fog_Mix, not the
    //   volumetric march), volume fog off, calm glow (threshold 1.0 keeps
    //   the chain but starves it). Started life as a minecraft-alpha
    //   atmosphere attempt: the cluster can never read as blocks without
    //   per-model albedo tint (params carry specular tint only), so this
    //   ships as what it is — a bright-day haze preset. See backlog 25.
    //   Default demo unchanged when unset.
    {
        const gX_I8 *mc = getenv("GX_NOON");
        if (mc && mc[0] == '1') {
            gfx->sun.brightness = 1.2f;
            gfx->sun.color = (gX_Color){255, 250, 240, 255};
            gfx->sun.diffuseTint = gfx->sun.color;
            gfx->sun.specularTint = gfx->sun.color;
            gfx->state.exposure = 0.85f;
            gfx->state.ENABLE_FOG = 1;
            gfx->state.ENABLE_VOLUME_FOG = 0;
            gfx->fog.near = 14.0f;
            gfx->fog.far = 48.0f;
            //opencode: hazy warm-grey — the 8u slab sits in infinite tan
            //   shader-ground; sky-blue fog clashed with it. Midpoint tone
            //   melts slab into surround (alpha's horizon-melt recipe).
            gfx->fog.color = (gX_Color){190, 185, 175, 255};
            gfx->bloom.threshold = 1.0f;
            gfx->bloom.intensity = 1.0f;
            //opencode: alpha has no drifting sprite blobs — the showcase
            //   particle system reads as pastel balloons in stills.
            gfx->state.ENABLE_PARTICLES = 0;
            //opencode: ACES, not AgX — the block diorama's greens do not
            //   survive AgX (backlog 23 rule: it preserves R, crushes G/B
            //   ~0.75x, so grass (110,175,70) and dirt (150,115,75)
            //   converge to one salmon; proven by capture pair 2026-09-18:
            //   magenta/lime vc split renders, MC greens collapse).
            //   Same per-scene opt-out as the SH grade; default untouched.
            gfx->state.ENABLE_AGX_TONEMAP = 0;
            //opencode: block diorama — terraced hill annex east of the
            //   cluster, inside the default orbit view (the first attempt
            //   at (11,9) sat behind the camera). Footprints disjoint from
            //   the cluster, so the reseat pass is a no-op; layers abut
            //   exactly, never overlap. Per-face grass-block split via
            //   injected vertex colors (green tops, dirt sides; bottom
            //   layer all dirt).
            //   NOTE: cube.glb is 2x2x2 centered (layout dump), so spacing
            //   is 2.0 and layer centers sit supportY + 1.0 + 2*li (the
            //   first 1.0-spaced attempt interpenetrated and buried every
            //   top face — read as uniform masses, no green).
            {
                const gX_I32 terraces[3] = {3, 2, 1};
                for (gX_I32 li = 0; li < 3; li++) {
                    gX_I32 n = terraces[li];
                    for (gX_I32 ix = 0; ix < n; ix++) {
                        for (gX_I32 iz = 0; iz < n; iz++) {
                            gX_RendererGFX_Model *bm = gX_Heap_Add_Push_NoPtr(&gfx->models);
                            if (!bm) continue;
                            gX_I8 bpath[256];
                            snprintf(bpath, sizeof(bpath), GX_RESOURCE_DIR "/model/primitive/cube.glb");
                            gX_RendererGFX_Model_Init(bm, bpath);
                            bm->params.pos = (gX_F32v3){
                                7.5f + 2.0f * (gX_F32)(ix - n / 2),
                                1.075f + 2.0f * (gX_F32)li,
                                -4.0f + 2.0f * (gX_F32)(iz - n / 2)};
                            bm->params.scale = (gX_F32v3){1.0f, 1.0f, 1.0f};
                            bm->params.usePBR = 1;
                            bm->params.metalness = 0.0f;
                            bm->params.shininess = 24.0f;
                            bm->params.roughness = 0.9f;
                            bm->params.albedo = 1.0f;
                            bm->params.useTopTint = 1;
                            if (li >= 1) bm->params.albedoTintTop = (gX_Color){110, 175, 70, 255};
                            else bm->params.albedoTintTop = (gX_Color){150, 115, 75, 255};
                            bm->params.albedoTint = (gX_Color){150, 115, 75, 255};
                        }
                    }
                }
            }
        }
    }

    return 0;
}
