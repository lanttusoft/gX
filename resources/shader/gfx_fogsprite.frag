//opencode: CPU fog sprite fragment — radial soft falloff, fog tint.
#version 330

in vec2 vUv;
in float vAlpha;

uniform vec3 tint;

out vec4 finalColor;

void main() {
    vec2 centered = vUv - vec2(0.5);
    float dist = length(centered) * 2.0;
    if (dist > 1.0) discard;
    float soft = 1.0 - smoothstep(0.2, 1.0, dist);
    finalColor = vec4(tint, vAlpha * soft);
}
