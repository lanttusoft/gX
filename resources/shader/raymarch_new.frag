#version 330
//ai: NVIDIA otherwise expands nested SDF/refinement/shadow loops into huge programs.
//   Unknown GLSL pragmas are ignored by other drivers.
#pragma optionNV(unroll none)
//ai: legacy/preset-only scenes do not compile the optional graph evaluator.
#ifndef CF_CSG_GRAPH
#define CF_CSG_GRAPH 0
#endif
//ai: hybrid depth masking is compiled only while a hybrid depth mode owns the
//   frame; a discard-bearing program loses the compositor layer draw, so the
//   standalone/composite permutation must stay discard-free.
#ifndef CF_HYBRID_MASK
#define CF_HYBRID_MASK 0
#endif
#if CF_OBJECTS || CF_LIGHTS
#include "common/sdf_basic.glsl"
#endif
#if CF_OBJECTS || CF_SDF_SMOOTH || CF_SDF_REPEAT
#include "common/sdf_ops.glsl"
#endif
#if CF_SDF_REPEAT
#include "common/sdf_deform.glsl"
#endif
#if CF_FBMNOISE3D || CF_CLOUDS
#include "common/noise.glsl"
#endif

/*new raymarch program*/
//opencode: CF_* flags injected by C code via gX_RendererRaymarch_CompileFlags_Apply
//opencode: Session 7 - inside-object rendering fix (backFace normal flip, forward-only march, abs stepping)

uniform float time =                0.0;
uniform vec2 res =                  vec2(1024,768);
uniform vec3 camPos=                vec3(0.0,0.0,0.0);
uniform vec3 camTarget =            vec3(0.0,0.0,1.0);
uniform vec3 camUp =                vec3(0.0,1.0,0.0);
uniform float camFov =              90.0;

//opencode: texture samplers for object texturing (triplanar/planar mapping)
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform sampler2D texNoise; //opencode: pre-generated noise texture (fast terrain)

//raymarch parameters
uniform int rmBounces =             1; 
uniform int rmSteps =               128;
uniform int rmStepsPerBounce =      0; //opencode: 0 = use global steps
uniform float rmNear =              0.01;
uniform float rmFar =               100.0;
uniform float rmStepMax =           0.0; //opencode: 0 = no clamp
uniform float rmDistTotal =         0.0; //opencode: 0 = use far
uniform float rmEpsilon =           0.001;
uniform float rmPolynomial[8]; //opencode: step LOD - correction = ? poly[j] ? bounceStep^j

//skybox : id = -3
uniform samplerCube texSky;

//sun
uniform float sunBright =           1.0;
uniform vec3 sunPos =               vec3(1.0,1.0,1.0);
uniform vec4 sunColor =             vec4(1.0,1.0,1.0,1.0);
uniform float sunSize =             1.0; //opencode: sun disk size / penumbra

//terrain : id = -1
uniform int terrainSeed =           0;
uniform vec3 terrainPos =           vec3(0.0,0.0,0.0);
uniform vec3 terrainScale =         vec3(1.0,1.0,1.0); //opencode
uniform vec4 terrainColor =         vec4(0.0,1.0,0.0,1.0);
uniform vec4 terrainColor_s =       vec4(1.0,1.0,0.0,1.0);
uniform vec4 terrainColor_c =       vec4(0.5,0.5,0.5,1.0);
uniform vec4 terrainParameters =    vec4(320.0,0.001,6.0,0.0); //opencode: x=amp, y=freqScale, z=octaves
uniform vec4 terrainRotate =        vec4(0.0,0.0,0.0,1.0); //opencode: terrain noise rotation quaternion (xyzw)

//water : id = -2
uniform vec3 waterPos =             vec3(0.0,0.0,0.0);
uniform vec4 waterColor =           vec4(0.0,0.0,1.0,1.0);
uniform vec4 waterRipple =          vec4(0.1,20.0,0.0,0.0);
uniform vec4 waterWhirlpool =       vec4(0.0,0.0,0.0,0.0);
uniform float waterSpeed =          0.0; //opencode: ripple animation speed multiplier
uniform int waterSeed =             0; //opencode: water noise seed
uniform float gradClamp =           0.2; //opencode: water gradient correction clamp

//objects : id = 0-7
uniform int objectAmount =          8;
uniform int objectType[8];
uniform vec3 objectPos[8];
uniform vec3 objectMatX[8];
uniform vec3 objectMatY[8];
uniform vec3 objectMatZ[8];
uniform vec4 objectColor[8];
uniform vec4 objectAttribute[8]; //"mesh" attributes
uniform vec4 objectMaterial[8]; //what material
//ai: roughness is not inferred from reflectivity/refraction/IOR/emission.
uniform float objectRoughness[8];
uniform vec4 objectLighting[8]; //lighting parameters
uniform vec4 objectTexturing[8]; //opencode: x=texIndex, y=mode(0=off,1=triplanar,2=planarXZ,3=planarXY,4=planarYZ), z=scale, w=blend

//ai: only reachable, validated nodes are uploaded; the root is the last instruction.
uniform int graphCount = 0;
uniform vec4 graphNodes[32];
uniform vec4 graphDomains[32];

//lights : id = 8-15
uniform int lightAmount =           8;
uniform float lightBright[8];
uniform vec3 lightPos[8];
uniform vec4 lightColor[8];
uniform vec4 lightAmbient =         vec4(1.0,1.0,1.0,1.0);

//fog
uniform float fogNear =             10.0;
uniform float fogFar =              100.0;
uniform vec4 fogColor =             vec4(1.0,1.0,1.0,1.0);

//mode
uniform int enableShadows =         1;
uniform int enableGlow =            1;
uniform int enableLightGlow =       1;
uniform int enableIllu =            1;
uniform int enableFrag =            1;
uniform int enableGrid =            1;
uniform int enableLastStepSample =  1;
uniform int enableSignSample =      1;
uniform int enableFog =             1;
uniform int enableTerrain =         1;
uniform int enableWater =           1;
uniform int enableObjects =         1;
uniform int enableLights =          1;
uniform int enableTexsky =          0;
uniform int enableAO =              0; //opencode
uniform int enableLightShadows =    0; //opencode: separate from sun shadows
uniform int enableProceduralSky =   1; //opencode
#if CF_CLOUDS
//ai: volumetric clouds — a procedural density slab marched after the
//   atmosphere. Off by default; the module/panel/preset own the values.
uniform int   enableClouds =        0;
uniform float cloudBottom =         12.0;
uniform float cloudTop =            42.0;
uniform float cloudCoverage =       0.50;
uniform float cloudDensity =        1.20;
uniform float cloudScale =          0.060;
uniform float cloudSpeed =          0.02;
uniform float cloudAbsorb =         0.70;
uniform float cloudPowder =         0.35;
uniform vec4  cloudColor =          vec4(1.0,1.0,1.0,1.0);
#endif
#if CF_VOLFOG
//ai: volumetric height fog with sun in-scattering — mirrors the gfx
//   fog_volume semantics (density / height falloff / scattering / HG lobe).
uniform int   enableVolFog =             0;
uniform float volFogDensity =            0.010;
uniform float volFogHeightFalloff =      1.0;
uniform float volFogScattering =         1.0;
uniform float volFogHG =                 0.55;
uniform vec4  volFogColor =              vec4(1.0, 0.94, 0.86, 1.0);
#endif
uniform vec4 firstColor       =     vec4(0.0,0.0,0.0,1.0);
uniform vec4 nullColor        =     vec4(1.0,1.0,1.0,1.0);
uniform vec3 lightAttenuation =     vec3(0.0,0.0,2.0);

//debug
uniform int debugShowPos = 0;
uniform int debugShowDir = 0;
uniform int debugShowRay = 0;
uniform int debugShowGradient = 0;
uniform int debugShowNormal = 0;
uniform int debugShowBounceCount = 0;
uniform int debugShowPathLength = 0;
uniform int debugShowPathSteps = 0;
uniform int debugShowDist = 0;

in vec2 fragTexCoord;
in vec3 fragPosition,fragNormal;
in vec4 fragColor;
out vec4 finalColor;

