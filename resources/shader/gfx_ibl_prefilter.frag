//opencode: roughness-prefiltered environment — independent gX GGX
//   importance-sampling implementation (split-sum method, Karis 2013).
//   No tutorial code is copied.
#version 330

in vec2 fragTexCoord;

uniform samplerCube sourceMap;
uniform float roughness = 0.5;
uniform mat4 captureView;

out vec4 fragColor;

float reverseBits(uint value) {
    value = (value << 16u) | (value >> 16u);
    value = ((value & 0x55555555u) << 1u) | ((value & 0xAAAAAAAAu) >> 1u);
    value = ((value & 0x33333333u) << 2u) | ((value & 0xCCCCCCCCu) >> 2u);
    value = ((value & 0x0F0F0F0Fu) << 4u) | ((value & 0xF0F0F0F0u) >> 4u);
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
    return float(value) * 2.3283064365386963e-10;
}

vec2 hammersleyPair(uint index, uint count) {
    return vec2(float(index) / float(count), reverseBits(index));
}

vec3 sampleGgxHalf(vec2 xi, vec3 normal, float roughness) {
    float alpha = roughness * roughness;
    float azimuth = 6.28318530718 * xi.x;
    float cosPolar = sqrt((1.0 - xi.y)
        / (1.0 + (alpha * alpha - 1.0) * xi.y));
    float sinPolar = sqrt(max(0.0, 1.0 - cosPolar * cosPolar));
    vec3 local = vec3(cos(azimuth) * sinPolar,
        sin(azimuth) * sinPolar, cosPolar);
    vec3 helper = abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y
        + normal * local.z);
}

void main() {
    vec2 localFace = fragTexCoord * 2.0 - 1.0;
    vec3 normal = normalize(mat3(captureView)
        * normalize(vec3(localFace, 1.0)));

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    const uint sampleCount = 256u;
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        vec2 xi = hammersleyPair(sampleIndex, sampleCount);
        vec3 halfVector = sampleGgxHalf(xi, normal, roughness);
        vec3 light = normalize(2.0 * dot(normal, halfVector) * halfVector
            - normal);
        float nDotL = max(dot(normal, light), 0.0);
        if (nDotL <= 0.0) continue;
        sum += texture(sourceMap, light).rgb * nDotL;
        weightSum += nDotL;
    }

    fragColor = vec4(sum / max(weightSum, 1e-6), 1.0);
}
