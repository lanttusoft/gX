#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float fogDistance = 200.0;
uniform vec4 fogColor = vec4(0.7, 0.7, 0.8, 1.0);
uniform vec3 sunDir = vec3(0.5, 0.8, 0.3);
uniform float ambient = 0.4;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    if (texColor.a < 0.1) discard;

    // Simple directional lighting
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(sunDir);
    float NdotL = max(dot(N, L), 0.0);
    float light = ambient + (1.0 - ambient) * NdotL;
    vec3 litColor = texColor.rgb * light;

    // Fog
    float z = gl_FragCoord.z * 2.0 - 1.0;
    float linearDepth = (2.0 * 0.1 * 1000.0) / (1000.0 + 0.1 - z * (1000.0 - 0.1));
    float fog = clamp(linearDepth / fogDistance, 0.0, 1.0);
    fog = fog * fog;

    finalColor = vec4(mix(litColor, fogColor.rgb, fog), texColor.a);
}
