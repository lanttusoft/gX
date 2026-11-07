//ai: aperture diffraction spikes — threshold the HDR source, gather along
//   aperture-blade directions, add RGB dispersion, and return only the spike
//   contribution (the engine adds this to hdrScene before bloom/tonemap).
#version 330

uniform sampler2D sceneColor;
uniform vec2  resolution;
uniform float threshold = 1.0;
uniform float knee = 0.1;
uniform float intensity = 1.0;
uniform float lengthUV = 0.06;
uniform float chromatic = 0.25;
uniform float rotation = 0.0;
uniform float sparkle = 0.0;
uniform int   blades = 6;
uniform int   taps = 12;
uniform float time = 0.0;

in vec2 fragTexCoord;
out vec4 fragColor;

float luma(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float highlight(float value) {
    return smoothstep(threshold - knee, threshold + knee, value)
        * max(value - threshold, 0.0);
}

float hash(vec2 value) {
    return fract(sin(dot(value, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    //opencode: screen Y-down vs texture Y-up, same convention as the other
    //   post shaders.
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec2 metric = vec2(resolution.y / max(resolution.x, 1.0), 1.0);
    float stepUV = max(lengthUV, 0.0) / float(max(taps, 1));
    vec3 sum = vec3(0.0);
    float twinkle = mix(1.0,
        0.5 + 0.5 * hash(floor(uv * resolution) + floor(time * 8.0)),
        clamp(sparkle, 0.0, 1.0));
    for (int blade = 0; blade < 9; blade++) {
        if (blade >= blades) break;
        float angle = rotation + 6.2831853 * float(blade)
            / float(max(blades, 1));
        vec2 dir = vec2(cos(angle), sin(angle)) * metric;
        for (int tap = 1; tap <= 16; tap++) {
            if (tap > taps) break;
            float distance = float(tap) / float(max(taps, 1));
            vec2 offset = dir * stepUV * float(tap);
            vec3 cR = texture(sceneColor,
                uv + offset * (1.0 + clamp(chromatic, 0.0, 1.0) * 0.35)).rgb;
            vec3 cG = texture(sceneColor, uv + offset).rgb;
            vec3 cB = texture(sceneColor,
                uv + offset * (1.0 - clamp(chromatic, 0.0, 1.0) * 0.35)).rgb;
            float weight = (1.0 - distance) * (1.0 - distance);
            sum.r += cR.r * highlight(luma(cR)) * weight;
            sum.g += cG.g * highlight(luma(cG)) * weight;
            sum.b += cB.b * highlight(luma(cB)) * weight;
        }
    }
    sum *= intensity * twinkle
        / float(max(blades * taps, 1));
    fragColor = vec4(clamp(sum, 0.0, 10.0), 1.0);
}
