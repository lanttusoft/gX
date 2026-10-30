//opencode: CPU falling snow — deterministic sim + GL33 point-sprite render.
//   Sim and render are separate stages: Update advances LCG-driven flakes,
//   Draw uploads the CPU buffer to a dynamic VBO. No compute, no SSBO.
#version 330

layout(location = 0) in vec3 inPos;
layout(location = 1) in float inSize;
layout(location = 2) in float inAlpha;

uniform mat4 vp;
uniform float pointScale;

out float vAlpha;

void main() {
    vec4 clipPos = vp * vec4(inPos, 1.0);
    float depth = max(clipPos.w, 0.1);
    gl_PointSize = clamp(inSize * pointScale / depth, 1.0, 9.0);
    gl_Position = clipPos;
    vAlpha = inAlpha;
}
