//opencode: primary fragment shader - full PBR lighting body ported from gfx.frag
#version 330
//opencode: QUALITY is injected at compile time by gX_BuildShaderDefines() from
//   gfx->state.shaderQuality (0=LOW, 1=MED, 2=HIGH). Controls compile-time
//   inclusion of expensive features: IBL, parallax self-shadow, refraction,
//   detail texture, rim lighting, PBR path (LOW forces Blinn-Phong).
//   Runtime uniform toggles still apply on top of compile-time gates.
#ifndef QUALITY
#define QUALITY 2  // default HIGH if not injected (development fallback)
#endif
//opencode: GX_GLFW_FRAG - injected by gX_BuildShaderDefines for GLFW backend.
//   Strips texture-sampler-heavy features where unbound samplers return NaN
//   on NVIDIA compat profile. Raylib backend is unaffected.
#ifndef GX_GLFW_FRAG
#define GX_GLFW_FRAG 0
#endif

uniform float exposure = 1.0;

//opencode: ACES filmic tone mapping (Narkowicz 2014 approximation) - wider
//   shoulder/rolloff than Reinhard, gives specular highlights a gentler
//   falloff and better mid-tone contrast.
vec3 aces_tonemap(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
//opencode: shadow bias uniforms - configurable from C (was hardcoded 0.015/0.0025)
uniform float shadowBiasMax = 0.015;
uniform float shadowBiasMin = 0.0025;
#include "common/fog.glsl"
#include "common/shadow.glsl"
//gX_STANDARD_UNIFORMS

in VS_OUT {
    vec3  fragPosition;
    vec2  fragTexCoord;
    vec2  fragTexCoord2;
    vec3  fragNormal;
    vec3  fragTangent;
    vec4  fragColor;
    float fragDistance;
    mat3  matTNB;
    vec3  tangentLightPosition;
    vec3  tangentViewPosition;
    vec3  tangentFragPosition;
    vec4  lightspaceFragPosition;
    vec3  gouraudLight;
} fs_in;

//opencode: flat shading - non-interpolated normal from provoking vertex
flat in vec3 flatNormal;

uniform int enableGouraud = 0;
uniform int enableFlat = 0;
uniform int ssrNormalPass = 0; //opencode: emit interpolated normal for opt-in SSR
uniform int enableLightingAmbient = 1;
uniform int enableLightingDiffuse = 1;
uniform int enableLightingSpecular = 1;

uniform int enableMapAlbedo = 1;
uniform int enableMapMetalness = 1;
uniform int enableMapNormal = 1;
uniform int enableMapParallax = 1;

uniform int enablePhong = 1;
uniform int enableFog = 1;
uniform int enableShadows = 1;

uniform int showPosition = 0;
uniform int showTexCoord = 0;
uniform int showTexCoord2 = 0;
uniform int showNormal = 0;
uniform int showColor = 0;
uniform int showTangent = 0;

//opencode: uniform diagnostic mode - encodes uniform values into output color
//   Mode 0: normal rendering (default)
//   Mode 1: output vec3(albedoFactor, 0.0, 0.0) as RGB
//   Mode 2: output vec3(metalnessFactor, 0.0, 0.0) as RGB
//   Mode 3: output normalize(abs(lightPosition[0].xyz)) as RGB
uniform int diagUniformMode = 0;
//ai: per-resource diagnostic output for the bound base-color map.
uniform int debugBaseColorMap = 0;

//opencode: per-model material uniforms - individual declarations (the only
//   path; a UBO alternative was removed after proving its uniform block
//   never links on this GL config).
uniform int hasAlbedoMap = 0;
uniform int hasMetalnessMap = 0;
uniform int hasNormalMap = 0;
uniform int hasHeightMap = 0;
uniform int hasEmissiveMap = 0;
uniform int hasRoughnessMap = 0;
uniform int hasAoMap = 0;
uniform int hasGlossMap = 0;
uniform int parallaxMode = 2;
uniform int enableReflections = 1;
//opencode: SSR receiving is independent of direct environment shading.
uniform int receiveSSR = 1;
uniform int enableAlphaTest = 0;
uniform int enableRefraction = 0;
//ai: opt-in environment replacement keeps the historical additive reflection default.
uniform int balancedReflection = 0;
uniform int planarReflection = 0;
uniform int mirrorFill = 0; //opencode: 1 inside the planar mirror fill: emit linear HDR (skip tonemap/exposure/SSAO) so the RT is tonemapped once, in main
//opencode: non-planar reflection - per-fragment local-plane lookup (GTA4/5
//   style): reflect the view ray across the fragment's own plane and
//   intersect the global mirror plane. Flat surfaces are unchanged.
uniform int nonPlanarReflection = 0;
uniform vec3 reflectionPlaneNormal = vec3(0.0, 1.0, 0.0);
uniform float reflectionPlaneHeight = 0.0;
uniform int usePBR = 0;
//opencode: Paper 1 M2 — opt-in diffuse-only raster parity fixture.
uniform int oracleDiffuseOnly = 0;
//opencode: Paper 1 M3 — opt-in direct GGX parity fixture.
uniform int oracleGgx = 0;
//opencode: Paper 1 M4 — opt-in one-secondary environment path.
uniform int oracleBounce = 0;

uniform float albedoFactor;
uniform float metalnessFactor;
uniform float modelShininess = 32.0;
uniform float emissiveFactor = 1.0;
uniform float roughness = 0.5;
uniform float ao = 1.0;
uniform float blendOpacity = 1.0;
uniform float alphaCutoff = 0.5;
uniform int roughnessAlpha = 0; //opencode: M1 - transparent repair pass flag
uniform float refractiveIndex = 1.0;

uniform float albedoMapFactor = 1.0;
uniform float metalnessMapFactor = 1.0;
uniform float emissiveMapFactor = 1.0;
uniform float roughnessMapFactor = 1.0;
uniform float aoMapFactor = 1.0;
uniform float normalMapFactor = 1.0;
uniform float heightMapFactor = 1.0;

uniform vec2 albedoMapScale = vec2(1.0,1.0);
uniform vec2 albedoMapOffset = vec2(0.0,0.0);
uniform vec2 metalnessMapScale = vec2(1.0,1.0);
uniform vec2 metalnessMapOffset = vec2(0.0,0.0);
uniform vec2 emissiveMapScale = vec2(1.0,1.0);
uniform vec2 emissiveMapOffset = vec2(0.0,0.0);
uniform vec2 roughnessMapScale = vec2(1.0,1.0);
uniform vec2 roughnessMapOffset = vec2(0.0,0.0);
uniform vec2 aoMapScale = vec2(1.0,1.0);
uniform vec2 aoMapOffset = vec2(0.0,0.0);
uniform vec2 normalMapScale = vec2(10.0,10.0);
uniform vec2 normalMapOffset = vec2(0.0,0.0);
uniform vec2 heightMapScale = vec2(10.0,10.0);
uniform vec2 heightMapOffset = vec2(0.0,0.0);

uniform vec4 emissiveColor = vec4(1.0);
//opencode: editor markers need a genuinely unlit output on both the full and
//   GLFW compatibility shader paths, which intentionally have different lighting.
uniform int lightMarkerMode = 0;
uniform vec3 specularColor = vec3(1.0);
uniform float envAmbientFactor = 0.3;

//opencode: configurable tuning constants (were hardcoded in shader body)
uniform float parallaxMinLayers = 48.0;
uniform float parallaxMaxLayers = 64.0;
uniform float parallaxOffset = 0.2;
uniform float parallaxShadowOffset = 0.15;
uniform float parallaxShadowSteps = 16.0;
uniform float pointShadowBias = 0.025;
uniform float pointShadowExtra = 0.01;
uniform float pointShadowMinBias = 0.01;
uniform float tangentLengthThreshold = 0.001;
uniform float normalYThreshold = 0.999;

//opencode: material maps - ordered by importance. On GPUs with limited sampler
//   slots (GL_MAX_FRAGMENT_IMAGE_UNIFORMS), the driver keeps the first N declared.
//   shadowMap/csmShadowMap0 first (critical for lighting), then common maps.
uniform sampler2D shadowMap;
uniform sampler2DArray csmShadowMapArray; //opencode: CSM cascade shadow maps (4 layers)
uniform mat4 csmVP[4];          //opencode: cascade VP matrices
uniform vec3 csmSplits = vec3(0.0); //opencode: split distances x=0->1, y=1->2, z=2->3
uniform float csmBlendScale = 1.0;
uniform int cascadeCount = 1;   //opencode: number of shadow cascades (1=standard, 2..4=CSM)
uniform sampler2D albedoMap, normalMap, metalnessMap;
//ai: explicit two-albedo terrain contract; macro is non-color and COLOR_0 remains tint.
in float terrainMask;
uniform int terrainBlend = 0;
uniform int terrainHasDetail = 0;
uniform sampler2D terrainAlbedoMap, terrainDetailMap;
uniform float terrainDetailScale = 0.1;
uniform sampler2D roughnessMap, glossMap, occlusionMap, emissionMap, heightMap;
//opencode: opt-in static lightmap, sampled with TEXCOORD_1. The baker writes
//   linear additive irradiance; production remains disabled by hasLightmap=0.
uniform sampler2D lightmapMap;
uniform int hasLightmap = 0;
uniform float lightmapStrength = 1.0;
uniform vec2 lightmapScale = vec2(1.0);
uniform vec2 lightmapOffset = vec2(0.0);
#if QUALITY >= 1
uniform sampler2D bdrfMap; //opencode: BRDF LUT - only needed for IBL (QUALITY >= 1)
#endif
uniform samplerCube cubeMap;
#if QUALITY >= 1
uniform samplerCube irradianceMap, prefilterMap; //opencode: IBL - only needed for QUALITY >= 1
uniform sampler2D fogLutMap;    //opencode: v0.3.8 - equirect fog delta LUT
uniform int hasFogLut = 0;      //opencode: v0.3.8
#endif
uniform vec3 cubePos = vec3(0.0); //opencode: parallax-corrected cubemap center
uniform vec3 cubeSize = vec3(30.0); //opencode: bounding box half-extents
//ai: opt-in single-pixel probe trace; modes expose box direction and raw A/B samples.
uniform int gXProbeTraceMode = 0;
//ai: candidate correction remains opt-in until unchanged reflection gates admit it.
uniform int gXProbeBoxCorrect = 0;
//ai: direction-only captures must not acquire finite-box positional parallax.
uniform int gXProbeSkyOnlyA = 0;
uniform int gXProbeSkyOnlyB = 0;
//ai: isolated capture alpha: 1=coverage, 2=world radial distance; scene retains SSR roughness.
uniform int gXProbeCoverageCapture = 0;
//ai: diagnostic-only authored solid fixture; no reflected RGB replacement.
uniform int gXProbeProxyDiagnostic = 0;
//ai: isolated direction-only resource, reusing the managed SSR sky LUT on unit 23.
uniform sampler2D gXProbeProxySky;
uniform int gXProbeProxySkyReady = 0;
uniform vec4 gXProbeTracePixel = vec4(-1.0);
vec3 gXProbeTraceRefDirection;
vec3 gXProbeTraceSampleDirection;
vec3 gXProbeTraceSampleDirectionB;
vec3 gXProbeTraceSampleA;
vec3 gXProbeTraceSampleB;
vec3 gXProbeTraceBlendedSample;
vec3 gXProbeTraceWorldPosition;
vec3 gXProbeTraceCubeCenter;
vec3 gXProbeTraceCubeCenterB;
vec3 gXProbeTraceCubeSize;
vec3 gXProbeTraceExactDirectionA;
vec3 gXProbeTraceExactDirectionB;
vec3 gXProbeTraceExactSampleA;
vec3 gXProbeTraceExactSampleB;
vec3 gXProbeTraceExactBlendedSample;
int gXProbeTraceHasSecondary;
int gXProbeTraceValid;
//opencode: probe-grid blend — second-nearest probe (unit 31, MAIN pass).
//   probeBlendB is B's weight (0 = nearest only); per-model uniform, so the
//   skip branch below is draw-call-coherent, never divergent.
uniform samplerCube cubeMapB;
uniform vec3 cubePosB = vec3(0.0);
uniform float probeBlendB = 0.0;
uniform sampler2D reflTex;
#if QUALITY >= 1
uniform sampler2D bgTex; //opencode: background for refraction - only needed for QUALITY >= 1
#endif
uniform mat4 reflMVP;
//opencode: point light shadow map - single sampler2DArray (layer = light*6+face).
//   Works on GL 3.3+ (no GL_ARB_texture_cube_map_array needed), replaces the
//   32-sampler cubemap array (unit + sampler pressure).
uniform sampler2DArray pointShadowMaps;

//opencode: detail texture (HL2/Source style) - only needed for QUALITY >= 1
#if QUALITY >= 1
uniform sampler2D detailMap;
#endif
uniform float detailUVScale = 8.0;
uniform float detailStrength = 0.0;
uniform float detailDistFade = 20.0;
//opencode: SSAO ambient occlusion - declared unconditionally so the final
//   color multiply compiles at every quality level; sampling is gated by
//   enableSSAO (0 at LOW quality).
uniform sampler2D ssaoMap;
uniform int enableSSAO = 0;
//opencode: contact shadows (spike 2) - short-range sun occlusion map
//   (screen space, half-res). Gated by enableContact (0 at LOW quality
//   and unless GX_T_CONTACTAPPLY opts in).
uniform sampler2D contactMap;
uniform int enableContact = 0;
uniform float pointShadowFarPlanes[32];
uniform float pointShadowRanges[32]; //opencode: per-light shadow bounding range, 0=unlimited

uniform float fogNear;
uniform float fogFar;
uniform vec4  fogColor;
uniform float heightFogDensity = 0.0;
uniform float heightFogBase = 0.0;

uniform vec4  sunColor = vec4(1,1,1,1), sunDiffuseTint = vec4(1,1,1,1), sunSpecularTint = vec4(1,1,1,1);
uniform float sunBrightness = 1.0;
uniform float sunDiffuseFactor = 1.0;
uniform float sunSpecularFactor = 1.0;

//opencode: rim/back lighting - edge glow for character pop (HL2, GTA4 era)
uniform vec4  rimColor = vec4(1.0);
uniform float rimIntensity = 0.5;
uniform float rimPower = 2.0;

uniform vec4 albedoColor, metalnessColor;

uniform int lightsAmount = 0;
uniform int clusteredEnabled = 0; //opencode: v0.5.0 — grid consumer (RG32UI bitmask)
uniform usampler2D lightGridTex;  //   RG32UI grid bound to unit 29 by shader.c
uniform int lightsType[32];
uniform float lightsFov[32];
uniform float lightsBrightness[32];
uniform vec3 lightsPos[32];
uniform vec3 lightsDir[32];
uniform vec4 lightsColor[32];

//opencode: v0.5.0 — SSBO light data (GX_COMPUTE_SSBO build). 7x vec4 per light,
//   std430 (no padding): pos+type, dir+fov, color, atten+brightness, diff/spec+
//   flags/range, far plane, and cookie atlas rect.
//   Uniform arrays above stay declared (unused in SSBO mode) so one source
//   compiles for both paths. The per-tile mask slot indexes this buffer; heap
//   order == uniform-array order, so parity with the 3.3 path is exact.
#ifdef GX_COMPUTE_SSBO
layout(std430, binding = 0) buffer LightDataBlock {
    vec4 lightsData[];
} lightDataBlock;
#define L_POS(i)    lightDataBlock.lightsData[(i)*7 + 0].xyz
#define L_TYPE(i)   int(lightDataBlock.lightsData[(i)*7 + 0].w)
#define L_DIR(i)    lightDataBlock.lightsData[(i)*7 + 1].xyz
#define L_FOV(i)    lightDataBlock.lightsData[(i)*7 + 1].w
#define L_COLOR(i)  lightDataBlock.lightsData[(i)*7 + 2]
#define L_ATTEN(i)  lightDataBlock.lightsData[(i)*7 + 3]
#define L_BRIGHT(i) lightDataBlock.lightsData[(i)*7 + 3].w
#define L_DIFF(i)   lightDataBlock.lightsData[(i)*7 + 4].x
#define L_SPEC(i)   lightDataBlock.lightsData[(i)*7 + 4].y
#define L_SHADOWRANGE(i) lightDataBlock.lightsData[(i)*7 + 4].w
#define L_FARPLANE(i)    lightDataBlock.lightsData[(i)*7 + 5].x
#define L_COOKIE(i)      ((int(lightDataBlock.lightsData[(i)*7 + 4].z) & 4) != 0)
#define L_COOKIERECT(i)  lightDataBlock.lightsData[(i)*7 + 6]
#else
#define L_POS(i)    lightsPos[i]
#define L_TYPE(i)   lightsType[i]
#define L_DIR(i)    lightsDir[i]
#define L_FOV(i)    lightsFov[i]
#define L_COLOR(i)  lightsColor[i]
#define L_ATTEN(i)  vec4(lightsAttenConst[i], lightsAttenLinear[i], lightsAttenQuad[i], 0.0)
#define L_BRIGHT(i) lightsBrightness[i]
#define L_DIFF(i)   lightsDiffuseFactor[i]
#define L_SPEC(i)   lightsSpecularFactor[i]
#define L_SHADOWRANGE(i) pointShadowRanges[i]
#define L_FARPLANE(i)    pointShadowFarPlanes[i]
#define L_COOKIE(i)      (lightsCookieEnabled[i] != 0)
#define L_COOKIERECT(i)  lightsCookieRects[i]
#endif
uniform float lightsAttenConst[32];
uniform float lightsAttenLinear[32];
uniform float lightsAttenQuad[32];
uniform float lightsDiffuseFactor[32];
uniform float lightsSpecularFactor[32];

uniform int lightsCookieEnabled[32];
uniform sampler2D lightsCookieAtlas; //opencode: packed cookie atlas (4x4 grid)
uniform vec4 lightsCookieRects[32];  //opencode: per-light UV rect into the atlas

uniform int shadowMode = 0;
uniform float shadowDiffuseBias = 0.5;
uniform float shadowSpecularBias = 1.0;

uniform float reflectionBias = 0.5;
uniform vec2 resolution;
uniform vec3 cameraPosition, cameraTarget, cameraUp;
uniform float cameraFov;

out vec4 finalColor;
layout(location = 1) out float outRoughness; //opencode: per-pixel roughness for SSR

struct Positions {
    vec3 lightpos;
    vec3 viewpos;
    vec3 fragpos;
};

struct LightInfo {
    float brightness;
    vec4 color;
};

void calc_positions(inout Positions pos) {
    pos.lightpos = lightPosition;
    pos.viewpos = viewPosition;
    pos.fragpos = fs_in.fragPosition;

    if ((enableMapNormal == 1) && (hasNormalMap == 1)) {
        pos.lightpos = fs_in.tangentLightPosition;
        pos.viewpos = fs_in.tangentViewPosition;
        pos.fragpos = fs_in.tangentFragPosition;
    }
}

//opencode: static baked irradiance contribution. It is deliberately additive
//   rather than a replacement for dynamic lighting, so existing forward-light
//   scenes keep their behavior and baked maps can carry indirect/static fill.
//ai: decode/encode only opted-in terrain, leaving the legacy material color pipeline intact.
vec3 gX_TerrainLinear(vec3 value) {
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
        step(vec3(0.04045), value));
}
vec3 gX_TerrainEncoded(vec3 value) {
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
        step(vec3(0.0031308), value));
}
vec3 gX_MaterialAlbedo(vec2 uv) {
    vec3 base = texture(albedoMap, uv).rgb;
    if (terrainBlend == 0) return base;
    vec3 mixed = mix(gX_TerrainLinear(base),
        gX_TerrainLinear(texture(terrainAlbedoMap, uv).rgb), clamp(terrainMask, 0.0, 1.0));
    vec3 macro = terrainHasDetail == 1
        ? texture(terrainDetailMap, uv * terrainDetailScale).rgb * 2.0 : vec3(1.0);
    return gX_TerrainEncoded(clamp(mixed * macro, 0.0, 1.0));
}

