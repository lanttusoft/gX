//opencode: volumetric fog generation - ray-march through height-based density
//   volume with sun in-scattering. Reconstructs world position from depth,
//   marches from camera to surface, accumulates Beer-Lambert transmittance
//   and in-scattered sun light. Output: vec4(inscattered.rgb, transmittance).
#version 330

uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;
uniform int lutMode = 0;   //opencode: v0.3.8 - fog-LUT equirect mode
uniform int clampLdr = 0;  //opencode: O-blowout - cap inscatter for post-ACES consumers (main view + LUT); planar fog stays HDR
uniform vec3  sunDir;          // direction toward the sun (normalized)
uniform vec3  fogSunColor = vec3(1.0);
uniform float fogSunBrightness = 1.0;
uniform float density = 0.01;  // global fog density
uniform float heightFalloff = 5.0;  // height decay factor (lower = fog stays low)
uniform float scattering = 0.5;     // sun-scattering bias
uniform vec2  resolution;
uniform vec3  cameraPos;
uniform float nearPlane = 0.1;
uniform float farPlane = 1000.0;
uniform mat4  invProjection;
uniform mat4  invView;

//opencode: v0.3-M3.1 - god rays: cascade-0-only shadow occlusion +
//   Henyey-Greenstein phase. The ARRAY sampler variant blacks the whole
//   frame whenever this shader compiles (see architecture.md M4 blocker):
//   every sampler2DArray/mat4[4]/split reference is removed so the poison
//   construct cannot exist in this program.
uniform sampler2D      sunShadowMap;      // unit 8  - cascade 0 / single shadow map
uniform mat4  layer0Mat;                  // vps[0] in CSM mode, sun VP otherwise
//opencode: v0.3.8 - plain-2D copies of cascade layers (blitted per frame in
//   fog_volume.c; sampler2DArray is poison in this program) + the same split
//   distances the scene shader uses
uniform sampler2D csmTex1;   // unit 26
uniform sampler2D csmTex2;   // unit 28
uniform sampler2D csmTex3;   // unit 29
uniform mat4  csmVp1;
uniform mat4  csmVp2;
uniform mat4  csmVp3;
uniform vec3  csmSplits = vec3(0.0);
uniform int   cascadeCount = 1;
uniform float hgAsymmetry = 0.55;
uniform float shadowRange = 40.0;         // beyond this distance: unshadowed
//opencode: item-13 - point/spot light inscatter (packed USED non-sun
//   lights, heap order, cap 8 - the only consumer of these arrays, so
//   dense packing is safe unlike the shadow-coupled standard.frag arrays).
//   Three vec4 arrays (no vec3-array raw-RHI upload exists): pos, color +
//   brightness, atten triple. Count 0 = loop dead = pre-item-13 math.
uniform int fogPointCount = 0;
uniform vec4 fogPointPos[8];
uniform vec4 fogPointColor[8];
uniform vec4 fogPointAtten[8];
//opencode: spot cone (item-13 follow-up) - axis + outer cosHalf; inner
//   cosHalf rides fogPointAtten.w (unused by the distance formula). Points
//   upload outer -2.0 (sentinel: real cosines never go below -1).
uniform vec4 fogSpotDir[8];
//opencode: point-light shadows in the march — plain-2D atlas of 512px tiles
//   (slot*6+face), blitted per frame from the point array (M3.1: no
//   sampler2DArray in this program). Tile = (tileU0, farPlane, layerBase,
//   hasShadow); beyond-far samples skip the test (depths invalid past far).
uniform sampler2D fogPointShadowAtlas;
uniform vec4 fogPointShadowTile[8];
uniform float fogPointShadowTileW = 0.0416667;
uniform float fogPointShadowBias = 0.05;

in vec2 fragTexCoord;
out vec4 fragColor;

