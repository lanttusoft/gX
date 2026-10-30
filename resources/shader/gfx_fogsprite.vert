//opencode: CPU fog sprite vertex — billboard corners computed on CPU.
#version 330

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAlpha;

uniform mat4 vp;

out vec2 vUv;
out float vAlpha;

void main() {
    gl_Position = vp * vec4(inPos, 1.0);
    vUv = inUv;
    vAlpha = inAlpha;
}
