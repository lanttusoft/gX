//opencode: particle fragment shader - renders point sprites as soft circles.
//   Color read from flat varying vColor (set by vertex shader from SSBO).
//   Additive blending expected. Alpha fades toward the soft circle edge.
#version 430

uniform vec3 cameraPos;

flat in vec4 vColor;
out vec4 fragColor;

void main() {
    vec2 center = gl_PointCoord - vec2(0.5);
    float dist = length(center);
    if (dist > 0.5) discard;

    float alpha = 1.0 - smoothstep(0.0, 0.5, dist);

    fragColor = vec4(vColor.rgb, alpha * vColor.a * 0.8);
}