vec3 gX_LightmapContribution(vec3 albedo, vec2 texCoords) {
    if (hasLightmap == 0) return vec3(0.0);
    if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
        vec2 albedoUV = albedoMapScale * texCoords - albedoMapOffset;
        albedo *= albedoMapFactor * gX_MaterialAlbedo(albedoUV);
    }
    vec2 uv = lightmapScale * fs_in.fragTexCoord2 - lightmapOffset;
    vec3 irradiance = max(texture(lightmapMap, uv).rgb, vec3(0.0));
    return albedo * irradiance * max(lightmapStrength, 0.0);
}

vec3 calc_normal(vec2 texCoords) {
    vec3 normal = (enableFlat == 1) ? flatNormal : fs_in.fragNormal.xyz;
    //opencode: fallback for zero normals - some models' normals don't reach
    //   the GPU from the VBO (UV mesh upload issue). Default to up-vector.
    if (length(normal) < 0.001) normal = vec3(0.0, 1.0, 0.0);
    if ((enableMapNormal == 1) && (hasNormalMap == 1)) {
        vec2 coord = normalMapScale * texCoords - normalMapOffset;
        vec3 mapNormal = texture(normalMap, coord).rbg;
        mapNormal = normalize(mapNormal * 2.0 - 1.0);
        normal = mix(vec3(0.0,1.0,0.0), mapNormal, normalMapFactor);
    }
    return normal;
}

//opencode: SSR normal target — preserve the interpolated vertex normal and
//   apply the material normal map in world space without changing production
//   lighting's legacy tangent-space path.
vec3 calc_ssr_normal(vec2 texCoords) {
    vec3 normal = (enableFlat == 1) ? flatNormal : fs_in.fragNormal;
    if (length(normal) < 0.001) normal = vec3(0.0, 1.0, 0.0);
    if ((enableMapNormal == 1) && (hasNormalMap == 1)) {
        vec2 coord = normalMapScale * texCoords - normalMapOffset;
        vec3 mapNormal = texture(normalMap, coord).rbg;
        mapNormal = normalize(mapNormal * 2.0 - 1.0);
        mapNormal = normalize(mix(vec3(0.0, 1.0, 0.0), mapNormal, normalMapFactor));
        normal = normalize(transpose(fs_in.matTNB) * mapNormal);
    }
    return normalize(normal);
}

//opencode: linear view-space distance from NDC depth (for CSM cascade selection)
float linearViewDist(float ndcZ, float n, float f) {
    float z_ndc = ndcZ * 2.0 - 1.0;
    return abs((2.0 * n * f) / (f + n - z_ndc * (f - n)));
}

float gX_Parallax_ReadHeight(vec2 uv) {
    vec2 scale = max(heightMapScale, vec2(0.001));
    float h = texture(heightMap, scale * uv - heightMapOffset).r;
    return clamp((h - 0.4) / 0.15, 0.0, 1.0);
}

//opencode: simple parallax (single tap) - kept for low-end fallback
//   (parallaxMode == 0). Default path is ParallaxMapping2 (steep + POM,
//   48-64 layers + jitter) which has been the live call site since the
//   standard.frag rewrite. Self-shadow is a separate per-model flag.
vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir) {
    vec2 scale = max(heightMapScale, vec2(0.001));
    float hs = 0.02 * max(heightMapFactor, 0.0);
    float height = gX_Parallax_ReadHeight(texCoords);
    vec2 p = vec2(viewDir.x, -viewDir.z) / viewDir.y * (height * hs / scale.x);
    return texCoords - p;
}

vec2 ParallaxMapping2(vec2 texCoords, vec3 viewDir) {
    float hs = 0.05 * max(heightMapFactor, 0.0);
    vec2 scale = max(heightMapScale, vec2(0.001));
    float minLayers = parallaxMinLayers;
    float maxLayers = parallaxMaxLayers;
    float numLayers = mix(maxLayers, minLayers, max(dot(vec3(0.0, 1.0, 0.0), viewDir), 0.0));
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    vec2 P = vec2(viewDir.x, -viewDir.z) / max(viewDir.y, 0.001) * hs / scale.x;
    float maxOffset = parallaxOffset / scale.x;
    float pLen = length(P);
    if (pLen > maxOffset) P *= maxOffset / pLen;
    vec2 deltaTexCoords = P / numLayers;

    //opencode: jitter starting depth to break up concentric ring aliasing
    vec2  currentTexCoords     = texCoords;
    float currentDepthMapValue = gX_Parallax_ReadHeight(currentTexCoords);
    float jitter = fract(sin(dot(texCoords * scale + fract(gl_FragCoord.xy * 0.01), vec2(12.9898, 78.233))) * 43758.5453);
    currentLayerDepth += jitter * layerDepth * 0.5;

    while (currentLayerDepth < currentDepthMapValue) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = gX_Parallax_ReadHeight(currentTexCoords);
        currentLayerDepth += layerDepth;
    }

    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = gX_Parallax_ReadHeight(prevTexCoords) - currentLayerDepth + layerDepth;
    float weight = afterDepth / clamp(afterDepth - beforeDepth, 0.001, 1.0);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}

//opencode: parallax self-shadowing - trace light ray through height map
float ParallaxShadow(vec2 texCoords, vec3 tangentLightDir) {
    if (hasHeightMap == 0 || enableMapParallax == 0) return 1.0;
    
    float hs = 0.05 * max(heightMapFactor, 0.0);
    vec2 scaleV = max(heightMapScale, vec2(0.001));
    //opencode: keep the sign of tangentLightDir.y - for lights BELOW the surface
    //   (point lights under a floor), the ray should walk in -UV, not +UV.
    vec2 P = vec2(tangentLightDir.x, -tangentLightDir.z) / max(tangentLightDir.y, 0.001) * hs / scaleV.x;
    float maxOffset = parallaxShadowOffset / scaleV.x;
    float pLen = length(P);
    if (pLen > maxOffset) P *= maxOffset / pLen;
    
    float shadows = parallaxShadowSteps;
    vec2 deltaTexCoords = P / shadows;
    float layerDepth = 1.0 / shadows;
    
    vec2 currentTexCoords = texCoords;
    float currentLayerDepth = 0.0;
    float currentDepth = gX_Parallax_ReadHeight(currentTexCoords);
    
    float shadow = 1.0;
    
    for (float s = 0.0; s < shadows; s++) {
        currentTexCoords -= deltaTexCoords;
        currentLayerDepth += layerDepth;
        float sampleDepth = gX_Parallax_ReadHeight(currentTexCoords);
        
        if (sampleDepth > currentLayerDepth) {
            // Surface occludes light - partially shadowed
            shadow = 0.4;
            break;
        }
    }
    
    return shadow;
}

vec3 phong_ambient(vec2 tex) {
    vec3 ambient = ambientFactor * ambientColor.rgb;
    if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
        vec2 coord = albedoMapScale * tex - albedoMapOffset;
        ambient *= albedoMapFactor * gX_MaterialAlbedo(coord);
    }
    //opencode: SSAO modulation - multiply ambient by the SSAO factor
    //   (1.0 = no occlusion, 0.0 = fully occluded). Sampled with screen-space
    //   UV (no parallax shift) since SSAO is in screen space. Disabled
    //   when enableSSAO is 0 (cost is one texture sample).
#if QUALITY >= 1
    if (enableSSAO == 1) {
        ambient *= texture(ssaoMap, gl_FragCoord.xy / resolution).r;
    }
#endif
    return ambient;
}

