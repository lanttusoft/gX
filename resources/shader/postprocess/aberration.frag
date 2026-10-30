#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 uv = gl_FragCoord.xy / res;
    float strength = uniforms[0].x * 0.01;
    float r = texture(input, uv + vec2(strength, 0.0)).r;
    float g = texture(input, uv).g;
    float b = texture(input, uv - vec2(strength, 0.0)).b;
    finalColor = vec4(r, g, b, 1.0);
}
