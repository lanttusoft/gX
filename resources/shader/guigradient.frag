#version 330

uniform vec4 col;
uniform vec2 res;
uniform float time;

out vec4 finalColor;

void main() {
    vec2 screencoord = gl_FragCoord.xy / res;
    vec4 color = col;
    float factor = 0.25 * (sin(100.0 * screencoord.y - 0.5 * time) + 1.0) + 0.75;
    color.rgb *= factor;
    if (color.a < 0.5) discard;
    finalColor = color;
}
