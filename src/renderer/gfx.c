/**********************************************************************************************
*
*   [gX: src/renderer/gfx.c]
*   gX — a game engine
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


//opencode: GFX renderer orchestrator — delegates to gfx/* subsystem modules
//   Load/Unload remain here; DrawTo moved to gfx/gfx_pipeline.c,
//   Render_Draw moved to gfx/gfx_render.c.

#include <string.h>
#include <math.h>

#include "gfx.h"
#include "gfx/schema.h"
#include "renderer.h"

#include "../wrappers/wrappers.h"

#include "../main.h"
#include "../camera.h"

#include "../common/log.h"
#include "../common/macros.h"
#include "../common/profiler.h"
#include "../common/path.h"
#include "../common/utils.h"

#include "../gxmath/matrix.h"
#include "../gxmath/constants.h"

//opencode: gX — game/ephemeris dropped
//#include "../game/game.h"
#include "cubemap.h"

//opencode: subsystem includes
#include "gfx/boilerplate.h"
#include "gfx/load.h"
#include "gfx/draw.h"
#include "gfx/decal.h"
#include "gfx/scene.h"
#include "gfx/bloom.h"
#include "gfx/dof.h"
#include "gfx/taa.h"
#include "gfx/motionblur.h"
#include "gfx/gl_level.h"
//ai: optional profiling resources follow renderer/context teardown.
#include "gfx/profile.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_tex.h"

//opencode: Draw alias — writes to canvas
#define gX_RendererGFX_Draw(gfx) gX_RendererGFX_DrawTo(gfx, NULL)

//opencode: GL state dump — gX_GL_DumpState/gX_GL_CheckError now live in
//   src/rhi/rhi_state.c (moved out of the harness).

//opencode: texture-unit budget validation — every gX_GFX_TEXUNIT_* constant
//   must be < 32 (GL_MAX_TEXTURE_IMAGE_UNITS). The old CSM=52/COOKIE=64
//   values exceeded the limit and were silently dropped by the driver
//   (samplers never bound). Units are per-PROGRAM bindings (each pass
//   rebinds its samplers before drawing), so the same number in different
//   programs is legal: FOG_LUT/VELOCITY share 21, FOG_CSM1/SSGI_BLUR share
//   26, FOG_CSM2/SSGI_PREV share 28. Only same-program duplicates are
//   violations. Scope tags: program that binds the sampler (audited
//   2026-09-10 against the per-function bind tables). Returns violations.
static gX_I32 gX_GFX_TexUnit_Validate(gX_I0)
{
    struct { const gX_I8 *name; gX_U32 unit; const gX_I8 *prog; } units[] = {
        {"SHADOW",        gX_GFX_TEXUNIT_SHADOW,         "MAIN"},
        {"SKYBOX",        gX_GFX_TEXUNIT_SKYBOX,         "MAIN"},
        {"CSM_ARRAY",     gX_GFX_TEXUNIT_CSM_ARRAY,      "MAIN"},
        {"COOKIE",        gX_GFX_TEXUNIT_COOKIE,         "MAIN"},
        {"SSAO",          gX_GFX_TEXUNIT_SSAO,           "MAIN"},
        {"GLOSS",         gX_GFX_TEXUNIT_GLOSS,          "MAIN"},
        {"REFRACTION",    gX_GFX_TEXUNIT_REFRACTION,     "MAIN"},
        {"DETAIL",        gX_GFX_TEXUNIT_DETAIL,         "MAIN"},
        {"IBL_IRRADIANCE",gX_GFX_TEXUNIT_IBL_IRRADIANCE, "MAIN"},
        {"IBL_PREFILTER", gX_GFX_TEXUNIT_IBL_PREFILTER,  "MAIN"},
        {"IBL_BRDF",      gX_GFX_TEXUNIT_IBL_BRDF,       "MAIN"},
        {"REFLECTION",    gX_GFX_TEXUNIT_REFLECTION,     "MAIN"},
        {"POINT_SHADOW",  gX_GFX_TEXUNIT_POINT_SHADOW,   "MAIN"},
        {"FOG_LUT",       gX_GFX_TEXUNIT_FOG_LUT,        "MAIN"},
        {"PROBE_B",       gX_GFX_TEXUNIT_PROBE_B,        "MAIN"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "FOGGEN"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "FOGGEN"},
        {"SHADOW",        gX_GFX_TEXUNIT_SHADOW,         "FOGGEN"},
        {"FOG_CSM1",      gX_GFX_TEXUNIT_FOG_CSM1,       "FOGGEN"},
        {"FOG_CSM2",      gX_GFX_TEXUNIT_FOG_CSM2,       "FOGGEN"},
        {"FOG_CSM3",      gX_GFX_TEXUNIT_FOG_CSM3,       "FOGGEN"},
        {"COLOR",         gX_GFX_TEXUNIT_COLOR,          "SSR"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "SSR"},
        {"FOG_LUT",       gX_GFX_TEXUNIT_FOG_LUT,        "SSR"},
        {"HIZ",           gX_GFX_TEXUNIT_HIZ,            "SSR"},
        {"SKY_LUT",       gX_GFX_TEXUNIT_SKY_LUT,        "SSR"},
        {"SSR_ENV",       gX_GFX_TEXUNIT_SSR_ENV,        "SSR"},
        {"SSR_PLANAR",    gX_GFX_TEXUNIT_SSR_PLANAR,     "SSR"},
        {"SSR_PLANAR_D",  gX_GFX_TEXUNIT_SSR_PLANAR_DEPTH,"SSR"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "SSGI"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "SSGI"},
        {"SSGI_RAW",      gX_GFX_TEXUNIT_SSGI_RAW,       "SSGI"},
        {"SSGI_BLUR",     gX_GFX_TEXUNIT_SSGI_BLUR,      "SSGI"},
        {"SSGI_HIST",     gX_GFX_TEXUNIT_SSGI_HIST,      "SSGI"},
        {"SSGI_PREV",     gX_GFX_TEXUNIT_SSGI_PREV,      "SSGI"},
        {"VELOCITY",      gX_GFX_TEXUNIT_VELOCITY,       "SSGI"},
        {"COLOR",         gX_GFX_TEXUNIT_COLOR,          "TAA"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "TAA"},
        {"AUX",           gX_GFX_TEXUNIT_AUX,            "TAA"},
        {"VELOCITY",      gX_GFX_TEXUNIT_VELOCITY,       "TAA"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "DOF"},
        {"COLOR",         gX_GFX_TEXUNIT_COLOR,          "DOF"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "DOF"},
        {"AUX",           gX_GFX_TEXUNIT_AUX,            "DOF"},
        {"COLOR",         gX_GFX_TEXUNIT_COLOR,          "MOTION"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "MOTION"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "BLOOMC"},
        {"BLOOM_BLUR",    gX_GFX_TEXUNIT_BLOOM_BLUR,     "BLOOMC"},
        {"SSGI_HIST",     gX_GFX_TEXUNIT_SSGI_HIST,      "BLOOMC"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "GODRAY"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "LENS"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "LENS"},
        {"SCENE",         gX_GFX_TEXUNIT_SCENE,          "WATER"},
        {"DEPTH",         gX_GFX_TEXUNIT_DEPTH,          "WATER"},
    };
    gX_I32 violations = 0;
    gX_I8 map[1024];
    gX_I32 mlen = 0;
    for (gX_U32 i = 0; i < sizeof(units)/sizeof(units[0]); i++) {
        if (units[i].unit >= 32) {
            gX_TraceLog(gX_LOG_ERROR, "GFX_TEXUNIT: %s.%s = %u EXCEEDS 31 — sampler will never bind",
                units[i].prog, units[i].name, units[i].unit);
            violations++;
        }
        for (gX_U32 j = i + 1; j < sizeof(units)/sizeof(units[0]); j++) {
            if (units[i].unit == units[j].unit
                && !strcmp(units[i].prog, units[j].prog)) {
                gX_TraceLog(gX_LOG_ERROR, "GFX_TEXUNIT: %s.%s and %s.%s both use unit %u",
                    units[i].prog, units[i].name, units[j].prog, units[j].name, units[i].unit);
                violations++;
            }
        }
        mlen += snprintf(map + mlen, sizeof(map) - (gX_U64)mlen, " %s.%s=%u", units[i].prog, units[i].name, units[i].unit);
    }
    //opencode: point shadows share ONE 2D-array texture (layers =
    //   light*6+face, single bind at unit 20) - the old per-light unit
    //   budget (20+li, 11-light cap warning) died with the cubemap path.
    //   The live budget is array layers, checked where the array is sized.
    gX_TraceLog(gX_LOG_INFO, "GFX_TEXUNIT: unit map (max 31):%s%s",
        map, violations ? " — VIOLATIONS!" : "");
    return violations;
}

//opencode: Load — set defaults, init heaps, load shaders/RTs/demo scene
//ai: initialize the GFX water height once; subsequent updates preserve author edits.
gX_I32 gX_RendererGFX_Load(gX_RendererGFX *gfx) {
    gfx->profile = NULL;
    gfx->profileAttempted = 0;
    
    strcpy(gfx->name,"gfx");
    strcpy(gfx->description,"graphics rendering");

    //opencode: detect backend GL capabilities before deferred GPU paths query
    //   gX_GL.hasCompute/hasSSBO and geometry-shader availability.
    gX_GL_Detect();
    
    gX_RendererGFX_Load_SetDefaults(gfx);
    gX_RendererGFX_OracleGPU_Init(gfx);
    gX_GFX_TexUnit_Validate();
    
    //opencode: skybox texture path — loaded on demand when ENABLE_SKYBOX is set
    gX_RendererGFX_Texture_Init(&gfx->sky.skybox);
    strcpy(gfx->sky.skybox.wrapper.path,GX_PATH("texture", "skybox/skybox1.png"));
    strcpy(gfx->sky.skyboxModelPath,GX_PATH("model", "primitive/cube.glb"));
    strcpy(gfx->sky.skyboxPath,GX_PATH("texture", "skybox/skybox1.png"));
    gfx->sky.skybox.wrapper.isSkybox = true;
    gfx->sky.skybox.wrapper.skyboxLayout = gX_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE;
    
    gX_RendererGFX_Light_Init(&gfx->sun);
    
    //opencode: initialize dynamic heaps (essential)
    gX_Heap_Zero(&gfx->models);
    gX_Heap_SetBlockSize(&gfx->models, sizeof(gX_RendererGFX_Model));
    gX_Heap_SetMax(&gfx->models, gX_GFX_MODELS_HEAP_MAX);
    gX_Heap_SetAmount(&gfx->models, 0);
    gX_Heap_Init(&gfx->models);
    
    gX_Heap_Zero(&gfx->lights);
    gX_Heap_SetBlockSize(&gfx->lights, sizeof(gX_RendererGFX_Light));
    gX_Heap_SetMax(&gfx->lights, gX_GFX_LIGHTS_HEAP_MAX);
    gX_Heap_SetAmount(&gfx->lights, 0);
    gX_Heap_Init(&gfx->lights);
    
    gX_Heap_Zero(&gfx->textures);
    gX_Heap_SetBlockSize(&gfx->textures, sizeof(gX_RendererGFX_Texture));
    gX_Heap_SetMax(&gfx->textures, gX_GFX_TEXTURES_HEAP_MAX);
    gX_Heap_SetAmount(&gfx->textures, 0);
    gX_Heap_Init(&gfx->textures);
    
    gX_Heap_Zero(&gfx->maps);
    gX_Heap_SetBlockSize(&gfx->maps, sizeof(gX_RendererGFX_RenderTexture));
    gX_Heap_SetMax(&gfx->maps, gX_GFX_MAPS_HEAP_MAX);
    gX_Heap_SetAmount(&gfx->maps, 1);
    gX_Heap_Init(&gfx->maps);

    //opencode: decals heap (was missing: Decal_Add silently failed forever)
    gX_Heap_Zero(&gfx->decals);
    gX_Heap_SetBlockSize(&gfx->decals, sizeof(gX_RendererGFX_Decal));
    gX_Heap_SetMax(&gfx->decals, gX_GFX_DECALS_HEAP_MAX);
    gX_Heap_SetAmount(&gfx->decals, 0);
    gX_Heap_Init(&gfx->decals);
    

    
    //opencode: init core shaders (standard + depth + procedural sky)
    gX_RendererGFX_Shader_Init(&gfx->shader, GX_PATH("shader", "standard.vert"), GX_PATH("shader", "standard.frag"));
    gX_RendererGFX_Shader_SetType(&gfx->shader, GX_SHADER_TYPE_STANDARD);
    gX_RendererGFX_Shader_Init(&gfx->depth, GX_PATH("shader", "gfx_depth.vert"), GX_PATH("shader", "gfx_depth.frag"));
    gfx->depth.reload = true;
    gX_RendererGFX_Shader_Init(&gfx->pointDepth.shader, GX_PATH("shader", "gfx_pointdepth.vert"), GX_PATH("shader", "gfx_pointdepth.frag"));
    gfx->pointDepth.shader.reload = true;
    gX_RendererGFX_Shader_Init(&gfx->proceduralSky, 0, GX_PATH("shader", "sky_new.frag"));

    //opencode: depth visualizer uses the default vertex shader (NULL vert path) —
    //   the custom gfx_depth_visualizer.vert passed pixel coords to gl_Position
    //   directly, clipping everything outside NDC (2x2-pixel triangle).
    gX_RendererGFX_Shader_Init(&gfx->depthVisualizer, 0, GX_PATH("shader", "gfx_depth_visualizer.frag"));
    gfx->depthVisualizer.reload = true;
    
    //opencode: init render textures (sceneColorLDR + hdrScene for bloom)
    gX_RendererGFX_Load_InitRTs(gfx);

    //opencode: create dummy white cubemap for IBL samplers — prevents NaN on NVIDIA
    {
        gX_U8 white = 255;
        gfx->ibl.dummyCubemap.id = gX_RHI_Tex_CreateCubemap(1, gX_RHI_RGB, gX_RHI_RGB, gX_RHI_UNSIGNED_BYTE, "ibl_dummy");
        if (gfx->ibl.dummyCubemap.id)
        {
            //opencode: upload white pixel data to each face
            for (gX_I32 face = 0; face < 6; face++)
                gX_RHI_Tex_Upload(gfx->ibl.dummyCubemap.id,
                    (gX_U32)(gX_RHI_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, 0, 0, 1, 1,
                    gX_RHI_RGB, gX_RHI_UNSIGNED_BYTE, &white);
        }
        gX_RHI_Tex_Param(gfx->ibl.dummyCubemap.id, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gfx->ibl.dummyCubemap.id, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gfx->ibl.dummyCubemap.id, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(gfx->ibl.dummyCubemap.id, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(gfx->ibl.dummyCubemap.id, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);
    }

    //opencode: init water system
    gX_RendererWater_Load(&gfx->water);
    gfx->water.planeHeight = 0.0f;
    //opencode: add default particle emitter
    {
        gX_I32 eidx = gX_RendererGFX_Emitter_Add(gfx);
        if (eidx >= 0) {
            gX_RendererGFX_Emitter *e = gX_Heap_SeekPtr_Index(&gfx->particle.emitters, (gX_U32)eidx);
            if (e) {
                e->pos     = (gX_F32v3){0.0f, 0.3f, 0.0f};
                e->baseVel = (gX_F32v3){0.0f, 3.0f, 0.0f};
                e->color   = (gX_F32v4){1.0f, 0.6f, 0.2f, 1.0f};
                e->spread  = 45.0f;
                e->life    = 1.5f;
                e->speed   = 2.0f;
                e->emitRate = 60.0f;
            }
        }
    }

    //opencode: check bindless texture support
    gX_Bindless_CheckSupport();

    //opencode: reposition preset light (index 1) within view frustum
    {
        gX_RendererGFX_Light *pl = gX_Heap_SeekPtr_Index(&gfx->lights, 1);
        if (pl) pl->pos = (gX_F32v3){-1.5f, 1.5f, 0.0f};
    }
    
    //opencode: legacy demo trio REMOVED — the demo scene (AddDemoContent)
    //   owns first-frame visuals in every GFX path now, and the trio's
    //   footprints interpenetrated the curated cluster (cube inside the
    //   teapot, sphere/cone through Button/cube1). Verification scenes
    //   were already trio-free; the auto-seat pass covers guests.

    return 0;
}
gX_I32 gX_RendererGFX_Unload(gX_RendererGFX *gfx) {
    gX_RendererGFX_Profile_Deinit(gfx->profile, maindata.frameCount);
    gfx->profile = NULL;
    gfx->profileAttempted = 0;
    
    gX_TraceLog_GFX(gX_LOG_INFO,"Unloading testmodel");
    gX_TraceLog_SuppressStart();
    
    {
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
            if (model->loaded) gX_RendererGFX_Model_Unload(gfx, model);
        }
    }
    gX_Heap_DeInit(&gfx->models);
    
    gX_RendererGFX_Shader_Unload(&gfx->shader);
    gX_RendererGFX_Shader_Unload(&gfx->depth);
    gX_RendererGFX_Shader_Unload(&gfx->depthVisualizer);
    gX_RendererGFX_Shader_Unload(&gfx->pointDepth.shader);
    
    gX_RendererGFX_Unload_FreeRTs(gfx);
    
    if (gfx->common.sphereMarkerLoaded) {
        gX_Model_Unload(gfx->common.sphereMarker);
        gfx->common.sphereMarkerLoaded = false;
    }
    
    gX_RendererGFX_Shader_Unload(&gfx->ibl.brdfShader);
    gX_RendererGFX_Shader_Unload(&gfx->ibl.irradianceShader);
    gX_RendererGFX_Shader_Unload(&gfx->ibl.prefilterShader);
    
    {
        GX_HEAP_FOREACH(gfx->textures, gX_RendererGFX_Texture, tex) {
            if (tex->wrapper.loadingState.loaded) gX_RendererGFX_Texture_Unload(tex);
        }
    }
    gX_Heap_DeInit(&gfx->textures);
    
    gX_RendererGFX_RenderTexture *map0 = gX_Heap_SeekPtr_Index(&gfx->maps, 0);
    if (map0 && map0->wrapper.loadingState.loaded) gX_RendererGFX_RenderTexture_Unload(map0);
    gX_Heap_DeInit(&gfx->maps);
    gX_Heap_DeInit(&gfx->lights);

    gX_RendererGFX_Particle_Unload(gfx);
    gX_RendererGFX_Weather_Unload(gfx);
    gX_RendererGFX_Splat_Unload(gfx);
    gX_RendererGFX_OracleGPU_Unload(gfx);

    gX_TraceLog_SuppressEnd();
    
    return 0;
}
