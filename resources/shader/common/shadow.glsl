//opencode: shared shadow mapping - PCSS (Percentage-Closer Soft Shadows) + PCF fallback.
//   Three-step PCSS algorithm: 1) blocker search -> 2) penumbra estimation -> 3) variable PCF.
//   Falls back to fixed 3?3 PCF when lightSize <= 0 or quality is MEDIUM/LOW.
//   lightSize = 0.0 gives the standard hard-edged PCF (current behavior).
//   lightSize > 0 enables PCSS with softness proportional to blocker distance.
//
//   Bias values from standard.frag (shadowBiasMax/shadowBiasMin).

#ifndef GX_COMMON_SHADOW_GLSL
#define GX_COMMON_SHADOW_GLSL

//opencode: oracle (Paper 1) - per-frame seed rotates the PCF/PCSS sample
//   pattern (rotated-grid PCF) so shadow edges carry varying signal that
//   temporal accumulation can converge. Deterministic per frame (CPU sets
//   seed = frame counter, NOT wall time - hermetic repeats stay bit-exact).
//   Default off: without accumulation the rotation reads as shimmer.
//   Declared here (the only includer is standard.frag) and uploaded with
//   the other model uniforms in model.c.
uniform float oracleSeed = 0.0;
uniform int oracleSeedActive = 0;
//opencode: oracle estimator (Paper 1) - single-light Monte Carlo pick.
//   Uniform pick x count scaling is an unbiased single-sample estimator of
//   the light sum (deterministic skips stay unbiased: E = sum/N * N).
//   Gate + scale live at the call sites; the sun stays exact (not picked).
uniform int oracleEstimateActive = 0;
//opencode: oracle multi-ray (Paper 1) - rays per pixel per frame (1..4).
//   K with-replacement picks averaged: unbiased, variance /K. Default 1 =
//   the original single-ray sequence exactly.
uniform int oracleRayCount = 1;
//opencode: Paper 1 M1 - cosine-hemisphere environment transport oracle.
//   This replaces only the diffuse IBL integral in standard.frag; the exact
//   irradiance cubemap remains the reference. Hook-gated and default off.
uniform int oraclePathActive = 0;

//opencode: shared deterministic hash for all Paper 1 stochastic samples.
uint gX_OracleHash(int salt) {
    uint x = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u
           + uint(oracleSeed + 0.5) * 26699u + uint(salt) * 40503u + 1u;
    x = (x ^ (x >> 16u)) * 0x45d9f3bu;
    x = (x ^ (x >> 16u)) * 0x45d9f3bu;
    x = x ^ (x >> 16u);
    return x;
}

//opencode: decorrelated per-pixel-per-frame pick in [0, count): integer
//   hash (splitmix-style mix) of pixel + frame seed + ray salt. Call sites
//   guarantee count > 0 (x % 0 is undefined). salt=0 reproduces the
//   original single-ray sequence exactly (added term vanishes).
int gX_OraclePick(int count, int salt) {
    uint x = gX_OracleHash(salt);
    return int(x % uint(count));
}

//opencode: deterministic [0,1) variate for cosine-hemisphere samples.
float gX_OracleRandom(int salt) {
    return (float(gX_OracleHash(salt) & 0x00ffffffu) + 0.5) / 16777216.0;
}

//opencode: seed rotation helper - rigid rotation of a tap offset by the
//   golden-ratio-hashed seed angle (well spread over consecutive frames).
vec2 gX_SeedRotate(vec2 off) {
    if (oracleSeedActive != 1) return off;
    float ang = fract(oracleSeed * 0.61803398875) * 6.28318530718;
    float ca = cos(ang);
    float sa = sin(ang);
    return mat2(ca, -sa, sa, ca) * off;
}