//------------------------------------------------------------------------------------------------------//
//----------UTILITIES----------//
const float pi = 3.141592;
//ai: opaque hybrid modes share the primary camera; mode 2 exports depth through hardware testing.
uniform int hybridDepthEnabled;
uniform sampler2D hybridRasterDepth;
uniform vec2 hybridClip;
//ai: shared static-camera reprojection uses the same clip-space jitter as raster beauty.
uniform vec2 hybridJitter;
uniform int hybridTemporalAnimated;
vec3 rayDir(float fov, vec2 size, vec2 pos) {
    vec2 xy = pos - 0.5*size;
    float a = tan(pi/2 - 0.5*fov);
    float z = 0.5 * size.y * a;
    return normalize(vec3(xy,-z));
}
mat3 setCamera( vec3 pos, in vec3 target, in vec3 up) {
    vec3 forward = normalize(target-pos);
    vec3 right = normalize(cross(forward,up));
    vec3 upward = normalize(cross(right,forward));
    return mat3( right, upward, -forward );
}
//opencode: HSV color math
#if CF_HSV
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
#endif
//ai: elevation-correct daylight/twilight sky; the reflected environment uses this same function.
vec3 proceduralSky(vec3 dir) {
    vec3 sunDir = sunPos / max(length(sunPos), 0.00001);
    float daylight = smoothstep(-0.12, 0.18, sunDir.y);
    float elevation = max(dir.y, 0.0);
    float horizonBand = exp(-elevation * 5.0);
    float twilight = exp(-abs(sunDir.y + 0.025) * 18.0);
    vec3 zenith = mix(vec3(0.008,0.012,0.035), vec3(0.08,0.25,0.55), daylight);
    vec3 horizon = mix(vec3(0.025,0.035,0.06), vec3(0.55,0.68,0.78), daylight);
    vec3 skyColor = mix(zenith, horizon, horizonBand);
    float towardSun = max(dot(dir.xz, sunDir.xz), 0.0);
    skyColor += vec3(0.65,0.18,0.045) * twilight * horizonBand * towardSun;
    float sunDot = dot(dir, sunDir);
    float sunDisk = smoothstep(1.0 - max(sunSize,0.0001) * 0.002, 1.0, sunDot);
    float visible = smoothstep(-0.03,0.02,sunDir.y);
    skyColor += sunDisk * sunColor.rgb * sunBright * visible;
    float glow = exp(-max(1.0 - sunDot, 0.0) * 80.0);
    skyColor += glow * sunColor.rgb * 0.06 * visible;
    skyColor = mix(skyColor, vec3(0.035,0.045,0.035) * (0.1+0.9*daylight),
        1.0-smoothstep(-0.15,0.0,dir.y));
    return max(skyColor, vec3(0.0));
}
//opencode: unique SDFs (not in includes) - basic SDFs from common/sdf_basic.glsl and
//   sdf_ops.glsl (gX_SDF_RepeatInf, gX_SDF_SmoothUnion etc.) are always available via includes.
//stackoverflow
//para: x=? y=power z=limit w=breakaway
float sdMandelbulb( vec3 p, vec4 para ) {
    //ai: atan(y,x) preserves quadrants; finite pole/origin values prevent NaN holes.
    vec3 z = p;
    float r = max(length(z), 0.000001);
    float zr,theta,phi;
    float dr = max(para.x, 1.0);
    float power = max(para.y, 2.0);
    int lim = clamp(int(para.z), 1, 32);
    for (int i = 0; i < lim; i++) {
        r = max(length(z), 0.000001);
        if (r > max(para.w, 2.0)) break;
        theta = acos(clamp(z.z / r, -1.0, 1.0)) * power;
        phi = atan(z.y, z.x) * power;
        zr = pow(r, power); 
        dr = pow(r, power-1) * power * dr + 1;
        z = zr * vec3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta)); 
        z += p;
    }
    return 0.5 * log(r) * r / max(dr, 0.000001);
}
//opencode: procedural tree - stacked cones + cylindrical trunk
//   ported from raymarch.frag
#if CF_OBJECTS || CF_VEGETATION
float sdTree(vec3 p) {
    float crown = gX_SDF_Cone(p - vec3(0,-0.2,0), vec3(1.0,1.0,0.50));
    crown = min(crown, gX_SDF_Cone(p - vec3(0,-0.5,0), vec3(1.0,1.0,0.75)));
    crown = min(crown, gX_SDF_Cone(p - vec3(0,-1.0,0), vec3(1.0,1.0,1.00)));
    float trunk = gX_SDF_Cylinder(p - vec3(0,-2.0,0), vec2(0.25,0.5));
    return (crown < trunk) ? crown : trunk;
}
#endif

//opencode: procedural vegetation - sdConifer (detailed frond tree) + sdHavu (branch frond)
#if CF_VEGETATION
//opencode: cartesian (x,y,z) to cylindrical (radial, angle, up) coordinates - used by sdHavu
vec3 rmCartesianToCylinder(vec3 p) {
    float radius = length(p.xz);
    float angle = atan(p.z, p.x);
    return vec3(radius, angle, p.y);
}

//opencode: 3D rotation matrix from Euler angles (radians) - used by sdHavu
mat3 rmRotationXYZ(vec3 angles) {
    float cx = cos(angles.x), sx = sin(angles.x);
    float cy = cos(angles.y), sy = sin(angles.y);
    float cz = cos(angles.z), sz = sin(angles.z);
    return mat3(
        cy*cz, cy*sz, -sy,
        sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, sx*cy,
        cx*sy*cz + sx*sz, cx*sy*sz - sx*cz, cx*cy
    );
}

//opencode: sdHavu - a single frond (conifer branch segment) in cylindrical space
//   Ported from raymarch.frag (old shader lines 484-504).
//   The 'r' output passes the transformed position for chained iterations.
float sdHavu(vec3 p, inout vec3 r, float scale) {
    vec3 x = rmCartesianToCylinder(p);
    float c = 2.0 * 3.14159265 / 9.0;
    x.y = mod(x.y + 0.5 * c, c) - 0.5 * c;
    x += 0.2 * x.z;
    float d = scale;
    float a = 5.0;
    x.z = x.z - d * clamp(round(x.z / d), -a, a);
    x.y *= x.x;
    x.x -= scale;
    x.xyz = x.yxz;
    x *= rmRotationXYZ(vec3(2.0 * 3.14159265 / 3.0, 0.0, x.z));
    r = x; // chain: output transformed position for next iteration
    return gX_SDF_Cylinder(x, vec2(0.1 * scale, 0.5 * scale));
}

//opencode: sdConifer - detailed procedural conifer tree with stacked fronds
//   Ported from raymarch.frag (old shader lines 510-547).
//   Uses shared noise functions from common/noise.glsl for forest floor distribution.
//   Single-instance SDF - caller handles placement and scale via object transform.
float sdConifer(vec3 p) {
    float result = 1000.0;
    float scale = 1.0;
    float scalefactor = 0.25;

    // Trunk
    result = gX_SDF_Cylinder(p, vec2(0.1, scale));
    scale *= scalefactor;

    // Branches via sdHavu - stacked fronds with decreasing scale
    //   Chained: first call transforms p into r0, second call transforms r0 into r1
    vec3 r0 = p;
    vec3 r1 = vec3(0.0);
    int iterations = 2;
    for (int i = 0; i < iterations; i++) {
        float test;
        if (i % 2 == 0) test = sdHavu(r0, r1, scale);
        else            test = sdHavu(r1, r0, scale);
        if (test < result) result = test;
        scale *= scalefactor;
    }
    return result;
}
#endif //CF_VEGETATION

