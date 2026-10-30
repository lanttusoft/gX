#version 330
//opencode: SSAO edge-aware blur — independent gX implementation.
//   Four-by-four depth-masked box reconstruction; it keeps occlusion from
//   crossing a depth discontinuity. No tutorial code is copied.

in vec2 fragTexCoord;

uniform sampler2D ssaoMap;
uniform sampler2D depthMap;
uniform vec2 resolution;
uniform float edgeThreshold;     // depth-difference threshold to avoid bleeding (default 0.05)

out vec4 finalColor;

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec2 texel = 1.0 / resolution;
    float centerDepth = texture(depthMap, uv).r;

    float occlusion = 0.0;
    float weightSum = 0.0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            vec2 tap = uv + vec2(float(column - 2), float(row - 2)) * texel;
            float tapDepth = texture(depthMap, tap).r;
            float depthGap = abs(tapDepth - centerDepth);
            float weight = 1.0 - smoothstep(0.0, edgeThreshold, depthGap);
            occlusion += texture(ssaoMap, tap).r * weight;
            weightSum += weight;
        }
    }

    finalColor = vec4(occlusion / max(weightSum, 0.001), 0.0, 0.0, 1.0);
}
