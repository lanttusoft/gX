//opencode: CPU rain streak vertex — line-segment endpoints in world space.
#version 330

layout(location = 0) in vec3 inPos;
layout(location = 1) in float inAlpha;

uniform mat4 vp;

out float vAlpha;

void main() {
    gl_Position = vp * vec4(inPos, 1.0);
    vAlpha = inAlpha;
}