vec3 phong_diffuse(vec3 normal, vec3 lightdir, vec4 col, float brightness, vec2 tex) {
    vec3 diffuse = vec3(albedoFactor);
    diffuse *= (brightness * col.rgb);
    diffuse *= max(dot(normal.xyz, lightdir), 0.0);
    if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
        vec2 coord = albedoMapScale * tex - albedoMapOffset;
        diffuse *= albedoMapFactor * gX_MaterialAlbedo(coord);
    }
    return diffuse;
}

vec3 phong_specular(vec3 normal, vec3 lightdir, vec3 viewdir, vec4 lightColor, float lightBrightness, vec2 tex) {
    vec3 reflectDir = reflect(-lightdir, normal.xyz);
    float NdotR = max(dot(viewdir, reflectDir), 0.0);
    //opencode: RGB specular color (was grayscale metalnessFactor)
    vec3 spec = specularColor * metalnessFactor;
    spec *= (lightBrightness * lightColor.rgb);
    //opencode: per-pixel gloss map modulates shininess
    float shininess = modelShininess;
    if (hasGlossMap == 1) {
        shininess *= texture(glossMap, tex).r;
    }
    spec *= pow(NdotR, shininess);
    //opencode: Schlick Fresnel approximation - F0=0.04 dielectric default
    //   Brightens specular at grazing angles, characteristic of HL2/Crysis era.
    float fresnel = 0.04 + 0.96 * pow(1.0 - NdotR, 5.0);
    spec *= fresnel;
    if (enableMapMetalness == 1 && hasMetalnessMap == 1) {
        vec2 coord = metalnessMapScale * tex - metalnessMapOffset;
        spec *= metalnessMapFactor * texture(metalnessMap, coord).rgb;
    }
    return spec;
}

//opencode: phong_shadow replaced by shared gX_Shadow_PCF from common/shadow.glsl
//   The shared function uses the same improved bias values and PCF kernel.

void phong(inout vec4 color, in vec3 normal, inout Positions pos, vec2 tex, float parShadow) {
    vec3 lightDir = normalize(pos.lightpos);
    vec3 viewDir = normalize(pos.viewpos - pos.fragpos);

    vec3 ambient  = vec3(0.0,0.0,0.0);
    vec3 diffuse  = vec3(0.0,0.0,0.0);
    vec3 specular = vec3(0.0,0.0,0.0);
    float shadow = 0.0;

    if (enableLightingAmbient == 1) ambient = phong_ambient(tex);

    //opencode: environment-aware ambient - blend cubemap into flat ambient term
    if (enableReflections == 1 && envAmbientFactor > 0.0) {
        vec3 env = texture(cubeMap, normal).rgb;
        ambient = mix(ambient, ambient * env * 2.0, envAmbientFactor);
    }

    if (enableLightingDiffuse == 1) diffuse = phong_diffuse(normal, lightDir, sunColor, sunBrightness, tex) * (sunDiffuseFactor * sunDiffuseTint.rgb);
    if (enableLightingSpecular == 1) specular = phong_specular(normal, lightDir, viewDir, sunColor, sunBrightness, tex) * (sunSpecularFactor * sunSpecularTint.rgb);
    //opencode: CSM-aware shadow sampling (up to 4 cascades)
    if (enableShadows == 1) {
        if (cascadeCount > 1 && csmSplits.x > 0.0) {
            float vDist = linearViewDist(gl_FragCoord.z, nearPlane, farPlane);
            int csmLayer = 0;
            vec4 lsPos = fs_in.lightspaceFragPosition;
            if (vDist > csmSplits.z && cascadeCount >= 4) {
                csmLayer = 3;
                lsPos = csmVP[3] * vec4(fs_in.fragPosition, 1.0);
            } else if (vDist > csmSplits.y && cascadeCount >= 3) {
                csmLayer = 2;
                lsPos = csmVP[2] * vec4(fs_in.fragPosition, 1.0);
            } else if (vDist > csmSplits.x) {
                csmLayer = 1;
                lsPos = csmVP[1] * vec4(fs_in.fragPosition, 1.0);
            }
            if (csmLayer > 0) {
                float sCur = gX_Shadow_PCF_Array(lsPos, normal, lightDir, csmShadowMapArray, csmLayer);
                //opencode: soft blend across the cascade boundary - sample the
                //   previous cascade and mix in a band above the split distance
                //   (prev layer 0 uses the original lightspace coords/shadowMap)
                float boundary = (csmLayer == 1) ? csmSplits.x :
                                 (csmLayer == 2) ? csmSplits.y : csmSplits.z;
                float sPrev = 0.0;
                if (csmLayer == 1) {
                    sPrev = gX_Shadow_PCF(fs_in.lightspaceFragPosition, normal, lightDir, shadowMap);
                } else {
                    vec4 prevLs = csmVP[csmLayer - 1] * vec4(fs_in.fragPosition, 1.0);
                    sPrev = gX_Shadow_PCF_Array(prevLs, normal, lightDir, csmShadowMapArray, csmLayer - 1);
                }
                float blend = (csmBlendScale > 0.0)
                    ? smoothstep(boundary, boundary * (1.0 + 0.1 * csmBlendScale), vDist) : 1.0;
                shadow += mix(sPrev, sCur, blend);
            } else {
                shadow += gX_Shadow_PCF(lsPos, normal, lightDir, shadowMap);
            }
        } else {
            shadow = gX_Shadow_PCF(fs_in.lightspaceFragPosition, normal, lightDir, shadowMap);
        }
    }

    for (int i = 0; i < lightsAmount; i++) {
        //opencode: world-space range test. shadowRanges are uploaded per-light as
        //   world units; check against world-space frag->light distance (pos.fragpos
        //   is in world space when normal maps are inactive, and matTNB would convert
        //   it, so we re-derive world-space distance from the raw lightsPos).
        float worldDist = length(fs_in.fragPosition - L_POS(i));
        if (L_SHADOWRANGE(i) > 0.0 && worldDist > L_SHADOWRANGE(i)) continue;

        vec3 lp = L_POS(i);
        if ((enableMapNormal == 1) && (hasNormalMap == 1)) lp = fs_in.matTNB * lp;
        vec3 delta = lp - pos.fragpos;
        float dist = length(delta);
        vec3 ldir = delta / dist;
        //ai: unused heap holes upload all-zero attenuation; skip them before
        //   the reciprocal so a valid later slot cannot be poisoned by inf*0.
        float attenDenom = L_ATTEN(i).x + L_ATTEN(i).y * dist
            + L_ATTEN(i).z * dist * dist;
        if (attenDenom <= 0.0) continue;
        float atten = 1.0 / attenDenom;

        //opencode: spot light cone falloff - smoothstep from inner to outer cone
        if (L_TYPE(i) == 2) {
            vec3 worldLightDir = normalize(L_DIR(i));
            vec3 worldToFrag = normalize(fs_in.fragPosition - L_POS(i));
            float cosAng = max(dot(worldToFrag, worldLightDir), 0.0);
            float halfAngle = L_FOV(i) * 0.5 * 0.0174533;
            atten *= smoothstep(cos(halfAngle), cos(halfAngle * 0.9), cosAng);
            //opencode: light cookie - projected texture for spot lights.
            //   Always sample the texture (compiler can't eliminate it) but
            //   mix with 1.0 when cookie is disabled so atten is unchanged.
            {
                vec3 L = worldLightDir;
                vec3 U = normalize(cross(abs(L.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0), L));
                vec3 V = cross(L, U);
                float d = max(cosAng, 0.001);
                vec2 cookieUV = vec2(dot(worldToFrag, U), dot(worldToFrag, V)) / d;
                cookieUV = cookieUV * 0.5 + 0.5;
                float cookieSample = 1.0;
                if (all(greaterThanEqual(cookieUV, vec2(0.0))) && all(lessThanEqual(cookieUV, vec2(1.0)))) {
                    cookieSample = texture(lightsCookieAtlas, L_COOKIERECT(i).xy + cookieUV * L_COOKIERECT(i).zw).r;
                }
                atten *= mix(1.0, cookieSample, float(L_COOKIE(i)));
            }
        }

        //opencode: point light shadow sampling - sampler2DArray (layer = light*6+face)
        float pointShadow = 0.0;
        if (L_FARPLANE(i) > 0.0) {
            vec3 ltf = fs_in.fragPosition - L_POS(i);
            if (L_SHADOWRANGE(i) > 0.0 && length(ltf) > L_SHADOWRANGE(i)) {
                pointShadow = 0.0;
            } else {
                float bias = pointShadowBias;
                bias += max(pointShadowExtra * (1.0 - dot(normal, -normalize(ltf))), 0.0);
                bias = max(bias, pointShadowMinBias);
                pointShadow = gX_PointShadow_PCF(pointShadowMaps, ltf,
                    L_FARPLANE(i), bias, i * 6);
            }
        }
        if (enableLightingDiffuse == 1) diffuse += (1.0 - pointShadow) * atten * L_DIFF(i) * phong_diffuse(normal, ldir, L_COLOR(i), L_BRIGHT(i), tex);
        if (enableLightingSpecular == 1) specular += (1.0 - pointShadow) * atten * L_SPEC(i) * phong_specular(normal, ldir, viewDir, L_COLOR(i), L_BRIGHT(i), tex);
    } // close for loop

    color.rgb *= ambient + (1.0 - shadowDiffuseBias * shadow) * diffuse * parShadow + (1.0 - shadowSpecularBias * shadow) * specular * parShadow;

    //opencode: rim/back lighting - edge glow from view-dependent Fresnel (HL2, GTA4)
    if (rimIntensity > 0.001) {
        float rim = 1.0 - max(dot(normal, viewDir), 0.0);
        rim = pow(rim, rimPower);
        color.rgb += rim * rimColor.rgb * rimIntensity;
    }
}

#if QUALITY >= 1
void apply_detail(inout vec3 color) {
    if (detailStrength > 0.0) {
        float fade = 1.0 - clamp(fs_in.fragDistance / detailDistFade, 0.0, 1.0);
        if (fade > 0.0) {
            vec2 detailUV = fs_in.fragTexCoord2 * detailUVScale;
            vec3 detail = texture(detailMap, detailUV).rgb;
            color.rgb = mix(color.rgb, color.rgb * detail * 2.0, detailStrength * fade);
        }
    }
}
#endif

void fog(inout vec4 color) {
    color = gX_Fog_Mix(color, fs_in.fragDistance, fogNear, fogFar, fogColor);
    //opencode: height-based fog (HL2 Ravenholm style)
    if (heightFogDensity > 0.0) {
        float h = max(heightFogBase - fs_in.fragPosition.y, 0.0);
        float heightFactor = 1.0 - exp(-heightFogDensity * h);
        color.rgb = mix(color.rgb, fogColor.rgb, heightFactor);
    }
}

//ai: diagnostic exact ray/AABB projection for comparison with the production approximation.
vec3 gXProbeTrace_BoxDirection(vec3 origin, vec3 direction,
    vec3 center, vec3 halfExtents)
{
    vec3 safeDirection = direction;
    if (abs(safeDirection.x) < 0.000001)
        safeDirection.x = (safeDirection.x < 0.0) ? -0.000001 : 0.000001;
    if (abs(safeDirection.y) < 0.000001)
        safeDirection.y = (safeDirection.y < 0.0) ? -0.000001 : 0.000001;
    if (abs(safeDirection.z) < 0.000001)
        safeDirection.z = (safeDirection.z < 0.0) ? -0.000001 : 0.000001;
    vec3 boxMin = center - halfExtents;
    vec3 boxMax = center + halfExtents;
    vec3 t0 = (boxMin - origin) / safeDirection;
    vec3 t1 = (boxMax - origin) / safeDirection;
    vec3 tFar = max(t0, t1);
    float tExit = min(tFar.x, min(tFar.y, tFar.z));
    return normalize(origin + direction * tExit - center);
}

//ai: one forward ray/AABB exit time; parallel axes do not constrain the exit.
//   Outside-box origins and degenerate lookups retain the outgoing world ray.
vec3 gX_Probe_BoxDirection(vec3 origin, vec3 direction,
    vec3 center, vec3 halfExtents, int skyOnly)
{
    vec3 localOrigin = origin - center;
    if (gXProbeBoxCorrect == 0)
        return (localOrigin + direction * halfExtents
            / max(abs(direction), vec3(0.001))) / halfExtents;
    if (skyOnly == 1) return direction;
    if (any(lessThanEqual(halfExtents, vec3(0.0)))
        || any(greaterThan(abs(localOrigin), halfExtents))) return direction;
    float exitTime = 3.402823e38;
    for (int axis = 0; axis < 3; axis++) {
        if (direction[axis] != 0.0) {
            float boundary = (direction[axis] > 0.0)
                ? halfExtents[axis] : -halfExtents[axis];
            exitTime = min(exitTime,
                (boundary - localOrigin[axis]) / direction[axis]);
        }
    }
    vec3 lookup = localOrigin + direction * exitTime;
    if (dot(lookup, lookup) == 0.0) return direction;
    return lookup;
}

//ai: the proxy fixture's adjacency math needs GLSL 330 bit-cast builtins;
//    the legacy 130 dialect keeps the entry point but returns a neutral result.
#if !GX_GLSL_LEGACY
//ai: adjacent float32 values enclose the isolated proxy's plane arithmetic.
float gX_Proxy_Next(float value, bool upward)
{
    if (value == 0.0) return uintBitsToFloat(upward ? 1u : 0x80000001u);
    if (isinf(value)) {
        if ((value > 0.0) == upward) return value;
        return (value > 0.0) ? 3.402823466e38 : -3.402823466e38;
    }
    uint bits = floatBitsToUint(value);
    return uintBitsToFloat(((value > 0.0) == upward) ? bits + 1u : bits - 1u);
}

