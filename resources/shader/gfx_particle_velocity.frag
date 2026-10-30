//opencode: particle velocity fragment shader — valid motion plus opaque flag.
#version 430

layout(location = 0) in vec2 vVel;
layout(location = 16) uniform float uGlass = 1.0;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(vVel, 1.0, uGlass);
}
