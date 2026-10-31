//ai: god rays consume current-frame base color, never SSR's potentially stale mips.
//opencode: god ray generation - unrolled 4-step ray-march
//   Samples scene luminance at 4 points toward the sun.
//   No GPU loops - safe on all drivers.
#version 330

uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;
uniform int hasSceneDepth = 0;
uniform vec2  sunScreenPos;
uniform int rayDensity = 64;
uniform float rayDecay = 0.9;
uniform float rayWeight = 0.42;
uniform float rayExposure = 1.0;

in vec2 fragTexCoord;
out vec4 fragColor;

//opencode: 1.0 on sky, 0.0 on scene geometry. Without a bound depth texture
//   the old full-scene source behavior is preserved for standalone probes.
float gX_GodraySky(vec2 uv) {
    if (hasSceneDepth == 0) return 1.0;
    return step(0.9999, texture(sceneDepth, uv).r);
}

float gX_GodrayLuma(vec2 uv, float threshold) {
    return max(dot(textureLod(sceneColor, uv, 0.0).rgb,
        vec3(0.2126, 0.7152, 0.0722)) - threshold, 0.0);
}

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up (matches all other
    //   post shaders; sunScreenPos is view-space with y=1 at top)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec2 delta = sunScreenPos - uv;
    float dist = length(delta);
    if (dist < 0.001) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 dir = delta / dist;
    //ai: four taps cannot step beyond the sun; preserve the prior stride for density >=16.
    float stepUV = min(dist * 4.0 / max(float(rayDensity), 1.0), dist * 0.25);

    float total = 0.0;
    float decay = 1.0;
    float threshold = 0.12;
    //opencode: foreground geometry stops the ray and never emits: this keeps
    //   PBR specular highlights from being replayed as light shafts.
    float visibility = 1.0;

    vec2 u1 = clamp(uv + dir * stepUV, 0.0, 1.0);
    float sky1 = gX_GodraySky(u1);
    float l1 = gX_GodrayLuma(u1, threshold) * sky1;
    total += l1 * visibility * decay; visibility *= sky1; decay *= rayDecay;

    vec2 u2 = clamp(uv + dir * stepUV * 2.0, 0.0, 1.0);
    float sky2 = gX_GodraySky(u2);
    float l2 = gX_GodrayLuma(u2, threshold) * sky2;
    total += l2 * visibility * decay; visibility *= sky2; decay *= rayDecay;

    vec2 u3 = clamp(uv + dir * stepUV * 3.0, 0.0, 1.0);
    float sky3 = gX_GodraySky(u3);
    float l3 = gX_GodrayLuma(u3, threshold) * sky3;
    total += l3 * visibility * decay; visibility *= sky3; decay *= rayDecay;

    vec2 u4 = clamp(uv + dir * stepUV * 4.0, 0.0, 1.0);
    float sky4 = gX_GodraySky(u4);
    float l4 = gX_GodrayLuma(u4, threshold) * sky4;
    total += l4 * visibility * decay;

    float falloff = exp(-dist * 2.0);
        //opencode: v0.3.3-audit - hard cap instead of re-tuning: the historical
    //   whiteout was the batch-pipeline white quad, not these constants.
    //   min() bounds any pathological HDR input while keeping the original
    //   response shape under the cap.
    float intensity = min(total * rayWeight * rayExposure * falloff, 1.2)
        * gX_GodraySky(uv);

    fragColor = vec4(intensity);
}
