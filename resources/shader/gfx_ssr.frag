//opencode: screen-space reflections - Hi-Z (min-reduced) ray march with
//   crossing-only hits + level-0 bisection refinement, multi-bounce chain,
//   dynamic-cubemap env fallback, water exclusion, roughness fade with
//   mip-blurred sampling, screen-edge dissolve (v0.1.11).
#version 330
in vec2 fragTexCoord;

uniform sampler2D depthMap;      // hdrScene depth (origin/normal reconstruction)
uniform sampler2D colorMap;      // hdrScene color (hit sampling; mips for rough blur)
uniform sampler2D normalMap;      //opencode: opt-in interpolated model normals
//ai: shared hybrid rays may cross empty screen-space gaps before reaching visible geometry.
uniform int hybridGapTraversal = 0;
uniform int hasNormalMap = 0;
uniform float normalFallbackAngle = 0.0; //opencode: opt-in edge fallback in radians
uniform int normalLiftMode = 0; //opencode: opt-in ray-start lift experiment
uniform float normalLiftHybridAngle = 0.0174533; //opencode: hybrid lift threshold
uniform int normalAcceptMode = 0; //opencode: opt-in final phantom acceptance
uniform int selfPlaneRejectEnabled = 1; //opencode: SSR edge-ray self-plane A/B
uniform sampler2D hizMap;        // min-reduced Hi-Z chain (march + refinement)
uniform sampler2D skyLutMap;     // equirect sky fallback LUT
uniform sampler2D fogLutMap;     //opencode: v0.3.8 - equirect fog delta LUT
uniform int hasFogLut = 0;       //opencode: v0.3.8
uniform sampler2D planarMap;     //opencode: v0.3.4 auxview - mirrored-camera RT
uniform int   hasPlanar = 0;      //opencode: v0.3.4 auxview enable
uniform sampler2D planarDepthMap; // audit - mirror depth validity mask
uniform int   hasPlanarDepth = 0; // audit - 1 when the depth mask is bound
uniform float planeDist = 0.075;  //opencode: v0.3.6 planar precision
uniform vec3  planeNormal = vec3(0.0, 1.0, 0.0);  //opencode: v0.3.6
uniform mat4  planarVP;          //opencode: v0.3.4 auxview - mirror-cam view-proj
uniform samplerCube envMap;      //opencode: dynamic reflection cubemap (v0.1.11)
uniform sampler2D velocityMap;   //opencode: M1-b velocity.w glass flag (unit 21)
uniform int hasVelocityMap = 0;  //opencode: M1-b 1 when the velocity buffer is bound
uniform int ssrGlassOn = 1;      //opencode: M1-b gate (GX_T_SSRGLASS=0 restores marching)
uniform mat4 vp;
uniform mat4 invVP;
uniform vec3 cameraPos;

//opencode: v0.3.8 - view-axis depth metric (was radial camera distance).
//   Radial distance is NON-MONOTONIC along rays that approach the camera:
//   a top-down mirror reflects rays straight up the view axis, their radial
//   distance dips to a minimum at closest approach then re-grows, and the
//   sign-flip crossing test fired at that artificial turning point - the
//   concentric-rectangle ghost rings on the slab. View-axis depth
//   (projection onto the camera forward axis) is monotonic along every
//   ray, making the crossing test unambiguous. camFwd is derived from
//   invVP by unprojecting two NDC points on the view axis.
vec3 camFwd = vec3(0.0, 0.0, -1.0);
float viewZ(vec3 p) { return dot(p - cameraPos, camFwd); }
uniform float near = 0.1;
uniform float far = 1000.0;
uniform float roughness = 0.0;
uniform int maxSteps = 64;
uniform float stepSize = 0.5;
uniform float maxDistance = 100.0;
uniform float depthBias = 0.25;
//opencode: v0.3.8 - silhouette-jump rejection. A crossing whose depth jumps
//   more than maxDepthDelta in one step is not a surface intersection but a
//   VISIBILITY discontinuity (the ray's projection slid behind a nearer
//   object's screen silhouette while the ray itself passes beside it - the
//   concentric ghost rings on the top-down mirror). Real crossings on
//   continuous surfaces move the depth by at most ~a step; jumps of whole
//   object extents are rejected and the march continues to the true target.
uniform float maxDepthDelta = 0.6;
uniform float maxDepthDistanceScale = 0.5; //opencode: opt-in range-scaling A/B
uniform float worldGapThreshold = 0.5;
uniform float finalDepthThreshold = -0.02;
uniform float hitFacingThreshold = -1.0;
uniform float opacity = 0.8;
uniform int debugMode = 0;
uniform int ssrPlanarDefer = 0;  //opencode: hunt - 1 on scenes with a
//   forward-planar mirror: confident march hits on plane pixels defer
//   (transparent) so the exact mirror shows through instead of being
//   overdrawn by march approximations. Aux-routed pixels keep content.
uniform float maxLod = 4.0;  //opencode: max Hi-Z mip LOD (0=no Hi-Z, higher=faster far rays)
uniform vec3 sunDir = vec3(0.0, 0.7, -0.7);   //opencode: analytic sun disc (LUT can't resolve it)
uniform float sunSize = 0.5;                  //opencode: sun angular radius in degrees
uniform vec4 sunColor = vec4(1.0, 0.95, 0.85, 1.0); //opencode: vec4 - generic SetValues uploads gX_Color
uniform float waterY = -0.6;                  //opencode: water plane height (SSR origin)
uniform int hasWater = 0;
//opencode: v0.1.15 - water SSR. The water surface normal is computed
//   analytically (Gerstner + ripple, replicating water.vert) - the depth
//   buffer's per-pixel derivative is per-mesh-triangle (128x128 grid = ~5-10px
//   facets with alternating diagonals), which shattered the reflections.
uniform int   waterWaveCount = 0;
uniform vec2  waterWaveDirs[6];
uniform float waterWaveAmps[6];
uniform float waterWaveFreqs[6];
uniform float waterWaveSpeeds[6];
uniform float waterWaveSteeps[6];
uniform int   waterRippleCount = 0;
uniform vec2  waterRipplePos[16];
uniform float waterRippleAmp[16];
uniform float waterRippleRadius[16];
uniform float waterRipplePhase[16];
uniform float waterTime = 0.0;
//opencode: v0.1.11 - cubemap env fallback + edge dissolve + multi-bounce
uniform int hasEnvMap = 0;                    //opencode: dynamic cubemap available (behind-camera content)
uniform float edgeFade = 0.08;                //opencode: screen-edge dissolve band (fraction of screen)
uniform int maxBounces = 1;                   //opencode: extra ray bounces after the first hit
uniform float bounceAtten = 0.4;              //opencode: per-bounce energy attenuation

out vec4 finalColor;

const float PI = 3.14159265359;

vec3 worldFromDepth(vec2 uv, float rawD) {
    vec4 clip = vec4(uv * 2.0 - 1.0, rawD * 2.0 - 1.0, 1.0);
    vec4 w = invVP * clip;
    return w.xyz / w.w;
}

//ai: interpolate planar depth only; nearest Hi-Z otherwise quantizes sloped intersections by a whole texel.
float hybridDepthAt(vec2 uv) {
    ivec2 size = textureSize(hizMap,0);
    vec2 pixel = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    if (any(lessThan(base,ivec2(1))) || any(greaterThanEqual(base + ivec2(1),size)))
        return textureLod(hizMap,uv,0.0).r;
    float a = texelFetch(hizMap,base,0).r;
    float b = texelFetch(hizMap,base + ivec2(1,0),0).r;
    float c = texelFetch(hizMap,base + ivec2(0,1),0).r;
    float d = texelFetch(hizMap,base + ivec2(1,1),0).r;
    float left = texelFetch(hizMap,base - ivec2(1,0),0).r;
    float down = texelFetch(hizMap,base - ivec2(0,1),0).r;
    if (max(max(a,b),max(c,d)) >= 0.999 || max(left,down) >= 0.999
        || max(max(abs(a + d - b - c),abs(left - 2.0 * a + b)),abs(down - 2.0 * a + c)) > 2.0e-7)
        return textureLod(hizMap,uv,0.0).r;
    vec2 fraction = fract(pixel);
    return mix(mix(a,b,fraction.x),mix(c,d,fraction.x),fraction.y);
}

//opencode: project a world point to UV; false when behind the camera (mirrored
//   NDC coordinates would otherwise produce ghost samples at screen edges)
bool worldToUV(vec3 wpos, out vec2 uv) {
    vec4 clip = vp * vec4(wpos, 1.0);
    if (clip.w < 1e-5) return false;
    vec3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * 0.5 + 0.5;
    return true;
}

//opencode: sky fallback - equirect LUT sample + analytic sun disc.
//   LUT convention (mirrors sky_new.frag skyEquirect mode):
//   v = 0 at BOTTOM = straight up (pitch = v*pi from +Y), u = yaw/(2pi) from +X
//   (the +0.5 yaw offset would rotate the reflections 180? - removed 2026-08-18)
vec3 skyColorAt(vec3 dir) {
    vec2 lutUv = vec2(fract(atan(dir.z, dir.x) / (2.0 * PI)),
                      acos(clamp(dir.y, -1.0, 1.0)) / PI);
    vec3 col = texture(skyLutMap, lutUv).rgb;
    vec3 sunN = normalize(sunDir);
    float sunDot = dot(dir, sunN);
    float discCos = cos(sunSize * PI / 180.0);
    col += sunColor.rgb * smoothstep(discCos - 0.02, discCos, sunDot);
    return col;
}

//opencode: environment fallback (v0.1.11) - the dynamic reflection cubemap
//   when available (captures geometry BEHIND the camera, which the screen-space
//   buffers never contain), else the equirect sky LUT. The cubemap faces are
//   cleared to flat sky color (no sun), so the analytic sun disc is added on
//   the cubemap path ONLY: skyColorAt already carries the disc, and the old
//   unconditional add painted it 2x with no env map.
//opencode: env roughness - cubemap sampled at the same roughness LOD as
//   geometry hits (pxRough*3.0; faces carry generated mips like the scene
//   color mips), so rough misses blur consistently with rough hits instead
//   of staying mirror-sharp while geometry smears.
vec3 envColorAt(vec3 dir, float rough) {
    if (hasEnvMap == 1) {
        vec3 col = textureLod(envMap, dir, rough * 3.0).rgb;
        vec3 sunN = normalize(sunDir);
        float discCos = cos(sunSize * PI / 180.0);
        col += sunColor.rgb * smoothstep(discCos - 0.02, discCos, dot(dir, sunN));
        return col;
    }
    return skyColorAt(dir);
}

