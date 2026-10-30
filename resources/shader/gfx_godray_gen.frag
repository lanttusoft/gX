//ai: god rays consume current-frame base color, never SSR's potentially stale mips.
//opencode: god ray generation - unrolled 4-step ray-march
//   Samples scene luminance at 4 points toward the sun.
//   No GPU loops - safe on all drivers.
#version 330

uniform sampler2D sceneColor;
uniform vec2  sunScreenPos;
uniform int rayDensity = 64;
uniform float rayDecay = 0.9;
uniform float rayWeight = 0.42;
uniform float rayExposure = 1.0;

in vec2 fragTexCoord;
out vec4 fragColor;

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

    vec2 u1 = clamp(uv + dir * stepUV, 0.0, 1.0);
    float l1 = max(dot(textureLod(sceneColor, u1, 0.0).rgb, vec3(0.2126, 0.7152, 0.0722)) - threshold, 0.0);
    total += l1 * decay; decay *= rayDecay;

    vec2 u2 = clamp(uv + dir * stepUV * 2.0, 0.0, 1.0);
    float l2 = max(dot(textureLod(sceneColor, u2, 0.0).rgb, vec3(0.2126, 0.7152, 0.0722)) - threshold, 0.0);
    total += l2 * decay; decay *= rayDecay;

    vec2 u3 = clamp(uv + dir * stepUV * 3.0, 0.0, 1.0);
    float l3 = max(dot(textureLod(sceneColor, u3, 0.0).rgb, vec3(0.2126, 0.7152, 0.0722)) - threshold, 0.0);
    total += l3 * decay; decay *= rayDecay;

    vec2 u4 = clamp(uv + dir * stepUV * 4.0, 0.0, 1.0);
    float l4 = max(dot(textureLod(sceneColor, u4, 0.0).rgb, vec3(0.2126, 0.7152, 0.0722)) - threshold, 0.0);
    total += l4 * decay;

    float falloff = exp(-dist * 2.0);
        //opencode: v0.3.3-audit - hard cap instead of re-tuning: the historical
    //   whiteout was the batch-pipeline white quad, not these constants.
    //   min() bounds any pathological HDR input while keeping the original
    //   response shape under the cap.
    float intensity = min(total * rayWeight * rayExposure * falloff, 1.2);

    fragColor = vec4(intensity);
}
