#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 coord = gl_FragCoord.xy / res - 0.5;
    float a = atan(coord.y, coord.x);
    float r = length(coord);
    a += uniforms[0].x * r + time * 0.5;
    coord = vec2(cos(a), sin(a)) * r + 0.5;
    finalColor = texture(input, coord);
}