//opencode: water SSR height (v0.1.15) - analytic Gerstner + ripple surface
//   height. The depth buffer's per-triangle linear interpolation
//   sawtooths across the 128-segment mesh (a few px per segment) - the
//   reconstructed ray-start Y followed it, banding the reflections at the
//   segment spacing. The analytic height gives the exact surface.
float waterHeightAt(vec2 pos) {
    float h = 0.0;
    for (int i = 0; i < waterWaveCount && i < 6; i++) {
        float theta = waterWaveFreqs[i] * dot(waterWaveDirs[i], pos) + waterTime * waterWaveSpeeds[i];
        h += waterWaveAmps[i] * sin(theta);
    }
    for (int i = 0; i < waterRippleCount && i < 16; i++) {
        vec2 delta = pos - waterRipplePos[i];
        float dist = length(delta);
        if (dist < waterRippleRadius[i]) {
            float r = dist / max(waterRippleRadius[i], 0.001);
            float envelope = 1.0 - r * r;
            float wave = sin(dist * 3.0 - waterRipplePhase[i]);
            h += waterRippleAmp[i] * wave * envelope;
        }
    }
    return h;
}

//opencode: water SSR normal (v0.1.15) - analytic Gerstner + ripple normal
//   replicating water.vert's tangent/bitangent accumulation (world-space,
//   plane unrotated: matModel = translate(0, planeHeight, 0)).
vec3 waterNormalAt(vec3 wpos) {
    vec2 pos = wpos.xz;
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 bitangent = vec3(0.0, 0.0, 1.0);
    for (int i = 0; i < waterWaveCount && i < 6; i++) {
        vec2 dir = waterWaveDirs[i];
        float amp = waterWaveAmps[i];
        float freq = waterWaveFreqs[i];
        float speed = waterWaveSpeeds[i];
        float steep = waterWaveSteeps[i];
        float theta = freq * dot(dir, pos) + waterTime * speed;
        float c = cos(theta);
        float s = sin(theta);
        float Qi = max(steep / max(freq * amp * float(max(waterWaveCount, 1)), 0.001), 0.0);
        Qi = min(Qi, 0.85);
        float WA = freq * amp;
        tangent.x -= Qi * dir.x * dir.x * WA * s;
        tangent.y += dir.x * WA * c;
        tangent.z -= Qi * dir.x * dir.y * WA * s;
        bitangent.x -= Qi * dir.x * dir.y * WA * s;
        bitangent.y += dir.y * WA * c;
        bitangent.z -= Qi * dir.y * dir.y * WA * s;
    }
    for (int i = 0; i < waterRippleCount && i < 16; i++) {
        vec2 delta = pos - waterRipplePos[i];
        float dist = length(delta);
        if (dist < waterRippleRadius[i]) {
            float r = dist / max(waterRippleRadius[i], 0.001);
            float envelope = 1.0 - r * r;
            float wave = sin(dist * 3.0 - waterRipplePhase[i]);
            float dr = 1.0 / max(waterRippleRadius[i], 0.001);
            float dEnvelope = -2.0 * r * dr;
            float dWave = cos(dist * 3.0 - waterRipplePhase[i]) * 3.0;
            float dRipple_dDist = waterRippleAmp[i] * (dWave * envelope + wave * dEnvelope);
            vec2 rippleDir = delta / max(dist, 0.001);
            tangent.y += dRipple_dDist * rippleDir.x;
            bitangent.y += dRipple_dDist * rippleDir.y;
        }
    }
    return normalize(cross(tangent, bitangent));
}

//opencode: per-step screen-edge margin (v0.1.11) - fades INSIDE the screen
//   (edgeFade fraction from the boundary), so reflections dissolve before the
//   frustum edge instead of popping at a 2%-band hard cut.
float edgeMargin(vec2 u) {
    vec2 d = min(u, 1.0 - u);
    return smoothstep(0.0, edgeFade, min(d.x, d.y));
}

//opencode: v0.1.12 - sub-step exit interpolation. Env terminations (frustum
//   exit, margin band) used to record step-quantized trav, which quantized
//   the env fades into visible horizontal bands on the glossy floor. Bisects
//   the exit boundary between the last valid step and the terminating step
//   (deterministic, no jitter).
float exitT(vec3 rayStart, vec3 reflectDir, float tLo, float tHi) {
    for (int k = 0; k < 4; k++) {
        float tm = (tLo + tHi) * 0.5;
        vec3 pm = rayStart + reflectDir * tm;
        vec2 um;
        bool ok = worldToUV(pm, um);
        if (ok && um.x >= -0.5 && um.x <= 1.5 && um.y >= -0.5 && um.y <= 1.5 && edgeMargin(um) >= 0.005) {
            tLo = tm;
        } else {
            tHi = tm;
        }
    }
    return tHi;
}

//opencode: march result - one ray's termination state (v0.1.11 refactor)
struct MarchResult {
    vec3 hitPos;        // world hit position (geometry hits)
    vec2 hitUV;         // hit UV (geometry hits)
    float trav;         // traversal distance at termination
    int hitType;        // 0 = exited/overran, 1 = geometry, 3 = in-screen sky
    float depthDelta;   // |diff - prevDiff| at the crossing (debug view 4)
    //ai: native candidate residuals distinguish refinement errors from true projection misses.
    vec3 candidateResidual;
    float minMargin;    // min screen-edge margin over the whole march
    int hitStep;        // march step index at termination
    int steps;          // capped step count (reach = steps * stepSz)
    float prevDiff;     // last diff (debug view 5)
    int state;          // derived: prevDiff > 0 (debug view 5)
    //opencode: v0.1.26 - contact-zone diagnostics (debug view 11)
    int confirmRejects; // LOD-0 confirmation rejections
    int phantomRejects; // phantom-crossing validation rejections
    int crossCount;     // sign-flip crossings encountered
    float lastCrossLod; // Hi-Z lod at the last crossing
    //opencode: item-15 - front-to-behind silhouette grazes (the march only
    //   counts behind-to-front as hits; a ray sailing PAST geometry flips
    //   + to - at its silhouette). The detached-ghost band marches sail
    //   clean (no flips either way); disocclusion cases (probe cube) flip.
    int grazeCount;
    //opencode: item-15 - closest-approach point (world) of the march while
    //   in front of geometry. The aux miss-fallback samples the mirror
    //   photo THERE (projected directly) instead of at the origin's mirror
    //   (which answers "what M sees through F" = detached M-shadow ghosts
    //   over true background). Init = rayStart: unarmed marches keep
    //   origin behavior (on-plane origins are mirror-fixed points, so
    //   identical by construction).
    vec3 nearPos;
    float stepUsed;       //opencode: actual cadence used by this march
};

//opencode: forward declaration for hit validation inside marchRay.
vec3 hitNormalAt(vec2 hv);

