#version 330
//opencode: v0.3-M2 - Ground-Truth Ambient Occlusion (Jimenez et al. 2016),
//   ported from the three.js GTAOPass reference implementation (MIT; see
//   license/three/LICENSE). Horizon-scan
//   in view space: DIRECTIONS slices x STEPS steps, cos-horizon tracking,
//   normal-clamped analytic slice integral. Selected via ssao.useGTAO;
//   output R = visibility (1 = open) so the existing edge-aware blur and
//   model-shader AO sampling work unchanged. Conventions match
//   gfx_ssao.frag (uv Y-flip for depth reads, cross(dx,dy) normals).

in vec2 fragTexCoord;

uniform sampler2D depthMap;
uniform mat4 invProjection;     // inverse projection - view-space reconstruction
uniform mat4 projection;        // projection - re-project sample points to screen
uniform vec2 resolution;
uniform float radius;           // world-unit sample radius
//ai: honor the authored SSAO bias as view-space normal separation, not a depth threshold.
uniform float aoBias;
uniform float thickness;        // ignore samples farther than this BEHIND the surface
uniform float aoStrength;       // exponent on final visibility
uniform float oracleSeed = 0.0; //opencode: oracle (Paper 1) - frame counter;
    //   with oracleSeedActive it rotates the AO taps per frame (rays over
    //   time, converged downstream). Default off = static taps exactly.
uniform int oracleSeedActive = 0;

out vec4 finalColor;

#define PI 3.14159265358979

float getDepth(vec2 uv) {
    return textureLod(depthMap, uv, 0.0).r;
}

vec3 getViewPosition(vec2 screenPosition, float depth) {
    //ai: nearest-filtered depth belongs to the selected texel center, not the unsnapped UV.
    vec2 depthSize = vec2(textureSize(depthMap, 0));
    screenPosition = (clamp(floor(screenPosition * depthSize), vec2(0.0), depthSize - 1.0) + 0.5) / depthSize;
    //opencode: GL-style depth [0,1] -> NDC [-1,1] (not reversed)
    vec4 clipSpacePosition = vec4(vec3(screenPosition, depth) * 2.0 - 1.0, 1.0);
    vec4 viewSpacePosition = invProjection * clipSpacePosition;
    return viewSpacePosition.xyz / viewSpacePosition.w;
}

vec3 computeNormalFromDepth(vec2 uv) {
    vec2 px = 1.0 / resolution;
    float cD = getDepth(uv);
    float lD = getDepth(uv - vec2(px.x, 0.0));
    float rD = getDepth(uv + vec2(px.x, 0.0));
    float uD = getDepth(uv + vec2(0.0, px.y));
    float dD = getDepth(uv - vec2(0.0, px.y));
    vec3 vL = getViewPosition(uv - vec2(px.x, 0.0), lD);
    vec3 vR = getViewPosition(uv + vec2(px.x, 0.0), rD);
    vec3 vU = getViewPosition(uv + vec2(0.0, px.y), uD);
    vec3 vD = getViewPosition(uv - vec2(0.0, px.y), dD);
    return normalize(cross(vR - vL, vU - vD));
}

