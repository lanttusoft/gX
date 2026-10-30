#version 330

//opencode: SpecularCSS-inspired GUI material preview. The material mode is
//   uniforms[0].z: 0 flat, 1 chalk, 2 plastic, 3 glass, 4 metal.

uniform vec4 col;
uniform vec2 res;
uniform vec4 box;
uniform vec4 uniforms[4];

out vec4 finalColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.y, q.x), 0.0) - r;
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    if (col.a <= 0.002) discard;

    vec2 fragPos = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y);
    vec2 center = box.xy + box.zw * 0.5;
    vec2 p = fragPos - center;
    vec2 half_ = max(box.zw * 0.5 - 1.0, vec2(1.0));
    float radius = clamp(uniforms[0].x, 0.0, min(box.z, box.w) * 0.5);
    float d = sdRoundBox(p, half_, radius);
    float aa = max(fwidth(d), 0.0001);
    float silhouette = 1.0 - smoothstep(-aa, aa, d);
    float localY = clamp((fragPos.y - box.y) / max(box.w, 1.0), 0.0, 1.0);
    float topLight = 1.0 - smoothstep(0.0, 0.72, localY);
    float mode = uniforms[0].z;
    float roughness = clamp(uniforms[2].x, 0.0, 1.0);
    vec3 base = col.rgb;
    vec3 light = uniforms[1].rgb;
    vec3 fill = base;
    float opacity = 1.0;

    if (mode < 0.5) {
        fill = base * mix(0.92, 1.02, topLight);
    } else if (mode < 1.5) {
        float grain = hash21(floor(fragPos * 0.65)) - 0.5;
        float fleck = step(0.78, hash21(floor(fragPos * 0.18)));
        fill = base * (0.83 + topLight * 0.12 + grain * 0.12);
        fill += light * fleck * 0.06;
    } else if (mode < 2.5) {
        float highlight = pow(max(0.0, 1.0 - abs(localY - 0.18) / 0.24), 4.0);
        fill = base * (0.72 + topLight * 0.24);
        fill += light * highlight * (0.12 + (1.0 - roughness) * 0.24);
    } else if (mode < 3.5) {
        float rim = smoothstep(0.35, 0.0, abs(localY - 0.5));
        float highlight = pow(max(0.0, 1.0 - abs(localY - 0.16) / 0.13), 8.0);
        fill = base * (0.34 + topLight * 0.22 + rim * 0.12);
        fill += light * highlight * 0.5;
        opacity = 0.58;
    } else if (mode < 4.5) {
        float brushed = 0.5 + 0.5 * sin(fragPos.y * 0.075 + sin(fragPos.x * 0.012) * 2.0);
        float highlight = pow(max(0.0, 1.0 - abs(localY - 0.2) / 0.22), 5.0);
        fill = base * (0.58 + brushed * 0.20 + topLight * 0.12);
        fill += light * highlight * (0.08 + (1.0 - roughness) * 0.2);
    } else {
        vec2 uv = fragPos / max(res, vec2(1.0));
        vec2 q = uv - vec2(0.5, 0.38);
        float spotlight = exp(-dot(q * vec2(0.9, 1.15), q * vec2(0.9, 1.15)) * 4.2);
        float vignette = 1.0 - smoothstep(0.32, 0.78, length(uv - vec2(0.5)));
        vec2 grid = fragPos / 64.0;
        vec2 gridDist = min(fract(grid), 1.0 - fract(grid));
        float gridWidth = max(max(fwidth(grid.x), fwidth(grid.y)) * 1.5, 0.002);
        float gridLine = 1.0 - smoothstep(0.0, gridWidth, min(gridDist.x, gridDist.y));
        fill = base * (0.70 + spotlight * 0.20 + vignette * 0.10);
        fill = mix(fill, light, gridLine * 0.035);
        fill += light * spotlight * 0.05;
    }

    if (mode < 4.5) {
        float rim = 1.0 - smoothstep(0.6, 1.8, abs(d));
        fill = mix(fill, light, rim * 0.22);
    }
    finalColor = vec4(fill, col.a * silhouette * opacity);
}
