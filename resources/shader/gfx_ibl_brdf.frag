//opencode: split-sum BRDF integration LUT — independent gX implementation of
//   the published GGX/split-sum method (Karis 2013). Red=scale, green=bias.
//   No tutorial code is copied.
#version 330

in vec2 fragTexCoord;

uniform vec2 resolution;

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

float smithG1(float cosTheta, float roughness) {
    float alpha = roughness * roughness;
    float k = alpha * 0.5;
    return cosTheta / (cosTheta * (1.0 - k) + k);
}

float smithG2(vec3 normal, vec3 view, vec3 light, float roughness) {
    return smithG1(max(dot(normal, view), 0.0), roughness)
        * smithG1(max(dot(normal, light), 0.0), roughness);
}

vec2 integrateSplitSum(float nDotV, float roughness) {
    vec3 view = vec3(sqrt(max(0.0, 1.0 - nDotV * nDotV)), 0.0, nDotV);
    vec3 normal = vec3(0.0, 0.0, 1.0);
    const uint sampleCount = 512u;
    float scale = 0.0;
    float bias = 0.0;

    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        vec2 xi = hammersleyPair(sampleIndex, sampleCount);
        vec3 halfVector = sampleGgxHalf(xi, normal, roughness);
        vec3 light = normalize(2.0 * dot(view, halfVector) * halfVector
            - view);

        float nDotL = max(light.z, 0.0);
        float nDotH = max(halfVector.z, 0.0);
        float vDotH = max(dot(view, halfVector), 0.0);
        if (nDotL <= 0.0) continue;

        float visibility = smithG2(normal, view, light, roughness)
            * vDotH / max(nDotH * nDotV, 1e-6);
        float fresnel = pow(1.0 - vDotH, 5.0);
        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }
    return vec2(scale, bias) / float(sampleCount);
}

void main() {
    float nDotV = clamp(fragTexCoord.x, 0.0, 1.0);
    float roughness = max(fragTexCoord.y, 0.001);
    fragColor = vec4(integrateSplitSum(nDotV, roughness), 0.0, 1.0);
}
