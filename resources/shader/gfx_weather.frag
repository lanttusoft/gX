//opencode: CPU falling snow fragment — soft circular flake, normal alpha.
#version 330

in float vAlpha;

//opencode: weather obeys the scene lighting (ambient + sun tint);
//   the fallback keeps the sprite visible if the uniform is unset.
uniform vec3 lightTint;

out vec4 finalColor;

void main() {
    vec2 centered = gl_PointCoord - vec2(0.5);
    float dist = length(centered) * 2.0;
    if (dist > 1.0) discard;
    float soft = 1.0 - dist * dist;
    vec3 tint = length(lightTint) < 0.001 ? vec3(1.0) : lightTint;
    vec3 flake = vec3(0.92, 0.96, 1.0) * tint;
    finalColor = vec4(flake, vAlpha * soft);
}
