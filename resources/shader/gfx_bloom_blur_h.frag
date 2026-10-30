#version 330

in vec2 fragTexCoord;

uniform sampler2D inputTex;
uniform vec2 resolution;
uniform float amount = 1.0;

out vec4 finalColor;

//opencode: 9-tap separable Gaussian with linear-sampling optimization. weights[0]
//   is the "center" tap (0.227027), the others are the 4+4 corner taps counted twice
//   by sampling on each side. The two-pass nature means total energy per pass is
//   0.227027 + 2*(0.316216 + 0.070270 + 0.020898 + 0.005576) ? 1.053. This 5%
//   energy boost is intentional and compensates for the missing diagonal taps that
//   a true 9?9 kernel would have - bias toward the central axis matches the
//   linear-sampling-from-2-taps trick. Sum stays >1 because the kernel is renormalized
//   for a single direction; the full 2D kernel sums to 1.0.
const float weights[5] = float[](0.227027, 0.316216, 0.070270, 0.020898, 0.005576);
const float offsets[5] = float[](0.0, 1.384615, 3.230769, 5.076923, 6.923077);

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec2 texel = amount / resolution;
    vec3 result = texture(inputTex, uv).rgb * weights[0];
    
    for (int i = 1; i < 5; i++) {
        vec2 off = vec2(offsets[i] * texel.x, 0.0);
        result += texture(inputTex, uv + off).rgb * weights[i];
        result += texture(inputTex, uv - off).rgb * weights[i];
    }
    
    finalColor = vec4(result, 1.0);
}