//opencode: FBM noise (slow link) or sin-based (fast link)
#if CF_FBMNOISE
float rmHash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
float rmRand(vec2 p) {
    return rmHash21(p + 0.1 + float(terrainSeed));
}
float rmNoise(vec2 p, float freq) {
    float unit = 1.0/freq;
    vec2 ij = floor(freq*p);
    vec2 xy = mod(p, unit)/unit;
    xy = smoothstep(0.0, 1.0, xy);
    float a = rmRand(ij+vec2(0.0,0.0));
    float b = rmRand(ij+vec2(1.0,0.0));
    float c = rmRand(ij+vec2(0.0,1.0));
    float d = rmRand(ij+vec2(1.0,1.0));
    float x1 = mix(a,b,xy.x);
    float x2 = mix(c,d,xy.x);
    return mix(x1,x2,xy.y);
}
float rmPNoise(vec2 p, int res) {
    float persistance = 0.5;
    float n = 0.0;
    float normK = 0.0;
    float f = 4.0;
    float amp = 1.0;
    for (int i = 0; i < 6; i++) {
        if (i >= res) break;
        n += amp * rmNoise(p, f);
        f *= 2.0;
        normK += amp;
        amp *= persistance;
    }
    if (normK == 0.0) return 1.0;
    float t = n / normK;
    return t * t * t * t;
}
#else
float rmPNoise(vec2 p, int res) {
    //opencode: sin-based terrain - fast link, no hash/smoothstep/loop overhead
    float h = 0.0;
    h += sin(p.x * 0.100 + p.y * 0.050 + float(terrainSeed)) * 0.50;
    h += sin(p.x * 0.050 + p.y * 0.100) * 0.25;
    h += cos(p.x * 0.030 - p.y * 0.070) * 0.12;
    h += sin(p.x * 0.200 + p.y * 0.150 + 1.0) * 0.06;
    return h * h * h * h;
}
#endif
#define RM_HASH (fract(dot(gl_FragCoord.xy, vec2(0.1031, 0.1030)) * 43758.5453))
//opencode: rotate 2D vector by quaternion (Y-axis centered)
vec2 rotByQuat(vec2 v, vec4 q) {
    float a = 2.0 * acos(clamp(q.w, -1.0, 1.0));
    float c = cos(a), s = sin(a);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float rmSdTerrain(vec3 p, float dist) {
    //ai: height scale is independent of horizontal noise frequency on every path.
    float amp = max(terrainParameters.x, 0.0) * terrainScale.y;
    float freqScale = terrainParameters.y > 0.0 ? terrainParameters.y : 0.001;
    vec2 sampleXZ = rotByQuat(p.xz, terrainRotate);
#if CF_NOISE_TEX
    //opencode: tileable 4-octave noise texture (cells 2,4,8,16 - wraps seamlessly).
    //   UV 0.01 -> base octave cell ? 50 world units; freqScale scales the field.
    float uvScale = 0.01 * (freqScale > 0.0 ? freqScale / 0.001 : 1.0);
    //ai: distance and normal probes must sample the same field in divergent loops.
    vec4 n = textureLod(texNoise, sampleXZ * terrainScale.xz * uvScale, 0.0);
    float h = (n.r * 0.5 + n.g * 0.25 + n.b * 0.125 + n.a * 0.0625) / 0.9375;
    h = h * 2.0 - 1.0;
    //ai: bound the baked octave slopes (including 8-bit interpolation error).
    float slopeBound = 16.0 * abs(amp * uvScale)
        * max(abs(terrainScale.x), abs(terrainScale.z));
    return (p.y - amp * h) / sqrt(1.0 + 2.0 * slopeBound * slopeBound);
#elif CF_FBMNOISE3D
    float n = 0.0;
    float f = freqScale * terrainScale.x;
    float a = 1.0;
    int oct = int(terrainParameters.z);
    if (oct < 1) oct = 6;
    //ai: distance-dependent octave changes alter the surface during a ray's march.
    for (int i = 0; i < oct && i < 6; i++) {
        n += a * gX_Noise_Simplex(vec3(sampleXZ * f, p.y * f * 0.3));
        f *= 2.0;
        a *= 0.5;
    }
    return p.y - amp * n;
#else
    vec2 uv = freqScale * terrainScale.xz * sampleXZ;
    return p.y - amp * rmPNoise(uv, int(terrainParameters.z));
#endif
}

struct rmData {
    int doMarch;
    
    //ray data
    vec3 pos; //ray starting position
    vec3 dir; //ray direction to march towards
    vec3 ray; //current ray position
    vec3 gradient; //current gradient
    vec3 normal; //current normal
    
    //path data
    int bounceCount;
    int pathSteps; //steps taken in this bounce
    int pathStepsTotal; //total steps across all bounces
    float pathLength; //distance marched in this bounce
    float pathLengthTotal; //total distance across all bounces
    float distFromPos;
    
    //object index data
    float currentObjectDist; //current closest object distance
    int currentObjectIndex; //what type of object it is
    float minObjectDist; //minimum object distance on current path
    int minObjectIndex; //what type of object it was
    
    //misc
    float firstSign; //initial sign
    float marchSign; //positive if outside, negative if inside
    float marchSignPrev; //previous sign (used by material() for refraction)
    float refractionIndex; //current material refraction index
    vec4 color; //current ray color
    vec4 colorAccumulated;
    vec4 colorFactor;
    int backFace; //opencode: 1 if ray started inside an object
};

void init(inout rmData data) {
    data.doMarch = 1;
    
    data.pos = vec3(0.0);
    data.dir = vec3(0.0);
    data.ray = vec3(0.0);
    data.gradient = vec3(0.0);
    data.normal = vec3(0.0);
    
    data.bounceCount = 0;
    data.pathSteps = 0;
    data.pathStepsTotal = 0;
    data.pathLength = 0.0;
    data.pathLengthTotal = 0.0;
    data.marchSignPrev = 1.0;
    data.distFromPos = 0.0;
    
    data.currentObjectDist = rmFar;
    data.currentObjectIndex = 0;
    data.minObjectDist = rmFar; //opencode: use rmFar so first absDist always updates
    data.minObjectIndex = 0;
    
    data.firstSign = 1.0;
    data.marchSign = 1.0;
    data.marchSignPrev = 1.0;
    data.refractionIndex = 1.0;
    data.color = vec4(0.0,0.0,0.0,1.0);
    data.colorAccumulated = vec4(0.0,0.0,0.0,0.0);
    data.colorFactor = vec4(1.0,1.0,1.0,1.0);
    data.backFace = 0; //opencode
}

//opencode: added whirlpool + gradient-corrected water SDF
#if CF_WATER
float waterfunc(vec2 coord) {
    //opencode: ripple - waterRipple.xy = (amplitude, frequency), .zw = (posX, posY)
    vec2 rc = coord - waterRipple.zw + vec2(float(waterSeed) * 0.1); //opencode: seed offsets ripple origin
    float r2 = dot(rc, rc);
    float ring = sqrt(r2 + 1.0);
    float rAmp = waterRipple.x;
    float rFreq = waterRipple.y;
    float f = rAmp * sin(rFreq * ring - pi * time * waterSpeed) / (ring * ring + 1.0);
    //ai: low-frequency crossing swells remain visible away from the ripple origin.
    float phase = time * waterSpeed;
    float frequency = max(abs(rFreq) * 0.05, 0.05);
    f += rAmp * (0.6*sin(dot(coord,vec2(0.8,0.6))*frequency-phase)
        + 0.35*sin(dot(coord,vec2(-0.6,0.8))*frequency*1.7-phase*1.3));
    //opencode: 1/x? whirlpool - waterWhirlpool.xy = (strength, scale), .zw = (posX, posY)
    vec2 wc = coord - waterWhirlpool.zw;
    float r = length(wc) + 0.001;
    float wp = waterWhirlpool.x / (r * r);
    f -= wp * waterWhirlpool.y;
    return f;
}

//opencode: water height-field gradient via finite differences
vec2 waterGrad(vec2 xz) {
    float d = 0.05;
    float s = waterfunc(xz);
    vec2 g;
    g.x = (waterfunc(xz + vec2(d, 0.0)) - s) / d;
    g.y = (waterfunc(xz + vec2(0.0, d)) - s) / d;
    return g;
}
#endif //CF_WATER

#if CF_CLOUDS
//ai: 3D simplex FBM in [0,1] (gX_Noise_Simplex returns 0..1); four octaves
//   keep the sun-scatter march affordable at the default render scale.
float rmCloudFbm(vec3 p, int octaves) {
    //ai: normalized to [0,1] with mean 0.5 (the raw sum's mean is ~0.47,
    //   which silently starved any coverage threshold at 0.5).
    float value = 0.0;
    float amplitude = 0.5;
    float norm = 0.0;
    for (int i = 0; i < 5; i++) {
        if (i >= octaves) break;
        value += amplitude * gX_Noise_Simplex(p);
        norm += amplitude;
        p = p * 2.07 + vec3(17.3, 9.1, 13.7);
        amplitude *= 0.5;
    }
    return norm > 0.0 ? value / norm : 0.0;
}

float rmCloudDensity(vec3 p) {
    if (p.y < cloudBottom || p.y > cloudTop) return 0.0;
    float h = (p.y - cloudBottom) / max(cloudTop - cloudBottom, 0.001);
    float profile = smoothstep(0.0, 0.22, h) * (1.0 - smoothstep(0.55, 1.0, h));
    vec3 q = p * cloudScale
        + vec3(time * cloudSpeed, 0.0, time * cloudSpeed * 0.4);
    float n = rmCloudFbm(q, 4);
    //ai: coverage is the threshold on the normalized field — higher covers
    //   more sky, with a fixed 0.25 ramp for soft edges.
    float cover = smoothstep(1.0 - cloudCoverage,
        1.0 - cloudCoverage + 0.15, n);
    return profile * cover * cloudDensity;
}

//ai: short sun march — four density samples toward the sun give the
//   self-shadow that makes the tops bright and the bases grey. Each sample
//   integrates a 5-unit step of extinction (density is per world unit).
float rmCloudSunTransmit(vec3 p) {
    vec3 sunDir = normalize(sunPos);
    float transmit = 1.0;
    for (int i = 0; i < 4; i++) {
        float t = 2.0 + 3.5 * float(i);
        transmit *= exp(-rmCloudDensity(p + sunDir * t)
            * cloudAbsorb * 3.5);
    }
    return transmit;
}

//ai: slab march from the atmosphere into the scene colour. 28 fixed steps
//   with an early-out below 2% transmittance; the phase term keeps the sun
//   side glowing (forward scattering) while edges stay opaque.
vec4 rmCloudMarch(vec3 ro, vec3 rd, vec4 sceneColor, float maxDist) {
    float denom = rd.y;
    if (abs(denom) < 0.0001) {
        if (ro.y < cloudBottom || ro.y > cloudTop) return sceneColor;
        denom = 0.0001;
    }
    float t0 = (cloudBottom - ro.y) / denom;
    float t1 = (cloudTop - ro.y) / denom;
    if (t0 > t1) { float swap = t0; t0 = t1; t1 = swap; }
    t0 = max(t0, 0.0);
    t1 = min(t1, maxDist);
    if (t1 <= t0) return sceneColor;
    vec3 sunDir = normalize(sunPos);
    float phase = 0.5 + 0.5 * pow(max(dot(rd, sunDir), 0.0), 6.0);
    vec3 sun = sunColor.rgb * sunBright;
    vec3 scatter = vec3(0.0);
    float transmit = 1.0;
    const int RM_CLOUD_STEPS = 28;
    float dt = (t1 - t0) / float(RM_CLOUD_STEPS);
    for (int i = 0; i < RM_CLOUD_STEPS; i++) {
        vec3 p = ro + (t0 + (float(i) + 0.5) * dt) * rd;
        float d = rmCloudDensity(p);
        if (d > 0.001) {
            float sunT = rmCloudSunTransmit(p);
            //ai: lit tops (forward phase) + ambient sky fill so the bases
            //   stay grey instead of black.
            vec3 light = sun * sunT * (0.60 + 0.40 * phase)
                + lightAmbient.rgb * 0.45;
            float powder = 1.0 - cloudPowder * exp(-d * 8.0);
            scatter += light * powder * d * dt * transmit * cloudColor.rgb;
            transmit *= exp(-d * cloudAbsorb * dt);
            if (transmit < 0.02) break;
        }
    }
    return vec4(sceneColor.rgb * transmit + scatter, sceneColor.a);
}
#endif

#if CF_VOLFOG
//ai: forward declaration — the fog primary march calls map() (defined below).
float map(vec3 ray, inout rmData data);

//ai: Henyey-Greenstein phase (4PI-normalized), same form as gfx fog_volume.
float rmVolFogHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2)
        / (12.566370614 * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

//ai: soft sun visibility via the scene SDF (3 taps) — occluded steps
//   contribute nothing, which carves the shafts out of the volume.
float rmVolFogSunVis(vec3 p) {
    vec3 sunDir = normalize(sunPos);
    rmData probe;
    init(probe);
    float vis = 1.0;
    for (int i = 0; i < 3; i++) {
        float t = 1.5 + 3.0 * float(i);
        float m = map(p + sunDir * t, probe);
        if (m < rmEpsilon) return 0.0;
        vis = min(vis, clamp(m / (t * 0.4), 0.0, 1.0));
    }
    return vis;
}

//ai: height-fog march from the camera to the surface (or maxDist for sky).
//   Height is anchored at eye level - 1.5 (the gfx fog_volume rule) so scenes
//   at any altitude keep their fog band. 20 steps, 3-tap sun visibility.
vec4 rmVolFogMarch(vec3 ro, vec3 rd, vec4 sceneColor, float dist) {
    if (dist <= 0.0) return sceneColor;
    const int RM_VOLFOG_STEPS = 20;
    float dt = dist / float(RM_VOLFOG_STEPS);
    float heightBaseEye = ro.y - 1.5;
    vec3 sunDir = normalize(sunPos);
    vec3 sun = sunColor.rgb * sunBright;
    float cosTheta = dot(rd, sunDir);
    //ai: 0.25 isotropic floor so the haze reads away from the sun while the
    //   HG lobe still carves the shafts; the pi factor turns the 4PI-
    //   normalized lobe into an albedo-like scale (peak capped like the gfx
    //   fog_volume).
    float phase = mix(rmVolFogHG(cosTheta, volFogHG),
        1.0 / 12.566370614, 0.25) * 3.14159265;
    phase = min(phase, 2.0);
    vec3 inscattered = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < RM_VOLFOG_STEPS; i++) {
        vec3 p = ro + (float(i) + 0.5) * dt * rd;
        float height = max(p.y - heightBaseEye, 0.0);
        float stepDensity = volFogDensity
            * exp(-height * volFogHeightFalloff) * dt;
        if (stepDensity <= 0.00001) continue;
        transmittance *= exp(-stepDensity);
        float vis = rmVolFogSunVis(p);
        //ai: sun term (phase-weighted, SDF-shadowed) plus a sky-ambient fill
        //   standing in for multiple scattering, so the haze reads bright
        //   even away from the sun instead of only darkening.
        vec3 lit = sun * volFogScattering * phase * vis
            + lightAmbient.rgb * 0.35;
        inscattered += lit * volFogColor.rgb * stepDensity * transmittance;
        if (transmittance < 0.02) break;
    }
    return vec4(sceneColor.rgb * transmittance + inscattered,
        sceneColor.a);
}
#endif

