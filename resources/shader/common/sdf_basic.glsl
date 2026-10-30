//opencode: shared basic SDF primitives
//   Inigo Quilez SDF formulations (MIT); see license/iq/LICENSE.

#ifndef GX_COMMON_SDF_BASIC_GLSL
#define GX_COMMON_SDF_BASIC_GLSL

float gX_SDF_Sphere(vec3 p, float r) {
    return length(p) - r;
}

float gX_SDF_Cube(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float gX_SDF_Torus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float gX_SDF_BoxFrame(vec3 p, vec3 b, float e) {
    p = abs(p) - b;
    vec3 q = abs(p + e) - e;
    return min(min(
        length(max(vec3(p.x, q.y, q.z), 0.0)) + min(max(p.x, max(q.y, q.z)), 0.0),
        length(max(vec3(q.x, p.y, q.z), 0.0)) + min(max(q.x, max(p.y, q.z)), 0.0)),
        length(max(vec3(q.x, q.y, p.z), 0.0)) + min(max(q.x, max(q.y, p.z)), 0.0));
}

float gX_SDF_Cylinder(vec3 p, vec2 h) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - h;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float gX_SDF_Cone(vec3 p, vec3 c) {
    vec2 q = vec2(length(p.xz), p.y);
    float d1 = -q.y - c.z;
    float d2 = max(dot(q, c.xy), q.y);
    return max(d1, d2);
}

float gX_SDF_Capsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float gX_SDF_Octahedron(vec3 p, float s) {
    p = abs(p);
    return (p.x + p.y + p.z - s) * 0.57735027;
}

float gX_SDF_Plane(vec3 p, vec4 n) {
    return dot(p, n.xyz) + n.w;
}

//opencode: rounded box (iq) — half extents b, corner radius r. CPU twin:
//   gX_F32_SDF_RoundBox.
float gX_SDF_RoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

//opencode: hex prism (iq) — radius r, half-height h. CPU twin:
//   gX_F32_SDF_HexPrism.
float gX_SDF_HexPrism(vec3 p, vec2 h) {
    const vec3 k = vec3(-0.8660254, 0.5, 0.57735);
    p = abs(p);
    p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
    vec2 d = vec2(
        length(p.xy - vec2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x))
            * sign(p.y - h.x),
        p.z - h.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

//opencode: Menger sponge (iq) — half extents b. CPU twin: gX_F32_SDF_Menger
//   (same fold; both use floor-mod so negative inputs match GLSL mod).
float gX_SDF_Menger(vec3 p, vec3 b) {
    float d = gX_SDF_Cube(p, b);
    float s = 1.0 / b.x;
    for (int m = 0; m < 8; m++) {
        vec3 a = mod(p * s, 2.0) - 1.0;
        s *= 3.0;
        vec3 r = abs(1.0 - abs(a) * 3.0);
        float da = max(r.x, r.y);
        float db = max(r.y, r.z);
        float dc = max(r.z, r.x);
        float c = (min(da, min(db, dc)) - 1.0) / s;
        d = max(d, c);
    }
    return d;
}

#endif //GX_COMMON_SDF_BASIC_GLSL
