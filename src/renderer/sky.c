/**********************************************************************************************
*
*   [gX: src/renderer/sky.c]
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


//opencode: ported from game3 — shader sky (LoadingState GPU deferral)
#include "sky.h"

#include "../camera.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "../common/utils.h"
#include "../gxmath/vector3.h"
#include "../main.h"
#include "../wrappers/wrappers.h"
#include "../resource/resource.h"

#include "renderer.h"

#include "../game/ephemeris.h"
#include "../game/game.h"

#include <math.h>
//ai: opt-in deterministic sky audit hooks.
#include <stdlib.h>
#include "../gxmath/constants.h"
#include "../rhi/rhi.h"

static gX_I0 sky_gpu_load(gX_I0 *arg) {
    gX_RendererSky *sky = (gX_RendererSky*)arg;
    { gX_Shader *_rsc = gX_Resource_LoadShaderFrag("sky_new.frag"); if (_rsc) sky->shader = *_rsc; }
    //opencode: initialize locs[] so the rlgl batch system doesn't call
    //   glUniform on garbage indices. gX_Resource_LoadShaderFrag returns
    //   a freshly-allocated shader with locs[] uninitialized (or set from
    //   a previous load). Calling GetDefaultLocations fills in -1 for
    //   missing uniforms.
    gX_Shader_GetDefaultLocations(&sky->shader);
    sky->shader_timeLoc    = gX_Shader_GetLocation(sky->shader, "time");
    sky->shader_resLoc     = gX_Shader_GetLocation(sky->shader, "res");
    sky->shader_cposLoc    = gX_Shader_GetLocation(sky->shader, "cpos");
    sky->shader_ctargetLoc = gX_Shader_GetLocation(sky->shader, "ctarget");
    sky->shader_cupLoc     = gX_Shader_GetLocation(sky->shader, "cup");
    sky->shader_cfovLoc    = gX_Shader_GetLocation(sky->shader, "cfov");
    sky->shader_sunDirLoc  = gX_Shader_GetLocation(sky->shader, "sunDir");
    sky->shader_atmosphereStateLoc = gX_Shader_GetLocation(sky->shader, "atmosphereState");
    sky->shader_skyDensityLoc     = gX_Shader_GetLocation(sky->shader, "skyDensity");
    sky->shader_skyHueLoc         = gX_Shader_GetLocation(sky->shader, "skyHue");
    sky->shader_horizonGlowLoc    = gX_Shader_GetLocation(sky->shader, "horizonGlow");
    sky->shader_skyContrastLoc    = gX_Shader_GetLocation(sky->shader, "skyContrast");
    sky->shader_sunDiskSizeLoc    = gX_Shader_GetLocation(sky->shader, "sunDiskSize");
    sky->shader_sunDiskIntensityLoc = gX_Shader_GetLocation(sky->shader, "sunDiskIntensity");
    // Starfield uniform locations
    sky->shader_starDensityLoc      = gX_Shader_GetLocation(sky->shader, "starDensity");
    sky->shader_starBrightnessLoc   = gX_Shader_GetLocation(sky->shader, "starBrightness");
    sky->shader_starTwinkleSpeedLoc = gX_Shader_GetLocation(sky->shader, "starTwinkleSpeed");
    // Moon uniform locations
    sky->shader_moonDirLoc      = gX_Shader_GetLocation(sky->shader, "moonDir");
    sky->shader_moonSizeLoc     = gX_Shader_GetLocation(sky->shader, "moonSize");
    sky->shader_moonBrightnessLoc = gX_Shader_GetLocation(sky->shader, "moonBrightness");

    // Planet uniform locations
    sky->shader_planetCountLoc = gX_Shader_GetLocation(sky->shader, "planetCount");
    for (gX_I32 pi = 0; pi < GX_SKY_MAX_PLANETS; pi++) {
        gX_I8 name[64];
        snprintf(name, sizeof(name), "planetDir[%i]", pi);
        sky->shader_planetDirLocs[pi] = gX_Shader_GetLocation(sky->shader, name);
        snprintf(name, sizeof(name), "planetSize[%i]", pi);
        sky->shader_planetSizeLocs[pi] = gX_Shader_GetLocation(sky->shader, name);
        snprintf(name, sizeof(name), "planetColor[%i]", pi);
        sky->shader_planetColorLocs[pi] = gX_Shader_GetLocation(sky->shader, name);
        snprintf(name, sizeof(name), "planetBrightness[%i]", pi);
        sky->shader_planetBrightnessLocs[pi] = gX_Shader_GetLocation(sky->shader, name);
    }
    // Default sky parameters — sun at ~30° elevation in the south
    //   A lower sun avoids the "bright blob at zenith" issue and gives
    //   more natural lighting.
    sky->sunpos = (gX_F32v3){0.6f, 0.5f, 0.6f};
    sky->skyDensity = 1.0f;
    sky->skyHue = 0.0f;
    sky->horizonGlow = 1.0f;
    sky->skyContrast = 1.8f;        // contrast boost for gradient visibility at narrow FOV (1.5 in GFX, 1.8 standalone)
    sky->altitude = 0.0f;           // sea level
    sky->turbidity = 3.0f;          // moderate clarity
    sky->sunDiskSize = 800.0f;
    sky->sunDiskIntensity = 3.0f;
    // Default starfield parameters
    sky->starDensity = 1.0f;
    sky->starBrightness = 1.0f;
    sky->starTwinkleSpeed = 1.0f;
    // Default moon parameters — 18° above forward in the eastern sky
    sky->moonDir = (gX_F32v3){0.3f, 0.3f, -0.8f};
    sky->moonSize = 700.0f;
    sky->moonBrightness = 0.5f;
    // Default planets: Venus + Mars visible in forward hemisphere
    sky->planetCount = 2;
    // Venus: bright yellowish, eastern sky
    sky->planetDirs[0] = (gX_F32v3){-0.4f, 0.2f, -0.6f};
    sky->planetSizes[0] = 200.0f;
    sky->planetColors[0] = (gX_F32v3){0.9f, 0.8f, 0.6f};
    sky->planetBrightnesses[0] = 1.0f;
    // Mars: reddish, western sky
    sky->planetDirs[1] = (gX_F32v3){0.5f, 0.1f, -0.7f};
    sky->planetSizes[1] = 150.0f;
    sky->planetColors[1] = (gX_F32v3){0.8f, 0.4f, 0.2f};
    sky->planetBrightnesses[1] = 1.0f;
    // Extra planets inactive
    for (gX_I32 pi = 2; pi < GX_SKY_MAX_PLANETS; pi++) {
        sky->planetDirs[pi] = (gX_F32v3){0.0f, 0.0f, -1.0f};
        sky->planetSizes[pi] = 100.0f;
        sky->planetColors[pi] = (gX_F32v3){1.0f, 0.8f, 0.6f};
        sky->planetBrightnesses[pi] = 1.0f;
    }
    sky->loadingstate.loaded = true;
}

static gX_I0 sky_gpu_unload(gX_I0 *arg) {
    gX_RendererSky *sky = (gX_RendererSky*)arg;
    //opencode: shader is RM-managed — cleanup handled by resource manager
    sky->loadingstate.loaded = false;
}

gX_I32 gX_RendererSky_Load(gX_RendererSky *sky) {
    gX_TRACELOG_PROLOGUE("RendererSky_Load","RENDERER_SKY: Loading")
    strcpy(sky->name, "[gX] Sky");
    strcpy(sky->description, "shader-based sky");
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "name: %s", sky->name);
    gX_TraceLog_RENDERER(gX_LOG_GREEN, "desc: %s", sky->description);
    sky->loadingstate = (gX_LoadingState){0};
    sky->loadingstate.loadAsap = true;
    gX_TRACELOG_EPILOGUE("RENDERER_SKY: Load done")
    return 0;
}

gX_I32 gX_RendererSky_Unload(gX_RendererSky *sky) {
    if (sky->loadingstate.loaded) sky_gpu_unload(sky);
    return 0;
}

//opencode: read body direction from ephemeris when available, fall back to hardcoded
static gX_F32v3 sky_get_sun_dir(gX_RendererSky *sky) {
    gX_Ephemeris_Body *sun = gX_Ephemeris_GetBody(&gamedata.ephemeris, "Sun");
    if (sun) return sun->direction;
    return sky->sunpos;
}

static gX_F32v3 sky_get_moon_dir(gX_RendererSky *sky) {
    gX_Ephemeris_Body *moon = gX_Ephemeris_GetBody(&gamedata.ephemeris, "Moon");
    if (moon) return moon->direction;
    return sky->moonDir;
}

//ai: test-only deterministic sky clock. GX_SKY_TIME sets the ephemeris epoch
//    and places the observer at Earth so the Sun/Moon directions are
//    physically meaningful; unset leaves preset/game time untouched.
gX_I0 gX_RendererSky_Update(gX_RendererSky *sky) {
    gX_LoadingState_Handler(&sky->loadingstate, sky_gpu_load, sky_gpu_unload, sky);
    if (!sky->loadingstate.loaded) return;

    const gX_I8 *skyTimeHook = getenv("GX_SKY_TIME");
    if (skyTimeHook && skyTimeHook[0])
    {
        gX_F64 skyTime = (gX_F64)atof((const char *)skyTimeHook);
        if (isfinite(skyTime))
        {
            gamedata.ephemeris.timeSec = skyTime;
            gX_Ephemeris_Body *earth = gX_Ephemeris_GetBody(
                &gamedata.ephemeris, "Earth");
            if (earth)
            {
                gX_I32 earthIndex = (gX_I32)(earth - gamedata.ephemeris.bodies);
                gamedata.ephemeris.observerPos = gX_Ephemeris_GetAbsolutePosition(
                    &gamedata.ephemeris, earthIndex, skyTime);
            }
            gX_Ephemeris_Update(&gamedata.ephemeris, skyTime);
        }
    }

    gX_F32v3 pos = camera.camera3D.position, target = camera.camera3D.target, up = camera.camera3D.up;
    gX_F32 t = (gX_F32)maindata.timeElapsed;
    gX_F32v2 res = { (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight };

    // Get body directions from ephemeris (or fallback to hardcoded)
    gX_F32v3 sunDir = sky_get_sun_dir(sky);
    gX_F32v3 moonDir = sky_get_moon_dir(sky);
    //ai: deterministic standalone sky probes cover day, twilight, night and polar conjunction.
    const gX_I8 *skyCheck = getenv("GX_SKY_CHECK");
    if (skyCheck)
    {
        gX_I32 mode = atoi(skyCheck);
        t = 0.0f;
        pos = (gX_F32v3){0};
        target = (gX_F32v3){0,0,1};
        up = (gX_F32v3){0,1,0};
        sunDir = (gX_F32v3){0,mode == 0 ? 0.6f : mode == 1 ? -0.02f : -0.8f,1};
        moonDir = (gX_F32v3){0,0.3f,1};
        sky->starTwinkleSpeed = 0;
        if (mode == 3)
        {
            target = sunDir = moonDir = (gX_F32v3){0,1,0};
            up = (gX_F32v3){0,0,-1};
        }
        camera.camera3D.position = pos;
        camera.camera3D.target = target;
        camera.camera3D.up = up;
        camera.camera3D.fovy = 90;
    }

    gX_Shader_SetValue_Float(sky->shader, sky->shader_timeLoc,    t);
    gX_Shader_SetValue_V2(   sky->shader, sky->shader_resLoc,     res);
    gX_Shader_SetValue_V3(   sky->shader, sky->shader_cposLoc,    pos);
    gX_Shader_SetValue_V3(   sky->shader, sky->shader_ctargetLoc, target);
    gX_Shader_SetValue_V3(   sky->shader, sky->shader_cupLoc,     up);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_cfovLoc,    fminf(camera.camera3D.fovy * GX_SKY_FOV_MULTIPLIER, GX_SKY_FOV_MAX));
    gX_Shader_SetValue_V3(   sky->shader, sky->shader_sunDirLoc,  sunDir);
    gX_Shader_SetValue_V2(   sky->shader, sky->shader_atmosphereStateLoc,
        (gX_F32v2){sky->altitude, sky->turbidity});
    gX_Shader_SetValue_Float(sky->shader, sky->shader_skyDensityLoc,     sky->skyDensity);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_skyHueLoc,         sky->skyHue);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_horizonGlowLoc,    sky->horizonGlow);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_skyContrastLoc,    sky->skyContrast);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_sunDiskSizeLoc,    sky->sunDiskSize);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_sunDiskIntensityLoc, sky->sunDiskIntensity);
    // Starfield uniforms
    gX_Shader_SetValue_Float(sky->shader, sky->shader_starDensityLoc,      sky->starDensity);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_starBrightnessLoc,   sky->starBrightness);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_starTwinkleSpeedLoc, sky->starTwinkleSpeed);
    // Moon uniforms
    gX_Shader_SetValue_V3(   sky->shader, sky->shader_moonDirLoc,      moonDir);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_moonSizeLoc,     sky->moonSize);
    gX_Shader_SetValue_Float(sky->shader, sky->shader_moonBrightnessLoc, sky->moonBrightness);
    // Planet uniforms — read from ephemeris when available
    gX_I32 planetCount = sky->planetCount;
    gX_F32v3 planetDirs[GX_SKY_MAX_PLANETS];
    for (gX_I32 pi = 0; pi < GX_SKY_MAX_PLANETS; pi++) {
        planetDirs[pi] = sky->planetDirs[pi];
    }
    if (planetCount == 0) {
        // Auto-populate planets from ephemeris (up to 4 planets beyond Sun/Earth/Moon)
        planetCount = 0;
        const gX_I8 *planetNames[] = {"Venus", "Mars", "Mercury", "Jupiter"};
        for (gX_I32 pi = 0; pi < 4 && pi + 3 < (gX_I32)gamedata.ephemeris.bodyCount; pi++) {
            gX_Ephemeris_Body *body = gX_Ephemeris_GetBody(&gamedata.ephemeris, planetNames[pi]);
            if (body) {
                planetDirs[pi] = body->direction;
                planetCount++;
            }
        }
    }
    gX_Shader_SetValue_Int(sky->shader, sky->shader_planetCountLoc, planetCount);
    for (gX_I32 pi = 0; pi < GX_SKY_MAX_PLANETS; pi++) {
        gX_Shader_SetValue_V3(   sky->shader, sky->shader_planetDirLocs[pi],        planetDirs[pi]);
        gX_Shader_SetValue_Float(sky->shader, sky->shader_planetSizeLocs[pi],       sky->planetSizes[pi]);
        gX_Shader_SetValue_V3(   sky->shader, sky->shader_planetColorLocs[pi],      sky->planetColors[pi]);
        gX_Shader_SetValue_Float(sky->shader, sky->shader_planetBrightnessLocs[pi], sky->planetBrightnesses[pi]);
    }
}

//opencode: project a 3D direction to screen pixel coordinates
//   Uses the same camera math as the sky shader.
//   Returns (x, y, inFront) where inFront > 0 means the point is in front of the camera.
static gX_F32v3 sky_project_dir(gX_F32v3 dir, gX_F32v3 cr, gX_F32v3 cu, gX_F32v3 cf,
    gX_F32 planeDist, gX_F32 aspect, gX_F32 w, gX_F32 h)
{
    gX_F32 x = gX_F32v3_Dot(dir, cr);
    gX_F32 y = gX_F32v3_Dot(dir, cu);
    gX_F32 z = -gX_F32v3_Dot(dir, cf);
    if (z <= GX_SKY_PROJECT_Z_THRESHOLD) return (gX_F32v3){0, 0, -1};
    gX_F32 uv_x = x / z * planeDist;
    gX_F32 uv_y = y / z * planeDist;
    gX_F32 sx = (uv_x / aspect + 0.5f) * w;
    gX_F32 sy = (uv_y + 0.5f) * h;
    return (gX_F32v3){sx, sy, 1};
}

//opencode: draw debug overlay helpers (horizon, altitude circles, cardinal directions, orbits)
static gX_I0 sky_draw_helpers(gX_RendererSky *sky) {
    if (!sky->showHelpers) return;
    if (!sky->loadingstate.loaded) return;

    gX_F32 w = (gX_F32)maindata.renderWidth;
    gX_F32 h = (gX_F32)maindata.renderHeight;
    gX_F32 aspect = w / h;
    gX_F32 fovRad = camera.camera3D.fovy * gX_PI / 180.0f;
    gX_F32 planeDist = 1.0f / tanf(fovRad * 0.5f);
    gX_F32v3 cf = gX_F32v3_Normalize(gX_F32v3_Sub(camera.camera3D.target, camera.camera3D.position));
    gX_F32v3 cr = gX_F32v3_Normalize(gX_F32v3_Cross(cf, camera.camera3D.up));
    gX_F32v3 cu = gX_F32v3_Cross(cr, cf);

    // Dot markers
    #define DRAW_DOT(x, y, sz, col) \
        gX_Rectangle_Draw((gX_Rectangle){(x)-(sz)*0.5f, (y)-(sz)*0.5f, (sz), (sz)}, (col))

    // --- Horizon line ---
    if (fabsf(cu.y) > GX_SKY_HORIZON_CUY_THRESHOLD) {
        gX_F32 uv_y_horizon = -cf.y * planeDist / cu.y;
        gX_F32 hz = (uv_y_horizon + 0.5f) * h;
        if (hz >= -GX_SKY_HORIZON_PADDING && hz < h + GX_SKY_HORIZON_PADDING) {
            gX_Rectangle_Draw((gX_Rectangle){0, hz, w, GX_SKY_HORIZON_LINE_THICKNESS}, (gX_Color){255,80,80,255});
        }
    }

    // --- Altitude circles (30°, 60°) ---
    {
        gX_F32 alts[] = {GX_SKY_ALT_ANGLE_1, GX_SKY_ALT_ANGLE_2};
        gX_Color aCols[] = {{0,255,0,255}, {100,200,255,255}};
        for (gX_I32 ai = 0; ai < 2; ai++) {
            gX_F32 sinA = sinf(alts[ai] * (gX_PI / 180.0f));
            gX_F32 cosA = cosf(alts[ai] * (gX_PI / 180.0f));
            gX_I32 segs = GX_SKY_ALT_CIRCLE_SEGMENTS;
            gX_F32v3 pv = {-1,-1,-1};
            for (gX_I32 si = 0; si <= segs; si++) {
                gX_F32 az = (gX_F32)si / (gX_F32)segs * 2.0f * gX_PI;
                gX_F32v3 d = {cosf(az) * cosA, sinA, sinf(az) * cosA};
                gX_F32v3 sp = sky_project_dir(d, cr, cu, cf, planeDist, aspect, w, h);
                if (sp.z > 0 && pv.z > 0) {
                    DRAW_DOT(pv.x, pv.y, GX_SKY_ALT_CIRCLE_DOT_SIZE, aCols[ai]);
                }
                pv = sp;
            }
        }
    }

    // --- Cardinal direction markers ---
    {
        struct { gX_F32v3 d; gX_Color c; } cards[] = {
            {{0,0,-1}, {255,255,255,255}},  // N (forward)
            {{0,0,1},  {150,150,150,255}},   // S (behind)
            {{1,0,0},  {200,200,80,255}},    // E (right)
            {{-1,0,0}, {200,80,200,255}},    // W (left)
        };
        for (gX_I32 ci = 0; ci < 4; ci++) {
            gX_F32v3 sp = sky_project_dir(cards[ci].d, cr, cu, cf, planeDist, aspect, w, h);
            if (sp.z > 0) {
                DRAW_DOT(sp.x, sp.y, GX_SKY_CARDINAL_DOT_SIZE, cards[ci].c);
                DRAW_DOT(sp.x - GX_SKY_CARDINAL_ARM, sp.y, GX_SKY_ALT_CIRCLE_DOT_SIZE, cards[ci].c);
                DRAW_DOT(sp.x + GX_SKY_CARDINAL_ARM, sp.y, GX_SKY_ALT_CIRCLE_DOT_SIZE, cards[ci].c);
                DRAW_DOT(sp.x, sp.y - GX_SKY_CARDINAL_ARM, GX_SKY_ALT_CIRCLE_DOT_SIZE, cards[ci].c);
                DRAW_DOT(sp.x, sp.y + GX_SKY_CARDINAL_ARM, GX_SKY_ALT_CIRCLE_DOT_SIZE, cards[ci].c);
            }
        }
    }

    // --- Ephemeris body orbit paths ---
    if (gamedata.ephemeris.bodyCount > 0) {
        gX_I32 bodies[] = {2, 3, 4}; // Moon, Venus, Mars
        gX_Color oCols[] = {{150,150,200,255}, {200,200,80,255}, {200,80,80,255}};
        for (gX_I32 bi = 0; bi < 3; bi++) {
            gX_I32 idx = bodies[bi];
            if (idx >= (gX_I32)gamedata.ephemeris.bodyCount) continue;
            gX_F64 period = gamedata.ephemeris.bodies[idx].orbit.orbitalPeriodSec;
            if (period <= 0) continue;
            gX_I32 steps = GX_SKY_ORBIT_BODY_STEPS;
            gX_F32v3 pv = {-1,-1,-1};
            for (gX_I32 si = 0; si <= steps; si++) {
                gX_F64 t = (gX_F64)si / (gX_F64)steps * period;
                gX_F32v3 pos = gX_Ephemeris_GetAbsolutePosition(&gamedata.ephemeris, idx, t);
                gX_F32v3 dir = gX_F32v3_Normalize(gX_F32v3_Sub(pos, gamedata.ephemeris.observerPos));
                gX_F32v3 sp = sky_project_dir(dir, cr, cu, cf, planeDist, aspect, w, h);
                if (sp.z > 0 && pv.z > 0) {
                    DRAW_DOT(pv.x, pv.y, GX_SKY_ORBIT_PATH_DOT_SIZE, oCols[bi]);
                }
                pv = sp;
            }
            // Current position marker
            gX_F32v3 cdir = gX_Ephemeris_GetBodyDirection(&gamedata.ephemeris,
                gamedata.ephemeris.bodies[idx].name);
            gX_F32v3 csp = sky_project_dir(cdir, cr, cu, cf, planeDist, aspect, w, h);
            if (csp.z > 0) DRAW_DOT(csp.x, csp.y, GX_SKY_ORBIT_MARKER_SIZE, oCols[bi]);
        }
    }

    #undef DRAW_DOT
}

//opencode: upload camera data to sky shader (override or global)
static gX_I0 sky_upload_camera(gX_RendererSky *sky, const gX_Camera3D *cam) {
    gX_F32v3 pos, target, up; gX_F32 fov;
    if (cam) {
        pos = cam->position; target = cam->target; up = cam->up; fov = cam->fovy;
    } else {
        pos = camera.camera3D.position; target = camera.camera3D.target;
        up = camera.camera3D.up; fov = camera.camera3D.fovy;
    }
    gX_Shader_SetValue_V3(sky->shader, sky->shader_cposLoc,    pos);
    gX_Shader_SetValue_V3(sky->shader, sky->shader_ctargetLoc, target);
    gX_Shader_SetValue_V3(sky->shader, sky->shader_cupLoc,     up);
    //opencode: use raw fov (no 2.5x multiplier) when camera override is provided —
    //   the caller (raymarch, etc.) ensures sky matches its own FOV
    gX_Shader_SetValue_Float(sky->shader, sky->shader_cfovLoc, cam ? fov : fminf(fov * GX_SKY_FOV_MULTIPLIER, GX_SKY_FOV_MAX));
}

gX_I0 gX_RendererSky_DrawTo(gX_RendererSky *sky, gX_RenderTexture *outputRT, const gX_Camera3D *camOverride) {
    if (!sky->loadingstate.loaded) return;
    gX_RenderTexture target = outputRT ? *outputRT : renderer.canvas;
    gX_Drawing_BeginTexture(target);
        gX_Drawing_ClearBG(gX_BLACK);
        gX_Drawing_BeginShader(sky->shader);
            sky_upload_camera(sky, camOverride);
            //opencode: upload ortho MVP for DrawScreenQuad — the custom vertex shader
            //   (with explicit layout locations) uses uniform mvp, not the RHI matrix stack.
            //   BeginTexture sets up ortho in the RHI stack but the vertex shader reads
            //   from mvp. Without this, vertex positions are in screen coords (0..W, 0..H)
            //   which are outside NDC [-1,1] and get clipped to nothing.
            {
                gX_Matrix ortho = gX_Matrix_Ortho(0, (gX_F32)maindata.renderWidth,
                    (gX_F32)maindata.renderHeight, 0, 0.0f, 1.0f);
                gX_I32 mvpLoc = gX_RHI_GetUniformLocation(sky->shader.id, "mvp");
                if (mvpLoc >= 0) {
                    gX_RHI_UniformMatrix(mvpLoc, 1, 1, &ortho.m0);
                }
            }
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
        // Debug overlay helpers
        sky_draw_helpers(sky);
    gX_Drawing_EndTexture();
}

//opencode: draw procedural sky to the current render target (no framebuffer management).
//   Call inside an active BeginTexture/EndTexture block. The sky shader fills the
//   full render area. Assumes the caller has cleared the buffer and depth state.
gX_I0 gX_RendererSky_DrawBackground(gX_RendererSky *sky, const gX_Camera3D *camOverride) {
    if (!sky->loadingstate.loaded) return;
    gX_Drawing_BeginShader(sky->shader);
        sky_upload_camera(sky, camOverride);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
    gX_Drawing_EndShader();
    sky_draw_helpers(sky);
}