//ai: the same primitive field serves independent objects and graph leaves.
#if CF_OBJECTS
float rmPrimitive(vec3 d, int i) {
    vec4 a = objectAttribute[i];
    switch (objectType[i]) {
        case 0: return gX_SDF_Sphere(d,a.x);
        case 1: return gX_SDF_Cube(d,a.xyz);
        case 2: return gX_SDF_Torus(d,a.xy);
        case 3: return gX_SDF_BoxFrame(d,a.xyz,a.w);
        case 4: return gX_SDF_Cylinder(d,a.xy);
        case 5: return gX_SDF_Cone(d,vec3(a.xy/max(length(a.xy),0.000001),a.z));
        case 6: return sdTree(d);
        case 7: return sdMandelbulb(d,a);
#if CF_VEGETATION
        case 8: return sdConifer(d);
#endif
        case 9: return gX_SDF_Menger(d,a.xyz);
        case 10: return gX_SDF_Capsule(d,vec3(0,-a.x,0),vec3(0,a.x,0),a.y);
        case 11: return gX_SDF_Octahedron(d,a.x);
        case 12: return gX_SDF_RoundBox(d,a.xyz,a.w);
        case 13: return gX_SDF_HexPrism(d,a.xy);
    }
    return rmFar;
}

//ai: neighbor-cell evaluation avoids unsafe discontinuous nearest-cell distance guesses.
float rmObjectDistance(vec3 ray, int i, int operation, vec4 domain) {
    vec3 d = ray-objectPos[i];
    float angle = float(i)*1.256, c = cos(angle), s = sin(angle);
    d = vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    mat3 transform = mat3(objectMatX[i],objectMatY[i],objectMatZ[i]);
    d *= transform;
    float result = rmFar;
    if (operation < 8) result = rmPrimitive(d,i);
#if CF_CSG_GRAPH
    else {
        vec3 period = domain.xyz;
        ivec3 limits = ivec3(int(domain.w)&255,(int(domain.w)>>8)&255,(int(domain.w)>>16)&255);
        vec3 cell = round(d/max(period,vec3(0.000001)));
        cell = mix(vec3(0),cell,greaterThan(period,vec3(0)));
        if (operation == 9) cell = clamp(cell,-vec3(limits),vec3(limits));
        for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
            ivec3 offset = ivec3(x,y,z);
            if ((period.x == 0 && x != 0) || (period.y == 0 && y != 0) || (period.z == 0 && z != 0)) continue;
            vec3 neighbor = cell+vec3(offset);
            if (operation == 9 && any(greaterThan(abs(neighbor),vec3(limits)))) continue;
            result = min(result,rmPrimitive(d-neighbor*period,i));
        }
    }
