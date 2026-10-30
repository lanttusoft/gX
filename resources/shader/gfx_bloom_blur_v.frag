#version 330

in vec2 fragTexCoord;

uniform sampler2D inputTex;
uniform vec2 resolution;
uniform float amount = 1.0;

out vec4 finalColor;

// 9-tap separable Gaussian (linear sampling optimization, sigma ~ 2.5)
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
        vec2 off = vec2(0.0, offsets[i] * texel.y);
        result += texture(inputTex, uv + off).rgb * weights[i];
        result += texture(inputTex, uv - off).rgb * weights[i];
    }
    
    finalColor = vec4(result, 1.0);
}
