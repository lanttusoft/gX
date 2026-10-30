#version 330
//opencode: v0.3-M4 - SSGI: screen-space global illumination via horizon-based
//   gather (SSRT3-inspired; shares GTAO's slice/step machinery). For each
//   pixel, march DIRECTIONS slices x STEPS steps through the HDR scene color:
//   each sample that passes the thickness heuristic contributes its lit
//   radiance weighted by the cosine of the sampling angle - one bounce of
//   indirect diffuse with colored light bleed. Output RGB = gathered
//   irradiance (denoised by IGN jitter + downstream blur).

in vec2 fragTexCoord;

uniform sampler2D sceneColor;   // lit HDR radiance (post-lighting)
uniform sampler2D depthMap;
uniform mat4 invProjection;
uniform mat4 projection;
uniform vec2 resolution;
uniform float radius;           // world-unit gather radius
//ai: use the unchanged authored SSAO bias to reject tangent-plane reconstruction noise.
uniform float giBias;
uniform float thickness;        // ignore samples farther than this BEHIND surface
uniform float intensity;        // output gain
uniform sampler2D prevGI;       //opencode: Phase3 - last frame's accumulated GI
uniform float bounceGain;       // 0 = single bounce
uniform float oracleSeed = 0.0; //opencode: oracle (Paper 1) - frame counter;
    //   with oracleSeedActive it rotates the gather taps per frame (rays
    //   distributed over TIME, converged by the accumulators). Default off =
    //   the original static-tap output exactly.
uniform int oracleSeedActive = 0;

out vec4 finalColor;

#define PI 3.14159265358979

//ai: integer fetch and reconstruction share an explicit index even on half-resolution pixel boundaries.
ivec2 getDepthTexel(vec2 uv) {
    ivec2 size = textureSize(depthMap, 0);
    return clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1);
}

float getDepth(vec2 uv) { return texelFetch(depthMap, getDepthTexel(uv), 0).r; }

vec3 getViewPosition(vec2 screenPosition, float depth) {
    //ai: nearest-filtered depth is defined at the selected depth texel center.
    vec2 depthSize = vec2(textureSize(depthMap, 0));
    screenPosition = (vec2(getDepthTexel(screenPosition)) + 0.5) / depthSize;
    vec4 clip = vec4(vec3(screenPosition, depth) * 2.0 - 1.0, 1.0);
    vec4 v = invProjection * clip;
    return v.xyz / v.w;
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

float interleaveGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    finalColor = vec4(0.0); //opencode: sentinel

    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float depth = getDepth(uv);
    if (depth >= 0.999) return;

    vec3 viewPos = getViewPosition(uv, depth);
    vec3 viewNormal = computeNormalFromDepth(uv);
    vec3 viewDir = normalize(-viewPos);
    vec2 texelSize = 1.0 / resolution;

    const int DIRECTIONS = 4;
    const int STEPS = 6;

    float seedRot = (oracleSeedActive == 1) ? fract(oracleSeed * 0.61803398875) : 0.0;
    float noiseAngle = (interleaveGradientNoise(gl_FragCoord.xy) + seedRot) * PI;
    vec3 tangentJit = normalize(vec3(cos(noiseAngle), sin(noiseAngle), 0.0));
    float stepFraction = interleaveGradientNoise(gl_FragCoord.xy + vec2(47.233, 89.117)
        + seedRot * vec2(131.7, 57.3));

    vec3 gathered = vec3(0.0);
    float weightSum = 0.0;

    for (int i = 0; i < DIRECTIONS; ++i) {
        float angle = float(i) / float(DIRECTIONS) * PI;
        vec3 dir3 = normalize(mat3(tangentJit,
                                   vec3(-tangentJit.y, tangentJit.x, 0.0),
                                   vec3(0.0, 0.0, 1.0))
                              * vec3(cos(angle), sin(angle), 0.0));

        for (int side = 0; side < 2; ++side) {
            for (int j = 0; j < STEPS; ++j) {
                //ai: stratified start jitter must not collapse the whole gather radius.
                vec3 offset = dir3 * radius
                    * ((float(j) + stepFraction) / float(STEPS))
                    * ((side == 0) ? 1.0 : -1.0);
                vec3 sp = viewPos + offset;
                //ai: hemisphere eligibility is tested on the reconstructed delta below, not an orthogonal tangent.

                vec4 clipP = projection * vec4(sp, 1.0);
                vec2 suv = clipP.xy / clipP.w * 0.5 + 0.5;
                if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
                //opencode: no y-flip (clip y-up == depth-texture y-up; the
                //   flipped taps read mirrored rows — same bug as GTAO's
                //   horizon band, fixed together).
                float sd = getDepth(suv);
                if (sd >= 0.999) continue;
                vec3 svp = getViewPosition(suv, sd);
                vec3 viewDelta = svp - viewPos;

                //opencode: v0.3.4-retune - plain geometric bounds instead of
                //   the mixed-axis expression that rejected ~every sample
                //   (GI read as the ambient floor only). Distance cap + the
                //   existing behind-surface test keep occlusion behavior.
                if (length(viewDelta) > radius * 1.25) continue;
                //ai: accept actual above-surface samples beyond the authored separation bias.
                if (dot(viewDelta, viewDelta) == 0.0) continue;
                if (dot(viewDelta, viewNormal) <= max(giBias, 0.0)) continue;

                vec3 radiance = textureLod(sceneColor, suv, 0.0).rgb;
                //opencode: Phase3 - inject previous frame's GI as extra
                //   radiance (SSRT3-style cheap multibounce)
                radiance += textureLod(prevGI, suv, 0.0).rgb * bounceGain;
                //opencode: cosine-weight by the angle between the sample's
                //   incoming direction and our normal (Lambertian gather)
                float cosW = max(dot(normalize(viewDelta), viewNormal), 0.0);
                float falloff = 1.0 - length(viewDelta) / radius;
                gathered += radiance * cosW * max(falloff, 0.05);
                weightSum += 1.0;
            }
        }
    }

    //opencode: Phase3 - off-screen/low-confidence fallback toward a soft
    //   ambient floor so screen edges don't go flat black
    //ai: both sides contribute taps; normalize confidence by the actual 48-candidate budget.
    float conf = clamp(weightSum / float(DIRECTIONS * STEPS * 2), 0.0, 1.0);
    //opencode: v0.3.4-retune - floor decoupled from user intensity: a full
    //   0.06..0.09 * intensity everywhere read as a uniform wash over the
    //   whole frame (the localized bleed pools were getting lost)
    vec3 ambientFloor = vec3(0.021, 0.0245, 0.0315);

    //opencode: Phase3 - cheap multi-bounce: sampled radiance already contains
    //   last frame's GI when prevGI was injected into the read below
    if (weightSum > 0.5) gathered /= weightSum;
    //ai: normalize the gather estimate before mixing fallback; never divide ambient radiance by a sample count.
    gathered = mix(ambientFloor, gathered, conf);

    //opencode: reinjection happens at sample time via the prevGI-augmented
    //   radiance read (see gather loop); apply bounce gain to the result's
    //   above-direct portion is not separable here - scale whole GI instead
    gathered *= intensity * mix(1.0, 1.15, clamp(bounceGain, 0.0, 1.0));

    finalColor = vec4(max(gathered, vec3(0.0)), 1.0);
}