#endif
    mat3 gram = transpose(transform)*transform;
    vec3 bounds = abs(gram[0])+abs(gram[1])+abs(gram[2]);
    return result/max(sqrt(max(bounds.x,max(bounds.y,bounds.z))),0.000001);
}

//ai: topological evaluation carries the winning material; difference cut faces retain A's material.
#if CF_CSG_GRAPH
vec2 rmGraphDistance(vec3 ray) {
    vec2 values[32];
    for (int n = 0; n < graphCount; n++) {
        vec4 node = graphNodes[n];
        int operation = int(node.x), ia = int(node.y), ib = int(node.z);
        if (operation == 1 || operation >= 8) {
            values[n] = vec2(rmObjectDistance(ray,ia,operation,graphDomains[n]),float(ia));
        } else {
            vec2 a = values[ia], b = values[ib];
            bool maximum = operation == 3 || operation == 4 || operation == 6 || operation == 7;
            float operandB = operation == 3 || operation == 6 ? -b.x : b.x;
            float distance = maximum ? max(a.x,operandB) : min(a.x,operandB);
            if (operation >= 5 && node.w > 0) {
                float h = max(node.w-abs(a.x-operandB),0.0)/node.w;
                distance += (maximum ? 1.0 : -1.0)*node.w*h*h*0.25;
            }
            float material = operation == 3 || operation == 6 ? a.y
                : ((maximum ? a.x >= b.x : a.x <= b.x) ? a.y : b.y);
            values[n] = vec2(distance,material);
        }
    }
    return values[graphCount-1];
}
#endif
#endif

float map(vec3 ray, inout rmData data) {
    float result = rmFar;
    //ai: a map miss must not inherit the previous probe's material index.
    data.currentObjectIndex = -3;
    float temp = result;
    
    //opencode: terrain SDF with LOD
    if (enableTerrain == 1) {
        temp = rmSdTerrain(ray - terrainPos, data.distFromPos);
        if (temp < result) {result = temp; data.currentObjectIndex = -1;}
    }
    
    //opencode: water SDF - gradient correction near surface
#if CF_WATER
    if (enableWater == 1) {
        vec3 wp = ray - waterPos;
        vec2 xz = wp.xz;
        float wfunc = waterfunc(xz);
        wp.y += wfunc;
        float wdist = abs(wp.y);
        if (wdist < 20.0) {
            vec2 wg = waterGrad(xz);
            float gradLen = 1.0 + dot(wg, wg);
            float corr = max(1.0 / sqrt(gradLen), gradClamp);
            temp = wp.y * corr;
        } else {
            temp = wp.y;
        }
        if (temp < result) {result = temp; data.currentObjectIndex = -2;}
    }
#endif
    
#if CF_OBJECTS
    if (enableObjects == 1) {
#if CF_CSG_GRAPH
        if (graphCount > 0) {
            vec2 graph = rmGraphDistance(ray);
            if (graph.x < result) {result = graph.x; data.currentObjectIndex = int(graph.y);}
        } else {
#endif
            for (int i = 0; i < objectAmount; i++) {
                temp = rmObjectDistance(ray,i,1,vec4(0));
                if (temp < result) {result = temp; data.currentObjectIndex = i;}
            }
#if CF_CSG_GRAPH
        }
#endif
    }
#endif
#if CF_LIGHTS
    if (enableLights == 1) {
        for (int i = 0; i < lightAmount; i++) {
            temp = gX_SDF_Sphere(ray-lightPos[i],0.1*log(lightBright[i]+1.0));
            if (temp < result) {result = temp; data.currentObjectIndex = i+8;}
        }
    }
#endif
    
    return result;
}

vec3 calc_gradient(vec3 ray, inout rmData data) {
    //ai: symmetric probes avoid a biased normal and preserve the hit metadata.
    rmData probe = data;
    float d = data.currentObjectIndex == -1 ? max(rmEpsilon, 0.1)
        : max(rmEpsilon, 0.0001);
    //ai: keep one shared pair of scene evaluations rather than six expanded copies.
    vec3 gradient = vec3(0.0);
    for (int axis = 0; axis < 3; axis++) {
        vec3 offset = vec3(0.0);
        offset[axis] = d;
        gradient[axis] = map(ray+offset,probe)-map(ray-offset,probe);
    }
    return gradient / (2.0 * d);
}

//opencode: triplanar texture mapping - blends samples from 3 axes by normal weight
vec3 triplanarTex(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 sp = p * scale;
    vec3 blend = abs(n);
    blend = pow(blend, vec3(4.0));
    blend /= dot(blend, vec3(1.0));
    vec3 x = texture(tex, sp.yz).rgb;
    vec3 y = texture(tex, sp.xz).rgb;
    vec3 z = texture(tex, sp.xy).rgb;
    return x * blend.x + y * blend.y + z * blend.z;
}

//opencode: apply per-object texture mapping based on objectTexturing parameters
vec3 applyObjectTexture(int t, vec3 baseColor, vec3 pos, vec3 norm) {
    vec4 ti = objectTexturing[t];
    int texIdx = int(ti.x);
    int mode = int(ti.y);
    if (mode == 0 || texIdx < 0 || texIdx > 2) return baseColor;
    float scale = ti.z > 0.0 ? ti.z : 1.0;
    vec3 tc;
    //opencode: GLSL 330 requires compile-time sampler selection
    if (texIdx == 0) {
        if (mode == 1) tc = triplanarTex(tex0, pos, norm, scale);
        else if (mode == 2) tc = texture(tex0, pos.xz * scale).rgb;
        else if (mode == 3) tc = texture(tex0, pos.xy * scale).rgb;
        else if (mode == 4) tc = texture(tex0, pos.yz * scale).rgb;
        else return baseColor;
    } else if (texIdx == 1) {
        if (mode == 1) tc = triplanarTex(tex1, pos, norm, scale);
        else if (mode == 2) tc = texture(tex1, pos.xz * scale).rgb;
        else if (mode == 3) tc = texture(tex1, pos.xy * scale).rgb;
        else if (mode == 4) tc = texture(tex1, pos.yz * scale).rgb;
        else return baseColor;
    } else {
        if (mode == 1) tc = triplanarTex(tex2, pos, norm, scale);
        else if (mode == 2) tc = texture(tex2, pos.xz * scale).rgb;
        else if (mode == 3) tc = texture(tex2, pos.xy * scale).rgb;
        else if (mode == 4) tc = texture(tex2, pos.yz * scale).rgb;
        else return baseColor;
    }
    return mix(tc, baseColor, clamp(ti.w, 0.0, 1.0));
}