//opencode: Interleaved Gradient Noise (Jimenez 2014) - texture-free jitter
float interleaveGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    finalColor = vec4(1.0, 0.0, 0.0, 1.0); //opencode: sentinel

    //opencode: Y-flip - screen Y-down vs depth-texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float depth = getDepth(uv);
    if (depth >= 0.999) return;

    vec3 viewPos = getViewPosition(uv, depth);
    vec3 viewNormal = computeNormalFromDepth(uv);

    const int DIRECTIONS = 4;
    const int STEPS = 6;

    vec3 viewDir = normalize(-viewPos);
    vec2 texelSize = 1.0 / resolution;

    // IGN jitter replaces three's noise texture: slice rotation + start fraction
    float seedRot = (oracleSeedActive == 1) ? fract(oracleSeed * 0.61803398875) : 0.0;
    float noiseAngle = (interleaveGradientNoise(gl_FragCoord.xy) + seedRot) * PI;
    vec3 tangentJit = normalize(vec3(cos(noiseAngle), sin(noiseAngle), 0.0));
    float stepFraction = interleaveGradientNoise(gl_FragCoord.xy + vec2(47.233, 89.117)
        + seedRot * vec2(131.7, 57.3));

    float ao = 0.0;
    for (int i = 0; i < DIRECTIONS; ++i) {
        float angle = float(i) / float(DIRECTIONS) * PI;
        vec3 dir3 = normalize(mat3(tangentJit,
                                   vec3(-tangentJit.y, tangentJit.x, 0.0),
                                   vec3(0.0, 0.0, 1.0))
                              * vec4(cos(angle), sin(angle), 0.0, 0.5 + 0.5 * stepFraction).xyz);

        vec3 sliceBitangent = normalize(cross(dir3, viewDir));
        vec3 sliceTangent = cross(sliceBitangent, viewDir);
        vec3 normalInSlice = normalize(viewNormal - sliceBitangent * dot(viewNormal, sliceBitangent));

        vec3 tangentToNormalInSlice = cross(normalInSlice, sliceBitangent);
        vec2 cosHorizons = vec2(dot(viewDir, tangentToNormalInSlice),
                                dot(viewDir, -tangentToNormalInSlice));

        for (int j = 0; j < STEPS; ++j) {
            //ai: jitter the start of uniformly spaced steps, not the entire sample radius.
            vec3 sampleViewOffset = dir3 * radius
                * ((float(j) + stepFraction) / float(STEPS));

            for (int side = 0; side < 2; ++side) {
                vec3 sp = (side == 0) ? viewPos + sampleViewOffset
                                      : viewPos - sampleViewOffset;
                vec4 clipP = projection * vec4(sp, 1.0);
                vec2 suv = clipP.xy / clipP.w * 0.5 + 0.5;
                if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
                //opencode: NO y-flip here — clip is y-up and so is the depth
                //   texture (texel row 0 = framebuffer bottom). The old flip
                //   read mirrored rows: taps landed far away (rejected)
                //   except near the viewport middle, where mirrored taps read
                //   nearby-but-wrong depths and pinned every horizon -> the
                //   full-width dark band at Y~0.5 (ao 0, screen-locked,
                //   camera/jitter/MSAA/shadow-independent).
                float sd = getDepth(suv);
                if (sd >= 0.999) continue;
                vec3 svp = getViewPosition(suv, sd);
                vec3 viewDelta = svp - viewPos;
                //ai: repeated nearest-depth texels can return the center itself; normalization is undefined there.
                if (dot(viewDelta, viewDelta) == 0.0) continue;
                //ai: tangent-plane and below-surface samples are not occluders; retain authored separation bias.
                if (dot(viewNormal, viewDelta) <= max(aoBias, 0.0)) continue;
                if (abs(viewDelta.z) < thickness) {
                    float sampleCosHorizon = dot(viewDir, normalize(viewDelta));
                    float delta = max(0.0, sampleCosHorizon -
                        ((side == 0) ? cosHorizons.x : cosHorizons.y));
                    // distanceFallOff=1 -> 2/(j+2) progressive weighting (paper)
                    delta *= mix(1.0, 2.0 / float(j + 2), 1.0);
                    if (side == 0) cosHorizons.x += delta;
                    else           cosHorizons.y += delta;
                }
            }
        }

        vec2 sinHorizons = sqrt(max(vec2(0.0), 1.0 - cosHorizons * cosHorizons));
        float nx = dot(normalInSlice, sliceTangent);
        float ny = dot(normalInSlice, viewDir);
        float nxb = 0.5 * (acos(clamp(cosHorizons.y, -1.0, 1.0))
                 - acos(clamp(cosHorizons.x, -1.0, 1.0))
                 + sinHorizons.x * cosHorizons.x - sinHorizons.y * cosHorizons.y);
        float nyb = 0.5 * (2.0 - cosHorizons.x * cosHorizons.x - cosHorizons.y * cosHorizons.y);
        ao += nx * nxb + ny * nyb;
    }

    ao = clamp(ao / float(DIRECTIONS), 0.0, 1.0);
    ao = pow(ao, aoStrength);

    finalColor = vec4(ao, 0.0, 0.0, 1.0);
}
