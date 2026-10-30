//opencode: CPU rain streak fragment — flat blue-grey, per-vertex alpha.
#version 330

in float vAlpha;

//opencode: weather obeys the scene lighting (ambient + sun tint);
//   the fallback keeps the sprite visible if the uniform is unset.
uniform vec3 lightTint;

out vec4 finalColor;

void main() {
    vec3 tint = length(lightTint) < 0.001 ? vec3(1.0) : lightTint;
    vec3 drop = vec3(0.65, 0.75, 0.90) * tint;
    finalColor = vec4(drop, vAlpha);
}