//ai: fixed authored fixture only; 0=resolved miss, 1=hit, 2=boundary uncertainty.
int gX_Proxy_Hit(vec3 origin, vec3 direction, out float distance, out int hitFace)
{
    distance = 0.0;
    hitFace = -1;
    if (all(equal(direction, vec3(0.0)))) return 2;
    vec3 center = vec3(8.0, 3.0, -3.0);
    vec3 extents = vec3(1.2);
    float nearTime = -3.402823466e38;
    float farTime = 3.402823466e38;
    float nearLower = nearTime;
    float nearUpper = nearTime;
    float farLower = farTime;
    float farUpper = farTime;
    vec3 nearAxisLower = vec3(nearTime);
    vec3 nearAxisUpper = vec3(nearTime);
    vec3 farAxisLower = vec3(farTime);
    vec3 farAxisUpper = vec3(farTime);
    bool parallelPossible = true;
    bool parallelCertain = true;
    for (int axis = 0; axis < 3; axis++) {
        float lowPlane = center[axis] - extents[axis];
        float highPlane = center[axis] + extents[axis];
        float lowLower = gX_Proxy_Next(gX_Proxy_Next(lowPlane, false) - origin[axis], false);
        float lowUpper = gX_Proxy_Next(gX_Proxy_Next(lowPlane, true) - origin[axis], true);
        float highLower = gX_Proxy_Next(gX_Proxy_Next(highPlane, false) - origin[axis], false);
        float highUpper = gX_Proxy_Next(gX_Proxy_Next(highPlane, true) - origin[axis], true);
        if (direction[axis] == 0.0) {
            parallelPossible = parallelPossible && !(lowLower > 0.0 || highUpper < 0.0);
            parallelCertain = parallelCertain && lowUpper <= 0.0 && highLower >= 0.0;
        } else {
            float a = (lowPlane - origin[axis]) / direction[axis];
            float b = (highPlane - origin[axis]) / direction[axis];
            nearTime = max(nearTime, min(a, b));
            farTime = min(farTime, max(a, b));
            float a0 = lowLower / direction[axis];
            float a1 = lowUpper / direction[axis];
            float b0 = highLower / direction[axis];
            float b1 = highUpper / direction[axis];
            float aLower = gX_Proxy_Next(min(a0, a1), false);
            float aUpper = gX_Proxy_Next(max(a0, a1), true);
            float bLower = gX_Proxy_Next(min(b0, b1), false);
            float bUpper = gX_Proxy_Next(max(b0, b1), true);
            nearLower = max(nearLower, min(aLower, bLower));
            nearUpper = max(nearUpper, min(aUpper, bUpper));
            farLower = min(farLower, max(aLower, bLower));
            farUpper = min(farUpper, max(aUpper, bUpper));
            nearAxisLower[axis] = min(aLower, bLower);
            nearAxisUpper[axis] = min(aUpper, bUpper);
            farAxisLower[axis] = max(aLower, bLower);
            farAxisUpper[axis] = max(aUpper, bUpper);
        }
    }
    distance = 0.0;
    bool possible = parallelPossible && farUpper >= max(nearLower, 0.0) && farUpper > 0.0;
    bool certain = parallelCertain && farLower >= max(nearUpper, 0.0) && farLower > 0.0;
    if (!possible) return 0;
    if (!certain) return 2;
    distance = (nearTime >= 0.0) ? nearTime : farTime;
    bool inside = nearTime < 0.0;
    for (int axis = 0; axis < 3; axis++) {
        bool unique = true;
        for (int other = 0; other < 3; other++) {
            if (other != axis && (inside
                ? farAxisUpper[axis] >= farAxisLower[other]
                : nearAxisLower[axis] <= nearAxisUpper[other])) unique = false;
        }
        if (unique)
            hitFace = axis * 2 + (inside
                ? ((direction[axis] > 0.0) ? 1 : 0)
                : ((direction[axis] > 0.0) ? 0 : 1));
    }
    return 1;
}

//ai: RT convention matches the native equirect generator: +X at u0, +Y at v0.
vec3 gX_Proxy_Sky(vec3 direction)
{
    const float proxyPi = 3.141592653589793;
    vec2 skyUV = vec2(fract(atan(direction.z, direction.x) / (2.0 * proxyPi)),
        acos(clamp(direction.y, -1.0, 1.0)) / proxyPi);
    return (gXProbeProxySkyReady == 1)
        ? textureLod(gXProbeProxySky, skyUV, 0.0).rgb : vec3(0.0);
}

//ai: marker alpha seven identifies diagnostics; geometry, visibility and RGB remain separate.
vec4 gX_Proxy_Diagnostic(vec3 worldNormal)
{
    if (gXProbeProxyDiagnostic == 13 || gXProbeProxyDiagnostic == 14) {
        vec3 directions[8] = vec3[](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0),
            vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1), vec3(1,2,3), vec3(1,2,-3));
        vec3 direction = normalize(directions[int(mod(floor(gl_FragCoord.x), 8.0))]);
        return vec4((gXProbeProxyDiagnostic == 13) ? direction : gX_Proxy_Sky(direction), 7.0);
    }
    if (gXProbeProxyDiagnostic == 7) {
        vec3 origins[9] = vec3[](vec3(8,0,-3), vec3(8,0,-3), vec3(10,0,-3),
            vec3(8,3,-3), vec3(8,0,-3), vec3(8,0,-3), vec3(8,0,-1.8),
            vec3(8,4.2,-3), vec3(8,0,-3));
        vec3 directions[9] = vec3[](vec3(0,1,0), vec3(0,-1,0), vec3(0,1,0),
            vec3(0,1,0), vec3(0,0,0), vec3(0,1,1), vec3(0,1,0),
            vec3(0,1,0), vec3(0,2,0));
        int index = int(mod(floor(gl_FragCoord.x), 9.0));
        float testDistance = 0.0;
        int testFace = -1;
        int testStatus = gX_Proxy_Hit(origins[index], directions[index], testDistance, testFace);
        return vec4(float(testStatus), 0.0, testDistance, 7.0);
    }
    vec3 outgoing = normalize(reflect(normalize(fs_in.fragPosition - viewPosition), worldNormal));
    float distance = 0.0;
    int geometryFace = -1;
    int geometry = gX_Proxy_Hit(fs_in.fragPosition, outgoing, distance, geometryFace);
    vec3 lookup = outgoing;
    int visibility = 0;
    if (geometry == 1) {
        lookup = fs_in.fragPosition + distance * outgoing - cubePos;
        float targetDistance = length(lookup);
        visibility = 3;
        if (targetDistance > 0.0) {
            float capturedDistance = 0.0;
            int capturedFace = -1;
            int captured = gX_Proxy_Hit(cubePos, lookup / targetDistance, capturedDistance, capturedFace);
            if (captured == 1 && geometryFace >= 0 && capturedFace >= 0)
                visibility = (geometryFace == capturedFace
                    && abs(capturedDistance - targetDistance)
                    <= 64.0 * 1.192092896e-7 * max(1.0, targetDistance)) ? 1 : 2;
        } else {
            lookup = outgoing;
        }
    }
    vec3 result = vec3(float(geometry), float(visibility), distance);
    if (gXProbeProxyDiagnostic == 1) result = fs_in.fragPosition;
    else if (gXProbeProxyDiagnostic == 2) result = outgoing;
    else if (gXProbeProxyDiagnostic == 4) result = cubePos;
    else if (gXProbeProxyDiagnostic == 5) result = normalize(lookup);
    else if (gXProbeProxyDiagnostic == 6) result = worldNormal;
    else if (gXProbeProxyDiagnostic == 8)
        result = textureLod(cubeMap, normalize(lookup), 0.0).rgb;
    else if (gXProbeProxyDiagnostic == 9)
        result = vec3(textureLod(cubeMap, normalize(lookup), 0.0).a,
            float(geometry), float(visibility));
    else if (gXProbeProxyDiagnostic >= 10) {
        vec3 sky = gX_Proxy_Sky(outgoing);
        vec4 local = textureLod(cubeMap, normalize(lookup), 0.0);
        int source = 0;
        int reason = 3;
        if (geometry == 0) {
            source = (gXProbeProxySkyReady == 1) ? 1 : 0;
            reason = (gXProbeProxySkyReady == 1) ? 0 : 1;
        } else if (geometry == 1) {
            if (visibility == 1 && local.a > 0.0) {
                source = 2;
                reason = 0;
            } else {
                reason = (visibility == 1) ? 4 : 2;
            }
        }
        vec3 routed = (source == 1) ? sky : ((source == 2) ? local.rgb : vec3(0.0));
        result = (gXProbeProxyDiagnostic == 10) ? sky
            : ((gXProbeProxyDiagnostic == 11) ? routed
                : vec3(float(source), float(reason), float(geometry)));
    }
    return vec4(result, 7.0);
}
#else
vec4 gX_Proxy_Diagnostic(vec3 worldNormal)
{
    return vec4(0.0);
}
#endif

