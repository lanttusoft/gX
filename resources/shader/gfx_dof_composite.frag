//opencode: DOF composite - blend original with blurred based on CoC
//   Sharp pixels (CoC ~ 0) pass through unchanged.
//   Blurry pixels (CoC ~ 1) use the blurred result.
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;  //opencode: original sharp image
uniform sampler2D blurMap;   //opencode: blurred result
uniform sampler2D cocMap;
uniform sampler2D depthMap;  //opencode: full-resolution depth classification

out vec4 finalColor;

//opencode: reduced-resolution DOF upsample. Spatial bilinear weights are
//   rejected at depth/CoC discontinuities so foreground silhouettes do not
//   inherit the far-background blur indiscriminately.
vec3 depthAwareBlur(vec2 uv, float centerDepth, float centerCoc) {
    vec2 blurSize = vec2(textureSize(blurMap, 0));
    vec2 footprint = uv * blurSize - 0.5;
    ivec2 base = ivec2(floor(footprint));
    vec2 fraction = fract(footprint);
    vec3 accumulated = vec3(0.0);
    float total = 0.0;

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            ivec2 tap = clamp(base + ivec2(x, y), ivec2(0), ivec2(blurSize) - ivec2(1));
            vec2 tapUv = (vec2(tap) + 0.5) / blurSize;
            float spatial = (x == 0 ? 1.0 - fraction.x : fraction.x)
                * (y == 0 ? 1.0 - fraction.y : fraction.y);
            float depthDelta = abs(texture(depthMap, tapUv).r - centerDepth);
            float cocDelta = abs(texture(cocMap, tapUv).r - centerCoc);
            float weight = spatial * exp(-depthDelta * 128.0 - cocDelta * 8.0);
            vec3 tapColor = texture(blurMap, tapUv).rgb;
            if (!(any(isnan(tapColor)) || any(isinf(tapColor)))) {
                accumulated += tapColor * weight;
                total += weight;
            }
        }
    }

    return accumulated / max(total, 0.001);
}

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: flip Y for RT source (colorMap, blurMap, cocMap are RTs)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec3 sharp = texture(colorMap, uv).rgb;
    float coc = texture(cocMap, uv).r;
    float depth = texture(depthMap, uv).r;
    ivec2 fullSize = textureSize(colorMap, 0);
    ivec2 blurSize = textureSize(blurMap, 0);
    vec3 blurry = fullSize == blurSize
        ? texture(blurMap, uv).rgb
        : depthAwareBlur(uv, depth, coc);
    
    vec3 color = mix(sharp, blurry, coc);
    finalColor = vec4(color, 1.0);
}