//opencode: single ray march - reused per bounce. Deterministic (t = 0, no
//   jitter): same phase for neighbouring pixels -> coherent reflection blobs.
//   Crossing-only hit detection with LOD-0 confirmation + linear-root seeded
//   bisection (v0.1.11): coarse-mip sign changes must persist at exact depth,
//   else the crossing was mip aliasing (blocky edge bands - neighbours cross
//   at different march steps).
MarchResult marchRay(vec3 rayStart, vec3 reflectDir, float stepSz, int maxStep,
                     float maxT, float maxLodVal, vec2 selfUv, float dOrigSurface) {
    MarchResult r;
    r.hitPos = vec3(0.0);
    r.hitUV = vec2(0.0);
    r.trav = 0.0;
    r.hitType = 0;
    r.depthDelta = 0.0;
    r.candidateResidual = vec3(0.0);
    r.minMargin = 1.0;
    r.hitStep = 0;
    r.steps = 0;
    r.prevDiff = 0.0;
    r.state = 0;
    r.confirmRejects = 0;
    r.phantomRejects = 0;
    r.crossCount = 0;
    r.lastCrossLod = 0.0;
    r.grazeCount = 0;
    r.nearPos = rayStart;
    r.stepUsed = stepSz;

    float t = 0.0;
    float prevDiff = 0.0;
    float prevRaw = 0.0;
    float lastStep = stepSz;
    //opencode: item-15 argmin state - best front-side approach AFTER the ray
    //   leaves home. V2 (UV-distance arming): identical output to origin
    //   mapping (0.0954) - the rays exit before arming usefully, and worse,
    //   viewZ-|diff| fundamentally cannot see LATERALLY-near geometry (the
    //   ray never approaches the ball in viewZ; the graze shares small
    //   |diff| with the hug). Kept for the nearPos plumbing below; the
    //   actual gate is crossing-based (see auxMiss). Unarmed rays keep
    //   nearPos=rayStart = today's origin behavior.
    float bestAbs = 1e30;
    bool trackArmed = false;
    int steps = min(maxStep, 128);
    r.steps = steps;

    for (int i = 0; i < steps && t < maxT; i++) {
        vec3 rayPos = rayStart + reflectDir * t;
        //opencode: v0.3.8 - camera-plane escape. A ray that crosses the view
        //   plane can never produce a screen-space hit: its projection
        //   explodes (w -> 0) and the wrapped UVs swing edgeMargin to 0,
        //   painting the concentric dark bands around the drift's vanishing
        //   point on grazing mirror views. Everything sampled past this
        //   point is wrap garbage; terminate as a clean escape.
        if (viewZ(rayPos) < 0.0) { r.trav = t; return r; }
        vec2 rayUv;
        if (!worldToUV(rayPos, rayUv)) { r.trav = exitT(rayStart, reflectDir, t - lastStep, t); return r; }
        if (rayUv.x < -0.5 || rayUv.x > 1.5 || rayUv.y < -0.5 || rayUv.y > 1.5) { r.trav = exitT(rayStart, reflectDir, t - lastStep, t); return r; }
        float margin = edgeMargin(rayUv);
        if (margin < 0.005) { r.trav = exitT(rayStart, reflectDir, t - lastStep, t); return r; }
        //opencode: minMargin excludes micro-step arrivals - the
        //   silhouette shrink below oversamples the path near depth
        //   discontinuities; folding those extra taps into the min
        //   poisoned marginTerm (proven: identical hits rendered 32%
        //   dimmer, yaw30 24.08->19.92). Full-stride sampling keeps the
        //   audited v0.1.11/v0.3.8 dissolve profile.
        if (lastStep >= stepSz * 0.5) r.minMargin = min(r.minMargin, margin);

        //opencode: Hi-Z LOD selection - farther rays use coarser mip levels.
        //   hizMap is MIN-reduced: a coarse texel never reads farther than the
        //   nearest surface in its footprint (no thin-object tunneling).
        float lod = min(t * 0.03, maxLodVal); //opencode: LOD slope 0.05 -> 0.03 - later coarse-mip entry, less blockiness
        float sceneRaw = textureLod(hizMap, rayUv, lod).r;
        //opencode: sky hit - in-screen sky (hiz reads nothing closer)
        if (sceneRaw > 0.999) {
            //ai: empty depth is not an intersection; bounded hybrid rays keep searching without inventing hits.
            if (hybridGapTraversal == 1) {
                t += stepSz;
                prevDiff = -1.0;
                prevRaw = sceneRaw;
                lastStep = stepSz;
                continue;
            }
            r.hitType = 3;
            //opencode: v0.1.12 - sub-step sky crossing (prevRaw/raw
            //   interpolation) - the old step-quantized trav banded the
            //   sky fade on the glossy floor
            r.trav = (t - lastStep) + lastStep * clamp((0.999 - prevRaw) / max(sceneRaw - prevRaw, 1e-6), 0.0, 1.0);
            r.hitStep = i;
            r.hitPos = rayPos;
            r.hitUV = rayUv;
            r.prevDiff = prevDiff;
            r.state = (prevDiff > 0.0) ? 1 : 0;
            return r;
        }

        vec3 sceneWorld = worldFromDepth(rayUv, sceneRaw);
        float sceneDist = viewZ(sceneWorld);
        float rayDist = viewZ(rayPos);
        float diff = rayDist - sceneDist;
        //opencode: item-15 - closest-approach tracking (see locals note).
        //   Written into r continuously so every early return carries it.
        //   UV-distance arming (not depth): the hug and the graze share
        //   small |diff| but the graze crosses the screen.
        if (!trackArmed && length(rayUv - selfUv) > 0.05) trackArmed = true;
        if (trackArmed && diff < 0.0 && -diff < bestAbs) {
            bestAbs = -diff;
            r.nearPos = rayPos;
        }

//opencode: v0.1.14 - Hi-Z jump-ahead. When the nearest footprint surface
//   is 8+ units away (abs(diff) > 8, either direction) past the near field
//   (t > 24), advance 4 units instead of 0.5 - crossing detection + LOD-0
//   confirmation + bisection handle the coarser bracket. MIN-reduced Hi-Z
//   bounds the risk in BOTH directions: the jump only fires when no surface
//   is within 8 units of the ray, so a 4-unit jump can't skip a surface the
//   ray would cross (diff > 8 = behind; diff < -8 = the surface is ?8 ahead).
        float step = stepSz;
        if (abs(diff) > 8.0 && t > 24.0) step = 4.0;
        //opencode: silhouette-proximity step shrink. Fixed 0.5-steps let
        //   crossings fall between samples where the trace nicks
        //   silhouette corners/edges (copy top-edge leak, diagonal
        //   streaks) or parallaxes fast over the footprint (bottom
        //   truncation): the first sky-projected sample then seals the
        //   escape (proven: every escape terminates at trav<=2.4 with no
        //   crossing ever encountered - joint hit/reject/cross viz shows
        //   all crossings hit, zero vanishings). Sample densely only
        //   where the depth signal has high frequency: MIN-mip2 (nearest
        //   in 4x4) disagreeing with the march tap means a nearer surface
        //   within ~2px, so quarter steps. Interior crossings are
        //   untouched (sign flips fire at any step size). Micro-step
        //   arrivals are excluded from minMargin above (extra taps must
        //   not deepen the audited edge dissolve). Global 0.25 was
        //   measured better (33.6 vs 30.1) but doubles the full-res march
        //   AND halves the i<12 self-swallow window's world span (contact
        //   ghost risk); sub-0.125 micro-steps go poisonous (budget burn +
        //   swallow-window + fade interactions - measured 21.5). Mip3 net
        //   adds nothing over mip2 (both 30.1).
        {
            float rawMin2 = textureLod(hizMap, rayUv, 2.0).r;
            if (rawMin2 < sceneRaw - 0.005 && diff < 0.0) step = min(step, stepSz * 0.25);
        }

        if (length(rayUv - selfUv) < 0.01) {
            t += step; prevDiff = diff; prevRaw = sceneRaw; lastStep = step; continue;
        }

        //opencode: item-15 - front-to-behind silhouette graze counter (see
        //   struct note). Away-from-self + object-scale jump only: hug noise
        //   is lift-scale near home.
        if (i > 0 && prevDiff > 0.0 && diff <= 0.0
            && length(rayUv - selfUv) > 0.02
            && abs(diff - prevDiff) > 0.5) {
            r.grazeCount++;
        }

        //opencode: crossing-only hit detection - the loose |diff| < depthBias
        //   test was removed: at grazing angles it fired while the ray still
        //   hugged the origin surface, smearing the surface's own color at the
        //   reflection contact (ghost reflections).
        //   v0.1.13: the old state machine (state 0/1 with a skip branch) is
        //   exactly equivalent to accepting only behind->front crossings
        //   (prevDiff < 0 && diff > 0): state==0 at a crossing ? prevDiff < 0
        //   (state is set by diff > 0 and reset only by a (+,-) crossing).
        if (i > 0 && prevDiff < 0.0 && diff > 0.0) {
            //ai: an empty-depth interval has no meaningful depth slope or discontinuity magnitude.
            bool gapCrossing = hybridGapTraversal == 1 && prevRaw > 0.999;
            //opencode: v0.3.3-audit - self-plane swallow (see lift-off note):
            //   crossed surface ~= origin surface depth while UV is still in
            //   the self footprint -> the ray just re-crossed its own plate.
            //   Restricted to the near-field (i<12) and a tight noise-scale
            //   band so genuine far-away reflections off the same material
            //   are untouched.
            if (selfPlaneRejectEnabled != 0
                       && i < 12 && abs(sceneDist - dOrigSurface) < 0.05
                       && length(rayUv - selfUv) < 0.02) {
                r.phantomRejects++;
                t = t + lastStep;
                prevDiff = diff; prevRaw = sceneRaw; lastStep = step;
                continue;
            }
            //opencode: v0.3.8 - silhouette-jump rejection (see maxDepthDelta):
            //   the depth step across this crossing reveals whether the ray
            //   met a continuous surface (small delta) or slid behind a
            //   silhouette (delta = the object's whole depth extent). Reject
            //   the latter BEFORE refinement and keep marching.
            //opencode: range-scaled threshold - a crossing at travel t
            //   legitimately spans ~t-sized depth steps at grazing angles
            //   (floor-to-cube jumps of several units); the absolute 0.6
            //   killed every TRUE grazing hit (proven: 0.0 score with true
            //   reads, 34.8 with the test off). Near-contact keeps near-0.6
            //   strictness (v0.1.23 contact ghosts live at small t); the
            //   post-refinement phantom validation backstops far ghosts.
            if (!gapCrossing && abs(diff - prevDiff) > maxDepthDelta * (1.0 + t * maxDepthDistanceScale)) {
                r.phantomRejects++;
                t = t + lastStep;
                prevDiff = diff; prevRaw = sceneRaw; lastStep = step;
                continue;
            }
            r.crossCount++;
            r.lastCrossLod = lod;
            float tPrev = t - lastStep;
            //opencode: LOD-0 confirmation (v0.1.11) - a coarse-mip crossing
            //   must persist at exact depth, else it was mip aliasing. Only
            //   needed when marching coarse mips (lod >= 1); near-exact
            //   marches keep the old behaviour.
            bool confirmed = true;
            if (lod >= 1.0) {
                vec2 uvPrev;
                if (worldToUV(rayStart + reflectDir * tPrev, uvPrev)) {
                    float smPrev = textureLod(hizMap, uvPrev, 0.0).r;
                    float smCur = textureLod(hizMap, rayUv, 0.0).r;
                    if (smPrev < 0.999 && smCur < 0.999) {
                        float sdPrev = viewZ(worldFromDepth(uvPrev, smPrev));
                        float sdCur = viewZ(worldFromDepth(rayUv, smCur));
                        float ddPrev = viewZ(rayStart + reflectDir * tPrev) - sdPrev;
                        float ddCur = viewZ(rayStart + reflectDir * t) - sdCur;
                        confirmed = ddPrev * ddCur < 0.0;
                    }
                }
            }
            if (!confirmed) { r.confirmRejects++; t += step; prevDiff = diff; prevRaw = sceneRaw; lastStep = step; continue; }

            r.hitStep = i;
            r.depthDelta = abs(diff - prevDiff);
            //opencode: linear-root seeded bisection at level 0 (exact depth) -
            //   the root estimate (prevDiff/diff ratio) tightens the bracket
            //   to ~half a step before the 4 iterations (v0.1.11)
            float rootFrac = clamp(prevDiff / (prevDiff - diff), 0.0, 1.0);
            //ai: tight hybrid coincidence needs the real sampled bracket and finer convergence.
            float tLo = hybridGapTraversal == 1 ? tPrev : max(tPrev, tPrev + lastStep * (rootFrac - 0.25));
            float tHi = hybridGapTraversal == 1 ? t : min(t, tPrev + lastStep * (rootFrac + 0.25));
            int refinementSteps = hybridGapTraversal == 1 ? 12 : 6;
            for (int k = 0; k < refinementSteps; k++) {
                float tm = (tLo + tHi) * 0.5;
                vec3 rm = rayStart + reflectDir * tm;
                vec2 um;
                if (worldToUV(rm, um)) {
                    float sm = hybridGapTraversal == 1 ? hybridDepthAt(um) : textureLod(hizMap, um, 0.0).r;
                    if (sm < 0.999) {
                        vec3 sw = worldFromDepth(um, sm);
                        float ddm = viewZ(rm) - viewZ(sw);
                        if (ddm > 0.0) tHi = tm; else tLo = tm;
                    } else if (hybridGapTraversal == 1) tLo = tm;
                    else tHi = tm;
                } else tHi = tm;
            }
            float hitT = tHi;

            //opencode: v0.1.23 - phantom-crossing validation. Noise dips in
            //   the reconstructed position fired sign-flips whose bisection
            //   bracket contained NO true surface: the refinement then
            //   converged to a bracket endpoint and the "hit" sampled scene
            //   content displaced from reality (the striped ghosts between
            //   objects and their reflections). At a REAL hit the ray has
            //   just crossed to behind the surface (ddFinal ? 0?); at a
            //   phantom it remains clearly IN FRONT (ddFinal ? 0) even at
            //   exact depth - reject those and keep marching.
            {
                bool phantom = false;
                vec3 hp = rayStart + reflectDir * hitT;
                vec2 hv2;
                if (worldToUV(hp, hv2)) {
                    float sm2 = hybridGapTraversal == 1 ? hybridDepthAt(hv2) : textureLod(hizMap, hv2, 0.0).r;
                    if (sm2 < 0.999) {
                        vec3 sw2 = worldFromDepth(hv2, sm2);
                        float ddFinal = viewZ(hp) - viewZ(sw2);
                        //ai: debug 32 retains world gap, signed depth residual, and candidate presence.
                        r.candidateResidual = vec3(length(hp - sw2),ddFinal,1.0);
                        //opencode: v0.3.8 - world-gap ghost test. Depth-space
                        //   crossing alone cannot distinguish a real surface
                        //   intersection (ray point ON the surface, world gap
                        //   ~0) from a projection ghost: rays rising toward a
                        //   top-down camera slide behind a near object's
                        //   screen silhouette while passing BESIDE it (the ray
                        //   point is units away from the surface in world
                        //   space - the concentric fade-band rings on the
                        //   top-down mirror). Reject hits whose ray point and
                        //   sampled surface point do not coincide.
                        //ai: empty-gap intersections need tight world coincidence, not the legacy silhouette tolerance.
                        float allowedGap = hybridGapTraversal == 1 ? min(worldGapThreshold,0.02) : worldGapThreshold;
                        if (length(hp - sw2) > allowedGap) phantom = true;
                        if (ddFinal < finalDepthThreshold) phantom = true;
                        if (hitFacingThreshold > -0.99) {
                            vec3 hitN = normalize(hitNormalAt(hv2));
                            if (dot(hitN, cameraPos - sw2) < 0.0) hitN = -hitN;
                            if (dot(hitN, normalize(-reflectDir)) < hitFacingThreshold)
                                phantom = true;
                        }
                    } else if (hybridGapTraversal == 1) phantom = true;
                } else if (hybridGapTraversal == 1) phantom = true;
                if (phantom && normalAcceptMode == 0) {
                    r.phantomRejects++;
                    t = t + lastStep;
                    prevDiff = diff; prevRaw = sceneRaw; lastStep = step;
                    continue;
                }
            }

            //ai: rejected candidates remain misses on later budget/viewport exits.
            r.hitType = 1;
            r.trav = hitT;
            r.hitPos = rayStart + reflectDir * hitT;
            vec2 hitUV;
            if (!worldToUV(r.hitPos, hitUV)) hitUV = rayUv;

            //opencode: v0.1.26 - surgical silhouette re-snap. Near object
            //   silhouettes the depth buffer carries a RAMP of interpolated
            //   edge texels between two real surfaces; a bisected hit whose
            //   UV lands on such a ramp texel samples displaced content and
            //   the contact-zone reflection "falls apart" into shards.
            //   After acceptance, probe the ?1-texel neighbourhood for a
            //   texel whose surface distance better matches the ray point
            //   and sample from there instead. Guards keep this strictly
            //   local: only fires on high depth-gradient (ramp) pixels,
            //   only ?1 texel, only when a candidate clearly improves the
            //   ray/surface agreement, sky texels skipped. Termination
            //   decisions (confirmation/bisection/validation) are untouched,
            //   so no global march behaviour changes.
            {
                vec2 texel1 = 1.0 / vec2(textureSize(hizMap, 0));
                float dC = textureLod(hizMap, hitUV, 0.0).r;
                float dL = textureLod(hizMap, hitUV - vec2(texel1.x, 0.0), 0.0).r;
                float dR = textureLod(hizMap, hitUV + vec2(texel1.x, 0.0), 0.0).r;
                float dU = textureLod(hizMap, hitUV - vec2(0.0, texel1.y), 0.0).r;
                float dD = textureLod(hizMap, hitUV + vec2(0.0, texel1.y), 0.0).r;
                float gradMag = max(max(abs(dR - dL), abs(dD - dU)), 1e-6);
                if (dC < 0.999 && gradMag > 0.002) { //opencode: ramp pixel?
                    float rayDist = viewZ(r.hitPos);
                    float errC = abs(rayDist - viewZ(worldFromDepth(hitUV, dC)));
                    vec2 bestUV = hitUV;
                    float bestErr = errC;
                    for (int sy = -1; sy <= 1; sy++) {
                        for (int sx = -1; sx <= 1; sx++) {
                            vec2 uv2 = clamp(hitUV + vec2(float(sx), float(sy)) * texel1, 0.001, 0.999);
                            float d2 = textureLod(hizMap, uv2, 0.0).r;
                            if (d2 > 0.999) continue;
                            float err2 = abs(rayDist - viewZ(worldFromDepth(uv2, d2)));
                            if (err2 < bestErr * 0.5) { bestErr = err2; bestUV = uv2; }
                        }
                    }
                    if (bestUV != hitUV) hitUV = bestUV; //opencode: resample from true surface
                }
            }

            r.hitUV = clamp(hitUV, 0.001, 0.999);
            r.prevDiff = prevDiff;
            r.state = 0;
            return r;
        }

        prevDiff = diff;
        prevRaw = sceneRaw;
        lastStep = step;
        t += step;
    }

    r.trav = t;
    r.prevDiff = prevDiff;
    r.state = (prevDiff > 0.0) ? 1 : 0;
    return r;
}