void reflection(inout vec4 color, in vec3 normal, inout Positions pos) {
    vec3 viewDir = normalize(pos.viewpos - pos.fragpos);
    //ai: opt-in correction reflects the incident ray; legacy direction stays default.
    vec3 refDir = reflect(viewDir, normalize(normal))
        * ((gXProbeBoxCorrect == 1) ? -1.0 : 1.0);
    vec3 traceSampleDirection = vec3(0.0);
    vec3 traceSampleDirectionB = vec3(0.0);
    vec3 traceSampleA = vec3(0.0);
    vec3 traceSampleB = vec3(0.0);
    vec3 traceBlendedSample = vec3(0.0);
    vec3 traceExactDirectionA = vec3(0.0);
    vec3 traceExactDirectionB = vec3(0.0);
    vec3 traceExactSampleA = vec3(0.0);
    vec3 traceExactSampleB = vec3(0.0);
    vec3 traceExactBlendedSample = vec3(0.0);
    int traceHasSecondary = 0;
    if ((enableMapNormal == 1) && (hasNormalMap == 1)) {
        refDir = transpose(fs_in.matTNB) * refDir;
    }
    if (gXProbeBoxCorrect == 1) refDir = normalize(refDir);
    if (gXProbeTraceMode >= 11) {
        vec3 normalizedRefDir = normalize(refDir);
        traceExactDirectionA = gXProbeTrace_BoxDirection(fs_in.fragPosition,
            normalizedRefDir, cubePos, cubeSize);
        traceExactDirectionB = gXProbeTrace_BoxDirection(fs_in.fragPosition,
            normalizedRefDir, cubePosB, cubeSize);
    }
    if (gXProbeTraceMode >= 13) {
        traceExactSampleA = texture(cubeMap, traceExactDirectionA).rgb;
        traceExactBlendedSample = traceExactSampleA;
        if (probeBlendB > 0.001) {
            traceExactSampleB = texture(cubeMapB,
                traceExactDirectionB).rgb;
            traceExactBlendedSample = mix(traceExactSampleA,
                traceExactSampleB, probeBlendB);
        }
    }
    vec3 ref;
    if (planarReflection == 1) {
        //opencode: non-planar reflection (GTA4/5 style) - per-fragment local
        //   plane from the shading normal; reflect the view ray across it and
        //   intersect the GLOBAL mirror plane. The hit point replaces the
        //   fragment position in the classic projection. Flat surfaces give
        //   t = 0 = the classic result. NOTE: keep the single reflClip
        //   assignment shape - the NVIDIA GLSL compiler DCEs the whole planar
        //   block when reflClip is reassigned inside a branch (verified).
        vec3 reflHit = fs_in.fragPosition;
        if (nonPlanarReflection == 1) {
            vec3 ln = normalize(normal);
            vec3 rd = reflect(-viewDir, ln); //opencode: outgoing ray matches CPU oracle
            float denom = dot(reflectionPlaneNormal, rd);
            float hDist = reflectionPlaneHeight - dot(reflectionPlaneNormal, fs_in.fragPosition);
            float t = (hDist * denom > 0.001) ? (hDist / denom) : 0.0f;
            reflHit = fs_in.fragPosition + rd * t;
        }
        //opencode: use fs_in.fragPosition (world-space) - pos.fragpos is tangent-space when normal maps active
        //opencode: v0.1.29 - reflMVP projection (best-fidelity variant of
        //   three tested: 22% vs 18% raw-screenUV vs 10%/0% y-flipped).
        //   KNOWN ISSUE: floor fragments whose world position lies behind the
        //   virtual mirror camera (reflClip.w <= 0) get no reflection - a
        //   half-space cut that removed the LEFT half of the reflected cube
        //   at yaw45. Next step: oblique-clipped mirror projection or a
        //   reflection-matrix formulation that keeps w>0 for all visible
        //   fragments; then restore UV-margin + edge/w fades.
        //opencode: v0.1.30 - SCREEN-UV sampling (projective identity: exact).
        //   TEMP-SWEEP: render-side handedness variants scored by trace oracle.
        //opencode: v0.1.30 - 180?-rotated screen-UV. The mirrored camera's
        //   up=(0,-1,0) construction stores the reflection rotated half a turn
        //   relative to the screen; rotating the lookup back aligns every
        //   pixel (verified numerically: RT content (670,389) <-> ideal
        //   screen (857,763) under (x,y) -> (W-x,H-y)).
        //opencode: v0.1.33 - X-FLIPPED screen-UV (single declaration).
        //   LookAt(mirrored pos/target/up) re-orthogonalizes into a RIGHT-
        //   handed frame, adding a hidden 180? yaw versus the true reflected
        //   frame V_main?R - numerically verified: V_M?X = (-0.424,...) vs
        //   V_main?R(X) = (+0.424,...). The RT image is therefore stored
        //   HORIZONTALLY MIRRORED relative to the screen-aligned image the
        //   planar identity needs; flipping u compensates exactly.
        //opencode: projective mirror sampling (replaces screen-UV identity).
        //   reflMVP is composed from the same mirror view/projection the fill
        //   pass rasterizes with, so reflClip reproduces the rasterizer's clip
        //   coords and puv lands on the texel the mirror camera rendered for
        //   this fragment: a true mirror image. Fragments the mirror camera
        //   cannot see (w<=0 behind it, or projected outside its frustum at
        //   grazing angles) own no valid texel: blend to the
        //   parallax-corrected env cubemap there (same as non-planar
        //   models) - the old X-flipped screen-UV fallback pasted the mirror
        //   view unmirrored, an apex-up duplicate beside SSR truth at
        //   grazing angles. Branchless (mix/step/smoothstep) - the
        //   translator DCEs branched reassignments in this block (verified).
        vec4 reflClip = reflMVP * vec4(reflHit, 1.0);
        float projW = max(reflClip.w, 0.0001);
        vec2 puv = reflClip.xy / projW * 0.5 + 0.5;
        vec2 pedge = min(puv, 1.0 - puv);
        //opencode: smooth w-gate (was step(0,w)): the hard step drew a
        //   full-width black strip where the mirror horizon crosses the
        //   floor (projective/cubemap contents differ there). Width in
        //   clip units; margins below handle RT-edge UVs separately.
        //opencode: plane-membership gate - the RT is one photo from one
        //   mirrored camera: exact only for fragments ON the mirror plane.
        //   Off-plane fragments (slab sides, displaced verts) sampled the
        //   floor photo through the plane projection ("see-through"): fade
        //   them to the parallax cubemap instead. Measured on reflHit (the
        //   non-planar path lands it on the plane by construction, so curved
        //   mirrors keep gate 1.0); 1cm tolerance keeps on-plane mirrors
        //   bit-exact. Folded into this declaration: reflClip must keep its
        //   single-assignment shape (NVIDIA DCE, verified) and the block
        //   stays branchless.
        float pconf = smoothstep(-0.02, 0.02, reflClip.w)
            * smoothstep(0.0, 0.03, min(pedge.x, pedge.y))
            * (1.0 - smoothstep(0.01, 0.25, abs(dot(reflectionPlaneNormal, reflHit) - reflectionPlaneHeight)));
        vec3 pref = texture(reflTex, puv).rgb;
        vec3 pSampleDir = gX_Probe_BoxDirection(fs_in.fragPosition,
            refDir, cubePos, cubeSize, gXProbeSkyOnlyA);
        vec3 cref = texture(cubeMap, pSampleDir).rgb;
        traceSampleDirection = normalize(pSampleDir);
        traceSampleA = cref;
        traceBlendedSample = cref;
        //opencode: probe-grid blend — same box projection through probe B.
        if (probeBlendB > 0.001) {
            vec3 pSampleDirB = gX_Probe_BoxDirection(fs_in.fragPosition,
                refDir, cubePosB, cubeSize, gXProbeSkyOnlyB);
            vec3 crefB = texture(cubeMapB, pSampleDirB).rgb;
            traceSampleDirectionB = normalize(pSampleDirB);
            traceSampleB = crefB;
            traceHasSecondary = 1;
            cref = mix(cref, crefB, probeBlendB);
            traceBlendedSample = cref;
        }
        ref = mix(cref, pref, pconf);
    } else {
        //ai: world-space ray-box projection uses the same contract as planar fallback.
        vec3 sampleDir = gX_Probe_BoxDirection(fs_in.fragPosition,
            refDir, cubePos, cubeSize, gXProbeSkyOnlyA);
        ref = texture(cubeMap, sampleDir).rgb;
        traceSampleDirection = normalize(sampleDir);
        traceSampleA = ref;
        traceBlendedSample = ref;
        //opencode: probe-grid blend — same box projection through probe B.
        if (probeBlendB > 0.001) {
            vec3 sampleDirB = gX_Probe_BoxDirection(fs_in.fragPosition,
                refDir, cubePosB, cubeSize, gXProbeSkyOnlyB);
            vec3 refB = texture(cubeMapB, sampleDirB).rgb;
            traceSampleDirectionB = normalize(sampleDirB);
            traceSampleB = refB;
            traceHasSecondary = 1;
            ref = mix(ref, refB, probeBlendB);
            traceBlendedSample = ref;
        }
    }
    if (gXProbeTraceMode > 0) {
        gXProbeTraceRefDirection = normalize(refDir);
        gXProbeTraceSampleDirection = traceSampleDirection;
        gXProbeTraceSampleDirectionB = traceSampleDirectionB;
        gXProbeTraceSampleA = traceSampleA;
        gXProbeTraceSampleB = traceSampleB;
        gXProbeTraceBlendedSample = traceBlendedSample;
        gXProbeTraceWorldPosition = fs_in.fragPosition;
        gXProbeTraceCubeCenter = cubePos;
        gXProbeTraceCubeCenterB = cubePosB;
        gXProbeTraceCubeSize = cubeSize;
        gXProbeTraceExactDirectionA = traceExactDirectionA;
        gXProbeTraceExactDirectionB = traceExactDirectionB;
        gXProbeTraceExactSampleA = traceExactSampleA;
        gXProbeTraceExactSampleB = traceExactSampleB;
        gXProbeTraceExactBlendedSample = traceExactBlendedSample;
        gXProbeTraceHasSecondary = traceHasSecondary;
        gXProbeTraceValid = 1;
    }
    //opencode: v0.1.35 - planar reflections REPLACE rather than add: an
    //   exact mirror image must not wash out under the surface's own
    //   lighting (bright-lit floor regions turned the mirrored cube pale).
    if (planarReflection == 1)
        color.rgb = mix(color.rgb, ref, clamp(reflectionBias + 0.45, 0.0, 1.0));
    else {
        //opencode: Schlick fresnel on the additive env term — dielectric
        //   head-on reflects ~4%, grazing ~100%. The unweighted add washed
        //   the whole slab (56% px brighter); now ghosts stay, wash goes.
        float cosT = clamp(dot(normalize(normal), viewDir), 0.0, 1.0);
        float fres = 0.04 + 0.96 * pow(1.0 - cosT, 5.0);
        //ai: bound only the opt-in interpolation weight, not HDR source radiance.
        vec3 reflected = mix(color.rgb, ref, clamp(reflectionBias * fres, 0.0, 1.0));
        vec3 additive = color.rgb + reflectionBias * fres * ref;
        color.rgb = balancedReflection == 1 ? reflected : additive;
    }
}

//opencode: screen-space refraction - Snell's law + background sample, QUALITY >= 1 only
#if QUALITY >= 1
void refraction(inout vec4 color, in vec3 normal, inout Positions pos) {
    vec3 viewDir = normalize(pos.viewpos - pos.fragpos);
    float ratio = 1.0 / refractiveIndex; // air to material
    vec3 refractDir = refract(viewDir, normalize(normal), ratio);
    
    // Screen-space UV offset from refraction
    vec2 uv = gl_FragCoord.xy / resolution;
    uv += refractDir.xy * 0.015;
    uv = clamp(uv, 0.0, 1.0);
    
    vec3 refractColor = texture(bgTex, uv).rgb;
    
    // Fresnel: more reflection at grazing angles, more refraction head-on
    float fresnel = pow(1.0 - abs(dot(viewDir, normalize(normal))), 3.0);
    fresnel = clamp(fresnel, 0.0, 1.0);
    
    color.rgb = mix(refractColor, color.rgb, fresnel * 0.5);
}
#endif

// ===== PBR: Cook-Torrance BRDF functions =====
//opencode: Trowbridge-Reitz GGX normal distribution
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    //opencode: firefly clamp (Dmax 64). At roughness 0.02 the unclamped peak
    //   hits ~2M: sub-pixel alignment lotteries spike single pixels to
    //   65504 HDR (the O-blowout stats hunt) and alias into checkerboard
    //   skid-marks after the half-res extract. 64 binds only the inner
    //   ~0.3 deg cone (invisible fireflies); every resolved lobe is far
    //   wider and untouched. Energy loss is negligible (sub-pixel solid
    //   angle); Toksvig roughness-AA is the fuller alternative if mirror
    //   glints ever read dim.
    return min(a2 / (3.14159265 * denom * denom), 64.0);
}

//opencode: Smith's Schlick-GGX geometry term for one direction
float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // direct lighting k (IBL uses a?/2)
    return NdotX / (NdotX * (1.0 - k) + k);
}

//opencode: Smith geometry term - product of view and light sub-terms
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.001);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

//opencode: Fresnel-Schlick - F0 is mix(0.04, albedo, metallic)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

//opencode: evaluate Cook-Torrance BRDF for one light - returns Lo (radiance)
vec3 pbr_direct_light(vec3 N, vec3 V, vec3 L, vec3 H,
                      vec3 albedo, vec3 F0, float roughness, float metalness,
                      vec3 lightRadiance) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    // Cook-Torrance specular
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metalness); // energy conservation
    float NdotV = max(dot(N, V), 0.001);

    // specular BRDF
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // diffuse Lambertian
    vec3 diffuse = kD * albedo / 3.14159265;

    return (diffuse + specular) * lightRadiance * NdotL;
}

//opencode: Paper 1 M1 - one-bounce cosine-hemisphere environment estimator.
//   For Lambertian transport, cosine PDF cancels cos(theta)/PDF exactly, so
//   each sample is the environment radiance multiplied by the diffuse BRDF
//   color. The prefiltered irradiance cubemap remains the reference path.
vec3 gX_Oracle_CosineEnvironment(vec3 N, vec3 diffuseColor, int rays) {
    int count = clamp(rays, 1, 4);
    vec3 helper = (abs(N.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, N));
    vec3 bitangent = cross(N, tangent);
    vec3 sum = vec3(0.0);
    for (int ri = 0; ri < 4; ri++) {
        if (ri < count) {
            float u1 = gX_OracleRandom(ri * 2 + 17);
            float u2 = gX_OracleRandom(ri * 2 + 18);
            float radius = sqrt(u1);
            float phi = 6.28318530718 * u2;
            vec3 local = vec3(radius * cos(phi), radius * sin(phi), sqrt(max(0.0, 1.0 - u1)));
            vec3 wi = normalize(tangent * local.x + bitangent * local.y + N * local.z);
            sum += texture(cubeMap, wi).rgb;
        }
    }
    return diffuseColor * (sum / float(count));
}

//opencode: M4 deterministic environment used by the CPU/GPU bounce fixture.
//   It deliberately avoids a texture dependency so both integrators consume
//   identical radiance while surface-to-surface GPU rays remain deferred.
vec3 gX_Oracle_AnalyticEnvironment(vec3 direction) {
    float height = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = vec3(0.025, 0.035, 0.060) * (1.0 - height);
    sky += vec3(0.18, 0.24, 0.38) * height;
    vec3 sun = vec3(0.55, 0.38, 0.20);
    vec3 sunDirection = normalize(vec3(1.0, 2.0, 1.0));
    float highlight = pow(max(dot(direction, sunDirection), 0.0), 180.0);
    return sky + sun * (5.0 * highlight);
}

#if GX_GLFW_FRAG == 1
//opencode: GLFW compat - simplified main() avoids all texture sampler calls.
//   Unbound samplers (cubeMap, ssaoMap, irradianceMap, prefilterMap, shadowMap,
//   pointShadowMaps[], etc.) return NaN on NVIDIA compat profile when their
//   functions are compiled in the shader - even when never called at runtime.
//   This simplified path uses only basic vertex color + Phong math (no textures).
//   The full path (below) is used on raylib backend where unbound samplers
//   safely return (0,0,0,0).
void main() {
    finalColor = vec4(1.0, 0.0, 1.0, 1.0); // magenta = fallthrough error
    outRoughness = 0.5;
    gXProbeTraceValid = 0;
    gXProbeTraceHasSecondary = 0;
    //opencode: light markers represent emitters, not lit scene materials.
    if (lightMarkerMode == 1) {
        vec3 markerEmission = max(emissiveColor.rgb, vec3(0.0))
            * max(emissiveFactor, 0.0);
        finalColor = vec4(vec3(1.0) - exp(-2.0 * markerEmission), 1.0);
        outRoughness = 0.5;
        return;
    }
    vec3 baseAlbedo = albedoColor.rgb;
    if (hasAlbedoMap == 0 && length(baseAlbedo) < 0.001) baseAlbedo = vec3(1.0);
    vec4 vc = fs_in.fragColor;
    if (vc.r == 0.0 && vc.g == 0.0 && vc.b == 0.0 && vc.a == 0.0) vc = vec4(1.0);
    vec4 color = vc * vec4(baseAlbedo, 1.0);
    Positions pos;
    calc_positions(pos);
    vec3 normal = calc_normal(fs_in.fragTexCoord);
    if (ssrNormalPass == 1) {
        finalColor = vec4(calc_ssr_normal(fs_in.fragTexCoord) * 0.5 + 0.5, 1.0);
        outRoughness = 0.5;
        return;
    }

    if (enablePhong == 1) {
        vec3 ldir = normalize(pos.lightpos);
        vec3 ambient = ambientFactor * ambientColor.rgb;
        vec3 diff = vec3(albedoFactor) * sunBrightness * sunColor.rgb * max(dot(normalize(normal), ldir), 0.0);
        color.rgb = color.rgb * (ambient + diff * sunDiffuseFactor * sunDiffuseTint.rgb);
    }
    //opencode: keep the GLFW compatibility path behaviorally aligned with the
    //   full raylib path when a lightmap is explicitly enabled.
    color.rgb += gX_LightmapContribution(baseAlbedo * vc.rgb, fs_in.fragTexCoord);
    //opencode: planar reflection + refraction for the GLFW compat path.
    //   Both reflTex (unit 19) and bgTex (unit 14) are ALWAYS bound by
    //   gX_RendererGFX_Model_Draw when planarReflection/enableRefraction are
    //   set - unlike cubeMap/ssaoMap/etc., so sampling them here is safe
    //   (no NaN risk on NVIDIA compat profile).
    if (planarReflection == 1) {
        vec4 reflClip = reflMVP * vec4(fs_in.fragPosition, 1.0);
        if (reflClip.w > 0.0) {
            vec3 reflNDC = reflClip.xyz / reflClip.w;
            vec2 uv = reflNDC.xy * 0.47 + 0.5;
            vec2 edgeDist = min(uv, 1.0 - uv);
            float edgeFade = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.y));
            float wFade = smoothstep(-1.0, 0.5, reflClip.w);
            color.rgb += reflectionBias * texture(reflTex, uv).rgb * edgeFade * wFade;
        }
    }
