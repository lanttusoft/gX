#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec4 c = texture(input, gl_FragCoord.xy / res);
    float a = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float mask = step(uniforms[0].x, a);
    finalColor = vec4(c.rgb * mask, 1.0);
}
