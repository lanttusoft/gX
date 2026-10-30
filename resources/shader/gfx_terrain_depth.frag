//opencode: Paper 2 spike — terrain depth march (G-buffer contract proof).
//   Fullscreen SDF march over an analytic heightfield; writes raster-identical
//   depth via gl_FragDepth (clip = viewProj * hit, depth = z/w*0.5+0.5).
//   Depth-only: the C pass disables color writes. Hook-gated GX_T_TERRAINDEPTH.
#version 330
//opencode: gap-2 — bias uniforms first: common/shadow.glsl references
//   them (like standard.frag:27-28 declares before its include).
uniform float shadowBiasMax = 0.015;
uniform float shadowBiasMin = 0.0025;
#include "common/shadow.glsl"

in vec2 fragTexCoord;

uniform mat4 invVP;
uniform mat4 viewProj;
uniform vec2 resolution;
uniform float hillAmp = 0.0;
uniform float baseY = 0.0;
//opencode: spike 4 — texture heightfield source (tileable value-noise
//   FBM, R32F, REPEAT). heightOn=1 samples heightTex (uv = xz/tile,
//   [0,1] -> [-1,1] like the sine path); 0 = analytic sine (default).
uniform sampler2D heightTex;
uniform int heightOn = 0;
uniform float heightTile = 8.0;
//opencode: spike 2 — shared-path lighting uniforms (same values the
//   model path uploads in Shader_SetValues: sun + ambient + exposure).
uniform vec3 lightPosition;
uniform vec4 sunColor;
uniform float sunBrightness;
uniform vec4 sunDiffuseTint;
uniform float sunDiffuseFactor;
uniform vec4 ambientColor;
uniform float ambientFactor;
uniform float exposure;
//opencode: spike 2 — point lights (type-1 only, first 4 from the heap;
//   spots/cookies/shadows are production work, noted in paper 2).
uniform vec3 pointPos[4];
uniform vec4 pointColor[4];
uniform float pointBright[4];
uniform vec3 pointAtten[4];
uniform float pointDiff[4];
uniform int pointCount;
//opencode: gap-2 shadows — CSM receive mirror (standard.frag PBR block):
//   cascade-0 single map + array layers 1..3, same selection math.
uniform int enableShadows = 0;
uniform float shadowDiffuseBias = 0.5;
uniform float shadowSpecularBias = 1.0;
uniform sampler2D shadowMap;
uniform sampler2DArray csmShadowMapArray;
uniform mat4 csmVP[4];
uniform vec3 csmSplits = vec3(0.0);
uniform float csmBlendScale = 1.0;
uniform int cascadeCount = 1;
//opencode: gap-2 single-cascade path uses the sun VP (mirrors the
//   matLightSpace selection in shader.c: single = sun.camera VP).
uniform mat4 sunVP;
uniform float nearPlane = 0.1;
uniform float farPlane = 1000.0;
//opencode: gap-3 SSAO — Phong placement mirror (standard.frag:869):
//   multiply pre-exposure linear by mix(1, ssao, 0.6). One frame stale
//   like every other consumer (SSAO pass runs after terrain).
uniform int enableSSAO = 0;
uniform sampler2D ssaoMap;

//opencode: linear view-space distance from NDC depth (CSM cascade
//   selection) — same formula as standard.frag.
float linearViewDist(float ndcZ, float n, float f) {
    float z_ndc = ndcZ * 2.0 - 1.0;
    return abs((2.0 * n * f) / (f + n - z_ndc * (f - n)));
}

out vec4 fragColor;