#if QUALITY >= 1
    if (enableRefraction == 1) refraction(color, normal, pos);
#endif
    //opencode: Reinhard tone mapping - prevents HDR clipping when sunBrightness > 1.0
    {
        float ssaoFactor = (enableSSAO == 1) ? texture(ssaoMap, gl_FragCoord.xy / resolution).r : 1.0;

        color.rgb *= mix(1.0, ssaoFactor, 0.6);
    }
    color.rgb = aces_tonemap(color.rgb * exposure);
    //opencode: dummy refs to prevent GLSL from eliminating cookie uniforms
    color.rgb += texture(lightsCookieAtlas, lightsCookieRects[0].xy).rgb * 1e-6;
    color.rgb += float(lightsCookieEnabled[0]) * 1e-6;
    //opencode: carry per-pixel roughness in the alpha - the MRT location-1
    //   output is silently dropped by the NVIDIA driver when the fragment
    //   shader uses the full VS_OUT interface (see session notes). The alpha
    //   is unused for opaque draws (blend off); transparent models keep their
    //   blend opacity via the blendOpacity guard.
    float ssrOutputAlpha = (receiveSSR == 1) ? 0.5 : 2.0;
    finalColor = vec4(color.rgb, (blendOpacity < 0.999) ? color.a : ssrOutputAlpha);
    if (roughnessAlpha == 1) finalColor.a = ssrOutputAlpha;
    outRoughness = 0.5;
}
#else
//ai: selected material map output is linear and skips material lighting.
//opencode: full PBR/Phong path - used on raylib backend where unbound samplers
//   return (0,0,0,0) instead of NaN.
//ai: live Phong ambient shading preserves albedo and honors the global ambient toggle.
void main() {
    finalColor = vec4(1.0, 0.0, 1.0, 1.0); // magenta = fallthrough error
    outRoughness = 0.5;
    //opencode: light markers represent emitters, not lit scene materials.
    if (lightMarkerMode == 1) {
        vec3 markerEmission = max(emissiveColor.rgb, vec3(0.0))
            * max(emissiveFactor, 0.0);
        finalColor = vec4(vec3(1.0) - exp(-2.0 * markerEmission), 1.0);
        outRoughness = 0.5;
        return;
    }
    vec3 baseAlbedo = albedoColor.rgb;
    if (hasAlbedoMap == 0 && length(baseAlbedo) < 0.001) baseAlbedo = vec3(1.0);
    vec4 vc = fs_in.fragColor;
    if (vc.r == 0.0 && vc.g == 0.0 && vc.b == 0.0 && vc.a == 0.0) vc = vec4(1.0);
    vec4 baseColor = vc * vec4(baseAlbedo, 1.0);
    vec4 color = baseColor;
    float ssrRoughness = 0.5; //opencode: per-pixel roughness for SSR (default 0.5)
    Positions pos;
    calc_positions(pos);

    vec2 texcoord = fs_in.fragTexCoord;
    if (debugBaseColorMap == 1 && ssrNormalPass != 1) {
        if (hasAlbedoMap == 1) {
            vec2 debugUV = albedoMapScale * texcoord - albedoMapOffset;
            finalColor = vec4(texture(albedoMap, debugUV).rgb, 1.0);
        } else {
            finalColor = vec4(1.0, 0.0, 1.0, 1.0);
        }
        outRoughness = 0.5;
        return;
    }
    
    //opencode: apply blend opacity to alpha channel
    if (blendOpacity < 0.999) {
        float finalAlpha = blendOpacity;
        if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
            vec2 coord = albedoMapScale * texcoord - albedoMapOffset;
            finalAlpha *= texture(albedoMap, coord).a;
        }
        color.a = finalAlpha;
    }
    
    //opencode: alpha test - discard low-alpha fragments (cutout transparency)
    if (enableAlphaTest == 1) {
        float alpha = color.a;
        if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
            vec2 coord = albedoMapScale * texcoord - albedoMapOffset;
            alpha = texture(albedoMap, coord).a;
        }
        if (alpha < alphaCutoff) discard;
    }
    if ((enableMapParallax == 1) && (hasHeightMap == 1)) {
        vec3 tanViewDir = normalize(fs_in.tangentViewPosition - fs_in.tangentFragPosition);
        if (parallaxMode == 0) {
            texcoord = ParallaxMapping(texcoord, tanViewDir);
        } else {
            texcoord = ParallaxMapping2(texcoord, tanViewDir);
        }
    }

    float parallaxShadow = 1.0;
#if QUALITY >= 2
    if ((enableMapParallax == 1) && (hasHeightMap == 1) && (parallaxMode >= 2)) {
        vec3 tanLightDir = normalize(fs_in.tangentLightPosition - fs_in.tangentFragPosition);
        parallaxShadow = ParallaxShadow(texcoord, tanLightDir);
    }
#endif

    vec3 normal = calc_normal(texcoord);

    if (ssrNormalPass == 1) {
        finalColor = vec4(calc_ssr_normal(texcoord) * 0.5 + 0.5, 1.0);
        outRoughness = 0.5;
        return;
    }

    if (enableGouraud == 1) {
        vec3 lit = color.rgb;
        if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
            vec2 coord = albedoMapScale * texcoord - albedoMapOffset;
            lit *= albedoMapFactor * gX_MaterialAlbedo(coord);
        }
        color.rgb = lit * fs_in.gouraudLight;
    } else if (usePBR == 1
#if QUALITY < 1
        && false
#endif
    ) {
        //opencode: Cook-Torrance PBR direct lighting path
#if QUALITY < 1
        //opencode: LOW quality - this branch is dead; compiler eliminates it.
        //   The flag-enforcement above means usePBR==1 never enters this block
        //   at LOW quality. Remains here for syntactical completeness.
#endif
        vec3 N = normalize(normal);
        vec3 V = normalize(pos.viewpos - pos.fragpos);

        // sample albedo - use baseColor which includes vertex color * material albedo
        vec3 albedo = baseColor.rgb;
        if (enableMapAlbedo == 1 && hasAlbedoMap == 1) {
            vec2 coord = albedoMapScale * texcoord - albedoMapOffset;
            albedo *= albedoMapFactor * gX_MaterialAlbedo(coord);
        }

        // sample metalness + roughness (or use defaults)
        float metalness = metalnessFactor;
        float rough = roughness;
        if (enableMapMetalness == 1 && hasMetalnessMap == 1) {
            vec2 coord = metalnessMapScale * texcoord - metalnessMapOffset;
            vec4 mr = texture(metalnessMap, coord);
            // glTF convention: G channel = roughness, B channel = metalness
            if (hasRoughnessMap == 0) rough = mr.g * roughness;
            metalness = mr.b * metalnessFactor;
        }
        if (hasRoughnessMap == 1) {
            vec2 coord = roughnessMapScale * texcoord - roughnessMapOffset;
            rough *= roughnessMapFactor * texture(roughnessMap, coord).r;
        }
        rough = clamp(rough, 0.001, 1.0);
        ssrRoughness = rough;

        // sample AO
        float occlusion = ao;
        if (hasAoMap == 1) {
            vec2 coord = aoMapScale * texcoord - aoMapOffset;
            occlusion *= aoMapFactor * texture(occlusionMap, coord).r;
        }

        //opencode: PBR F0 - mix dielectric (0.04) and metallic (albedo), then tint with specularColor
        vec3 F0 = mix(vec3(0.04), albedo, metalness) * specularColor;

        // Sun (directional light)
        {
            vec3 L = normalize(pos.lightpos);
            vec3 H = normalize(V + L);
            vec3 radiance = sunBrightness * sunColor.rgb * sunDiffuseFactor * sunDiffuseTint.rgb;
            //opencode: CSM-aware shadow (PBR path, up to 4 cascades)
            if (enableShadows == 1) {
                float shad;
                if (cascadeCount > 1 && csmSplits.x > 0.0) {
                    float vDist = linearViewDist(gl_FragCoord.z, nearPlane, farPlane);
                    int csmLayer = 0;
                    vec4 lsPos = fs_in.lightspaceFragPosition;
                    if (vDist > csmSplits.z && cascadeCount >= 4) {
                        csmLayer = 3;
                        lsPos = csmVP[3] * vec4(fs_in.fragPosition, 1.0);
                    } else if (vDist > csmSplits.y && cascadeCount >= 3) {
                        csmLayer = 2;
                        lsPos = csmVP[2] * vec4(fs_in.fragPosition, 1.0);
                    } else if (vDist > csmSplits.x) {
                        csmLayer = 1;
                        lsPos = csmVP[1] * vec4(fs_in.fragPosition, 1.0);
                    }
                    if (csmLayer > 0) {
                        float sCur = gX_Shadow_PCF_Array(lsPos, N, L, csmShadowMapArray, csmLayer);
                        float boundary = (csmLayer == 1) ? csmSplits.x :
                                         (csmLayer == 2) ? csmSplits.y : csmSplits.z;
                        float sPrev;
                        if (csmLayer == 1)
                            sPrev = gX_Shadow_PCF(fs_in.lightspaceFragPosition, N, L, shadowMap);
                        else {
                            vec4 prevLs = csmVP[csmLayer - 1] * vec4(fs_in.fragPosition, 1.0);
                            sPrev = gX_Shadow_PCF_Array(prevLs, N, L, csmShadowMapArray, csmLayer - 1);
                        }
                        //opencode: match Phong's 10% split fade so PBR seams do not pop.
                        float blend = (csmBlendScale > 0.0)
                            ? smoothstep(boundary, boundary * (1.0 + 0.1 * csmBlendScale), vDist) : 1.0;
                        shad = mix(sPrev, sCur, blend);
                    } else {
                        shad = gX_Shadow_PCF(lsPos, N, L, shadowMap);
                    }
                } else {
                    shad = gX_Shadow_PCF(fs_in.lightspaceFragPosition, N, L, shadowMap);
                }
                radiance *= (1.0 - shadowDiffuseBias * shad);
            }
            //opencode: contact shadows (spike 2) - multiply the SUN term
            //   only (ambient/IBL untouched: contact is sun occlusion).
            //   Spike 3 verdict: no CSM/contact overlap problem exists, so
            //   the physical product stands. High sun: bands disjoint
            //   (CSM-wins-ties tested identical). Grazing sun: contact is
            //   ~no-op by band-limit (long shadows escape the 2u window,
            //   CSM owns them) - correct division of labor, nothing to
            //   double-count in either regime.
            if (enableContact == 1) {
                radiance *= texture(contactMap, gl_FragCoord.xy / resolution).r;
            }
            //opencode: split-sum IBL (image-based lighting) for PBR path.
            //   QUALITY >= 1 required - at LOW quality the PBR path is dead
            //   (forced to Blinn-Phong), so this block is only reachable at
            //   MEDIUM or HIGH, but guard explicitly for clarity.
#if QUALITY >= 1
            float NdotV = max(dot(N, V), 0.0);
            vec3 R = reflect(-V, N);
            vec3 kD = (1.0 - F0) * (1.0 - metalness);
            vec3 irradiance = texture(irradianceMap, N).rgb;
            vec3 diffuseIBL = irradiance * albedo * kD;
            if (oraclePathActive == 1)
                diffuseIBL = gX_Oracle_CosineEnvironment(N, albedo * kD, oracleRayCount);
            float lod = rough * 4.0;
            vec3 prefiltered = textureLod(prefilterMap, R, lod).rgb;
            vec2 brdf = texture(bdrfMap, vec2(NdotV, rough)).rg;
            vec3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);
            //opencode: v0.3.8 - fog the IBL reflection: the cubemap carries no
            //   fog, so mirrored sky stayed crisp while the direct sky fogged
            //   (the mirror-horizon seam). The fog LUT stores the per-direction
            //   sky delta; scale by the specular weight so diffuse-dominant
            //   surfaces do not pick up sky-only fog.
            if (hasFogLut == 1) {
                vec2 flUv = vec2(fract(atan(R.z, R.x) / (2.0 * 3.14159265)),
                                 acos(clamp(R.y, -1.0, 1.0)) / 3.14159265);
                specularIBL += texture(fogLutMap, flUv).rgb;
            }
            vec3 iblColor = mix(diffuseIBL + specularIBL, vec3(0.0), 1.0 - envAmbientFactor);
            float ssaoFactor = (enableSSAO == 1) ? texture(ssaoMap, gl_FragCoord.xy / resolution).r : 1.0;
            color.rgb = iblColor * occlusion * ssaoFactor;
            color.rgb += pbr_direct_light(N, V, L, H, albedo, F0, rough, metalness, radiance) * occlusion;
#else
            //opencode: LOW quality - no IBL, direct light only with simple ambient
            float ssaoFactor = (enableSSAO == 1) ? texture(ssaoMap, gl_FragCoord.xy / resolution).r : 1.0;
            color.rgb = occlusion * ambientFactor * ambientColor.rgb * ssaoFactor;
            color.rgb += pbr_direct_light(N, V, L, H, albedo, F0, rough, metalness, radiance) * occlusion;
#endif
        }

        // Point/spot lights
        //opencode: v0.5.0 — Forward+ grid consumer (was dead: the CPU-built
        //   RG32UI bitmask grid was uploaded + bound but never read, so
        //   ENABLE_CLUSTERED only added per-frame overhead). Enumerate the
        //   tile mask in ASCENDING slot order (GLSL 330 has no findLSB —
        //   lowbit = mask & (~mask+1u), index via log2 of a power of two).
        //   Legacy path = slot i == light i, so both enumerate identically.
        int slotCount = 0;
        int slotIdx[64];
        if (clusteredEnabled == 1) {
            ivec2 tile = ivec2(gl_FragCoord.xy / 16.0); // GX_CLUSTER_TILE
            uint mask = texelFetch(lightGridTex, tile, 0).r;
            uint maskHi = texelFetch(lightGridTex, tile, 0).g;
            while (mask != 0u) {
                uint lowbit = mask & (~mask + 1u);
                slotIdx[slotCount++] = int(log2(float(lowbit)));
                mask ^= lowbit;
            }
            while (maskHi != 0u) {
                uint lowbit = maskHi & (~maskHi + 1u);
                slotIdx[slotCount++] = 32 + int(log2(float(lowbit)));
                maskHi ^= lowbit;
            }
        } else {
            int n = lightsAmount;
            //opencode: SSBO mode carries 64 (uniform mode stays 32) — the
            //   cluster mask already enumerates both RG32UI halves.
            #ifdef GX_COMPUTE_SSBO
                if (n > 64) n = 64;
            #else
                if (n > 32) n = 32;
            #endif
            for (int si = 0; si < n; si++) slotIdx[si] = si;
            slotCount = n;
        }
        //opencode: oracle estimator (Paper 1) - uniform single-light pick
        //   over this pixel's candidate set; inactive (-1) = all lights.
        //   Multi-ray (GX_T_ORACLERAYS=K 1..4): K with-replacement picks
        //   averaged (x N/K stays unbiased, variance /K). Colliding picks
        //   count multiply (correct for with-replacement sampling).
        int oraclePicks[4];
        int oracleRays = 0;
        float oraclePickScale = 1.0;
        if (oracleEstimateActive == 1 && slotCount > 0) {
            oracleRays = clamp(oracleRayCount, 1, 4);
            for (int rk = 0; rk < oracleRays; rk++)
                oraclePicks[rk] = gX_OraclePick(slotCount, rk);
            oraclePickScale = float(slotCount) / float(oracleRays);
        }
        for (int si = 0; si < slotCount; si++) {
            int i = (clusteredEnabled == 1) ? slotIdx[si] : si;
            //opencode: oracle estimator (Paper 1) - evaluate only picked
            //   lights; the scale below unbiases the averaged samples.
            int oracleHits = 0;
            if (oracleRays > 0) {
                for (int rk = 0; rk < oracleRays; rk++)
                    if (si == oraclePicks[rk]) oracleHits++;
                if (oracleHits == 0) continue;
            }
            float worldDist = length(fs_in.fragPosition - L_POS(i));
            if (L_SHADOWRANGE(i) > 0.0 && worldDist > L_SHADOWRANGE(i)) continue;

            vec3 lp = L_POS(i);
            if ((enableMapNormal == 1) && (hasNormalMap == 1)) lp = fs_in.matTNB * lp;
            vec3 delta = lp - pos.fragpos;
            float dist = length(delta);
            vec3 L = delta / dist;
            //ai: unused heap holes upload all-zero attenuation; keep PBR safe
            //   even when clustering does not filter the hole.
            float attenDenom = L_ATTEN(i).x + L_ATTEN(i).y * dist
                + L_ATTEN(i).z * dist * dist;
            if (attenDenom <= 0.0) continue;
            float atten = 1.0 / attenDenom;

            //opencode: spot light cone falloff (PBR path)
            if (L_TYPE(i) == 2) {
                vec3 worldLightDir = normalize(L_DIR(i));
                vec3 worldToFrag = normalize(fs_in.fragPosition - L_POS(i));
                float cosAng = max(dot(worldToFrag, worldLightDir), 0.0);
                float halfAngle = L_FOV(i) * 0.5 * 0.0174533;
                atten *= smoothstep(cos(halfAngle), cos(halfAngle * 0.9), cosAng);
                {
                    vec3 Lc = worldLightDir;
                    vec3 Uc = normalize(cross(abs(Lc.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0), Lc));
                    vec3 Vc = cross(Lc, Uc);
                    float dc = max(cosAng, 0.001);
                    vec2 cookieUV = vec2(dot(worldToFrag, Uc), dot(worldToFrag, Vc)) / dc;
                    cookieUV = cookieUV * 0.5 + 0.5;
                    float cookieSample = 1.0;
                    if (all(greaterThanEqual(cookieUV, vec2(0.0))) && all(lessThanEqual(cookieUV, vec2(1.0)))) {
                        cookieSample = texture(lightsCookieAtlas, L_COOKIERECT(i).xy + cookieUV * L_COOKIERECT(i).zw).r;
                    }
                    atten *= mix(1.0, cookieSample, float(L_COOKIE(i)));
                }
            }

            // point light shadow - sampler2DArray (layer = light*6+face)
            float pointShadow = 0.0;
            if (L_FARPLANE(i) > 0.0) {
                vec3 ltf = fs_in.fragPosition - L_POS(i);
                if (L_SHADOWRANGE(i) <= 0.0 || length(ltf) <= L_SHADOWRANGE(i)) {
                    float bias = pointShadowBias;
                    bias += max(pointShadowExtra * (1.0 - dot(N, -normalize(ltf))), 0.0);
                    bias = max(bias, pointShadowMinBias);
                    pointShadow = gX_PointShadow_PCF(pointShadowMaps, ltf,
                        L_FARPLANE(i), bias, i * 6);
                }
            }

            vec3 H = normalize(V + L);
            vec3 radiance = atten * L_BRIGHT(i) * L_COLOR(i).rgb * L_DIFF(i);
            radiance *= (1.0 - pointShadow);
            color.rgb += pbr_direct_light(N, V, L, H, albedo, F0, rough, metalness, radiance) * (oraclePickScale * float(max(oracleHits, 1)));
        }

        color.rgb *= parallaxShadow; // apply parallax self-shadow to final color

        //opencode: rim/back lighting (PBR path) - QUALITY >= 1
