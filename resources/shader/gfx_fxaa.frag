#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;
uniform vec2 resolution;
uniform int ditherMode = 1;       //opencode: 0 off, 1 IGN, 2 Bayer4, 3 seeded noise
uniform float ditherSeed = 0.0;   //opencode: frame-stable seed unless animated by hook
uniform float ditherStrength = 1.5; //opencode: amplitude in 8-bit LSBs

out vec4 finalColor;

//opencode: v0.1.18 - FXAA 3.11-quality edge-aware anti-aliasing (compact
//   Timothy Lottes formulation; 3-clause BSD, license/fxaa/LICENSE). Runs on
//   the final composite (post-tonemap,
//   LDR) where aliasing is most visible; MSAA only covers geometry edges, so
//   shader-derived edges (specular boundaries, alpha content) stay aliased
//   without this pass. 5-tap luma-edge detection + directional 4-tap blur.

#define FXAA_SPAN_MAX 8.0
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_REDUCE_MIN (1.0 / 128.0)

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

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention (same as the
    //   other post-process passes in this pipeline)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec2 invRes = 1.0 / resolution;

    vec3 rgbNW = texture(sceneTex, uv + vec2(-1.0, -1.0) * invRes).rgb;
    vec3 rgbNE = texture(sceneTex, uv + vec2( 1.0, -1.0) * invRes).rgb;
    vec3 rgbSW = texture(sceneTex, uv + vec2(-1.0,  1.0) * invRes).rgb;
    vec3 rgbSE = texture(sceneTex, uv + vec2( 1.0,  1.0) * invRes).rgb;
    vec3 rgbM  = texture(sceneTex, uv).rgb;

    const vec3 lumaW = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, lumaW);
    float lNE = dot(rgbNE, lumaW);
    float lSW = dot(rgbSW, lumaW);
    float lSE = dot(rgbSE, lumaW);
    float lM  = dot(rgbM,  lumaW);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    //opencode: flat region - early out keeps text/UI crisp (no blur at all)
    if (lMax - lMin < 0.06) {
        finalColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * invRes;

    vec3 rgbA = 0.5 * (texture(sceneTex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(sceneTex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneTex, uv + dir * -0.5).rgb +
                                     texture(sceneTex, uv + dir *  0.5).rgb);
    float lB = dot(rgbB, lumaW);

    //opencode: if the blurred sample falls outside the local luma range the
    //   direction straddled an unrelated edge - keep the safer 2-tap blend
    //opencode: judging - end-of-chain IGN dither (the composite's dither
    //   dies in this blur; quantize here instead, +-0.75 LSB like composite).
    vec3 fxaaOut = (lB < lMin || lB > lMax) ? rgbA : rgbB;
    float fxaaDither = ditherSample(gl_FragCoord.xy);
    finalColor = vec4(fxaaOut + (fxaaDither - 0.5) * (ditherStrength / 255.0), 1.0);
}
