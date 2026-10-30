#version 330
//opencode: postprocess shader - vertical Gaussian blur (9-tap)
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 px = 1.0 / res;
    vec4 sum = vec4(0.0);
    float kernel[9] = float[](0.05, 0.09, 0.12, 0.15, 0.18, 0.15, 0.12, 0.09, 0.05);
    float total = 0.0;
    for (int i = -4; i <= 4; i++) {
        float k = kernel[i + 4];
        sum += texture(input, gl_FragCoord.xy / res + vec2(0.0, float(i)) * px) * k;
        total += k;
    }
    finalColor = sum / total;
}
