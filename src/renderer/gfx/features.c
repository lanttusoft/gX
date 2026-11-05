/**********************************************************************************************
*
*   [gX: src/renderer/gfx/features.c]
*   gX — shared GFX feature predicates
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


#include <stdlib.h>

#include "../gfx.h"
#include "../../common/log.h"
#include "features.h"
#include "gl_level.h"
#include "load.h"
#include "../renderer.h"
#include "../../gui/gui.h"
#include "../../main.h"

//ai: one table is the source of truth for enabled features, dependencies,
//   passes and targets. The existing ENABLE_* flags remain the scene-level
//   inputs; allocation/pass code can migrate onto this table incrementally.
typedef enum gX_RendererGFX_FeatureId {
    GX_GFX_FEATURE_SHADOWS = 0,
    GX_GFX_FEATURE_POINT_SHADOWS,
    GX_GFX_FEATURE_COOKIES,
    GX_GFX_FEATURE_SSAO,
    GX_GFX_FEATURE_SSGI,
    GX_GFX_FEATURE_SSR,
    GX_GFX_FEATURE_BLOOM,
    GX_GFX_FEATURE_GODRAYS,
    GX_GFX_FEATURE_LENS_FLARE,
    GX_GFX_FEATURE_FOG,
    GX_GFX_FEATURE_VOLUME_FOG,
    GX_GFX_FEATURE_MOTION_BLUR,
    GX_GFX_FEATURE_DOF,
    GX_GFX_FEATURE_TAA,
    GX_GFX_FEATURE_PARTICLES,
    GX_GFX_FEATURE_PLANAR_REFLECTIONS,
    GX_GFX_FEATURE_FXAA,
    GX_GFX_FEATURE_RAYMARCH,
    GX_GFX_FEATURE_GUI_MENU,
    GX_GFX_FEATURE_BATTLE,
    GX_GFX_FEATURE_GFX_SCENE,
    GX_GFX_FEATURE_COUNT
} gX_RendererGFX_FeatureId;

#define GX_FEATURE_BIT(id) (1u << (gX_U32)(id))

typedef struct gX_RendererGFX_FeatureSpec {
    const gX_I8 *id;
    const gX_I8 *label;
    gX_I1 (*enabled)(gX_RendererGFX *gfx);
    gX_U32 depends;
    gX_I1 needsHdr;
    gX_I1 hasEffects;
    gX_I8 cost; // 0 cheap, 1 medium, 2 expensive
    const gX_I8 *passes;
    const gX_I8 *targets;
} gX_RendererGFX_FeatureSpec;

static gX_I1 feature_shadows(gX_RendererGFX *gfx) { return gfx->state.ENABLE_SHADOWS ? 1 : 0; }
static gX_I1 feature_point_shadows(gX_RendererGFX *gfx) { return gfx->state.ENABLE_POINT_SHADOWS ? 1 : 0; }
static gX_I1 feature_cookies(gX_RendererGFX *gfx) { return gfx->state.ENABLE_COOKIES ? 1 : 0; }
static gX_I1 feature_ssao(gX_RendererGFX *gfx) { return gfx->state.ENABLE_SSAO ? 1 : 0; }
static gX_I1 feature_ssgi(gX_RendererGFX *gfx) { return gX_RendererGFX_Feature_HasSsgi(gfx); }
static gX_I1 feature_ssr(gX_RendererGFX *gfx) { return gfx->state.ENABLE_SSR ? 1 : 0; }
static gX_I1 feature_bloom(gX_RendererGFX *gfx) { return gfx->state.ENABLE_BLOOM ? 1 : 0; }
static gX_I1 feature_godrays(gX_RendererGFX *gfx) { return gfx->state.ENABLE_GODRAYS ? 1 : 0; }
static gX_I1 feature_lensflare(gX_RendererGFX *gfx) { return gfx->state.ENABLE_LENS_FLARE ? 1 : 0; }
static gX_I1 feature_fog(gX_RendererGFX *gfx) { return gfx->state.ENABLE_FOG ? 1 : 0; }
static gX_I1 feature_volume_fog(gX_RendererGFX *gfx) { return gfx->state.ENABLE_VOLUME_FOG ? 1 : 0; }
static gX_I1 feature_motion(gX_RendererGFX *gfx) { return gfx->state.ENABLE_MOTION_BLUR ? 1 : 0; }
static gX_I1 feature_dof(gX_RendererGFX *gfx) { return gfx->state.ENABLE_DOF ? 1 : 0; }
static gX_I1 feature_taa(gX_RendererGFX *gfx) { return gfx->state.ENABLE_TAA ? 1 : 0; }
static gX_I1 feature_particles(gX_RendererGFX *gfx) { return gfx->state.ENABLE_PARTICLES ? 1 : 0; }
static gX_I1 feature_planar(gX_RendererGFX *gfx) { return gfx->state.ENABLE_PLANAR_REFLECTIONS ? 1 : 0; }
static gX_I1 feature_fxaa(gX_RendererGFX *gfx) { return gfx->state.ENABLE_FXAA ? 1 : 0; }
static gX_I1 feature_raymarch(gX_RendererGFX *gfx) {
    (void)gfx;
    if (renderer.raymarch.amount > 0 && renderer.raymarch.state.DRAW_ALL) return 1;
    const gX_I8 *view = getenv("GX_GFX_RAYMARCH_VIEW");
    const gX_I8 *depth = getenv("GX_GFX_RAYMARCH_DEPTH");
    const gX_I8 *composite = getenv("GX_GFX_RAYMARCH_COMPOSITE");
    return (view && view[0] == '1') || (depth && depth[0] == '1')
        || (composite && composite[0] != '0')
        || getenv("GX_RAYMARCH_DEPTH_TEST") != NULL;
}
static gX_I1 feature_gui_menu(gX_RendererGFX *gfx) { (void)gfx; return gui.state.SHOW_MENU ? 1 : 0; }
static gX_I1 feature_battle(gX_RendererGFX *gfx) { return gfx->common.battleScene ? 1 : 0; }
static gX_I1 feature_gfx_scene(gX_RendererGFX *gfx) {
    (void)gfx;
    return ((maindata.masterIndex == gX_SCENE_RENDERER_GFX && !arguments.showcase)
        || maindata.masterIndex == gX_SCENE_GFXDEBUG) ? 1 : 0;
}

static const gX_RendererGFX_FeatureSpec gX_FeatureSpecs[GX_GFX_FEATURE_COUNT] = {
    {"shadows", "Sun shadows", feature_shadows, 0, 0, 0, 1, "shadow", "sun.map,csm"},
    {"point-shadows", "Point-light shadows", feature_point_shadows, 0, 0, 0, 2, "shadow", "pointarray"},
    {"cookies", "Light cookies", feature_cookies, 0, 0, 0, 0, "opaque", "cookie.atlas"},
    {"ssao", "GTAO/SSAO", feature_ssao, 0, 1, 0, 1, "ssao", "ssao.map,blur"},
    {"ssgi", "Screen-space GI", feature_ssgi, GX_FEATURE_BIT(GX_GFX_FEATURE_SSAO), 1, 1, 2, "ssgi", "ssgi.accum"},
    {"ssr", "Screen-space reflections", feature_ssr, 0, 1, 1, 2, "ssr", "ssr.map"},
    {"bloom", "Bloom/HDR", feature_bloom, 0, 1, 1, 1, "bloom", "bloom.scene,hdrScene"},
    {"godrays", "God rays", feature_godrays, 0, 1, 1, 1, "godrays", "godray.map"},
    {"lensflare", "Lens flare", feature_lensflare, 0, 1, 1, 0, "lensflare", "lensflare.map"},
    {"fog", "Distance fog", feature_fog, 0, 0, 1, 0, "fog", "bloom.scene"},
    {"volume-fog", "Volumetric fog", feature_volume_fog, GX_FEATURE_BIT(GX_GFX_FEATURE_SHADOWS), 1, 1, 2, "fogvolume", "volfog.map"},
    {"motion-blur", "Motion blur", feature_motion, 0, 1, 1, 1, "motionblur", "motion.map"},
    {"dof", "Depth of field", feature_dof, 0, 1, 1, 1, "dof", "dof.coc"},
    {"taa", "Temporal AA", feature_taa, 0, 1, 0, 1, "taa", "taa.history"},
    {"particles", "Particles", feature_particles, 0, 0, 1, 1, "particles", "particle.buffers"},
    {"planar-reflections", "Planar reflections", feature_planar, 0, 0, 0, 2, "reflections", "planar.rt"},
    {"fxaa", "FXAA", feature_fxaa, 0, 0, 0, 0, "fxaa", "post.ldr"},
    {"raymarch", "Raymarch renderer", feature_raymarch, 0, 1, 0, 2, "raymarch_depth", "hdrScene.depth"},
    {"gui-menu", "Pause menu backdrop", feature_gui_menu, 0, 1, 0, 0, "gui", "gui.ping"},
    {"battle", "Battle scene", feature_battle, 0, 1, 0, 1, "battle", "hdrScene"},
    {"gfx-scene", "GFX demo scene", feature_gfx_scene, 0, 1, 0, 1, "scene", "hdrScene"},
};

static gX_U32 gX_RendererGFX_Feature_Mask(gX_RendererGFX *gfx)
{
    gX_U32 mask = 0;
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++)
        if (gX_FeatureSpecs[i].enabled(gfx)) mask |= GX_FEATURE_BIT(i);
    return mask;
}

static gX_U32 gX_RendererGFX_Feature_PopCount(gX_U32 value)
{
    gX_U32 count = 0;
    while (value) { count += value & 1u; value >>= 1u; }
    return count;
}

gX_I1 gX_RendererGFX_Feature_HasSsgi(gX_RendererGFX *gfx)
{
    return (gfx->state.ENABLE_SSAO && gfx->ssao.useSSGI) ? 1 : 0;
}

gX_I1 gX_RendererGFX_Feature_HasEffects(gX_RendererGFX *gfx)
{
    gX_U32 mask = gX_RendererGFX_Feature_Mask(gfx);
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++)
        if ((mask & GX_FEATURE_BIT(i)) && gX_FeatureSpecs[i].hasEffects) return 1;
    return 0;
}

gX_RenderTexture *gX_RendererGFX_Feature_SceneColorTarget(gX_RendererGFX *gfx)
{
    if (!gfx) return NULL;
    if (gfx->bloom.hdrScene.wrapper.loadingState.loaded
        && gfx->bloom.hdrScene.wrapper.rt.texture.id > 0)
        return &gfx->bloom.hdrScene.wrapper.rt;
    if (gfx->sceneColorLDR.wrapper.loadingState.loaded
        && gfx->sceneColorLDR.wrapper.rt.texture.id > 0)
        return &gfx->sceneColorLDR.wrapper.rt;
    return NULL;
}

gX_I1 gX_RendererGFX_Feature_NeedHdr(gX_RendererGFX *gfx)
{
    gX_U32 mask = gX_RendererGFX_Feature_Mask(gfx);
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++)
        if ((mask & GX_FEATURE_BIT(i)) && gX_FeatureSpecs[i].needsHdr) return 1;
    return 0;
}

gX_U32 gX_RendererGFX_Feature_Count(gX_I0)
{
    return (gX_U32)GX_GFX_FEATURE_COUNT;
}

const gX_RendererGFX_FeatureInfo *gX_RendererGFX_Feature_InfoAt(gX_U32 id)
{
    static gX_RendererGFX_FeatureInfo info;
    if (id >= (gX_U32)GX_GFX_FEATURE_COUNT) return NULL;
    const gX_RendererGFX_FeatureSpec *spec = &gX_FeatureSpecs[id];
    info.id = spec->id;
    info.label = spec->label;
    info.dependencies = spec->depends;
    info.needsHdr = spec->needsHdr;
    info.hasEffects = spec->hasEffects;
    info.contextOnly = id >= (gX_U32)GX_GFX_FEATURE_RAYMARCH;
    info.cost = spec->cost;
    info.passes = spec->passes;
    info.targets = spec->targets;
    return &info;
}

gX_I1 gX_RendererGFX_Feature_IsEnabled(gX_RendererGFX *gfx, gX_U32 id)
{
    if (id >= (gX_U32)GX_GFX_FEATURE_COUNT) return 0;
    return gX_FeatureSpecs[id].enabled(gfx);
}

gX_I0 gX_RendererGFX_Feature_Set(gX_RendererGFX *gfx, gX_U32 id,
    gX_I1 enabled)
{
    if (id >= (gX_U32)GX_GFX_FEATURE_RAYMARCH) return;
    gX_I1 value = enabled ? 1 : 0;
    switch (id) {
        case GX_GFX_FEATURE_SHADOWS: gfx->state.ENABLE_SHADOWS = value; break;
        case GX_GFX_FEATURE_POINT_SHADOWS: gfx->state.ENABLE_POINT_SHADOWS = value; break;
        case GX_GFX_FEATURE_COOKIES: gfx->state.ENABLE_COOKIES = value; break;
        case GX_GFX_FEATURE_SSAO:
            gfx->state.ENABLE_SSAO = value;
            if (!value) gfx->ssao.useSSGI = 0;
            break;
        case GX_GFX_FEATURE_SSGI:
            gfx->state.ENABLE_SSAO = value ? 1 : gfx->state.ENABLE_SSAO;
            gfx->ssao.useSSGI = value;
            break;
        case GX_GFX_FEATURE_SSR: gfx->state.ENABLE_SSR = value; break;
        case GX_GFX_FEATURE_BLOOM: gfx->state.ENABLE_BLOOM = value; break;
        case GX_GFX_FEATURE_GODRAYS: gfx->state.ENABLE_GODRAYS = value; break;
        case GX_GFX_FEATURE_LENS_FLARE: gfx->state.ENABLE_LENS_FLARE = value; break;
        case GX_GFX_FEATURE_FOG: gfx->state.ENABLE_FOG = value; break;
        case GX_GFX_FEATURE_VOLUME_FOG: gfx->state.ENABLE_VOLUME_FOG = value; break;
        case GX_GFX_FEATURE_MOTION_BLUR: gfx->state.ENABLE_MOTION_BLUR = value; break;
        case GX_GFX_FEATURE_DOF: gfx->state.ENABLE_DOF = value; break;
        case GX_GFX_FEATURE_TAA: gfx->state.ENABLE_TAA = value; break;
        case GX_GFX_FEATURE_PARTICLES: gfx->state.ENABLE_PARTICLES = value; break;
        case GX_GFX_FEATURE_PLANAR_REFLECTIONS: gfx->state.ENABLE_PLANAR_REFLECTIONS = value; break;
        case GX_GFX_FEATURE_FXAA: gfx->state.ENABLE_FXAA = value; break;
        default: return;
    }
    if (value) {
        for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_RAYMARCH; i++)
            if (gX_FeatureSpecs[id].depends & GX_FEATURE_BIT(i))
                gX_RendererGFX_Feature_Set(gfx, i, 1);
    } else {
        for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_RAYMARCH; i++)
            if ((gX_FeatureSpecs[i].depends & GX_FEATURE_BIT(id))
                && gX_RendererGFX_Feature_IsEnabled(gfx, i))
                gX_RendererGFX_Feature_Set(gfx, i, 0);
    }
}

gX_I0 gX_RendererGFX_Feature_ApplyTemplate(gX_RendererGFX *gfx,
    const gX_I8 *name)
{
    if (!gfx || !name) return;
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_RAYMARCH; i++)
        gX_RendererGFX_Feature_Set(gfx, i, 0);
    gfx->state.ENABLE_FLAT = 0;
    gfx->state.ENABLE_GOURAUD = 0;
    gfx->state.ENABLE_PHONG = 1;
    if (strcmp((const char *)name, "unlit") == 0) {
        gfx->state.ENABLE_FLAT = 1;
        gfx->state.ENABLE_PHONG = 0;
        return;
    }
    gX_U32 mask = 0;
    if (strcmp((const char *)name, "lit") == 0) {
        mask = 0;
    } else if (strcmp((const char *)name, "lit-shadows") == 0) {
        mask = GX_FEATURE_BIT(GX_GFX_FEATURE_SHADOWS)
            | GX_FEATURE_BIT(GX_GFX_FEATURE_FXAA);
    } else if (strcmp((const char *)name, "hdr-bloom") == 0) {
        mask = GX_FEATURE_BIT(GX_GFX_FEATURE_SHADOWS)
            | GX_FEATURE_BIT(GX_GFX_FEATURE_BLOOM)
            | GX_FEATURE_BIT(GX_GFX_FEATURE_FXAA);
    } else if (strcmp((const char *)name, "full") == 0) {
        for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_RAYMARCH; i++)
            mask |= GX_FEATURE_BIT(i);
    } else {
        return;
    }
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++)
        if (mask & GX_FEATURE_BIT(i))
            gX_RendererGFX_Feature_Set(gfx, i, 1);
}

//ai: the "fewer, better pixels" preset — every user feature plus ultra
//   quality. MSAA follows the Ultra preset (4x); at 320x240 that is cheap and
//   the present filter still owns the soft/sharp look.
gX_I0 gX_RendererGFX_ConsoleMax_Apply(gX_RendererGFX *gfx)
{
    if (!gfx) return;
    gX_RendererGFX_Feature_ApplyTemplate(gfx, "full");
    gfx->state.qualityPreset = 3;
    gX_RendererGFX_ApplyQualityPreset(gfx);
    gfx->state.ENABLE_REFLECTIONS = 1;
    gfx->state.ENABLE_DECALS = 1;
    gfx->state.ENABLE_MAP_ALBEDO = 1;
    gfx->state.ENABLE_MAP_METALNESS = 1;
    gfx->state.ENABLE_MAP_NORMAL = 1;
    gfx->state.ENABLE_MAP_PARALLAX = 1;
    gfx->state.ENABLE_LIGHTING_AMBIENT = 1;
    gfx->state.ENABLE_LIGHTING_DIFFUSE = 1;
    gfx->state.ENABLE_LIGHTING_SPECULAR = 1;
    gfx->state.ENABLE_CLUSTERED = 1;
    gfx->state.ENABLE_RIM = 1;
    gfx->state.ENABLE_ENV_AMBIENT = 1;
    gfx->state.ENABLE_FRUSTUM_CULLING = 1;
    gfx->state.ENABLE_OCCLUSION_CULLING = 1;
    gfx->state.ENABLE_RT_DIET = 0;
    gfx->csm.cascadeCount = 4;
    gfx->ssao.kernelSize = 64;
    gfx->ssao.useGTAO = 1;
    gfx->ssao.useSSGI = 1;
    gfx->ssr.resolutionDivisor = 1;
    gfx->bloom.autoExposure = 1;
    if (gX_GL.hasLayeredShadows && gX_GL.hasInstancing)
        gfx->state.ENABLE_LAYERED_SHADOWS = 1;
    if (renderer.raymarch.amount > 0 && renderer.raymarch.data) {
        gX_RendererRaymarch *raymarch =
            (gX_RendererRaymarch *)renderer.raymarch.data;
        gX_RendererRaymarch_Quality_Set(raymarch, 3);
    }
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "Console Max applied: features=full quality=ultra");
}

gX_I0 gX_RendererGFX_Feature_PlanLog(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_GFX_PLAN");
    static gX_I1 logged = 0;
    if (!hook || hook[0] == '0' || logged) return;
    logged = 1;
    gX_U32 mask = gX_RendererGFX_Feature_Mask(gfx);
    gX_U32 missing = 0;
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++)
        if (mask & GX_FEATURE_BIT(i))
            missing |= gX_FeatureSpecs[i].depends & ~mask;
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "GFX_PLAN mask=0x%08x count=%u needHdr=%d effects=%d missing=0x%08x",
        mask, gX_RendererGFX_Feature_PopCount(mask),
        gX_RendererGFX_Feature_NeedHdr(gfx),
        gX_RendererGFX_Feature_HasEffects(gfx), missing);
    for (gX_U32 i = 0; i < (gX_U32)GX_GFX_FEATURE_COUNT; i++) {
        if (!(mask & GX_FEATURE_BIT(i))) continue;
        gX_TraceLog_GFX(gX_LOG_NOTICE,
            "GFX_PLAN feature=%s cost=%d passes=%s targets=%s",
            gX_FeatureSpecs[i].id, (gX_I32)gX_FeatureSpecs[i].cost,
            gX_FeatureSpecs[i].passes, gX_FeatureSpecs[i].targets);
    }
}

gX_I0 gX_RendererGFX_Feature_Trace(gX_RendererGFX *gfx)
{
    gX_RendererGFX_Feature_PlanLog(gfx);
    const gX_I8 *trace = getenv("GX_GFX_FEATURE_TRACE");
    static gX_I1 traced = 0;
    if (!trace || trace[0] == '0' || traced) return;
    traced = 1;
    gX_TraceLog_GFX(gX_LOG_NOTICE,
        "GFX_FEATURES hasEffects=%d needHdr=%d ssgi=%d bloom=%d ssr=%d fog=%d "
        "dof=%d motion=%d particles=%d | hdrLoaded=%d bloomSceneLoaded=%d",
        gX_RendererGFX_Feature_HasEffects(gfx),
        gX_RendererGFX_Feature_NeedHdr(gfx),
        gX_RendererGFX_Feature_HasSsgi(gfx),
        gfx->state.ENABLE_BLOOM ? 1 : 0,
        gfx->state.ENABLE_SSR ? 1 : 0,
        gfx->state.ENABLE_FOG ? 1 : 0,
        gfx->state.ENABLE_DOF ? 1 : 0,
        gfx->state.ENABLE_MOTION_BLUR ? 1 : 0,
        gfx->state.ENABLE_PARTICLES ? 1 : 0,
        gfx->bloom.hdrScene.wrapper.loadingState.loaded ? 1 : 0,
        gfx->bloom.scene.wrapper.loadingState.loaded ? 1 : 0);
}
