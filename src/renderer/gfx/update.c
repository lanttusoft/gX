/**********************************************************************************************
*
*   [gX: src/renderer/gfx/update.c]
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


//opencode: extracted from gfx.c Update — per-frame update, including authored
//   point-light orbit motion before light/shadow matrices are refreshed.
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "../../common/log.h"
#include "../../common/enum.h"
#include "../../common/macros.h"
#include "../../common/utils.h"
#include "../../common/path.h"
#include "../../input.h"
#include "../../gxmath/matrix.h"
#include "../../gxmath/constants.h"
#include "../gfx.h"
#include "../renderer.h"
#include "../../main.h"
#include "../../camera.h"
#include "../../wrappers/wrappers.h"
#include "boilerplate.h"
#include "gl_level.h"
#include "layout.h"
#include "animation.h"
#include "bloom.h"
#include "dof.h"
#include "taa.h"
#include "motionblur.h"
#include "rt_helpers.h"
#include "../cubemap.h"
#include "shader.h"
#include "texture.h"
#include "load.h"

//opencode: needed for IBL/cubemap init
#include "../../rhi/rhi_fbo.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_tex.h"
#include "../../rhi/rhi_state.h"
#include "scene.h"
//opencode: hunt - implicit-decl sweep

#ifdef GX_WATER_REFRACTION_EXPERIMENT
//ai: isolated glass diagnostics move the actual camera or first refractive model once, without resizing.
static gX_I0 glass_test_move(gX_RendererGFX *Gfx)
{
    const gX_I8 *Hook = getenv("GX_GLASS_TEST_MOVE");
    if (!Hook || maindata.frameCount != 12) return;
    if (strcmp(Hook, "camera") == 0)
    {
        camera.camera3D.position.x += 0.75f;
        gX_TraceLog(gX_LOG_NOTICE, "GLASS_TEST_MOVE: frame=12 cameraX=%.9g", camera.camera3D.position.x);
    }
    else if (strcmp(Hook, "model") == 0)
    {
        GX_HEAP_FOREACH(Gfx->models, gX_RendererGFX_Model, Model)
        {
            if (!Model->loaded || !Model->params.enableRefraction) continue;
            Model->params.pos.x += 0.5f;
            gX_TraceLog(gX_LOG_NOTICE, "GLASS_TEST_MOVE: frame=12 modelX=%.9g", Model->params.pos.x);
            break;
        }
    }
}

//ai: bounded headless transitions exercise authored runtime state, not launch overrides or shader reloads.
static gX_I0 water_refraction_test_toggle(gX_RendererGFX *Gfx)
{
    const gX_I8 *Hook = getenv("GX_WATER_REFRACTION_TEST_TOGGLE");
    if (!Hook) return;
    if (maindata.frameCount == 12) Gfx->water.sceneRefractionEnabled = true;
    if (maindata.frameCount == 20 && strcmp(Hook, "on") != 0)
        Gfx->water.sceneRefractionEnabled = false;
    if (maindata.frameCount == 24 && strcmp(Hook, "cycle") == 0)
        Gfx->water.sceneRefractionEnabled = true;
}

//ai: opt-in headless seam drives the native resolution-reload path before any scene/capture draw.
static gX_I0 water_refraction_test_transition(gX_RendererGFX *Gfx)
{
    const gX_I8 *Hook = getenv("GX_WATER_REFRACTION_TEST_TRANSITION");
    if (!Hook || strcmp(Hook, "1") != 0 || maindata.frameCount < 12) return;
    maindata.renderWidth = 800;
    maindata.renderHeight = 600;
    if (maindata.frameCount != 12) return;
    camera.camera3D.position.x = 0.75f;
    Gfx->state.RELOAD = true;
    gX_TraceLog(gX_LOG_NOTICE, "WATER_REFRACTION_TEST_TRANSITION: frame=12 cameraX=0.75 size=800x600");
}
#endif

//opencode: keep an opted-in fog/void color link live across editor tabs.
//ai: refresh the active TAA safety-uniform location together with the matrix locations on reload.
//ai: cache GTAO/SSGI authored normal-separation bias uniforms alongside radius.
//ai: detect GTAO/SSGI activation and algorithm transitions before clearing temporal targets.
//ai: cache SSGI's exact hasVelocity uniform so its accumulation pass can reproject history.
//ai: load the shared velocity pass whenever temporal GTAO is active.
gX_I0 gX_RendererGFX_Update(gX_RendererGFX *gfx)
{
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    glass_test_move(gfx);
    water_refraction_test_toggle(gfx);
    water_refraction_test_transition(gfx);
#endif
    if (gfx->fog.matchVoidColor) gfx->fog.color = gfx->sky.color;
    //opencode: interactive showcase controller (--showcase) — orbit camera,
    //   effect hotkeys, model focus. Runs before the frame's rendering.
    extern gX_I0 gX_Showcase_Update(gX_RendererGFX *gfx);
    gX_Showcase_Update(gfx);

    //opencode: flush deferred shader hot-reload swaps at frame start — swaps
    //   queued during the previous DrawTo (deferred so the old program is never
    //   deleted while bound mid-pass) are applied here, before any rendering
    extern gX_I0 gX_Shader_Compiler_FlushDeferredSwaps(gX_I0);
    gX_Shader_Compiler_FlushDeferredSwaps();

    //opencode: file-scope one-shot flags — reset on RELOAD so freshly recreated
    //   SSAO maps get re-cleared after a resize
    static gX_I1 ssaoCleared = false;

    //opencode: detect texture filter changes and re-apply to all loaded textures
    {
        static gX_I32 prevFilter = gX_TEXTURE_FILTER_BILINEAR;
        static gX_F32 prevLodBias = 0.0f;
        if (gfx->state.textureFilter != prevFilter || fabsf(gfx->state.mipmapBias - prevLodBias) > 0.001f) {
            gX_RendererGFX_Textures_ApplyFilter(gfx);
            prevFilter = gfx->state.textureFilter;
            prevLodBias = gfx->state.mipmapBias;
        }
    }

    //opencode: detect quality preset change — cascades to dependent fields.
    //   Individual change detectors (textureFilter, shaderQuality, msaaLevel,
    //   mipmapBias) pick up the cascaded values and trigger their own updates.
    {
        static gX_I32 prevPreset = 0;
        if (gfx->state.qualityPreset != prevPreset) {
            gX_RendererGFX_ApplyQualityPreset(gfx);
            prevPreset = gfx->state.qualityPreset;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "qualityPreset=%d cascaded to filter=%d shader=%d msaa=%d lod=%.1f",
                prevPreset, gfx->state.textureFilter, gfx->state.shaderQuality,
                gfx->state.msaaLevel, gfx->state.mipmapBias);
        }
    }

    //opencode: detect MSAA level change — recreate MSAA FBO
    {
        static gX_I32 prevMsaa = 0;
        if (gfx->state.msaaLevel != prevMsaa) {
            gX_RendererGFX_MSAA_Create(gfx);
            prevMsaa = gfx->state.msaaLevel;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "msaaLevel changed from %d to %d", prevMsaa, gfx->state.msaaLevel);
        }
    }

    //opencode: detect shader quality change — cascade to runtime feature toggles,
    //   then trigger reload on all GFX shaders so the compile-time QUALITY gates
    //   (standard.frag #if QUALITY >= N) are regenerated with the new level.
    {
        static gX_I32 prevQuality = 0;
        if (gfx->state.shaderQuality != prevQuality) {
            gX_I32 q = gfx->state.shaderQuality;
            //opencode: cascade quality to CPU-side state so it matches shader QUALITY gates
            if (q <= 0) {
                // LOW: force Phong path, disable parallax/rim/detail/refraction/SSAO.
                //   The force is snapshot-guarded (M6): Q-cycling LOW->HIGH used
                //   to leave every model Phong permanently (no restore path).
                gfx->state.ENABLE_MAP_PARALLAX = 0;
                gfx->state.ENABLE_RIM = 0;
                gfx->state.ENABLE_DETAIL = 0;
                gfx->state.ENABLE_SSAO = 0;
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, m) {
                    if (m->loaded) {
                        if (!m->qualitySaved) {
                            m->savedUsePBR = m->params.usePBR;
                            m->savedParallaxMode = m->params.parallaxMode;
                            m->qualitySaved = 1;
                        }
                        m->params.usePBR = 0;
                    }
                }
            } else {
                // MEDIUM/HIGH: enable PBR features (disabled by LOW above)
                gfx->state.ENABLE_MAP_PARALLAX = 1;
                gfx->state.ENABLE_RIM = 1;
                gfx->state.ENABLE_DETAIL = 1;
                gfx->state.ENABLE_SSAO = 1;
                // MEDIUM/HIGH does NOT force usePBR=1 — per-model usePBR is set by the
                // scene and should be preserved. The shader's #if QUALITY >= 1 gates
                // handle Fresnel, rim, detail, and refraction. PBR-only models already
                // have usePBR=1 from their scene setup; Phong-only models stay Phong
                // to avoid the GGX distribution blowing up at low roughness values.
                //opencode: M6 - restore any LOW/MEDIUM snapshot first (Q-cycle
                //   return to HIGH must undo the force/cap above).
                GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, m) {
                    if (m->loaded && m->qualitySaved) {
                        m->params.usePBR = m->savedUsePBR;
                        m->params.parallaxMode = m->savedParallaxMode;
                        m->qualitySaved = 0;
                    }
                }
                if (q <= 1) {
                    // MEDIUM: cap parallaxMode at 1 (steep+POM, no self-shadow).
                    //   Snapshot-guarded like LOW: the cap restores on HIGH.
                    GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, m) {
                        if (m->loaded && m->params.parallaxMode >= 2) {
                            if (!m->qualitySaved) {
                                m->savedUsePBR = m->params.usePBR;
                                m->savedParallaxMode = m->params.parallaxMode;
                                m->qualitySaved = 1;
                            }
                            m->params.parallaxMode = 1;
                        }
                    }
                }
            }
            gX_TraceLog_GFX(gX_LOG_NOTICE, "shaderQuality=%d cascaded to %d models, parallax=%d rim=%d detail=%d ssao=%d",
                q, gfx->models.amount,
                gfx->state.ENABLE_MAP_PARALLAX,
                gfx->state.ENABLE_RIM,
                gfx->state.ENABLE_DETAIL,
                gfx->state.ENABLE_SSAO);
            gfx->shader.reload = true;
            gfx->depth.reload = true;
            gfx->depthVisualizer.reload = true;
            gfx->proceduralSky.reload = true;
            gfx->pointDepth.shader.reload = true;
            // Bloom sub-shaders
            gfx->bloom.extractShader.reload = true;
            gfx->bloom.blurHShader.reload = true;
            gfx->bloom.blurVShader.reload = true;
            gfx->bloom.compositeShader.reload = true;
            // DOF sub-shaders
            gfx->dof.cocShader.reload = true;
            gfx->dof.blurShader.reload = true;
            gfx->dof.compositeShader.reload = true;
            // Motion blur, TAA, SSR, SSAO
            gfx->motionBlur.shader.reload = true;
            gfx->taa.resolveShader.reload = true;
            gfx->ssr.shader.reload = true;
            gfx->ssao.blurShader.reload = true;
            // IBL shaders
            gfx->ibl.brdfShader.reload = true;
            gfx->ibl.irradianceShader.reload = true;
            gfx->ibl.prefilterShader.reload = true;
            // Godray shaders
            gfx->godray.genShader.reload = true;
            // Fog volume shaders
            gfx->fogVolume.genShader.reload = true;
            gfx->fogVolume.blendShader.reload = true;
            // Lens flare shader
            gfx->lensflare.shader.reload = true;
            //opencode: include every lazily-enabled shader in quality reloads
            gfx->fxaaShader.reload = true;
            //opencode: reload the experimental palette shader only when active.
            if (getenv("GX_T_PALETTE_MODE") && atoi(getenv("GX_T_PALETTE_MODE")) > 0)
                gfx->paletteShader.reload = true;
            //opencode: reload the experimental interlace shader only when active.
            if (getenv("GX_T_INTERLACE_MODE") && atoi(getenv("GX_T_INTERLACE_MODE")) > 0)
                gfx->interlaceShader.reload = true;
            if (gfx->transparentBackground)
                gfx->alphaMaskShader.reload = true;
            gfx->ssr.hizShader.reload = true;
            gfx->ssao.gtaoShader.reload = true;
            gfx->ssao.gtaoAccumShader.reload = true;
            gfx->ssao.ssgiShader.reload = true;
            gfx->ssao.ssgiBlurShader.reload = true;
            gfx->ssao.ssgiAccumShader.reload = true;
            gfx->velocity.shader.reload = true;
            gfx->terrain.depthShader.reload = true;
            gfx->terrain.velShader.reload = true;
            gfx->contact.genShader.reload = true;
            gfx->bloom.luminanceShader.reload = true;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "shaderQuality changed from %d to %d — triggering reload of all GFX shaders",
                prevQuality, gfx->state.shaderQuality);
            prevQuality = gfx->state.shaderQuality;
        }
    }

    //opencode: reload RTs on resolution change
    if (gfx->state.RELOAD && maindata.backendLoaded)
    {
        gfx->state.RELOAD = false;
        GX_GFX_RT_RELOAD(gfx, motionBlur.map);
        gfx->motionBlur.map.wrapper.width = maindata.renderWidth;
        gfx->motionBlur.map.wrapper.height = maindata.renderHeight;

        //opencode: reload CSM array texture (recreate on resize)
        if (gfx->csm.arrayTexId > 0) {
            gX_RHI_Tex_Delete(gfx->csm.arrayTexId);
            gfx->csm.arrayTexId = 0;
        }
        if (gfx->csm.fboId > 0) {
            gX_RHI_FBO_Delete(gfx->csm.fboId);
            gfx->csm.fboId = 0;
        }
        gfx->csm.map.wrapper.loadingState.loaded = false;
        gfx->csm.inited = false;

        gX_RendererGFX_RT_ReloadSized(&gfx->taa.historyMap,
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight);
        gX_RendererGFX_RT_ReloadSized(&gfx->taa.resolveMap,
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight);
        //opencode: v0.1.38 — velocity buffer follows render resolution
        gfx->velocity.map.wrapper.loadingState.loaded = false;
        gfx->velocity.map.wrapper.width = maindata.renderWidth;
        gfx->velocity.map.wrapper.height = maindata.renderHeight;
        gfx->velocity.hasPrevVP = false; //opencode: camera jump on resize
        gX_RendererGFX_TemporalHistory_Reset(&gfx->taa.temporal);
        gfx->taa.jitter = (gX_F32v2){0.0f, 0.0f};
        gfx->taa.prevJitter = (gX_F32v2){0.0f, 0.0f};

        gX_RendererGFX_RT_ReloadSized(&gfx->dof.cocMap,
            (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight);
        gX_U32 dofDivisor = 1;
        const gX_I8 *dofResHook = getenv("GX_T_DOF_RES");
        if (dofResHook && atoi((const char *)dofResHook) == 1) dofDivisor = 2;
        if (dofResHook && atoi((const char *)dofResHook) >= 2) dofDivisor = 4;
        gX_RendererGFX_RT_ReloadSized(&gfx->dof.blurMap,
            (gX_U32)maindata.renderWidth / dofDivisor,
            (gX_U32)maindata.renderHeight / dofDivisor);
        gX_RendererGFX_RT_ReloadSized(&gfx->dof.blurPing,
            (gX_U32)maindata.renderWidth / dofDivisor,
            (gX_U32)maindata.renderHeight / dofDivisor);
        if (dofDivisor > 1)
            gX_RendererGFX_RT_ReloadSized(&gfx->dof.compositeMap,
                (gX_U32)maindata.renderWidth, (gX_U32)maindata.renderHeight);

        //opencode: two-mirror feedback RTs stay half-res across resizes.
        gX_RendererGFX_RT_ReloadSized(&gfx->mirror.freshB,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->mirror.histA,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->mirror.histB,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);

        gX_RendererGFX_RenderTexture *bloomRTs[] = {
            &gfx->bloom.hdrScene, &gfx->bloom.extract, &gfx->bloom.ping,
            &gfx->bloom.pong, &gfx->bloom.scene
        };
        for (gX_U32 i = 0; i < 5; i++)
        {
            if (bloomRTs[i]->wrapper.loadingState.loaded) gX_RendererGFX_RenderTexture_Unload(bloomRTs[i]);
            bloomRTs[i]->wrapper.loadingState.loaded = false;
            bloomRTs[i]->wrapper.loadingState.loadAsap = true;
            //opencode: restore HDR flag — Unload doesn't clear it but be explicit on reload.
            bloomRTs[i]->hdr = true;
        }
        // Update dimensions
        gfx->bloom.hdrScene.wrapper.width  = maindata.renderWidth;
        gfx->bloom.hdrScene.wrapper.height = maindata.renderHeight;
        gfx->bloom.extract.wrapper.width   = maindata.renderWidth / 2;
        gfx->bloom.extract.wrapper.height  = maindata.renderHeight / 2;
        gfx->bloom.ping.wrapper.width      = maindata.renderWidth / 2;
        gfx->bloom.ping.wrapper.height     = maindata.renderHeight / 2;
        gfx->bloom.pong.wrapper.width      = maindata.renderWidth / 2;
        gfx->bloom.pong.wrapper.height     = maindata.renderHeight / 2;
        gfx->bloom.scene.wrapper.width     = maindata.renderWidth;
        gfx->bloom.scene.wrapper.height    = maindata.renderHeight;

        GX_GFX_RT_RELOAD(gfx, godray.genMap);
        gfx->godray.genMap.wrapper.width = maindata.renderWidth / 2;
        gfx->godray.genMap.wrapper.height = maindata.renderHeight / 2;

        GX_GFX_RT_RELOAD(gfx, fogVolume.genMap);
        gfx->fogVolume.genMap.wrapper.width = maindata.renderWidth / 2;
        gfx->fogVolume.genMap.wrapper.height = maindata.renderHeight / 2;

        //opencode: E-5 — 7 RTs were missing from the resize handler and kept
        //   stale dimensions after a window resize (RenderTexture_Update only
        //   loads, never resizes): planar reflection, SSR map, SSAO
        //   map + blur (half-res), refraction background, sceneColorLDR.
        GX_GFX_RT_RELOAD(gfx, reflection.planar);
        gfx->reflection.planar.wrapper.width = maindata.renderWidth;
        gfx->reflection.planar.wrapper.height = maindata.renderHeight;
        GX_GFX_RT_RELOAD(gfx, ssr.map);
        gfx->ssr.map.wrapper.width = maindata.renderWidth;
        gfx->ssr.map.wrapper.height = maindata.renderHeight;
        if (gfx->ssr.normalBufferEnabled) {
            GX_GFX_RT_RELOAD(gfx, ssr.normalMap);
            gfx->ssr.normalMap.wrapper.width = maindata.renderWidth;
            gfx->ssr.normalMap.wrapper.height = maindata.renderHeight;
        }
        GX_GFX_RT_RELOAD(gfx, ssao.map);
        gfx->ssao.map.wrapper.width = maindata.renderWidth / 2;
        gfx->ssao.map.wrapper.height = maindata.renderHeight / 2;
        GX_GFX_RT_RELOAD(gfx, ssao.blurMap);
        gfx->ssao.blurMap.wrapper.width = maindata.renderWidth / 2;
        gfx->ssao.blurMap.wrapper.height = maindata.renderHeight / 2;
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.gtaoAccumA,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.gtaoAccumB,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.gtaoTemporal);
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.ssgiMap,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.ssgiBlurMap,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.ssgiAccumA,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_RT_ReloadSized(&gfx->ssao.ssgiAccumB,
            (gX_U32)maindata.renderWidth / 2, (gX_U32)maindata.renderHeight / 2);
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.ssgiTemporal);
        GX_GFX_RT_RELOAD(gfx, debugVis.refractionBg);
        gfx->debugVis.refractionBg.wrapper.width = maindata.renderWidth;
        gfx->debugVis.refractionBg.wrapper.height = maindata.renderHeight;
        GX_GFX_RT_RELOAD(gfx, sceneColorLDR);
        gfx->sceneColorLDR.wrapper.width = maindata.renderWidth;
        gfx->sceneColorLDR.wrapper.height = maindata.renderHeight;

        //opencode: recreate the MSAA FBO at the new resolution — hdrMSAA is
        //   MSAA_Create-owned (excluded from the generic reload above, which
        //   would single-sample-allocate it); MSAA_Create destroys first.
        gX_RendererGFX_MSAA_Create(gfx);

        //opencode: reset one-shot state so freshly recreated RTs are re-cleared
        //   (TAA history, SSAO maps) and the SSR roughness texture is re-created
        //   and re-attached to the new hdrScene FBO (COLOR_ATTACHMENT1).
        ssaoCleared = false;

    }


    //opencode: deferred load sphere marker model
    if (!gfx->common.sphereMarkerLoaded && maindata.backendLoaded) {
        gfx->common.sphereMarkerLoaded = 1;
        gfx->common.sphereMarker = gX_Model_Load(GX_PATH("model", "primitive/sphere.glb"));
        if (!(gfx->common.sphereMarker.meshCount > 0)) gfx->common.sphereMarkerLoaded = 0;
        else gX_TraceLog_GFX(gX_LOG_NOTICE, "Sphere marker loaded (%u meshes)", gfx->common.sphereMarker.meshCount);
    }

    //opencode: populate the default demo scene (--gfx / --showcase) once the
    //   backend is up — models + point lights so the scene is not empty.
    //ai: battle and physics sandbox scenes own their heap content; the demo
    //   content must not be injected while they are the active scene.
    if (!gfx->common.demoSceneAdded && maindata.backendLoaded
        && maindata.masterIndex != gX_SCENE_BATTLE
        && maindata.masterIndex != gX_SCENE_PHYSICS) {
        gX_RendererGFX_Scene_AddDemoContent(gfx);
        gX_TraceLog_GFX(gX_LOG_NOTICE, "Demo scene content added (%u models, %u lights)",
            gfx->models.amount, gfx->lights.amount);
    }

    //opencode: auto-seat — once models finish loading, lift sunk models onto
    //   the support plane and push overlapping footprints apart (heap order,
    //   deterministic). Skipped for the pristine verification scenes
    //   (minimal/planar/reflectrow) and via GX_LAYOUT_NORESEAT=1.
    //   --layout-check instead verifies before/after and exits the loop.
    {
        extern gX_MainArguments arguments;
        static gX_I1 s_layoutDone = 0;
        if (!s_layoutDone && gfx->common.demoSceneAdded && maindata.backendLoaded) {
            gX_I1 pristine = (maindata.masterIndex == gX_SCENE_BATTLE
                || maindata.masterIndex == gX_SCENE_PHYSICS
                || arguments.showcaseMinimal || arguments.showcasePlanar
                || arguments.showcaseReflectRow || arguments.showcaseLedRow
                || arguments.showcaseY2K
                || arguments.showcaseForest || arguments.showcaseLake) ? 1 : 0;
            gX_I1 allLoaded = 1;
            gX_I1 anyValid = 0;
            {
                gX_RendererGFX_Model *mp = (gX_RendererGFX_Model*)gfx->models.data;
                for (gX_U32 mi = 0; mi < gfx->models.amount; mi++) {
                    if (!gfx->models.meta[mi].USED) continue;
                    if (!mp[mi].loaded) { allLoaded = 0; break; }
                    if (mp[mi].aabbValid) anyValid = 1;
                }
            }
            if ((allLoaded && anyValid) || maindata.frameCount > 600) {
                s_layoutDone = 1;
                if (arguments.layoutCheck) {
                    gX_F32 supY = gX_RendererGFX_Layout_SupportY(gfx);
                    gX_I32 before = gX_RendererGFX_Layout_Verify(gfx, supY);
                    gX_I32 moves = gX_RendererGFX_Layout_Reseat(gfx, supY);
                    gX_I32 after = gX_RendererGFX_Layout_Verify(gfx, supY);
                    gX_TraceLog(gX_LOG_WARNING, "LAYOUT_CHECK: before=%d moves=%d after=%d verdict=%s",
                        before, moves, after,
                        (before > 0 && after == 0) ? "PASS" : "FAIL");
                    maindata.loop = 0;
                } else if (!pristine && !getenv("GX_LAYOUT_NORESEAT")) {
                    gX_F32 supY = gX_RendererGFX_Layout_SupportY(gfx);
                    gX_RendererGFX_Layout_Reseat(gfx, supY);
                }
            }
        }
    }

    //opencode: deferred load geometry demo shader + model
    if (!gfx->geoDemo.loaded && maindata.backendLoaded) {
        gfx->geoDemo.loaded = 1;
        //opencode: geometry shaders are GL 3.2 / ES 3.2+ — skip on ES 3.0/3.1
        //   (strict ES3.0 has no geometry shader stage at all).
        if (gX_GL.hasGeometryShaders) {
            gfx->geoDemo.program = gX_Shader_Load_Geo(
                GX_PATH("shader", "geo_demo.vert"),
                GX_PATH("shader", "geo_demo.geom"),
                GX_PATH("shader", "geo_demo.frag"));
            if (gfx->geoDemo.program.id > 0)
            {
                gfx->geoDemo.matModelLoc = gX_Shader_GetLocation(gfx->geoDemo.program, "matModel");
                gfx->geoDemo.matViewLoc = gX_Shader_GetLocation(gfx->geoDemo.program, "matView");
                gfx->geoDemo.matProjectionLoc = gX_Shader_GetLocation(gfx->geoDemo.program, "matProjection");
                gfx->geoDemo.explodeLoc = gX_Shader_GetLocation(gfx->geoDemo.program, "u_Explode");
                gfx->geoDemo.viewPosLoc = gX_Shader_GetLocation(gfx->geoDemo.program, "viewPosition");
                gfx->geoDemo.explodeFactor = 0.0f;
            }
            gfx->geoDemo.model = gX_Model_Load(GX_PATH("model", "props/tubetech_vt.glb"));
        }
        gX_TraceLog_GFX(gX_LOG_NOTICE, "Geo demo loaded: shader_id=%u meshes=%u",
            gfx->geoDemo.program.id, gfx->geoDemo.model.meshCount);
    }

    //opencode: animate explode factor using elapsed time
    if (gfx->state.ENABLE_GEO_DEMO && gfx->geoDemo.loaded && gfx->geoDemo.program.id > 0)
    {
        gfx->geoDemo.explodeFactor = (gX_F32)(sin(maindata.timeElapsed) * 0.15 + 0.15);
    }

    //opencode: shader debug visualization — keyboard toggles (built-in SHOW_* uniforms)
    {
        // F3: cycle fragment debug mode using existing SHOW_* state flags
        if (input.keyboard.keys[gX_KEY_F3].state.PRESSED) {
            static gX_I32 cycleIdx = 0;
            cycleIdx = (cycleIdx + 1) % 7;
            gfx->state.SHOW_POSITION  = 0;
            gfx->state.SHOW_TEXCOORD  = 0;
            gfx->state.SHOW_TEXCOORD2 = 0;
            gfx->state.SHOW_NORMAL    = 0;
            gfx->state.SHOW_COLOR     = 0;
            gfx->state.SHOW_TANGENT   = 0;
            if (cycleIdx == 1) gfx->state.SHOW_TEXCOORD  = 1;
            if (cycleIdx == 2) gfx->state.SHOW_NORMAL    = 1;
            if (cycleIdx == 3) gfx->state.SHOW_TANGENT   = 1;
            if (cycleIdx == 4) gfx->state.SHOW_POSITION  = 1;
            if (cycleIdx == 5) gfx->state.SHOW_COLOR     = 1;
            if (cycleIdx == 6) gfx->state.SHOW_TEXCOORD2 = 1;
            gX_TraceLog(gX_LOG_NOTICE, "GFX_DEBUG: F3 debug vis cycle=%d (0=off)", cycleIdx);
        }
        // F4: toggle normal lines geo shader — deferred load on first use
        if (input.keyboard.keys[gX_KEY_F4].state.PRESSED) {
            gfx->debugVis.debugNormalsMode = (gfx->debugVis.debugNormalsMode + 1) % 4;
            gX_TraceLog(gX_LOG_NOTICE, "GFX_DEBUG: F4 normal lines mode=%d (0=off 1=normals 2=tangents 3=both)", gfx->debugVis.debugNormalsMode);
        }
        // Deferred load debug normal lines shader (V+G+F) — only when enabled;
        //   loading + unloading every frame (when mode==0) leaked programs and
        //   spammed GL_INVALID_OPERATION ("Wrong component type or count")
        if (gfx->debugVis.debugNormalsMode > 0 && !gfx->debugVis.debugNormalsLoaded && maindata.backendLoaded
            && gX_GL.hasGeometryShaders) {
            gfx->debugVis.debugNormalsLoaded = 1;
            gfx->debugVis.debugNormalsShader = gX_Shader_Load_Geo(
                GX_PATH("shader", "debug_normals.vert"),
                GX_PATH("shader", "debug_normals.geom"),
                GX_PATH("shader", "debug_normals.frag"));
            if (!(gfx->debugVis.debugNormalsShader.id > 0)) gfx->debugVis.debugNormalsLoaded = 0;
            else gX_TraceLog(gX_LOG_NOTICE, "GFX_DEBUG: normal lines shader loaded id=%u", gfx->debugVis.debugNormalsShader.id);
        }
        if (gfx->debugVis.debugNormalsLoaded && gfx->debugVis.debugNormalsMode == 0) {
            gX_Shader_Unload(gfx->debugVis.debugNormalsShader);
            gfx->debugVis.debugNormalsShader = (gX_Shader){0};
            gfx->debugVis.debugNormalsLoaded = 0;
        }
    }

    //opencode: CSM scene bounds + cascade frustum computation. The sun ortho
    //   half-extent is derived from scene AABBs (single cascade) or from the
    //   camera frustum at each split distance (cascadeCount > 1). Previously
    //   sun.camera.size was never set (0 -> degenerate Ortho) and
    //   csm.frustumSizes/splits had no writers, so CSM was fully inert.
    {
        gX_F32 minX = 1e30f, minY = 1e30f, minZ = 1e30f;
        gX_F32 maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
        gX_I1 anyModel = 0;
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, m) {
            if (!m->loaded || !m->aabbValid) continue;
            gX_F32v3 wMin = {
                m->params.pos.x + m->aabbMin[0] * m->params.scale.x,
                m->params.pos.y + m->aabbMin[1] * m->params.scale.y,
                m->params.pos.z + m->aabbMin[2] * m->params.scale.z
            };
            gX_F32v3 wMax = {
                m->params.pos.x + m->aabbMax[0] * m->params.scale.x,
                m->params.pos.y + m->aabbMax[1] * m->params.scale.y,
                m->params.pos.z + m->aabbMax[2] * m->params.scale.z
            };
            if (wMin.x < minX) minX = wMin.x;
            if (wMax.x > maxX) maxX = wMax.x;
            if (wMin.y < minY) minY = wMin.y;
            if (wMax.y > maxY) maxY = wMax.y;
            if (wMin.z < minZ) minZ = wMin.z;
            if (wMax.z > maxZ) maxZ = wMax.z;
            anyModel = 1;
        }
        gX_F32 sceneSize = 20.0f;
        if (anyModel) {
            gX_F32 hw = (maxX - minX) * 0.5f;
            gX_F32 hh = (maxZ - minZ) * 0.5f;
            sceneSize = fmaxf(hw, hh) * 1.5f;
        }
        gfx->sun.camera.size = sceneSize;
        gfx->sun.camera.near = 0.1f;
        gfx->sun.camera.far = sceneSize * 4.0f;

        //opencode: per-cascade view-space splits + ortho frustum sizes.
        //   World distances approximate view-space depth (good enough for the
        //   shader's split test); frustumSize_i covers the view cone at split_i.
        if (gfx->csm.cascadeCount > 1) {
            gX_F32 aspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
            gX_F32 tanHalf = tanf(camera.camera3D.fovy * gX_DEG2RAD * 0.5f);
            gX_F32 farPlane = camera.frustum.far;
            for (gX_I32 ci = 0; ci < 4; ci++) {
                //opencode: every cascade uses its configured distance (the
                //   old fallback sent the LAST cascade to the raw camera far
                //   plane (1000): cascade 1 spanned +-600 units, so all
                //   mid-field shadows washed sub-texel (crisp inside split
                //   10, gone beyond it). Clamp to farPlane for safety.
                gX_F32 split = gfx->csm.cascadeDistances[ci];
                //opencode: force split 0 across the showcase slab for seam probes.
                if (ci == 0 && getenv("GX_T_CSMSPLIT"))
                    split = (gX_F32)atof(getenv("GX_T_CSMSPLIT"));
                split = fminf(split, farPlane);
                gfx->csm.splits[ci] = split;
                gX_F32 halfH = split * tanHalf;
                gX_F32 halfW = halfH * aspect;
                gfx->csm.frustumSizes[ci] = fmaxf(halfW, halfH);
            }
        } else {
            for (gX_I32 ci = 0; ci < 4; ci++) {
                gfx->csm.splits[ci] = 0.0f;
                gfx->csm.frustumSizes[ci] = sceneSize;
            }
        }
    }

    gX_RendererGFX_Light_Update(gfx, &gfx->sun);
    //opencode: ENABLE_SHADOWS is now independent from sun.castShadows.
    //   ImGui toggles of ENABLE_SHADOWS are no longer overwritten per-frame.
    //   The shader still respects per-light castShadows for individual lights.
    //   The depth shadow pass gates on ENABLE_SHADOWS; if it's true but no light
    //   has shadows enabled, the pass renders empty depth (harmless).

    //opencode: update all lights via heap iteration
    {
        gX_F64 lightDeltaTime = gX_FrameTime_Get();
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
            gX_RendererGFX_Light_Orbit_Update(light, lightDeltaTime);
            gX_RendererGFX_Light_Update(gfx, light);
        }
    }

    gX_RendererGFX_Texture_Update(&gfx->sky.skybox);

    //opencode: compute Halton jitter for TAA BEFORE shader update so the main
    //   shader receives the CURRENT frame's jitter (not the stale value from
    //   last frame). Previously this was AFTER shader update, causing a one-frame
    //   mismatch between what the scene rendered with and what TAA expected.
    //   When TAA is disabled, jitter is reset to zero to avoid sub-pixel offset.
    if (gfx->state.ENABLE_TAA)
    {
        gfx->taa.prevJitter = gfx->taa.jitter;
        gX_F32 h2 = 0.0f, h3 = 0.0f;
        gX_I32 idx = gX_RendererGFX_TemporalHistory_FrameIndex(&gfx->taa.temporal);
        gX_F32 f2 = 1.0f, f3 = 1.0f;
        gX_I32 i2 = idx, i3 = idx;
        while (i2 > 0) { f2 /= 2.0f; h2 += f2 * (gX_F32)(i2 % 2); i2 /= 2; }
        while (i3 > 0) { f3 /= 3.0f; h3 += f3 * (gX_F32)(i3 % 3); i3 /= 3; }
        gfx->taa.jitter.x = (h2 - 0.5f) * 2.0f / (gX_F32)maindata.renderWidth;
        gfx->taa.jitter.y = (h3 - 0.5f) * 2.0f / (gX_F32)maindata.renderHeight;
    } else {
        gfx->taa.jitter = (gX_F32v2){0.0f, 0.0f};
        gfx->taa.prevJitter = (gX_F32v2){0.0f, 0.0f};
    }

    gX_RendererGFX_Shader_Update(&gfx->shader, gfx);
    if (gfx->state.ENABLE_SHADOWS)
        gX_RendererGFX_Shader_Update(&gfx->depth, gfx);
    gX_RendererGFX_Shader_Update(&gfx->depthVisualizer, gfx);
    gX_RendererGFX_Shader_Update(&gfx->proceduralSky, gfx);
    gX_RendererGFX_Shader_Update(&gfx->fxaaShader, gfx); //opencode: v0.1.18 — hot reload
    //opencode: avoid polling the opt-in palette shader on production frames.
    if (getenv("GX_T_PALETTE_MODE") && atoi(getenv("GX_T_PALETTE_MODE")) > 0)
        gX_RendererGFX_Shader_Update(&gfx->paletteShader, gfx);
    //opencode: avoid polling the opt-in interlace shader on production frames.
    if (getenv("GX_T_INTERLACE_MODE") && atoi(getenv("GX_T_INTERLACE_MODE")) > 0)
        gX_RendererGFX_Shader_Update(&gfx->interlaceShader, gfx);
    //ai: update the alpha mask only when Battle or the GFX demo enables it.
    if (gfx->transparentBackground)
        gX_RendererGFX_Shader_Update(&gfx->alphaMaskShader, gfx);
    if (gfx->state.ENABLE_POINT_SHADOWS)
        gX_RendererGFX_Shader_Update(&gfx->pointDepth.shader, gfx);

    //opencode: cache point depth shader uniform locations after first load
    if (gfx->state.ENABLE_POINT_SHADOWS && gfx->pointDepth.shader.loaded
        && (!gfx->pointDepth.locsCached || gfx->pointDepth.shader.locsStale))
    {
        gfx->pointDepth.lightSpaceLoc = gX_Shader_GetLocation(gfx->pointDepth.shader.shader, "matLightSpace");
        gfx->pointDepth.lightPosLoc = gX_Shader_GetLocation(gfx->pointDepth.shader.shader, "lightPos");
        gfx->pointDepth.farPlaneLoc = gX_Shader_GetLocation(gfx->pointDepth.shader.shader, "farPlane");
        gfx->pointDepth.matModelLoc = gX_Shader_GetLocation(gfx->pointDepth.shader.shader, "matModelDepth");
        gX_TraceLog_GFX(gX_LOG_INFO, "pointDepth locs: lightSpace=%d lightPos=%d farPlane=%d matModel=%d",
            gfx->pointDepth.lightSpaceLoc, gfx->pointDepth.lightPosLoc,
            gfx->pointDepth.farPlaneLoc, gfx->pointDepth.matModelLoc);
        gfx->pointDepth.locsCached = true;
        gfx->pointDepth.shader.locsStale = false;
    }

    //opencode: update HDR→LDR passthrough shader

    //opencode: update bloom shaders and render textures
    if (gfx->state.ENABLE_BLOOM) {
        gX_RendererGFX_Shader_Update(&gfx->bloom.extractShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->bloom.blurHShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->bloom.blurVShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->bloom.compositeShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->bloom.luminanceShader, gfx); //opencode: v0.3-M1
    }
    // Cache bloom uniform locations once all shaders are loaded
    if (gfx->state.ENABLE_BLOOM && (!gfx->bloom.locsCached
        || gfx->bloom.extractShader.locsStale
        || gfx->bloom.blurHShader.locsStale
        || gfx->bloom.blurVShader.locsStale
        || gfx->bloom.compositeShader.locsStale
        || gfx->bloom.luminanceShader.locsStale)
        && gfx->bloom.compositeShader.loaded)
    {
        //opencode: luminance caches its sampler independently in bloom.c
        if (gfx->bloom.luminanceShader.locsStale)
            gfx->bloom.luminanceInputLoc = -1;
        gX_Shader ex = gfx->bloom.extractShader.shader;
        gfx->bloom.extractInputLoc    = gX_Shader_GetLocation(ex, "sceneTex");
        gfx->bloom.extractThresholdLoc = gX_Shader_GetLocation(ex, "threshold");
        gfx->bloom.extractKneeLoc      = gX_Shader_GetLocation(ex, "knee");
        gfx->bloom.blurInputLoc       = gX_Shader_GetLocation(gfx->bloom.blurVShader.shader, "inputTex");
        gfx->bloom.blurResLoc         = gX_Shader_GetLocation(gfx->bloom.blurVShader.shader, "resolution");
        gfx->bloom.blurAmountLoc      = gX_Shader_GetLocation(gfx->bloom.blurVShader.shader, "amount");
        gX_Shader co = gfx->bloom.compositeShader.shader;
        gfx->bloom.compositeSceneLoc    = gX_Shader_GetLocation(co, "sceneTex");
        gfx->bloom.compositeBloomLoc    = gX_Shader_GetLocation(co, "bloomTex");
        gfx->bloom.compositeIntensityLoc = gX_Shader_GetLocation(co, "intensity");
        gfx->bloom.compositeExposureLoc = gX_Shader_GetLocation(co, "exposure");
        gfx->bloom.compositeVignetteLoc = gX_Shader_GetLocation(co, "vignette");
        gfx->bloom.compositeSaturationLoc = gX_Shader_GetLocation(co, "saturation");
        gfx->bloom.compositeContrastLoc = gX_Shader_GetLocation(co, "contrast");
        gfx->bloom.compositeACESLoc     = gX_Shader_GetLocation(co, "enableACES");
        gfx->bloom.compositeAgXLoc      = gX_Shader_GetLocation(co, "enableAgX");
        gfx->bloom.compositeGammaLoc    = gX_Shader_GetLocation(co, "enableGamma");
        gfx->bloom.compositeDitherModeLoc = gX_Shader_GetLocation(co, "ditherMode");
        gfx->bloom.compositeDitherSeedLoc = gX_Shader_GetLocation(co, "ditherSeed");
        gfx->bloom.compositeDitherStrengthLoc = gX_Shader_GetLocation(co, "ditherStrength");
        //opencode: 0x502 hunt — these three were never queried (stale cacher
        //   predates the uniforms); the fields sat at 0 and uploads hit
        //   location 0 (the vertex mvp mat4) with wrong types → per-frame
        //   GL_INVALID_OPERATION on every driver. The sibling RHI-based
        //   cacher (Bloom_CacheLocs) has them but is dead code — deleted.
        gfx->bloom.compositeSsgiTexLoc  = gX_Shader_GetLocation(co, "ssgiTex");
        gfx->bloom.compositeSsgiGainLoc = gX_Shader_GetLocation(co, "ssgiGain");
        gfx->bloom.locsCached = true;
        gfx->bloom.extractShader.locsStale = false;
        gfx->bloom.blurHShader.locsStale = false;
        gfx->bloom.blurVShader.locsStale = false;
        gfx->bloom.compositeShader.locsStale = false;
        gfx->bloom.luminanceShader.locsStale = false;
    }

    //opencode: CSM shadow map deferred init — create 2D array depth texture + FBO
    if (!gfx->csm.inited && gfx->csm.arrayTexId == 0)
    {
        gX_U32 w = gX_GFX_SHADOW_MAP_SIZE;
        gX_U32 h = gX_GFX_SHADOW_MAP_SIZE;
        gX_U32 tex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D_ARRAY, "csm_array");
        gX_RHI_Tex_Storage3D(tex, gX_RHI_TEXTURE_2D_ARRAY, 1, gX_RHI_DEPTH_COMPONENT24, w, h, 4);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_BORDER);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_BORDER);
        gX_F32 borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        gX_RHI_Tex_Paramfv(tex, gX_RHI_TEXTURE_2D_ARRAY, gX_RHI_TEXTURE_BORDER_COLOR, borderColor);
        gfx->csm.arrayTexId = tex;

        gX_U32 fbo = 0;
        gX_RHI_FBO_Gen(1, &fbo);
        gfx->csm.fboId = fbo;

        gfx->csm.map.wrapper.rt.id = fbo;
        gfx->csm.map.wrapper.rt.depth.id = tex;
        gfx->csm.map.wrapper.rt.texture.id = 0;
        //opencode: texture.width/height drive the viewport in BeginTexture —
        //   without these the CSM layers rendered at 0x0 (nothing drawn)
        gfx->csm.map.wrapper.rt.texture.width = (gX_I32)w;
        gfx->csm.map.wrapper.rt.texture.height = (gX_I32)h;
        gfx->csm.map.wrapper.width = w;
        gfx->csm.map.wrapper.height = h;
        gfx->csm.map.wrapper.loadingState.loaded = true;

        gfx->csm.inited = true;
        gX_TraceLog_GFX(gX_LOG_NOTICE, "CSM array texture created: %dx%d layers=4 tex=%u fbo=%u", w, h, tex, fbo);
    }

    //opencode: cookie fallback texture — deferred GL init (1x1 white pixel)
    if (gfx->common.cookieFallback.id == 0)
    {
        gX_U8 whitePixel[4] = {255, 255, 255, 255};
        gfx->common.cookieFallback.id = gX_RHI_Tex_Create2D(1, 1, gX_RHI_RGBA, gX_RHI_RGBA, gX_RHI_UNSIGNED_BYTE, whitePixel, "cookie_fallback");
        gX_RHI_Tex_Param(gfx->common.cookieFallback.id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(gfx->common.cookieFallback.id, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
        gX_TraceLog_GFX(gX_LOG_TRACE, "Cookie fallback texture created (id=%u)", gfx->common.cookieFallback.id);
    }

    //opencode: DOF shader updates
    if (gfx->state.ENABLE_DOF) {
        gX_RendererGFX_Shader_Update(&gfx->dof.cocShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->dof.blurShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->dof.compositeShader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->dof.cocMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->dof.blurMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->dof.blurPing);
        if (getenv("GX_T_DOF_RES") && atoi(getenv("GX_T_DOF_RES")) > 0)
            gX_RendererGFX_RenderTexture_Update(&gfx->dof.compositeMap);
    }
    //opencode: two-mirror feedback shader + RT updates (pair-gated loads).
    gX_RendererGFX_Shader_Update(&gfx->mirror.feedbackShader, gfx);
    gX_RendererGFX_RenderTexture_Update(&gfx->mirror.freshB);
    gX_RendererGFX_RenderTexture_Update(&gfx->mirror.histA);
    gX_RendererGFX_RenderTexture_Update(&gfx->mirror.histB);
    if ((!gfx->dof.locsCached || gfx->dof.cocShader.locsStale || gfx->dof.blurShader.locsStale || gfx->dof.compositeShader.locsStale) && gfx->dof.cocShader.loaded && gfx->dof.blurShader.loaded && gfx->dof.compositeShader.loaded)
    {
        gX_Shader cocS = gfx->dof.cocShader.shader;
        gfx->dof.cocDepthMapLoc = gX_Shader_GetLocation(cocS, "depthMap");
        gfx->dof.cocResLoc      = gX_Shader_GetLocation(cocS, "resolution");
        gfx->dof.cocFocalDistLoc = gX_Shader_GetLocation(cocS, "focalDistance");
        gfx->dof.cocFocalRangeLoc = gX_Shader_GetLocation(cocS, "focalRange");
        gfx->dof.cocNearLoc     = gX_Shader_GetLocation(cocS, "nearPlane");
        gfx->dof.cocFarLoc      = gX_Shader_GetLocation(cocS, "farPlane");
        gX_Shader blrS = gfx->dof.blurShader.shader;
        gfx->dof.blurColorMapLoc = gX_Shader_GetLocation(blrS, "colorMap");
        gfx->dof.blurCocMapLoc   = gX_Shader_GetLocation(blrS, "cocMap");
        gfx->dof.blurDirLoc      = gX_Shader_GetLocation(blrS, "direction");
        gfx->dof.blurResLoc      = gX_Shader_GetLocation(blrS, "resolution");
        gfx->dof.blurMaxRadiusLoc = gX_Shader_GetLocation(blrS, "maxRadius");
        gX_Shader cmpS = gfx->dof.compositeShader.shader;
        gfx->dof.compColorMapLoc = gX_Shader_GetLocation(cmpS, "colorMap");
        gfx->dof.compBlurMapLoc  = gX_Shader_GetLocation(cmpS, "blurMap");
        gfx->dof.compCocMapLoc   = gX_Shader_GetLocation(cmpS, "cocMap");
        gfx->dof.compDepthMapLoc = gX_Shader_GetLocation(cmpS, "depthMap");
        gfx->dof.locsCached = true;
        gfx->dof.cocShader.locsStale = false;
        gfx->dof.blurShader.locsStale = false;
        gfx->dof.compositeShader.locsStale = false;
    }

    if (gfx->state.ENABLE_MOTION_BLUR) {
        gX_RendererGFX_Shader_Update(&gfx->motionBlur.shader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->motionBlur.map);
    }
    if ((!gfx->motionBlur.locsCached || gfx->motionBlur.shader.locsStale) && gfx->motionBlur.shader.loaded)
    {
        gX_Shader mb = gfx->motionBlur.shader.shader;
        gfx->motionBlur.colorMapLoc  = gX_Shader_GetLocation(mb, "colorMap");
        gfx->motionBlur.depthMapLoc  = gX_Shader_GetLocation(mb, "depthMap");
        gfx->motionBlur.prevVPLoc    = gX_Shader_GetLocation(mb, "prevVP");
        gfx->motionBlur.invVPLoc     = gX_Shader_GetLocation(mb, "invVP");
        gfx->motionBlur.resolutionLoc = gX_Shader_GetLocation(mb, "resolution");
        gfx->motionBlur.samplesLoc   = gX_Shader_GetLocation(mb, "samples");
        gfx->motionBlur.strengthLoc  = gX_Shader_GetLocation(mb, "blurStrength");
        //opencode: velocity-tap locs live here (not in the dead
        //   MotionBlur_CacheLocs below): uncached struct fields stay 0 and
        //   pass every `>= 0` guard, uploading to location 0 of whatever
        //   program is current (caught on strict GL 3.2 as int-into-mvp).
        gfx->motionBlur.velocityMapLoc = gX_Shader_GetLocation(mb, "velocityMap");
        gfx->motionBlur.hasVelocityLoc = gX_Shader_GetLocation(mb, "hasVelocityBuffer");
        gfx->motionBlur.locsCached = true;
        gfx->motionBlur.shader.locsStale = false;
    }

    //opencode: TAA update — shader, RTs, jitter
    if (gfx->state.ENABLE_TAA) {
        gX_RendererGFX_Shader_Update(&gfx->taa.resolveShader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->taa.historyMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->taa.resolveMap);
    }
    if (gfx->state.ENABLE_TAA || gfx->ssao.useSSGI || gfx->state.ENABLE_MOTION_BLUR
        || (gfx->state.ENABLE_SSAO && gfx->ssao.useGTAO)) {
        gX_RendererGFX_Shader_Update(&gfx->velocity.shader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->velocity.map);
    }
    //opencode: RT inspector (GX_RT_VIEW) — viewer shader + locs, live only
    //   while the hook is set so default runs pay nothing.
    {
        const gX_I8 *rtviewHook = getenv("GX_RT_VIEW");
        if (rtviewHook && rtviewHook[0]) {
            gX_RendererGFX_Shader_Update(&gfx->rtview.shader, gfx);
            if ((!gfx->rtview.locsCached || gfx->rtview.shader.locsStale)
                && gfx->rtview.shader.loaded) {
                gX_Shader vs = gfx->rtview.shader.shader;
                gfx->rtview.colorMapLoc = gX_Shader_GetLocation(vs, "colorMap");
                gfx->rtview.cubeMapLoc = gX_Shader_GetLocation(vs, "cubeMap");
                gfx->rtview.cubeModeLoc = gX_Shader_GetLocation(vs, "cubeMode");
                gfx->rtview.channelModeLoc = gX_Shader_GetLocation(vs, "channelMode");
                gfx->rtview.gainLoc = gX_Shader_GetLocation(vs, "gain");
                gfx->rtview.biasLoc = gX_Shader_GetLocation(vs, "bias");
                gfx->rtview.locsCached = true;
                gfx->rtview.shader.locsStale = false;
            }
        }
    }
    //opencode: E-10 — scene switch: force-reclear the TAA history map
    //   so the new scene doesn't blend with stale ghost frames of the old one.
    if (maindata.masterIndexSwitched) {
        gX_RendererGFX_TemporalHistory_Reset(&gfx->taa.temporal);
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.gtaoTemporal);
        gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.ssgiTemporal);
        gfx->taa.jitter = (gX_F32v2){0.0f, 0.0f};
        gfx->taa.prevJitter = (gX_F32v2){0.0f, 0.0f};
    }
    {
        gX_I1 GtaoActive = gfx->state.ENABLE_SSAO && gfx->ssao.useGTAO;
        gX_I1 SsgiActive = gfx->state.ENABLE_SSAO && gfx->ssao.useSSGI;
        if (gfx->ssao.temporalModesInitialized) {
            if (GtaoActive != gfx->ssao.gtaoWasActive)
                gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.gtaoTemporal);
            if (SsgiActive != gfx->ssao.ssgiWasActive)
                gX_RendererGFX_TemporalHistory_Reset(&gfx->ssao.ssgiTemporal);
        }
        gfx->ssao.temporalModesInitialized = true;
        gfx->ssao.gtaoWasActive = GtaoActive;
        gfx->ssao.ssgiWasActive = SsgiActive;
    }
    //opencode: clear both temporal targets before their first use; the shader's
    //   black-history test cannot safely reject arbitrary GPU memory contents.
    if (gX_RendererGFX_TemporalHistory_NeedsClear(&gfx->taa.temporal)
        && gfx->taa.historyMap.wrapper.loadingState.loaded
        && gfx->taa.resolveMap.wrapper.loadingState.loaded) {
        gX_Drawing_BeginTexture(gfx->taa.historyMap.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 0});
        gX_Drawing_EndTexture();
        gX_Drawing_BeginTexture(gfx->taa.resolveMap.wrapper.rt);
            gX_Drawing_ClearBG((gX_Color){0, 0, 0, 0});
        gX_Drawing_EndTexture();
        gX_RendererGFX_TemporalHistory_MarkCleared(&gfx->taa.temporal);
    }
    //opencode: per-pixel roughness rides the hdrScene color ALPHA (standard.frag
    //   writes ssrRoughness into finalColor.a for opaque draws). The former MRT
    //   attachment-1 path was removed: the NVIDIA driver silently drops a
    //   fragment's location-1 output when the shader uses the full VS_OUT
    //   interface (verified via the mrtprobe harness — see session notes).
    if ((!gfx->taa.locsCached || gfx->taa.resolveShader.locsStale) && gfx->taa.resolveShader.loaded)
    {
        gX_Shader ts = gfx->taa.resolveShader.shader;
        gfx->taa.colorMapLoc    = gX_Shader_GetLocation(ts, "colorMap");
        gfx->taa.depthMapLoc    = gX_Shader_GetLocation(ts, "depthMap");
        gfx->taa.historyMapLoc  = gX_Shader_GetLocation(ts, "historyMap");
        gfx->taa.prevVPLoc      = gX_Shader_GetLocation(ts, "prevVP");
        gfx->taa.invVPLoc       = gX_Shader_GetLocation(ts, "invVP");
        gfx->taa.reprojectionValidLoc = gX_Shader_GetLocation(ts, "reprojectionValid");
        gfx->taa.resolutionLoc  = gX_Shader_GetLocation(ts, "resolution");
        gfx->taa.blendFactorLoc = gX_Shader_GetLocation(ts, "blendFactor");
        gfx->taa.jitterLoc      = gX_Shader_GetLocation(ts, "jitter");
        gfx->taa.prevJitterLoc  = gX_Shader_GetLocation(ts, "prevJitter");
        //opencode: velocity-buffer locs were never queried (stayed 0, hitting
        //   location 0 = the wrong uniform): the velocity path could never
        //   engage and hasVelocityBuffer stayed default-off in-shader.
        gfx->taa.velocityMapLoc = gX_Shader_GetLocation(ts, "velocityMap");
        gfx->taa.hasVelocityLoc = gX_Shader_GetLocation(ts, "hasVelocityBuffer");
        //opencode: oracle (Paper 1) - clamp-bypass loc; lives here (the live
        //   cache site), not TAA_CacheLocs (dead twin, never called).
        gfx->taa.oracleAccumLoc = gX_Shader_GetLocation(ts, "oracleAccum");
        //ai: refresh native temporal diagnostics together with the resolve shader.
        gfx->taa.hybridReprojectionDebugLoc = gX_Shader_GetLocation(ts, "hybridReprojectionDebug");
        gfx->taa.locsCached = true;
        gfx->taa.resolveShader.locsStale = false;
    }

    //opencode: godray update — shader + RT + loc caching
    if (gfx->state.ENABLE_GODRAYS) {
        gX_RendererGFX_Shader_Update(&gfx->godray.genShader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->godray.genMap);
    }
    gX_RendererGFX_Godray_CacheLocs(gfx);

    //opencode: Paper 2 spike — terrain depth shader (hook-gated update).
    {
        const gX_I8 *th = getenv("GX_T_TERRAINDEPTH");
        if (th && th[0] != '\0' && atof((const char *)th) != 0.0) {
            gX_RendererGFX_Shader_Update(&gfx->terrain.depthShader, gfx);
            gX_RendererGFX_Shader_Update(&gfx->terrain.velShader, gfx);
        }
    }
    gX_RendererGFX_Terrain_CacheLocs(gfx);

    //opencode: contact shadows (spike 1) — hook-gated update (both load
    //   and loc caching: querying locations on an unloaded program emits
    //   GL_INVALID_VALUE noise into VALIDATE runs).
    {
        const gX_I8 *ch = getenv("GX_T_CONTACT");
        if (ch && ch[0] != '\0' && ch[0] != '0') {
            gX_RendererGFX_Shader_Update(&gfx->contact.genShader, gfx);
            gX_RendererGFX_RenderTexture_Update(&gfx->contact.map);
            gX_RendererGFX_Contact_CacheLocs(gfx);
        }
    }

    //opencode: volumetric fog update — shaders + RT + loc caching
    if (gfx->state.ENABLE_VOLUME_FOG) {
        gX_RendererGFX_Shader_Update(&gfx->fogVolume.genShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->fogVolume.blendShader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->fogVolume.genMap);
    }
    gX_RendererGFX_FogVolume_CacheLocs(gfx);

    //opencode: lens flare update — shader + loc caching (no RT)
    if (gfx->state.ENABLE_LENS_FLARE)
        gX_RendererGFX_Shader_Update(&gfx->lensflare.shader, gfx);
    gX_RendererGFX_LensFlare_CacheLocs(gfx);

    //opencode: particle system update (compute dispatch)
    gX_RendererGFX_Particle_Update(gfx);

    //opencode: CPU falling snow update (deterministic sim)
    gX_RendererGFX_Weather_Update(gfx);

    //opencode: gaussian splat fixture update (env/LOD)
    gX_RendererGFX_Splat_Update(gfx);

    //opencode: IBL shader updates
    gX_RendererGFX_Shader_Update(&gfx->ibl.brdfShader, gfx);
    gX_RendererGFX_Shader_Update(&gfx->ibl.irradianceShader, gfx);
    gX_RendererGFX_Shader_Update(&gfx->ibl.prefilterShader, gfx);
    //opencode: IBL generation — BRDF LUT + irradiance + prefilter (one-time,
    //   deferred GL). Gated on ENABLE_ENV_AMBIENT: when it's off the shader
    //   receives envAmbientFactor=0 so IBL contributes exactly zero (and at
    //   QUALITY=0 the IBL samplers are compiled out entirely) — generating the
    //   maps is pure dead work. On software rasterizers (lavapipe) the
    //   irradiance convolution alone is ~97M cubemap samples ≈ 30-90s of
    //   single-thread rasterization before the first frame presents.
    if (gfx->state.ENABLE_ENV_AMBIENT && !gfx->ibl.ready &&
        gfx->ibl.brdfShader.loaded && gfx->ibl.irradianceShader.loaded &&
        gfx->ibl.prefilterShader.loaded && gfx->reflection.cubeReady)
    {
        //opencode: save viewport + active texture unit before IBL generation
        gX_I32 savedVp[4], savedTexUnit, savedProg;
        gX_RHI_GetIntegerv(gX_RHI_VIEWPORT, savedVp);
        gX_RHI_GetIntegerv(gX_RHI_ACTIVE_TEXTURE, &savedTexUnit);
        gX_RHI_GetIntegerv(gX_RHI_CURRENT_PROGRAM, &savedProg);
        gX_RHI_FBO savedFbo = gX_RHI_GetDrawFBO();

        const gX_I32 lutSize = gX_GFX_IBL_LUT_SIZE;
        gX_U32 capFbo = 0;
        gX_RHI_FBO_Gen(1, &capFbo);

        // ---- 1. BRDF LUT (2D RG16F) ----
        {
            gX_U32 lutTex = 0;
            lutTex = gX_RHI_Tex_Create2D((gX_U32)lutSize, (gX_U32)lutSize, gX_RHI_RG16F, gX_RHI_RG, gX_RHI_FLOAT, NULL, "brdf_lut");
            gX_RHI_Tex_Param(lutTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
            gX_RHI_Tex_Param(lutTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
            gX_RHI_Tex_Param(lutTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
            gX_RHI_Tex_Param(lutTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);

            gX_RHI_BindFBO(capFbo);
            gX_RHI_FBO_AttachTexture(lutTex, gX_RHI_COLOR_ATTACHMENT0, gX_RHI_TEXTURE_2D);
            if (!gX_RHI_FBO_IsComplete()) {
                gX_TraceLog_GFX(gX_LOG_ERROR, "IBL BRDF LUT FBO incomplete — IBL disabled");
                gfx->ibl.failed = true;
                if (lutTex) gX_RHI_Tex_Delete(lutTex);
                gX_RHI_FBO_Delete(capFbo);
                gX_RHI_Viewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
                gX_RHI_ActiveTexture((gX_U32)savedTexUnit);
                gX_RHI_Shader_Use((gX_U32)savedProg);
                gX_RHI_BindFBO(savedFbo);
                return;
            }
            gX_RHI_Viewport(0, 0, lutSize, lutSize);
            gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);

            gX_Shader brdfS = gfx->ibl.brdfShader.shader;
            gX_Drawing_BeginShader(brdfS);
                gX_F32v2 brdfRes = {(gX_F32)lutSize, (gX_F32)lutSize};
                gX_Shader_SetValue_V2(brdfS, gX_Shader_GetLocation(brdfS, "resolution"), brdfRes);
                gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)lutSize, (gX_F32)lutSize);
            gX_Drawing_EndShader();

            gfx->ibl.brdfLut.id = lutTex;
            gfx->ibl.brdfLut.width = lutSize;
            gfx->ibl.brdfLut.height = lutSize;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "IBL BRDF LUT generated (%dx%d RG16F)", lutSize, lutSize);
        }

        // ---- 2. Irradiance cubemap (32x32 RGB16F) ----
        gX_U32 skyboxId = gfx->sky.skybox.wrapper.texture.id;
        gX_Shader irrS = gfx->ibl.irradianceShader.shader;
        if (skyboxId > 0 && gfx->ibl.irradianceMap.id == 0 && irrS.id > 0)
        {
            const gX_I32 irrSize = 32;
            const gX_F32v3 targets[6] = {{ 1,0,0},{-1,0,0},{ 0, 1, 0},{ 0,-1, 0},{ 0,0, 1},{ 0,0,-1}};
            const gX_F32v3 ups[6]      = {{ 0,-1,0},{ 0,-1,0},{ 0, 0, 1},{ 0, 0,-1},{ 0,-1, 0},{ 0,-1, 0}};

            gX_U32 irrTex = 0;
            irrTex = gX_RHI_Tex_CreateCubemap((gX_U32)irrSize, gX_RHI_RGB16F, gX_RHI_RGB, gX_RHI_FLOAT, "irradiance");
            gX_RHI_Tex_Param(irrTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
            gX_RHI_Tex_Param(irrTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
            gX_RHI_Tex_Param(irrTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
            gX_RHI_Tex_Param(irrTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
            gX_RHI_Tex_Param(irrTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);

            gX_I32 sourceLoc = gX_RHI_GetUniformLocation(irrS.id, "sourceMap");
            gX_I32 cvLoc = gX_RHI_GetUniformLocation(irrS.id, "captureView");
            gX_RHI_BindFBO(capFbo);
            if (!gX_RHI_FBO_IsComplete()) {
                gX_TraceLog_GFX(gX_LOG_ERROR, "IBL irradiance FBO incomplete — IBL disabled");
                gfx->ibl.failed = true;
                gX_RHI_FBO_Delete(capFbo);
                gX_RHI_Viewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
                gX_RHI_ActiveTexture((gX_U32)savedTexUnit);
                gX_RHI_Shader_Use((gX_U32)savedProg);
                gX_RHI_BindFBO((gX_RHI_FBO)savedFbo);
                return;
            }
            for (gX_I32 face = 0; face < 6; face++)
            {
                gX_RHI_FBO_AttachCubeFace(irrTex, (gX_U32)face);
                gX_RHI_Viewport(0, 0, irrSize, irrSize);
                gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);

                gX_Matrix faceView = gX_Matrix_LookAt(
                    (gX_F32v3){0,0,0}, targets[face], ups[face]);
                gX_Drawing_BeginShader(irrS);
                    gX_RHI_Tex_BindCubemapOnUnit(skyboxId, 0);
                    gX_RHI_UniformInt(sourceLoc, 0);
                    gX_RHI_UniformMatrix(cvLoc, 1, 1, &faceView.m0);
                    gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)irrSize, (gX_F32)irrSize);
                gX_Drawing_EndShader();
            }

            gfx->ibl.irradianceMap.id = irrTex;
            gfx->ibl.irradianceMap.width = irrSize;
            gfx->ibl.irradianceMap.height = irrSize;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "IBL irradiance cubemap generated (%dx%d)", irrSize, irrSize);
        }

        // ---- 3. Prefilter cubemap (128x128 RGB16F, 5 mip levels) ----
        gX_Shader preS = gfx->ibl.prefilterShader.shader;
        if (skyboxId > 0 && gfx->ibl.prefilterMap.id == 0 && preS.id > 0)
        {
            const gX_I32 preSize = 128;
            const gX_I32 preMips = 5; // 128→64→32→16→8
            const gX_F32v3 targets[6] = {{ 1,0,0},{-1,0,0},{ 0, 1, 0},{ 0,-1, 0},{ 0,0, 1},{ 0,0,-1}};
            const gX_F32v3 ups[6]      = {{ 0,-1,0},{ 0,-1,0},{ 0, 0, 1},{ 0, 0,-1},{ 0,-1, 0},{ 0,-1, 0}};

            gX_U32 preTex = 0;
            //opencode: immutable storage defines all 5 mips atomically — mutable
            //   glTexImage2D mip allocation left levels undefined on NVIDIA
            //   (silently, no GL error), making every per-mip FBO attachment
            //   GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT.
            preTex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_CUBE_MAP, "prefilter");
            if (preTex) {
                gX_RHI_Tex_Storage2D(preTex, gX_RHI_TEXTURE_CUBE_MAP, preMips,
                    gX_RHI_RGB16F, (gX_U32)preSize, (gX_U32)preSize);
            }
            gX_RHI_Tex_Param(preTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
            gX_RHI_Tex_Param(preTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
            gX_RHI_Tex_Param(preTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
            gX_RHI_Tex_Param(preTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
            gX_RHI_Tex_Param(preTex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);

            gX_I32 sourceLoc = gX_RHI_GetUniformLocation(preS.id, "sourceMap");
            gX_I32 preResLoc = gX_RHI_GetUniformLocation(preS.id, "resolution");
            gX_I32 roughLoc  = gX_RHI_GetUniformLocation(preS.id, "roughness");
            gX_I32 preCVLoc  = gX_RHI_GetUniformLocation(preS.id, "captureView");

            gX_RHI_BindFBO(capFbo);
            if (!gX_RHI_FBO_IsComplete()) {
                gX_TraceLog_GFX(gX_LOG_ERROR, "IBL prefilter FBO incomplete — IBL disabled");
                gfx->ibl.failed = true;
                gX_RHI_FBO_Delete(capFbo);
                gX_RHI_Viewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
                gX_RHI_ActiveTexture((gX_U32)savedTexUnit);
                gX_RHI_Shader_Use((gX_U32)savedProg);
                gX_RHI_BindFBO((gX_RHI_FBO)savedFbo);
                return;
            }
            //opencode: explicit draw/read buffers — stale per-name FBO state
            //   (e.g. GL_NONE from a recycled colorless FBO) made glClear raise
            //   GL_INVALID_FRAMEBUFFER_OPERATION despite a complete framebuffer.
            gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
            gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
            for (gX_I32 mip = 0; mip < preMips; mip++)
            {
                gX_I32 mipSize = preSize >> mip;
                gX_F32 roughness = (gX_F32)mip / (gX_F32)(preMips - 1);
                for (gX_I32 face = 0; face < 6; face++)
                {
                    gX_RHI_FBO_AttachCubeFaceLevel(preTex, (gX_U32)face, mip);
                    gX_RHI_Viewport(0, 0, mipSize, mipSize);
                    gX_RHI_Clear(gX_RHI_COLOR_BUFFER_BIT);

                    gX_Matrix faceView = gX_Matrix_LookAt(
                        (gX_F32v3){0,0,0}, targets[face], ups[face]);
                    gX_Drawing_BeginShader(preS);
                        gX_RHI_Tex_BindCubemapOnUnit(skyboxId, 0);
                        gX_RHI_UniformInt(sourceLoc, 0);
                        gX_RHI_Uniform_Float(preResLoc, (gX_F32)mipSize);
                        gX_RHI_Uniform_Float(roughLoc, roughness);
                        gX_RHI_UniformMatrix(preCVLoc, 1, 1, &faceView.m0);
                        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)mipSize, (gX_F32)mipSize);
                    gX_Drawing_EndShader();
                }
            }

            gfx->ibl.prefilterMap.id = preTex;
            gfx->ibl.prefilterMap.width = preSize;
            gfx->ibl.prefilterMap.height = preSize;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "IBL prefilter cubemap generated (%dx%d, %d mips)", preSize, preSize, preMips);
        }

        gX_RHI_FBO_Delete(capFbo);
        //opencode: restore viewport, active texture, program, and FBO after IBL generation
        gX_RHI_Viewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
        gX_RHI_ActiveTexture((gX_U32)savedTexUnit);
        gX_RHI_Shader_Use((gX_U32)savedProg);
        gX_RHI_BindFBO((gX_RHI_FBO)savedFbo);
        gfx->ibl.ready = (gfx->ibl.brdfLut.id > 0 && gfx->ibl.irradianceMap.id > 0 && gfx->ibl.prefilterMap.id > 0);
        if (gfx->ibl.ready)
            gX_TraceLog_GFX(gX_LOG_NOTICE, "IBL ready — all 3 textures generated");
    }

    gX_RendererGFX_RenderTexture_Update(&gfx->bloom.hdrScene);
    gX_RendererGFX_RenderTexture_Update(&gfx->bloom.luminanceRT); //opencode: v0.3-M1 — auto-exposure target
    //opencode: bloom.scene is the composite target for all effects (godrays, lensflare,
    //   fog, SSR, bloom). Update it whenever any effect is active.
    //   (ENABLE_VOLUME_FOG was missing: volume-fog-only configs never loaded
    //   bloom.scene, so scene_copy + the fog composite wrote nowhere and the
    //   present fell back to pristine hdrScene - fog silently invisible
    //   unless SSR/BLOOM loaded the RT as a side effect.)
    if (gfx->state.ENABLE_BLOOM || gfx->state.ENABLE_GODRAYS || gfx->state.ENABLE_LENS_FLARE ||
        gfx->state.ENABLE_FOG || gfx->state.ENABLE_SSR || gfx->state.ENABLE_VOLUME_FOG ||
        (gfx->state.ENABLE_SSAO && gfx->ssao.useSSGI)) {
        gX_RendererGFX_RenderTexture_Update(&gfx->bloom.extract);
        gX_RendererGFX_RenderTexture_Update(&gfx->bloom.ping);
        gX_RendererGFX_RenderTexture_Update(&gfx->bloom.pong);
        gX_RendererGFX_RenderTexture_Update(&gfx->bloom.scene);
    }
    if (gfx->state.ENABLE_PLANAR_REFLECTIONS
        || (gfx->state.ENABLE_SSR && gfx->state.ENABLE_SSR_AUXVIEW))
        gX_RendererGFX_RenderTexture_Update(&gfx->reflection.planar);
    gX_RendererGFX_RenderTexture_Update(&gfx->sceneColorLDR);

    //opencode: clear SSAO maps to white on first load so the main shader
    //   doesn't sample garbage during model draw (SSAO compute runs after
    //   opaque draw, but the shader reads ssaoBlurMap during the draw).
    {
        if (!ssaoCleared && gfx->ssao.blurMap.wrapper.loadingState.loaded) {
            gX_Drawing_BeginTexture(gfx->ssao.map.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
            gX_Drawing_EndTexture();
            gX_Drawing_BeginTexture(gfx->ssao.blurMap.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
            gX_Drawing_EndTexture();
            ssaoCleared = true;
        }
    }

    //opencode: decal quad mesh — deferred GL init (needs GL context)
    if (gfx->decalQuad.meshCount == 0)
    {
        gX_Mesh quad = gX_Mesh_GenPlane(1.0f, 1.0f, 1, 1);
        gfx->decalQuad = gX_Model_LoadMesh(quad);
        gX_TraceLog_GFX(gX_LOG_TRACE, "Decal quad model created");
    }

    //opencode: dynamic reflection cubemap — deferred GL init (GL context not ready during Load)
    if (!gfx->reflection.cubeReady)
    {
        gX_U32 size = gX_GFX_REFLECTION_CUBE_SIZE; //opencode: v0.1.14 — 512 (was shadow-map 1024): 4x cheaper, visually identical for the blurry env fallback
        gfx->reflection.cubeSize = size;
        gX_U32 fbo = 0, depthRbo = 0;
        gX_U32 tex = gX_RHI_Tex_CreateCubemap(size, gX_RHI_RGBA16F, gX_RHI_RGBA, gX_RHI_HALF_FLOAT, "reflection_cube");
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
        gX_RHI_Tex_Param(tex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);
        //opencode: generate mipmaps now so first-frame sampling is valid even
        // before the first reflection render (refraction, SSR use the cubemap too)
        gX_RHI_Tex_GenMipmap(tex, gX_RHI_TEXTURE_CUBE_MAP);

        depthRbo = gX_RHI_RBO_Create(size, size, gX_RHI_DEPTH_COMPONENT24);
        gX_RHI_FBO_Gen(1, &fbo);
        gX_RHI_BindFBO(fbo);
        gX_RHI_FBO_AttachDepthRBO(depthRbo);
        //opencode: v0.1.16 — was DrawBuffer/ReadBuffer(NONE): every color clear
        //   AND draw into the faces was silently discarded (the completeness
        //   check passed — a depth-only FBO is complete) and the cubemap stayed
        //   BLACK forever. RenderEx's reuse branch never re-enables the draw
        //   buffer, so the color attachment must be wired at creation.
        gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
        if (!gX_RHI_FBO_IsComplete()) {
            gX_TraceLog_GFX(gX_LOG_ERROR, "reflCube FBO incomplete");
            gX_RHI_FBO_Delete(fbo);
            gX_RHI_RBO_Delete(depthRbo);
            gX_RHI_Tex_Delete(tex);
            gfx->reflection.dynamicCubemap.id = 0;
            gX_RHI_UnbindFBO();
            gX_RHI_DrawBuffer(gX_RHI_BACK);
            gfx->reflection.cubeReady = false;
            return;
        }
        gX_RHI_UnbindFBO();

        gfx->reflection.dynamicCubemap.id = tex;
        gfx->reflection.dynamicCubemap.width = (gX_I32)size;
        gfx->reflection.dynamicCubemap.height = (gX_I32)size;
        gfx->reflection.cubeFboID = fbo;
        gfx->reflection.cubeDepthRbo = depthRbo;
        gfx->reflection.cubeReady = true;
        //opencode: seamless cubemap sampling — blends across face boundaries
        gX_RHI_Enable(gX_RHI_TEXTURE_CUBE_MAP_SEAMLESS);
    }

    //opencode: probe grid MVP-1 — static baked probes (GX_PROBE_GRID=1).
    //   Same texture format/filter/wrap as the dynamic cubemap so the
    //   existing box projection and roughness sampling behave identically;
    //   only smaller (default 128). Faces render on demand in draw.c.
    {
        const gX_I8 *probeHook = getenv("GX_PROBE_GRID");
        gX_I1 wantProbes = (probeHook && atoi((const char *)probeHook) != 0) ? 1 : 0;
        if (wantProbes && gfx->reflection.probeCount == 0 && gfx->reflection.cubeReady) {
            const gX_I8 *sizeHook = getenv("GX_PROBE_SIZE");
            gX_U32 psize = (sizeHook && atoi((const char *)sizeHook) > 0)
                ? (gX_U32)atoi((const char *)sizeHook) : 128u;
            static const gX_F32v3 probePos[GFX_PROBE_MAX] = {
                {-3.0f, 1.5f, -3.0f}, {3.0f, 1.5f, -3.0f},
                {-3.0f, 1.5f, 3.0f}, {3.0f, 1.5f, 3.0f}
            };
            gX_U32 made = 0;
            for (gX_U32 pi = 0; pi < (gX_U32)GFX_PROBE_MAX; pi++) {
                gX_U32 ptex = gX_RHI_Tex_CreateCubemap(psize, gX_RHI_RGBA16F,
                    gX_RHI_RGBA, gX_RHI_HALF_FLOAT, "probe_cube");
                if (ptex == 0) break;
                gX_RHI_Tex_Param(ptex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR_MIPMAP_LINEAR);
                gX_RHI_Tex_Param(ptex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
                gX_RHI_Tex_Param(ptex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_Param(ptex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_Param(ptex, gX_RHI_TEXTURE_CUBE_MAP, gX_RHI_TEXTURE_WRAP_R, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_GenMipmap(ptex, gX_RHI_TEXTURE_CUBE_MAP);
                gX_U32 pdepth = gX_RHI_RBO_Create(psize, psize, gX_RHI_DEPTH_COMPONENT24);
                gX_U32 pfbo = 0;
                gX_RHI_FBO_Gen(1, &pfbo);
                gX_RHI_BindFBO(pfbo);
                gX_RHI_FBO_AttachDepthRBO(pdepth);
                gX_RHI_DrawBuffer(gX_RHI_COLOR_ATTACHMENT0);
                gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                if (!gX_RHI_FBO_IsComplete()) {
                    gX_TraceLog_GFX(gX_LOG_ERROR, "probe cube FBO incomplete");
                    gX_RHI_FBO_Delete(pfbo);
                    gX_RHI_RBO_Delete(pdepth);
                    gX_RHI_Tex_Delete(ptex);
                    gX_RHI_UnbindFBO();
                    break;
                }
                gX_RHI_UnbindFBO();
                gfx->reflection.probes[pi].pos = probePos[pi];
                gfx->reflection.probes[pi].cube.id = ptex;
                gfx->reflection.probes[pi].cube.width = (gX_I32)psize;
                gfx->reflection.probes[pi].cube.height = (gX_I32)psize;
                gfx->reflection.probes[pi].fboID = pfbo;
                gfx->reflection.probes[pi].depthRbo = pdepth;
                gfx->reflection.probes[pi].baked = false;
                //ai: only a completed capture can establish direction-only content.
                gfx->reflection.probes[pi].skyOnly = false;
                made++;
            }
            gfx->reflection.probeCount = made;
            gfx->reflection.probeSize = psize;
            gfx->reflection.nextProbe = 0;
            gfx->reflection.probesReady = false;
            gX_TraceLog_GFX(gX_LOG_NOTICE, "probe grid: %u probes at %u", made, psize);
        }
    }

    if (gfx->state.ENABLE_SSR) {
        gX_RendererGFX_RenderTexture_Update(&gfx->ssr.map);
        if (gfx->ssr.normalBufferEnabled)
            gX_RendererGFX_RenderTexture_Update(&gfx->ssr.normalMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssr.skyLut);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssr.fogLut);   //opencode: v0.3.8
        gX_RendererGFX_Shader_Update(&gfx->ssr.shader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->ssr.hizShader, gfx);
        //opencode: recreate the Hi-Z chain if the SSR RTs (re)loaded at a new size
        if (gfx->ssr.hizInited &&
            (gfx->ssr.hizTex.width != maindata.renderWidth ||
             gfx->ssr.hizTex.height != maindata.renderHeight))
        {
            if (gfx->ssr.hizTex.id > 0) gX_RHI_Tex_Delete(gfx->ssr.hizTex.id);
            if (gfx->ssr.hizFbo > 0) gX_RHI_FBO_Delete(gfx->ssr.hizFbo);
            gfx->ssr.hizTex.id = 0;
            gfx->ssr.hizFbo = 0;
            gfx->ssr.hizInited = false;
        }
        //opencode: one-shot Hi-Z texture + FBO creation (immutable storage so
        //   every mip level exists for FBO attachment, like the IBL prefilter)
        if (!gfx->ssr.hizInited && gfx->bloom.hdrScene.wrapper.loadingState.loaded)
        {
            gX_U32 hizW = maindata.renderWidth;
            gX_U32 hizH = maindata.renderHeight;
            gX_I32 levels = 1;
            while (((hizW >> levels) > 0) || ((hizH >> levels) > 0)) levels++;
            gX_U32 hizTex = gX_RHI_Tex_Gen(gX_RHI_TEXTURE_2D, "ssr_hiz");
            if (hizTex)
            {
                gX_RHI_Tex_Storage2D(hizTex, gX_RHI_TEXTURE_2D, levels, gX_RHI_R32F, hizW, hizH);
                gX_RHI_Tex_Param(hizTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST_MIPMAP_NEAREST);
                gX_RHI_Tex_Param(hizTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
                gX_RHI_Tex_Param(hizTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_Param(hizTex, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
            }
            gX_U32 hizFbo = gX_RHI_FBO_Create(hizTex, 0, "ssr_hiz_fbo");
            if (hizFbo && hizTex && !gX_RHI_FBO_IsComplete())
            {
                gX_TraceLog_GFX(gX_LOG_ERROR, "SSR Hi-Z FBO incomplete — Hi-Z disabled");
                gX_RHI_FBO_Delete(hizFbo);
                gX_RHI_Tex_Delete(hizTex);
                hizFbo = 0;
                hizTex = 0;
            }
            gX_RHI_UnbindFBO();
            gfx->ssr.hizTex.id = hizTex;
            gfx->ssr.hizTex.width = hizW;
            gfx->ssr.hizTex.height = hizH;
            gfx->ssr.hizFbo = hizFbo;
            gfx->ssr.hizLevels = levels;
            gfx->ssr.hizInited = true;
            gX_TraceLog_GFX(gX_LOG_INFO, "SSR Hi-Z created (%ux%u, %d levels)", hizW, hizH, levels);
        }
    }
    gX_RendererGFX_RenderTexture_Update(&gfx->debugVis.refractionBg);
    if (gfx->state.ENABLE_SSAO) {
        gX_RendererGFX_Shader_Update(&gfx->ssao.gtaoShader, gfx); //opencode: v0.3-M2
        gX_RendererGFX_Shader_Update(&gfx->ssao.gtaoAccumShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->ssao.ssgiShader, gfx); //opencode: v0.3-M4
        gX_RendererGFX_Shader_Update(&gfx->ssao.ssgiBlurShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->ssao.ssgiAccumShader, gfx);
        gX_RendererGFX_Shader_Update(&gfx->ssao.blurShader, gfx);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.map);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.blurMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.gtaoAccumA);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.gtaoAccumB);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.ssgiMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.ssgiBlurMap);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.ssgiAccumA);
        gX_RendererGFX_RenderTexture_Update(&gfx->ssao.ssgiAccumB); //opencode: v0.3-M4
        if (gX_RendererGFX_TemporalHistory_NeedsClear(&gfx->ssao.gtaoTemporal)
            && gfx->ssao.gtaoAccumA.wrapper.loadingState.loaded
            && gfx->ssao.gtaoAccumB.wrapper.loadingState.loaded) {
            gX_Drawing_BeginTexture(gfx->ssao.gtaoAccumA.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
            gX_Drawing_EndTexture();
            gX_Drawing_BeginTexture(gfx->ssao.gtaoAccumB.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){255, 255, 255, 255});
            gX_Drawing_EndTexture();
            gX_RendererGFX_TemporalHistory_MarkCleared(&gfx->ssao.gtaoTemporal);
        }
        if (gX_RendererGFX_TemporalHistory_NeedsClear(&gfx->ssao.ssgiTemporal)
            && gfx->ssao.ssgiAccumA.wrapper.loadingState.loaded
            && gfx->ssao.ssgiAccumB.wrapper.loadingState.loaded) {
            gX_Drawing_BeginTexture(gfx->ssao.ssgiAccumA.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
            gX_Drawing_EndTexture();
            gX_Drawing_BeginTexture(gfx->ssao.ssgiAccumB.wrapper.rt);
                gX_Drawing_ClearBG((gX_Color){0, 0, 0, 255});
            gX_Drawing_EndTexture();
            gX_RendererGFX_TemporalHistory_MarkCleared(&gfx->ssao.ssgiTemporal);
        }
    }
    // SSR uniform locations (cached once)
    {
        static gX_I1 ssrLocsCached = false;
        if ((!ssrLocsCached || gfx->ssr.shader.locsStale) && gfx->ssr.shader.loaded)
        {
            gX_Shader sh = gfx->ssr.shader.shader;
            gfx->ssr.depthMapLoc = gX_Shader_GetLocation(sh, "depthMap");
            gfx->ssr.colorMapLoc = gX_Shader_GetLocation(sh, "colorMap");
            gfx->ssr.normalMapLoc = gX_Shader_GetLocation(sh, "normalMap");
            gfx->ssr.hasNormalMapLoc = gX_Shader_GetLocation(sh, "hasNormalMap");
            gfx->ssr.normalFallbackAngleLoc = gX_Shader_GetLocation(sh, "normalFallbackAngle");
            gfx->ssr.normalLiftModeLoc = gX_Shader_GetLocation(sh, "normalLiftMode");
            gfx->ssr.normalLiftHybridAngleLoc = gX_Shader_GetLocation(sh, "normalLiftHybridAngle");
            gfx->ssr.normalAcceptModeLoc = gX_Shader_GetLocation(sh, "normalAcceptMode");
            //opencode: cache edge-ray A/B control.
            gfx->ssr.selfPlaneRejectEnabledLoc = gX_Shader_GetLocation(sh, "selfPlaneRejectEnabled");
            gfx->ssr.vpLoc = gX_Shader_GetLocation(sh, "vp");
            gfx->ssr.invVPLoc = gX_Shader_GetLocation(sh, "invVP");
            gfx->ssr.camPosLoc = gX_Shader_GetLocation(sh, "cameraPos");
            //opencode: SSR tuning parameter locations
            gfx->ssr.maxStepsLoc = gX_Shader_GetLocation(sh, "maxSteps");
            gfx->ssr.stepSizeLoc = gX_Shader_GetLocation(sh, "stepSize");
            gfx->ssr.maxDistanceLoc = gX_Shader_GetLocation(sh, "maxDistance");
            gfx->ssr.depthBiasLoc = gX_Shader_GetLocation(sh, "depthBias");
            gfx->ssr.planarMapLoc = gX_Shader_GetLocation(sh, "planarMap");
            gfx->ssr.planarDepthMapLoc = gX_Shader_GetLocation(sh, "planarDepthMap");
            gfx->ssr.hasPlanarDepthLoc = gX_Shader_GetLocation(sh, "hasPlanarDepth");
            gfx->ssr.hasPlanarLoc = gX_Shader_GetLocation(sh, "hasPlanar");
            gfx->ssr.planarVPLoc = gX_Shader_GetLocation(sh, "planarVP");
            gfx->ssr.planeDistLoc = gX_Shader_GetLocation(sh, "planeDist");
            gfx->ssr.planeNormalLoc = gX_Shader_GetLocation(sh, "planeNormal");
            gfx->ssr.opacityLoc = gX_Shader_GetLocation(sh, "opacity");
            gfx->ssr.roughnessLoc = gX_Shader_GetLocation(sh, "roughness");
            gfx->ssr.maxLodLoc = gX_Shader_GetLocation(sh, "maxLod");
            gfx->ssr.maxDepthDeltaLoc = gX_Shader_GetLocation(sh, "maxDepthDelta");
            //opencode: cache opt-in silhouette distance-scale A/B control.
            gfx->ssr.maxDepthDistanceScaleLoc = gX_Shader_GetLocation(sh, "maxDepthDistanceScale");
            gfx->ssr.worldGapThresholdLoc = gX_Shader_GetLocation(sh, "worldGapThreshold");
            gfx->ssr.finalDepthThresholdLoc = gX_Shader_GetLocation(sh, "finalDepthThreshold");
            gfx->ssr.hitFacingThresholdLoc = gX_Shader_GetLocation(sh, "hitFacingThreshold");
            gfx->ssr.debugModeLoc = gX_Shader_GetLocation(sh, "debugMode");
            gfx->ssr.planarDeferLoc = gX_Shader_GetLocation(sh, "ssrPlanarDefer");
            gfx->ssr.hizMapLoc = gX_Shader_GetLocation(sh, "hizMap");
            gfx->ssr.skyLutMapLoc = gX_Shader_GetLocation(sh, "skyLutMap");
            //opencode: v0.3.8 — fog delta LUT for sky escapes
            gfx->ssr.fogLutMapLoc = gX_Shader_GetLocation(sh, "fogLutMap");
            gfx->ssr.hasFogLutLoc = gX_Shader_GetLocation(sh, "hasFogLut");
            gfx->ssr.sunDirLoc = gX_Shader_GetLocation(sh, "sunDir");
            gfx->ssr.sunSizeLoc = gX_Shader_GetLocation(sh, "sunSize");
            gfx->ssr.sunColorLoc = gX_Shader_GetLocation(sh, "sunColor");
            gfx->ssr.waterYLoc = gX_Shader_GetLocation(sh, "waterY");
            gfx->ssr.hasWaterLoc = gX_Shader_GetLocation(sh, "hasWater");
            //opencode: v0.1.15 — water SSR wave/ripple uniforms
            gfx->ssr.waterWaveCountLoc = gX_Shader_GetLocation(sh, "waterWaveCount");
            gfx->ssr.waterWaveDirsLoc = gX_Shader_GetLocation(sh, "waterWaveDirs");
            gfx->ssr.waterWaveAmpsLoc = gX_Shader_GetLocation(sh, "waterWaveAmps");
            gfx->ssr.waterWaveFreqsLoc = gX_Shader_GetLocation(sh, "waterWaveFreqs");
            gfx->ssr.waterWaveSpeedsLoc = gX_Shader_GetLocation(sh, "waterWaveSpeeds");
            gfx->ssr.waterWaveSteepsLoc = gX_Shader_GetLocation(sh, "waterWaveSteeps");
            gfx->ssr.waterRippleCountLoc = gX_Shader_GetLocation(sh, "waterRippleCount");
            gfx->ssr.waterRipplePosLoc = gX_Shader_GetLocation(sh, "waterRipplePos");
            gfx->ssr.waterRippleAmpLoc = gX_Shader_GetLocation(sh, "waterRippleAmp");
            gfx->ssr.waterRippleRadiusLoc = gX_Shader_GetLocation(sh, "waterRippleRadius");
            gfx->ssr.waterRipplePhaseLoc = gX_Shader_GetLocation(sh, "waterRipplePhase");
            gfx->ssr.waterTimeLoc = gX_Shader_GetLocation(sh, "waterTime");
            //opencode: v0.1.11 — cubemap env fallback + edge/bounce tuning
            gfx->ssr.envMapLoc = gX_Shader_GetLocation(sh, "envMap");
            gfx->ssr.hasEnvMapLoc = gX_Shader_GetLocation(sh, "hasEnvMap");
            //opencode: M1-b — velocity.w glass flag for the env-escape
            gfx->ssr.velMapLoc = gX_Shader_GetLocation(sh, "velocityMap");
            gfx->ssr.hasVelMapLoc = gX_Shader_GetLocation(sh, "hasVelocityMap");
            gfx->ssr.glassOnLoc = gX_Shader_GetLocation(sh, "ssrGlassOn");
            //ai: follow shader reloads without per-frame location queries.
            gfx->ssr.hybridGapTraversalLoc = gX_Shader_GetLocation(sh, "hybridGapTraversal");
            gfx->ssr.edgeFadeLoc = gX_Shader_GetLocation(sh, "edgeFade");
            gfx->ssr.maxBouncesLoc = gX_Shader_GetLocation(sh, "maxBounces");
            gfx->ssr.bounceAttenLoc = gX_Shader_GetLocation(sh, "bounceAtten");
            ssrLocsCached = true;
            gfx->ssr.shader.locsStale = false;
        }
    }
    //opencode: SSR Hi-Z shader locations (cached once)
    {
        static gX_I1 ssrHizLocsCached = false;
        if ((!ssrHizLocsCached || gfx->ssr.hizShader.locsStale) && gfx->ssr.hizShader.loaded)
        {
            gX_Shader hz = gfx->ssr.hizShader.shader;
            gfx->ssr.hizDepthMapLoc = gX_Shader_GetLocation(hz, "depthMap");
            gfx->ssr.hizHizMapLoc   = gX_Shader_GetLocation(hz, "hizMap");
            gfx->ssr.hizModeLoc     = gX_Shader_GetLocation(hz, "mode");
            gfx->ssr.hizSrcLevelLoc = gX_Shader_GetLocation(hz, "srcLevel");
            ssrHizLocsCached = true;
            gfx->ssr.hizShader.locsStale = false;
        }
    }
    // SSAO uniform locations (cached once)
    {
        static gX_I1 ssaoLocsCached = false;
        if ((!ssaoLocsCached || gfx->ssao.blurShader.locsStale
            || gfx->ssao.gtaoShader.locsStale
            || gfx->ssao.gtaoAccumShader.locsStale
            || gfx->ssao.ssgiShader.locsStale
            || gfx->ssao.ssgiBlurShader.locsStale
            || gfx->ssao.ssgiAccumShader.locsStale)
            && gfx->ssao.blurShader.loaded && gfx->ssao.gtaoShader.loaded
            && gfx->ssao.gtaoAccumShader.loaded && gfx->ssao.ssgiShader.loaded
            && gfx->ssao.ssgiBlurShader.loaded && gfx->ssao.ssgiAccumShader.loaded)
        {
            gX_Shader bs = gfx->ssao.blurShader.shader;
            gfx->ssao.blurMapLoc       = gX_Shader_GetLocation(bs, "ssaoMap");
            gfx->ssao.blurDepthMapLoc  = gX_Shader_GetLocation(bs, "depthMap");
            gfx->ssao.blurResolutionLoc= gX_Shader_GetLocation(bs, "resolution");
            gfx->ssao.blurEdgeThresholdLoc = gX_Shader_GetLocation(bs, "edgeThreshold");
            //opencode: v0.3-M4 — SSGI locations
            {
                gX_Shader gi = gfx->ssao.ssgiShader.shader;
                gfx->ssao.ssgiSceneColorLoc = gX_Shader_GetLocation(gi, "sceneColor");
                gfx->ssao.ssgiDepthLoc      = gX_Shader_GetLocation(gi, "depthMap");
                gfx->ssao.ssgiInvProjLoc    = gX_Shader_GetLocation(gi, "invProjection");
                gfx->ssao.ssgiProjLoc       = gX_Shader_GetLocation(gi, "projection");
                gfx->ssao.ssgiResLoc        = gX_Shader_GetLocation(gi, "resolution");
                gfx->ssao.ssgiRadiusLoc     = gX_Shader_GetLocation(gi, "radius");
                gfx->ssao.ssgiBiasLoc       = gX_Shader_GetLocation(gi, "giBias");
                gfx->ssao.ssgiThicknessLoc  = gX_Shader_GetLocation(gi, "thickness");
                gfx->ssao.ssgiIntensityLoc  = gX_Shader_GetLocation(gi, "intensity");
                gfx->ssao.ssgiPrevLoc       = gX_Shader_GetLocation(gi, "prevGI");
                gfx->ssao.ssgiBounceLoc     = gX_Shader_GetLocation(gi, "bounceGain");
                gfx->ssao.ssgiSeedLoc       = gX_Shader_GetLocation(gi, "oracleSeed");
                gfx->ssao.ssgiSeedActiveLoc = gX_Shader_GetLocation(gi, "oracleSeedActive");
            }
            //opencode: v0.3.4-fix - blur/accum program locations were never
            //   cached (fields existed but stayed zero) so both passes bound
            //   samplers through OTHER programs' location indices -> black GI
            {
                gX_Shader gb = gfx->ssao.ssgiBlurShader.shader;
                gfx->ssao.ssgiBlurMapLoc  = gX_Shader_GetLocation(gb, "ssgiMap");
                gfx->ssao.ssgiBlurDepthLoc= gX_Shader_GetLocation(gb, "depthMap");
                gfx->ssao.ssgiBlurResLoc  = gX_Shader_GetLocation(gb, "resolution");
                gX_Shader ga2 = gfx->ssao.ssgiAccumShader.shader;
                gfx->ssao.ssgiAccCurLoc    = gX_Shader_GetLocation(ga2, "currentMap");
                gfx->ssao.ssgiAccHistLoc   = gX_Shader_GetLocation(ga2, "historyMap");
                gfx->ssao.ssgiAccHistoryValidLoc = gX_Shader_GetLocation(ga2, "historyValid");
                gfx->ssao.ssgiAccVelLoc    = gX_Shader_GetLocation(ga2, "velocityMap");
                gfx->ssao.ssgiAccHasVelLoc = gX_Shader_GetLocation(ga2, "hasVelocity");
                gfx->ssao.ssgiAccResLoc    = gX_Shader_GetLocation(ga2, "resolution");
            }
            //opencode: v0.3-M2 — GTAO locations
            {
                gX_Shader gs = gfx->ssao.gtaoShader.shader;
                gfx->ssao.gtaoDepthMapLoc  = gX_Shader_GetLocation(gs, "depthMap");
                gfx->ssao.gtaoInvProjLoc   = gX_Shader_GetLocation(gs, "invProjection");
                gfx->ssao.gtaoProjLoc      = gX_Shader_GetLocation(gs, "projection");
                gfx->ssao.gtaoResolutionLoc= gX_Shader_GetLocation(gs, "resolution");
                gfx->ssao.gtaoRadiusLoc    = gX_Shader_GetLocation(gs, "radius");
                gfx->ssao.gtaoBiasLoc      = gX_Shader_GetLocation(gs, "aoBias");
                gfx->ssao.gtaoThicknessLoc = gX_Shader_GetLocation(gs, "thickness");
                gfx->ssao.gtaoStrengthLoc  = gX_Shader_GetLocation(gs, "aoStrength");
                gfx->ssao.gtaoSeedLoc       = gX_Shader_GetLocation(gs, "oracleSeed");
                gfx->ssao.gtaoSeedActiveLoc = gX_Shader_GetLocation(gs, "oracleSeedActive");
            }
            //opencode: cache GTAO temporal accumulation locations with the other SSAO programs
            {
                gX_Shader ga = gfx->ssao.gtaoAccumShader.shader;
                gfx->ssao.gtaoAccCurLoc    = gX_Shader_GetLocation(ga, "currentMap");
                gfx->ssao.gtaoAccHistLoc   = gX_Shader_GetLocation(ga, "historyMap");
                gfx->ssao.gtaoAccDepthMapLoc = gX_Shader_GetLocation(ga, "depthMap");
                gfx->ssao.gtaoAccHistoryValidLoc = gX_Shader_GetLocation(ga, "historyValid");
                gfx->ssao.gtaoAccVelLoc    = gX_Shader_GetLocation(ga, "velocityMap");
                gfx->ssao.gtaoAccHasVelLoc = gX_Shader_GetLocation(ga, "hasVelocity");
                gfx->ssao.gtaoAccCameraMovedLoc = gX_Shader_GetLocation(ga, "cameraMoving");
                gfx->ssao.gtaoAccResLoc    = gX_Shader_GetLocation(ga, "resolution");
            }
            ssaoLocsCached = true;
            gfx->ssao.blurShader.locsStale = false;
            gfx->ssao.gtaoShader.locsStale = false;
            gfx->ssao.gtaoAccumShader.locsStale = false;
            gfx->ssao.ssgiShader.locsStale = false;
            gfx->ssao.ssgiBlurShader.locsStale = false;
            gfx->ssao.ssgiAccumShader.locsStale = false;
            gfx->ssao.ssgiBlurNearLoc = -1;
            gfx->ssao.ssgiBlurFarLoc = -1;
        }
    }

    //opencode: heap iteration for models
    {
        gX_F64 modelDeltaTime = gX_FrameTime_Get();
        GX_HEAP_FOREACH(gfx->models, gX_RendererGFX_Model, model) {
            gX_RendererGFX_Model_Orbit_Update(model, modelDeltaTime);
            gX_RendererGFX_Animation_Update(model, modelDeltaTime);
            gX_RendererGFX_Model_Update(model, gfx);
        }
    }

    //opencode: heap iteration for textures
    {
        GX_HEAP_FOREACH(gfx->textures, gX_RendererGFX_Texture, tex) {
            //ai: newly loaded ordinary heap textures inherit the current policy without a settings toggle.
            if (!tex->wrapper.isSkybox && (!tex->wrapper.loadingState.loaded
                || tex->wrapper.filter != gfx->state.textureFilter))
            {
                tex->wrapper.filter = gfx->state.textureFilter;
                tex->wrapper.filterChanged = true;
            }
            gX_RendererGFX_Texture_Update(tex);
        }
    }

    gX_RendererGFX_RenderTexture *map0u = gX_Heap_SeekPtr_Index(&gfx->maps, 0);
    if (map0u) gX_RendererGFX_RenderTexture_Update(map0u);

    //opencode: decal lifetime update
    gX_RendererGFX_Decal_UpdateAll(gfx);

    //ai: update water dynamics without resetting the editor/preset's authored plane height.
    if (gfx->state.ENABLE_WATER) {
        gX_RendererWater_Update(&gfx->water);
    }

}
