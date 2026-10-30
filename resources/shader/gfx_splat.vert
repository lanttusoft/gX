//opencode: gaussian splat vertex — anisotropic billboard from 2D eigen axes.
#version 330

layout(location = 0) in vec3 inCenter;
layout(location = 1) in vec2 inCorner;
layout(location = 2) in vec3 inConic;
layout(location = 3) in vec3 inColor;
layout(location = 4) in float inAlpha;
layout(location = 5) in vec2 inAxisU;
layout(location = 6) in vec2 inAxisV;

uniform mat4 vp;
uniform vec2 halfView;

out vec2 vOffset;
out vec3 vConic;
out vec3 vColor;
out float vAlpha;

void main() {
    vec4 clip = vp * vec4(inCenter, 1.0);
    vec2 pxOffset = inCorner.x * inAxisU + inCorner.y * inAxisV;
    clip.xy += pxOffset / halfView * clip.w;
    gl_Position = clip;
    vOffset = pxOffset;
    vConic = inConic;
    vColor = inColor;
    vAlpha = inAlpha;
}
