//opencode: mirror feedback — fresh bounce to history with energy decay.
//   The decay bounds feedback energy so the regress converges instead of
//   accumulating to white (or NaN-poisoning through 0*NaN convex paths).
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform float energy;

out vec4 finalColor;

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 bounce = texture(colorMap, uv).rgb;
    if (any(isnan(bounce)) || any(isinf(bounce))) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        finalColor = vec4(bounce * energy, 1.0);
    }
}
