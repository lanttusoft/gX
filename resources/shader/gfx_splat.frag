//opencode: gaussian splat fragment — conic exponent falloff, hard discard.
#version 330

in vec2 vOffset;
in vec3 vConic;
in vec3 vColor;
in float vAlpha;

out vec4 finalColor;

void main() {
    float power = -0.5 * (vConic.x * vOffset.x * vOffset.x
        + vConic.z * vOffset.y * vOffset.y)
        - vConic.y * vOffset.x * vOffset.y;
    if (power > 0.0) discard;
    float weight = exp(power);
    if (weight < 0.004) discard;
    finalColor = vec4(vColor, vAlpha * weight);
}