//opencode: Poisson disc sample positions (16 samples, deterministic for stability).
//   Used by PCSS blocker search and filtering to avoid regular grid artifacts.
const vec2 gX_PCSS_Samples[16] = vec2[16](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
    vec2(-0.203,  0.621), vec2( 0.164, -0.249), vec2( 0.566,  0.711),
    vec2( 0.457, -0.750), vec2( 0.422, -0.078), vec2(-0.527, -0.781),
    vec2(-0.594,  0.250), vec2( 0.836, -0.473), vec2( 0.281,  0.148),
    vec2(-0.121, -0.027), vec2( 0.715,  0.219), vec2( 0.234,  0.414),
    vec2(-0.078,  0.543)
);

//opencode: PCSS blocker search - find average depth of occluders in a search region.
//   shadowMap parameter passed through so helper has access.
float gX_PCSS_BlockerSearch(sampler2D sm, vec2 uv, float currentDepth, vec2 texelSize, float searchRadius)
{
    float blockerSum = 0.0;
    int blockerCount = 0;
    for (int i = 0; i < 16; i++) {
        vec2 sampleUV = uv + gX_PCSS_Samples[i] * texelSize * searchRadius;
        float sampleDepth = texture(sm, sampleUV).r;
        if (sampleDepth < currentDepth) {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }
    return (blockerCount > 0) ? (blockerSum / float(blockerCount)) : -1.0;
}

//opencode: PCSS PCF filter - sample shadow map with radius scaled to penumbra.
//   Falls back to fixed 3?3 when lightSize=0 or no blockers found.
float gX_PCSS_Filter(sampler2D sm, vec2 uv, float currentDepth, float bias,
    vec2 texelSize, float filterRadius, float lightSize)
{
    //opencode: if lightSize is 0, use fixed 3?3 PCF (backward compatible)
    if (lightSize <= 0.0 || filterRadius <= 0.0) {
        float shadow = 0.0;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                vec2 off = gX_SeedRotate(vec2(float(x), float(y)));
                float pcfDepth = texture(sm, uv + off * texelSize).r;
                shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            }
        }
        return shadow / 9.0;
    }

    //opencode: variable-size PCF with Poisson disc
    float shadow = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 sampleUV = uv + gX_SeedRotate(gX_PCSS_Samples[i]) * texelSize * filterRadius;
        float pcfDepth = texture(sm, sampleUV).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

//opencode: main shadow function - PCSS or fixed PCF depending on lightSize.
//   lightSize uniform controls the apparent size of the light source in
//   shadow-map texels. Larger values = softer shadows.
//   When lightSize <= 0, behaves identically to the original fixed 3?3 PCF.
//opencode: PCF with sampler2DArray (CSM array of cascades).
float gX_Shadow_PCF_Array(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir,
    sampler2DArray shadowMap, int layer)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(shadowBiasMax * (1.0 - dot(normal, lightDir)), shadowBiasMin);
    vec2 texelSize = vec2(1.0) / vec2(textureSize(shadowMap, 0).xy);
    vec3 uvw = vec3(projCoords.xy, float(layer));

    //opencode: simplified PCF for array texture
    //opencode: v0.1.19 - irregular Poisson disc (center + 8 taps, ~2.25-texel
    //   radius) replaces the axis-aligned 3?3 grid: same cost and footprint,
    //   but the non-uniform pattern breaks the stair-step banding on cascade
    //   shadow boundaries (visible as blocky edges at 1024? over 10-30m).
    float shadow = currentDepth - bias > texture(shadowMap, uvw).r ? 1.0 : 0.0;
    for (int i = 1; i < 9; i++) {
        vec2 sampleUV = projCoords.xy + gX_PCSS_Samples[i] * texelSize * 2.25;
        float pcfDepth = texture(shadowMap, vec3(sampleUV, float(layer))).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

float gX_Shadow_PCF(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir,
    sampler2D shadowMap)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(shadowBiasMax * (1.0 - dot(normal, lightDir)), shadowBiasMin);
    vec2 texelSize = vec2(1.0) / vec2(textureSize(shadowMap, 0));
    vec2 uv = projCoords.xy;

    //opencode: PCSS step 1 - blocker search (search radius 5 texels)
    float blockerAvg = gX_PCSS_BlockerSearch(shadowMap, uv, currentDepth, texelSize, 5.0);

    //opencode: PCSS step 2 - penumbra estimation
    float filterRadius = 1.0; // default = 1 texel (? current 3?3 PCF)
    if (blockerAvg > 0.0) {
        filterRadius = (currentDepth - blockerAvg) / blockerAvg * 2.0;
        filterRadius = clamp(filterRadius, 0.5, 16.0);
    }

    //opencode: PCSS step 3 - variable PCF filtering
    float shadow = gX_PCSS_Filter(shadowMap, uv, currentDepth, bias, texelSize, filterRadius, 1.0);

    return shadow;
}

