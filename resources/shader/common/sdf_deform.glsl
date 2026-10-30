//opencode: shared SDF space deformation utilities
//   Inigo Quilez domain formulations (MIT); see license/iq/LICENSE.

#ifndef GX_COMMON_SDF_DEFORM_GLSL
#define GX_COMMON_SDF_DEFORM_GLSL

vec3 gX_SDF_Twist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    mat2 rot = mat2(c, -s, s, c);
    return vec3(rot * p.xz, p.y);
}

vec3 gX_SDF_Bend(vec3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    mat2 rot = mat2(c, -s, s, c);
    return vec3(rot * vec2(p.z, p.y), p.x);
}

vec3 gX_SDF_Elongate(vec3 p, vec3 h) {
    return p - clamp(p, -h, h);
}

vec3 gX_SDF_Displace(vec3 p, float amount, float freq) {
    float d = sin(p.x * freq) * sin(p.y * freq) * sin(p.z * freq);
    return p + d * amount;
}

float gX_SDF_Round(float d, float r) {
    return d - r;
}

float gX_SDF_Onion(float d, float t) {
    return abs(d) - t;
}

#endif //GX_COMMON_SDF_DEFORM_GLSL