#if QUALITY >= 1
        if (rimIntensity > 0.001) {
            float rim = 1.0 - max(dot(N, V), 0.0);
            rim = pow(rim, rimPower);
            color.rgb += rim * rimColor.rgb * rimIntensity;
        }
#endif

        //opencode: emissive in PBR path
        {
            vec3 emissive = emissiveColor.rgb * emissiveFactor;
            if (hasEmissiveMap == 1) {
                vec2 coord = emissiveMapScale * fs_in.fragTexCoord - emissiveMapOffset;
                emissive *= emissiveMapFactor * texture(emissionMap, coord).rgb;
            }
            color.rgb += emissive;
        }
    } else if (enablePhong == 1) {
        //opencode: inline simplified phong - avoids unbound sampler NaN on NVIDIA GLFW
        //   The full phong() function calls texture(cubeMap, ...), texture(ssaoMap, ...)
        //   which return NaN when the sampler has id=0 on compatibility profile.
        //   This path adds point light support (the original inline path only had sun).
        //opencode: use world-space light direction for sun (lightPosition uniform).
        //   pos.lightpos is TNB-transformed (tangent space) for normal map shading,
        //   but the interpolated vertex normal (no normal map) is in world space -
        //   dot(world_normal, tangent_dir) gives wrong NdotL.
        vec3 ldir = normalize(lightPosition);
        vec3 viewDir = normalize(pos.viewpos - pos.fragpos);
        vec3 ambient = (enableLightingAmbient == 1)
            ? phong_ambient(texcoord) : vec3(0.0);
        vec3 sunDiffuse = vec3(0.0);
        vec3 sunSpecular = vec3(0.0);
        vec3 pointDiffuse = vec3(0.0);
        vec3 pointSpecular = vec3(0.0);
        float shadow = 0.0;

        // Sun light
        if (enableLightingDiffuse == 1)
            sunDiffuse = phong_diffuse(normal, ldir, sunColor, sunBrightness, texcoord) * (sunDiffuseFactor * sunDiffuseTint.rgb);
        if (enableLightingSpecular == 1)
            sunSpecular = phong_specular(normal, ldir, viewDir, sunColor, sunBrightness, texcoord) * (sunSpecularFactor * sunSpecularTint.rgb);
        if (enableShadows == 1)
            shadow = gX_Shadow_PCF(fs_in.lightspaceFragPosition, normal, ldir, shadowMap);

        // Point/spot lights contribute regardless of sun shadow - a surface in sun
        // shadow should still be lit by nearby point lights for a natural look.
        //opencode: oracle estimator (Paper 1) - multi-ray MC picks over
        //   all point/spot lights (sun stays exact, evaluated above).
        //   With-replacement average: x N/K unbiased, variance /K.
        int oraclePicksP[4];
        int oracleRaysP = 0;
        float oraclePickScaleP = 1.0;
        if (oracleEstimateActive == 1 && lightsAmount > 0) {
            oracleRaysP = clamp(oracleRayCount, 1, 4);
            for (int rk = 0; rk < oracleRaysP; rk++)
                oraclePicksP[rk] = gX_OraclePick(lightsAmount, rk);
            oraclePickScaleP = float(lightsAmount) / float(oracleRaysP);
        }
        for (int i = 0; i < lightsAmount; i++) {
            int oracleHitsP = 0;
            if (oracleRaysP > 0) {
                for (int rk = 0; rk < oracleRaysP; rk++)
                    if (i == oraclePicksP[rk]) oracleHitsP++;
                if (oracleHitsP == 0) continue;
            }
            float worldDist = length(fs_in.fragPosition - L_POS(i));
            if (L_SHADOWRANGE(i) > 0.0 && worldDist > L_SHADOWRANGE(i)) continue;

            vec3 lp = L_POS(i);
            if ((enableMapNormal == 1) && (hasNormalMap == 1)) lp = fs_in.matTNB * lp;
            vec3 delta = lp - pos.fragpos;
            float dist = length(delta);
            vec3 pldir = delta / dist;
            //ai: unused heap holes upload all-zero attenuation; skip them
            //   before the reciprocal (NaN sanitizer must not mask the light).
            float attenDenom = L_ATTEN(i).x + L_ATTEN(i).y * dist
                + L_ATTEN(i).z * dist * dist;
            if (attenDenom <= 0.0) continue;
            float atten = 1.0 / attenDenom;

            if (L_TYPE(i) == 2) {
                vec3 worldLightDir = normalize(L_DIR(i));
                vec3 worldToFrag = normalize(fs_in.fragPosition - L_POS(i));
                float cosAng = max(dot(worldToFrag, worldLightDir), 0.0);
                float halfAngle = L_FOV(i) * 0.5 * 0.0174533;
                atten *= smoothstep(cos(halfAngle), cos(halfAngle * 0.9), cosAng);
                //opencode: light cookie - projected texture for spot lights
                //   (was missing from the inline path - the dead phong() function
                //   had it, so cookies silently did nothing in the active path)
                {
                    vec3 L = worldLightDir;
                    vec3 U = normalize(cross(abs(L.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0), L));
                    vec3 V = cross(L, U);
                    float d = max(cosAng, 0.001);
                    vec2 cookieUV = vec2(dot(worldToFrag, U), dot(worldToFrag, V)) / d;
                    cookieUV = cookieUV * 0.5 + 0.5;
                    float cookieSample = 1.0;
                    if (all(greaterThanEqual(cookieUV, vec2(0.0))) && all(lessThanEqual(cookieUV, vec2(1.0)))) {
                        cookieSample = texture(lightsCookieAtlas, L_COOKIERECT(i).xy + cookieUV * L_COOKIERECT(i).zw).r;
                    }
                    atten *= mix(1.0, cookieSample, float(L_COOKIE(i)));
                }
            }

            // Point shadow attenuation - sampler2DArray (layer = light*6+face)
            float pointShadow = 0.0;
            if (L_FARPLANE(i) > 0.0) {
                vec3 ltf = fs_in.fragPosition - L_POS(i);
                if (L_SHADOWRANGE(i) <= 0.0 || length(ltf) <= L_SHADOWRANGE(i)) {
                    float bias = pointShadowBias;
                    bias += max(pointShadowExtra * (1.0 - dot(normal, -normalize(ltf))), 0.0);
                    bias = max(bias, pointShadowMinBias);
                    pointShadow = gX_PointShadow_PCF(pointShadowMaps, ltf,
                        L_FARPLANE(i), bias, i * 6);
                }
            }

            if (enableLightingDiffuse == 1)
                pointDiffuse += (1.0 - pointShadow) * atten * L_DIFF(i) * phong_diffuse(normal, pldir, L_COLOR(i), L_BRIGHT(i), texcoord) * (oraclePickScaleP * float(max(oracleHitsP, 1)));
            if (enableLightingSpecular == 1)
                pointSpecular += (1.0 - pointShadow) * atten * L_SPEC(i) * phong_specular(normal, pldir, viewDir, L_COLOR(i), L_BRIGHT(i), texcoord) * (oraclePickScaleP * float(max(oracleHitsP, 1)));
        }

        float shadowAttn = 1.0 - shadow * 0.85;
        color.rgb = color.rgb * (ambient + sunDiffuse * shadowAttn + pointDiffuse + sunSpecular * shadowAttn + pointSpecular);
    }
    //opencode: add static baked irradiance after dynamic forward lighting and
    //   before reflection/refraction, so the map contributes to the base
    //   surface but does not overwrite view-dependent terms.
    color.rgb += gX_LightmapContribution(baseColor.rgb, texcoord);
    //opencode: refraction + reflections - QUALITY >= 1 for refraction
#if QUALITY >= 1
    if (enableRefraction == 1) refraction(color, normal, pos);
