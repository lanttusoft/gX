//opencode: procedural pattern generator - plasma, gradient, checker, noise modes
#version 330

uniform float time;
uniform vec2  res;
uniform int   mode;
uniform float speed;
uniform float scale;
uniform vec3  color1;
uniform vec3  color2;

in vec2 fragTexCoord;
out vec4 fragColor;

// --- Utility ---
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; i++) {
        value += amp * noise(p * freq);
        freq *= 2.0;
        amp *= 0.5;
    }
    return value;
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// --- Modes ---

vec3 mode_plasma(vec2 uv, float t) {
    float c1 = sin(uv.x * 4.0 + t);
    float c2 = sin(uv.y * 5.0 - t * 1.3);
    float c3 = sin((uv.x + uv.y) * 3.0 + t * 0.7);
    float c4 = sin(length(uv) * 6.0 - t * 0.9);
    float v = c1 + c2 + c3 + c4;
    v = v * 0.25 + 0.5;
    vec3 col = mix(color1, color2, v);
    col *= 0.7 + 0.3 * sin(v * 6.28 + t);
    return col;
}

vec3 mode_gradient(vec2 uv, float t) {
    float v = uv.x * 0.5 + 0.5;
    float pulse = 0.5 + 0.5 * sin(t * 0.5);
    v = mix(v, uv.y * 0.5 + 0.5, pulse);
    vec3 col = mix(color1, color2, v);
    return col;
}

vec3 mode_checker(vec2 uv, float t) {
    vec2 gv = uv * 4.0;
    vec2 i = floor(gv);
    vec2 f = fract(gv);
    float pat = mod(i.x + i.y, 2.0);
    float border = 1.0 - step(0.9, max(f.x, 1.0 - f.x));
    border = max(border, 1.0 - step(0.9, max(f.y, 1.0 - f.y)));
    vec3 col = pat > 0.5 ? color1 : color2;
    col = mix(col, vec3(1.0), border * 0.3);
    // subtle animation - shift grid lines
    float glow = 0.5 + 0.5 * sin(uv.x * 20.0 + t) * 0.1;
    glow *= 0.5 + 0.5 * sin(uv.y * 20.0 - t * 0.7) * 0.1;
    col += glow * 0.05;
    return col;
}

vec3 mode_noise(vec2 uv, float t) {
    vec2 p = uv * 2.0 + vec2(t * 0.05, t * 0.03);
    float n = fbm(p);
    float n2 = fbm(p * 2.0 + vec2(1.0, 2.0));
    vec3 col = mix(color1, color2, n);
    col += 0.1 * n2 * vec3(0.5, 0.2, 0.8);
    return col;
}

void main() {
    vec2 uv = (gl_FragCoord.xy / res) * 2.0 - 1.0;
    uv.x *= res.x / res.y;
    uv *= scale;

    float t = time;

    vec3 col;
    if (mode == 0) {
        col = mode_plasma(uv, t);
    } else if (mode == 1) {
        col = mode_gradient(uv, t);
    } else if (mode == 2) {
        col = mode_checker(uv, t);
    } else if (mode == 3) {
        col = mode_noise(uv, t);
    } else {
        col = vec3(0.0);
    }

    fragColor = vec4(col, 1.0);
}