void frag(inout rmData data) {   //color objects
    vec4 color = vec4(0.0,0.0,0.0,0.0);
    int t = data.currentObjectIndex;
    if (t == -1) { //terrain
        vec4 sandcolor = terrainColor_c;
        vec4 cliffcolor = terrainColor_s;
        vec4 groundcolor = terrainColor;
        //opencode: height above terrain base (terrainPos.y = -3)
        float h = data.ray.y - terrainPos.y;
        float hFac = clamp(h * 0.01, 0.0, 1.0);
        color = mix(groundcolor, cliffcolor, hFac);
        //opencode: sand at low height (beach) - near terrain base, gentle slopes
        //opencode: smoothed slope for the color mix — the epsilon-scale
        //   gradient is sub-pixel noisy and speckled the mix per pixel.
        float colorSlopeE = 0.5;
        vec3 tp = data.ray - terrainPos;
        float hx = rmSdTerrain(tp + vec3(colorSlopeE, 0.0, 0.0), data.distFromPos);
        float hz = rmSdTerrain(tp + vec3(0.0, 0.0, colorSlopeE), data.distFromPos);
        float slope = length(vec2(hx, hz)) / colorSlopeE;
        float sandFac = clamp((1.0 - slope) * max(1.0 - h * 0.02, 0.0), 0.0, 1.0);
        color = mix(sandcolor, color, sandFac);
        //opencode: cliff on steep slopes (smoothed slope magnitude)
        color = mix(color, cliffcolor, clamp(slope - 0.3, 0.0, 1.0));
        //ai: reserved terrain parameter w controls tiled tex2 surface detail (zero preserves old presets).
        float detail = textureLod(tex2, data.ray.xz * 0.25, 0.0).r;
        color.rgb *= mix(1.0, 0.55 + detail, clamp(terrainParameters.w,0.0,1.0));
        //grid
        if (enableGrid == 1) {
            float coleur = 0;
            if (int(floor(data.ray.x))%2 == 0) coleur += 1;
            if (int(floor(data.ray.z))%2 == 0) coleur += 1;
            if (coleur == 2) coleur = 0;
            color.rgb += vec3(0.1*coleur);
        }
    } 
    //opencode: water coloring
#if CF_WATER
    if (t == -2) {   //water
        color = waterColor;
        //ai: shallow-water tint and shore foam use the conservative terrain distance.
        if (enableTerrain == 1) {
            vec3 terrainPoint = data.ray - terrainPos;
            float depth = max(rmSdTerrain(terrainPoint, data.distFromPos), 0.0);
            color.rgb *= mix(0.25, 1.0, exp(-depth * 0.18));
            float foam = (1.0-smoothstep(0.02,0.35,depth)) * 0.65;
            color.rgb = mix(color.rgb,vec3(0.8,0.9,0.86),foam);
        }
        if (enableGrid == 1) {
            float coleur = 0;
            if (int(floor(data.ray.x))%2 == 0) coleur += 1;
            if (int(floor(data.ray.z))%2 == 0) coleur += 1;
            if (coleur == 2) coleur = 0;
            color.rgb += vec3(0.1*coleur);
        }
    }
#endif //CF_WATER
    //opencode: objects with optional texturing
    if (t >= 0 && t < 8) {
        color = objectColor[t];
        color.rgb = applyObjectTexture(t, color.rgb, data.ray, data.normal);
    }
    if (t >= 8 && t < 16) color = lightColor[t-8]; //lights
    data.colorAccumulated += color;
}

void illu(inout rmData data) { //illumination: lighting & shadows
    int objIdx = data.currentObjectIndex;
    
    //opencode: map() calls below overwrite currentObjectIndex/Dist - save for restore
    int saveIndex = data.currentObjectIndex;
    float saveDist = data.currentObjectDist;
    //ai: hit tolerance can leave a sample slightly inside the surface. Offset all
    //   lighting queries along the normal, not toward a grazing light direction.
    float bias = 4.0 * max(rmEpsilon, min(0.01,
        data.distFromPos * tan(pi * camFov / 360.0) / max(res.y, 1.0) * 0.1));
    vec3 lightingOrigin = data.ray + bias * data.normal;
    
    vec4 lig = vec4(0.0);
    if (objIdx == -1)         {lig = vec4(0.35,1.0,0.15,32.0);} //opencode: terrain ambient=0.35, spec=0.15, shininess=32
#if CF_WATER
    if (objIdx == -2)         {lig = vec4(0.3,0.9,1.0,64.0);}
#endif
    if (objIdx >= 0 && objIdx < 8) {lig = objectLighting[objIdx];}
    if (objIdx >= 8 && objIdx < 16){lig = vec4(1.0,0.0,0.0,1.0);}
    //blinn-phong - sun light
    vec3 sunDir = normalize(sunPos);
    vec3 reflectDir = reflect(sunDir, data.normal); 
    vec4 ambient  = lig.x * lightAmbient;
    //opencode: hemisphere fill - ground bounce adds color to underside
    ambient += lig.x * 0.25 * (0.5 + 0.5 * data.normal.y) * vec4(0.4, 0.6, 0.8, 1.0);
    vec4 diffuse  = lig.y * sunColor * max(dot(data.normal, sunDir), 0.0);
    vec4 specular = lig.z * sunColor * pow(max(dot(normalize(-data.dir), reflectDir), 0.0), lig.w);
    diffuse *= sunBright;
    specular *= sunBright;
    //shadows - sun shadow with step limit (8 steps)
    float shadow = 1.0; float k = 8;
    if (enableShadows == 1 && objIdx < 8) {
        int shSteps = 0;
        for (float st = 0.01; st < 64.0 && shSteps < 8; shSteps++) {
            float m = map(lightingOrigin + st * sunDir, data);
            if (m < rmEpsilon) { shadow = 0.0; break; }
            shadow = min(shadow, k * m / st);
            st += m;
        }
    }
    //ai: illumination and occlusion affect radiance, never surface coverage.
    data.colorAccumulated.rgb *= (ambient + shadow*(diffuse+specular)).rgb;
    //opencode: ambient occlusion (2 steps - was 3, reduced for perf)
    if (enableAO == 1 && objIdx < 8) {
        float ao = 0.0;
        float aoStep = 0.3;
        for (int j = 0; j < 2; j++) {
            float d = aoStep * float(j + 1);
            float m = map(lightingOrigin + d * data.normal, data);
            ao += max(d - m, 0.0) / (d * 2.0);
        }
        data.colorAccumulated.rgb *= clamp(1.0 - ao * aoStep, 0.3, 1.0);
    }
    //opencode: point lights with per-light shadow
    for (int i = 0; i < lightAmount; i++) {
        float lightDist = length(lightPos[i] - data.ray);
        vec3 lightDir = normalize(lightPos[i] - data.ray);
        vec3 lightReflectDir = reflect(lightDir, data.normal); 
        vec3 factors = lightAttenuation;
        float attDen = factors.x + factors.y*lightDist + factors.z*lightDist*lightDist;
        float att = 1.0/max(attDen, 0.001);
        float lightDiff = lig.y * max(dot(data.normal,lightDir),0.0)*att;
        float lightSpec = lig.z * pow(max(dot(normalize(data.dir), lightReflectDir), 0.0), lig.w)*att;
        //opencode: separate point light shadow flag
        float lightShadow = 1.0;
        if (enableLightShadows == 1) {
            float lk = 8.0;
            int lsSteps = 0;
            for (float t = 0.01; t < lightDist && lsSteps < 8; lsSteps++) {
                vec3 lpos = lightingOrigin + t * lightDir;
                float m = map(lpos, data);
                if (m < rmEpsilon) { lightShadow = 0.0; break; }
                lightShadow = min(lightShadow, lk * m / t);
                t += abs(m) + 0.001;
            }
        }
        data.colorAccumulated.rgb += clamp(lightBright[i]*lightShadow
            *(lightDiff+lightSpec)*lightColor[i].rgb, vec3(0.0), vec3(2.0));
    }
    //opencode: restore hit object info (shadow/AO map() calls overwrote it)
    data.currentObjectIndex = saveIndex;
    data.currentObjectDist = saveDist;
}

//opencode: glow - light glow (indices 8-15) + object emissive glow (indices 0-7)
vec4 glow(inout rmData data) {
    vec4 result = vec4(0.0);
    float d = abs(data.minObjectDist); //opencode: use absolute closest approach distance
    //opencode: light glow (indices 8-15) - gated by enableLightGlow
    if (enableLightGlow == 1 && data.minObjectIndex >= 8 && data.minObjectIndex < 16) {
        int li = data.minObjectIndex - 8;
        float glowFactor = pow(d * 0.3 + 1.0, 4.0);
        result.rgb += lightColor[li].rgb / glowFactor;
        result.a += 1.0 / glowFactor;
    }
    //opencode: object glow (indices 0-7, emissive from material.w)
    if (data.minObjectIndex >= 0 && data.minObjectIndex < 8) {
        float emissive = objectMaterial[data.minObjectIndex].w;
        if (emissive > 0.0) {
            float glowFactor = pow(d * 0.5 + 1.0, 3.0);
            result.rgb += objectColor[data.minObjectIndex].rgb * emissive / glowFactor;
            result.a += emissive / glowFactor;
        }
    }
    return result;
}

