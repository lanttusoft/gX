#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec4 c = texture(input, gl_FragCoord.xy / res);
    vec2 uv = gl_FragCoord.xy / res;
    vec2 center = vec2(uniforms[0].x, uniforms[0].y);
    vec2 dir = normalize(uv - center);
    float d = distance(uv, center);
    float flare = 0.01 / (d * d + 0.01);
    vec3 flareColor = vec3(1.0, 0.8, 0.4) * flare * 0.5;
    finalColor = vec4(c.rgb + fl color, 1.0);
}
