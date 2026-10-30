//opencode: shared SDF boolean operations and domain repetition
//   Inigo Quilez smooth-min/domain formulations (MIT); see license/iq/LICENSE.

#ifndef GX_COMMON_SDF_OPS_GLSL
#define GX_COMMON_SDF_OPS_GLSL

float gX_SDF_Union(float a, float b) {
    return min(a, b);
}

float gX_SDF_Sub(float a, float b) {
    return max(a, -b);
}

float gX_SDF_Inter(float a, float b) {
    return max(a, b);
}

float gX_SDF_SmoothUnion(float a, float b, float k) {
    //ai: zero smoothing is the hard operator, never a division by zero.
    if (k <= 0.0) return min(a,b);
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float gX_SDF_SmoothSub(float a, float b, float k) {
    //ai: A minus B matches gX_SDF_Sub, including zero smoothness.
    if (k <= 0.0) return max(a,-b);
    float h = max(k-abs(a+b),0.0)/k;
    return max(a,-b) + k*h*h*0.25;
}

float gX_SDF_SmoothInter(float a, float b, float k) {
    //ai: zero smoothing is an ordinary intersection.
    if (k <= 0.0) return max(a,b);
    float h = clamp(0.5 - 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

vec3 gX_SDF_RepeatInf(vec3 p, vec3 c) {
    return mod(p + 0.5 * c, c) - 0.5 * c;
}

vec3 gX_SDF_RepeatFin(vec3 p, vec3 a, vec3 d) {
    return p - d * clamp(round(p / d), -a, a);
}

vec2 gX_SDF_RepeatInf_Polar(vec2 p, float n) {
    float angle = 2.0 * 3.14159265 / n;
    float a = atan(p.y, p.x) + angle * 0.5;
    float r = length(p);
    float c = floor(a / angle);
    a = mod(a, angle) - angle * 0.5;
    return vec2(vec2(cos(a), sin(a)) * r);
}

#endif //GX_COMMON_SDF_OPS_GLSL