float linearizeDepth(float d, float near, float far) {
    float z = d * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

vec3 worldFromDepth(float depth, vec2 uv) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos.xyz /= viewPos.w;
    vec4 worldPos = invView * viewPos;
    return worldPos.xyz;
}

float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

//opencode: Henyey-Greenstein phase - forward-scattering lobe toward the sun
float hgPhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (12.566370614 * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

//opencode: hard shadow test at a world point along the ray (1-tap, cheap at
//   32 march steps). Cascade-0 matrix only - far field unshadowed by design.
//opencode: single-tap hard test through one cascade's transform+depth
float csmTest(vec3 wp, mat4 vp, sampler2D depthTex) {
    vec4 ls = vp * vec4(wp, 1.0);
    vec3 pc = ls.xyz / ls.w * 0.5 + 0.5;
    if (pc.z > 1.0 || pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0) return 1.0;
    float d = texture(depthTex, pc.xy).r;
    return (pc.z - 0.0025 > d) ? 0.0 : 1.0;
}

//opencode: v0.3.8 - cascade-picked occlusion over the whole CSM range
//   (was cascade-0 only with a hard 40u unshadowed cutoff)
float shadowAtWorld(vec3 wp, float t) {
    if (cascadeCount > 1 && csmSplits.x > 0.0) {
        if (t > csmSplits.z && cascadeCount >= 4) return csmTest(wp, csmVp3, csmTex3);
        if (t > csmSplits.y && cascadeCount >= 3) return csmTest(wp, csmVp2, csmTex2);
        if (t > csmSplits.x) return csmTest(wp, csmVp1, csmTex1);
        return csmTest(wp, layer0Mat, sunShadowMap);
    }
    if (t > shadowRange) return 1.0;
    return csmTest(wp, layer0Mat, sunShadowMap);
}

//opencode: point-light visibility from the plain-2D shadow atlas (face math
//   mirrors gX_PointShadow_PCF, single hard tap like csmTest above).
float fogPointVis(vec3 lightToFrag, float farPlane, float tileU0) {
    float currentDepth = length(lightToFrag);
    vec3 dir = lightToFrag / currentDepth;
    vec3 a = abs(dir);
    float face;
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z) {
        face = (dir.x >= 0.0) ? 0.0 : 1.0;
        //ai: negative X uses +z, matching GL and the surface point-shadow sampler.
        uv = vec2(dir.x >= 0.0 ? -dir.z : dir.z, -dir.y) / a.x;
    } else if (a.y >= a.z) {
        face = (dir.y >= 0.0) ? 2.0 : 3.0;
        uv = vec2(dir.x, (dir.y >= 0.0) ? dir.z : -dir.z) / a.y;
    } else {
        face = (dir.z >= 0.0) ? 4.0 : 5.0;
        uv = vec2((dir.z >= 0.0) ? dir.x : -dir.x, -dir.y) / a.z;
    }
    uv = uv * 0.5 + 0.5;
    vec2 auv = vec2(tileU0 + (face + uv.x) * fogPointShadowTileW, uv.y);
    float stored = texture(fogPointShadowAtlas, auv).r;
    return (currentDepth - fogPointShadowBias > stored * farPlane) ? 0.0 : 1.0;
}

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float rawDepth = texture(sceneDepth, uv).r;

    //opencode: procedural sky owns clear-depth pixels; marching them to the
    //   far plane replaces the sky gradient with saturated fog. Keep the
    //   equirectangular LUT path enabled so SSR sky escapes still receive fog.
    if (lutMode == 0 && rawDepth >= 0.9999) {
        fragColor = vec4(0.0);
        return;
    }

    //opencode: v0.3-M3 - sky rays march a fixed distance so light shafts
    //   remain visible AGAINST the sky (where god-rays read most strongly)
    //opencode: v0.3.8 - per-pixel sky rays. The M3 hack marched ALL sky
    //   pixels along the camera-forward ray, so when the camera faced the
    //   sun every sky pixel lit at the HG forward peak (~2-4x) and the sky
    //   washed out. Unprojecting the far-plane point per pixel gives each
    //   sky pixel its true ray direction; the 140u cap bounds the march.
    //opencode: v0.3.8 - lutMode: the sky branch per EQUIRECT texel (the fog
    //   delta LUT for SSR escapes). Direction from the sky LUT's own
    //   convention (sky_new.frag skyEquirect): the whole texel is a sky ray.
    vec3 marchEnd;
    if (lutMode == 1) {
        vec2 equ = vec2(uv.x, 1.0 - uv.y);
        float pitch = equ.y * 3.14159265;
        float yaw = equ.x * 2.0 * 3.14159265;
        marchEnd = cameraPos + normalize(vec3(cos(yaw) * sin(pitch), cos(pitch), sin(yaw) * sin(pitch))) * 140.0;
    } else {
        marchEnd = worldFromDepth(rawDepth, uv);
    }
    vec3 marchDelta = marchEnd - cameraPos;
    float viewDist = min(length(marchDelta), 140.0);
    //opencode: rayDir is the TRUE unit ray — viewDist caps only the march
    //   LENGTH. Dividing by capped viewDist left |rayDir| = dist/140 (~7x)
    //   for sky rays, pushing cosTheta past 1 into the HG pole
    //   ((1+g^2)/2g = 1.18): a clamped bright band perpendicular to the sun.
    vec3 rayDir = marchDelta / max(length(marchDelta), 0.0001);
    float stepLen = viewDist / 32.0;
    float jitter = ign(gl_FragCoord.xy);

    vec3 inscattered = vec3(0.0);
    float transmittance = 1.0;
    float visAccum = 0.0;
    float hgLobe = -1.0;

    float heightBase = 0.0;

    for (int i = 0; i < 32; i++) {
        float t = ((float(i) + jitter) * viewDist) / 32.0;
        vec3 worldPos = cameraPos + rayDir * t;

        //opencode: v0.3-M3 - height band anchored near EYE level (camera.y-1.5):
        //   world-absolute heightBase made fog vanish entirely in scenes whose
        //   playable altitude differs from y=0
        float heightBaseEye = cameraPos.y - 1.5;
        float height = max(worldPos.y - heightBaseEye, 0.0);
        float heightWeight = exp(-height * heightFalloff);

        float stepDensity = density * heightWeight * stepLen;
        transmittance *= exp(-stepDensity);

        //opencode: v0.3-M3 - HG forward-scatter toward the sun (gives shafts
        //   their silver-lining look), small isotropic floor so fog never goes
        //   fully black away from the light; occluded steps contribute nothing,
        //   which is what carves visible light shafts out of the volume.
        float cosTheta = dot(rayDir, sunDir);
        float phase = mix(hgPhase(cosTheta, hgAsymmetry), 1.0 / 12.566370614, 0.15);
        //opencode: O-blowout - peak cap. hgAsymmetry 0.85 (the fog-seam fix)
        //   peaks at 6.5x: in-band grazing-sun marches saturate inscatter at
        //   ~4 HDR and white out (no rolloff left post-ACES). Capping the
        //   PEAK at 2.0 bounds the asymptote (~1.5) while preserving lobe
        //   WIDTH (only the inner ~10 deg cone changes; the 37deg+ seam zone
        //   sits at phase ~0.1, untouched).
        phase = min(phase, 2.0);
        float vis = shadowAtWorld(worldPos, t);
        visAccum += vis;
        hgLobe = max(hgLobe, cosTheta);
        float sunLum = stepDensity * scattering * phase * vis; //opencode: no xPI here - the HG phase is already 4PI-normalized; a second xPI below double-counted it (~x10 inscatter, white-out toward sun at density 0.01)
        inscattered += fogSunColor * fogSunBrightness * sunLum * transmittance;
        //opencode: item-13 - point/spot inscatter. Same recipe as the sun:
        //   HG phase toward the LIGHT (forward lobe = bright core, falloff
        //   = diffuse glow), same 0.15 isotropic floor, same attenuation
        //   formula as standard.frag (1/(c+l*d+q*d^2)), same transmittance
        //   weighting. Point shadows ride the plain-2D atlas below
        //   (fogPointVis; unshadowed only without casters — tile.w = 0).
        //   The 1e-3 denominator floor keeps a degenerate all-zero atten
        //   triple from NaN-poisoning the whole inscatter accumulator
        //   (the end-of-main scrub would nuke the sun term too).
        for (int li = 0; li < fogPointCount && li < 8; li++) {
            vec3 toL = fogPointPos[li].xyz - worldPos;
            float ld = length(toL);
            if (ld < 0.001) continue;
            vec3 ldir = toL / ld;
            vec4 at = fogPointAtten[li];
            float atten = 1.0 / max(at.x + at.y * ld + at.z * ld * ld, 0.001);
            //opencode: spot cone (mirrors standard.frag exactly: axis =
            //   travel direction, cosAng from light to sample, smoothstep
            //   outer->inner cos). Points carry outer -2.0 and skip this.
            if (fogSpotDir[li].w > -1.5) {
                vec3 axis = normalize(fogSpotDir[li].xyz);
                float cosAng = max(dot(-ldir, axis), 0.0);
                atten *= smoothstep(fogSpotDir[li].w, at.w, cosAng);
            }
            float cosT = dot(rayDir, ldir);
            float ph = mix(hgPhase(cosT, hgAsymmetry), 1.0 / 12.566370614, 0.15);
            vec4 ptTile = fogPointShadowTile[li];
            float pvis = (ptTile.w > 0.5 && ld < ptTile.y)
                ? fogPointVis(-ldir * ld, ptTile.y, ptTile.x) : 1.0;
            inscattered += fogPointColor[li].rgb * fogPointColor[li].a
                         * stepDensity * scattering * ph * atten * pvis * transmittance;
        }
    }

    //opencode: v0.3-M3 - bake the FULL composite delta here so the blend
    //   pass can be a plain additive Texture_Draw (the rlgl batch path is
    //   the only one proven to reach bloom.scene reliably):
    //   delta = scene*T + inscatter*sunColor - scene
    vec3 sceneCol = texture(sceneColor, uv).rgb;
    float T = max(transmittance, 0.12); //opencode: never fully black out
    vec3 insc = inscattered;
    if (isnan(insc.r) || isnan(insc.g) || isnan(insc.b))
        insc = vec3(0.0);
    //opencode: O-blowout - LDR-domain cap. The main-view delta composites
    //   additively onto the POST-ACES bloom.scene (and the LUT onto SSR
    //   escapes the same way): unclamped HDR inscatter has no rolloff left
    //   and clips to white at grazing sun. Cap inscatter so scene + delta
    //   never exceeds 1 (extinction negatives untouched). Planar fog keeps
    //   full HDR range (tonemapped later) via clampLdr=0.
    if (clampLdr == 1)
        insc = min(insc, max(vec3(1.0) - sceneCol * T, vec3(0.0)));
    vec3 delta = sceneCol * (T - 1.0)
               + insc; //opencode: no xPI - see above (was double-pi).
        //opencode: M3.1 - NaN scrub (additive NaN renders black)
    if (isnan(inscattered.r) || isnan(inscattered.g) || isnan(inscattered.b))
        inscattered = vec3(0.0);
    //opencode: v0.3.8 - output the ALREADY-DECLARED composite delta
    //   (extinction + inscatter): the inscatter-only fragColor made this
    //   pass a no-op for non-sky pixels and dead-stripped sceneColor
    if (isnan(delta.r) || isnan(delta.g) || isnan(delta.b))
        delta = vec3(0.0);
    fragColor = vec4(min(delta, vec3(60.0)), 1.0);
}
