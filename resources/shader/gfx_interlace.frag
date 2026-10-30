#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;
uniform int interlaceMode = 0;      //opencode: 0 off, 1 horizontal, 2 vertical, 3 dot
uniform float interlaceStrength = 0.35;
uniform float interlacePhase = 0.0;

out vec4 finalColor;

void main() {
    //opencode: final display treatment samples the already presented LDR image.
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 color = texture(sceneTex, uv).rgb;
    float x = floor(gl_FragCoord.x + interlacePhase);
    float y = floor(gl_FragCoord.y + interlacePhase);
    float active = 0.0;
    if (interlaceMode == 1) active = mod(y, 2.0);
    else if (interlaceMode == 2) active = mod(x, 2.0);
    else if (interlaceMode == 3) active = mod(x + y, 2.0);
    float strength = clamp(interlaceStrength, 0.0, 1.0);
    color *= 1.0 - active * (0.45 * strength);
    finalColor = vec4(color, 1.0);
}
