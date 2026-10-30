#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 coord = gl_FragCoord.xy / res;
    vec2 center = vec2(0.5, 0.5);
    float d = distance(coord, center);
    coord += (coord - center) * uniforms[0].x * sin(d * 20.0 + time * 2.0);
    finalColor = texture(input, coord);
}
