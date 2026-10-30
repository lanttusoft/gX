//opencode: shared noise functions - simplex noise and hash helpers
//   Simplex formulation: Ashima Arts / Stefan Gustavson (MIT); see
//   license/ashima/LICENSE.

#ifndef GX_COMMON_NOISE_GLSL
#define GX_COMMON_NOISE_GLSL

float gX_Noise_Hash1(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float gX_Noise_Hash1(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 gX_Noise_Hash3(vec2 p) {
    vec3 h = vec3(dot(p, vec2(127.1, 311.7)),
                  dot(p, vec2(269.5, 183.3)),
                  dot(p, vec2(419.2, 371.9)));
    return fract(sin(h) * 43758.5453123);
}

vec3 gX_Noise_Hash3(vec3 p) {
    //ai: signed gradients from the sine-free hash — fract(sin(dot(...)))
    //   degenerates for the large integer lattice inputs the simplex uses
    //   (the field measured 0.5 +/- 0.002 on NVIDIA, starving coverage).
    vec3 h = vec3(gX_Noise_Hash1(p),
                  gX_Noise_Hash1(p + vec3(17.1, 53.7, 91.3)),
                  gX_Noise_Hash1(p + vec3(41.3, 7.9, 123.7)));
    return h * 2.0 - 1.0;
}

float gX_Noise_Value(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = gX_Noise_Hash1(i);
    float b = gX_Noise_Hash1(i + vec2(1.0, 0.0));
    float c = gX_Noise_Hash1(i + vec2(0.0, 1.0));
    float d = gX_Noise_Hash1(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float gX_Noise_Simplex(vec3 p) {
    const vec3 C = vec3(0.166666667, 0.333333333, 0.5);
    vec3 i  = floor(p + dot(p, C.yyy));
    vec3 x0 = p - i + dot(i, C.xxx);
    vec3 g  = step(x0.yzx, x0.xyz);
    vec3 l  = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - C.zzz;
    vec4 v  = vec4(
        dot(x0, x0), dot(x1, x1),
        dot(x2, x2), dot(x3, x3)
    );
    vec4 h  = max(0.5 - v, 0.0);
    vec4 s  = h * h * h * h;
    vec4 t  = vec4(
        dot(x0, gX_Noise_Hash3(i)),
        dot(x1, gX_Noise_Hash3(i + i1)),
        dot(x2, gX_Noise_Hash3(i + i2)),
        dot(x3, gX_Noise_Hash3(i + 1.0))
    );
    //ai: Ashima's 42x normalisation — the h^4 weights make the raw sum
    //   ~+/-0.02, so without it the field sat at 0.5 +/- 0.002 and every
    //   coverage threshold collapsed.
    return 0.5 + 0.5 * clamp(42.0 * dot(s, t), -1.0, 1.0);
}

float gX_Noise_FBM(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        value += amplitude * gX_Noise_Value(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

#endif //GX_COMMON_NOISE_GLSL