//opencode: v0.1.26 - nearest-surface distance over a 3x3 exact-depth
//   footprint. Single-tap tests near object silhouettes evaluate the DEPTH
//   RAMP (interpolated edge texels between floor and a tall object): those
//   depths are not real surfaces, so short grazing crossings there fired at
//   ramp positions and the accepted hitUVs sampled displaced content - the
//   reflection "fell apart" into vertical shards in the contact zone. The
//   min-reduced footprint snaps every bracket evaluation to the TRUE nearest
//   surface (the object), matching the Hi-Z chain's own conservative min
//   semantics. Sky texels are ignored (1e9).
float sceneDistMin(vec2 uv) {
    float best = 1e9;
    vec2 texel = 1.0 / vec2(textureSize(hizMap, 0));
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            float d = textureLod(hizMap, uv + vec2(float(dx), float(dy)) * texel, 0.0).r;
            if (d < 0.999) {
                best = min(best, viewZ(worldFromDepth(uv + vec2(float(dx), float(dy)) * texel, d)));
            }
        }
    }
    return best;
}

//opencode: surface normal at a hit UV - silhouette-aware central depth
//   interpolation. Equal-side differences smooth planar gradients; missing
//   neighbors fall back to the valid side and depth-disparate sides are
//   downweighted so silhouettes do not turn ramps into faceted directions.
vec3 hitNormalAt(vec2 hv) {
    vec2 texel = 2.0 / vec2(textureSize(depthMap, 0));
    float d0 = textureLod(depthMap, hv, 0.0).r;
    if (d0 > 0.999) return vec3(0.0, 1.0, 0.0);
    vec3 w0 = worldFromDepth(hv, d0);
    float dL = textureLod(depthMap, hv - vec2(texel.x, 0.0), 0.0).r;
    float dR = textureLod(depthMap, hv + vec2(texel.x, 0.0), 0.0).r;
    float dD = textureLod(depthMap, hv - vec2(0.0, texel.y), 0.0).r;
    float dU = textureLod(depthMap, hv + vec2(0.0, texel.y), 0.0).r;
    bool leftValid = dL < 0.999;
    bool rightValid = dR < 0.999;
    bool downValid = dD < 0.999;
    bool upValid = dU < 0.999;
    vec3 dWorldX;
    vec3 dWorldY;
    if (!leftValid && rightValid) {
        dWorldX = worldFromDepth(hv + vec2(texel.x, 0.0), dR) - w0;
    } else if (leftValid && !rightValid) {
        dWorldX = w0 - worldFromDepth(hv - vec2(texel.x, 0.0), dL);
    } else if (leftValid && rightValid) {
        vec3 wL = worldFromDepth(hv - vec2(texel.x, 0.0), dL);
        vec3 wR = worldFromDepth(hv + vec2(texel.x, 0.0), dR);
        float leftWeight = 1.0 / max(abs(d0 - dL), 1.0e-5);
        float rightWeight = 1.0 / max(abs(dR - d0), 1.0e-5);
        dWorldX = ((w0 - wL) * leftWeight + (wR - w0) * rightWeight)
            / (leftWeight + rightWeight);
    } else {
        dWorldX = vec3(1.0, 0.0, 0.0);
    }
    if (!downValid && upValid) {
        dWorldY = worldFromDepth(hv + vec2(0.0, texel.y), dU) - w0;
    } else if (downValid && !upValid) {
        dWorldY = w0 - worldFromDepth(hv - vec2(0.0, texel.y), dD);
    } else if (downValid && upValid) {
        vec3 wD = worldFromDepth(hv - vec2(0.0, texel.y), dD);
        vec3 wU = worldFromDepth(hv + vec2(0.0, texel.y), dU);
        float downWeight = 1.0 / max(abs(d0 - dD), 1.0e-5);
        float upWeight = 1.0 / max(abs(dU - d0), 1.0e-5);
        dWorldY = ((w0 - wD) * downWeight + (wU - w0) * upWeight)
            / (downWeight + upWeight);
    } else {
        dWorldY = vec3(0.0, 1.0, 0.0);
    }
    vec3 n = cross(dWorldY, dWorldX);
    float nl = length(n);
    if (nl < 1e-8) return vec3(0.0, 1.0, 0.0);
    return n / nl;
}

