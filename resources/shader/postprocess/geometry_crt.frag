#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 coord = gl_FragCoord.xy / res;
    vec2 curved = coord - 0.5;
    curved *= 1.0 + uniforms[0].x * dot(curved, curved);
    coord = curved + 0.5;
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
        finalColor = vec4(0.0);
        return;
    }
    vec4 c = texture(input, coord);
    float scanline = sin(gl_FragCoord.y * 3.14159 * 0.5) * 0.5 + 0.5;
    c.rgb *= 0.8 + 0.2 * scanline;
    finalColor = c;
}