//opencode: spike heightfield — flat y=0 under the reflectrow slab footprint
//   (|x|<=7, |z|<=4, smooth blend out to 9/6), sine hills outside.
//   CPU mirror Terrain_H in terrain.c must stay op-identical (same order).
float terrainH(vec2 xz) {
    float mx = smoothstep(7.0, 9.0, abs(xz.x));
    float mz = smoothstep(4.0, 6.0, abs(xz.y));
    float m = max(mx, mz);
    float hills;
    if (heightOn == 1) {
        hills = texture(heightTex, xz / heightTile).r * 2.0 - 1.0;
    } else {
        hills = sin(xz.x * 0.35) * cos(xz.y * 0.31) * 0.5
            + sin(xz.x * 0.13 + 1.7) * sin(xz.y * 0.17 + 0.6) * 0.5;
    }
    return hillAmp * m * hills;
}

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up (all post shaders)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 wn4 = invVP * vec4(ndc, -1.0, 1.0);
    vec4 wf4 = invVP * vec4(ndc, 1.0, 1.0);
    wn4 /= wn4.w;
    wf4 /= wf4.w;
    vec3 ro = wn4.xyz;
    vec3 rd = wf4.xyz - wn4.xyz;
    rd /= length(rd);

    float t = 0.0;
    bool hit = false;
    if (hillAmp == 0.0) {
        //opencode: flat plane at baseY — analytic (exact, no iteration).
        //   CPU mirrors this branch bit-for-bit (IEEE division both sides).
        //   Double-sided (planar mirror looks up from below): either ray
        //   sign hits; t>=0 rejects behind-the-origin.
        if (abs(rd.y) > 0.0) {
            t = (baseY - ro.y) / rd.y;
            hit = (t >= 0.0 && t <= 200.0);
        }
    } else {
        //opencode: sphere-trace with 0.7 safety scale + 12-step bisection
        //   on overshoot (same recipe as the raymarch SDF marcher). 64
        //   steps covers rays >= ~11deg below horizon; grazing rays miss
        //   (spike limitation, documented in paper 2).
        //   Double-sided: underside approaches (mirror from below) march
        //   on |f| and bisect on sign change; normal flips at shade time.
        {
            float f = ro.y - baseY - terrainH(ro.xz);
            float fPrev = f;
            for (int i = 0; i < 64; i++) {
                float stepLen = abs(f) * 0.7;
                t += stepLen;
                if (t > 200.0) break;
                vec3 p = ro + rd * t;
                f = p.y - baseY - terrainH(p.xz);
                if (abs(f) < 0.0001) { hit = true; break; }
                if ((f > 0.0) != (fPrev > 0.0)) {
                    float lo = t - stepLen;
                    float hi = t;
                    float fLo = fPrev;
                    for (int j = 0; j < 12; j++) {
                        float m = (lo + hi) * 0.5;
                        vec3 pm = ro + rd * m;
                        float fm = pm.y - baseY - terrainH(pm.xz);
                        if ((fm > 0.0) == (fLo > 0.0)) { lo = m; fLo = fm; }
                        else hi = m;
                    }
                    t = (lo + hi) * 0.5;
                    hit = true;
                    break;
                }
                fPrev = f;
            }
        }
    }
    if (!hit) discard;
    vec3 hitP = ro + rd * t;
    vec4 clip = viewProj * vec4(hitP, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;
    //opencode: spike 2 — shared-path shading: finite-difference normal,
    //   two-tone procedural albedo, sun + ambient + points, linear HDR
    //   (exposure, no tonemap — M9, same as standard.frag main). No
    //   specular (rough ground) and no SSAO/shadows (production work).
    //   CPU mirror Terrain_Shade in terrain.c must stay op-identical.
    float e = 0.06;
    float hx0 = baseY + terrainH(hitP.xz - vec2(e, 0.0));
    float hx1 = baseY + terrainH(hitP.xz + vec2(e, 0.0));
    float hz0 = baseY + terrainH(hitP.xz - vec2(0.0, e));
    float hz1 = baseY + terrainH(hitP.xz + vec2(0.0, e));
    vec3 nrm = normalize(vec3(hx0 - hx1, 2.0 * e, hz0 - hz1));
    //opencode: planar underside convention — do NOT flip for backfaces:
    //   the mirror looks up from below and must see top-lit terrain
    //   (flipping points the normal away from the sun and blacks out
    //   the mirror ground). CPU mirrors by not flipping either.
    float slope = 1.0 - nrm.y;
    vec3 alb = mix(vec3(0.30, 0.38, 0.14), vec3(0.36, 0.34, 0.30),
        smoothstep(0.02, 0.15, slope));
    alb = mix(alb, vec3(0.55, 0.54, 0.50), smoothstep(0.35, 0.65, hitP.y - baseY));
    vec3 L = normalize(lightPosition);
    vec3 col = alb * (ambientFactor * ambientColor.rgb);
    vec3 sunC = alb * (sunBrightness * sunColor.rgb) * max(dot(nrm, L), 0.0)
        * (sunDiffuseFactor * sunDiffuseTint.rgb);
    //opencode: gap-1 specular — Phong recipe mirror (standard.frag
    //   phong_specular): dielectric rough ground, shininess 24, Schlick
    //   F0 0.04. CPU mirror Terrain_Shade stays op-identical.
    vec3 specC = vec3(0.0);
    {
        vec3 V = normalize(ro - hitP);
        vec3 R = reflect(-L, nrm);
        float NdotR = max(dot(V, R), 0.0);
        float spec = pow(NdotR, 24.0);
        spec *= 0.04 + 0.96 * pow(1.0 - NdotR, 5.0);
        specC = (sunBrightness * sunColor.rgb) * spec;
    }
    //opencode: gap-2 shadows — CSM block mirror (standard.frag PBR sun):
    //   same selection, same PCF entry points, hitP for fragPosition.
    if (enableShadows == 1) {
        float shad;
        if (cascadeCount > 1 && csmSplits.x > 0.0) {
            float vDist = linearViewDist(gl_FragCoord.z, nearPlane, farPlane);
            int csmLayer = 0;
            vec4 lsPos = csmVP[0] * vec4(hitP, 1.0);
            if (vDist > csmSplits.z && cascadeCount >= 4) {
                csmLayer = 3;
                lsPos = csmVP[3] * vec4(hitP, 1.0);
            } else if (vDist > csmSplits.y && cascadeCount >= 3) {
                csmLayer = 2;
                lsPos = csmVP[2] * vec4(hitP, 1.0);
            } else if (vDist > csmSplits.x) {
                csmLayer = 1;
                lsPos = csmVP[1] * vec4(hitP, 1.0);
            }
            if (csmLayer > 0) {
                float sCur = gX_Shadow_PCF_Array(lsPos, nrm, L, csmShadowMapArray, csmLayer);
                float boundary = (csmLayer == 1) ? csmSplits.x :
                                 (csmLayer == 2) ? csmSplits.y : csmSplits.z;
                float sPrev;
                if (csmLayer == 1)
                    sPrev = gX_Shadow_PCF(csmVP[0] * vec4(hitP, 1.0), nrm, L, shadowMap);
                else {
                    vec4 prevLs = csmVP[csmLayer - 1] * vec4(hitP, 1.0);
                    sPrev = gX_Shadow_PCF_Array(prevLs, nrm, L, csmShadowMapArray, csmLayer - 1);
                }
                //opencode: match Phong's 10% split fade so seams do not pop.
                float blend = (csmBlendScale > 0.0)
                    ? smoothstep(boundary, boundary * (1.0 + 0.1 * csmBlendScale), vDist) : 1.0;
                shad = mix(sPrev, sCur, blend);
            } else {
                shad = gX_Shadow_PCF(csmVP[0] * vec4(hitP, 1.0), nrm, L, shadowMap);
            }
        } else {
            shad = gX_Shadow_PCF(sunVP * vec4(hitP, 1.0), nrm, L, shadowMap);
        }
        sunC *= (1.0 - shadowDiffuseBias * shad);
        specC *= (1.0 - shadowSpecularBias * shad);
    }
    col += sunC + specC;
    //opencode: gap-3 SSAO — pre-exposure linear multiply (Phong:869).
    {
        float ssaoF = (enableSSAO == 1)
            ? texture(ssaoMap, gl_FragCoord.xy / resolution).r : 1.0;
        col *= mix(1.0, ssaoF, 0.6);
    }
    for (int i = 0; i < 4; i++) {
        if (i >= pointCount) break;
        vec3 delta = pointPos[i] - hitP;
        float dist = length(delta);
        vec3 ld = delta / dist;
        float att = 1.0 / (pointAtten[i].x + pointAtten[i].y * dist
            + pointAtten[i].z * dist * dist);
        col += alb * (pointBright[i] * pointColor[i].rgb)
            * max(dot(nrm, ld), 0.0) * att * pointDiff[i];
    }
    col *= exposure;
    if (any(isnan(col))) col = vec3(0.0);
    //opencode: opaque rough ground — alpha carries roughness for SSR (M1).
    fragColor = vec4(col, 0.9);
}