//opencode: v0.3.6 planar precision - exact mirror-RT sample coordinate
//   for a world point: reflect it across the mirror plane (dot(n,x)=d)
//   and project through the mirrored-camera VP. For on-plane origins the
//   reflection is the point itself, so the mapping is analytic-exact.
vec2 planarSampleUV(vec3 wp) {
    float dn = dot(wp, planeNormal) - planeDist;
    vec3 wm = wp - planeNormal * (2.0 * dn);
    vec4 pc = planarVP * vec4(wm, 1.0);
    if (pc.w <= 0.001) return vec2(-1.0);
    vec2 ndcA = pc.xy / pc.w;
    //opencode: NO flip (the old y-flip compensated the pre-fix upside-down
    //   RT: unflipped winding kept back faces, so the RT held upside-down
    //   shading and the flip un-flipped it. With the winding fix the RT is
    //   a true mirror view and the flip re-inverts it - apex-up duplicate
    //   beside SSR truth. FBO row 0 = NDC bottom = v0: identity mapping.
    //   Matches the verified direct path (standard.frag projective).
    return vec2((ndcA.x + 1.0) * 0.5, (ndcA.y + 1.0) * 0.5);
    }


void main() {
    //opencode: v0.3.8 - derive the world-space view axis from invVP
    {
        vec4 fa4 = invVP * vec4(0.0, 0.0, -1.0, 1.0);
        vec4 fb4 = invVP * vec4(0.0, 0.0,  1.0, 1.0);
        vec3 fwdA = fa4.xyz / max(fa4.w, 1e-6);
        vec3 fwdB = fb4.xyz / max(fb4.w, 1e-6);
        camFwd = normalize(fwdB - fwdA);
    }
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: screen UV - the quad delivers Y-down UVs (fragY=0 at screen
    //   top: passthrough vertex UVs under the y-down ortho) while the
    //   depth/color RTs are Y-up (GL row 0 = screen bottom): flip once so
    //   reads address true rows. Clip-derived taps (worldToUV) are already
    //   y-up and pass through - including the hizMap taps (that chain is
    //   built per-pixel from true depths, so its content is y-up-addressed
    //   like the depth texture it reduces).
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float rawDepth = texture(depthMap, uv).r;
    if (rawDepth > 0.999) {
        finalColor = debugMode == 15 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0);
        return;
    }

    vec3 worldPos = worldFromDepth(uv, rawDepth);

    //opencode: water SSR (v0.1.15) - the water surface is displaced by Gerstner
    //   waves + ripples, so its depth lands in a band around waterY. The old
    //   exclusion (abs(y-waterY) < 0.1) cut wave crests/troughs - every pixel
    //   outside the band got processed while inside-band pixels were skipped,
    //   producing horizontal stripes. The band must ALSO absorb the depth
    //   buffer's per-triangle interpolation error on the far water (the
    //   reconstructed Y sawtooths across mesh segments - a tight bound flips
    //   the test at every segment boundary -> pulse lines every ~7px). The
    //   mask follows the actual 40x40 water footprint and surface band; the
    //   old one-sided 2.2 bound also classified lake banks and trees as water.
    bool isWater = (hasWater == 1
        && abs(worldPos.y - waterY) < 0.8
        && abs(worldPos.x) <= 20.5
        && abs(worldPos.z) <= 20.5);

    //opencode: robust normal - analytic screen-space derivatives of the
    //   reconstructed world position. The old 1px neighbour cross-product
    //   (cross(pR-pL, pD-pU)) collapsed at limbs/silhouettes where both screen
    //   tangents run parallel (?50% of the scene in the showcase), and the
    //   viewDir fallback reversed the reflect ray into the camera -> self-hits
    //   on every surface (smear + black region).
    //opencode: the reject test is RELATIVE - an absolute |cross| threshold
    //   (was < 0.001) killed every surface here: at the showcase's scale the
    //   per-pixel world tangents are ~0.01, so |cross| ? 1e-4 - well under the
    //   threshold even for perfectly flat geometry. The relative test only
    //   catches true degeneracy (parallel tangents at silhouettes, zero depth
    //   gradient).
    vec3 dFdy_w = dFdy(worldPos);
    vec3 dFdx_w = dFdx(worldPos);
    vec3 normal = cross(dFdy_w, dFdx_w);
    float nLen = length(normal);
    if (nLen < 1e-8 || nLen < 0.001 * (length(dFdx_w) * length(dFdy_w))) {
        //opencode: degenerate derivative pixels are still valid for SSR. The
        //   non-water path replaces this provisional normal with hitNormalAt()
        //   below, and water replaces it with its analytic wave normal.
        normal = vec3(0.0, 1.0, 0.0);
    } else {
        normal /= nLen;
    }
    //opencode: v0.1.22 - smooth primary normal from DEPTH-BUFFER reconstruction
    //   instead of the raw dFdx/dFdy derivative. Screen-space derivatives are
    //   constant per 2?2 quad: the reflected ray direction staircase-quantizes
    //   every two pixels, and at grazing angles + distance the quantization
    //   amplifies into alternating hit/miss columns (vertical striping between
    //   objects and their reflections). The 4-neighbour depth reconstruction
    //   averages across quad boundaries.

    //opencode: v0.1.15 - water pixels use the analytic wave normal instead of
    //   the per-triangle derivative (the mesh facet normal shatters the
    //   reflections on the 128x128 grid). dFdx/dFdy are still computed above -
    //   the flat water's derivative is degenerate-ish but the relative reject
    //   passes; only the slope direction is replaced.
    //opencode: the ray START is also re-derived: the depth buffer's per-triangle
    //   interpolation sawtooths the reconstructed Y at the mesh segment spacing
    //   (banding at ~4-6 rows) - the analytic surface height replaces it.
    vec3 depthReconstructedNormal = vec3(0.0, 1.0, 0.0);
    vec3 rasterNormal = vec3(0.0, 1.0, 0.0);
    float rasterNormalCoverage = 0.0;
    if (isWater) {
        normal = waterNormalAt(worldPos);
        worldPos.y = waterY + waterHeightAt(worldPos.xz);
    } else {
        //opencode: v0.1.22 - smooth primary normal from depth-buffer
        //   reconstruction (4-neighbour). dFdx/dFdy normals are constant per
        //   2?2 quad: the reflected ray direction staircase-quantizes every
        //   two pixels and grazing reflections amplify that into alternating
        //   hit/miss columns (vertical striping between objects and their
        //   mirror images).
        vec3 sn = hitNormalAt(uv);
        depthReconstructedNormal = sn;
        rasterNormal = sn;
        //opencode: prefer the rasterized shading normal when the opt-in buffer
        //   has coverage; depth reconstruction remains the fallback for
        //   terrain and any other non-model scene geometry.
        if (hasNormalMap == 1) {
            vec4 packedNormal = texture(normalMap, uv);
            if (packedNormal.a > 0.5) {
                vec3 bufferedNormal = packedNormal.rgb * 2.0 - 1.0;
                if (length(bufferedNormal) > 0.5) {
                    rasterNormal = normalize(bufferedNormal);
                    rasterNormalCoverage = 1.0;
                    sn = rasterNormal;
                }
            }
        }
        if (dot(sn, worldPos - cameraPos) > 0.0) {
            //opencode: compare both candidates after the same camera-facing
            //   hemisphere correction used by the active SSR normal.
            sn = -sn;
            depthReconstructedNormal = -depthReconstructedNormal;
            rasterNormal = -rasterNormal;
        }
        //opencode: the comparison itself must orient each candidate
        //   independently; the active candidate can otherwise hide an
        //   opposite-facing depth normal behind its own hemisphere flip.
        if (rasterNormalCoverage > 0.5) {
            if (dot(depthReconstructedNormal, worldPos - cameraPos) > 0.0)
                depthReconstructedNormal = -depthReconstructedNormal;
            if (dot(rasterNormal, worldPos - cameraPos) > 0.0)
                rasterNormal = -rasterNormal;
            if (normalFallbackAngle > 0.0
                && acos(clamp(dot(depthReconstructedNormal, rasterNormal), -1.0, 1.0))
                    > normalFallbackAngle)
                sn = depthReconstructedNormal;
        }
        normal = sn;
    }

    vec3 viewDir = normalize(worldPos - cameraPos);

    vec3 reflectDir = normalize(reflect(viewDir, normal));

    if (debugMode == 28) {
        //opencode: normal-disagreement heatmap — green is aligned, red is a
        //   larger angular difference, blue marks water/unavailable coverage.
        if (isWater || rasterNormalCoverage < 0.5) {
            finalColor = vec4(0.0, 0.0, 1.0, 1.0);
        } else {
            float angle = acos(clamp(dot(depthReconstructedNormal,
                                         rasterNormal), -1.0, 1.0)) / 3.14159265;
            float heat = smoothstep(0.0, 0.5, angle);
            finalColor = vec4(heat, 1.0 - heat, angle, 1.0);
        }
        return;
    }
    if (debugMode == 2) {
        //opencode: SSR ray-direction probe — encode the active primary
        //   reflection ray so normals-off/on captures can be compared directly.
        finalColor = vec4(reflectDir * 0.5 + 0.5, 1.0);
        return;
    }

    //opencode: v0.3.3-audit - grazing-angle ghost doubling fix. Two changes:
    //   1) Ray lift-off gains a NORMAL-aligned component: along reflectDir the
    //      separation is nearly tangential at grazing angles, so the noisy
    //      reconstructed start sits effectively ON the surface and noise dips
    //      re-cross it. A minimum perpendicular lift puts the march clearly
    //      off-plane.
    //   2) Crossings are rejected when the crossed surface's view distance is
    //      within a thin band of the ORIGIN surface and the UV is still inside
    //      an enlarged self footprint - that pattern is the ray dipping behind
    //      its own plate and re-emerging (the duplicated offset image), not a
    //      genuine reflection target.
    //opencode: halve the cadence only at grazing angles, where march-step
    //   quantization is visible as faceting; preserve the tuned cadence for
    //   face-on surfaces and the coarser water path.
    float grazing = 1.0 - abs(dot(viewDir, normal));
    float grazingBlend = smoothstep(0.25, 0.80, grazing);
    float adaptiveStep = mix(stepSize, stepSize * 0.5, grazingBlend);
    //ai: water keeps a coarser cadence than opaque surfaces, but 1.0 u skipped
    //   thin props (the axis arrows) entirely; 0.5 keeps them visible.
    float traceStep = isWater ? max(stepSize, 0.5) : adaptiveStep;
    int traceMaxSteps = isWater ? maxSteps
        : min(128, int(ceil(float(maxSteps) * stepSize / max(traceStep, 1.0e-4))));
    vec3 liftNormal = normal;
    float liftScale = 1.0;
    if (normalLiftMode == 1 && rasterNormalCoverage > 0.5)
        liftNormal = depthReconstructedNormal;
    if (normalLiftMode == 4 && rasterNormalCoverage > 0.5
        && acos(clamp(dot(depthReconstructedNormal, rasterNormal), -1.0, 1.0))
            > normalLiftHybridAngle)
        liftNormal = depthReconstructedNormal;
    if (normalLiftMode == 2)
        liftScale = 0.0;
    else if (normalLiftMode == 3)
        liftScale = 0.5;
    vec3 rayStart = worldPos + reflectDir * (traceStep * 0.75)
                  + liftNormal * (traceStep * 0.25 + 0.01) * liftScale;
    if (debugMode == 30) {
        //opencode: SSR ray-start probe — encode the primary lift-off vector
        //   for normals-off/on comparison before any march or rejection path.
        finalColor = vec4((rayStart - worldPos) * 0.5 + 0.5, 1.0);
        return;
    }
    float dOrigSurface = viewZ(worldPos);

    //opencode: HDR alpha carries roughness in [0,1]; >1 marks an explicit
    //   per-model SSR opt-out. Water remains a dedicated SSR receiver.
    float packedRoughness = texture(colorMap, uv).a;
    if (debugMode == 31) {
        bool receivesSSR = isWater || packedRoughness <= 1.0;
        finalColor = receivesSSR ? vec4(0.0, 1.0, 0.0, 1.0)
                                 : vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    if (!isWater && packedRoughness > 1.0) {
        finalColor = vec4(0.0);
        return;
    }
    float pxRough = clamp(packedRoughness, 0.0, 1.0);

    //opencode: v0.1.15 - water pixels: the blended alpha (0.6-0.95 water
    //   opacity over the scene) would ride the alpha channel as a fake
    //   roughness and hit the early-out below. The surface is a mirror -
    //   use a fixed low roughness.
    if (isWater) pxRough = 0.03;

    //opencode: v0.1.13 - roughness early-out. The composite fade is
    //   (1 - pxRough?) and the roughnessFade floor is 0.05: pixels with
    //   roughness > 0.85 contribute < 0.28 ? env at best (and < 0.1 at
    //   normal incidence via fresnel) - marching 96 steps for them is waste.
    if (pxRough > 0.85) {
        finalColor = debugMode == 15 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(0.0);
        return;
    }

    //opencode: rough pixels march the exact audited center ray every frame
    //   (deterministic): the Paper-1 oracle seed rotated the lobe per frame
    //   for TAA to integrate, but the resolve sustains it as shimmer
    //   (static tstd 1.93 vs 0.22 seeded-off, 2026-09-18) instead of
    //   converging it — removed, SSGI/GTAO seeds untouched.

    //opencode: v0.1.17 - water blend raised 0.5 -> 0.85. The old cap existed
    //   because the overlay re-added sky on top of water.frag's fresnel ? 0.7
    //   term (double-brightness at grazing). With the env fallback now
    //   geometry-only, only real scene mirrors arrive via SSR - full-ish
    //   strength is safe and restores reflection contrast.
    float waterBlend = isWater ? 0.85 : 1.0;

    //opencode: v0.1.11 - multi-bounce chain. Bounce 0 = the classic march; each
    //   geometry hit reflects the ray at the hit normal and re-marches (coarser
    //   steps, capped). The terminal bounce (env or budget) samples the env
    //   cubemap - which is where behind-camera content enters reflections.
    vec3 accColor = vec3(0.0);
    float accMul = 1.0;
    vec3 chainEnd = vec3(0.0);   //opencode: v0.3.4 auxview - last fallback point
    float finalMargin = 1.0;
    int finalBounce = 0;
    MarchResult firstMr;
    bool firstSet = false;
    vec3 curDir = reflectDir;
    vec3 curStart = rayStart;
    //opencode: grazing water rays over the lake span many pixels per world
    //   unit; the default fine step accepts unstable silhouette crossings and
    //   creates a black/white impulse field. Keep normal SSR at its tuned step,
    //   but use a coarser water-only cadence to reject those false crossings.
    float curStep = traceStep;
    //opencode: v0.1.19 - water fresnel gate. Face-on water is a large marching
    //   area for a weak reflection; skip it below 0.05 (the same term is
    //   recomputed for the composite below). The 12% floor keeps object
    //   reflections visible from high views without turning water into a mirror.
    if (isWater) {
        //ai: keep a small non-zero reflection footprint at normal incidence;
        //   the physical 4% term washed object reflections out under a bright
        //   procedural sky, reading as "no planar reflection" from high views.
        float wfres = mix(0.12, 1.0, pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0));
        if (wfres < 0.05) {
            finalColor = debugMode == 15 ? vec4(1.0, 1.0, 0.0, 1.0) : vec4(0.0);
            return;
        }
    }
    //opencode: M1-b — transparent origins skip the march (they carry
    //   background depth: marching would mirror the background, not the
    //   glass) and take the env fallback directly (industry standard).
    //   Weighting mirrors the clean sky-escape tail exactly (fade=1,
    //   margin=1, env + fog delta, Schlick fresnel, floored roughness,
    //   planar-defer for on-plane origins).
    if (ssrGlassOn == 1 && hasVelocityMap == 1
        && texture(velocityMap, uv).a < 0.5) {
        vec3 gCol = envColorAt(reflectDir, pxRough);
        if (hasFogLut == 1) {
            vec2 flUv = vec2(fract(atan(reflectDir.z, reflectDir.x) / (2.0 * PI)),
                             acos(clamp(reflectDir.y, -1.0, 1.0)) / PI);
            gCol += texture(fogLutMap, flUv).rgb;
        }
        float gFres = mix(0.04, 1.0, pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0));
        float gFade = max(1.0 - pxRough * pxRough, 0.05);
        //opencode: rgb uses the UNFLOORED factor like the escape tail
        //   (line ~1076); alpha uses the floored one (finalColor line).
        vec3 gAcc = gCol * gFres * (1.0 - pxRough * pxRough);
        float gAlpha = opacity * gFres * gFade * waterBlend;
        bool gOnPlane = abs(dot(worldPos, planeNormal) - planeDist) < 0.10
                        && abs(dot(normal, planeNormal)) > 0.80;
        if (ssrPlanarDefer == 1 && hasPlanar == 1 && gOnPlane) {
            finalColor = vec4(0.0);
            return;
        }
        if (debugMode == 15) { finalColor = vec4(0.0, 1.0, 1.0, 1.0); return; }
        finalColor = vec4(gAcc, gAlpha);
        return;
    }
    int curMax = min(traceMaxSteps, 128);
    float curMaxT = maxDistance;
    vec2 selfUv = uv;

    for (int b = 0; b <= maxBounces; b++) {
        MarchResult mr = marchRay(curStart, curDir, curStep, curMax, curMaxT, maxLod, selfUv,
                                  viewZ(worldPos));
        if (!firstSet) { firstMr = mr; firstSet = true; }
        finalMargin = min(finalMargin, mr.minMargin);
        finalBounce = b;
        float reachB = max(float(mr.steps) * curStep, 0.001);
        if (mr.hitType == 1) {
            //opencode: mip-blurred sampling for rough surfaces (color mips
            //   generated per frame when SSR is enabled)
            //opencode: v0.3.5-polish - blur exponent 4.0 -> 3.0: mid-roughness hits
            //   sampled too coarse mips and read as soft patches; 3.0 caps
            //   the worst-case LOD at ~2.55
            vec3 col = textureLod(colorMap, mr.hitUV, pxRough * 3.0).rgb;
            float fadeB = clamp(1.0 - mr.trav / reachB, 0.0, 1.0);
            accColor += col * accMul * fadeB;
            if (b >= maxBounces) break;
            //opencode: reflect at the hit normal for the next bounce
            vec3 hn = hitNormalAt(mr.hitUV);
            vec3 viewAtHit = normalize(mr.hitPos - cameraPos);
            vec3 outDir = normalize(reflect(viewAtHit, hn));
            curStart = mr.hitPos + outDir * (curStep * 2.0 * 0.75);
            curDir = outDir;
            curStep = stepSize * 2.0;   //opencode: coarser steps for indirect bounces
            curMax = 48;
            curMaxT = maxDistance - mr.trav;
            selfUv = mr.hitUV;
            accMul *= bounceAtten;
        } else {
            //opencode: v0.1.17 - water pixels are GEOMETRY-ONLY: the env
            //   fallback re-added pale horizon sky on top of water.frag's own
            //   sky term (fresnel ? 0.7 LUT, covers ALL directions incl.
            //   behind-camera) - the doubled sky washed the near field
            //   gray-pink. Water rays that miss geometry contribute nothing;
            //   the water shader owns the entire sky response.
            chainEnd = curStart + curDir * mr.trav;
            if (!isWater) {
                //opencode: env termination - in-screen sky (type 3) fades by
                //   distance; exited/overran rays (type 0) use the traversal
                //   fade (cap 0.6 keeps grazing floors from flashing the env).
                //   v0.1.12: the hard 0.6 clamp was a kink at trav/reach=0.7
                //   (brightness transition ring) - now a smooth taper.
                vec3 col = envColorAt(curDir, pxRough);
                //opencode: v0.3.8 - reflected sky gets the same fog as direct
                //   sky: add the per-direction fog delta from the LUT (the
                //   direct view's fog composite only covers the surface ray,
                //   which left the mirror-horizon seam). Same equirect mapping
                //   as skyColorAt's LUT.
                if (hasFogLut == 1) {
                    vec2 flUv = vec2(fract(atan(curDir.z, curDir.x) / (2.0 * PI)),
                                     acos(clamp(curDir.y, -1.0, 1.0)) / PI);
                    col += texture(fogLutMap, flUv).rgb;
                }
                float travFracB = mr.trav / reachB;
                //opencode: v0.3.8 - a type-3 sky hit is a DEFINITE reflection:
                //   the sky is at infinity, so the traversal distance to reach
                //   it carries no uncertainty. The old 1 - travFrac ramp turned
                //   the exit-distance field (kinked at slab-corner bisectors)
                //   into visible luminance contours - the concentric band
                //   rings on grazing mirror views. Keep only a mild far fade.
                //opencode: v0.3.8 - unify clean escapes. A margin-exit ray
                //   that never crossed geometry escaped upward exactly like a
                //   sky hit (degenerate drift on grazing mirror views); both
                //   are definite sky reflections. Only escapes AFTER real
                //   crossings keep the penalized uncertainty ramp. The old
                //   split painted exit-type contours as luminance bands.
                float fadeB = (mr.hitType == 3 || mr.crossCount == 0)
                    ? clamp(1.0 - travFracB * 0.1, 0.0, 1.0)
                    : 0.6 * (1.0 - smoothstep(0.7, 1.0, travFracB));
                accColor += col * accMul * fadeB;
            }
            break;
        }
    }

    if (debugMode == 4) {
        float stepEff = firstMr.hitType == 1 ? (1.0 - float(firstMr.hitStep) / float(firstMr.steps)) : 0.0;
        float depthQual = firstMr.hitType == 1 ? (1.0 - clamp(firstMr.depthDelta / depthBias, 0.0, 1.0)) : 0.0;
        float convergence = firstMr.hitType == 1 ? 1.0 : (firstMr.hitType == 3 ? 0.75 : 0.0);
        finalColor = vec4(stepEff, depthQual, convergence, 1.0); //opencode: opaque - debug map must not blend with the scene
        return;
    }
    //ai: native float inspector preserves signed residuals even when the displayed probe clips them.
    if (debugMode == 32) {
        finalColor = vec4(firstMr.candidateResidual,1.0);
        return;
    }
    if (debugMode == 15) {
        //opencode: SSR lab - white means the pixel reached the full march;
        //   early-return reasons are red=normal, green=rough, blue=background,
        //   yellow=water, cyan=glass.
        finalColor = vec4(1.0);
        return;
    }
    if (debugMode == 5) {
        float normState = float(firstMr.state);
        float normPrevDiff = clamp(firstMr.prevDiff / (max(float(firstMr.steps) * firstMr.stepUsed, 0.001) * 0.05), -1.0, 1.0) * 0.5 + 0.5;
        float normHitDist = firstMr.trav / max(float(firstMr.steps) * firstMr.stepUsed, 0.001);
        finalColor = vec4(normState, normPrevDiff, normHitDist, 1.0); //opencode: opaque
        return;
    }
    if (debugMode == 7) {
        float stepEff = firstMr.hitType == 1 ? (1.0 - float(firstMr.hitStep) / float(firstMr.steps)) : 0.0;
        float depthQual = firstMr.hitType == 1 ? (1.0 - clamp(firstMr.depthDelta / depthBias, 0.0, 1.0)) : 0.0;
        float convergence = firstMr.hitType == 1 ? 1.0 : (firstMr.hitType == 3 ? 0.75 : 0.0);
        float hitQuality = stepEff * depthQual * convergence;
        float fade = firstMr.hitType == 1
            ? clamp(1.0 - firstMr.trav / max(float(firstMr.steps) * firstMr.stepUsed, 0.001), 0.0, 1.0)
            : clamp((1.0 - firstMr.trav / max(float(firstMr.steps) * firstMr.stepUsed, 0.001)) * 2.0, 0.0, 0.6);
        float confidence = hitQuality * fade * finalMargin * (1.0 - pxRough * pxRough);
        finalColor = vec4(confidence, confidence, confidence, 1.0); //opencode: opaque
        return;
    }

    if (debugMode == 8) {
        //opencode: v0.1.16 - the probe now mirrors the ACTUAL reject test
        //   (relative threshold - the old absolute |cross| < 0.001 flagged
        //   everything at the showcase's scale: per-pixel world tangents are
        //   ~0.01 so |cross| ? 1e-4 on any legitimately flat surface).
        bool rejected = (nLen < 1e-8 || nLen < 0.001 * (length(dFdx_w) * length(dFdy_w)));
        finalColor = vec4(rejected ? 1.0 : 0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (debugMode == 9) {
        //opencode: probe - reconstructed normal (RGB), viewDir fallback = magenta.
        //   Uses the same relative degeneracy test as the main path (the old
        //   absolute <0.001 test flagged every pixel at showcase scale).
        bool nFlat = (nLen < 1e-8 || nLen < 0.001 * (length(dFdx_w) * length(dFdy_w)));
        finalColor = nFlat ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debugMode == 10) {
        //opencode: v0.1.11 - bounce index (R=0, G=1, B=2)
        finalColor = vec4(finalBounce == 0 ? 1.0 : 0.0,
                          finalBounce == 1 ? 1.0 : 0.0,
                          finalBounce == 2 ? 1.0 : 0.0, 1.0);
        return;
    }
    if (debugMode == 11) {
        //opencode: v0.1.26 - contact-zone diagnostics.
        //   R = LOD-0 confirmation rejections (saturated at 3)
        //   G = phantom-validation rejections (saturated at 3)
        //   B = crossings encountered (saturated at 3)
        //   A bright channel = that rejection class fires on this pixel.
        float cr = clamp(float(firstMr.confirmRejects) / 3.0, 0.0, 1.0);
        float pr = clamp(float(firstMr.phantomRejects) / 3.0, 0.0, 1.0);
        float cc = clamp(float(firstMr.crossCount) / 3.0, 0.0, 1.0);
        finalColor = vec4(cr, pr, cc, 1.0); //opencode: opaque - diagnostic map
        return;
    }
    if (debugMode == 12) {
        //opencode: SSR hit-normal probe - accepted first-hit normal encoded as RGB.
        if (firstMr.hitType == 1) {
            vec3 probeNormal = hitNormalAt(firstMr.hitUV);
            finalColor = vec4(probeNormal * 0.5 + 0.5, 1.0);
        } else {
            finalColor = vec4(0.0);
        }
        return;
    }
    if (debugMode == 13) {
        //opencode: SSR hit-facing probe - hit normal versus the outgoing SSR ray.
        if (firstMr.hitType == 1) {
            vec3 probeNormal = normalize(hitNormalAt(firstMr.hitUV));
            float facing = dot(probeNormal, normalize(-reflectDir));
            finalColor = vec4(vec3(facing * 0.5 + 0.5), 1.0);
        } else {
            finalColor = vec4(0.0);
        }
        return;
    }
    if (debugMode == 14) {
        //opencode: SSR lab - red is a geometry hit, green is an in-screen sky
        //   hit, and blue is a clean exit to the environment fallback.
        finalColor = firstMr.hitType == 1 ? vec4(1.0, 0.0, 0.0, 1.0)
            : (firstMr.hitType == 3 ? vec4(0.0, 1.0, 0.0, 1.0)
                                    : vec4(0.0, 0.0, 1.0, 1.0));
        return;
    }
    if (debugMode == 17) {
        //opencode: SSR lab - direct primary-ray environment sample, isolating
        //   cubemap/LUT content from screen-space hit sampling.
        finalColor = vec4(envColorAt(reflectDir, pxRough), 1.0);
        return;
    }
    if (debugMode == 18) {
        //opencode: SSR lab - accepted screen-space hit color, isolating
        //   colorMap lookup and hitUV refinement from environment sampling.
        finalColor = firstMr.hitType == 1
            ? vec4(textureLod(colorMap, firstMr.hitUV, pxRough * 3.0).rgb, 1.0)
            : vec4(0.0);
        return;
    }
    if (debugMode == 16) {
        //opencode: aux-gate inspector - R=edge fade, G=planar depth validity,
        //   B=miss routing gate. Pinpoints aux-coverage dropouts.
        vec2 suvG = planarSampleUV(worldPos);
        vec2 eG = min(clamp(suvG, 0.0, 1.0), 1.0 - clamp(suvG, 0.0, 1.0));
        float edgeG = smoothstep(0.0, 0.06, min(eG.x, eG.y));
        float dokG = 1.0;
        if (hasPlanarDepth == 1)
            dokG = texture(planarDepthMap, clamp(suvG, 0.0, 1.0)).r < 0.999 ? 1.0 : 0.0;
        float gateG = (hasPlanar == 1 && !isWater && firstMr.hitType != 1) ? 1.0 : 0.0;
        finalColor = vec4(edgeG, dokG, gateG, 1.0);
        return;
    }
    if (debugMode == 23) {
        //opencode: oracle UV echo - raw planarSampleUV(worldPos) as RG so an
        //   offline CPU oracle can verify the mirror chain (mirror math +
        //   planarVP upload + y-flip + RT orientation) texel-numerically.
        vec2 suvO = planarSampleUV(worldPos);
        finalColor = vec4(suvO.x, suvO.y, 0.0, 1.0);
        return;
    }
    if (debugMode == 24) {
        //opencode: oracle worldPos echo - G-buffer position feeding the
        //   planar lookup, roughly scaled (x/8, (y+1)/4, z/8) for 8-bit.
        finalColor = vec4(worldPos.x / 8.0, (worldPos.y + 1.0) / 4.0,
                          worldPos.z / 8.0, 1.0);
        return;
    }
    if (debugMode == 26) {
        //opencode: oracle depth echo - raw depth (R) + forward view
        //   distance /50 (G) to decompose worldFromDepth error into
        //   depth-source vs linearization vs inverse-matrix stages.
        finalColor = vec4(rawDepth, viewZ(worldPos) / 50.0, 0.0, 1.0);
        return;
    }
    if (debugMode == 27) {
        //opencode: oracle joint echo - W.xz + raw from the SAME pixel/run:
        //   distinguishes intra-frame contradiction from cross-run drift.
        finalColor = vec4(worldPos.x / 8.0, rawDepth, worldPos.z / 8.0, 1.0);
        return;
    }
    if (debugMode == 25) {
        //opencode: oracle matrix echo - first column of the in-shader
        //   planarVP (should equal logged m0,m1,m2), scaled /4+0.5.
        //   Distinguishes upload corruption from oracle-side convention bugs.
        finalColor = vec4(planarVP[0].x / 4.0 + 0.5,
                          planarVP[0].y / 4.0 + 0.5,
                          planarVP[0].z / 4.0 + 0.5, 1.0);
        return;
    }

    //opencode: Schlick Fresnel - face-on reflections stay weak, grazing gets
    //   full strength. Water uses a 12% floor (visible object reflections from
    //   high views); opaque surfaces keep the physical 4% term. Without the
    //   term, SSR blended flat at 0.8 opacity: model tops reflecting the bright
    //   sky washed out and the grazing floor showed no mirror reflections.
    float fresnel = isWater
        ? mix(0.12, 1.0, pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0))
        : mix(0.04, 1.0, pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0));

    //opencode: composite fade - geometry chains fade by the first hit's
    //   traversal; env chains use the traversal fade (same curve as the
    //   per-bounce env fade)
    float travFrac = firstMr.trav / max(float(firstMr.steps) * firstMr.stepUsed, 0.001);
    float fade = firstMr.hitType == 1
        ? clamp(1.0 - travFrac, 0.0, 1.0)
        : (firstMr.hitType == 3 || firstMr.crossCount == 0
            //opencode: v0.3.8 - clean escapes (sky hit or margin exit with no
            //   crossings) are definite sky reflections; flat fade (see the
            //   per-bounce note) instead of the exit-distance ramp
            ? clamp(1.0 - travFrac * 0.1, 0.0, 1.0)
            : 0.6 * (1.0 - smoothstep(0.7, 1.0, travFrac)));

    //opencode: v0.1.11 - finalMargin (min over all bounces) replaces the old
    //   last-step uvMargin: one continuous edge-dissolve profile for both hit
    //   and env paths - no seam where rays hand off to the env fallback.
    //opencode: v0.3.8 - screen-edge dissolve only applies where the sampled
    //   content is unreliable. A clean escape (sky hit, or margin exit with no
    //   crossings) reflects the sky LUT/cubemap, which is valid EVERYWHERE on
    //   screen - grading it by the walked path's frame-edge proximity painted
    //   the vanishing-point contour field as luminance bands on grazing mirror
    //   views (every upward walk exits the frame, so the whole surface got a
    //   margin gradient). Geometry hits and post-crossing escapes keep it.
    float cleanEscape = (firstMr.hitType == 3 || firstMr.crossCount == 0) ? 1.0 : 0.0;
    float marginTerm = mix(finalMargin, 1.0, cleanEscape);

    accColor *= fresnel * fade * marginTerm * (1.0 - pxRough * pxRough);

    //ai: optional hybrid mirror-view lookup; the matrix projection alone does
    //   not certify off-plane visibility. Even equal reflection directions can
    //   have different ray origins (GfxSsrRayCoverage.lean). This remains a
    //   heuristic for curved receivers, not a general second-camera ray hit.
    //   Routes select misses with depth activity, wrong-side hits or plane
    //   seams; they do not require an unavailable cubemap contribution.
    float roughnessFade = max(1.0 - pxRough * pxRough, 0.05);
    //opencode: v0.3.6 planar precision - routing rules. The v0.3.4 exit-point
    //   projection put candidates at FAILED hit positions, which mapped into
    //   wall/floor areas of the mirror RT instead of the true sightline
    //   continuation; undersides of elevated objects never materialized and
    //   silhouette self-hits stayed confident. Candidates are now the origin
    //   point itself mirrored across the plane (analytic-exact for on-plane
    //   origins). Hit-route replacement fires on physically impossible
    //   crossings (ray arriving from behind a camera-front face) and on
    //   low-margin silhouette zones of plane origins.
    vec3 hnAUX = firstMr.hitType == 1 ? hitNormalAt(firstMr.hitUV) : vec3(0.0);
    float dnWrong = (firstMr.hitType == 1)
        ? dot(normalize(reflectDir), normalize(hnAUX)) : -1.0;
    bool originOnPlane = abs(dot(worldPos, planeNormal) - planeDist) < 0.10
                         && abs(dot(normal, planeNormal)) > 0.80;
    //opencode: item-15 - auxMiss requires depth activity along the march
    //   (accepted/rejected crossings or silhouette grazes). A march that
    //   sailed clean never neared geometry: its TRUE reflection is
    //   background, and pasting the mirror photo there paints the ball's
    //   M-shadow as detached ghosts. Clean escapes fall through to env
    //   (sky-on-sky is correct). Disocclusion cases (probe cube) flip the
    //   trace at silhouettes, so the gate stays open for them.
    bool auxMiss = (hasPlanar == 1 && !isWater && firstMr.hitType != 1
        && (firstMr.crossCount > 0 || firstMr.grazeCount > 0
            || firstMr.phantomRejects > 0));
    bool auxWrongSide = (hasPlanar == 1 && !isWater && firstMr.hitType == 1
                         && dnWrong > 0.05);
    bool auxSeam = (hasPlanar == 1 && !isWater && firstMr.hitType == 1
                    && originOnPlane && finalMargin < 0.35);
    if (auxMiss || auxWrongSide || auxSeam) {
        //opencode: item-15 - auxMiss samples the photo at the march
        //   closest-approach point (projected directly: the point is
        //   already in the room) instead of the origin's mirror. Origin
        //   mirroring pastes the ball's M-shadow over true background
        //   (detached ghosts ~60px below contact); closest-approach
        //   carries the geometry the ray actually neared (contact ball,
        //   probe cube, or background). WrongSide/Seam keep the audited
        //   origin mapping (validated paths, different contract).
        vec2 suvO = planarSampleUV(worldPos);
        vec4 argPC = planarVP * vec4(firstMr.nearPos, 1.0);
        vec2 suvN = (argPC.w > 0.001)
            ? (argPC.xy / argPC.w * 0.5 + 0.5) : vec2(-1.0);
        //opencode: argmin mapping applies to on-plane origins only (the
        //   ghost zone): off-plane origins (ball surfaces) were pixel-clean
        //   under origin mapping, and argmin pastes floor content onto them
        //   (white bloom bands with hard seams). originOnPlane already in
        //   scope from the seam routing above.
        vec2 suvA = (auxMiss && originOnPlane) ? suvN : suvO;
        if (all(greaterThan(suvA, vec2(-0.02))) && all(lessThan(suvA, vec2(1.02)))) {
            vec2 eA = min(clamp(suvA, 0.0, 1.0), 1.0 - clamp(suvA, 0.0, 1.0));
            float edgeFA = smoothstep(0.0, 0.06, min(eA.x, eA.y));
            // audit - depth validity: empty mirror pixels (depth clear =
            //   far) fall through to env instead of painting clear color
            //   as a veil / dark tiles. Unavailable mask = old behavior.
            //opencode: binary depth gate (empty mirror pixels fall through
            //   to env - the veil fix) + GRAZING RULE for the mirror/env
            //   choice on misses: a floor ray leaving nearly parallel to the
            //   plane sees horizon sky in a true mirror, which is what the env
            //   path already returns. Letting those rays paint the mirror RT
            //   (asymptotic slab depths + stretched silhouette contours =
            //   mosaic tiles) is pure loss; steep rays keep the mirror.
            bool depthOk = true;
            if (hasPlanarDepth == 1)
                depthOk = texture(planarDepthMap, clamp(suvA, 0.0, 1.0)).r < 0.999;
            if (edgeFA > 0.001 && depthOk) {
                vec3 pcol = texture(planarMap, clamp(suvA, 0.0, 1.0)).rgb;
                if (auxMiss) {
                    //opencode: v0.3.4-polish retained - far-march misses keep
                    //   the env path (sky reflections must not gray out)
                    float travFracA = clamp(firstMr.trav
                        / max(float(firstMr.steps) * firstMr.stepUsed, 0.001), 0.0, 1.0);
                    float auxConf = 1.0 - travFracA * travFracA;
                    //opencode: clean-escape margin bypass - finalMargin is a
                    //   min-over-march-steps staircase (the argmin step flips
                    //   discretely along t-contours: the top-down concentric
                    //   rings). For clean escapes (sky/margin-exit, no
                    //   accepted crossings) the mirror sample is reliable
                    //   everywhere, same argument as marginTerm - use 1.0.
                    //   Post-crossing misses keep the penalty (unreliable).
                    float auxMargin = (firstMr.hitType == 3 || firstMr.crossCount == 0)
                        ? 1.0 : finalMargin;
                    //opencode: directed graze gate (no abs): the mirror RT
                    //   only holds the scene on the plane's reflecting side.
                    //   Downward-facing pixels (slab underside seen from
                    //   below) must fall back to env - their mirror sample
                    //   would be a ghost of the scene above, not the void
                    //   below. Up-facing and steep rays are unaffected.
                    float grazW = smoothstep(0.03, 0.3,
                        dot(normalize(reflectDir), planeNormal));
                    if (auxConf > 0.05 && grazW > 0.001) {
                        vec3 mirrorCol = pcol * fresnel * auxMargin
                                       * roughnessFade * auxConf;
                        accColor = mix(accColor, mirrorCol, grazW);
                        fade = max(fade, edgeFA * auxConf * grazW);
                    }
                } else {
                    //opencode: v0.3.6 - no finalMargin penalty on replacement:
                    //   the mirror-RT sample is valid wherever it exists
                    accColor = mix(accColor,
                        pcol * fresnel * roughnessFade,
                        edgeFA * 0.9);
                }
            }
        }
    }

    finalColor = vec4(accColor, opacity * fresnel * fade * marginTerm * roughnessFade * waterBlend);
    //opencode: hunt - planar defer (see uniform note): transparent output
    //   lets the forward-planar mirror own plane pixels. Covers hits AND
    //   misses: march approximations and full-strength sky escapes would
    //   overdraw the exact mirror (washout); aux content is the same RT
    //   the forward path already sampled, so nothing is lost.
    if (ssrPlanarDefer == 1 && hasPlanar == 1 && originOnPlane) {
        finalColor = vec4(0.0);
        return;
    }
}