float gX_Shadow_BlendFactor(float rawShadow,
    float shadowDiffuseBias, float shadowSpecularBias)
{
    return 1.0 - shadowDiffuseBias * rawShadow;
}

//opencode: map a point-shadow direction to its cube-array face and UV.
void gX_PointShadow_FaceUV(vec3 direction, out int face, out vec2 uv)
{
    vec3 a = abs(direction);
    if (a.x >= a.y && a.x >= a.z) {
        face = direction.x >= 0.0 ? 0 : 1;
        //opencode: the -X face follows the GL cube-map convention sc = +z
        //   (the +X face is sc = -z); a shared -z mirrored every -X lookup in
        //   Z, which displaced the shadow on one side of a point light (the
        //   "double shadow" ghost) and broke the PCF tap reprojection there.
        float sc = direction.x >= 0.0 ? -direction.z : direction.z;
        uv = vec2(sc, -direction.y) / a.x;
    } else if (a.y >= a.z) {
        face = direction.y >= 0.0 ? 2 : 3;
        uv = vec2(direction.x, direction.y >= 0.0 ? direction.z : -direction.z) / a.y;
    } else {
        face = direction.z >= 0.0 ? 4 : 5;
        uv = vec2(direction.z >= 0.0 ? direction.x : -direction.x, -direction.y) / a.z;
    }
    uv = uv * 0.5 + 0.5;
}

//opencode: extend a face-local PCF tap into a direction before selecting the
//   destination face, so taps crossing a cube edge read the adjacent layer.
vec3 gX_PointShadow_FaceDirection(int face, vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    if (face == 0) return vec3(1.0, -p.y, -p.x);
    if (face == 1) return vec3(-1.0, -p.y, p.x);
    if (face == 2) return vec3(p.x, 1.0, p.y);
    if (face == 3) return vec3(p.x, -1.0, -p.y);
    if (face == 4) return vec3(p.x, -p.y, 1.0);
    return vec3(-p.x, -p.y, -1.0);
}

//opencode: point light shadow sampling via a 2D array (layer = light*6 + face).
//   PCF taps are reprojected across cube-face boundaries instead of clamping
//   to the selected face, avoiding a clipped shadow edge at cube seams.
float gX_PointShadow_PCF(sampler2DArray sm, vec3 lightToFrag, float farPlane,
    float bias, int layerBase)
{
    float currentDepth = length(lightToFrag);
    vec3 dir = lightToFrag / currentDepth;
    int face;
    vec2 uv;
    gX_PointShadow_FaceUV(dir, face, uv);
    float stored = texture(sm, vec3(uv, float(layerBase + face))).r;
    float shadow = (currentDepth - bias > stored * farPlane) ? 1.0 : 0.0;
    vec2 texel = 1.0 / vec2(textureSize(sm, 0).xy);
    vec2 pcfOffsets[4] = vec2[](
        vec2( 0.5, 0.5), vec2(-0.5, 0.5),
        vec2( 0.5,-0.5), vec2(-0.5,-0.5)
    );
    for (int s = 0; s < 4; s++) {
        vec3 tapDir = gX_PointShadow_FaceDirection(face, uv + pcfOffsets[s] * texel);
        int tapFace;
        vec2 tapUV;
        gX_PointShadow_FaceUV(tapDir, tapFace, tapUV);
        float d = texture(sm, vec3(tapUV, float(layerBase + tapFace))).r;
        shadow += (currentDepth - bias > d * farPlane) ? 1.0 : 0.0;
    }
    return shadow / 5.0;
}

#endif //GX_COMMON_SHADOW_GLSL