vec4 material(inout rmData data) {
    //ai: offset along the surface normal, not a grazing outgoing direction.
    float bias = 4.0 * max(rmEpsilon, min(0.01,
        data.distFromPos * tan(pi * camFov / 360.0) / max(res.y, 1.0) * 0.1));
    vec4 mater = vec4(0.0);
    switch (data.currentObjectIndex) {
        case -1: mater = vec4(0.000,0.000,0.000,0.000); break; //terrain
#if CF_WATER
        //ai: dielectric Fresnel approaches full reflection at grazing angles.
        case -2: mater = vec4(0.0204+0.9796*pow(1.0-clamp(-dot(data.dir,data.normal),0.0,1.0),5.0),0.0,1.333,0.0); break;
#endif
        //ai: light markers and map misses are not object array entries.
        default:
            if (data.currentObjectIndex >= 0 && data.currentObjectIndex < 8)
                mater = objectMaterial[data.currentObjectIndex];
            break;
    }
    //ai: water is a reflective surface too, despite its negative map index.
    if (mater.x > 0.0 && (data.currentObjectIndex == -2 ||
        (data.currentObjectIndex >= 0 && data.currentObjectIndex < 8))) {
        if (mater.y == 0.0) { //reflect
            data.dir = reflect(data.dir,data.normal);
            data.pos = data.ray + bias * data.normal;
        }
        //use fresnel to mix?
        if (mater.y == 1.0) { //transparency
            vec3 tempNormal = data.normal;
            //ai: the normal already faces the incident ray; eta is incident/transmitted.
            float ior = max(mater.z, 0.0001);
            data.refractionIndex = data.backFace != 0 ? ior : 1.0 / ior;
            vec3 tempDir = vec3(1.0);
            tempDir = refract(data.dir,tempNormal,data.refractionIndex);
            if (length(tempDir) == 0) tempDir = reflect(data.dir,tempNormal);
            data.dir = tempDir;
            data.pos = data.ray + bias * data.normal
                * (dot(data.dir, data.normal) < 0.0 ? -1.0 : 1.0);
        }
    }
    else {
        data.doMarch = 0;
    }
    //opencode: colorFactor for multi-bounce. Guard against out-of-bounds (terrain index == -1).
    if (data.currentObjectIndex >= 0 && data.currentObjectIndex < 8) {
        data.colorFactor *= mater.x * mix(vec4(1.0) - objectColor[data.currentObjectIndex], vec4(1.0), mater.x);
    } else {
        data.colorFactor *= mater.x;
    }
    return mater;
}

vec4 march(inout rmData data) {
    //ai: each bounce owns its start sign, bracket, and counters.
    float depth = data.bounceCount == 0 ? max(rmNear, 0.0) : 0.0;
    int stepsThisBounce = 0; //opencode: relative step counter for this bounce
    
    data.currentObjectIndex = 0;
    data.pathSteps = 0;
    data.pathLength = 0.0;
    data.distFromPos = 0.0;
    data.backFace = 0;
    //ai: shade only this bounce, then composite it with the previous surface.
    data.colorAccumulated = vec4(0.0);
    data.minObjectDist = rmFar;
    data.minObjectIndex = -3;
    
    //opencode: use stepsPerBounce when non-zero
    int maxSteps = rmSteps;
    if (rmStepsPerBounce > 0) maxSteps = data.pathStepsTotal + rmStepsPerBounce;
    if (maxSteps > rmSteps) maxSteps = rmSteps;
    
    //opencode: use distTotal when non-zero
    float maxDist = rmFar;
    if (rmDistTotal > 0.0)
        maxDist = min(maxDist, rmDistTotal - data.pathLengthTotal);
    
    //opencode: previous ray position for binary search refinement
    vec3 prevRay = data.pos;
    float prevDist = 0.0;
    bool hit = false;
    
    for (int i = data.pathStepsTotal; i < maxSteps; i++) {
        
        //opencode: track previous step's sign (updated per step, not per bounce)
        data.marchSignPrev = sign(prevDist);
        
        //ai: enforce the distance budget before evaluating the next sample.
        if (depth > maxDist) {
            break;
        }
        
        //opencode: always march forward regardless of inside/outside
        data.ray = data.pos + depth*data.dir;
        data.distFromPos = depth;
        stepsThisBounce++;
        
        //get map
        data.currentObjectDist = map(data.ray,data);
        if (isnan(data.currentObjectDist) || isinf(data.currentObjectDist)) break;
        //opencode: track minimum absolute distance AND which object (for glow calculation)
        float absDist = abs(data.currentObjectDist);
        if (absDist < data.minObjectDist) {
            data.minObjectDist = absDist;
            data.minObjectIndex = data.currentObjectIndex;
        }
        data.marchSign = sign(data.currentObjectDist);
        if (stepsThisBounce == 1) {
            data.firstSign = data.marchSign;
            data.backFace = data.firstSign < 0.0 ? 1 : 0;
        }
        
        //opencode: always use absolute distance for surface detection
        float absD = abs(data.currentObjectDist);
        //ai: a small subpixel tolerance keeps distant grazing rays convergent.
        float visualEpsilon = max(rmEpsilon, min(0.01,
            depth * tan(pi * camFov / 360.0) / max(res.y, 1.0) * 0.1));
        //ai: exported primary depth must converge more tightly than a visual silhouette.
        float hitEpsilon = hybridDepthEnabled >= 2 && data.bounceCount == 0
            ? min(visualEpsilon,0.00001) : visualEpsilon;
        bool objectHit = absD < hitEpsilon;
        bool signChanged = enableSignSample == 1 && stepsThisBounce > 1
            && data.marchSign * sign(prevDist) < 0.0;
        
        //ai: exhausted budgets are misses, never unproven surface hits.
        if (objectHit || signChanged) {
            
            //ai: refine a genuine sign bracket instead of shading an overshoot.
            if (signChanged && !objectHit) {
                vec3 lo = prevRay, hi = data.ray;
                for (int bi = 0; bi < 16; bi++) {
                    vec3 mid = (lo + hi) * 0.5;
                    float m = map(mid, data);
                    float s = sign(m);
                    float refineEpsilon = hybridDepthEnabled >= 2 && data.bounceCount == 0
                        ? hitEpsilon : rmEpsilon;
                    if (abs(m) < refineEpsilon) { hi = mid; lo = mid; break; }
                    if (s * sign(prevDist) < 0.0) hi = mid;
                    else lo = mid;
                }
                data.ray = (lo + hi) * 0.5;
                data.currentObjectDist = map(data.ray, data);
                data.marchSign = sign(data.currentObjectDist);
                depth = length(data.ray - data.pos);
                data.distFromPos = depth;
            }
            
            //opencode: save object index/dist before gradient (map() overwrites them)
            int hitIndex = data.currentObjectIndex;
            float hitDist = data.currentObjectDist;
            
            data.gradient = calc_gradient(data.ray,data);
            data.normal = dot(data.gradient, data.gradient) > 1e-16
                ? normalize(data.gradient) : -data.dir;
            //ai: orient the surface normal against the incident ray.
            data.backFace = dot(data.normal, data.dir) > 0.0 ? 1 : 0;
            if (data.backFace != 0) data.normal = -data.normal;
            
            //opencode: restore the original hit index/dist (gradient calls map())
            data.currentObjectIndex = hitIndex;
            data.currentObjectDist = hitDist;
            
            if (enableFrag == 1) frag(data);
            if (enableIllu == 1) illu(data);
            //opencode: enabled glow
            //ai: emissive radiance can exceed one; coverage must never do so.
            if (enableGlow == 1) data.colorAccumulated.rgb += glow(data).rgb;
            data.colorAccumulated = mix(data.color,data.colorAccumulated,data.colorFactor);
            material(data);
            
            hit = true;
            data.pathLength = depth;
            data.pathSteps = stepsThisBounce;
            data.bounceCount++;
            break;
        }
        
        //opencode: save for binary search before updating depth
        prevRay = data.ray;
        prevDist = data.currentObjectDist;
        
        //opencode: Pad? [3/3] step correction (LOD - larger steps with distance)
        //   poly[0..3] = numerator coefficients (const, linear, quad, cubic)
        //   poly[4..6] = denominator coefficients (linear, quad, cubic)
        //   corr = num / (1.0 + den) - denominator asymptotes naturally, no zero-crossing
        //   poly[7] unused. Old Taylor presets (poly[4..6]=0) -> denom=1.0, identical behavior.
        float stepDist = abs(data.currentObjectDist);
        if ((enableTerrain == 1 && data.currentObjectIndex == -1) ||
            (enableWater  == 1 && data.currentObjectIndex == -2)) {
            float idx = float(stepsThisBounce);
            float num = rmPolynomial[0] + rmPolynomial[1]*idx + rmPolynomial[2]*idx*idx + rmPolynomial[3]*idx*idx*idx;
            float den = 1.0 + rmPolynomial[4]*idx + rmPolynomial[5]*idx*idx + rmPolynomial[6]*idx*idx*idx;
            //opencode: poly[7] = minimum corr floor (0.0 = use 0.1 default)
            float minCorr = rmPolynomial[7] > 0.0 ? rmPolynomial[7] : 0.1;
            float corr = abs(den) > 0.000001 ? num / den : 1.0;
            //ai: author correction may slow a march, never exceed a safe SDF step.
            corr = max(corr, max(minCorr, 1.0));
            stepDist = abs(data.currentObjectDist) / corr;
        }
        //opencode: clamp step distance when rmStepMax > 0
        if (rmStepMax > 0.0 && stepDist > rmStepMax) stepDist = rmStepMax;
        depth += stepDist;
        
    }
    //ai: secondary misses retain the first surface's opacity and sample its environment.
    if (!hit) {
        vec4 environment = nullColor;
        if (enableTexsky == 1) environment = vec4(texture(texSky, data.dir).rgb, 1.0);
        else if (enableProceduralSky == 1)
            environment = vec4(proceduralSky(data.dir), 1.0);
        //ai: reflected/refracted sky includes the same cloud deck, from the secondary origin.
#if CF_CLOUDS
        if (data.bounceCount > 0 && enableClouds == 1)
            environment = rmCloudMarch(data.pos, data.dir, environment, rmFar);
#endif
        if (data.bounceCount > 0) {
            data.colorAccumulated = mix(data.color, environment, data.colorFactor);
            data.colorAccumulated.a = 1.0;
        } else {
            data.colorAccumulated = environment;
            data.colorAccumulated.a = enableTexsky == 1 || enableProceduralSky == 1
                ? 1.0 : 0.0;
        }
        data.doMarch = 0;
        data.pathLength = 0.0;
    }
    data.pathSteps = stepsThisBounce;
    data.pathStepsTotal += data.pathSteps;
    data.pathLengthTotal += data.pathLength;
    return data.colorAccumulated;
}

