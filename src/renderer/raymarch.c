/**********************************************************************************************
*
*   [gX: src/renderer/raymarch.c]
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


#include <math.h>
#include "cubemap.h"
#include <string.h>
#include <stdio.h>


#include "raymarch.h"
//ai: loader draws temporarily to the window while preserving the caller's GL state.
#include "../rhi/gl_state.h"
#include "../rhi/batch.h"
#include "../rhi/rhi_state.h"
#include "renderer.h"
#include "sky.h" //opencode: draw sky background before raymarch
#include "../main.h"
#include "../camera.h"

#include "../common/macros.h"
#include "../common/utils.h"

#include "../common/path.h"
#include "../gxmath/gxmath.h"

#include "../game/ephemeris.h"
#include "../game/game.h"

#include "../wrappers/wrappers.h"

#include "../common/random.h"
#include <stdlib.h>
#include "raymarch/schema.h"
#include "raymarch/schema2.h"
//opencode: hunt - implicit-decl sweep
#include "../rhi/rhi.h"

#define GX_RM_GET_LOC(locname,locstr) \
    sha->locations.locname =  gX_Shader_GetLocation(sha->shader.shader,#locstr);
    
#define GX_RM_GET_LOCV(locname,count,locstrs) \
    sha->locations.locname = gX_Shader_GetLocation_Multi(sha->shader.shader, count, (locstrs));
    
#define GX_RM_GET_LOC2(locname,loc0,loc1) \
    sha->locations.locname = gX_Shader_GetLocation_Multi(sha->shader.shader, 2, loc0, loc1);
    
#define GX_RM_GET_LOC3(locname,loc0,loc1,loc2) \
    sha->locations.locname = gX_Shader_GetLocation_Multi(sha->shader.shader, 3, loc0, loc1, loc2);

//opencode: headless preset round-trip hook (gate) — see Update.
static gX_I1 s_headlessPresetCheckDone = 0;
static gX_I1 s_headlessScaleDone = 0;
static gX_I1 s_headlessGalleryDone = 0;
static gX_I1 s_headlessFeatureDone = 0;

//opencode: map a feature name to its enable flag (feature-matrix gate).
static gX_I1 *gX_RendererRaymarch_Flag_By_Name(gX_Raymarch_Flags *flags,
    const gX_I8 *name) {
    if (!strcmp((const char *)name, "terrain")) return &flags->enableTerrain;
    if (!strcmp((const char *)name, "grid")) return &flags->enableTerrainGrid;
    if (!strcmp((const char *)name, "water")) return &flags->enableWater;
    if (!strcmp((const char *)name, "fog")) return &flags->enableFog;
    if (!strcmp((const char *)name, "sky")) return &flags->enableSky;
    if (!strcmp((const char *)name, "ao")) return &flags->enableAO;
    if (!strcmp((const char *)name, "objects")) return &flags->enableObjects;
    if (!strcmp((const char *)name, "lights")) return &flags->enableLights;
    if (!strcmp((const char *)name, "glow")) return &flags->enableObjectGlow;
    if (!strcmp((const char *)name, "lightglow"))
        return &flags->enableLightGlow;
    if (!strcmp((const char *)name, "sunshadows"))
        return &flags->enableSunShadows;
    if (!strcmp((const char *)name, "lightshadows"))
        return &flags->enableLightShadows;
    if (!strcmp((const char *)name, "frag")) return &flags->enableFrag;
    if (!strcmp((const char *)name, "proceduralsky"))
        return &flags->enableProceduralSky;
    if (!strcmp((const char *)name, "clouds")) return &flags->enableClouds;
    if (!strcmp((const char *)name, "proxy")) return &flags->enableProxy;
    if (!strcmp((const char *)name, "volfog"))
        return &flags->enableVolumetricFog;
    return NULL;
}

static gX_I0 gX_RendererRaymarch_Shader_GetLocations(gX_Raymarch_Shader *sha) {
    
    GX_RM_GET_LOC2(RMbounces,"rmBounces","rmbounces")
    GX_RM_GET_LOC2(RMsteps,"rmSteps","rmsteps")
    //opencode
    GX_RM_GET_LOC2(RMstepsPerBounce,"rmStepsPerBounce","rmstepsperbounce")
    GX_RM_GET_LOC2(RMnear,"rmNear","rmnear")
    GX_RM_GET_LOC2(RMfar,"rmFar","rmfar")
    //opencode
    GX_RM_GET_LOC2(RMstepMax,"rmStepMax","rmstepmax")
    GX_RM_GET_LOC2(RMdistTotal,"rmDistTotal","rmdisttotal")
    GX_RM_GET_LOC2(RMepsilon,"rmEpsilon","rmlimit")
    GX_RM_GET_LOC2(RMpoly,"rmPolynomial","rmpolynomial")
    
    //opencode: water animation (replaces dead texSky/waterScale queries)
    GX_RM_GET_LOC2(waterSpeed,"waterSpeed","waterspeed")
    GX_RM_GET_LOC2(waterSeed,"waterSeed","waterseed")
    GX_RM_GET_LOC2(gradClamp,"gradClamp","gradclamp")
    
    GX_RM_GET_LOC2(sunBright,"sunBright","sunbright")
    GX_RM_GET_LOC2(sunPos,"sunPos","sunpos")
    GX_RM_GET_LOC2(sunColor,"sunColor","suncolor")
    GX_RM_GET_LOC2(sunSize,"sunSize","sunsize") //opencode
    
    GX_RM_GET_LOC2(terrainSeed,"terrainSeed","terrainseed")
    GX_RM_GET_LOC2(terrainOffset,"terrainPos","terrainpos")
    GX_RM_GET_LOC2(terrainScale,"terrainScale","terrainscale")
    GX_RM_GET_LOC2(terrainColor,"terrainColor","terraincolor")
    GX_RM_GET_LOC2(terrainColorSteep,"terrainColor_s","terraincolor_s")
    GX_RM_GET_LOC2(terrainColorBeach,"terrainColor_c","terraincolor_c")
    //opencode
    GX_RM_GET_LOC2(terrainParameters,"terrainParameters","terrainparameters")
    GX_RM_GET_LOC2(terrainRotate,"terrainRotate","terrainrotate") //opencode
    
    GX_RM_GET_LOC2(waterOffset,"waterPos","waterpos")
    GX_RM_GET_LOC2(waterColor,"waterColor","watercolor")
    GX_RM_GET_LOC2(waterRipple,"waterRipple","waterripple")
    GX_RM_GET_LOC2(waterWhirlpool,"waterWhirlpool","waterwhirlpool")
    
    GX_RM_GET_LOC2(objectAmount,"objectAmount","objectamount")
    GX_RM_GET_LOC2(objectType,"objectType","objecttype")
    GX_RM_GET_LOC2(objectPos,"objectPos","objectpos")
    GX_RM_GET_LOC2(objectMatX,"objectMatX","objectmatx")
    GX_RM_GET_LOC2(objectMatY,"objectMatY","objectmaty")
    GX_RM_GET_LOC2(objectMatZ,"objectMatZ","objectmatz")
    GX_RM_GET_LOC2(objectColor,"objectColor","objectcolor")
    GX_RM_GET_LOC2(objectAttribute,"objectAttribute","objectattribute")
    GX_RM_GET_LOC2(objectMaterial,"objectMaterial","objectmaterial")
    GX_RM_GET_LOC2(objectTexturing,"objectTexturing","objecttexturing") //opencode
    //ai: graph arrays are compacted CPU-side and never exceed the GL33 program cap.
    GX_RM_GET_LOC2(graphCount,"graphCount","graphCount")
    GX_RM_GET_LOC2(graphNodes,"graphNodes","graphNodes")
    GX_RM_GET_LOC2(graphDomains,"graphDomains","graphDomains")
    GX_RM_GET_LOC2(objectLighting,"objectLighting","objectlighting")
    
    GX_RM_GET_LOC2(lightAmount,"lightAmount","lightamount")
    GX_RM_GET_LOC2(lightBrightness,"lightBright","lightbright")
    GX_RM_GET_LOC2(lightPos,"lightPos","lightpos")
    GX_RM_GET_LOC2(lightColor,"lightColor","lightcolor")
    GX_RM_GET_LOC2(lightAmbient,"lightAmbient","lightambient")
    
    GX_RM_GET_LOC2(fogNear,"fogNear","fognear")
    GX_RM_GET_LOC2(fogFar,"fogFar","fogfar")
    GX_RM_GET_LOC2(fogColor,"fogColor","fogcolor")
    
    GX_RM_GET_LOC2(enableShadows,"enableShadows","enable_shadows")
    GX_RM_GET_LOC2(enableGlow,"enableGlow","enable_glow")
    GX_RM_GET_LOC2(enableLightGlow,"enableLightGlow","enable_lightglow")
    GX_RM_GET_LOC2(enableIllu,"enableIllu","enable_illu")
    GX_RM_GET_LOC2(enableFrag,"enableFrag","enable_frag")
    GX_RM_GET_LOC2(enableFog,"enableFog","enable_fog")
    GX_RM_GET_LOC2(enableTerrain,"enableTerrain","enable_terrain")
    GX_RM_GET_LOC2(enableWater,"enableWater","enable_water")
    GX_RM_GET_LOC2(enableObjects,"enableObjects","enable_objects")
    GX_RM_GET_LOC2(enableLights,"enableLights","enable_lights")
    GX_RM_GET_LOC2(enableGrid,"enableGrid","enable_grid")
    GX_RM_GET_LOC2(enableLSS,"enableLastStepSample","enable_laststepsample")
    GX_RM_GET_LOC2(enableSS,"enableSignSample","enable_signsample")
    GX_RM_GET_LOC2(enableSky,"enableTexsky","enable_texsky")
    
    GX_RM_GET_LOC2(firstColor,"firstColor","firstcolor")
    GX_RM_GET_LOC2(nullColor,"nullColor","nullcolor")
    GX_RM_GET_LOC2(lightAttenuation,"lightAttenuation","lightattenuation")
    
    //opencode: debug visualizer uniforms
    GX_RM_GET_LOC2(debugShowPos,"debugShowPos","debugshowpos")
    GX_RM_GET_LOC2(debugShowDir,"debugShowDir","debugshowdir")
    GX_RM_GET_LOC2(debugShowRay,"debugShowRay","debugshowray")
    GX_RM_GET_LOC2(debugShowGradient,"debugShowGradient","debugshowgradient")
    GX_RM_GET_LOC2(debugShowNormal,"debugShowNormal","debugshownormal")
    GX_RM_GET_LOC2(debugShowBounceCount,"debugShowBounceCount","debugshowbouncecount")
    GX_RM_GET_LOC2(debugShowPathSteps,"debugShowPathSteps","debugshowpathsteps")
    GX_RM_GET_LOC2(debugShowPathLength,"debugShowPathLength","debugshowpathlength")
    GX_RM_GET_LOC2(debugShowDist,"debugShowDist","debugshowdist")
    //opencode: AO
    GX_RM_GET_LOC2(enableAO,"enableAO","enable_ao")
    //opencode
    GX_RM_GET_LOC2(enableLightShadows,"enableLightShadows","enable_lightshadows")
    //opencode: noise texture
    sha->locations.texNoise = gX_RHI_GetUniformLocation(sha->shader.shader.id, "texNoise");
    //opencode: procedural sky
    sha->locations.enableProceduralSky = gX_Shader_GetLocation(sha->shader.shader, "enableProceduralSky");
    //ai: volumetric clouds
    GX_RM_GET_LOC2(enableClouds,"enableClouds","enable_clouds")
    GX_RM_GET_LOC2(cloudBottom,"cloudBottom","cloudbottom")
    GX_RM_GET_LOC2(cloudTop,"cloudTop","cloudtop")
    GX_RM_GET_LOC2(cloudCoverage,"cloudCoverage","cloudcoverage")
    GX_RM_GET_LOC2(cloudDensity,"cloudDensity","clouddensity")
    GX_RM_GET_LOC2(cloudScale,"cloudScale","cloudscale")
    GX_RM_GET_LOC2(cloudSpeed,"cloudSpeed","cloudspeed")
    GX_RM_GET_LOC2(cloudAbsorb,"cloudAbsorb","cloudabsorb")
    GX_RM_GET_LOC2(cloudPowder,"cloudPowder","cloudpowder")
    GX_RM_GET_LOC2(cloudColor,"cloudColor","cloudcolor")
    //ai: volumetric fog
    GX_RM_GET_LOC2(enableVolFog,"enableVolFog","enable_volfog")
    GX_RM_GET_LOC2(volFogDensity,"volFogDensity","volfogdensity")
    GX_RM_GET_LOC2(volFogHeightFalloff,"volFogHeightFalloff","volfogheightfalloff")
    GX_RM_GET_LOC2(volFogScattering,"volFogScattering","volfogscattering")
    GX_RM_GET_LOC2(volFogHG,"volFogHG","volfoghg")
    GX_RM_GET_LOC2(volFogColor,"volFogColor","volfogcolor")
}

//ai: primitive type IDs are not array slots; reject invalid storage indices.
gX_I0 gX_RendererRaymarch_Object_Init(gX_RendererRaymarch *rm, gX_U32 i) {
    if (!rm || i >= GX_RM_MAX_OBJECTS) return;
    rm->objects.roughness[i] = 1.0f;
    rm->objects.type[i] = (gX_I32)i;
    //opencode: arrange objects in visible positions around the camera
    gX_F32 angle = (gX_F32)i * 1.2f;
    gX_F32 dist = 8.0f + (gX_F32)i * 2.0f;
    rm->objects.pos[i] = (gX_F32v3){cosf(angle) * dist, 1.0f + (gX_F32)i * 0.5f, sinf(angle) * dist};
    
    rm->objects.matX[i] = (gX_F32v3){1.0f,0.0f,0.0f};
    rm->objects.matY[i] = (gX_F32v3){0.0f,1.0f,0.0f};
    rm->objects.matZ[i] = (gX_F32v3){0.0f,0.0f,1.0f};
    
    rm->objects.color[i].x = 0.5f*(sinf((gX_F32)i+0.0f*gX_PI/3.0f)+1.0f);
    rm->objects.color[i].y = 0.5f*(sinf((gX_F32)i+2.0f*gX_PI/3.0f)+1.0f);
    rm->objects.color[i].z = 0.5f*(sinf((gX_F32)i+4.0f*gX_PI/3.0f)+1.0f);
    rm->objects.color[i].w = 1.0f;
    
    rm->objects.parameters[i] = (gX_F32v4){1.0f,1.0f,1.0f,1.0f};
    
    rm->objects.material[i] = (gX_F32v4){0.0f,0.0f,1.0f,1.0f};
    rm->objects.texturing[i] = (gX_F32v4){0.0f,0.0f,1.0f,1.0f};
    rm->objects.lighting[i] = (gX_F32v4){0.1f,1.0f,1.0f,64.0f};
}

gX_I0 gX_RendererRaymarch_Light_Init(gX_RendererRaymarch *rm, gX_U32 i) {
    rm->lights.brightness[i] = 1.0f;
    rm->lights.pos[i] = (gX_F32v3){0.0f+5.0f*(gX_F32)i,2.0f,3.0f};
    rm->lights.color[i] = (gX_F32v4){0.0f,1.0f,0.0f,1.0f};
}

//ai: presets change tracing quality only, never authored geometry/material/feature flags.
static const gX_F32 rmQualityScale[4] = {0.25f,0.35f,0.5f,1.0f};
static const gX_I32 rmQualitySteps[4] = {128,128,192,256};
static const gX_I32 rmQualityBounces[4] = {1,2,3,4};

//ai: union/intersection/difference and their polynomial smooth counterparts share A-minus-B semantics.
gX_F32 gX_RendererRaymarch_Graph_Combine(gX_I32 operation, gX_F32 a, gX_F32 b, gX_F32 smoothness)
{
    if (operation < 2 || operation > 7 || !isfinite(a) || !isfinite(b)
        || !isfinite(smoothness) || smoothness < 0) return NAN;
    if (operation == 3 || operation == 6) b = -b;
    gX_I1 maximum = operation == 3 || operation == 4 || operation == 6 || operation == 7;
    gX_F32 result = maximum ? fmaxf(a,b) : fminf(a,b);
    if (operation >= 5 && operation <= 7 && smoothness > 0)
    {
        gX_F32 h = fmaxf(smoothness-fabsf(a-b),0.0f) / smoothness;
        result += (maximum ? 1.0f : -1.0f) * smoothness * h * h * 0.25f;
    }
    return result;
}

//ai: compile a bounded DAG without recursion; repetitions initially accept centered sphere/box leaves.
gX_I1 gX_RendererRaymarch_Graph_Compile(gX_RendererRaymarch *rm)
{
    if (!rm) return 0;
    gX_Raymarch_Graph *graph = &rm->graph;
    graph->count = 0;
    graph->error[0] = 0;
    if (graph->root == -1) return 1;
    gX_I32 used[64] = {0}, indices[64], stack[64], pending = 0;
    if (graph->root < 0 || graph->root >= 64 || rm->objects.amount < 0 || rm->objects.amount > GX_RM_MAX_OBJECTS) goto invalid;
    for (gX_I32 i = 0; i < 64; i++) indices[i] = -1;
    used[graph->root] = 1;
    stack[pending++] = graph->root;
    while (pending)
    {
        gX_I32 slot = stack[--pending];
        gX_F32v4 node = graph->nodes[slot];
        if (!isfinite(node.x) || node.x < 1 || node.x > 9 || node.x != floorf(node.x)
            || !isfinite(node.w) || node.w < 0) goto invalid;
        gX_I32 op = (gX_I32)node.x;
        if (!isfinite(node.y) || node.y != floorf(node.y) || node.y < 0
            || node.y >= (op == 1 ? rm->objects.amount : 64)) goto invalid;
        if (op == 1) continue;
        gX_F32 refs[2] = {node.y,node.z};
        for (gX_I32 j = 0; j < (op >= 8 ? 1 : 2); j++)
        {
            if (!isfinite(refs[j]) || refs[j] < 0 || refs[j] >= 64 || refs[j] != floorf(refs[j])) goto invalid;
            gX_I32 input = (gX_I32)refs[j];
            if (!used[input]) {used[input] = 1; stack[pending++] = input;}
        }
        if (op >= 8)
        {
            gX_F32v4 leaf = graph->nodes[(gX_I32)node.y], domain = graph->domains[slot];
            if (leaf.x != 1 || !isfinite(leaf.y) || leaf.y < 0 || leaf.y >= rm->objects.amount
                || leaf.y != floorf(leaf.y)) goto invalid;
            gX_I32 object = (gX_I32)leaf.y, type = rm->objects.type[object];
            if (type != 0 && type != 1) goto invalid;
            gX_F32v4 dimensions = rm->objects.parameters[object];
            gX_F32 periods[3] = {domain.x,domain.y,domain.z};
            gX_F32 extents[3] = {dimensions.x,type ? dimensions.y : dimensions.x,type ? dimensions.z : dimensions.x};
            if (!isfinite(domain.w) || domain.w < 0 || domain.w > 16777215 || domain.w != floorf(domain.w)) goto invalid;
            for (gX_I32 axis = 0; axis < 3; axis++)
                if (!isfinite(periods[axis]) || periods[axis] < 0 || !isfinite(extents[axis]) || extents[axis] <= 0
                    || (periods[axis] > 0 && extents[axis] > periods[axis]*0.5f)) goto invalid;
        }
    }
    for (gX_I32 pass = 0; pass < 64; pass++)
    {
        gX_I32 progress = 0, remaining = 0;
        for (gX_I32 slot = 0; slot < 64; slot++)
        {
            if (!used[slot] || indices[slot] >= 0) continue;
            remaining++;
            gX_F32v4 node = graph->nodes[slot];
            gX_I32 op = (gX_I32)node.x, a = (gX_I32)node.y;
            if (op != 1 && (indices[a] < 0 || (op < 8 && indices[(gX_I32)node.z] < 0))) continue;
            if (graph->count >= GX_RM_GRAPH_PROGRAM) goto invalid;
            if (op >= 8) node.y = graph->nodes[a].y;
            else if (op != 1) {node.y = (gX_F32)indices[a]; node.z = (gX_F32)indices[(gX_I32)node.z];}
            indices[slot] = graph->count;
            graph->program[graph->count] = node;
            graph->programDomains[graph->count++] = graph->domains[slot];
            progress++;
        }
        if (!remaining) return 1;
        if (!progress) goto invalid;
    }
invalid:
    graph->count = 0;
    snprintf(graph->error,sizeof(graph->error),"Invalid/cyclic graph, >32 reachable nodes, or unsupported/overlapping repeat domain");
    return 0;
}

//ai: invalid quality selections leave the previous tuning intact.
gX_I1 gX_RendererRaymarch_Quality_Set(gX_RendererRaymarch *rm, gX_I32 level)
{
    if (!rm || level < 0 || level >= 4) return 0;
    rm->parameters.renderScale = rmQualityScale[level];
    rm->parameters.steps = rmQualitySteps[level];
    rm->parameters.bounces = rmQualityBounces[level];
    rm->parameters.stepsPerBounce = 0;
    return 1;
}

//ai: report Custom (-1) when manual tuning no longer matches a quality preset.
gX_I32 gX_RendererRaymarch_Quality_Get(const gX_RendererRaymarch *rm)
{
    if (!rm || rm->parameters.stepsPerBounce != 0) return -1;
    for (gX_I32 i = 0; i < 4; i++)
    {
        if (fabsf(rm->parameters.renderScale-rmQualityScale[i]) < 0.00001f
            && rm->parameters.steps == rmQualitySteps[i]
            && rm->parameters.bounces == rmQualityBounces[i]) return i;
    }
    return -1;
}

//ai: changing a primitive resets only its dimensions, preserving appearance and placement.
gX_I1 gX_RendererRaymarch_Object_Type_Set(gX_RendererRaymarch *rm,
    gX_I32 index, gX_I32 type)
{
    static const gX_F32v4 defaults[14] = {
        {1,0,0,0}, {1,1,1,0}, {1,0.25f,0,0}, {1,1,1,0.1f},
        {1,1,0,0}, {0.6f,0.8f,1,0}, {0}, {1,8,8,4}, {0},
        {1,1,1,0}, {1,0.35f,0,0}, {1,0,0,0}, {1,1,1,0.2f}, {1,1,0,0}};
    if (!rm || index < 0 || index >= rm->objects.amount
        || index >= GX_RM_MAX_OBJECTS || type < 0 || type >= 14) return 0;
    rm->objects.type[index] = type;
    rm->objects.parameters[index] = defaults[type];
    return 1;
}

//ai: measure the forward inverse-transpose axes, including imported shear/mirroring.
gX_F32v3 gX_RendererRaymarch_Object_Scale_Get(const gX_RendererRaymarch *rm, gX_I32 index)
{
    gX_F32v3 result = {0};
    if (!rm || index < 0 || index >= rm->objects.amount || index >= GX_RM_MAX_OBJECTS)
        return result;
    gX_F32v3 x = gX_F32v3_Cross(rm->objects.matY[index], rm->objects.matZ[index]);
    gX_F32v3 y = gX_F32v3_Cross(rm->objects.matZ[index], rm->objects.matX[index]);
    gX_F32v3 z = gX_F32v3_Cross(rm->objects.matX[index], rm->objects.matY[index]);
    gX_F32 det = fabsf(gX_F32v3_Dot(rm->objects.matX[index], x));
    if (!isfinite(det) || det < 0.000000000001f) return result;
    result = (gX_F32v3){gX_F32v3_Length(x)/det, gX_F32v3_Length(y)/det,
        gX_F32v3_Length(z)/det};
    return result;
}

//ai: rescale columns without replacing their orientation or rewriting imported matrices.
gX_I1 gX_RendererRaymarch_Object_Scale_Set(gX_RendererRaymarch *rm,
    gX_I32 index, gX_F32v3 scale)
{
    gX_F32v3 old = gX_RendererRaymarch_Object_Scale_Get(rm, index);
    if (!isfinite(scale.x) || !isfinite(scale.y) || !isfinite(scale.z)
        || scale.x <= 0 || scale.y <= 0 || scale.z <= 0
        || !isfinite(old.x) || !isfinite(old.y) || !isfinite(old.z)
        || old.x <= 0 || old.y <= 0 || old.z <= 0) return 0;
    rm->objects.matX[index] = gX_F32v3_Scale(rm->objects.matX[index], old.x/scale.x);
    rm->objects.matY[index] = gX_F32v3_Scale(rm->objects.matY[index], old.y/scale.y);
    rm->objects.matZ[index] = gX_F32v3_Scale(rm->objects.matZ[index], old.z/scale.z);
    return 1;
}

//ai: apply XYZ world-axis deltas, conjugating the shader's legacy slot-dependent yaw.
gX_I1 gX_RendererRaymarch_Object_Rotate(gX_RendererRaymarch *rm,
    gX_I32 index, gX_F32v3 degrees)
{
    if (!rm || index < 0 || index >= rm->objects.amount || index >= GX_RM_MAX_OBJECTS
        || !isfinite(degrees.x) || !isfinite(degrees.y) || !isfinite(degrees.z)) return 0;
    gX_F32v3 *columns[3] = {&rm->objects.matX[index], &rm->objects.matY[index],
        &rm->objects.matZ[index]};
    gX_F32 yaw = (gX_F32)index * 1.256f;
    gX_F32 rad = 0.01745329252f;
    for (gX_I32 i = 0; i < 3; i++)
    {
        gX_F32v3 v = gX_F32v3_RotateByAxisAngle(*columns[i], (gX_F32v3){0,1,0}, -yaw);
        v = gX_F32v3_RotateByAxisAngle(v, (gX_F32v3){1,0,0}, degrees.x*rad);
        v = gX_F32v3_RotateByAxisAngle(v, (gX_F32v3){0,1,0}, degrees.y*rad);
        v = gX_F32v3_RotateByAxisAngle(v, (gX_F32v3){0,0,1}, degrees.z*rad);
        *columns[i] = gX_F32v3_RotateByAxisAngle(v, (gX_F32v3){0,1,0}, yaw);
    }
    return 1;
}

//ai: stable compaction compensates slot yaw so deleting a neighbor cannot rotate a mesh.
gX_I1 gX_RendererRaymarch_Object_Remove(gX_RendererRaymarch *rm, gX_I32 index)
{
    if (!rm || index < 0 || index >= rm->objects.amount || rm->objects.amount > GX_RM_MAX_OBJECTS)
        return 0;
    for (gX_I32 i = index; i + 1 < rm->objects.amount; i++)
    {
        gX_I32 src = i + 1;
        rm->objects.type[i] = rm->objects.type[src];
        rm->objects.pos[i] = rm->objects.pos[src];
        rm->objects.matX[i] = gX_F32v3_RotateByAxisAngle(rm->objects.matX[src], (gX_F32v3){0,1,0}, -1.256f);
        rm->objects.matY[i] = gX_F32v3_RotateByAxisAngle(rm->objects.matY[src], (gX_F32v3){0,1,0}, -1.256f);
        rm->objects.matZ[i] = gX_F32v3_RotateByAxisAngle(rm->objects.matZ[src], (gX_F32v3){0,1,0}, -1.256f);
        rm->objects.color[i] = rm->objects.color[src];
        rm->objects.parameters[i] = rm->objects.parameters[src];
        rm->objects.material[i] = rm->objects.material[src];
        rm->objects.roughness[i] = rm->objects.roughness[src];
        rm->objects.texturing[i] = rm->objects.texturing[src];
        rm->objects.lighting[i] = rm->objects.lighting[src];
    }
    rm->objects.amount--;
    //ai: graph leaves follow stable object compaction; deleting a referenced leaf produces an explicit validation error.
    for (gX_I32 slot = 0; slot < GX_RM_GRAPH_SLOTS; slot++)
        if (rm->graph.nodes[slot].x == 1 && rm->graph.nodes[slot].y >= index)
            rm->graph.nodes[slot].y = rm->graph.nodes[slot].y == index ? -1 : rm->graph.nodes[slot].y-1;
    return 1;
}

//ai: inverse-transpose the stored SDF transform, then undo the implicit slot yaw.
gX_F32v3 gX_RendererRaymarch_Object_ToWorld(const gX_Raymarch_Objects *objects,
    gX_I32 index, gX_F32v3 local)
{
    if (!objects || index < 0 || index >= objects->amount || index >= GX_RM_MAX_OBJECTS)
        return (gX_F32v3){0};
    gX_F32v3 x = gX_F32v3_Cross(objects->matY[index], objects->matZ[index]);
    gX_F32v3 y = gX_F32v3_Cross(objects->matZ[index], objects->matX[index]);
    gX_F32v3 z = gX_F32v3_Cross(objects->matX[index], objects->matY[index]);
    gX_F32 det = gX_F32v3_Dot(objects->matX[index], x);
    if (!isfinite(det) || fabsf(det) < 0.000000000001f) return objects->pos[index];
    gX_F32v3 point = {(x.x*local.x+y.x*local.y+z.x*local.z)/det,
        (x.y*local.x+y.y*local.y+z.y*local.z)/det,
        (x.z*local.x+y.z*local.y+z.z*local.z)/det};
    point = gX_F32v3_RotateByAxisAngle(point, (gX_F32v3){0,1,0}, -(gX_F32)index*1.256f);
    return gX_F32v3_Add(point, objects->pos[index]);
}

//opencode: pure CPU octave sampler — the terrain noise bake and the unit
//   tests share it. Value noise over `cells` per axis with smoothstep
//   interpolation; corner hashes wrap at the cell count so it tiles.
gX_F32 gX_RendererRaymarch_Noise_Octave(gX_U32 x, gX_U32 y, gX_U32 size,
    gX_I32 cells, gX_F32 seed) {
    if (cells < 1) cells = 1;
    gX_F32 px = (gX_F32)x * (gX_F32)cells / (gX_F32)size;
    gX_F32 py = (gX_F32)y * (gX_F32)cells / (gX_F32)size;
    gX_I32 ix = (gX_I32)px, iy = (gX_I32)py;
    gX_F32 tx = px - (gX_F32)ix, ty = py - (gX_F32)iy;
    gX_F32 sx = tx * tx * (3.0f - 2.0f * tx);
    gX_F32 sy = ty * ty * (3.0f - 2.0f * ty);
    gX_I32 x0 = ix % cells, y0 = iy % cells;
    gX_I32 x1 = (ix + 1) % cells, y1 = (iy + 1) % cells;
    gX_F32 h = sinf((gX_F32)x0 * 127.1f + (gX_F32)y0 * 311.7f + seed * 73.7f) * 43758.5453123f;
    gX_F32 a0 = h - floorf(h);
    h = sinf((gX_F32)x1 * 127.1f + (gX_F32)y0 * 311.7f + seed * 73.7f) * 43758.5453123f;
    gX_F32 b0 = h - floorf(h);
    h = sinf((gX_F32)x0 * 127.1f + (gX_F32)y1 * 311.7f + seed * 73.7f) * 43758.5453123f;
    gX_F32 c0 = h - floorf(h);
    h = sinf((gX_F32)x1 * 127.1f + (gX_F32)y1 * 311.7f + seed * 73.7f) * 43758.5453123f;
    gX_F32 d0 = h - floorf(h);
    gX_F32 top = a0 + (b0 - a0) * sx;
    gX_F32 bot = c0 + (d0 - c0) * sx;
    return top + (bot - top) * sy;
}

//opencode: generate tileable 4-octave noise texture packed into RGBA.
//   Octave cells: 2,4,8,16 per axis — all wrap seamlessly (corner hashes
//   modded by cell count), so the texture tiles without seams. Seed shifts
//   the hash field so terrain.seed actually changes the terrain.
//   Shader side: UV scale 0.01 = freqScale 0.001 → base cell ≈ 50 world units.
static gX_I0 gX_RendererRaymarch_Noise_Generate(gX_RendererRaymarch *rm) {
    if (rm->texture.noiseTex.id > 0) {
        gX_Texture_Unload(rm->texture.noiseTex);
        rm->texture.noiseTex.id = 0;
    }
    gX_U32 ns = 256;
    gX_U8 *pixels = (gX_U8*)malloc(ns * ns * 4);
    gX_F32 seedF = (gX_F32)rm->terrain.seed;
    for (gX_U32 y = 0; y < ns; y++) {
        for (gX_U32 x = 0; x < ns; x++) {
            gX_F32 f = 2.0f; //opencode: cells per axis — starts at 2 so every octave tiles
            for (gX_I32 o = 0; o < 4; o++) {
                gX_F32 v = gX_RendererRaymarch_Noise_Octave(x, y, ns,
                    (gX_I32)f, seedF);
                gX_U32 ci = o < 3 ? o : 3;
                pixels[(y * ns + x) * 4 + ci] = (gX_U8)(v * 255.0f);
                f *= 2.0f;
            }
        }
    }
    rm->texture.noiseTex = gX_Texture_LoadFromData(ns, ns, pixels);
    gX_Texture_SetFilter(rm->texture.noiseTex, gX_TEXTURE_FILTER_BILINEAR);
    rm->texture.noiseSeed = rm->terrain.seed;
    free(pixels);
}

gX_I0 gX_RendererRaymarch_Load(gX_RendererRaymarch *rm) {
    //ai: old presets and fresh scenes keep independent-object rendering until a graph root is selected.
    rm->graph = (gX_Raymarch_Graph){.root=-1};
    
    strcpy(rm->name,"raymarch");
    strcpy(rm->description,"raymarching");
    
    //set if color has 3 components
    rm->state.colorIsRGB = false;
    
    rm->parameters.bounces = GX_RM_DEFAULT_BOUNCES;
    rm->parameters.steps = GX_RM_DEFAULT_STEPS;
    rm->parameters.stepsPerBounce = 0;
    rm->parameters.near = GX_RM_DEFAULT_NEAR;
    rm->parameters.far = GX_RM_DEFAULT_FAR;
    rm->parameters.stepMax = GX_RM_DEFAULT_STEP_MAX;
    rm->parameters.distTotal = 0.0f;
    rm->parameters.epsilon = GX_RM_DEFAULT_EPSILON;
    rm->parameters.correctionPolynomial[0] = 1.0f;
    rm->parameters.correctionPolynomial[1] = 0.0f;
    rm->parameters.correctionPolynomial[2] = 0.0f;
    rm->parameters.correctionPolynomial[3] = 0.0f;
    rm->parameters.correctionPolynomial[4] = 0.01f;
    rm->parameters.correctionPolynomial[7] = 0.2f;
    
    rm->fog.near = 40.0f;
    rm->fog.far = 120.0f;
    rm->fog.color = (gX_Color){128, 140, 160, 255};
    
    //ai: volumetric clouds default off (opt-in); values read well when on.
    rm->clouds.bottom = 12.0f;
    rm->clouds.top = 42.0f;
    rm->clouds.coverage = 0.50f;
    rm->clouds.density = 1.2f;
    rm->clouds.scale = 0.06f;
    rm->clouds.speed = 0.02f;
    rm->clouds.absorb = 0.7f;
    rm->clouds.powder = 0.35f;
    rm->clouds.color = gX_WHITE;
    
    //ai: volumetric fog defaults (opt-in): a low haze band with forward
    //   scattering; visible from the default eye-level camera.
    rm->volFog.density = 0.010f;
    rm->volFog.heightFalloff = 1.0f;
    rm->volFog.scattering = 1.0f;
    rm->volFog.hgAsymmetry = 0.55f;
    rm->volFog.color = (gX_Color){255, 240, 220, 255};
    
    rm->sun.brightness = 0.8f;
    rm->sun.size = 1.0f;
    rm->sun.dir = (gX_F32v3){0.3f,1.0f,0.5f};
    rm->sun.color = (gX_Color){255, 240, 220, 255};
    
    rm->terrain.offset = (gX_F32v3){0.0f,-3.0f,0.0f};
    rm->terrain.scale = gX_F32v3_One();
    rm->terrain.rotate = gX_F32Qua_Identity();
    rm->terrain.seed = 0;
    rm->terrain.color = (gX_Color){63,127,31,255};
    rm->terrain.colorSteep = (gX_Color){127,127,80,255};
    rm->terrain.colorBeach = (gX_Color){200,200,42,255};
    //opencode: terrain noise defaults (amplitude, freqScale, octaves)
    //ai: w is tex2 surface-detail strength; old presets retaining zero remain untextured.
    rm->terrain.parameters = (gX_F32v4){8.0f, 0.001f, 3.0f, 0.45f};
    
    rm->water.offset = (gX_F32v3){0.0f,-3.5f,0.0f};
    rm->water.scale = gX_F32v3_One();
    rm->water.rotate = gX_F32Qua_Identity();
    //ai: playing the clock animates the existing wave-speed control.
    rm->water.speed = (gX_F32v3){0.4f,0.0f,0.0f};
    rm->water.seed = 0;
    rm->water.color = (gX_Color){40,127,255,255};
    rm->water.ripple = (gX_F32v4){0.1f, 6.0f, 3.0f, 3.0f};
    rm->water.whirlpool = (gX_F32v4){0.0f, 0.0f, 0.0f, 0.0f};
    rm->water.gradClamp = GX_RM_WATER_GRADCLAMP_DEFAULT;
    
    //opencode: one hero object + one light by default — the feature set reads
    //   and two extra SDF evals at renderScale 0.35 stay cheap.
    gX_RendererRaymarch_Object_Init(rm, 0);
    rm->objects.type[0] = 2; // torus
    rm->objects.pos[0] = (gX_F32v3){ 2.0f, 0.0f, 8.0f };
    rm->objects.parameters[0] = (gX_F32v4){ 1.2f, 0.4f, 0.0f, 0.0f };
    rm->objects.color[0] = (gX_F32v4){ 0.85f, 0.90f, 1.0f, 1.0f };
    rm->objects.material[0] = (gX_F32v4){ 0.2f, 0.0f, 0.0f, 0.0f };
    //ai: readable ambient stone albedo rather than a near-black torus silhouette.
    rm->objects.lighting[0] = (gX_F32v4){0.45f,1.0f,0.2f,32.0f};
    //ai: show the shipped displacement texture as a neutral stone albedo by default.
    rm->objects.texturing[0] = (gX_F32v4){2.0f,1.0f,0.75f,0.15f};
    rm->objects.amount = 1;
    gX_RendererRaymarch_Light_Init(rm, 0);
    rm->lights.brightness[0] = 1.5f;
    rm->lights.pos[0] = (gX_F32v3){ 6.0f, 5.0f, 14.0f };
    rm->lights.color[0] = (gX_F32v4){ 0.6f, 0.8f, 1.0f, 1.0f };
    rm->lights.amount = 1;
    
    rm->flags.enableSunShadows = true;
    
    rm->flags.enableObjects = true;
    rm->flags.enableObjectGlow = true;
    
    rm->flags.enableLights = true;
    rm->flags.enableLightShadows = false;
    rm->flags.enableLightGlow = true;
    rm->flags.enableLightIllumination = true;
    
    rm->flags.enableFrag = true;
    
    rm->flags.enableFog = true;
    rm->flags.enableTerrain = true;
    rm->flags.enableTerrainGrid = false;
    rm->flags.enableWater = true;
    
    rm->flags.enableSampleSign = true;
    rm->flags.enableSampleLastStep = false;
    rm->flags.enableSky = false;
    rm->flags.enableAO = true;
    rm->flags.enableProceduralSky = true;
    
    rm->misc.lightAmbient = (gX_Color){200, 210, 230, 255};
    //opencode: gentle falloff — pure quadratic made the default light invisible
    //   beyond a few units (and blew out up close); the clamp in the shader
    //   caps the near-field.
    rm->misc.attenuation = (gX_F32v3){0.5f,0.5f,0.05f};
    rm->misc.firstColor = gX_BLACK;
    rm->misc.nullColor = gX_BLACK;
    
    rm->shader.locationsLoaded = false;
    rm->shader.shader.loadingState.load = true;
    rm->shader.shader.loadingState.loadAsap = true;
    rm->shader.shader.useDefaultVert = false;
    strcpy(rm->shader.shader.pathVert, GX_PATH("shader", "raymarch.vert"));
    strcpy(rm->shader.shader.pathFrag, GX_PATH("shader", "raymarch_new.frag"));
    rm->shader.shader.version = 330;
    rm->timersInited = false;
    snprintf(rm->shader.shader.defines, sizeof(rm->shader.shader.defines),
        "#define CF_FBMNOISE 0\n#define CF_WATER 1\n#define CF_OBJECTS 1\n"
        "#define CF_LIGHTS 1\n#define CF_FOG 1\n#define CF_GLOW 1\n"
        "#define CF_AO 1\n#define CF_SKYBOX 0\n#define CF_GRID 0\n"
        "#define CF_DEBUGVIS 0\n#define CF_SDF_REPEAT 0\n#define CF_SDF_SMOOTH 0\n"
        "#define CF_HSV 0\n#define CF_VEGETATION 1\n#define CF_FBMNOISE3D 0\n"
        "#define CF_NOISE_TEX 1\n#define CF_SKY_PROCEDURAL 1\n"
        "#define CF_CLOUDS 1\n#define CF_VOLFOG 1");
    //opencode: headless defines override (gate): shader feature permutations.
    const char *definesOverride = getenv("GX_GFX_RAYMARCH_DEFINES");
    if (definesOverride && definesOverride[0])
        snprintf(rm->shader.shader.defines, sizeof(rm->shader.shader.defines),
            "%s", definesOverride);
    //ai: the graph feature is internally owned, including explicit feature-matrix overrides.
    if (!strstr(rm->shader.shader.defines,"#define CF_CSG_GRAPH "))
    {
        gX_U32 length = (gX_U32)strlen(rm->shader.shader.defines);
        snprintf(rm->shader.shader.defines+length,sizeof(rm->shader.shader.defines)-length,
            "\n#define CF_CSG_GRAPH 0\n");
    }
    //ai: hybrid masking is internally owned too — the standalone/composite
    //   permutation must not carry its discard statements.
    if (!strstr(rm->shader.shader.defines,"#define CF_HYBRID_MASK "))
    {
        gX_U32 length = (gX_U32)strlen(rm->shader.shader.defines);
        snprintf(rm->shader.shader.defines+length,sizeof(rm->shader.shader.defines)-length,
            "\n#define CF_HYBRID_MASK 0\n");
    }

    //opencode: generate tileable seed-aware noise texture (4 octaves in RGBA)
    gX_RendererRaymarch_Noise_Generate(rm);

    //ai: texture slots are live authoring resources; tex2 also supplies terrain surface detail.
    rm->texture.skybox.loadingState.load = true;
    rm->texture.skybox.loadingState.loadAsap = true;
    rm->texture.skybox.filterChanged = true;
    rm->texture.skybox.filter = gX_TEXTURE_FILTER_POINT;
    strcpy(rm->texture.skybox.path,GX_PATH("texture", "skybox/skybox1.png"));
    rm->texture.skybox.isSkybox = true;
    rm->texture.skybox.skyboxLayout = gX_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE;
    
    rm->texture.textures[0].loadingState.load = true;
    rm->texture.textures[0].loadingState.loadAsap = true;
    rm->texture.textures[0].filterChanged = true;
    rm->texture.textures[0].filter = gX_TEXTURE_FILTER_POINT;
    strcpy(rm->texture.textures[0].path,GX_PATH("texture", "texture.png"));
    
    rm->texture.textures[1].loadingState.load = true;
    rm->texture.textures[1].loadingState.loadAsap = true;
    rm->texture.textures[1].filterChanged = true;
    rm->texture.textures[1].filter = gX_TEXTURE_FILTER_POINT;
    strcpy(rm->texture.textures[1].path,GX_PATH("texture", "texture_normal.png"));
    
    rm->texture.textures[2].loadingState.load = true;
    rm->texture.textures[2].loadingState.loadAsap = true;
    rm->texture.textures[2].filterChanged = true;
    rm->texture.textures[2].filter = gX_TEXTURE_FILTER_BILINEAR;
    strcpy(rm->texture.textures[2].path,GX_PATH("texture", "texture_displace.png"));
    
    rm->parameters.renderScale = 0.35f; //opencode: third-res by default — ~8x fewer pixels for the heavy shader (raise toward 1.0 for quality)
}

//ai: texture slots for the panel/browser/preset wiring (see raymarch.h).
static gX_Texture_Wrapper *rm_texture_slot(gX_RendererRaymarch *rm, gX_I32 slot) {
    if (!rm) return NULL;
    if (slot < 0) return &rm->texture.skybox;
    if (slot < GX_RM_MAX_TEXTURES) return &rm->texture.textures[slot];
    return NULL;
}

gX_I0 gX_RendererRaymarch_Texture_Reload(gX_RendererRaymarch *rm, gX_I32 slot) {
    gX_Texture_Wrapper *wrapper = rm_texture_slot(rm, slot);
    if (!wrapper) return;
    if (wrapper->loadingState.loaded)
        gX_Texture_Wrapper_Unload(wrapper);
    wrapper->loadingState.load = true;
    wrapper->loadingState.loadAsap = true;
    wrapper->filterChanged = true;
}

gX_I0 gX_RendererRaymarch_Texture_SetPath(gX_RendererRaymarch *rm,
    gX_I32 slot, const gX_I8 *path) {
    gX_Texture_Wrapper *wrapper = rm_texture_slot(rm, slot);
    if (!wrapper || !path || !path[0]) return;
    if (strcmp((const char *)wrapper->path, (const char *)path) == 0
        && wrapper->loadingState.loaded) return;
    snprintf((char *)wrapper->path, sizeof(wrapper->path), "%s",
        (const char *)path);
    gX_RendererRaymarch_Texture_Reload(rm, slot);
}

//ai: release all module-owned GPU resources, including timer pairs, before preset reset.
gX_I0 gX_RendererRaymarch_Unload(gX_RendererRaymarch *rm) {
    
    if (rm->shader.shader.shader.id) gX_Shader_Wrapper_Unload(&rm->shader.shader);
    rm->shader.shader.shader = (gX_Shader){0};
    rm->shader.locationsLoaded = false;
    
    if (rm->texture.skybox.loadingState.loaded)
        gX_Texture_Wrapper_Unload(&rm->texture.skybox);
    for (gX_U32 i = 0; i < GX_RM_MAX_TEXTURES; i++) {
        if (rm->texture.textures[i].loadingState.loaded)
            gX_Texture_Wrapper_Unload(&rm->texture.textures[i]);
    }
    if (rm->texture.noiseTex.id > 0) {
        gX_Texture_Unload(rm->texture.noiseTex);
        rm->texture.noiseTex.id = 0;
    }
    //opencode: unload cached render target
    if (rm->cachedRT.rt.id) {
        gX_RenderTexture_Unload(rm->cachedRT.rt);
        rm->cachedRT.rt.id = 0;
        rm->cachedRT.width = 0;
        rm->cachedRT.height = 0;
    }
    if (rm->timerDraw.queryIds[0] || rm->timerDraw.queryIds[1])
        gX_RHI_Query_Timer_Deinit(&rm->timerDraw);
    if (rm->timerTotal.queryIds[0] || rm->timerTotal.queryIds[1])
        gX_RHI_Query_Timer_Deinit(&rm->timerTotal);
    rm->timerDraw = (gX_RHI_Query_Timer){0};
    rm->timerTotal = (gX_RHI_Query_Timer){0};
    rm->timersInited = false;
}

#define GX_RM_SET_LOC(type,locname,key,value) \
    gX_Shader_SetValue_##type(rm->shader.shader.shader, rm->shader.locations.locname, rm->key.value); \
    
    
#define GX_RM_SET_LOCV(type,locname,key,value) \
    gX_Shader_SetValue_##type##Array(rm->shader.shader.shader, \
                                     rm->shader.locations.locname, &rm->key.value[0], (gX_U32)rm->key.amount); \
                                     
#define GX_RM_SET_LOC_COLOR(locname,key,value) \
    if (rm->state.colorIsRGB) { \
        gX_Shader_SetValue_ColorRGB(rm->shader.shader.shader, rm->shader.locations.locname, rm->key.value); \
    } \
    else { \
        gX_Shader_SetValue_Color(rm->shader.shader.shader, rm->shader.locations.locname, rm->key.value); \
    } \
    
#define GX_RM_SET_LOC_COLORV(locname,key,value) \
    if (rm->state.colorIsRGB) { \
        gX_Shader_SetValue_V3Array(rm->shader.shader.shader, \
                                  rm->shader.locations.locname, (gX_F32v3*)&rm->key.value[0], (gX_U32)rm->key.amount); \
    } \
    else { \
        gX_Shader_SetValue_V4Array(rm->shader.shader.shader, \
                                  rm->shader.locations.locname, (gX_F32v4*)&rm->key.value[0], (gX_U32)rm->key.amount); \
    } \
                                     
//opencode: accept camera+time params (was reading globals that may not be bound)
//ai: reset hybrid state here so cubemap-face draws never inherit raster-depth sampling.
static gX_I0 gX_RendererRaymarch_Shader_SetLocs(gX_RendererRaymarch *rm, gX_Camera3D cam, gX_F32 time) {
    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(rm->shader.shader.shader.id,"hybridDepthEnabled"),0);
    //ai: cubemap/standalone callers must never inherit shared TAA camera jitter.
    gX_Shader_SetValue_V2(rm->shader.shader.shader,
        gX_RHI_GetUniformLocation(rm->shader.shader.shader.id,"hybridJitter"),(gX_F32v2){0});
    gX_Shader_SetValue_Float(rm->shader.shader.shader, rm->shader.shader.locs.locTime, time);
    gX_Shader_SetValue_V2(rm->shader.shader.shader, rm->shader.shader.locs.locRes,
                          (gX_F32v2){maindata.renderWidth,maindata.renderHeight});
    
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.shader.locs.locCamPos,cam.position);
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.shader.locs.locCamTarget,cam.target);
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.shader.locs.locCamUp,cam.up);
    gX_Shader_SetValue_Float(rm->shader.shader.shader, rm->shader.shader.locs.locCamFov,cam.fovy);
    
    GX_RM_SET_LOC(Int,RMbounces,parameters,bounces)
    GX_RM_SET_LOC(Int,RMsteps,parameters,steps)
    //opencode
    GX_RM_SET_LOC(Int,RMstepsPerBounce,parameters,stepsPerBounce)
    GX_RM_SET_LOC(Float,RMnear,parameters,near)
    GX_RM_SET_LOC(Float,RMfar,parameters,far)
    //opencode
    GX_RM_SET_LOC(Float,RMstepMax,parameters,stepMax)
    GX_RM_SET_LOC(Float,RMdistTotal,parameters,distTotal)
    GX_RM_SET_LOC(Float,RMepsilon,parameters,epsilon)
    gX_Shader_SetValue_FloatArray(rm->shader.shader.shader,
                                  rm->shader.locations.RMpoly, &rm->parameters.correctionPolynomial[0], GX_RM_POLYNOMIAL_SIZE);
    
    GX_RM_SET_LOC(Float,fogNear,fog,near)
    GX_RM_SET_LOC(Float,fogFar,fog,far)
    GX_RM_SET_LOC_COLOR(fogColor,fog,color)
    
    gX_Shader_SetValue_Float(rm->shader.shader.shader, rm->shader.locations.sunBright, rm->sun.brightness);
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.locations.sunPos, rm->sun.dir);
    GX_RM_SET_LOC_COLOR(sunColor,sun,color)
    GX_RM_SET_LOC(Float,sunSize,sun,size) //opencode
    
    GX_RM_SET_LOC(Int,terrainSeed,terrain,seed)
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.locations.terrainOffset, rm->terrain.offset);
    GX_RM_SET_LOC(V3,terrainScale,terrain,scale)
    GX_RM_SET_LOC_COLOR(terrainColor,terrain,color)
    GX_RM_SET_LOC_COLOR(terrainColorSteep,terrain,colorSteep)
    GX_RM_SET_LOC_COLOR(terrainColorBeach,terrain,colorBeach)
    //opencode: terrain noise params
    gX_Shader_SetValue_V4(rm->shader.shader.shader, rm->shader.locations.terrainParameters, rm->terrain.parameters);
    gX_Shader_SetValue_V4(rm->shader.shader.shader, rm->shader.locations.terrainRotate, rm->terrain.rotate); //opencode
    
    gX_Shader_SetValue_V3(rm->shader.shader.shader, rm->shader.locations.waterOffset, rm->water.offset);
    GX_RM_SET_LOC_COLOR(waterColor,water,color)
    gX_Shader_SetValue_V4(rm->shader.shader.shader, rm->shader.locations.waterRipple, rm->water.ripple);
    gX_Shader_SetValue_V4(rm->shader.shader.shader, rm->shader.locations.waterWhirlpool, rm->water.whirlpool);
    //opencode: water animation uniforms (was dead — now active)
    GX_RM_SET_LOC(Float,waterSpeed,water,speed.x)
    GX_RM_SET_LOC(Int,waterSeed,water,seed)
    GX_RM_SET_LOC(Float,gradClamp,water,gradClamp)
    
    gX_Shader_SetValue_Int(rm->shader.shader.shader, rm->shader.locations.objectAmount, rm->objects.amount);
    GX_RM_SET_LOCV(Int,objectType,objects,type)
    GX_RM_SET_LOCV(V3,objectPos,objects,pos)
    GX_RM_SET_LOCV(V3,objectMatX,objects,matX)
    GX_RM_SET_LOCV(V3,objectMatY,objects,matY)
    GX_RM_SET_LOCV(V3,objectMatZ,objects,matZ)
    GX_RM_SET_LOC_COLORV(objectColor,objects,color)
    GX_RM_SET_LOCV(V4,objectAttribute,objects,parameters)
    GX_RM_SET_LOCV(V4,objectMaterial,objects,material)
    gX_Shader_SetValue_Array(rm->shader.shader.shader,
        gX_RHI_GetUniformLocation(rm->shader.shader.shader.id,"objectRoughness"),
        rm->objects.roughness,gX_SHADER_UNIFORM_FLOAT,(gX_U32)rm->objects.amount);
    GX_RM_SET_LOCV(V4,objectTexturing,objects,texturing) //opencode: per-object texture mapping
    GX_RM_SET_LOCV(V4,objectLighting,objects,lighting)
    //ai: upload only the validated reachable program; invalid edits fall back to the legacy union.
    gX_RendererRaymarch_Graph_Compile(rm);
    gX_Shader_SetValue_Int(rm->shader.shader.shader,rm->shader.locations.graphCount,rm->graph.count);
    if (rm->graph.count)
    {
        gX_Shader_SetValue_V4Array(rm->shader.shader.shader,rm->shader.locations.graphNodes,rm->graph.program,rm->graph.count);
        gX_Shader_SetValue_V4Array(rm->shader.shader.shader,rm->shader.locations.graphDomains,rm->graph.programDomains,rm->graph.count);
    }
    
    gX_Shader_SetValue_Int(rm->shader.shader.shader, rm->shader.locations.lightAmount, rm->lights.amount);
    GX_RM_SET_LOCV(Float,lightBrightness,lights,brightness)
    GX_RM_SET_LOCV(V3,lightPos,lights,pos)
    GX_RM_SET_LOC_COLORV(lightColor,lights,color)
    
    gX_Shader_SetValue_Int(rm->shader.shader.shader, rm->shader.locations.enableShadows, rm->flags.enableSunShadows);
    GX_RM_SET_LOC(Int,enableGlow,flags,enableObjectGlow)
    GX_RM_SET_LOC(Int,enableLightGlow,flags,enableLightGlow)
    GX_RM_SET_LOC(Int,enableIllu,flags,enableLightIllumination)
    GX_RM_SET_LOC(Int,enableFrag,flags,enableFrag)
    GX_RM_SET_LOC(Int,enableFog,flags,enableFog)
    GX_RM_SET_LOC(Int,enableTerrain,flags,enableTerrain)
    GX_RM_SET_LOC(Int,enableWater,flags,enableWater)
    GX_RM_SET_LOC(Int,enableObjects,flags,enableObjects)
    GX_RM_SET_LOC(Int,enableLights,flags,enableLights)
    GX_RM_SET_LOC(Int,enableGrid,flags,enableTerrainGrid)
    GX_RM_SET_LOC(Int,enableLSS,flags,enableSampleLastStep)
    GX_RM_SET_LOC(Int,enableSS,flags,enableSampleSign)
    GX_RM_SET_LOC(Int,enableSky,flags,enableSky)
    //opencode
    GX_RM_SET_LOC(Int,enableAO,flags,enableAO)
    //opencode: separate point light shadows
    GX_RM_SET_LOC(Int,enableLightShadows,flags,enableLightShadows)
    //opencode: procedural sky
    GX_RM_SET_LOC(Int,enableProceduralSky,flags,enableProceduralSky)
    //ai: volumetric clouds
    GX_RM_SET_LOC(Int,enableClouds,flags,enableClouds)
    GX_RM_SET_LOC(Float,cloudBottom,clouds,bottom)
    GX_RM_SET_LOC(Float,cloudTop,clouds,top)
    GX_RM_SET_LOC(Float,cloudCoverage,clouds,coverage)
    GX_RM_SET_LOC(Float,cloudDensity,clouds,density)
    GX_RM_SET_LOC(Float,cloudScale,clouds,scale)
    GX_RM_SET_LOC(Float,cloudSpeed,clouds,speed)
    GX_RM_SET_LOC(Float,cloudAbsorb,clouds,absorb)
    GX_RM_SET_LOC(Float,cloudPowder,clouds,powder)
    GX_RM_SET_LOC_COLOR(cloudColor,clouds,color)
    //ai: volumetric fog
    GX_RM_SET_LOC(Int,enableVolFog,flags,enableVolumetricFog)
    GX_RM_SET_LOC(Float,volFogDensity,volFog,density)
    GX_RM_SET_LOC(Float,volFogHeightFalloff,volFog,heightFalloff)
    GX_RM_SET_LOC(Float,volFogScattering,volFog,scattering)
    GX_RM_SET_LOC(Float,volFogHG,volFog,hgAsymmetry)
    GX_RM_SET_LOC_COLOR(volFogColor,volFog,color)
    
    GX_RM_SET_LOC_COLOR(lightAmbient,misc,lightAmbient)
    GX_RM_SET_LOC_COLOR(firstColor,misc,firstColor)
    GX_RM_SET_LOC_COLOR(nullColor,misc,nullColor)
    GX_RM_SET_LOC(V3,lightAttenuation,misc,attenuation)
    
    //opencode: debug visualizer flags
    GX_RM_SET_LOC(Int,debugShowPos,         debug,showPos)
    GX_RM_SET_LOC(Int,debugShowDir,         debug,showDir)
    GX_RM_SET_LOC(Int,debugShowRay,         debug,showRay)
    GX_RM_SET_LOC(Int,debugShowGradient,    debug,showGradient)
    GX_RM_SET_LOC(Int,debugShowNormal,      debug,showNormal)
    GX_RM_SET_LOC(Int,debugShowBounceCount, debug,showBounceCount)
    GX_RM_SET_LOC(Int,debugShowPathSteps,   debug,showPathSteps)
    GX_RM_SET_LOC(Int,debugShowPathLength,  debug,showPathLength)
    GX_RM_SET_LOC(Int,debugShowDist,        debug,showDist)
}

//ai: compile presentation owns a small private feedback field, never the scene's history.
typedef struct rmLoading
{
    gX_I8 name[128];
    gX_I8 description[256];
    gX_RenderTexture gas[2];
    gX_Shader effects[3];
    gX_I32 locations[3][3];
    gX_F64 start;
    gX_U32 frames;
} rmLoading;

//ai: existing PP shaders provide diffusion, translation/expansion, and hue drift.
static gX_I0 rm_loading_pass(rmLoading *loading, gX_I32 effect, gX_F32 t)
{
    gX_Shader shader = loading->effects[effect];
    gX_F32v4 uniforms[16] = {0};
    if (effect == 0)
    {
        uniforms[0] = (gX_F32v4){0.995f, 0.995f, 0, 0};
        uniforms[1] = (gX_F32v4){0.001f * sinf(t), -0.0015f, 0, 0};
    }
    if (effect == 1) uniforms[0] = (gX_F32v4){0, 1.5f, 0, 0};
    if (effect == 2) uniforms[0] = (gX_F32v4){0.001f, 0, -0.007f, 0};
    gX_Drawing_BeginTexture(loading->gas[1]);
    gX_Drawing_ClearBG(gX_BLACK);
    gX_Drawing_BeginShader(shader);
    gX_Shader_SetValue_V2(shader, loading->locations[effect][0],
        (gX_F32v2){256,256});
    gX_Shader_SetValue_Array(shader, loading->locations[effect][1],
        uniforms, gX_SHADER_UNIFORM_VEC4, 2);
    gX_Shader_SetValue_TextureSlot(shader, loading->locations[effect][2],
        loading->gas[0].texture, 0);
    gX_Drawing_DrawScreenQuad(0, 0, 256, 256);
    gX_Drawing_EndShader();
    gX_Drawing_EndTexture();
    gX_RenderTexture swap = loading->gas[0];
    loading->gas[0] = loading->gas[1];
    loading->gas[1] = swap;
}

//ai: invoked only between GL completion polls; all drawing stays on the context thread.
static gX_I1 rm_loading_frame(gX_I0 *userData)
{
    rmLoading *loading = userData;
    if (gX_Window_ShouldClose())
    {
        maindata.loop = false;
        return 0;
    }
    gX_GL_State_Push();
    if (!loading->gas[0].id)
    {
        gX_RHI_Shader_SetLoading(NULL, NULL);
        const gX_I8 *paths[3] = {"resources/shader/postprocess/geometry_scale.frag",
            "resources/shader/postprocess/blur_gauss.frag",
            "resources/shader/postprocess/color_hsv.frag"};
        for (gX_I32 i = 0; i < 3; i++)
        {
            loading->effects[i] = gX_Shader_Load(NULL, paths[i]);
            const gX_I8 *names[3] = {"res", "uniforms", "input"};
            for (gX_I32 j = 0; j < 3; j++)
                loading->locations[i][j] = gX_Shader_GetLocation(loading->effects[i], names[j]);
            //ai: GLSL >=130 renames the reserved `input` sampler at preprocess.
            if (loading->locations[i][2] < 0)
                loading->locations[i][2] =
                    gX_Shader_GetLocation(loading->effects[i], "inputTex");
        }
        for (gX_I32 i = 0; i < 2; i++)
        {
            loading->gas[i] = gX_RenderTexture_Load(256, 256);
            gX_Texture_SetFilter(loading->gas[i].texture, gX_TEXTURE_FILTER_BILINEAR);
            gX_Drawing_BeginTexture(loading->gas[i]);
            gX_Drawing_ClearBG(gX_BLACK);
            gX_Drawing_EndTexture();
        }
        loading->start = gX_Time_Get();
        gX_RHI_Shader_SetLoading(rm_loading_frame, loading);
    }
    gX_F32 t = (gX_F32)(gX_Time_Get() - loading->start);
    gX_RHI_Disable(gX_RHI_CULL_FACE);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    for (gX_I32 effect = 0; effect < 3; effect++) rm_loading_pass(loading, effect, t);
    const gX_Color colors[3] = {{255,45,75,255},{55,255,140,255},{65,120,255,255}};
    gX_Drawing_BeginTexture(loading->gas[0]);
    gX_Drawing_BeginBlend(gX_BLEND_ADDITIVE);
    gX_Batch_SetTexture(gX_Batch_GetDefaultTex());
    for (gX_I32 i = 0; i < 3; i++)
    {
        gX_F32 angle = t * 1.5f + (gX_F32)i * 2.094395f;
        gX_F32v2 pos = {128 + 55 * cosf(angle), 128 + 55 * sinf(angle)};
        for (gX_I32 puff = 5; puff > 0; puff--)
        {
            gX_Color color = colors[i];
            color.a = 14;
            gX_Circle_Draw((gX_Circle){pos, 3.0f * (gX_F32)puff}, color);
        }
    }
    gX_Drawing_EndBlend();
    gX_Drawing_EndTexture();
    gX_F32 size = (gX_F32)maindata.screenHeight * 0.65f;
    gX_F32v2 center = {(gX_F32)maindata.screenWidth * 0.5f,
        (gX_F32)maindata.screenHeight * 0.44f};
    gX_Drawing_Begin();
    gX_Drawing_ClearBG((gX_Color){5,7,14,255});
    gX_Drawing_BeginBlend(gX_BLEND_ADDITIVE);
    gX_Texture_Draw(loading->gas[0].texture, (gX_Texture_Draw_Params){
        .dst = {center.x-size/2, center.y-size/2, size, size},
        .tint = gX_WHITE, .scale = {1,1}});
    gX_Drawing_EndBlend();
    gX_Batch_SetTexture(gX_Batch_GetDefaultTex());
    for (gX_I32 i = 0; i < 3; i++)
    {
        gX_F32 angle = t * 1.5f + (gX_F32)i * 2.094395f;
        gX_F32v2 pos = {center.x + size * 55.0f/256.0f * cosf(angle),
            center.y + size * 55.0f/256.0f * sinf(angle)};
        gX_Circle_Draw((gX_Circle){pos, size * 0.022f}, colors[i]);
        gX_Circle_Draw((gX_Circle){pos, size * 0.009f}, gX_WHITE);
    }
    const gX_I8 *labels[4] = {"loading", "loading.", "loading..", "loading..."};
    gX_RawString_Draw_Lesser(labels[(gX_I32)(t * 3.0f) % 4],
        (gX_F32v2){center.x-55, center.y+size*0.33f}, 24, (gX_Color){215,225,245,255});
    gX_Drawing_End();
    loading->frames++;
    const char *capture = getenv("GX_RAYMARCH_LOADING_CAPTURE");
    if (capture && loading->frames <= 90 && loading->frames % 5 == 0)
    {
        gX_I8 path[512];
        snprintf(path, sizeof(path), "%s/loading_%03u.png", capture, loading->frames);
        gX_TakeScreenShot(path);
    }
    gX_GL_State_Pop();
    return 1;
}

//opencode: check useInternalTime/Camera flags; GetLocations only once; SetLocs moved to Draw
gX_I0 gX_RendererRaymarch_Update(gX_RendererRaymarch *rm) {
    //ai: graph activation changes one shader permutation, not every preset's compile cost.
    gX_RendererRaymarch_Graph_Compile(rm);
    gX_RendererRaymarch_Defines_Set(rm,"CF_CSG_GRAPH",rm->graph.count > 0);
    //ai: the hybrid masking block (discards, shared-depth writes) exists only
    //   while a hybrid depth mode owns the frame; the composite/standalone
    //   permutation stays discard-free (a discard-bearing program loses the
    //   compositor layer draw).
    gX_RendererRaymarch_Defines_Set(rm,"CF_HYBRID_MASK",
        renderer.state.raymarchDepth != 0);
    //ai: both initial compilation and watched reloads present the same private loader.
    rmLoading loading = {0};
    gX_U32 oldProgram = rm->shader.shader.shader.id;
    gX_RHI_Shader_SetLoading(rm_loading_frame, &loading);
    gX_Shader_Wrapper_Update(&rm->shader.shader);
    gX_RHI_Shader_SetLoading(NULL, NULL);
    if (oldProgram != rm->shader.shader.shader.id) rm->shader.locationsLoaded = false;
    if (loading.frames)
    {
        for (gX_I32 i = 0; i < 3; i++) gX_Shader_Unload(loading.effects[i]);
        for (gX_I32 i = 0; i < 2; i++) gX_RenderTexture_Unload(loading.gas[i]);
        gX_TraceLog(gX_LOG_NOTICE, "RAYMARCH_LOADING: frames=%u", loading.frames);
    }
    
    gX_Texture_Wrapper_Update(&rm->texture.skybox);
    for (gX_U32 i = 0; i < GX_RM_MAX_TEXTURES; i++) {
        gX_Texture_Wrapper_Update(&rm->texture.textures[i]);
    }
    
    if (rm->shader.shader.loadingState.loaded && !rm->shader.locationsLoaded) {
        gX_RendererRaymarch_Shader_GetLocations(&rm->shader);
        rm->shader.locationsLoaded = true;
    }

    //ai: exercise real editor-style preset resets with live shader/cache/query resources.
    {
        static gX_I32 presetIndex = 0;
        static gX_U64 nextPresetFrame = 20;
        if (getenv("GX_RAYMARCH_PRESET_CYCLE") && presetIndex < 9
            && maindata.frameCount >= nextPresetFrame
            && rm->shader.shader.loadingState.loaded)
        {
            gX_RendererRaymarch_Preset_BuiltIn(rm, presetIndex);
            gX_TraceLog(gX_LOG_NOTICE, "RAYMARCH_PRESET_CYCLE: preset=%d", presetIndex++);
            nextPresetFrame = maindata.frameCount + 16;
        }
    }
    
    //ai: headless quality hook (benchmark): pin the module render scale so
    //   cost rows measure resolution scaling without editing a preset.
    if (!s_headlessScaleDone) {
        const char *scaleHook = getenv("GX_GFX_RAYMARCH_RENDERSCALE");
        if (scaleHook && scaleHook[0]) {
            gX_F32 scale = (gX_F32)atof(scaleHook);
            if (scale < 0.1f) scale = 0.1f;
            if (scale > 1.0f) scale = 1.0f;
            rm->parameters.renderScale = scale;
        }
        s_headlessScaleDone = 1;
    }

    //opencode: headless preset round-trip hook (gate): save the live scene,
    //   mutate fields, reload from disk, and trace the restored values.
    {
        const char *presetCheck = getenv("GX_GFX_RAYMARCH_CHECK");
        if (presetCheck && presetCheck[0] && !s_headlessPresetCheckDone
            && rm->shader.shader.loadingState.loaded) {
            gX_I8 path[512];
            snprintf((char *)path, sizeof(path),
                "%s/raymarch_roundtrip.json", presetCheck);
            gX_I1 savedWater = rm->flags.enableWater;
            //ai: texturing round-trip — mutate tex1's path so the preset must
            //   carry it, then restore the authored path after the reload.
            gX_I8 savedTex1[128];
            snprintf((char *)savedTex1, sizeof(savedTex1), "%s",
                (const char *)rm->texture.textures[1].path);
            snprintf((char *)rm->texture.textures[1].path,
                sizeof(rm->texture.textures[1].path), "%s",
                "resources/texture/texture_normal.png");
            rm->objects.amount = 1;
            rm->objects.type[0] = 3;
            rm->objects.pos[0] = (gX_F32v3){ 1.5f, 0.25f, -2.5f };
            rm->objects.color[0] = (gX_F32v4){ 0.25f, 0.5f, 0.75f, 1.0f };
            rm->lights.amount = 1;
            rm->lights.brightness[0] = 2.5f;
            rm->lights.pos[0] = (gX_F32v3){ -1.0f, 2.0f, 0.5f };
            rm->sun.brightness = 1.234f;
            rm->terrain.seed = 4242;
            rm->flags.enableWater = savedWater ? 0 : 1;
            gX_I32 saveOK = gX_RendererRaymarch_Preset_Save_New(rm, path);
            rm->objects.amount = 0;
            rm->objects.type[0] = 0;
            rm->objects.pos[0] = (gX_F32v3){ 0.0f, 0.0f, 0.0f };
            rm->lights.amount = 0;
            rm->lights.brightness[0] = 0.0f;
            rm->sun.brightness = 9.9f;
            rm->terrain.seed = 1;
            rm->flags.enableWater = savedWater;
            gX_I32 loadOK = gX_RendererRaymarch_Preset_Load_New(rm, path);
            gX_I1 restored = (fabsf(rm->sun.brightness - 1.234f) < 1e-4f
                && rm->terrain.seed == 4242
                && rm->flags.enableWater == (savedWater ? 0 : 1)
                && rm->objects.amount == 1
                && rm->objects.type[0] == 3
                && fabsf(rm->objects.pos[0].x - 1.5f) < 1e-4f
                && fabsf(rm->objects.color[0].z - 0.75f) < 1e-4f
                && rm->lights.amount == 1
                && fabsf(rm->lights.brightness[0] - 2.5f) < 1e-4f
                && fabsf(rm->lights.pos[0].y - 2.0f) < 1e-4f
                && strcmp((const char *)rm->texture.textures[1].path,
                    "resources/texture/texture_normal.png") == 0) ? 1 : 0;
            snprintf((char *)rm->texture.textures[1].path,
                sizeof(rm->texture.textures[1].path), "%s", savedTex1);
            if (rm->texture.textures[1].loadingState.loaded)
                gX_Texture_Wrapper_Unload(&rm->texture.textures[1]);
            gX_I1 pass = (saveOK == 0 && loadOK == 0 && restored) ? 1 : 0;
            gX_TraceLog(pass ? gX_LOG_NOTICE : gX_LOG_ERROR,
                "GFX_RAYMARCH_CHECK: %s save=%d load=%d restored=%d "
                "sun=%.4f seed=%d water=%d objects=%d lights=%d",
                pass ? "PASS" : "FAIL", saveOK, loadOK, restored,
                (gX_F64)rm->sun.brightness, rm->terrain.seed,
                rm->flags.enableWater, rm->objects.amount, rm->lights.amount);
            s_headlessPresetCheckDone = 1;
        }
    }
    
    //opencode: headless primitive-gallery hook (gate): load the "Primitives"
    //   built-in (all SDF types in two rows); state 2 keeps the same scene
    //   with the objects removed as the per-primitive reference frame.
    {
        const char *gallery = getenv("GX_GFX_RAYMARCH_GALLERY");
        if (gallery && gallery[0] && !s_headlessGalleryDone
            && rm->shader.shader.loadingState.loaded) {
            gX_RendererRaymarch_Preset_BuiltIn(rm, 7);
            //ai: state 2 = empty reference; state 3 keeps only the five v0.15
            //   types (row indices 3..7), re-centred so the per-primitive ROIs
            //   stay orientation-agnostic (the camera basis mirrors X).
            if (gallery[0] == '2') {
                rm->objects.amount = 0;
            } else if (gallery[0] == '3') {
                for (gX_I32 i = 0; i < 5; i++) {
                    gX_I32 src = 3 + i;
                    rm->objects.type[i] = rm->objects.type[src];
                    rm->objects.parameters[i] = rm->objects.parameters[src];
                    rm->objects.color[i] = rm->objects.color[src];
                    rm->objects.matX[i] = rm->objects.matX[src];
                    rm->objects.matY[i] = rm->objects.matY[src];
                    rm->objects.matZ[i] = rm->objects.matZ[src];
                    rm->objects.pos[i] = (gX_F32v3){
                        -4.0f + 2.0f * (gX_F32)i, 1.5f, 6.0f
                    };
                }
                rm->objects.amount = 5;
            }
            //ai: analytic solid-surface probes share the gallery's deterministic camera.
            if (gallery[0] >= '4' && gallery[0] <= '9') {
                gX_RendererRaymarch_Object_Init(rm, 0);
                rm->objects.amount = 1;
                rm->objects.type[0] = 0;
                rm->objects.pos[0] = (gX_F32v3){0.0f, 0.0f, 6.0f};
                rm->objects.parameters[0].x = 1.5f;
                rm->objects.material[0] = (gX_F32v4){0};
                rm->objects.color[0] = (gX_F32v4){1,1,1,1};
                rm->lights.amount = 0;
                rm->flags.enableLightIllumination = false;
                rm->flags.enableObjectGlow = false;
                rm->parameters.bounces = 1;
                rm->parameters.renderScale = 1.0f;
                if (gallery[0] == '4') {
                    rm->objects.matX[0].x = 3.0f;
                    rm->objects.matY[0].y = 1.5f;
                }
                if (gallery[0] == '5') {
                    rm->objects.pos[0].z = 0.0f;
                }
                if (gallery[0] == '6') {
                    gX_RendererRaymarch_Object_Init(rm, 1);
                    rm->objects.amount = 2;
                    rm->objects.pos[0].z = 45.0f;
                    rm->objects.type[1] = 0;
                    rm->objects.pos[1] = (gX_F32v3){3.0f,0.0f,2.0f};
                    rm->objects.parameters[1].x = 1.0f;
                    rm->objects.material[1] = (gX_F32v4){0};
                    rm->objects.color[1] = (gX_F32v4){1,1,1,1};
                }
                if (gallery[0] == '7') {
                    rm->parameters.steps = 1;
                    rm->flags.enableSampleLastStep = true;
                }
                //ai: a lit reflective white sphere exposes unstable self-shadow hits.
                if (gallery[0] == '8') {
                    rm->flags.enableLightIllumination = true;
                    rm->objects.material[0].x = 0.5f;
                    const char *jitter = getenv("GX_GFX_RAYMARCH_HIT_JITTER");
                    if (jitter) rm->objects.pos[0].x = strtof(jitter, NULL);
                }
                //ai: exercise authoring commands and slot compaction through the live shader.
                if (gallery[0] == '9') {
                    gX_RendererRaymarch_Quality_Set(rm, 3);
                    gX_RendererRaymarch_Object_Init(rm, 1);
                    rm->objects.amount = 2;
                    gX_RendererRaymarch_Object_Type_Set(rm, 1, 0);
                    rm->objects.parameters[1].x = 1.5f;
                    rm->objects.pos[1] = (gX_F32v3){0,0,6};
                    rm->objects.color[1] = (gX_F32v4){1,1,1,1};
                    rm->objects.material[1] = (gX_F32v4){0};
                    gX_RendererRaymarch_Object_Rotate(rm, 1, (gX_F32v3){0,1.256f*57.29577951f,0});
                    gX_RendererRaymarch_Object_Scale_Set(rm, 1, (gX_F32v3){0.5f,1.5f,2});
                    gX_RendererRaymarch_Object_Rotate(rm, 1, (gX_F32v3){0,0,30});
                    gX_RendererRaymarch_Object_Remove(rm, 0);
                }
            }
            const gX_I8 *mode = (gallery[0] == '2') ? "reference"
                : (gallery[0] == '3') ? "new-types" : "primitives";
            gX_TraceLog(gX_LOG_NOTICE, "GFX_RAYMARCH_GALLERY: %s objects=%d",
                (const char *)mode, rm->objects.amount);
            s_headlessGalleryDone = 1;
        }
    }

    //ai: isolated white CSG/repetition silhouettes are compared with a CPU field oracle.
    {
        static gX_I1 graphDone = 0;
        const gX_I8 *probe = getenv("GX_RAYMARCH_GRAPH_TEST");
        if (probe && !graphDone && rm->shader.shader.loadingState.loaded)
        {
            gX_I32 operation = atoi(probe);
            rm->flags = (gX_Raymarch_Flags){.enableObjects=1,.enableFrag=1,.enableSampleSign=1,.enableProceduralSky=1};
            rm->sun.brightness = 0;
            rm->parameters.steps = 256;
            rm->parameters.bounces = 1;
            rm->parameters.renderScale = 1;
            rm->state.useInternalCamera = 1;
            rm->camera.camera.camera3D = (gX_Camera3D){.position={0},.target={0,0,1},.up={0,1,0},.fovy=90};
            rm->objects.amount = 2;
            for (gX_I32 i = 0; i < 2; i++)
            {
                gX_RendererRaymarch_Object_Init(rm,i);
                rm->objects.type[i] = 0;
                rm->objects.pos[i] = (gX_F32v3){i ? 0.7f : -0.7f,0,6};
                rm->objects.parameters[i].x = 1.2f;
                rm->objects.color[i] = (gX_F32v4){1,1,1,1};
                rm->objects.material[i] = (gX_F32v4){0};
            }
            rm->graph = (gX_Raymarch_Graph){.root=2};
            rm->graph.nodes[0] = (gX_F32v4){1,0,0,0};
            rm->graph.nodes[1] = (gX_F32v4){1,1,0,0};
            rm->graph.nodes[2] = (gX_F32v4){(gX_F32)operation,0,1,0.4f};
            if (operation >= 8)
            {
                rm->objects.pos[0].x = 0;
                rm->objects.parameters[0].x = 0.6f;
                rm->graph.domains[2] = (gX_F32v4){2,0,0,1};
            }
            gX_RendererRaymarch_Graph_Compile(rm);
            gX_TraceLog(gX_LOG_NOTICE,"RAYMARCH_GRAPH: operation=%d nodes=%d",operation,rm->graph.count);
            graphDone = 1;
        }
    }

    //ai: analytic sphere/cube visibility fixture, including an asymmetric perspective view.
    const gX_I8 *depthProbe = getenv("GX_RAYMARCH_DEPTH_TEST");
    if (depthProbe && rm->shader.shader.loadingState.loaded)
    {
        camera.camera3D = (gX_Camera3D){.position={0,0,8},.target={0},.up={0,1,0},.fovy=50};
        camera.frustum.near = 0.1f;
        camera.frustum.far = 100.0f;
        if (getenv("GX_RAYMARCH_DEPTH_CAMERA")) camera.camera3D.position = (gX_F32v3){4,2,8};
        //ai: analytic static-scene camera motion is frame-indexed and independent of wall time.
        const gX_I8 *cameraMotion = getenv("GX_RAYMARCH_CAMERA_MOTION");
        if (cameraMotion)
        {
            gX_F32 phase = (gX_F32)maindata.frameCount * strtof(cameraMotion,NULL);
            camera.camera3D.position = (gX_F32v3){8.0f*sinf(phase),0,8.0f*cosf(phase)};
        }
        rm->flags = (gX_Raymarch_Flags){.enableObjects=1,.enableFrag=1,.enableSampleSign=1};
        rm->sun.brightness = 0;
        rm->parameters.steps = 256;
        rm->parameters.bounces = 1;
        rm->graph.root = -1;
        rm->objects.amount = 1;
        gX_RendererRaymarch_Object_Init(rm,0);
        rm->objects.type[0] = 0;
        rm->objects.parameters[0].x = 1.3f;
        rm->objects.pos[0] = (gX_F32v3){0.7f,0,0};
        if (!strcmp(depthProbe,"front") || !strncmp(depthProbe,"reflect",7)) rm->objects.pos[0].z = 2.5f;
        if (!strcmp(depthProbe,"back")) rm->objects.pos[0].z = -2.5f;
        if (!strcmp(depthProbe,"miss")) rm->objects.pos[0].x = 100;
        rm->objects.color[0] = (gX_F32v4){1,0,1,1};
        rm->objects.material[0] = (gX_F32v4){0};
        const gX_I8 *roughnessProbe = getenv("GX_RAYMARCH_ROUGHNESS");
        if (roughnessProbe) rm->objects.roughness[0] = strtof(roughnessProbe,NULL);
        const gX_I8 *temporalEdit = getenv("GX_RAYMARCH_TEMPORAL_EDIT");
        if (temporalEdit && maindata.frameCount >= 20)
        {
            if (!strcmp(temporalEdit,"geometry")) rm->objects.pos[0].x += 0.2f;
            if (!strcmp(temporalEdit,"material")) rm->objects.color[0] = (gX_F32v4){0,1,0,1};
            if (!strcmp(temporalEdit,"time")) rm->state.time = 2.0f;
            if (!strcmp(temporalEdit,"clock")) rm->state.useInternalTime = true;
            if (!strcmp(temporalEdit,"routing")) renderer.state.raymarchDepth = 1;
        }
    }

    //opencode: headless feature-state hook (gate): force one enable flag so
    //   the feature matrix compares frames per shader feature.
    {
        const char *feature = getenv("GX_GFX_RAYMARCH_FEATURE");
        const char *featureState = getenv("GX_GFX_RAYMARCH_FEATURE_STATE");
        if (feature && feature[0] && featureState && !s_headlessFeatureDone) {
            gX_I1 state = (featureState[0] == '1') ? 1 : 0;
            gX_I1 *flag = gX_RendererRaymarch_Flag_By_Name(&rm->flags,
                feature);
            gX_I1 handled = (flag != NULL);
            if (flag) *flag = state;
            //ai: parameter probes verify the formerly inert baked scale and paused clock.
            if (!strcmp((const char *)feature, "terrainscalex"))
            {
                rm->terrain.scale.x = state ? 4.0f : 1.0f;
                handled = true;
            }
            if (!strcmp((const char *)feature, "terrainscaley"))
            {
                rm->terrain.scale.y = state ? 2.0f : 1.0f;
                handled = true;
            }
            if (!strcmp((const char *)feature, "terrainscalez"))
            {
                rm->terrain.scale.z = state ? 4.0f : 1.0f;
                handled = true;
            }
            if (!strcmp((const char *)feature, "time"))
            {
                rm->flags.enableClouds = true;
                rm->clouds.speed = 0.2f;
                rm->state.useInternalTime = false;
                rm->state.time = state ? 64.0f : 0.0f;
                handled = true;
            }
            //ai: give the marker-only feature probe a resolved footprint at scale 0.35;
            //   keep illumination identical in both on/off captures.
            if (flag && !strcmp((const char *)feature, "lights")
                && rm->lights.amount > 0) rm->lights.brightness[0] = 8.0f;
            //ai: texturing is per-object, not a module flag — the feature hook
            //   maps `objecttexture` to tex0 triplanar vs off on the hero torus.
            if (!flag && !strcmp((const char *)feature, "objecttexture")) {
                handled = 1;
                if (rm->objects.amount < 1) {
                    gX_RendererRaymarch_Object_Init(rm, 0);
                    rm->objects.amount = 1;
                }
                rm->objects.type[0] = 2;
                rm->objects.pos[0] = (gX_F32v3){2.0f, 2.0f, 8.0f};
                rm->objects.parameters[0] = (gX_F32v4){1.8f, 0.6f, 0.0f, 0.0f};
                //diffuse-biased fixture so the albedo (texture) dominates.
                rm->objects.color[0] = (gX_F32v4){1.0f, 1.0f, 1.0f, 1.0f};
                rm->objects.material[0] = (gX_F32v4){0.0f, 0.0f, 1.0f, 0.0f};
                rm->objects.lighting[0] = (gX_F32v4){0.45f, 1.0f, 0.15f, 16.0f};
                //w blends toward the base colour: 0 = full texture.
                rm->objects.texturing[0] = state
                    ? (gX_F32v4){0.0f, 1.0f, 6.0f, 0.0f}
                    : (gX_F32v4){0.0f, 0.0f, 1.0f, 0.0f};
            }
            //ai: isolated water probes distinguish reflected geometry and wave motion from flat color.
            if (!strcmp((const char *)feature, "waterreflection")
                || !strcmp((const char *)feature, "waterwaves")) {
                handled = true;
                rm->flags.enableWater = true;
                rm->flags.enableTerrain = false;
                rm->flags.enableFog = false;
                rm->flags.enableAO = false;
                rm->flags.enableLightIllumination = false;
                rm->flags.enableObjectGlow = false;
                rm->flags.enableLights = false;
                rm->water.offset = (gX_F32v3){0};
                rm->water.color = (gX_Color){25,90,120,255};
                rm->water.ripple.x = !strcmp((const char *)feature,"waterwaves") ? 0.18f : 0.0f;
                rm->state.useInternalTime = false;
                rm->state.time = state ? 3.0f : 0.0f;
                rm->parameters.steps = 256;
                rm->parameters.bounces = !strcmp((const char *)feature,"waterreflection") && !state ? 1 : 3;
                rm->state.useInternalCamera = true;
                rm->camera.camera.camera3D = (gX_Camera3D){
                    .position={0,2,-6}, .target={0,0,6}, .up={0,1,0}, .fovy=60};
                gX_RendererRaymarch_Object_Init(rm, 0);
                rm->objects.amount = 1;
                rm->objects.type[0] = 1;
                rm->objects.parameters[0] = (gX_F32v4){1.5f,1.5f,1.5f,0};
                rm->objects.pos[0] = (gX_F32v3){0,2,6};
                rm->objects.color[0] = (gX_F32v4){1,0.02f,0.01f,1};
                rm->objects.material[0] = (gX_F32v4){0};
            }
            //ai: test surface detail independently of geometry, lights and object mapping.
            if (!strcmp((const char *)feature, "terraintexture")) {
                handled = true;
                rm->terrain.parameters.w = state ? 1.0f : 0.0f;
                rm->flags.enableWater = false;
            }
            //opencode: fog is invisible with the default 40..120 range from
            //   the default camera; give the check a range that shows it.
            if (flag && state && !strcmp((const char *)feature, "fog")) {
                rm->fog.near = 5.0f;
                rm->fog.far = 60.0f;
            }
            gX_TraceLog(handled ? gX_LOG_NOTICE : gX_LOG_ERROR,
                "GFX_RAYMARCH_FEATURE: %s=%d %s", (const char *)feature,
                (gX_I32)state, handled ? "set" : "unknown");
            s_headlessFeatureDone = 1;
        }
    }
    
    //opencode: regenerate noise texture when terrain.seed changes (ImGui/preset edits)
    if (rm->texture.noiseTex.id > 0 && rm->terrain.seed != rm->texture.noiseSeed) {
        gX_RendererRaymarch_Noise_Generate(rm);
    }
    
    if (rm->state.useInternalTime) {
        rm->state.time += (gX_F32)gX_FrameTime_Get();
    }
    
    //opencode: poll GPU timer results (non-blocking)
    if (rm->timersInited) {
        gX_RHI_Query_Timer_Get(&rm->timerDraw);
        gX_RHI_Query_Timer_Get(&rm->timerTotal);
    }
}

//ai: --raymarch-profile CSV write (shared by the shader and proxy paths).
static gX_I0 rm_profile_write(gX_RendererRaymarch *rm) {
    if (!arguments.raymarchProfile) return;
    static gX_U32 csvFrame = 0;
    static gX_I1 csvHeaderWritten = 0;
    static FILE *csvFile = NULL;
    if (!csvHeaderWritten) {
        csvFile = fopen(arguments.raymarchProfilePath, "w");
        if (csvFile) {
            fprintf(csvFile, "frame,draw_ms,total_ms,render_scale\n");
            gX_TraceLog(gX_LOG_INFO, "RM_PROFILE: writing CSV to %s",
                arguments.raymarchProfilePath);
        } else {
            gX_TraceLog(gX_LOG_ERROR,
                "RM_PROFILE: cannot open %s for writing",
                arguments.raymarchProfilePath);
        }
        csvHeaderWritten = 1;
    }
    if (csvFile) {
        fprintf(csvFile, "%u,%.3f,%.3f,%.3f\n", csvFrame++,
            rm->timerDraw.elapsedMs, rm->timerTotal.elapsedMs,
            rm->parameters.renderScale);
        if (!getenv("GX_BENCHMARK_NO_FLUSH")) fflush(csvFile);
    }
}

//ai: --- proxy wireframe mode -------------------------------------------------
//   Stop-render authoring view: the SDF march is skipped entirely and the
//   scene is drawn as raster line proxies (objects, terrain/water grids,
//   light/sun markers) so camera and panel stay responsive at near-zero GPU
//   cost. Object proxies mirror the shader transform:
//   local -> inverse-transpose SDF basis -> undo slot yaw -> object position.

static gX_F32v3 rm_proxy_point(const gX_Raymarch_Objects *o, gX_I32 i,
    gX_F32v3 local)
{
    return gX_RendererRaymarch_Object_ToWorld(o, i, local);
}

static gX_I0 rm_proxy_line(gX_F32v3 a, gX_F32v3 b, gX_Color color)
{
    gX_Line3D_Draw((gX_Line3D){ a, b, 0.0f }, color);
}

static gX_Color rm_proxy_color(gX_F32v4 c)
{
    gX_F32 r = c.x < 0.0f ? 0.0f : (c.x > 1.0f ? 1.0f : c.x);
    gX_F32 g = c.y < 0.0f ? 0.0f : (c.y > 1.0f ? 1.0f : c.y);
    gX_F32 b = c.z < 0.0f ? 0.0f : (c.z > 1.0f ? 1.0f : c.z);
    return (gX_Color){ (gX_U8)(r * 255.0f + 0.5f),
        (gX_U8)(g * 255.0f + 0.5f), (gX_U8)(b * 255.0f + 0.5f), 255 };
}

static gX_I0 rm_proxy_ring(const gX_Raymarch_Objects *o, gX_I32 i,
    gX_F32v3 center, gX_F32v3 axisA, gX_F32v3 axisB, gX_F32 radius,
    gX_Color color)
{
    const gX_I32 segments = 24;
    const gX_F32 tau = 6.28318530718f;
    gX_F32v3 prev = { 0 };
    for (gX_I32 s = 0; s <= segments; s++) {
        gX_F32 t = (gX_F32)s / (gX_F32)segments * tau;
        gX_F32 c = cosf(t) * radius, sn = sinf(t) * radius;
        gX_F32v3 local = {
            center.x + axisA.x * c + axisB.x * sn,
            center.y + axisA.y * c + axisB.y * sn,
            center.z + axisA.z * c + axisB.z * sn,
        };
        gX_F32v3 world = rm_proxy_point(o, i, local);
        if (s > 0) rm_proxy_line(prev, world, color);
        prev = world;
    }
}

static gX_I0 rm_proxy_box(const gX_Raymarch_Objects *o, gX_I32 i,
    gX_F32v3 half, gX_Color color)
{
    static const gX_I32 edge[12][2] = {
        {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    gX_F32v3 c[8];
    for (gX_I32 s = 0; s < 8; s++) {
        gX_F32 sx = (s & 1) ? 1.0f : -1.0f;
        gX_F32 sy = (s & 2) ? 1.0f : -1.0f;
        gX_F32 sz = (s & 4) ? 1.0f : -1.0f;
        c[s] = rm_proxy_point(o, i,
            (gX_F32v3){ sx * half.x, sy * half.y, sz * half.z });
    }
    for (gX_I32 e = 0; e < 12; e++)
        rm_proxy_line(c[edge[e][0]], c[edge[e][1]], color);
}

static gX_I0 rm_proxy_vertical(const gX_Raymarch_Objects *o, gX_I32 i,
    gX_F32 y0, gX_F32 y1, gX_F32 radius, gX_Color color)
{
    for (gX_I32 k = 0; k < 4; k++) {
        gX_F32 a = (gX_F32)k * 1.57079633f;
        gX_F32v3 p0 = rm_proxy_point(o, i,
            (gX_F32v3){ cosf(a) * radius, y0, sinf(a) * radius });
        gX_F32v3 p1 = rm_proxy_point(o, i,
            (gX_F32v3){ cosf(a) * radius, y1, sinf(a) * radius });
        rm_proxy_line(p0, p1, color);
    }
}

static gX_I0 rm_proxy_object(gX_RendererRaymarch *rm, gX_I32 i)
{
    gX_Raymarch_Objects *o = &rm->objects;
    gX_Color color = rm_proxy_color(o->color[i]);
    gX_F32v4 a = o->parameters[i];
    gX_F32v3 up = { 0.0f, 1.0f, 0.0f };
    switch (o->type[i]) {
        case 0: // sphere
            rm_proxy_ring(o, i, (gX_F32v3){0}, (gX_F32v3){1,0,0}, up, a.x, color);
            rm_proxy_ring(o, i, (gX_F32v3){0}, up, (gX_F32v3){0,0,1}, a.x, color);
            rm_proxy_ring(o, i, (gX_F32v3){0}, (gX_F32v3){0,0,1}, (gX_F32v3){1,0,0}, a.x, color);
            break;
        case 1: case 9: case 12: // cube / menger / rounded box
            rm_proxy_box(o, i, (gX_F32v3){a.x, a.y, a.z}, color);
            break;
        case 3: // box frame — outer + inner shells
            rm_proxy_box(o, i, (gX_F32v3){a.x, a.y, a.z}, color);
            rm_proxy_box(o, i, (gX_F32v3){a.x - a.w, a.y - a.w, a.z - a.w},
                color);
            break;
        case 2: // torus — main ring + two tube rings
            rm_proxy_ring(o, i, (gX_F32v3){0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.x, color);
            rm_proxy_ring(o, i, (gX_F32v3){a.x,0,0}, up, (gX_F32v3){0,0,1}, a.y, color);
            rm_proxy_ring(o, i, (gX_F32v3){-a.x,0,0}, up, (gX_F32v3){0,0,1}, a.y, color);
            break;
        case 4: // cylinder
            rm_proxy_ring(o, i, (gX_F32v3){0,-a.y,0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.x, color);
            rm_proxy_ring(o, i, (gX_F32v3){0, a.y,0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.x, color);
            rm_proxy_vertical(o, i, -a.y, a.y, a.x, color);
            break;
        case 5: { // cone — base ring + apex lines
            rm_proxy_ring(o, i, (gX_F32v3){0,-a.z,0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.x, color);
            gX_F32v3 apex = rm_proxy_point(o, i, (gX_F32v3){0, a.z, 0});
            for (gX_I32 k = 0; k < 4; k++) {
                gX_F32 ang = (gX_F32)k * 1.57079633f;
                gX_F32v3 base = rm_proxy_point(o, i,
                    (gX_F32v3){cosf(ang) * a.x, -a.z, sinf(ang) * a.x});
                rm_proxy_line(base, apex, color);
            }
            break; }
        case 10: // capsule
            rm_proxy_ring(o, i, (gX_F32v3){0,-a.x,0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.y, color);
            rm_proxy_ring(o, i, (gX_F32v3){0, a.x,0}, (gX_F32v3){1,0,0}, (gX_F32v3){0,0,1}, a.y, color);
            rm_proxy_vertical(o, i, -a.x, a.x, a.y, color);
            break;
        case 11: { // octahedron edges
            gX_F32v3 v[6] = {
                {a.x,0,0}, {-a.x,0,0}, {0,a.x,0}, {0,-a.x,0}, {0,0,a.x},
                {0,0,-a.x}
            };
            static const gX_I32 e[12][2] = {
                {0,2},{0,3},{0,4},{0,5}, {1,2},{1,3},{1,4},{1,5},
                {2,4},{2,5},{3,4},{3,5}
            };
            for (gX_I32 k = 0; k < 12; k++)
                rm_proxy_line(rm_proxy_point(o, i, v[e[k][0]]),
                    rm_proxy_point(o, i, v[e[k][1]]), color);
            break; }
        case 13: { // hex prism — two hexagons + verticals
            for (gX_I32 cap = 0; cap < 2; cap++) {
                gX_F32 y = cap ? a.y : -a.y;
                gX_F32v3 prev = { 0 };
                for (gX_I32 k = 0; k <= 6; k++) {
                    gX_F32 ang = 1.04719755f * (gX_F32)k;
                    gX_F32v3 p = rm_proxy_point(o, i, (gX_F32v3){
                        cosf(ang) * a.x, y, sinf(ang) * a.x });
                    if (k > 0) rm_proxy_line(prev, p, color);
                    prev = p;
                }
            }
            for (gX_I32 k = 0; k < 6; k++) {
                gX_F32 ang = 1.04719755f * (gX_F32)k;
                gX_F32v3 p0 = rm_proxy_point(o, i,
                    (gX_F32v3){cosf(ang) * a.x, -a.y, sinf(ang) * a.x});
                gX_F32v3 p1 = rm_proxy_point(o, i,
                    (gX_F32v3){cosf(ang) * a.x, a.y, sinf(ang) * a.x});
                rm_proxy_line(p0, p1, color);
            }
            break; }
        case 6: case 8: // tree / conifer — bounds
            rm_proxy_box(o, i, (gX_F32v3){1.2f, 2.5f, 1.2f}, color);
            break;
        case 7: // mandelbulb — bounds
            rm_proxy_box(o, i, (gX_F32v3){1.2f, 1.2f, 1.2f}, color);
            break;
        default:
            rm_proxy_box(o, i, (gX_F32v3){1.0f, 1.0f, 1.0f}, color);
            break;
    }
}

static gX_I0 rm_proxy_grid(gX_F32 y, gX_I32 slices, gX_F32 spacing,
    gX_Color color)
{
    gX_I32 half = slices / 2;
    gX_F32 extent = (gX_F32)half * spacing;
    for (gX_I32 i = -half; i <= half; i++) {
        gX_F32 p = (gX_F32)i * spacing;
        rm_proxy_line((gX_F32v3){ p, y, -extent },
            (gX_F32v3){ p, y, extent }, color);
        rm_proxy_line((gX_F32v3){ -extent, y, p },
            (gX_F32v3){ extent, y, p }, color);
    }
}

static gX_I0 gX_RendererRaymarch_Proxy_Draw(gX_RendererRaymarch *rm,
    gX_Camera3D *cam, gX_RenderTexture *target)
{
    gX_Drawing_BeginTexture(*target);
        gX_Drawing_ClearBG((gX_Color){ 10, 12, 18, 255 });
        gX_Drawing_Begin3D(*cam);
            if (rm->flags.enableTerrain)
                rm_proxy_grid(rm->terrain.offset.y, 40, 5.0f,
                    (gX_Color){ 70, 90, 70, 255 });
            if (rm->flags.enableWater)
                rm_proxy_grid(rm->water.offset.y, 20, 5.0f,
                    (gX_Color){ 60, 95, 150, 255 });
            if (rm->flags.enableObjects) {
                for (gX_I32 i = 0; i < rm->objects.amount; i++)
                    rm_proxy_object(rm, i);
            }
            if (rm->flags.enableLights) {
                gX_Color lightColor = { 255, 220, 140, 255 };
                for (gX_I32 i = 0; i < rm->lights.amount; i++) {
                    gX_F32 radius = 0.1f * logf(rm->lights.brightness[i]
                        + 1.0f);
                    if (radius < 0.05f) radius = 0.05f;
                    gX_F32v3 c = rm->lights.pos[i];
                    gX_Circle3D_Draw((gX_Circle3D){ c, radius,
                        {0,1,0}, 0.0f }, lightColor);
                    gX_Circle3D_Draw((gX_Circle3D){ c, radius,
                        {1,0,0}, 0.0f }, lightColor);
                    gX_Circle3D_Draw((gX_Circle3D){ c, radius,
                        {0,0,1}, 0.0f }, lightColor);
                }
            }
            //sun direction marker
            {
                gX_F32v3 sunDir = gX_F32v3_Normalize(rm->sun.dir);
                gX_F32v3 tip = { sunDir.x * 100.0f, sunDir.y * 100.0f,
                    sunDir.z * 100.0f };
                rm_proxy_line((gX_F32v3){0,0,0}, tip,
                    (gX_Color){ 255, 200, 120, 255 });
                gX_Circle3D_Draw((gX_Circle3D){ tip, 4.0f, {0,1,0}, 0.0f },
                    (gX_Color){ 255, 220, 160, 255 });
            }
        gX_Drawing_End3D();
    gX_Drawing_EndTexture();
}

//opencode: SetLocs called inside BeginShader (shader bound before uniform uploads)
static gX_I0 rm_draw(gX_RendererRaymarch *rm, gX_RenderTexture *outputRT,
    gX_Texture rasterDepth, gX_I32 hybridMode) {
    //opencode: deferred GPU timer init
    if (!rm->timersInited) {
        if (gX_RHI_Query_Timer_Init(&rm->timerDraw, "rm:draw") >= 0 &&
            gX_RHI_Query_Timer_Init(&rm->timerTotal, "rm:total") >= 0)
            rm->timersInited = true;
    }
    gX_RHI_Query_Timer_Begin(&rm->timerTotal);

    //opencode: use internal camera when flagged
    gX_Camera3D cam = !hybridMode && rm->state.useInternalCamera ? rm->camera.camera.camera3D : camera.camera3D;
    //ai: pausing stops the clock, not its value; frozen presets can scrub time.
    gX_F32 time = rm->state.time;

    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;

    //ai: proxy wireframe mode — stops the SDF march entirely (see the proxy
    //   helpers above); the profiler still records the cheap frame.
    if (!hybridMode && rm->flags.enableProxy) {
        gX_RendererRaymarch_Proxy_Draw(rm, &cam, &target);
        gX_RHI_Query_Timer_End(&rm->timerTotal);
        rm_profile_write(rm);
        return;
    }

    //opencode: sync sun direction with sky/ephemeris for consistent lighting
    //opencode: disabled for headless — ephemeris may return zero direction
    //{
    //    gX_Ephemeris_Body *sunBody = gX_Ephemeris_GetBody(&gamedata.ephemeris, "Sun");
    //    if (sunBody) rm->sun.dir = sunBody->direction;
    //}

    //opencode: draw background sky (only if procedural sky is off — procedural
    //   sky is drawn in-shader and replaces the background entirely)
    if (!hybridMode && !rm->flags.enableProceduralSky) {
        gX_RendererSky *sky = (renderer.sky.amount > 0 && renderer.sky.data)
            ? (gX_RendererSky*)renderer.sky.data : NULL;
        if (sky && sky->loadingstate.loaded) {
            gX_RendererSky_DrawTo(sky, &target, &cam);
        } else {
            gX_Drawing_BeginTexture(target);
                gX_Drawing_ClearBG((gX_Color){10,10,30,255});
            gX_Drawing_EndTexture();
        }
    }

    //opencode: determine per-module render resolution (renderScale < 1.0 = half-res RT)
    gX_U16 rw = target.texture.width;
    gX_U16 rh = target.texture.height;
    gX_RenderTexture *renderTarget = &target;

    if (!hybridMode && rm->parameters.renderScale > 0.0f && rm->parameters.renderScale < 1.0f) {
        gX_U16 hw = (gX_U16)((gX_F32)rw * rm->parameters.renderScale + 0.5f);
        gX_U16 hh = (gX_U16)((gX_F32)rh * rm->parameters.renderScale + 0.5f);
        if (hw < 1) hw = 1;
        if (hh < 1) hh = 1;
        if (rm->cachedRT.width != hw || rm->cachedRT.height != hh) {
            if (rm->cachedRT.rt.id) gX_RenderTexture_Unload(rm->cachedRT.rt);
            rm->cachedRT.rt = gX_RenderTexture_Load_HDR(hw, hh);
            rm->cachedRT.width = hw;
            rm->cachedRT.height = hh;
        }
        rw = hw; rh = hh;
        renderTarget = &rm->cachedRT.rt;
    }

    //ai: clear only the private cache so transparent misses cannot retain old geometry.
    //opencode: draw raymarch terrain over sky with alpha blend (miss pixels are transparent
    //   unless procedural sky is active)
    gX_Drawing_BeginTexture(*renderTarget);
        if (renderTarget != &target) gX_Drawing_ClearBG((gX_Color){0,0,0,0});
        gX_Drawing_BeginBlend(gX_BLEND_ALPHA);

        if (rm->shader.shader.shader.id) {
            gX_RHI_Query_Timer_Begin(&rm->timerDraw);
            gX_Drawing_BeginShader(rm->shader.shader.shader);

                gX_RendererRaymarch_Shader_SetLocs(rm, cam, time);

                gX_Shader shader = rm->shader.shader.shader;
                gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,"hybridDepthEnabled"),hybridMode);
                //ai: temporal coverage is opt-in; standalone and legacy shared draws keep zero jitter.
                const gX_I8 *temporalHook = getenv("GX_RAYMARCH_TEMPORAL");
                gX_RendererGFX *temporalGfx = renderer.gfx.data;
                gX_F32v2 temporalJitter = {0};
                if (hybridMode >= 2 && temporalHook && temporalHook[0] != '0'
                    && temporalGfx && temporalGfx->state.ENABLE_TAA)
                    temporalJitter = temporalGfx->taa.jitter;
                gX_Shader_SetValue_V2(shader,gX_RHI_GetUniformLocation(shader.id,"hybridJitter"),temporalJitter);
                gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,"hybridTemporalAnimated"),rm->state.useInternalTime);
                if (hybridMode)
                {
                    gX_Shader_SetValue_V2(shader,gX_RHI_GetUniformLocation(shader.id,"hybridClip"),
                        (gX_F32v2){camera.frustum.near,camera.frustum.far});
                }
                if (rasterDepth.id)
                {
                    gX_RHI_Tex_BindOnUnit(rasterDepth.id,3);
                    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,"hybridRasterDepth"),3);
                }

                //opencode: override res uniform to match actual render dimensions
                gX_Shader_SetValue_V2(rm->shader.shader.shader, rm->shader.shader.locs.locRes,
                    (gX_F32v2){(gX_F32)rw, (gX_F32)rh});

                gX_Shader_SetValue_Cubemap(rm->shader.shader.shader,rm->shader.shader.locs.locTexSky,
                                  rm->texture.skybox.texture);
                gX_Shader_SetValue_Texture(rm->shader.shader.shader,rm->shader.shader.locs.locTex0,
                                  rm->texture.textures[0].texture);
                gX_Shader_SetValue_Texture(rm->shader.shader.shader,rm->shader.shader.locs.locTex1,
                                  rm->texture.textures[1].texture);
                gX_Shader_SetValue_Texture(rm->shader.shader.shader,rm->shader.shader.locs.locTex2,
                                  rm->texture.textures[2].texture);
                //opencode: bind noise texture
                if (rm->shader.locations.texNoise >= 0 && rm->texture.noiseTex.id > 0) {
                    gX_RHI_Tex_BindOnUnit(rm->texture.noiseTex.id, 4);
                    gX_RHI_Shader_Use(rm->shader.shader.shader.id);
                    gX_RHI_UniformInt(rm->shader.locations.texNoise, 4);
                }
                if (hybridMode >= 2 && hybridMode != 4)
                {
                    gX_RHI_Tex_BindOnUnit(0,5);
                    gX_RHI_UniformInt(gX_RHI_GetUniformLocation(shader.id,"hybridRasterDepth"),5);
                    gX_RHI_InvalidateCaches();
                    gX_RHI_Disable(gX_RHI_BLEND);
                    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
                    gX_RHI_Depth_Func(gX_RHI_LESS);
                    gX_RHI_Depth_Write(1);
                }
                //ai: coverage replaces only visible SDF pixels; sampled shared depth owns visibility.
                if (hybridMode == 4)
                {
                    gX_RHI_Disable(gX_RHI_BLEND);
                    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
                    gX_RHI_Depth_Write(0);
                }
                gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)rw, (gX_F32)rh);
            gX_Drawing_EndShader();
            gX_RHI_Query_Timer_End(&rm->timerDraw);
        }

        gX_Drawing_EndBlend();
    gX_Drawing_EndTexture();

    //opencode: upscale from half-res cached RT to full-res target
    if (renderTarget != &target) {
        gX_Drawing_BeginTexture(target);
            gX_Drawing_BeginBlend(gX_BLEND_ALPHA);
            gX_Texture_Draw_Params upParams = {
                .dst = {0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
                .tint = {255, 255, 255, 255},
                .scale = {1.0f, 1.0f}
            };
            gX_Texture_Draw(rm->cachedRT.rt.texture, upParams);
            gX_Drawing_EndBlend();
        gX_Drawing_EndTexture();
    }

    gX_RHI_Query_Timer_End(&rm->timerTotal);
    rm_profile_write(rm);
}

//ai: standalone/module rendering retains its original color-only contract.
gX_I0 gX_RendererRaymarch_DrawTo(gX_RendererRaymarch *rm, gX_RenderTexture *outputRT)
{
    rm_draw(rm,outputRT,(gX_Texture){0},0);
}

//ai: hybrid input must be sampleable depth from the current perspective-camera frame.
gX_I0 gX_RendererRaymarch_DrawHybrid(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT, gX_Texture rasterDepth)
{
    if (!rasterDepth.id || !outputRT || !rm->shader.shader.loadingState.loaded
        || camera.camera3D.projection != 0) return;
    rm_draw(rm,outputRT,rasterDepth,1);
}

//ai: bracket the raw depth draw and restore state before downstream GFX passes.
static gX_I0 rm_draw_shared(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT, gX_I32 mode)
{
    if (!outputRT || !outputRT->id || !outputRT->depth.id
        || !rm->shader.shader.loadingState.loaded || camera.camera3D.projection != 0) return;
    gX_Batch_Flush();
    gX_GL_State_Push();
    rm_draw(rm,outputRT,(gX_Texture){0},mode);
    gX_Batch_Flush();
    gX_GL_State_Pop();
    gX_RHI_InvalidateCaches();
}

//ai: the shared scene color/depth and normal coverage use the same primary-hit precision.
gX_I0 gX_RendererRaymarch_DrawSharedDepth(gX_RendererRaymarch *rm, gX_RenderTexture *outputRT)
{
    rm_draw_shared(rm,outputRT,2);
}

//ai: rerender primary hits only into the normal target, preserving foreground raster normals.
gX_I0 gX_RendererRaymarch_DrawSharedNormals(gX_RendererRaymarch *rm, gX_RenderTexture *outputRT)
{
    rm_draw_shared(rm,outputRT,3);
}

//ai: fingerprint authored inputs and texture identities/sampling only; camera/profiling do not invalidate history.
gX_U64 gX_RendererRaymarch_Temporal_Signature(const gX_RendererRaymarch *rm)
{
    if (!rm) return 0;
    const gX_U8 *bytes = (const gX_U8 *)&rm->parameters;
    gX_U64 length = (const gX_U8 *)(&rm->debug + 1) - bytes;
    gX_U64 hash = 14695981039346656037ULL;
    for (gX_U64 i = 0; i < length; i++)
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    bytes = (const gX_U8 *)&rm->state.colorIsRGB;
    for (gX_U64 i = 0; i < sizeof(rm->state.colorIsRGB); i++)
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    hash = (hash ^ (gX_U64)rm->state.useInternalTime) * 1099511628211ULL;
    if (!rm->state.useInternalTime)
    {
        bytes = (const gX_U8 *)&rm->state.time;
        for (gX_U64 i = 0; i < sizeof(rm->state.time); i++)
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    for (gX_U32 slot = 0; slot <= GX_RM_MAX_TEXTURES; slot++)
    {
        const gX_Texture_Wrapper *texture = slot ? &rm->texture.textures[slot-1] : &rm->texture.skybox;
        for (gX_U64 i = 0; i < sizeof(texture->path) && texture->path[i]; i++)
            hash = (hash ^ (gX_U8)texture->path[i]) * 1099511628211ULL;
        gX_U32 values[] = {texture->texture.id,(gX_U32)texture->filter,
            (gX_U32)texture->wrap,(gX_U32)texture->skyboxLayout};
        bytes = (const gX_U8 *)values;
        for (gX_U64 i = 0; i < sizeof(values); i++)
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    return hash ^ rm->shader.shader.shader.id;
}

//ai: tag visible SDF ownership for camera-only TAA, preserving raster vectors outside its coverage.
gX_I0 gX_RendererRaymarch_DrawSharedTemporal(gX_RendererRaymarch *rm,
    gX_RenderTexture *outputRT, gX_Texture sharedDepth)
{
    if (!outputRT || !outputRT->id || !sharedDepth.id
        || sharedDepth.id == outputRT->depth.id || !rm->shader.shader.loadingState.loaded
        || camera.camera3D.projection != 0) return;
    gX_Batch_Flush();
    gX_GL_State_Push();
    rm_draw(rm,outputRT,sharedDepth,4);
    gX_Batch_Flush();
    gX_GL_State_Pop();
    gX_RHI_InvalidateCaches();
}

//opencode: zero-init the raymarch state
gX_I0 gX_RendererRaymarch_LoadClear(gX_RendererRaymarch *rm) {
    *rm = (gX_RendererRaymarch){ 0 };
}

//ai: presets reset values only after releasing the previous shader, textures, and queries.
gX_I0 gX_RendererRaymarch_LoadDefault(gX_RendererRaymarch *rm) {
    gX_RendererRaymarch_Unload(rm);
    gX_RendererRaymarch_LoadClear(rm);
    gX_RendererRaymarch_Load(rm);
}

//opencode: randomize raymarch parameters for exploration
gX_I0 gX_RendererRaymarch_LoadRandom(gX_RendererRaymarch *rm) {
    gX_RendererRaymarch_LoadDefault(rm); //opencode: start from valid defaults (was LoadClear-only — empty scene bug)
    gX_Random_Call();
    rm->terrain.seed = (gX_I32)(rng.value & 0x7FFFFFFF);
    rm->terrain.offset = (gX_F32v3){0.0f,-3.0f,0.0f};
    rm->terrain.scale = gX_F32v3_One();
    rm->terrain.color = (gX_Color){63 + (gX_U8)(rng.value % 64),127,31,255};
    //opencode: randomize terrain shape (amp + freq) while keeping sane range
    rm->terrain.parameters.x = 3.0f + (gX_F32)(rng.value % 16);
    rm->terrain.parameters.y = 0.0008f + (gX_F32)(rng.value % 8) * 0.0002f;
    //opencode: regenerate noise texture for the new seed
    gX_RendererRaymarch_Noise_Generate(rm);
}



//opencode: built-in presets for quick switching
gX_I0 gX_RendererRaymarch_Preset_BuiltIn(gX_RendererRaymarch *rm, gX_I32 index) {
    gX_TRACELOG_PROLOGUE("RendererRaymarch_Preset_BuiltIn","RENDERER_RM: Loading builtin preset")
    gX_RendererRaymarch_LoadDefault(rm);
    switch (index) {
        case 1: { // Island — terrain only, slightly lower, far view
            rm->flags.enableWater = false;
            rm->flags.enableTerrainGrid = false;
            rm->terrain.offset.y = -5.0f;
            rm->terrain.parameters = (gX_F32v4){6.0f, 0.002f, 3.0f, 0.0f};
            rm->parameters.far = 500.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            break;
        }
        case 2: { // Water — flat water surface, no terrain, point lights
            rm->flags.enableTerrain = false;
            rm->flags.enableWater = true;
            rm->water.offset.y = -1.0f;
            rm->water.speed = (gX_F32v3){0.5f,0.0f,0.3f};
            rm->water.color = (gX_Color){20, 100, 220, 255};
            rm->water.ripple = (gX_F32v4){0.2f, 15.0f, 0.0f, 0.0f};
            rm->flags.enableLights = true;
            rm->flags.enableLightIllumination = true;
            rm->flags.enableLightGlow = true;
            rm->misc.lightAmbient = (gX_Color){100, 140, 200, 255};
            rm->lights.amount = 4;
            rm->lights.pos[0] = (gX_F32v3){6.0f, 8.0f, 6.0f};
            rm->lights.pos[1] = (gX_F32v3){-6.0f, 4.0f, 8.0f};
            rm->lights.pos[2] = (gX_F32v3){8.0f, 6.0f, -4.0f};
            rm->lights.pos[3] = (gX_F32v3){-8.0f, 5.0f, -6.0f};
            rm->lights.color[0] = (gX_F32v4){0.0f, 0.5f, 1.0f, 1.0f};
            rm->lights.color[1] = (gX_F32v4){1.0f, 0.3f, 0.0f, 1.0f};
            rm->lights.color[2] = (gX_F32v4){0.3f, 1.0f, 0.5f, 1.0f};
            rm->lights.color[3] = (gX_F32v4){1.0f, 0.5f, 1.0f, 1.0f};
            rm->lights.brightness[0] = 5.0f;
            rm->lights.brightness[1] = 4.0f;
            rm->lights.brightness[2] = 3.0f;
            rm->lights.brightness[3] = 4.0f;
            break;
        }
        case 3: { // Flatlands — very flat terrain, wide view
            rm->terrain.parameters = (gX_F32v4){2.0f, 0.001f, 2.0f, 0.0f};
            rm->terrain.color = (gX_Color){80, 160, 60, 255};
            rm->terrain.colorSteep = (gX_Color){140, 120, 70, 255};
            rm->terrain.colorBeach = (gX_Color){190, 180, 90, 255};
            rm->parameters.far = 400.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            rm->flags.enableWater = true;
            rm->water.offset.y = -2.0f;
            break;
        }
        case 4: { // Desert — warm sand colors, gentle dunes
            rm->terrain.parameters = (gX_F32v4){5.0f, 0.003f, 3.0f, 0.0f};
            rm->terrain.color = (gX_Color){194, 178, 128, 255};
            rm->terrain.colorSteep = (gX_Color){160, 140, 100, 255};
            rm->terrain.colorBeach = (gX_Color){210, 195, 150, 255};
            rm->sun.color = (gX_Color){255, 230, 180, 255};
            rm->sun.brightness = 1.2f;
            rm->parameters.far = 300.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            break;
        }
        case 5: { // Alien — red terrain, purple sky, low gravity feel
            rm->terrain.parameters = (gX_F32v4){12.0f, 0.004f, 4.0f, 0.0f};
            rm->terrain.color = (gX_Color){180, 60, 40, 255};
            rm->terrain.colorSteep = (gX_Color){220, 80, 50, 255};
            rm->terrain.colorBeach = (gX_Color){140, 100, 80, 255};
            rm->sun.color = (gX_Color){255, 180, 200, 255};
            rm->sun.brightness = 0.8f;
            rm->parameters.far = 400.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            break;
        }
        case 6: { // Canyon — steep terrain, deep valleys
            rm->terrain.offset.y = -8.0f;
            rm->terrain.parameters = (gX_F32v4){20.0f, 0.005f, 4.0f, 0.0f};
            rm->terrain.color = (gX_Color){160, 100, 60, 255};
            rm->terrain.colorSteep = (gX_Color){200, 130, 70, 255};
            rm->terrain.colorBeach = (gX_Color){120, 100, 70, 255};
            rm->parameters.far = 500.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            break;
        }
        case 7: { // Primitives — one hero row of the SDF set (the five v0.15
            //   types + torus, mandelbulb, conifer); the object array caps at
            //   GX_RM_MAX_OBJECTS, so the row is exactly that many slots.
            static const gX_I32 galleryTypes[GX_RM_MAX_OBJECTS] = {
                2, 7, 8, 9, 10, 11, 12, 13 };
            rm->flags.enableTerrain = false;
            rm->flags.enableWater = false;
            rm->flags.enableTerrainGrid = false;
            rm->flags.enableFog = false;
            rm->fog.near = 400.0f;
            rm->fog.far = 800.0f;
            rm->parameters.far = 60.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            rm->objects.amount = GX_RM_MAX_OBJECTS;
            for (gX_I32 i = 0; i < GX_RM_MAX_OBJECTS; i++) {
                gX_RendererRaymarch_Object_Init(rm, (gX_U32)i);
                rm->objects.type[i] = galleryTypes[i];
                rm->objects.pos[i] = (gX_F32v3){
                    -7.0f + 2.0f * (gX_F32)i, 1.5f, 6.0f
                };
                rm->objects.matX[i] = (gX_F32v3){1.5f, 0.0f, 0.0f};
                rm->objects.matY[i] = (gX_F32v3){0.0f, 1.5f, 0.0f};
                rm->objects.matZ[i] = (gX_F32v3){0.0f, 0.0f, 1.5f};
                rm->objects.color[i] = (gX_F32v4){
                    0.55f + 0.35f * (gX_F32)(i % 3) / 2.0f,
                    0.55f + 0.35f * (gX_F32)((i + 1) % 3) / 2.0f,
                    0.55f + 0.35f * (gX_F32)((i + 2) % 3) / 2.0f,
                    1.0f,
                };
            }
            //opencode: per-type attribute defaults are applied by the init
            //   above using type=i; re-apply for the remapped gallery types.
            for (gX_I32 i = 0; i < GX_RM_MAX_OBJECTS; i++) {
                switch (rm->objects.type[i]) {
                    case 2:  rm->objects.parameters[i] = (gX_F32v4){1.2f,0.4f,0,0}; break;
                    case 7:  rm->objects.parameters[i] = (gX_F32v4){1.0f,8.0f,6.0f,2.0f}; break;
                    case 9:  rm->objects.parameters[i] = (gX_F32v4){1.0f,1.0f,1.0f,0}; break;
                    case 10: rm->objects.parameters[i] = (gX_F32v4){1.0f,0.35f,0,0}; break;
                    case 11: rm->objects.parameters[i] = (gX_F32v4){1.0f,0,0,0}; break;
                    case 12: rm->objects.parameters[i] = (gX_F32v4){1.0f,0.7f,0.7f,0.2f}; break;
                    case 13: rm->objects.parameters[i] = (gX_F32v4){1.0f,0.7f,0,0}; break;
                    default: break;
                }
            }
            rm->lights.amount = 2;
            rm->lights.pos[0] = (gX_F32v3){0.0f, 6.0f, 8.0f};
            rm->lights.brightness[0] = 2.0f;
            rm->lights.color[0] = (gX_F32v4){0.7f, 0.85f, 1.0f, 1.0f};
            rm->lights.pos[1] = (gX_F32v3){-5.0f, 3.0f, 3.0f};
            rm->lights.brightness[1] = 1.2f;
            rm->lights.color[1] = (gX_F32v4){1.0f, 0.8f, 0.6f, 1.0f};
            break;
        }
        case 8: { // Cloudscape — open low terrain with clouds + haze
            rm->flags.enableClouds = true;
            rm->flags.enableVolumetricFog = true;
            rm->flags.enableWater = false;
            rm->flags.enableTerrainGrid = false;
            rm->terrain.offset.y = -6.0f;
            rm->terrain.parameters = (gX_F32v4){4.0f, 0.002f, 3.0f, 0.0f};
            rm->terrain.color = (gX_Color){80, 130, 60, 255};
            rm->terrain.colorSteep = (gX_Color){130, 120, 80, 255};
            rm->terrain.colorBeach = (gX_Color){190, 180, 100, 255};
            rm->parameters.far = 400.0f;
            rm->parameters.steps = GX_RM_DEFAULT_STEPS;
            break;
        }
        default: break;
    }
    gX_TRACELOG_EPILOGUE("RENDERER_RM: Loading done")
}

//opencode: load preset from JSON file (delegates to new schema system)
gX_I0 gX_RendererRaymarch_Preset_Load(gX_RendererRaymarch *rm, const gX_I8 *path) {
    gX_TRACELOG_PROLOGUE("RendererRaymarch_Preset_Load","RENDERER_RM: Loading preset")
    gX_RendererRaymarch_Preset_Load_New(rm, path);
    gX_TRACELOG_EPILOGUE("RENDERER_RM: Loading done")
}

//opencode: save preset to JSON file (delegates to new schema system)
gX_I0 gX_RendererRaymarch_Preset_Save(gX_RendererRaymarch *rm, const gX_I8 *path) {
    gX_TRACELOG_PROLOGUE("RendererRaymarch_Preset_Save","RENDERER_RM: Saving preset")
    gX_RendererRaymarch_Preset_Save_New(rm, path);
    gX_TRACELOG_EPILOGUE("RENDERER_RM: Saving done")
}

//opencode: unload + reload shader wrapper (hot-reload)
gX_I0 gX_RendererRaymarch_Shader_Reload(gX_RendererRaymarch *rm) {
    gX_Shader_Wrapper_Unload(&rm->shader.shader);
    rm->shader.shader.loadingState.load = true;
    rm->shader.shader.loadingState.loadAsap = true;
    rm->shader.locationsLoaded = false;
}

//opencode: compile-time feature toggles for the editor panel — edit the
//   defines string in place and reload the shader when the digit changes.
gX_I1 gX_RendererRaymarch_Defines_Get(gX_RendererRaymarch *rm,
    const gX_I8 *name) {
    gX_I8 pattern[64];
    snprintf((char *)pattern, sizeof(pattern), "#define %s ",
        (const char *)name);
    const gX_I8 *at = strstr((const char *)rm->shader.shader.defines,
        (const char *)pattern);
    if (!at) return 0;
    at += strlen((const char *)pattern);
    return (gX_I1)(*at == '1');
}

gX_I0 gX_RendererRaymarch_Defines_Set(gX_RendererRaymarch *rm,
    const gX_I8 *name, gX_I1 value) {
    gX_I8 pattern[64];
    snprintf((char *)pattern, sizeof(pattern), "#define %s ",
        (const char *)name);
    gX_I8 *at = strstr(rm->shader.shader.defines, (const char *)pattern);
    if (!at) return;
    at += strlen((const char *)pattern);
    if (*at == (value ? '1' : '0')) return;
    *at = value ? '1' : '0';
    gX_RendererRaymarch_Shader_Reload(rm);
}

//opencode: cubemap face draw callback — renders raymarch terrain for one face
static gX_RendererRaymarch *g_rm_cubemap = NULL;
static gX_I0 rm_cubemap_draw_face(gX_Camera3D *faceCam, gX_I32 faceIndex) {
    gX_RendererRaymarch *rm = g_rm_cubemap;
    if (!rm) return;
    (void)faceIndex;

    gX_F32 time = 0.0f; //opencode: always 0 — consistent cubemap faces

    if (rm->shader.shader.shader.id) {
        gX_Drawing_Begin3D(*faceCam);
        gX_Drawing_BeginShader(rm->shader.shader.shader);

            gX_RendererRaymarch_Shader_SetLocs(rm, *faceCam, time);

            gX_Shader_SetValue_Cubemap(rm->shader.shader.shader, rm->shader.shader.locs.locTexSky,
                              rm->texture.skybox.texture);
            gX_Shader_SetValue_Texture(rm->shader.shader.shader, rm->shader.shader.locs.locTex0,
                              rm->texture.textures[0].texture);
            gX_Shader_SetValue_Texture(rm->shader.shader.shader, rm->shader.shader.locs.locTex1,
                              rm->texture.textures[1].texture);
            gX_Shader_SetValue_Texture(rm->shader.shader.shader, rm->shader.shader.locs.locTex2,
                              rm->texture.textures[2].texture);
            //opencode: bind noise texture
            if (rm->shader.locations.texNoise >= 0 && rm->texture.noiseTex.id > 0) {
                gX_RHI_Tex_BindOnUnit(rm->texture.noiseTex.id, 4);
                gX_RHI_Shader_Use(rm->shader.shader.shader.id);
                gX_RHI_UniformInt(rm->shader.locations.texNoise, 4);
            }

            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);

        gX_Drawing_EndShader();
        gX_Drawing_End3D();
    }
}

gX_Texture gX_RendererRaymarch_GenerateCubemap(gX_RendererRaymarch *rm,
    gX_U32 size, gX_F32v3 position)
{
    gX_Texture cm = gX_Cubemap_Create(size);
    if (cm.id == 0) return cm;

    g_rm_cubemap = rm;
    gX_Cubemap_Render(cm, size, position, (gX_F32v3){0.3f, 0.5f, 0.8f},
                      &rm_cubemap_draw_face);
    return cm;
}
