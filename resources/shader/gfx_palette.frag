#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;
uniform int paletteMode = 0;       //opencode: 0 off, 1 fixed 16, 2 RGB332 256
uniform int ditherMode = 0;       //opencode: 0 off, 1 IGN, 2 Bayer4, 3 seeded noise
uniform float ditherSeed = 0.0;
uniform float ditherStrength = 1.5;

out vec4 finalColor;

float bayer4x4(vec2 pixel) {
    int x = int(mod(pixel.x, 4.0));
    int y = int(mod(pixel.y, 4.0));
    int index = y * 4 + x;
    if (index == 0) return 0.0;
    if (index == 1) return 8.0;
    if (index == 2) return 2.0;
    if (index == 3) return 10.0;
    if (index == 4) return 12.0;
    if (index == 5) return 4.0;
    if (index == 6) return 14.0;
    if (index == 7) return 6.0;
    if (index == 8) return 3.0;
    if (index == 9) return 11.0;
    if (index == 10) return 1.0;
    if (index == 11) return 9.0;
    if (index == 12) return 15.0;
    if (index == 13) return 7.0;
    if (index == 14) return 13.0;
    return 5.0;
}

float ditherSample(vec2 pixel) {
    if (ditherMode == 0) return 0.5;
    vec2 seededPixel = pixel + vec2(ditherSeed, ditherSeed * 1.6180339);
    if (ditherMode == 2)
        return (bayer4x4(seededPixel) + 0.5) / 16.0;
    if (ditherMode == 3)
        return fract(sin(dot(seededPixel, vec2(12.9898, 78.233))) * 43758.5453);
    return fract(52.9829189 * fract(dot(seededPixel, vec2(0.06711056, 0.00583715))));
}

//opencode: post-tonemap fixed EGA and RGB332 quantizers.
vec3 palette16Color(int index) {
    if (index == 0) return vec3(0.0, 0.0, 0.0);
    if (index == 1) return vec3(0.0, 0.0, 0.667);
    if (index == 2) return vec3(0.0, 0.667, 0.0);
    if (index == 3) return vec3(0.0, 0.667, 0.667);
    if (index == 4) return vec3(0.667, 0.0, 0.0);
    if (index == 5) return vec3(0.667, 0.0, 0.667);
    if (index == 6) return vec3(0.667, 0.333, 0.0);
    if (index == 7) return vec3(0.667, 0.667, 0.667);
    if (index == 8) return vec3(0.333, 0.333, 0.333);
    if (index == 9) return vec3(0.0, 0.0, 1.0);
    if (index == 10) return vec3(0.0, 1.0, 0.0);
    if (index == 11) return vec3(0.0, 1.0, 1.0);
    if (index == 12) return vec3(1.0, 0.0, 0.0);
    if (index == 13) return vec3(1.0, 0.0, 1.0);
    if (index == 14) return vec3(1.0, 1.0, 0.0);
    return vec3(1.0, 1.0, 1.0);
}

vec3 quantizePalette16(vec3 color) {
    vec3 result = palette16Color(0);
    float bestDistance = dot(color - result, color - result);
    for (int index = 1; index < 16; index++) {
        vec3 candidate = palette16Color(index);
        float candidateDistance = dot(color - candidate, color - candidate);
        if (candidateDistance < bestDistance) {
            result = candidate;
            bestDistance = candidateDistance;
        }
    }
    return result;
}

vec3 quantizePalette(vec3 color) {
    if (paletteMode == 1) return quantizePalette16(color);
    if (paletteMode == 2) {
        vec3 levels = vec3(7.0, 7.0, 3.0);
        return floor(clamp(color, 0.0, 1.0) * levels + 0.5) / levels;
    }
    return color;
}

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 color = clamp(texture(sceneTex, uv).rgb, 0.0, 1.0);
    float paletteAmplitude = ditherStrength / (paletteMode == 1 ? 8.0 : 16.0);
    color = clamp(color + (ditherSample(gl_FragCoord.xy) - 0.5) * paletteAmplitude, 0.0, 1.0);
    finalColor = vec4(quantizePalette(color), 1.0);
}