void main() {
    //ai: define depth on every non-discard path, including standalone and cubemap draws.
    gl_FragDepth = gl_FragCoord.z;
    //ai: gl_FragCoord already names pixel centers; do not shift the camera half a pixel.
    vec2 screenCoord = gl_FragCoord.xy;
    //ai: ray construction unprojects raster's positive clip-space Halton offset.
    vec2 rayCoord = screenCoord - hybridJitter * res.xy * 0.5;
    rmData data;
    init(data);
    data.pos = camPos;
    data.dir = setCamera( camPos, camTarget, camUp)*rayDir(pi*camFov/180.0,res.xy,rayCoord);
    //ai: atmosphere follows the primary camera segment, not a reflected direction.
    vec3 primaryDir = data.dir;
    data.color = firstColor;
    
    //opencode: use distTotal when non-zero
    float maxDist = rmFar;
    if (rmDistTotal > 0.0) maxDist = rmDistTotal;
    float primaryLength = maxDist;
    bool primaryHit = false;
    vec3 primaryNormal = vec3(0);
    float primaryRoughness = 1.0;
    for (int i = 0; i < rmBounces; i++) {
        data.color = march(data);
        if (i == 0 && data.bounceCount > 0) {
            primaryLength = data.pathLength;
            primaryHit = true;
            primaryNormal = data.normal;
            if (data.currentObjectIndex >= 0 && data.currentObjectIndex < objectAmount)
                primaryRoughness = clamp(objectRoughness[data.currentObjectIndex],0.0,1.0);
        }
        if (hybridDepthEnabled >= 3) break;
        if (data.doMarch == 0 ||
            data.pathStepsTotal >= rmSteps ||
            data.pathLengthTotal > maxDist ||
            //ai: normal-incidence water has ~2% Fresnel; do not discard that valid secondary ray.
            length(data.colorFactor) < 0.01) {
            break;
        }
    }
    //ai: raw GL depth shares gl_FragCoord's bottom-up convention; misses preserve raster sky.
#if CF_HYBRID_MASK
    if (hybridDepthEnabled > 0) {
        if (!primaryHit) discard;
        float viewDepth = primaryLength * dot(primaryDir,normalize(camTarget-camPos));
        float n = hybridClip.x, f = hybridClip.y;
        if (viewDepth < n || viewDepth > f) discard;
        //ai: sample only the separate shared scene depth, never the velocity framebuffer attachment.
        if (hybridDepthEnabled == 4) {
            float projectedDepth = f/(f-n) - (f*n)/((f-n)*viewDepth);
            if (projectedDepth > texture(hybridRasterDepth,screenCoord/res.xy).r + 2.0e-6) discard;
            finalColor = vec4(0.0,0.0,hybridTemporalAnimated == 1 ? -2.0 : -1.0,1.0);
            return;
        }
        if (hybridDepthEnabled == 1) {
            float raster = texture(hybridRasterDepth,screenCoord/res.xy).r;
            float rasterViewDepth = n*f / (f-raster*(f-n));
            if (viewDepth >= rasterViewDepth) discard;
        } else {
            gl_FragDepth = f/(f-n) - (f*n)/((f-n)*viewDepth);
        }
    }
#endif
    //ai: the normal-only pass does not composite fog/cloud/color after its primary hit.
    if (hybridDepthEnabled == 3) {
        finalColor = vec4(normalize(primaryNormal)*0.5+0.5,1.0);
        return;
    }
    if (debugShowPos == 1)          data.color.rgb = data.pos;
    if (debugShowDir == 1)          data.color.rgb = data.dir;
    if (debugShowRay == 1)          data.color.rgb = data.ray;
    if (debugShowGradient == 1)     data.color.rgb = data.gradient;
    if (debugShowNormal == 1)       data.color.rgb = data.normal;
    if (debugShowBounceCount == 1)  data.color.rgb = vec3(float(data.bounceCount)/float(rmBounces));
    if (debugShowPathSteps == 1)    data.color.rgb = vec3(float(data.pathStepsTotal)/float(rmSteps));
    if (debugShowPathLength == 1)   data.color.rgb = vec3(sqrt(float(data.pathLengthTotal)/float(rmFar)));
    if (debugShowDist == 1)         data.color.rgb = vec3(sqrt(float(data.currentObjectDist)/float(rmFar)));
    
    //fog
    //ai: fog must not turn a miss opaque and suppress the procedural sky.
    if (enableFog == 1 && data.bounceCount > 0) {
        float fogFactor = clamp((primaryLength-fogNear)
            / max(fogFar-fogNear, 0.0001),0.0,1.0);
        data.color = mix(data.color,fogColor,fogFactor);
    }
    
    //opencode: procedural sky - fill transparent pixels with sky color
    if (enableProceduralSky == 1 && data.color.a < 0.01) {
        vec3 skyCol = proceduralSky(normalize(data.dir));
        data.color.rgb = skyCol;
        data.color.a = 1.0;
    }
    
#if CF_CLOUDS
    //ai: volumetric clouds composite last so they sit over terrain, water and
    //   the procedural sky (opt-in; enableClouds = 0 keeps the frame identical).
    if (enableClouds == 1) {
        data.color = rmCloudMarch(camPos, primaryDir, data.color, primaryLength);
    }
#endif
    
#if CF_VOLFOG
    //ai: volumetric height fog with sun shafts, over the shaded scene. The
    //   march distance is the accumulated path length (surface hit) or the
    //   far plane for sky rays.
    if (enableVolFog == 1) {
        data.color = rmVolFogMarch(camPos, primaryDir, data.color, primaryLength);
    }
#endif
    
    //ai: floating-point caches preserve alpha > 1, which makes the next alpha
    //   composite subtract the previous frame (alternating white/black light cores).
    finalColor = vec4(data.color.rgb, clamp(data.color.a, 0.0, 1.0));
    //ai: shared HDR alpha carries authored roughness; the normal target uses coverage instead.
    if (hybridDepthEnabled == 2) finalColor.a = primaryRoughness;
}
