/**********************************************************************************************
*
*   [gX: src/renderer/water.c]
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


//opencode: water renderer with Gerstner waves + ripple system + GFX integration
#include "water.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "../camera.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/profiler.h"
#include "../common/random.h"
#include "../common/utils.h"
#include "../gxmath/constants.h"
#include "../main.h"
#include "../resource/resource.h"
#include "../wrappers/wrappers.h"
#include "../audio.h"

#include "renderer.h"
#include "gfx.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_matrix.h"
#include "../rhi/rhi_buf.h"
#include "../rhi/rhi_state.h"

#ifdef GX_WATER_REFRACTION_EXPERIMENT
//ai: isolated capture uses RHI-only copies/binds and the authoritative pass-local sampler map.
#include "../common/alloc.h"
#include "../rhi/rhi_fbo.h"
#include "../rhi/rhi_tex.h"
#include "../rhi/rhi_enums.h"
#include "../rhi/batch.h"
#endif

// Default wave parameters — 6 Gerstner wave trains
static const gX_F32 s_defaultWaveDirs[GX_WATER_MAX_WAVES][2] = {
    {1.0f, 0.0f}, {0.7f, 0.7f}, {0.0f, 1.0f},
    {-0.5f, 0.8f}, {-0.9f, 0.3f}, {0.3f, -0.9f}
};
//opencode: v0.1.20 — waves 4-5 amplitude trimmed ~20%% (second pass): the
//   residual normal-perturbation lattice after the frequency drop.
static const gX_F32 s_defaultWaveAmps[GX_WATER_MAX_WAVES] = {
    0.15f, 0.12f, 0.10f, 0.075f, 0.04f, 0.026f
};
//opencode: v0.1.20 — high-frequency waves lowered (2.0->1.2, 2.5->1.5):
//   at the demo's camera angle the 6 crossed trains built a diagonal
//   interference lattice; wavelengths below ~4 units alias on the 0.31-unit
//   grid and the sun specular lit every facet edge.
static const gX_F32 s_defaultWaveFreqs[GX_WATER_MAX_WAVES] = {
    0.8f, 1.2f, 0.5f, 1.5f, 1.2f, 1.5f
};
static const gX_F32 s_defaultWaveSpeeds[GX_WATER_MAX_WAVES] = {
    1.5f, 2.0f, 1.0f, 2.5f, 3.0f, 3.5f
};
//opencode: v0.1.20 — steepness halved: Qi drives HORIZONTAL displacement,
//   which pinches the 0.31-unit mesh quads into visible diamonds (the
//   amplitude trims plateaued because the grid was geometric, not shading).
static const gX_F32 s_defaultWaveSteeps[GX_WATER_MAX_WAVES] = {
    0.15f, 0.12f, 0.1f, 0.075f, 0.05f, 0.04f
};

//ai: the default normal map must exist in clean source checkouts; automatic ripple sources default on.
gX_I32 gX_RendererWater_Load(gX_RendererWater *water)
{
    gX_TRACELOG_PROLOGUE("RendererWater_Load","RENDERER_WATER: Loading")
    strcpy(water->name, "[gX] Water");
    strcpy(water->description, "water shader renderer with waves and ripples");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", water->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", water->description);
    water->loadingstate = (gX_LoadingState){0};
    water->loadingstate.loadAsap = true;

    water->enableAudio = false;  //opencode: standalone mode has ambient audio
    water->calmWater = 0;  //opencode: --lake sets still-lake state at scene setup

    // Init wave defaults
    water->waveCount = GX_WATER_MAX_WAVES;
    for (gX_I32 i = 0; i < GX_WATER_MAX_WAVES; i++)
    {
        water->waveDirections[i] = atan2f(s_defaultWaveDirs[i][1], s_defaultWaveDirs[i][0]);
        water->waveAmplitudes[i] = s_defaultWaveAmps[i];
        water->waveFrequencies[i] = s_defaultWaveFreqs[i];
        water->waveSpeeds[i] = s_defaultWaveSpeeds[i];
        water->waveSteepness[i] = s_defaultWaveSteeps[i];
    }

    // Init ripple pool
    water->rippleCount = 0;
    water->rippleTimer = 0.0f;
    water->automaticRipples = true;
    for (gX_I32 i = 0; i < GX_WATER_MAX_RIPPLES; i++)
    {
        water->rippleAmp[i] = 0.0f;
        water->rippleLifetime[i] = 0.0f;
    }

    // Sun defaults (standalone mode)
    water->sunDirection = (gX_F32v3){0.5f, -0.8f, 0.3f};
    water->sunColorVec = (gX_F32v3){1.0f, 0.95f, 0.85f};
    water->sunBrightnessVal = 1.5f;
    water->planeHeight = -0.6f;
    water->tint = gX_WHITE;
    water->normalMapStrength = 0.6f;
    water->normalMapScale = (gX_F32v2){4.0f, 4.0f};
    water->proceduralNormalStrength = 1.0f;
    water->proceduralNormalScale = 1.0f;
    water->normalMapScroll = (gX_F32v2){0.01f, 0.01f};
    snprintf(water->normalMapPath, sizeof(water->normalMapPath), "%s",
        GX_PATH("texture", "texture_normal.png"));

    water->gfxRef = NULL;
    water->skyCubemapLoaded = false;
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    water->sceneRefractionEnabled = false;
    water->refractionCapture = (gX_RenderTexture){0};
    water->refractionValid = false;
    water->refractionTraced = false;
#endif

    //opencode: init buoyancy demo object
    water->buoyPos = (gX_F32v3){3.0f, 0.0f, 2.0f};
    water->buoyVel = (gX_F32v3){0, 0, 0};
    water->buoyRadius = 0.4f;
    water->buoySplashDone = false;

    gX_TRACELOG_EPILOGUE("RENDERER_WATER: Load done")
    return 0;
}

//ai: experimental input lifetime follows the owning water renderer on both unload paths.
gX_I32 gX_RendererWater_Unload(gX_RendererWater *water)
{
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    if (water->refractionCapture.id) gX_RenderTexture_Unload(water->refractionCapture);
    water->refractionCapture = (gX_RenderTexture){0};
    water->refractionValid = false;
#endif
    if (!water->loadingstate.loaded) return 0;
    gX_Shader_Unload(water->shader);
    gX_Model_Unload(water->model);
    gX_Texture_Unload(water->tex);
    if (water->skyCubemapLoaded)
    {
        gX_Texture_Unload(water->skyCubemap);
        water->skyCubemapLoaded = false;
    }
    return 0;
}

//opencode: load the authored normal-map path along with the water resources.
//ai: experiment builds retain one dormant transmission shader so editor toggles never swap live programs.
static gX_I0 water_gpu_load(gX_I0 *arg)
{
    gX_RendererWater *water = (gX_RendererWater*)arg;

    // Load shader with custom vertex shader
    {
#ifdef GX_WATER_REFRACTION_EXPERIMENT
        water->shader = gX_Shader_Load_Ex("resources/shader/water.vert",
            "resources/shader/water.frag", "#define GX_WATER_REFRACTION_EXPERIMENT 1\n");
        gX_Shader_GetDefaultLocations(&water->shader);
        if (gX_Shader_GetLocation(water->shader, "waterSceneValid") < 0)
#endif
        {
#ifdef GX_WATER_REFRACTION_EXPERIMENT
        gX_Shader_Unload(water->shader);
        water->shader = (gX_Shader){0};
#endif
        gX_Shader *_rsc = gX_Resource_LoadShader("water.vert", "water.frag");
        if (_rsc) {
            water->shader = *_rsc;
            gX_Shader_GetDefaultLocations(&water->shader);
        }
        }
    }

    // Cache standard locations
    water->shader_timeLoc    = gX_Shader_GetLocation(water->shader, "time");
    water->shader_resLoc     = gX_Shader_GetLocation(water->shader, "res");
    water->shader_camposLoc  = gX_Shader_GetLocation(water->shader, "cpos");
    water->shader_camtarLoc  = gX_Shader_GetLocation(water->shader, "ctarget");
    water->shader_camupLoc   = gX_Shader_GetLocation(water->shader, "cup");
    water->shader_camfovLoc  = gX_Shader_GetLocation(water->shader, "cfov");

    // Wave locations
    water->shader_waveCountLoc = gX_Shader_GetLocation(water->shader, "waveCount");
    for (gX_I32 i = 0; i < GX_WATER_MAX_WAVES; i++)
    {
        gX_I8 name[64];
        snprintf(name, sizeof(name), "waveDirs[%i]", i);
        water->shader_waveDirsLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "waveAmps[%i]", i);
        water->shader_waveAmpsLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "waveFreqs[%i]", i);
        water->shader_waveFreqsLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "waveSpeeds[%i]", i);
        water->shader_waveSpeedsLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "waveSteeps[%i]", i);
        water->shader_waveSteepsLoc[i] = gX_Shader_GetLocation(water->shader, name);
    }

    // Ripple locations
    water->shader_rippleCountLoc = gX_Shader_GetLocation(water->shader, "rippleCount");
    for (gX_I32 i = 0; i < GX_WATER_MAX_RIPPLES; i++)
    {
        gX_I8 name[64];
        snprintf(name, sizeof(name), "ripplePos[%i]", i);
        water->shader_ripplePosLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "rippleAmp[%i]", i);
        water->shader_rippleAmpLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "rippleRadius[%i]", i);
        water->shader_rippleRadiusLoc[i] = gX_Shader_GetLocation(water->shader, name);
        snprintf(name, sizeof(name), "ripplePhase[%i]", i);
        water->shader_ripplePhaseLoc[i] = gX_Shader_GetLocation(water->shader, name);
    }

    // Sky/sun locations
    water->shader_skyBoxLoc    = gX_Shader_GetLocation(water->shader, "skyBox");
    water->shader_skyTex2DLoc  = gX_Shader_GetLocation(water->shader, "skyTex2D");
    water->shader_skyUse2DLoc  = gX_Shader_GetLocation(water->shader, "skyUse2D");
    water->shader_underwaterLoc = gX_Shader_GetLocation(water->shader, "underwater");
    water->shader_sunDirLoc    = gX_Shader_GetLocation(water->shader, "sunDir");
    water->shader_sunColorLoc  = gX_Shader_GetLocation(water->shader, "sunColor");
    water->shader_sunBrightLoc = gX_Shader_GetLocation(water->shader, "sunBrightness");

    // Normal map locations
    water->shader_normalMapLoc   = gX_Shader_GetLocation(water->shader, "normalMap");
    water->shader_normalStrengthLoc = gX_Shader_GetLocation(water->shader, "normalMapStrength");
    water->shader_normalScaleLoc = gX_Shader_GetLocation(water->shader, "normalMapScale");
    water->shader_proceduralNormalStrengthLoc = gX_Shader_GetLocation(water->shader, "proceduralNormalStrength");
    water->shader_proceduralNormalScaleLoc = gX_Shader_GetLocation(water->shader, "proceduralNormalScale");
    water->shader_normalScrollLoc = gX_Shader_GetLocation(water->shader, "normalMapScroll");
    water->shader_tintLoc = gX_Shader_GetLocation(water->shader, "waterTint");

    // Matrix locations
    water->shader_matViewLoc   = gX_Shader_GetLocation(water->shader, "matView");
    water->shader_matProjLoc   = gX_Shader_GetLocation(water->shader, "matProjection");
    water->shader_matModelLoc  = gX_Shader_GetLocation(water->shader, "matModel");
    water->shader_velocityModeLoc = gX_Shader_GetLocation(water->shader, "velocityMode");
    water->shader_velocityCurrVPLoc = gX_Shader_GetLocation(water->shader, "velocityCurrVP");
    water->shader_velocityPrevVPLoc = gX_Shader_GetLocation(water->shader, "velocityPrevVP");
    water->shader_velocityPrevTimeLoc = gX_Shader_GetLocation(water->shader, "velocityPrevTime");

    // Create tessellated water plane mesh using raw VAO (bypasses raylib's broken mesh upload)
    {
        gX_I32 segs = GX_WATER_PLANE_SEGMENTS;
        gX_F32 sizeX = GX_WATER_PLANE_SIZE_X;
        gX_F32 sizeZ = GX_WATER_PLANE_SIZE_Z;
        gX_I32 vertCount = (segs + 1) * (segs + 1);
        gX_I32 idxCount = segs * segs * 12; // double-sided: 2 tris * 3 idx * 2 sides

        gX_F32 *verts = (gX_F32*)malloc(vertCount * 3 * sizeof(gX_F32));
        gX_F32 *normals = (gX_F32*)malloc(vertCount * 3 * sizeof(gX_F32));
        gX_F32 *texcoords = (gX_F32*)malloc(vertCount * 2 * sizeof(gX_F32));
        gX_U16 *indices = (gX_U16*)malloc(idxCount * sizeof(gX_U16));

        gX_I32 vi = 0;
        for (gX_I32 iz = 0; iz <= segs; iz++) {
            for (gX_I32 ix = 0; ix <= segs; ix++) {
                verts[vi*3+0] = -sizeX/2 + (gX_F32)ix / segs * sizeX;
                verts[vi*3+1] = 0;
                verts[vi*3+2] = -sizeZ/2 + (gX_F32)iz / segs * sizeZ;
                normals[vi*3+0] = 0; normals[vi*3+1] = 1; normals[vi*3+2] = 0;
                texcoords[vi*2+0] = (gX_F32)ix / segs;
                texcoords[vi*2+1] = (gX_F32)iz / segs;
                vi++;
            }
        }

        gX_I32 ii = 0;
        for (gX_I32 iz = 0; iz < segs; iz++) {
            for (gX_I32 ix = 0; ix < segs; ix++) {
                gX_U32 a = (gX_U32)(iz * (segs + 1) + ix);
                gX_U32 b = a + 1;
                gX_U32 c = (gX_U32)((iz + 1) * (segs + 1) + ix);
                gX_U32 d = c + 1;
                // CCW front
                indices[ii++] = (gX_U16)a; indices[ii++] = (gX_U16)c; indices[ii++] = (gX_U16)b;
                indices[ii++] = (gX_U16)b; indices[ii++] = (gX_U16)c; indices[ii++] = (gX_U16)d;
                // Reversed-winding for back faces
                indices[ii++] = (gX_U16)a; indices[ii++] = (gX_U16)b; indices[ii++] = (gX_U16)c;
                indices[ii++] = (gX_U16)b; indices[ii++] = (gX_U16)d; indices[ii++] = (gX_U16)c;
            }
        }

        // Create VAO, VBO for position+texcoord, separate VBO for normals
        water->customVao = gX_RHI_VAO_Create("water_vao");
        water->customVbo = gX_RHI_Buffer_Create("water_vbo");
        water->customEbo = gX_RHI_Buffer_Create("water_ebo");
        gX_RHI_VAO_Bind(water->customVao);
        gX_RHI_Buffer_Bind(gX_RHI_ARRAY_BUFFER, water->customVbo);
        gX_I32 vertSize = vertCount * 3; // positions only, interleaved with nothing
        gX_I32 totalVerts = vertSize + vertCount * 2 + vertCount * 3; // pos + uv + normal
        gX_F32 *all = (gX_F32*)malloc(totalVerts * sizeof(gX_F32));
        memcpy(all, verts, vertSize * sizeof(gX_F32));
        memcpy(all + vertSize, texcoords, vertCount * 2 * sizeof(gX_F32));
        memcpy(all + vertSize + vertCount * 2, normals, vertCount * 3 * sizeof(gX_F32));
        gX_RHI_Buffer_Data(gX_RHI_ARRAY_BUFFER, totalVerts * sizeof(gX_F32), all, gX_RHI_STATIC_DRAW);
        free(all);
        // Position at loc 0
        gX_RHI_Attrib_Pointer(0, 3, gX_RHI_FLOAT, 0, 0, (void*)0);
        gX_RHI_Attrib_Enable(0);
        // Texcoord at loc 1 (starts after positions)
        gX_RHI_Attrib_Pointer(1, 2, gX_RHI_FLOAT, 0, 0, (void*)(vertSize * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(1);
        // Normal at loc 2 (starts after texcoords)
        gX_RHI_Attrib_Pointer(2, 3, gX_RHI_FLOAT, 0, 0, (void*)((vertSize + vertCount * 2) * sizeof(gX_F32)));
        gX_RHI_Attrib_Enable(2);
        gX_RHI_Buffer_Bind(gX_RHI_ELEMENT_ARRAY_BUFFER, water->customEbo);
        gX_RHI_Buffer_Data(gX_RHI_ELEMENT_ARRAY_BUFFER, idxCount * sizeof(gX_U16), indices, gX_RHI_STATIC_DRAW);
        gX_RHI_VAO_Bind(0);

        free(verts); free(normals); free(texcoords); free(indices);

        water->customTriCount = idxCount / 3;
        gX_TraceLog(gX_LOG_INFO, "WATER: custom VAO=%u VBO=%u EBO=%u verts=%d tris=%d",
            water->customVao, water->customVbo, water->customEbo, vertCount, water->customTriCount);

        // Still load the model for material/shader binding, but we draw via customVao
        {
            gX_Mesh dummy;
            dummy = gX_Mesh_GenPlane(1, 1, 2, 2);
            water->model = gX_Model_LoadMesh(dummy);
        }
    }

    //opencode: load buoyancy demo sphere
    {
        gX_Mesh sphereMesh = gX_Mesh_GenSphere(water->buoyRadius, 16, 16);
        water->buoyModel = gX_Model_LoadMesh(sphereMesh);
    }

    // Load testcard or water texture
    {
        gX_Texture *_rsct = gX_Resource_LoadTexture("misc/testcard.png");
        if (_rsct) water->tex = *_rsct;
    }

    if (water->normalMapPath[0])
        water->normalMapTex = gX_Texture_Load(water->normalMapPath);
    water->model.materials[0].maps[gX_MATERIAL_MAP_ALBEDO].texture = water->tex;
    gX_Model_AttachShader(&water->model, water->shader);

    // Try loading sky cubemap for reflection in standalone mode
    if (!water->gfxRef)
    {
        gX_Texture *sky = gX_Resource_LoadTexture("skybox/skybox1.png");
        if (sky)
        {
            //opencode: store the 2D panorama for reflection via skyTex2D uniform.
            //   The water.frag shader checks skyUse2D — when true it samples the
            //   2D texture using equirectangular mapping from the reflection vector.
            water->skyCubemap = *sky;
            water->skyCubemapLoaded = true;
            gX_TraceLog(gX_LOG_INFO, "RENDERER_WATER: sky 2D panorama loaded, id=%d", water->skyCubemap.id);
        }
        else
        {
            gX_TraceLog(gX_LOG_WARNING, "RENDERER_WATER: skybox/skybox1.png not found — reflection will be black");
        }
    }

    gX_TraceLog(gX_LOG_INFO, "RENDERER_WATER: shader.id=%d model.meshCount=%d tex.id=%d normalMap.id=%d triCount=%d",
        water->shader.id, water->model.meshCount, water->tex.id, water->normalMapTex.id,
        water->model.meshCount > 0 ? water->model.meshes[0].triangleCount : -1);

    water->loadingstate.loaded = true;
}

//ai: invalidate and release experimental captures before their water resources disappear.
static gX_I0 water_gpu_unload(gX_I0 *arg)
{
    gX_RendererWater *water = (gX_RendererWater*)arg;
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    if (water->refractionCapture.id) gX_RenderTexture_Unload(water->refractionCapture);
    water->refractionCapture = (gX_RenderTexture){0};
    water->refractionValid = false;
#endif
    gX_Shader_Unload(water->shader);
    gX_Model_Unload(water->model);
    if (water->buoyModel.meshCount > 0) gX_Model_Unload(water->buoyModel);
    gX_Texture_Unload(water->tex);
    if (water->normalMapTex.id > 0) gX_Texture_Unload(water->normalMapTex);
    if (water->skyCubemapLoaded)
    {
        gX_Texture_Unload(water->skyCubemap);
        water->skyCubemapLoaded = false;
    }
    water->loadingstate.loaded = false;
}

//ai: periodic sources can stop independently while ripples decay smoothly at any frame rate.
gX_I0 gX_RendererWater_Update(gX_RendererWater *water)
{
    gX_LoadingState_Handler(&water->loadingstate, water_gpu_load, water_gpu_unload, water);
    if (!water->loadingstate.loaded) return;

    gX_F64 dt = gX_FrameTime_Get();

    // --- Ripple manager ---
    // Spawn new ripples periodically
    water->rippleTimer += (gX_F32)dt;
    //opencode: --lake calm water - no ripple rain on a still lake
    //   (buoy splash ripples still spawn explicitly).
    if (water->automaticRipples && !water->calmWater
        && water->rippleTimer > GX_WATER_RIPPLE_SPAWN_INTERVAL)
    {
        water->rippleTimer = 0.0f;
        if (water->rippleCount < GX_WATER_MAX_RIPPLES)
        {
            gX_I32 idx = water->rippleCount;
            water->ripplePos[idx] = (gX_F32v2){
                gX_F32_RandomRange(-GX_WATER_RIPPLE_POS_RANGE, GX_WATER_RIPPLE_POS_RANGE),
                gX_F32_RandomRange(-GX_WATER_RIPPLE_POS_RANGE, GX_WATER_RIPPLE_POS_RANGE)
            };
            water->rippleAmp[idx] = gX_F32_RandomRange(GX_WATER_RIPPLE_AMP_MIN, GX_WATER_RIPPLE_AMP_MAX);
            water->rippleRadius[idx] = GX_WATER_RIPPLE_INIT_RADIUS;
            water->ripplePhase[idx] = GX_WATER_RIPPLE_INIT_PHASE;
            water->rippleLifetime[idx] = GX_WATER_RIPPLE_INIT_LIFETIME;
            water->rippleCount++;
        }
    }

    // Update existing ripples
    gX_I32 writeIdx = 0;
    gX_F32 rippleFadeDuration = GX_WATER_RIPPLE_MAX_LIFETIME * 0.25f;
    gX_F32 rippleFadeStart = GX_WATER_RIPPLE_MAX_LIFETIME - rippleFadeDuration;
    for (gX_I32 i = 0; i < water->rippleCount; i++)
    {
        gX_F32 fadeBefore = 1.0f;
        if (water->rippleLifetime[i] > rippleFadeStart)
        {
            gX_F32 fadeProgress = (GX_WATER_RIPPLE_MAX_LIFETIME
                - water->rippleLifetime[i]) / rippleFadeDuration;
            if (fadeProgress < 0.0f) fadeProgress = 0.0f;
            if (fadeProgress > 1.0f) fadeProgress = 1.0f;
            fadeBefore = fadeProgress * fadeProgress * (3.0f - 2.0f * fadeProgress);
        }
        water->rippleLifetime[i] += (gX_F32)dt;
        water->ripplePhase[i] += GX_WATER_RIPPLE_ANGULAR_SPEED * (gX_F32)dt;
        water->rippleRadius[i] += GX_WATER_RIPPLE_EXPAND_SPEED * (gX_F32)dt;
        water->rippleAmp[i] *= powf(GX_WATER_RIPPLE_FADE_FACTOR, (gX_F32)dt * 60.0f);
        gX_F32 fadeAfter = 1.0f;
        if (water->rippleLifetime[i] > rippleFadeStart)
        {
            gX_F32 fadeProgress = (GX_WATER_RIPPLE_MAX_LIFETIME
                - water->rippleLifetime[i]) / rippleFadeDuration;
            if (fadeProgress < 0.0f) fadeProgress = 0.0f;
            if (fadeProgress > 1.0f) fadeProgress = 1.0f;
            fadeAfter = fadeProgress * fadeProgress * (3.0f - 2.0f * fadeProgress);
        }
        if (fadeBefore > 0.0f)
            water->rippleAmp[i] *= fadeAfter / fadeBefore;
        else
            water->rippleAmp[i] = 0.0f;

        if (water->rippleAmp[i] > GX_WATER_RIPPLE_AMP_THRESHOLD && water->rippleLifetime[i] < GX_WATER_RIPPLE_MAX_LIFETIME)
        {
            if (writeIdx != i)
            {
                water->ripplePos[writeIdx] = water->ripplePos[i];
                water->rippleAmp[writeIdx] = water->rippleAmp[i];
                water->rippleRadius[writeIdx] = water->rippleRadius[i];
                water->ripplePhase[writeIdx] = water->ripplePhase[i];
                water->rippleLifetime[writeIdx] = water->rippleLifetime[i];
            }
            writeIdx++;
        }
    }
    water->rippleCount = writeIdx;

    // --- Sun from GFX if available ---
    if (water->gfxRef)
    {
        gX_F32v3 gfxSunDir = water->gfxRef->sun.dir;
        water->sunDirection = (gX_F32v3){-gfxSunDir.x, -gfxSunDir.y, -gfxSunDir.z}; // negate = TO sun
        water->sunColorVec = (gX_F32v3){
            water->gfxRef->sun.color.r / 255.0f,
            water->gfxRef->sun.color.g / 255.0f,
            water->gfxRef->sun.color.b / 255.0f
        };
        water->sunBrightnessVal = water->gfxRef->sun.brightness;
    }

    //opencode: buoyancy demo — floating object follows wave height
    if (water->buoyModel.meshCount > 0)
    {
        gX_F32 waveH = gX_RendererWater_GetHeight(water, water->buoyPos.x, water->buoyPos.z);
        gX_F32 targetY = water->planeHeight + waveH - water->buoyRadius * 0.3f;
        gX_F32 disp = targetY - water->buoyPos.y;
        // Spring-damper: buoyancy force proportional to displacement
        water->buoyVel.y += disp * 3.0f * (gX_F32)dt;
        water->buoyVel.y *= 0.98f; // damping
        water->buoyPos.y += water->buoyVel.y * (gX_F32)dt;

        //opencode: spawn splash ripple + audio when object submerges
        if (!water->buoySplashDone && water->buoyPos.y < water->planeHeight + 0.1f)
        {
            gX_RendererWater_SpawnRipple(water, water->buoyPos.x, water->buoyPos.z, 0.5f);
            // Procedural splash sound (only in standalone mode)
            if (water->enableAudio) gX_Audio_Gen_Play(300.0f, 120, GX_AUDIO_WAVE_NOISE, 0.3f);
            water->buoySplashDone = true;
        }
    }

    //opencode: procedural wave ambient — low-frequency rumble (standalone only)
    if (water->enableAudio) {
        static gX_F32 waveAmbientTimer = 0.0f;
        waveAmbientTimer += (gX_F32)dt;
        if (waveAmbientTimer > 3.0f) {
            waveAmbientTimer = 0.0f;
            gX_F32 waveAmp = 0.0f;
            for (gX_I32 wi = 0; wi < water->waveCount && wi < GX_WATER_MAX_WAVES; wi++)
                waveAmp += water->waveAmplitudes[wi];
            gX_F32 freq = 80.0f + waveAmp * 40.0f;
            gX_F32 vol = 0.05f + waveAmp * 0.1f;
            if (vol > 0.3f) vol = 0.3f;
            gX_Audio_Gen_Play(freq, 400, GX_AUDIO_WAVE_SINE, vol);
        }
    }
}

//opencode: upload shader uniforms common to both Draw and DrawTo
//   Takes an explicit camera so standalone (waterCam override) and GFX
//   integration (DrawTo cam param) both use the correct view state.
//ai: upload procedural normal controls alongside authored normal-map inputs.
static gX_I0 water_upload_uniforms(gX_RendererWater *water, gX_Camera3D *cam)
{
    gX_Shader sha = water->shader;
    gX_F32 t = (gX_F32)maindata.timeElapsed;
    gX_F32v2 res = { (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight };

    // Standard uniforms — use the provided camera (waterCam, DrawTo cam, or global)
    gX_Shader_SetValue_Float(sha, water->shader_timeLoc, t);
    gX_Shader_SetValue_V2(   sha, water->shader_resLoc, res);
    gX_Shader_SetValue_V3(   sha, water->shader_camposLoc, cam->position);
    gX_Shader_SetValue_V3(   sha, water->shader_camtarLoc, cam->target);
    gX_Shader_SetValue_V3(   sha, water->shader_camupLoc, cam->up);
    gX_Shader_SetValue_Float(sha, water->shader_camfovLoc, cam->fovy);

    // Wave uniforms
    gX_Shader_SetValue_Int(sha, water->shader_waveCountLoc, water->waveCount);
    for (gX_I32 i = 0; i < water->waveCount && i < GX_WATER_MAX_WAVES; i++)
    {
        gX_F32v2 dir = { cosf(water->waveDirections[i]), sinf(water->waveDirections[i]) };
        if (water->shader_waveDirsLoc[i] >= 0)
            gX_Shader_SetValue_V2(sha, water->shader_waveDirsLoc[i], dir);
        if (water->shader_waveAmpsLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_waveAmpsLoc[i], water->waveAmplitudes[i]);
        if (water->shader_waveFreqsLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_waveFreqsLoc[i], water->waveFrequencies[i]);
        if (water->shader_waveSpeedsLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_waveSpeedsLoc[i], water->waveSpeeds[i]);
        if (water->shader_waveSteepsLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_waveSteepsLoc[i], water->waveSteepness[i]);
    }

    // Ripple uniforms
    gX_Shader_SetValue_Int(sha, water->shader_rippleCountLoc, water->rippleCount);
    for (gX_I32 i = 0; i < water->rippleCount && i < GX_WATER_MAX_RIPPLES; i++)
    {
        if (water->shader_ripplePosLoc[i] >= 0)
            gX_Shader_SetValue_V2(sha, water->shader_ripplePosLoc[i], water->ripplePos[i]);
        if (water->shader_rippleAmpLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_rippleAmpLoc[i], water->rippleAmp[i]);
        if (water->shader_rippleRadiusLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_rippleRadiusLoc[i], water->rippleRadius[i]);
        if (water->shader_ripplePhaseLoc[i] >= 0)
            gX_Shader_SetValue_Float(sha, water->shader_ripplePhaseLoc[i], water->ripplePhase[i]);
    }

    // Sky reflection — prefer cubemap (GFX's refl), fall back to 2D panorama
    gX_I1 skyIs2D = 0;
    if (water->gfxRef && water->gfxRef->reflection.cubeReady && water->shader_skyBoxLoc >= 0)
    {
        //opencode: v0.1.16 — GFX mode: when the procedural sky is active and
        //   the SSR sky LUT (real procedural sky, equirect, regenerated every
        //   frame) is loaded, use the 2D panorama path — sharper than the
        //   512² dynamic cubemap (re-rendered every 2nd frame) and unaffected
        //   by its face seams. 1-frame stale: the LUT is generated after the
        //   water draw in the pipeline.
        if (water->gfxRef->state.ENABLE_PROCEDURAL_SKY &&
            water->gfxRef->ssr.skyLut.wrapper.loadingState.loaded &&
            water->shader_skyTex2DLoc >= 0)
        {
            gX_Shader_SetValue_TextureSlot(sha, water->shader_skyTex2DLoc,
                water->gfxRef->ssr.skyLut.wrapper.rt.texture, 9);
            skyIs2D = 1;
        }
        else
        {
            gX_Shader_SetValue_CubemapSlot(sha, water->shader_skyBoxLoc,
                water->gfxRef->reflection.dynamicCubemap, 7);
        }
    }
    else if (water->skyCubemapLoaded && water->skyCubemap.id > 0)
    {
        //opencode: standalone mode uses a 2D panorama loaded as skyCubemap.
        //   Bind it as a 2D texture and tell the shader to use equirectangular
        //   sampling via skyUse2D=1. The samplerCube skyBox will be unbound but
        //   won't be sampled since the shader checks skyUse2D first.
        if (water->shader_skyTex2DLoc >= 0)
            gX_Shader_SetValue_TextureSlot(sha, water->shader_skyTex2DLoc, water->skyCubemap, 9);
        skyIs2D = 1;
    }
    if (water->shader_skyUse2DLoc >= 0)
        gX_Shader_SetValue_Int(sha, water->shader_skyUse2DLoc, skyIs2D ? 1 : 0);

    if (water->shader_underwaterLoc >= 0)
        gX_Shader_SetValue_Int(sha, water->shader_underwaterLoc,
            (cam->position.y < water->planeHeight + 0.3f) ? 1 : 0);

    // Normal map texture + uniforms
    if (water->normalMapTex.id > 0 && water->shader_normalMapLoc >= 0)
    {
        gX_Shader_SetValue_TextureSlot(sha, water->shader_normalMapLoc, water->normalMapTex, 1);
    }
    if (water->shader_normalStrengthLoc >= 0)
        gX_Shader_SetValue_Float(sha, water->shader_normalStrengthLoc,
            water->normalMapTex.id > 0 ? water->normalMapStrength : 0.0f);
    if (water->shader_normalScaleLoc >= 0)
        gX_Shader_SetValue_V2(sha, water->shader_normalScaleLoc, water->normalMapScale);
    if (water->shader_proceduralNormalStrengthLoc >= 0)
        gX_Shader_SetValue_Float(sha, water->shader_proceduralNormalStrengthLoc,
            water->proceduralNormalStrength);
    if (water->shader_proceduralNormalScaleLoc >= 0)
        gX_Shader_SetValue_Float(sha, water->shader_proceduralNormalScaleLoc,
            water->proceduralNormalScale);
    if (water->shader_normalScrollLoc >= 0)
        gX_Shader_SetValue_V2(sha, water->shader_normalScrollLoc, water->normalMapScroll);
    if (water->shader_tintLoc >= 0)
        gX_Shader_SetValue_V3(sha, water->shader_tintLoc,
            (gX_F32v3){water->tint.r / 255.0f,
                water->tint.g / 255.0f, water->tint.b / 255.0f});

    // Sun uniforms
    if (water->shader_sunDirLoc >= 0)
    {
        gX_F32v3 sDir = water->sunDirection;
        gX_Shader_SetValue_V3(sha, water->shader_sunDirLoc, sDir);
    }
    if (water->shader_sunColorLoc >= 0)
        gX_Shader_SetValue_V3(sha, water->shader_sunColorLoc, water->sunColorVec);
    if (water->shader_sunBrightLoc >= 0)
        gX_Shader_SetValue_Float(sha, water->shader_sunBrightLoc, water->sunBrightnessVal);
}

//opencode: RHI mesh draw — bypasses raylib_Mesh_Draw (broken on GLFW Core Profile).
//   Uploads matModel/matView/matProjection via raw GL (avoids raylib's rlgl matrix
//   stack which may be out of sync with RHI stack). Uses gX_glfw_Mesh_Draw for the
//   actual VAO draw — the same raw draw the GFX scene uses.
static gX_I0 water_mesh_draw(gX_RendererWater *water, gX_Matrix *viewMat, gX_Matrix *projMat) {
    gX_Matrix mModel = gX_Matrix_Translate(0, water->planeHeight, 0);
    gX_RHI_Shader_Use(water->shader.id);
    if (water->shader_matModelLoc >= 0)
        gX_RHI_UniformMatrix(water->shader_matModelLoc, 1, 1, &mModel.m0);
    if (water->shader_matViewLoc >= 0 && viewMat)
        gX_RHI_UniformMatrix(water->shader_matViewLoc, 1, 1, &viewMat->m0);
    if (water->shader_matProjLoc >= 0 && projMat)
        gX_RHI_UniformMatrix(water->shader_matProjLoc, 1, 1, &projMat->m0);

    //opencode: draw via custom VAO (bypasses raylib mesh upload entirely)
    if (water->customVao && water->customTriCount > 0) {
        gX_RHI_VAO_Bind(water->customVao);
        gX_RHI_DrawElements(gX_RHI_TRIANGLES, (gX_U32)(water->customTriCount * 3), gX_RHI_UNSIGNED_SHORT, NULL);
        gX_RHI_VAO_Bind(0);
    }
}

gX_I0 gX_RendererWater_DrawVelocity(gX_RendererWater *water,
                                    gX_Matrix *currVP, gX_Matrix *prevVP,
                                    gX_I1 hasPrevVP)
{
    extern gX_Camera camera;
    gX_Matrix fallbackVP;
    gX_F32 prevTime;
    gX_I1 depthWasEnabled;
    if (!water || !water->loadingstate.loaded || !currVP || !water->customVao
        || water->customTriCount <= 0) return;
    fallbackVP = *currVP;
    if (!hasPrevVP || !prevVP) prevVP = &fallbackVP;
    prevTime = (gX_F32)maindata.timeElapsed - (gX_F32)gX_FrameTime_Get();
    if (prevTime < 0.0f) prevTime = (gX_F32)maindata.timeElapsed;
    depthWasEnabled = gX_RHI_IsEnabled(gX_RHI_DEPTH_TEST);
    gX_RHI_Disable(gX_RHI_DEPTH_TEST);
    gX_RHI_Shader_Use(water->shader.id);
    water_upload_uniforms(water, &camera.camera3D);
    if (water->shader_velocityModeLoc >= 0)
        gX_RHI_UniformInt(water->shader_velocityModeLoc, 1);
    if (water->shader_velocityCurrVPLoc >= 0)
        gX_RHI_UniformMatrix(water->shader_velocityCurrVPLoc, 1, 1, &currVP->m0);
    if (water->shader_velocityPrevVPLoc >= 0)
        gX_RHI_UniformMatrix(water->shader_velocityPrevVPLoc, 1, 1, &prevVP->m0);
    if (water->shader_velocityPrevTimeLoc >= 0)
        gX_RHI_Uniform_Float(water->shader_velocityPrevTimeLoc, prevTime);
    water_mesh_draw(water, NULL, NULL);
    if (water->shader_velocityModeLoc >= 0)
        gX_RHI_UniformInt(water->shader_velocityModeLoc, 0);
    if (depthWasEnabled) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
}

gX_I0 gX_RendererWater_Draw(gX_RendererWater *water)
{
    if (!water->loadingstate.loaded) return;

    //opencode: use the global camera (controllable via WASD/mouse in standalone).
    //   Initial position/target is set when the water scene loads (see input.c).
    //   Previously this overrode the camera with hardcoded values, which broke
    //   user input. Now Water_Update in Update can apply input-driven movement.
    gX_Camera3D *waterCam = &camera.camera3D;

    //opencode: set camera to look at water plane when standalone (not GFX-integrated)
    if (!water->gfxRef) {
        waterCam->position = (gX_F32v3){0.0f, 3.0f, 3.0f};
        waterCam->target = (gX_F32v3){0.0f, -0.6f, 0.0f};
        waterCam->fovy = 60.0f;
    }

    //opencode: draw water to canvas (like wireframe/clock) so Scene template composites it
    gX_Drawing_BeginTexture(renderer.canvas);
        gX_Drawing_ClearBG(gX_BLACK);
        gX_Drawing_Begin3D(*waterCam);
            water_upload_uniforms(water, waterCam);

            //opencode: underwater — double-sided mesh + culling disabled so back faces render
            gX_I1 isUnderwater = (waterCam->position.y < water->planeHeight + 0.3f);
            if (isUnderwater) gX_Drawing_DisableCulling();

            //opencode: RHI draw — bypasses raylib_Mesh_Draw (broken on GLFW Core Profile)
            {
                gX_Matrix viewMat, projMat;
                viewMat = gX_RHI_Matrix_GetModelview();
                projMat = gX_RHI_Matrix_GetProjection();
                water_mesh_draw(water, &viewMat, &projMat);
            }

            if (isUnderwater) gX_Drawing_EnableCulling();

            //opencode: buoyancy demo — floating sphere using default shader
            if (water->buoyModel.meshCount > 0)
            {
                gX_Model_DrawEx(water->buoyModel, water->buoyPos,
                    (gX_F32v3){0, 1, 0}, maindata.timeElapsed * 30.0f,
                    (gX_F32v3){1, 1, 1}, (gX_Color){180, 70, 30, 255});
            }
        gX_Drawing_End3D();
    gX_Drawing_EndTexture();
}

#ifdef GX_WATER_REFRACTION_EXPERIMENT
//ai: launch overrides remain diagnostic; authored state drives normal editor sessions without an override.
static gX_I1 water_refraction_enabled(gX_RendererWater *Water)
{
    const gX_I8 *Hook = getenv("GX_WATER_REFRACTION");
    return Hook ? strcmp(Hook, "1") == 0 : Water->sceneRefractionEnabled;
}

//ai: invalidate first; copy a matching color/depth pair before water writes either source attachment.
static gX_I0 water_refraction_capture(gX_RendererWater *Water, gX_RenderTexture *Target,
    gX_Camera3D *Cam)
{
    Water->refractionValid = false;
    if (!water_refraction_enabled(Water) || !Water->gfxRef
        || Cam->position.y < Water->planeHeight + 0.3f
        || getenv("GX_WATER_REFRACTION_MISSING")) return;
    if (!Target->texture.id || !Target->depth.id) return;
    if (gX_Shader_GetLocation(Water->shader, "waterSceneValid") < 0) return;
    GX_PROFILE_ZONE_BEGIN("water:refraction_capture");
    gX_Batch_Flush();
    gX_I32 Draw = 0, Read = 0;
    gX_RHI_GetIntegerv(gX_RHI_DRAW_FRAMEBUFFER_BINDING, &Draw);
    gX_RHI_GetIntegerv(gX_RHI_READ_FRAMEBUFFER_BINDING, &Read);
    if (Water->refractionCapture.texture.width != Target->texture.width
        || Water->refractionCapture.texture.height != Target->texture.height)
    {
        if (Water->refractionCapture.id) gX_RenderTexture_Unload(Water->refractionCapture);
        Water->refractionCapture = gX_RenderTexture_Load_HDR(Target->texture.width, Target->texture.height);
        gX_RHI_InvalidateFBO();
        if (getenv("GX_WATER_REFRACTION_TEST_TRANSITION"))
            gX_TraceLog(gX_LOG_NOTICE, "WATER_REFRACTION_CAPTURE_RESIZE: frame=%llu size=%ux%u",
                (unsigned long long)maindata.frameCount, Target->texture.width, Target->texture.height);
        if (Water->refractionCapture.texture.id)
        {
            gX_RHI_Tex_Param(Water->refractionCapture.texture.id, gX_RHI_TEXTURE_2D,
                gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(Water->refractionCapture.texture.id, gX_RHI_TEXTURE_2D,
                gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
        }
    }
    if (Water->refractionCapture.id && Water->refractionCapture.depth.id)
    {
        gX_RHI_BindFBO(Water->refractionCapture.id);
        if (gX_RHI_FBO_IsComplete())
        {
            gX_RHI_Blit(Target->id, Water->refractionCapture.id, 0, 0,
                Target->texture.width, Target->texture.height, 0, 0,
                Target->texture.width, Target->texture.height,
                gX_RHI_COLOR_BUFFER_BIT | gX_RHI_DEPTH_BUFFER_BIT, gX_RHI_NEAREST);
            Water->refractionValid = true;
        }
    }
    gX_RHI_BindDrawFBO((gX_U32)Draw);
    gX_RHI_BindReadFBO((gX_U32)Read);
    GX_PROFILE_ZONE_END("water:refraction_capture");
}

//ai: measured blend eligibility gates transmission; depth foam stays separately hook-gated.
static gX_I0 water_refraction_uniforms(gX_RendererWater *Water, gX_Matrix *View,
    gX_Matrix *Projection)
{
    gX_I32 Src = 0, Dst = 0, Equation = 0;
    gX_RHI_GetIntegerv(gX_RHI_BLEND_SRC_RGB, &Src);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_DST_RGB, &Dst);
    gX_RHI_GetIntegerv(gX_RHI_BLEND_EQUATION_RGB, &Equation);
    gX_I1 Blend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_I1 Valid = Water->refractionValid && View && Projection && Blend
        && Src == gX_RHI_SRC_ALPHA && Dst == gX_RHI_ONE_MINUS_SRC_ALPHA
        && Equation == gX_RHI_FUNC_ADD;
    const gX_I8 *PixelsHook = getenv("GX_WATER_REFRACTION_PIXELS");
    gX_F32 Pixels = PixelsHook ? strtof(PixelsHook, NULL) : 4.0f;
    if (!isfinite(Pixels)) Pixels = 0.0f;
    Pixels = fminf(4.0f, fmaxf(0.0f, Pixels));
    gX_U32 Shader = Water->shader.id;
    gX_RHI_Shader_Use(Shader);
    gX_I32 ValidLoc = gX_Shader_GetLocation(Water->shader, "waterSceneValid");
    if (ValidLoc < 0) return;
    gX_RHI_UniformInt(ValidLoc, Valid ? 1 : 0);
    gX_RHI_UniformInt(gX_Shader_GetLocation(Water->shader, "waterSceneDiagnostic"),
        getenv("GX_WATER_REFRACTION_DIAGNOSTIC") ? 1 : 0);
    const gX_I8 *ContactFoamHook = getenv("GX_WATER_DEPTH_FOAM");
    gX_I32 ContactFoamLoc = gX_Shader_GetLocation(Water->shader, "waterDepthFoamEnabled");
    if (ContactFoamLoc >= 0)
        gX_RHI_UniformInt(ContactFoamLoc,
            Valid && ContactFoamHook && strcmp((const char *)ContactFoamHook, "1") == 0 ? 1 : 0);
    gX_RHI_Uniform_Float(gX_Shader_GetLocation(Water->shader, "waterPlaneHeight"), Water->planeHeight);
    gX_RHI_Uniform_Float(gX_Shader_GetLocation(Water->shader, "waterOffsetPixels"), Pixels);
    gX_RHI_UniformInt(gX_Shader_GetLocation(Water->shader, "waterSceneColor"), gX_GFX_TEXUNIT_SCENE);
    gX_RHI_UniformInt(gX_Shader_GetLocation(Water->shader, "waterSceneDepth"), gX_GFX_TEXUNIT_DEPTH);
    if (Valid)
    {
        gX_Matrix Inverse = gX_Matrix_Invert(gX_Matrix_Multiply(*View, *Projection));
        if (getenv("GX_WATER_REFRACTION_INVALID")) Inverse = (gX_Matrix){0};
        gX_RHI_UniformMatrix(gX_Shader_GetLocation(Water->shader, "waterInverseVP"), 1, 1, &Inverse.m0);
        gX_RHI_Tex_BindOnUnit(Water->refractionCapture.texture.id, gX_GFX_TEXUNIT_SCENE);
        gX_RHI_Tex_BindOnUnit(Water->refractionCapture.depth.id, gX_GFX_TEXUNIT_DEPTH);
    }
    if (!Water->refractionTraced)
    {
        gX_TraceLog(gX_LOG_NOTICE, "WATER_REFRACTION: blend=%d src=%d dst=%d equation=%d valid=%d pixels=%.2f",
            Blend, Src, Dst, Equation, Valid, Pixels);
        Water->refractionTraced = true;
    }
}
#endif

//ai: experimental capture precedes BeginTexture/draw; additional sampler state is restored afterwards.
#ifdef GX_WATER_REFRACTION_EXPERIMENT
//ai: opt-in float evidence retains the exact capture, matrices and pre-tonemap diagnostics for an independent oracle.
static gX_I0 water_refraction_dump(gX_RendererWater *Water, gX_RenderTexture *Target,
    gX_Matrix *View, gX_Matrix *Projection)
{
    const gX_I8 *Path = getenv("GX_WATER_REFRACTION_DUMP");
    if (!Path || !Water->refractionValid || !View || !Projection || maindata.frameCount != 29) return;
    gX_U32 Width = Target->texture.width, Height = Target->texture.height;
    gX_U64 Count = (gX_U64)Width * Height;
    if (Count > UINT32_MAX / (9 * sizeof(gX_F32))) return;
    gX_F32 *Data = gX_Malloc((gX_U32)(Count * 9 * sizeof(gX_F32)));
    if (!Data) return;
    gX_I32 Read = 0;
    gX_RHI_GetIntegerv(gX_RHI_READ_FRAMEBUFFER_BINDING, &Read);
    gX_RHI_BindReadFBO(Water->refractionCapture.id);
    gX_RHI_ReadPixels(0, 0, Width, Height, gX_RHI_RGBA, gX_RHI_FLOAT, Data);
    gX_RHI_ReadPixels(0, 0, Width, Height, gX_RHI_DEPTH_COMPONENT, gX_RHI_FLOAT, Data + Count * 4);
    gX_RHI_BindReadFBO(Target->id);
    gX_RHI_ReadPixels(0, 0, Width, Height, gX_RHI_RGBA, gX_RHI_FLOAT, Data + Count * 5);
    gX_RHI_BindReadFBO((gX_U32)Read);
    gX_Matrix Inverse = gX_Matrix_Invert(gX_Matrix_Multiply(*View, *Projection));
    if (getenv("GX_WATER_REFRACTION_INVALID")) Inverse = (gX_Matrix){0};
    FILE *File = fopen(Path, "wb");
    if (File)
    {
        fwrite("WTRF0001", 1, 8, File);
        fwrite(&Width, sizeof(Width), 1, File);
        fwrite(&Height, sizeof(Height), 1, File);
        fwrite(&Inverse.m0, sizeof(gX_F32), 16, File);
        fwrite(&View->m0, sizeof(gX_F32), 16, File);
        fwrite(&Water->planeHeight, sizeof(gX_F32), 1, File);
        fwrite(Data, sizeof(gX_F32), Count * 9, File);
        fclose(File);
    }
    gX_Free(Data);
}
#endif

//ai: experimental capture precedes BeginTexture/draw; additional sampler state is restored afterwards.
gX_I0 gX_RendererWater_DrawTo(gX_RendererWater *water, gX_RenderTexture *target,
                               gX_Camera3D *cam, gX_Matrix *viewMat, gX_Matrix *projMat)
{
    if (!water->loadingstate.loaded) return;
    if (!target || target->id <= 0) return;
    if (!cam) cam = &camera.camera3D;

#ifdef GX_WATER_REFRACTION_EXPERIMENT
    gX_I32 Active = 0, Scene = 0, Depth = 0, Zero = 0;
    gX_RHI_GetIntegerv(gX_RHI_ACTIVE_TEXTURE, &Active);
    gX_RHI_ActiveTexture(0);
    gX_RHI_GetIntegerv(gX_RHI_TEXTURE_BINDING_2D, &Zero);
    gX_RHI_ActiveTexture(gX_GFX_TEXUNIT_SCENE);
    gX_RHI_GetIntegerv(gX_RHI_TEXTURE_BINDING_2D, &Scene);
    gX_RHI_ActiveTexture(gX_GFX_TEXUNIT_DEPTH);
    gX_RHI_GetIntegerv(gX_RHI_TEXTURE_BINDING_2D, &Depth);
    gX_RHI_ActiveTexture((gX_U32)Active - gX_RHI_TEXTURE0);
    water_refraction_capture(water, target, cam);
#endif

    //opencode: DrawTo doesn't use Begin3D, so upload view/proj/matModel
    //   explicitly, then draw via RHI mesh draw (bypasses raylib_Mesh_Draw).
    gX_Drawing_BeginTexture(*target);
        water_upload_uniforms(water, cam);

#ifdef GX_WATER_REFRACTION_EXPERIMENT
        water_refraction_uniforms(water, viewMat, projMat);
#endif

        water_mesh_draw(water, viewMat, projMat);
#ifdef GX_WATER_REFRACTION_EXPERIMENT
        water_refraction_dump(water, target, viewMat, projMat);
#endif

    gX_Drawing_EndTexture();
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    if (getenv("GX_WATER_REFRACTION_TEST_TOGGLE"))
        gX_TraceLog(gX_LOG_NOTICE,
            "WATER_REFRACTION_TOGGLE: frame=%llu authored=%d valid=%d shader=%u",
            (unsigned long long)maindata.frameCount, water->sceneRefractionEnabled,
            water->refractionValid, water->shader.id);
    gX_RHI_Tex_BindOnUnit((gX_U32)Zero, 0);
    gX_RHI_Tex_BindOnUnit((gX_U32)Scene, gX_GFX_TEXUNIT_SCENE);
    gX_RHI_Tex_BindOnUnit((gX_U32)Depth, gX_GFX_TEXUNIT_DEPTH);
    gX_RHI_ActiveTexture((gX_U32)Active - gX_RHI_TEXTURE0);
#endif
}

gX_I0 gX_RendererWater_SpawnRipple(gX_RendererWater *water, gX_F32 x, gX_F32 z,
                                    gX_F32 amplitude)
{
    if (water->rippleCount >= GX_WATER_MAX_RIPPLES) return;
    gX_I32 idx = water->rippleCount;
    water->ripplePos[idx] = (gX_F32v2){x, z};
    water->rippleAmp[idx] = amplitude;
    water->rippleRadius[idx] = GX_WATER_RIPPLE_SPAWN_RADIUS;
    water->ripplePhase[idx] = 0.0f;
    water->rippleLifetime[idx] = 0.0f;
    water->rippleCount++;
}

//opencode: evaluate Gerstner wave + ripple height at a world (x,z) position.
//   Replicates the vertex shader math on the CPU for buoyancy, displacement, etc.
//   Returns the wave height offset to add to the base planeHeight.
gX_F32 gX_RendererWater_GetHeight(gX_RendererWater *water, gX_F32 x, gX_F32 z)
{
    if (!water || !water->loadingstate.loaded) return 0.0f;
    gX_F32 t = (gX_F32)maindata.timeElapsed;
    gX_F32v2 pos = {x, z};
    gX_F32 totalY = 0.0f;

    // Gerstner wave sum
    for (gX_I32 i = 0; i < water->waveCount && i < GX_WATER_MAX_WAVES; i++)
    {
        gX_F32v2 dir = { cosf(water->waveDirections[i]), sinf(water->waveDirections[i]) };
        gX_F32 theta = water->waveFrequencies[i] * (dir.x * pos.x + dir.y * pos.y)
                     + t * water->waveSpeeds[i];
        totalY += water->waveAmplitudes[i] * sinf(theta);
    }

    // Ripple sum
    for (gX_I32 i = 0; i < water->rippleCount && i < GX_WATER_MAX_RIPPLES; i++)
    {
        gX_F32v2 delta = { pos.x - water->ripplePos[i].x, pos.y - water->ripplePos[i].y };
        gX_F32 dist = sqrtf(delta.x * delta.x + delta.y * delta.y);
        if (dist < water->rippleRadius[i])
        {
            gX_F32 r = dist / water->rippleRadius[i];
            gX_F32 envelope = 1.0f - r * r;
            gX_F32 wave = sinf(dist * 3.0f - water->ripplePhase[i]);
            totalY += water->rippleAmp[i] * wave * envelope;
        }
    }

    return totalY;
}