#endif
    if (enableReflections == 1 || planarReflection == 1) reflection(color, normal, pos);
    //opencode: emissive - add after reflections so it's visible on the lit surface
    if (usePBR != 1) { // PBR path already handles emissive above
        vec3 emissive = emissiveColor.rgb * emissiveFactor;
        if (hasEmissiveMap == 1) {
            vec2 coord = emissiveMapScale * fs_in.fragTexCoord - emissiveMapOffset;
            emissive *= emissiveMapFactor * texture(emissionMap, coord).rgb;
        }
        color.rgb += emissive;
    }
    //opencode: detail texture - high-frequency surface detail (HL2 style), QUALITY >= 1
#if QUALITY >= 1
    apply_detail(color.rgb);
#endif
    if (enableFog == 1)         fog(color);

    if (showPosition == 1)  color = vec4(fs_in.fragPosition, 1.0);
    if (showTexCoord == 1)  color = vec4(fs_in.fragTexCoord, 1.0, 1.0);
    if (showTexCoord2 == 1) color = vec4(fs_in.fragTexCoord2, 1.0, 1.0);
    if (showNormal == 1)    color = vec4(normal, 1.0);
    if (showColor == 1)     color = fs_in.fragColor;
    if (showTangent == 1)   color = vec4(fs_in.fragTangent, 1.0);

    //opencode: uniform diagnostic - override output with uniform values
    if (diagUniformMode == 1) {
        color = vec4(vec3(albedoFactor, 0.0, 0.0), 1.0);
    } else if (diagUniformMode == 2) {
        color = vec4(vec3(metalnessFactor, 0.0, 0.0), 1.0);
    } else if (diagUniformMode == 3) {
        vec3 lp = normalize(abs(lightPosition.xyz));
        color = vec4(lp, 1.0);
    }

    //opencode: mirror fill renders linear: main-view SSAO does not apply
    //   to mirror-view fragments, and tonemap+exposure run once in main.
    //   Branchless (float casts) - the translator DCEs branched
    //   reassignments (verified in the planar block above).
    {
        float ssaoGate = (1.0 - float(mirrorFill)) * float(enableSSAO == 1);
        float ssaoFactor = mix(1.0, texture(ssaoMap, gl_FragCoord.xy / resolution).r, ssaoGate);

        color.rgb *= mix(1.0, ssaoFactor, 0.6 * (1.0 - float(mirrorFill)));
    }
    //opencode: Paper 1 M2 — the opt-in raster parity fixture bypasses the
    //   production PBR/Phong accumulation and evaluates the same Lambert
    //   direct-light contract as oracle_cpu_path.py. This is deliberately
    //   disabled for every normal draw, preserving production shading.
    if (oracleDiffuseOnly == 1 && oracleBounce == 0) {
        vec3 oracleN = normalize(normal);
        vec3 oracleAlbedo = baseColor.rgb;
        vec3 oracleRadiance = vec3(0.0);
        vec3 oracleSunL = normalize(pos.lightpos);
        oracleRadiance += oracleAlbedo / 3.14159265
            * max(dot(oracleN, oracleSunL), 0.0)
            * sunBrightness * sunColor.rgb * sunDiffuseFactor * sunDiffuseTint.rgb;
        for (int oracleLight = 0; oracleLight < lightsAmount; oracleLight++) {
            vec3 oracleDelta = L_POS(oracleLight) - pos.fragpos;
            float oracleDistance = max(length(oracleDelta), 0.000001);
            vec3 oracleL = oracleDelta / oracleDistance;
            float oracleCos = max(dot(oracleN, oracleL), 0.0);
            float oracleAttenDenom = L_ATTEN(oracleLight).x
                + L_ATTEN(oracleLight).y * oracleDistance
                + L_ATTEN(oracleLight).z * oracleDistance * oracleDistance;
            if (oracleAttenDenom <= 0.0) continue;
            float oracleAtten = 1.0 / oracleAttenDenom;
            oracleRadiance += oracleAlbedo / 3.14159265 * oracleCos
                * oracleAtten * L_BRIGHT(oracleLight)
                * L_COLOR(oracleLight).rgb * L_DIFF(oracleLight);
        }
        color.rgb = oracleRadiance;
    }
    if (oracleBounce == 1) {
        vec3 oracleN = normalize(normal);
        vec3 oracleAlbedo = baseColor.rgb;
        vec3 oracleSunL = normalize(pos.lightpos);
        vec3 oracleRadiance = oracleAlbedo / 3.14159265
            * max(dot(oracleN, oracleSunL), 0.0)
            * sunBrightness * sunColor.rgb * sunDiffuseFactor * sunDiffuseTint.rgb;
        for (int oracleLight = 0; oracleLight < lightsAmount; oracleLight++) {
            vec3 oracleDelta = L_POS(oracleLight) - pos.fragpos;
            float oracleDistance = max(length(oracleDelta), 0.000001);
            vec3 oracleL = oracleDelta / oracleDistance;
            float oracleCos = max(dot(oracleN, oracleL), 0.0);
            float oracleAttenDenom = L_ATTEN(oracleLight).x
                + L_ATTEN(oracleLight).y * oracleDistance
                + L_ATTEN(oracleLight).z * oracleDistance * oracleDistance;
            if (oracleAttenDenom <= 0.0) continue;
            float oracleAtten = 1.0 / oracleAttenDenom;
            oracleRadiance += oracleAlbedo / 3.14159265 * oracleCos
                * oracleAtten * L_BRIGHT(oracleLight)
                * L_COLOR(oracleLight).rgb * L_DIFF(oracleLight);
        }
        vec3 oracleHelper = (abs(oracleN.y) < 0.999)
            ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 oracleTangent = normalize(cross(oracleHelper, oracleN));
        vec3 oracleBitangent = cross(oracleN, oracleTangent);
        float oracleU1 = gX_OracleRandom(17);
        float oracleU2 = gX_OracleRandom(18);
        float oracleRadius = sqrt(oracleU1);
        float oraclePhi = 6.28318530718 * oracleU2;
        vec3 oracleLocal = vec3(oracleRadius * cos(oraclePhi),
            oracleRadius * sin(oraclePhi), sqrt(max(0.0, 1.0 - oracleU1)));
        vec3 oracleWi = normalize(oracleTangent * oracleLocal.x
            + oracleBitangent * oracleLocal.y + oracleN * oracleLocal.z);
        color.rgb = oracleRadiance + oracleAlbedo
            * gX_Oracle_AnalyticEnvironment(oracleWi);
    }
    if (oracleGgx == 1) {
        vec3 oracleN = normalize(normal);
        vec3 oracleV = normalize(pos.viewpos - pos.fragpos);
        vec3 oracleAlbedo = baseColor.rgb;
        float oracleMetalness = clamp(metalnessFactor, 0.0, 1.0);
        float oracleRoughness = clamp(roughness, 0.001, 1.0);
        vec3 oracleF0 = mix(vec3(0.04), oracleAlbedo, oracleMetalness);
        vec3 oracleSunL = normalize(pos.lightpos);
        vec3 oracleSunH = normalize(oracleV + oracleSunL);
        vec3 oracleRadiance = pbr_direct_light(
            oracleN, oracleV, oracleSunL, oracleSunH, oracleAlbedo,
            oracleF0, oracleRoughness, oracleMetalness,
            sunBrightness * sunColor.rgb * sunDiffuseFactor * sunDiffuseTint.rgb);
        for (int oracleLight = 0; oracleLight < lightsAmount; oracleLight++) {
            vec3 oracleDelta = L_POS(oracleLight) - pos.fragpos;
            float oracleDistance = max(length(oracleDelta), 0.000001);
            vec3 oracleL = oracleDelta / oracleDistance;
            vec3 oracleH = normalize(oracleV + oracleL);
            float oracleAttenDenom = L_ATTEN(oracleLight).x
                + L_ATTEN(oracleLight).y * oracleDistance
                + L_ATTEN(oracleLight).z * oracleDistance * oracleDistance;
            if (oracleAttenDenom <= 0.0) continue;
            float oracleAtten = 1.0 / oracleAttenDenom;
            vec3 oraclePointRadiance = oracleAtten * L_BRIGHT(oracleLight)
                * L_COLOR(oracleLight).rgb * L_DIFF(oracleLight);
            oracleRadiance += pbr_direct_light(
                oracleN, oracleV, oracleL, oracleH, oracleAlbedo,
                oracleF0, oracleRoughness, oracleMetalness,
                oraclePointRadiance);
        }
        color.rgb = oracleRadiance;
    }
    //opencode: M9 - linear HDR G-buffer. Exposure scales here (linear-safe)
    //   but the ACES tonemap moved to the bloom composite: it ran here AND
    //   there (double tonemap - hdrScene capped at 1.0 even sunlit, bloom
    //   extract starved to the 0.85-1.0 sliver). mirrorFill already emitted
    //   raw color, so all paths are consistent now; water/sky/particles
    //   never tonemapped (their linear output finally matches the models).
    color.rgb *= exposure;
    //opencode: dummy ref to prevent GLSL from eliminating lightsCookieMap
    color.rgb += texture(lightsCookieAtlas, lightsCookieRects[0].xy).rgb * 1e-6;
    color.rgb += float(lightsCookieEnabled[0]) * 1e-6;
    //opencode: NaN sanitize at end of main (NOT a graceful-degradation
    //   fallback - it never fires): 125 clustered pixels (button rim +
    //   figure silhouette, all-effects config) read back 0x7FFFFFFF from
    //   hdrScene with every effect, light, and env path bisected away, yet
    //   merely OBSERVING color via any(isnan()) suppresses generation
    //   entirely (0 non-finites, 0 red in trap form). Diagnosis: driver
    //   compiler optimization artifact on finite inputs (reassociation/
    //   vectorization class), not a shader-math bug - no finite-input op
    //   in this path can produce NaN (verified: meshes, attributes,
    //   factors, uniforms all finite; GGX/TBN/parallax guards hold).
    //   The observation constrains compilation to precise materialization;
    //   the black fallback additionally contains any residual case so a
    //   stray NaN can never again poison downstream kernels x74 (bloom
    //   item-11 / DOF item-12 class). Do not remove while the readback
    //   harness (GX_COMPOSITE_TEX_STATS nan-cells) exists to verify it.
    if (any(isnan(color.rgb))) { color.rgb = vec3(0.0); }
    //opencode: per-pixel roughness in the alpha (MRT loc-1 is driver-broken)
    //opencode: alpha stores roughness for receivers; values above one mark
    //   surfaces that explicitly opt out of SSR without a new render target.
    float ssrOutputAlpha = (receiveSSR == 1) ? ssrRoughness : 2.0;
    finalColor = vec4(color.rgb, (blendOpacity < 0.999) ? color.a : ssrOutputAlpha);
    //opencode: M1 - roughness-alpha repair (transparent pass 2): emit the
    //   material roughness so SSR reads roughness, not blended opacity.
    //   Coverage/alpha-test already resolved identically to pass 1.
    if (roughnessAlpha == 1) finalColor.a = ssrOutputAlpha;
    outRoughness = ssrRoughness;
    if (gXProbeCoverageCapture == 1) finalColor.a = 1.0;
    if (gXProbeCoverageCapture == 2)
        finalColor.a = length(fs_in.fragPosition - viewPosition);
    if (gXProbeProxyDiagnostic > 0 && gXProbeCoverageCapture == 0
        && enableReflections == 1)
        finalColor = gX_Proxy_Diagnostic(calc_ssr_normal(texcoord));
    if (gXProbeTraceMode > 0 && gXProbeTraceValid == 1
        && floor(gl_FragCoord.x) == gXProbeTracePixel.x
        && floor(gl_FragCoord.y) == gXProbeTracePixel.y) {
        if (gXProbeTraceMode == 1)
            finalColor = vec4(gXProbeTraceSampleDirection * 0.5 + 0.5, 1.0);
        else if (gXProbeTraceMode == 2)
            finalColor = vec4(gXProbeTraceSampleA, 1.0);
        else if (gXProbeTraceMode == 3)
            finalColor = vec4(gXProbeTraceRefDirection * 0.5 + 0.5, 1.0);
        else if (gXProbeTraceMode == 4)
            finalColor = vec4(gXProbeTraceHasSecondary == 1
                ? gXProbeTraceSampleDirectionB * 0.5 + 0.5
                : vec3(0.0), float(gXProbeTraceHasSecondary));
        else if (gXProbeTraceMode == 5)
            finalColor = vec4(gXProbeTraceSampleB,
                float(gXProbeTraceHasSecondary));
        else if (gXProbeTraceMode == 6)
            finalColor = vec4(gXProbeTraceBlendedSample,
                probeBlendB);
        else if (gXProbeTraceMode == 7)
            finalColor = vec4(gXProbeTraceWorldPosition, 1.0);
        else if (gXProbeTraceMode == 8)
            finalColor = vec4(gXProbeTraceCubeCenter, 1.0);
        else if (gXProbeTraceMode == 9)
            finalColor = vec4(gXProbeTraceCubeCenterB, 1.0);
        else if (gXProbeTraceMode == 10)
            finalColor = vec4(gXProbeTraceCubeSize, 1.0);
        else if (gXProbeTraceMode == 11)
            finalColor = vec4(gXProbeTraceExactDirectionA * 0.5 + 0.5,
                1.0);
        else if (gXProbeTraceMode == 12)
            finalColor = vec4(gXProbeTraceExactDirectionB * 0.5 + 0.5,
                float(gXProbeTraceHasSecondary));
        else if (gXProbeTraceMode == 13)
            finalColor = vec4(gXProbeTraceExactSampleA, 1.0);
        else if (gXProbeTraceMode == 14)
            finalColor = vec4(gXProbeTraceExactSampleB,
                float(gXProbeTraceHasSecondary));
        else if (gXProbeTraceMode == 15)
            finalColor = vec4(gXProbeTraceExactBlendedSample,
                probeBlendB);
    }
}
#endif // GX_GLFW_FRAG
