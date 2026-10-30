/**********************************************************************************************
*
*   [gX: src/renderer/gfx/showcase.c]
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


#include "showcase.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../main.h"
#include "../../input.h"
#include "../../camera.h"
#include "../renderer.h"
#include "../../wrappers/wrappers.h"
#include "load.h"
#include "../../rhi/rhi_state.h"
//opencode: hunt - implicit-decl sweep
#include "../../gxmath/gxmath.h"

//opencode: interactive GFX demo (--showcase). Orbit camera (LMB drag + wheel),
//   effect hotkeys, model focus (1-9), quality cycle (Q), HUD (H).
//   All state is scene-local statics; the gfx renderer is untouched.

static gX_I1  s_defaultsApplied = false;
static gX_F32 s_yaw = 45.0f;
static gX_F32 s_pitch = 25.0f;
static gX_F32 s_dist = 9.0f;
static gX_F32v3 s_target = {0, 0, 0};
static gX_I32 s_focus = -1;
static gX_I1  s_hudVisible = true;
static gX_I1  s_flyMode = false;
static gX_I32 s_skyMode = 1; // 0=off 1=procedural 2=cubemap
//opencode: raw-RT debug view — D cycles: 0=off 1=ssao 2=ssaoBlur 3=depth 4=ssr
static gX_I32 s_debugView = 0;

static gX_F32v3 gX_Showcase_V3(gX_F32 x, gX_F32 y, gX_F32 z) {
    gX_F32v3 v; v.x = x; v.y = y; v.z = z; return v;
}

static gX_F32 gX_Showcase_V3_Dist(gX_F32v3 a, gX_F32v3 b) {
    gX_F32 dx = a.x-b.x, dy = a.y-b.y, dz = a.z-b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static gX_F32v3 gX_Showcase_SceneCenter(gX_RendererGFX *gfx) {
    gX_F32v3 c = {0,0,0};
    gX_I32 n = 0;
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model*)gfx->models.data;
    for (gX_U32 i = 0; i < gfx->models.amount; i++) {
        if (gfx->models.meta[i].USED && models[i].aabbValid) {
            c.x += (models[i].aabbMin[0] + models[i].aabbMax[0]) * 0.5f;
            c.y += (models[i].aabbMin[1] + models[i].aabbMax[1]) * 0.5f;
            c.z += (models[i].aabbMin[2] + models[i].aabbMax[2]) * 0.5f;
            n++;
        }
    }
    if (n > 0) { c.x /= (gX_F32)n; c.y /= (gX_F32)n; c.z /= (gX_F32)n; }
    return c;
}

static gX_I0 gX_Showcase_FocusModel(gX_RendererGFX *gfx, gX_I32 idx) {
    gX_RendererGFX_Model *models = (gX_RendererGFX_Model*)gfx->models.data;
    if (idx < 0 || (gX_U32)idx >= gfx->models.amount) return;
    if (!gfx->models.meta[idx].USED || !models[idx].aabbValid) return;
    s_focus = idx;
    s_target = gX_Showcase_V3(
        (models[idx].aabbMin[0] + models[idx].aabbMax[0]) * 0.5f,
        (models[idx].aabbMin[1] + models[idx].aabbMax[1]) * 0.5f,
        (models[idx].aabbMin[2] + models[idx].aabbMax[2]) * 0.5f);
}

gX_I0 gX_Showcase_Update(gX_RendererGFX *gfx) {
    if (!arguments.showcase) return;

    if (!s_defaultsApplied) {
        s_defaultsApplied = true;
        //opencode: Ultra preset (16x AF, HIGH shader, 4x MSAA, occlusion culling).
        //   Phase 1 — safe mode takes preset 0 (true all-off: no MSAA, LOW
        //   shader, no occlusion culling) instead of Ultra; a single cascade
        //   (was: Ultra applied, then re-applied below, then re-cascaded by
        //   the update loop — the loop's re-cascade silently re-enabled the
        //   MSAA that safe mode had forced off).
        gfx->state.qualityPreset = arguments.gfxSafeMode ? 0 : (arguments.hasPreset ? arguments.preset : 3); //opencode: --preset overrides
        gX_RendererGFX_ApplyQualityPreset(gfx);
        //opencode: --gfx-safe-mode overrides — keep all effects off (the
        //   showcase defaults would otherwise re-enable what safe mode disabled)
        if (!arguments.gfxSafeMode) {
            gfx->state.ENABLE_BLOOM = 1;
            gfx->state.ENABLE_SSAO = 1;
            gfx->state.ENABLE_SSR = 1; //opencode: SSR shipped v0.1.7 — showcase default on (R toggles)
            gfx->state.ENABLE_REFLECTIONS = 1;
            gfx->state.ENABLE_PROCEDURAL_SKY = 1;
            gfx->state.ENABLE_POINT_SHADOWS = 1;
            gfx->state.ENABLE_SHADOWS = 1; //opencode: v0.1.21 — re-enabled (v0.1.19 flicker fixed by every-frame cubemap updates)
            gfx->state.ENABLE_PARTICLES = 1;
            //opencode: v0.3-M3.1 — OFF until the black-frame blocker is
            //   solved (see architecture.md M4 section); schema opt-in works
            //opencode: v0.3.1 — fog re-enabled; array-sampler removal fixed
            //   the black frame (cascade-0-only occlusion by design)
            gfx->state.ENABLE_VOLUME_FOG = 1;
            gfx->state.ENABLE_COOKIES = 1; //opencode: demo spot light uses a projected cookie
            gfx->state.ENABLE_MOTION_BLUR = 0;
            gfx->state.ENABLE_DOF = 0;
            //opencode: --showcase-y2k visual direction study — clean realtime
            //   raster look with authored neon, glass, metal, and twilight air.
            if (arguments.showcaseY2K) {
                gfx->state.ENABLE_PARTICLES = 0;
                gfx->state.ENABLE_COOKIES = 0;
                gfx->state.ENABLE_LENS_FLARE = 0;
                gfx->state.ENABLE_RIM = 1;
                gfx->state.rimIntensity = 0.35f;
                gfx->state.rimPower = 2.5f;
                gfx->state.rimColor = (gX_Color){80, 220, 255, 255};
                gfx->state.ENABLE_ENV_AMBIENT = 1;
                gfx->state.envAmbientFactor = 0.18f;
                gfx->state.exposure = 1.0f;
                gfx->bloom.threshold = 0.65f;
                gfx->bloom.intensity = 2.2f;
                gfx->bloom.vignette = 0.12f;
                gfx->bloom.saturation = 1.12f;
                gfx->bloom.contrast = 1.08f;
                gfx->fogVolume.density = 0.012f;
                gfx->fogVolume.heightFalloff = 1.4f;
                gfx->fogVolume.scattering = 0.45f;
            }
            //opencode: --showcase-water — float the demo cluster above the water
            //   surface (the scene content is raised +2 in the demo scene setup)
            if (arguments.showcaseWater) {
                gfx->state.ENABLE_WATER = 1;
                gfx->state.ENABLE_SSR = 1;
                //opencode: Water layout — farther + higher camera so the plane
                //   (y=0, 40x40) fills the lower view below the raised cluster.
                //   Moderate pitch: keeps the horizon + sky band in frame
                //   above the water. (The old steep-pitch workaround existed
                //   to avoid the platform slab's gray underside — the slab no
                //   longer lifts with the cluster.)
                s_pitch = 28.0f;
                s_dist = 13.0f;
            }
        }
        s_target = gX_Showcase_SceneCenter(gfx);
        //opencode: --debug-view <0-12> — preset the raw-RT debug view at startup
        if (arguments.hasDebugView) {
            s_debugView = (gX_I32)arguments.debugView % 13;
            s_hudVisible = true;
        }
        //opencode: --ssr-debug <0-27> — preset the SSR shader debug mode
        //   (0=normal 1=normals 2=reflectDir 3=depth 4=hitQuality 5=stateMachine
        //    6=temporalRejection 7=confidence 8=normalFallback 9=normalRGB 10=bounceIndex)
        if (arguments.hasSsrDebug) {
            gfx->state.ssrDebugMode = arguments.ssrDebug;
            s_hudVisible = true;
        }
        gX_TraceLog(gX_LOG_NOTICE, "SHOWCASE: interactive demo — LMB drag orbit, wheel zoom, 1-9 focus, Q preset, H help");
    }

    //opencode: v0.1.24 — harness hook: GX_T_BLOOM forces the bloom state so
    //   scripts can separate bloom glow from reflection-content artifacts
    if (getenv("GX_T_BLOOM")) gfx->state.ENABLE_BLOOM = (gX_I1)atoi(getenv("GX_T_BLOOM"));
    //opencode: audit hook — the showcase HUD text re-renders with frame-timing
    //   sensitive glyph pixels; diff-based audits disable it for stable frames.
    if (getenv("GX_T_HUD")) s_hudVisible = (gX_I1)atoi(getenv("GX_T_HUD"));

    //opencode: v0.1.24 — harness hook: GX_T_BOUNCES overrides the extra
    //   SSR bounce count; 0 gives strict single-bounce reflections so
    //   verification scripts can demand EXACTLY one mirrored image
    if (getenv("GX_T_BOUNCES")) gfx->ssr.maxBounces = atoi(getenv("GX_T_BOUNCES"));
    //opencode: --ledrow night rig - deterministic frameCount-driven light
    //   animation (2 orbiters, hue-cycler, strobe) + LED emissive traveling
    //   wave. Static heap indices: ledrow owns lights 0-3, models 0-12 in
    //   layout order (slab, teapot, 3 balls, 8 LEDs). Count-guarded so a
    //   shifted heap skips instead of scrambling. Electives GX_T_LEDORBIT,
    //   GX_T_LEDBLINK, GX_T_LEDHUE (absent or !=0 = on).
    if (arguments.showcaseLedRow && gfx->lights.amount >= 4 && gfx->models.amount >= 13) {
        gX_F32 lt = (gX_F32)maindata.frameCount;
        gX_I1 ledOrbit = !getenv("GX_T_LEDORBIT") || atoi(getenv("GX_T_LEDORBIT")) != 0;
        gX_I1 ledBlink = !getenv("GX_T_LEDBLINK") || atoi(getenv("GX_T_LEDBLINK")) != 0;
        gX_I1 ledHue = !getenv("GX_T_LEDHUE") || atoi(getenv("GX_T_LEDHUE")) != 0;
        gX_RendererGFX_Light *ll = (gX_RendererGFX_Light*)gfx->lights.data;
        gX_RendererGFX_Model *mm = (gX_RendererGFX_Model*)gfx->models.data;
        if (ledOrbit) {
            gX_F32 a0 = lt * 0.025f;
            ll[0].pos = (gX_F32v3){cosf(a0) * 2.6f, 1.8f, sinf(a0) * 2.6f};
            gX_F32 a1 = -lt * 0.018f + 3.14159265f;
            ll[1].pos = (gX_F32v3){cosf(a1) * 3.2f, 1.2f, sinf(a1) * 3.2f};
        }
        if (ledHue) {
            gX_Color hc = gX_Color_FromHSV(fmodf(lt * 1.5f, 360.0f), 1.0f, 1.0f);
            ll[2].color = hc;
            ll[2].diffuseTint = hc;
            ll[2].specularTint = hc;
        }
        ll[3].brightness = (!ledBlink || ((maindata.frameCount / 30) % 2 == 0)) ? 8.0f : 0.0f;
        for (gX_I32 li = 0; li < 8; li++) {
            mm[5 + li].params.emissiveFactor = 3.2f + 1.0f * sinf(lt * 0.08f + (gX_F32)li * 0.785f);
        }
    }
    //opencode: SSR lab - expose the march sliders to deterministic captures.
    if (getenv("GX_T_STEP")) gfx->ssr.stepSize = (gX_F32)atof(getenv("GX_T_STEP"));
    if (getenv("GX_T_MAXSTEPS")) gfx->ssr.maxSteps = atoi(getenv("GX_T_MAXSTEPS"));
    if (getenv("GX_T_MAXDIST")) gfx->ssr.maxDistance = (gX_F32)atof(getenv("GX_T_MAXDIST"));
    if (getenv("GX_T_MAXLOD")) gfx->ssr.maxLod = (gX_F32)atof(getenv("GX_T_MAXLOD"));
    if (getenv("GX_T_BIAS")) gfx->ssr.depthBias = (gX_F32)atof(getenv("GX_T_BIAS"));
    if (getenv("GX_T_OPACITY")) gfx->ssr.opacity = (gX_F32)atof(getenv("GX_T_OPACITY"));
    //opencode: SSR acceptance A/B hooks isolate false rejects without changing defaults.
    if (getenv("GX_T_MAXDEPTH")) gfx->ssr.maxDepthDelta = (gX_F32)atof(getenv("GX_T_MAXDEPTH"));
    if (getenv("GX_T_WORLDGAP")) gfx->ssr.worldGapThreshold = (gX_F32)atof(getenv("GX_T_WORLDGAP"));
    if (getenv("GX_T_FINALDEPTH")) gfx->ssr.finalDepthThreshold = (gX_F32)atof(getenv("GX_T_FINALDEPTH"));
    if (getenv("GX_T_HITFACING")) gfx->ssr.hitFacingThreshold = (gX_F32)atof(getenv("GX_T_HITFACING"));
    //opencode: edge-band audit hook: GX_T_EDGEFADE overrides the screen-edge
    //   dissolve width (default 0.08); 0 disables it to expose edge pops.
    if (getenv("GX_T_EDGEFADE")) gfx->ssr.edgeFade = (gX_F32)atof(getenv("GX_T_EDGEFADE"));

    //opencode: v0.1.24 — harness hook: GX_T_SSR=0 forces SSR off while
    //   keeping every other effect identical, giving scripts a clean
    //   no-screen-reflection reference frame of the same scene
    if (getenv("GX_T_SSR")) gfx->state.ENABLE_SSR = (gX_I1)atoi(getenv("GX_T_SSR"));
    //opencode: debug-vis - GX_T_DEPTHVIS engages the linearized depth
    //   visualizer pass headless (0=off 1=sun/CSM 2=scene 3=point). Raw
    //   depth RTs read ~white, so the linearizer is the only honest view
    //   (same reason magic error colors live pre-tonemap, never on frame).
    if (getenv("GX_T_DEPTHVIS")) {
        gX_I32 v = (gX_I32)atoi(getenv("GX_T_DEPTHVIS"));
        gfx->state.SHOW_DEPTH_VISUALIZER = (v > 0) ? 1 : 0;
        if (v > 0) gfx->state.depthVisSource = (v > 3) ? 2 : v - 1;
    }
    //opencode: oracle - GX_T_AUXFLIP=N emulates the editor "planar fallback"
    //   checkbox: flips ENABLE_SSR_AUXVIEW once when frameCount hits N so a
    //   headless capture can isolate toggle-transient artifacts
    {
        const gX_I8 *af = getenv("GX_T_AUXFLIP");
        if (af && af[0] && maindata.frameCount == (gX_U64)atoi(af)) {
            gfx->state.ENABLE_SSR_AUXVIEW = !gfx->state.ENABLE_SSR_AUXVIEW;
            gX_TraceLog(gX_LOG_NOTICE, "AUXFLIP: f=%llu aux=%d",
                (unsigned long long)maindata.frameCount,
                (int)gfx->state.ENABLE_SSR_AUXVIEW);
        }
    }

    //opencode: TAA on by default in showcase (validated 2026-09-16:
    //   same brightness, hf -30%, edges -36%, fps parity, motion crisp
    //   after the E7 velocity fix). Env hook below still overrides, as
    //   does the T key at runtime.
    gfx->state.ENABLE_TAA = true;
    //opencode: v0.1.38 — TAA harness hook: deterministic on/off for captures
    if (getenv("GX_T_TAA")) gfx->state.ENABLE_TAA = (gX_I1)atoi(getenv("GX_T_TAA"));

    //opencode: v0.3.4 — SSR auxview A/B hook
    if (getenv("GX_T_SSRAUX")) gfx->state.ENABLE_SSR_AUXVIEW = (gX_I1)atoi(getenv("GX_T_SSRAUX"));

    //opencode: v0.3.8 — cubemap/IBL force hook (SSR tunnel diagnosis)
    if (getenv("GX_T_REFL")) gfx->state.ENABLE_REFLECTIONS = (gX_I1)atoi(getenv("GX_T_REFL"));

    //opencode: v0.3.2-audit — fog/godray force hooks (audit capture matrix)
    if (getenv("GX_T_FOG")) gfx->state.ENABLE_VOLUME_FOG = (gX_I1)atoi(getenv("GX_T_FOG"));
    { const char* fd = getenv("GX_FOG_DENSITY"); if (fd) gfx->fogVolume.density = (gX_F32)atof(fd); }
    { const char* fs = getenv("GX_FOG_SCATTER"); if (fs) gfx->fogVolume.scattering = (gX_F32)atof(fs); }
    //opencode: item-13 - point/spot-light inscatter kill switch (default on).
    if (getenv("GX_T_FOGPOINTS")) gfx->fogVolume.enablePointLights = (gX_I1)atoi(getenv("GX_T_FOGPOINTS"));
    //opencode: deterministic sun-radiance overrides for fog color/intensity probes.
    { const char* sb = getenv("GX_T_SUNBRIGHTNESS"); if (sb) gfx->sun.brightness = (gX_F32)atof(sb); }
    {
        const char* sc = getenv("GX_T_SUNCOLOR");
        gX_I32 r, g, b;
        if (sc && sscanf(sc, "%d,%d,%d", &r, &g, &b) == 3)
            gfx->sun.color = (gX_Color){(gX_U8)r, (gX_U8)g, (gX_U8)b, 255};
    }
    if (getenv("GX_T_GODRAY")) gfx->state.ENABLE_GODRAYS = (gX_I1)atoi(getenv("GX_T_GODRAY"));
    //opencode: deterministic godray parameter overrides for audit A/Bs.
    if (getenv("GX_T_GODRAYDENSITY")) gfx->godray.density = atoi(getenv("GX_T_GODRAYDENSITY"));
    if (getenv("GX_T_GODRAYDECAY")) gfx->godray.decay = (gX_F32)atof(getenv("GX_T_GODRAYDECAY"));
    if (getenv("GX_T_GODRAYWEIGHT")) gfx->godray.weight = (gX_F32)atof(getenv("GX_T_GODRAYWEIGHT"));
    if (getenv("GX_T_GODRAYEXPOSURE")) gfx->godray.exposure = (gX_F32)atof(getenv("GX_T_GODRAYEXPOSURE"));
    //opencode: hunt - vignette force hook (echo amplitude forensics)
    if (getenv("GX_T_VIGNETTE")) gfx->bloom.vignette = (gX_F32)atof(getenv("GX_T_VIGNETTE"));
    //opencode: silent-hill band sourcing — sky kill switch for A/B
    //   attribution (procedural sky vs fog inscatter vs clear color).
    if (getenv("GX_T_SKY")) {
        gX_I1 skyOn = (gX_I1)atoi(getenv("GX_T_SKY"));
        if (!skyOn) {
            gfx->state.ENABLE_PROCEDURAL_SKY = 0;
            gfx->state.ENABLE_SKYBOX = 0;
        }
    }
    //opencode: sky atmosphere density override (high-airmass horizon
    //   reddening scales with it — audit probe for mood grades).
    {
        const gX_I8 *sd = getenv("GX_SKY_DENSITY");
        if (sd) gfx->sky.density = (gX_F32)atof(sd);
    }
    //opencode: silent-hill sky-off must live here, not scene setup:
    //   the defaults block above re-enables the procedural sky on the
    //   first update frame, silently resurrecting it (found via the
    //   chocolate-mauve daylight capture). Per-frame enforce like GX_T_SKY.
    {
        const gX_I8 *sh = getenv("GX_SILENTHILL");
        if (sh && sh[0] == '1' && arguments.showcaseForest) {
            gfx->state.ENABLE_PROCEDURAL_SKY = 0;
            gfx->state.ENABLE_SKYBOX = 0;
        }
    }
    //opencode: bloom intensity hook - isolates the glow term from the
    //   tonemap/vignette chain. Bloom-OFF returns after a raw blit (no
    //   ACES/gamma/vignette), so on/off A/B measures the whole composite,
    //   not the glow; intensity-0-vs-1.2 with the chain engaged isolates it.
    if (getenv("GX_T_BLOOMINT")) gfx->bloom.intensity = (gX_F32)atof(getenv("GX_T_BLOOMINT"));
    //opencode: halo probe — bloom extract threshold override (default 0.85).
    //   Lower values fatten halos around small brights (LED orbs); the
    //   audit question is how far it goes before the frame washes.
    if (getenv("GX_T_BLOOMTHRESH")) gfx->bloom.threshold = (gX_F32)atof(getenv("GX_T_BLOOMTHRESH"));
    //opencode: bloom stage isolation for scripts/bloom_audit.py — each hook
    //   overrides exactly one pipeline parameter (extract / blur / composite).
    //   GX_T_BLOOMEXPOSURE needs the auto-exposure update off (GX_T_BLOOMAE=0
    //   or GX_NO_AE=1) or the adapted exposure overwrites it every 4th frame.
    if (getenv("GX_T_BLOOMKNEE")) gfx->bloom.knee = (gX_F32)atof(getenv("GX_T_BLOOMKNEE"));
    if (getenv("GX_T_BLOOMBLUR")) gfx->bloom.blurAmount = (gX_F32)atof(getenv("GX_T_BLOOMBLUR"));
    if (getenv("GX_T_BLOOMEXPOSURE")) gfx->bloom.exposure = (gX_F32)atof(getenv("GX_T_BLOOMEXPOSURE"));
    if (getenv("GX_T_BLOOMSAT")) gfx->bloom.saturation = (gX_F32)atof(getenv("GX_T_BLOOMSAT"));
    if (getenv("GX_T_BLOOMCONTRAST")) gfx->bloom.contrast = (gX_F32)atof(getenv("GX_T_BLOOMCONTRAST"));
    if (getenv("GX_T_BLOOMAE")) gfx->bloom.autoExposure = (gX_I1)atoi(getenv("GX_T_BLOOMAE"));
    //opencode: hunt - feature-matrix hooks (one-line forces for A/B verify)
    if (getenv("GX_T_SSAO")) gfx->state.ENABLE_SSAO = (gX_I1)atoi(getenv("GX_T_SSAO"));
    if (getenv("GX_T_SHADOWS")) gfx->state.ENABLE_SHADOWS = (gX_I1)atoi(getenv("GX_T_SHADOWS"));
    if (getenv("GX_T_POINT_SHADOWS")) gfx->state.ENABLE_POINT_SHADOWS = (gX_I1)atoi(getenv("GX_T_POINT_SHADOWS"));
    if (getenv("GX_T_INSTANCING")) gfx->state.ENABLE_INSTANCING = (gX_I1)atoi(getenv("GX_T_INSTANCING"));
    //opencode: matrix-only source visibility hook; production marker defaults stay off.
    if (getenv("GX_T_LIGHT_MARKERS")) gfx->state.SHOW_LIGHT_MARKERS = (gX_I1)atoi(getenv("GX_T_LIGHT_MARKERS"));
    //opencode: low-GL capability fixtures. The policy gates are applied by
    //   the GFX paths; hooks request one feature at a time for visual A/Bs.
    if (getenv("GX_T_GEOMETRY")) gfx->state.ENABLE_GEO_DEMO = (gX_I1)atoi(getenv("GX_T_GEOMETRY"));
    if (getenv("GX_T_MSAA")) gfx->state.msaaLevel = (gX_I32)atoi(getenv("GX_T_MSAA"));
    if (getenv("GX_T_DOF")) gfx->state.ENABLE_DOF = (gX_I1)atoi(getenv("GX_T_DOF"));
    if (getenv("GX_T_MOTIONBLUR")) gfx->state.ENABLE_MOTION_BLUR = (gX_I1)atoi(getenv("GX_T_MOTIONBLUR"));
    if (getenv("GX_T_FXAA")) gfx->state.ENABLE_FXAA = (gX_I1)atoi(getenv("GX_T_FXAA"));
    if (getenv("GX_T_COOKIES")) gfx->state.ENABLE_COOKIES = (gX_I1)atoi(getenv("GX_T_COOKIES"));
    if (getenv("GX_T_PARTICLES")) gfx->state.ENABLE_PARTICLES = (gX_I1)atoi(getenv("GX_T_PARTICLES"));
    if (getenv("GX_T_SNOWFALL")) gfx->state.ENABLE_SNOWFALL = (gX_I1)atoi(getenv("GX_T_SNOWFALL"));
    if (getenv("GX_T_RAIN")) gfx->state.ENABLE_RAIN = (gX_I1)atoi(getenv("GX_T_RAIN"));
    if (getenv("GX_T_FOGSPRITE")) gfx->state.ENABLE_FOGSPRITE = (gX_I1)atoi(getenv("GX_T_FOGSPRITE"));
    if (getenv("GX_T_SPLAT")) gfx->state.ENABLE_SPLAT = (gX_I1)atoi(getenv("GX_T_SPLAT"));
    if (getenv("GX_T_LENSFLARE")) gfx->state.ENABLE_LENS_FLARE = (gX_I1)atoi(getenv("GX_T_LENSFLARE"));
    if (getenv("GX_T_DECALS")) gfx->state.ENABLE_DECALS = (gX_I1)atoi(getenv("GX_T_DECALS"));
    if (getenv("GX_T_SNOW") && atoi(getenv("GX_T_SNOW")) > 0) gfx->state.ENABLE_DECALS = 1;
    if (getenv("GX_T_WIREFRAME")) gfx->state.ENABLE_WIREFRAME = (gX_I1)atoi(getenv("GX_T_WIREFRAME"));
    if (getenv("GX_T_ENVAMBIENT")) gfx->state.ENABLE_ENV_AMBIENT = (gX_I1)atoi(getenv("GX_T_ENVAMBIENT"));
    if (getenv("GX_T_FRUSTUM")) gfx->state.ENABLE_FRUSTUM_CULLING = (gX_I1)atoi(getenv("GX_T_FRUSTUM"));
    //opencode: CSM split-seam probe controls; production defaults remain unchanged.
    if (getenv("GX_T_CSMBLEND")) gfx->csm.blendScale = (gX_F32)atof(getenv("GX_T_CSMBLEND"));
    //opencode: hunt - DOF focal provocation (static scenes sit inside the
    //   default 5+-2 focal slab, so the toggle alone is legitimately ~zero)
    if (getenv("GX_T_DOFFOCAL")) gfx->dof.focalDistance = (gX_F32)atof(getenv("GX_T_DOFFOCAL"));
    if (getenv("GX_T_DOFRANGE")) gfx->dof.focalRange = (gX_F32)atof(getenv("GX_T_DOFRANGE"));

    //opencode: v0.1.39 — deterministic camera spin (deg/frame): exercises
    //   TAA reprojection under REAL camera motion in headless captures
    //   (the historical failure mode of the pre-velocity TAA).
    {
        const gX_I8 *spin = getenv("GX_CAM_SPIN");
        if (spin) s_yaw += (gX_F32)atof(spin);
    }

    //opencode: v0.1.24 — deterministic orbit override for the reflection
    //   verification harness (scripts/probe_reflect_verify.py). Headless runs
    //   cannot drag the mouse; these pin the orbit state so captures are
    //   reproducible and the analytic checks can reconstruct the projection.
    {
        const gX_I8 *envYaw = getenv("GX_CAM_YAW");
        const gX_I8 *envPitch = getenv("GX_CAM_PITCH");
        const gX_I8 *envDist = getenv("GX_CAM_DIST");
        static gX_I1 s_camOverride = false;
        if (!s_camOverride && !s_flyMode) {
            s_camOverride = true;
            //opencode: v0.3.4 --reflectrow testbed pins a deterministic view
            //   down the row diagonal (no orbit drag in headless captures);
            //   GX_CAM_* env hooks still override afterwards
            extern gX_MainArguments arguments;
            if (arguments.showcaseReflectRow) {
                s_yaw = -35.0f; s_pitch = 16.0f; s_dist = 11.0f;
                s_target = (gX_F32v3){0.0f, 0.35f, 0.0f};
            }
            //opencode: --ledrow testbed pins a front-left three-quarter view:
            //   LED ring + sweep balls + teapot all in frame above the slab.
            if (arguments.showcaseLedRow) {
                s_yaw = -25.0f; s_pitch = 15.0f; s_dist = 11.5f;
                s_target = (gX_F32v3){0.0f, 0.6f, 0.0f};
            }
            if (arguments.showcaseY2K) {
                s_yaw = -32.0f; s_pitch = 19.0f; s_dist = 14.5f;
                s_target = (gX_F32v3){0.0f, 0.95f, -0.25f};
            }
            //opencode: --forest testbed pins a low under-canopy view:
            //   clearing foreground, trunk layer mid, fog wall behind.
            //   (Canopies span +-6u: the camera must sit in a wide pocket,
            //   not just a trunk clearing, or the frame fills with bark.)
            if (arguments.showcaseForest) {
                s_yaw = 20.0f; s_pitch = 6.0f; s_dist = 20.0f;
                s_target = (gX_F32v3){0.0f, 1.5f, 0.0f};
            }
            //opencode: --lake testbed pins an elevated 3/4 view north:
            //   looking down steepens the water mirror (dark zenith rows,
            //   not the hot horizon band) and compresses the far seam into
            //   the fog. Low grazing angles chopped the horizon-band mirror
            //   into a full-width speckle band. GX_CAM_* override.
            if (arguments.showcaseLake) {
                s_yaw = 12.0f; s_pitch = 13.0f; s_dist = 22.0f;
                s_target = (gX_F32v3){0.0f, 0.8f, -4.0f};
            }
            if (envYaw) { s_yaw = (gX_F32)atof(envYaw); }
            if (envPitch) { s_pitch = (gX_F32)atof(envPitch); }
            if (envDist) { s_dist = (gX_F32)atof(envDist); }
        }
    }

    //opencode: orbit camera — LMB drag rotates, wheel zooms. C toggles free-fly
    //   (WASD + mouse-look via the camera system's FREE mode) vs orbit.
    //opencode: run header - one-shot state dump (frame 0, after hooks+pins):
    //   answers "why is X off" in the first 60 log lines. Env dump lives in
    //   gX_Load; scene switches log in the dispatcher.
    {
        static gX_I1 s_stateDumped = 0;
        if (!s_stateDumped) {
            s_stateDumped = 1;
            gX_TraceLog(gX_LOG_NOTICE,
                "STATE: ssr=%d bloom=%d fog=%d shadows=%d taa=%d refl=%d aux=%d occl=%d frust=%d depthvis=%d contact=%d view=%d expo=%.2f",
                (gX_I32)gfx->state.ENABLE_SSR, (gX_I32)gfx->state.ENABLE_BLOOM,
                (gX_I32)gfx->state.ENABLE_VOLUME_FOG, (gX_I32)gfx->state.ENABLE_SHADOWS,
                (gX_I32)gfx->state.ENABLE_TAA, (gX_I32)gfx->state.ENABLE_REFLECTIONS,
                (gX_I32)gfx->state.ENABLE_SSR_AUXVIEW, (gX_I32)gfx->state.ENABLE_OCCLUSION_CULLING,
                (gX_I32)gfx->state.ENABLE_FRUSTUM_CULLING, (gX_I32)gfx->state.SHOW_DEPTH_VISUALIZER,
                (gX_I32)gfx->contact.map.wrapper.loadingState.loaded, (gX_I32)s_debugView,
                gfx->state.exposure);
        }
    }
    //opencode: hermetic headless captures - a stray real-mouse event (wheel
    //   scroll over the window, click/drag) mid-run moves the orbit camera
    //   and voids A/B determinism (--ai-screenshot* runs open a real
    //   window). Camera-affecting input is ignored while capturing.
    gX_I1 aiCapture = (arguments.aiScreenshotFinal || arguments.hasAiScreenshotAfter);
    if (!aiCapture && input.keyboard.keys[gX_KEY_C].state.PRESSED) s_flyMode = !s_flyMode;

    if (s_flyMode) {
        //opencode: fly mode — let the camera system's FREE mode drive the camera
        //   (WASD move + mouse look, cursor hidden). The orbit state stays synced
        //   so toggling back resumes from the current viewpoint.
        s_yaw = atan2f(camera.camera3D.position.x - camera.camera3D.target.x,
                       camera.camera3D.position.z - camera.camera3D.target.z) * gX_RAD2DEG;
        s_pitch = asinf((camera.camera3D.position.y - camera.camera3D.target.y) / s_dist) * gX_RAD2DEG;
        s_dist = gX_Showcase_V3_Dist(camera.camera3D.position, camera.camera3D.target);
    } else if (!aiCapture && input.mouse.leftButton.DOWN) {
        s_yaw -= input.mouse.coordDelta.x * 0.2f;
        s_pitch += input.mouse.coordDelta.y * 0.2f;
        if (s_pitch > 89.0f) s_pitch = 89.0f;
        if (s_pitch < -89.0f) s_pitch = -89.0f;
    }
    if (!aiCapture && input.mouse.scroll != 0.0f && !s_flyMode) {
        s_dist *= (input.mouse.scroll > 0.0f) ? 0.9f : 1.1f;
        if (s_dist < 0.5f) s_dist = 0.5f;
        if (s_dist > 300.0f) s_dist = 300.0f;
    }
    if (!s_flyMode) {
        gX_F32 radY = s_yaw * gX_DEG2RAD;
        gX_F32 radP = s_pitch * gX_DEG2RAD;
        camera.camera3D.position = gX_Showcase_V3(
            s_target.x + s_dist * cosf(radP) * sinf(radY),
            s_target.y + s_dist * sinf(radP),
            s_target.z + s_dist * cosf(radP) * cosf(radY));
        camera.camera3D.target = s_target;
        camera.camera3D.up = gX_Showcase_V3(0.0f, 1.0f, 0.0f);
        //opencode: matrix-only wide-FOV override frames the directional sun
        //   together with the stage; unset runs retain their existing FOV.
        {
            const gX_I8 *envFov = getenv("GX_CAM_FOV");
            if (envFov) {
                gX_F32 fov = (gX_F32)atof(envFov);
                if (fov >= 10.0f && fov <= 140.0f)
                    camera.camera3D.fovy = fov;
            }
        }
        //opencode: v0.1.35 — one-shot runtime camera dump (oracle calibration)
        {
            static int camDumped = 0;
            if (camDumped < 1 && maindata.frameCount > 30) { camDumped++;
                gX_TraceLog(gX_LOG_NOTICE,
                    "CAMDUMP pos=(%.4f,%.4f,%.4f) tgt=(%.4f,%.4f,%.4f) up=(%.1f,%.1f,%.1f) fovy=%.2f dist=%.3f",
                    camera.camera3D.position.x, camera.camera3D.position.y, camera.camera3D.position.z,
                    camera.camera3D.target.x, camera.camera3D.target.y, camera.camera3D.target.z,
                    camera.camera3D.up.x, camera.camera3D.up.y, camera.camera3D.up.z,
                    camera.camera3D.fovy, s_dist);
            }
        }
    }

    //opencode: effect/camera hotkeys are ignored during screenshot captures;
    //   real keyboard state is as nondeterministic as the real mouse input
    //   already filtered above and can switch raw views or quality mid-run.
    if (!aiCapture) {
        //opencode: effect hotkeys (pressed edges)
        if (input.keyboard.keys[gX_KEY_B].state.PRESSED) gfx->state.ENABLE_BLOOM = !gfx->state.ENABLE_BLOOM;
        if (input.keyboard.keys[gX_KEY_S].state.PRESSED) gfx->state.ENABLE_SSAO = !gfx->state.ENABLE_SSAO;
        if (input.keyboard.keys[gX_KEY_R].state.PRESSED) gfx->state.ENABLE_SSR = !gfx->state.ENABLE_SSR;
        if (input.keyboard.keys[gX_KEY_T].state.PRESSED) gfx->state.ENABLE_TAA = !gfx->state.ENABLE_TAA;
        if (input.keyboard.keys[gX_KEY_M].state.PRESSED) gfx->state.ENABLE_MOTION_BLUR = !gfx->state.ENABLE_MOTION_BLUR;
        if (input.keyboard.keys[gX_KEY_F].state.PRESSED) gfx->state.ENABLE_FOG = !gfx->state.ENABLE_FOG;
        if (input.keyboard.keys[gX_KEY_G].state.PRESSED) gfx->state.ENABLE_GODRAYS = !gfx->state.ENABLE_GODRAYS;
        if (input.keyboard.keys[gX_KEY_P].state.PRESSED) gfx->state.ENABLE_PARTICLES = !gfx->state.ENABLE_PARTICLES;
        if (input.keyboard.keys[gX_KEY_Y].state.PRESSED) gfx->state.ENABLE_REFLECTIONS = !gfx->state.ENABLE_REFLECTIONS;
        if (input.keyboard.keys[gX_KEY_X].state.PRESSED) gfx->state.ENABLE_OCCLUSION_CULLING = !gfx->state.ENABLE_OCCLUSION_CULLING;
        //opencode: L toggles light-marker overlay (was schema-only, no runtime option).
        if (input.keyboard.keys[gX_KEY_L].state.PRESSED) gfx->state.SHOW_LIGHT_MARKERS = !gfx->state.SHOW_LIGHT_MARKERS;
        if (input.keyboard.keys[gX_KEY_H].state.PRESSED) s_hudVisible = !s_hudVisible;
        //opencode: debug-vis - D cycles all raw-RT views 0-12 (was 0-4;
        //   views 5+ were startup-only and view 10 labeled but unwired).
        if (input.keyboard.keys[gX_KEY_D].state.PRESSED) s_debugView = (s_debugView + 1) % 13;

        //opencode: Q cycles quality presets 0-3 (Ultra = occlusion culling on)
        if (input.keyboard.keys[gX_KEY_Q].state.PRESSED) {
            gfx->state.qualityPreset = (gfx->state.qualityPreset + 1) % 4;
            gX_RendererGFX_ApplyQualityPreset(gfx);
        }

        //opencode: K toggles CSM cascades 1 <-> 2
        if (input.keyboard.keys[gX_KEY_K].state.PRESSED) {
            gfx->csm.cascadeCount = (gfx->csm.cascadeCount > 1) ? 1 : 2;
        }

        //opencode: Z cycles sky mode off -> procedural -> cubemap
        if (input.keyboard.keys[gX_KEY_Z].state.PRESSED) {
            s_skyMode = (s_skyMode + 1) % 3;
            gfx->state.ENABLE_PROCEDURAL_SKY = (s_skyMode == 1) ? 1 : 0;
            gfx->state.ENABLE_SKYBOX = (s_skyMode == 2) ? 1 : 0;
        }

        //opencode: 1-9 focus models, 0 resets to scene center
        for (gX_I32 k = gX_KEY_ONE; k <= gX_KEY_NINE; k++) {
            if (input.keyboard.keys[k].state.PRESSED) {
                gX_Showcase_FocusModel(gfx, k - gX_KEY_ONE);
            }
        }
        if (input.keyboard.keys[gX_KEY_ZERO].state.PRESSED) {
            s_focus = -1;
            s_target = gX_Showcase_SceneCenter(gfx);
        }
    }
}

gX_I0 gX_Showcase_DrawHUD(gX_RendererGFX *gfx, gX_RenderTexture *overlayTgt) {
    if (!arguments.showcase || !s_hudVisible) return;

    gX_Drawing_BeginTexture(*overlayTgt);

    gX_Color white = {255, 255, 255, 255};
    gX_Color dim = {200, 200, 200, 255};

    //opencode: raw-RT debug view — draw the selected intermediate fullscreen.
    //   Drawn BEFORE Begin2D (whose screenW/2 translate would push a fullscreen
    //   quad off the canvas): projection is the canvas ortho, modelview identity.
    if (s_debugView > 0) {
        gX_Texture tex;
        switch (s_debugView) {
            case 1: tex = gfx->ssao.map.wrapper.rt.texture;    break;
            case 2: tex = gfx->ssao.blurMap.wrapper.rt.texture; break;
            case 3: tex = gfx->bloom.hdrScene.wrapper.rt.depth; break;
            case 4: tex = gfx->ssr.map.wrapper.rt.texture;      break;
            case 5: tex = gfx->bloom.hdrScene.wrapper.rt.texture; break;
            case 6: tex = gfx->sceneColorLDR.wrapper.rt.texture; break;
            case 7: if (gfx->bloom.scene.wrapper.loadingState.loaded) tex = gfx->bloom.scene.wrapper.rt.texture;
                    else tex = gfx->sceneColorLDR.wrapper.rt.texture;
                    break;
            case 8: tex = gfx->reflection.planar.wrapper.rt.texture; break;
            case 9: if (gfx->ssr.skyLut.wrapper.loadingState.loaded) tex = gfx->ssr.skyLut.wrapper.rt.texture;
                    else tex = gfx->sceneColorLDR.wrapper.rt.texture;
                    break;
            //opencode: debug-vis - view 10 wires the labeled-but-missing
            //   cookie atlas (was falling through to default/ssao). Raw GL
            //   id wrapped as gX_Texture (8x8 cells of 256px = 2048);
            //   rebuilt on demand so the view works with cookies off.
            case 10: if (gfx->common.cookieAtlasTex == 0) gX_RendererGFX_CookieAtlas_Rebuild(gfx);
                    if (gfx->common.cookieAtlasTex != 0) tex = (gX_Texture){gfx->common.cookieAtlasTex, 2048, 2048, 1, gX_RHI_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                    else tex = gfx->sceneColorLDR.wrapper.rt.texture;
                    break;
            //opencode: debug-vis - contact occlusion map (half-res, 1=lit).
            //   Pass is hook-gated (GX_T_CONTACT=1) so without it this view
            //   falls back to LDR, honestly showing the map is absent.
            case 11: if (gfx->contact.map.wrapper.loadingState.loaded) tex = gfx->contact.map.wrapper.rt.texture;
                    else tex = gfx->sceneColorLDR.wrapper.rt.texture;
                    break;
            //opencode: debug-vis - raw motion-vector RG (signed UV-space,
            //   ~0 = still). Tonemapper-unfriendly by nature: read edges,
            //   not shades.
            case 12: if (gfx->velocity.map.wrapper.loadingState.loaded) tex = gfx->velocity.map.wrapper.rt.texture;
                    else tex = gfx->sceneColorLDR.wrapper.rt.texture;
                    break;
            default: tex = gfx->ssao.map.wrapper.rt.texture;    break;
        }
        gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        gX_Texture_Draw(tex, (gX_Texture_Draw_Params){
            .dst = {0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight},
            //opencode: view 9 (sky LUT) is rendered with the Y-down ortho
            //   so its row 0 is the visual TOP, inverse of the raylib RT
            //   convention — cancel the RT auto-invert. (View 4, the SSR
            //   map, is a standard y-up buffer since the depth-tap
            //   orientation fix, so it takes the normal auto-invert path.)
            .scale = {1.0f, (s_debugView == 9) ? -1.0f : 1.0f},
            .tint = white,
        });
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    }

    gX_Camera2D cam2d = {{0,0}, {0,0}, 0.0f, 1.0f};
    gX_Drawing_Begin2D(cam2d);

    gX_Font font = gX_Font_GetDefault();
    gX_F32 fsize = 16.0f;
    gX_F32 x = 12.0f, y = 12.0f;
    gX_I8 line[256];

    //opencode: FPS + preset line
    gX_F64 dt = gX_FrameTime_Get();
    gX_I32 fps = (dt > 0.0) ? (gX_I32)(1.0 / dt + 0.5) : 0;
    snprintf(line, sizeof(line), "gX showcase  fps=%d  preset=%d  csms=%d  mode=%s  focus=%s",
        fps, gfx->state.qualityPreset, gfx->csm.cascadeCount,
        s_flyMode ? "fly" : "orbit",
        s_focus >= 0 ? "model" : "scene");
    gX_RawString_Draw(line, font, (gX_F32v2){x, y}, (gX_F32v2){0,0}, 0.0f, fsize, 1.0f, white);
    y += fsize + 4.0f;

    gX_I8 *togg[16];
    gX_I32 tn = 0;
    #define TOGGLE_ENTRY(key, flag) do { if (flag) togg[tn++] = (key); } while (0)
    TOGGLE_ENTRY("B:bloom", gfx->state.ENABLE_BLOOM);
    TOGGLE_ENTRY("S:ssao", gfx->state.ENABLE_SSAO);
    TOGGLE_ENTRY("R:ssr", gfx->state.ENABLE_SSR);
    TOGGLE_ENTRY("T:taa", gfx->state.ENABLE_TAA);
    TOGGLE_ENTRY("M:mblur", gfx->state.ENABLE_MOTION_BLUR);
    //opencode: report volume fog in the showcase HUD; the showcase uses the
    //   volumetric path rather than the legacy linear fog flag.
    TOGGLE_ENTRY("F:fog", gfx->state.ENABLE_FOG || gfx->state.ENABLE_VOLUME_FOG);
    TOGGLE_ENTRY("G:godrays", gfx->state.ENABLE_GODRAYS);
    TOGGLE_ENTRY("P:particles", gfx->state.ENABLE_PARTICLES);
    TOGGLE_ENTRY("Y:refl", gfx->state.ENABLE_REFLECTIONS);
    TOGGLE_ENTRY("X:occull", gfx->state.ENABLE_OCCLUSION_CULLING);
    TOGGLE_ENTRY("L:markers", gfx->state.SHOW_LIGHT_MARKERS);
    #undef TOGGLE_ENTRY

    snprintf(line, sizeof(line), "on: %s", tn > 0 ? togg[0] : "-");
    for (gX_I32 i = 1; i < tn; i++) {
        gX_I8 tmp[256];
        snprintf(tmp, sizeof(tmp), "  %s", togg[i]);
        strncat(line, tmp, sizeof(line) - strlen(line) - 1);
    }
    gX_RawString_Draw(line, font, (gX_F32v2){x, y}, (gX_F32v2){0,0}, 0.0f, fsize, 1.0f, dim);
    y += fsize + 4.0f;

    snprintf(line, sizeof(line),
        "keys: LMB orbit  wheel zoom  C fly 1-9 focus  0 center  Q preset  K csm  Z sky  D debug  L markers  H hud");
    gX_RawString_Draw(line, font, (gX_F32v2){x, y}, (gX_F32v2){0,0}, 0.0f, fsize, 1.0f, dim);
    y += fsize + 4.0f;

    //opencode: active raw-RT debug view name
    if (s_debugView > 0) {
        static const gX_I8 *dbgNames[13] = {"", "ssao map", "ssao blur", "depth", "ssr", "hdrScene", "sceneColorLDR", "bloom.scene", "reflRT", "skyLUT", "cookieAtlas", "contact", "velocity"};
        snprintf(line, sizeof(line), "debug: %s", dbgNames[s_debugView]);
        gX_RawString_Draw(line, font, (gX_F32v2){x, y}, (gX_F32v2){0,0}, 0.0f, fsize, 1.0f, white);
    }

    gX_Drawing_End2D();
    gX_Drawing_EndTexture();
}
