#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec4 c = texture(input, gl_FragCoord.xy / res);
    vec3 fogColor = uniforms[0].rgb;
    float depth = gl_FragCoord.z / gl_FragCoord.w;
    float fog = 1.0 - exp(-depth * uniforms[0].a);
    finalColor = vec4(mix(c.rgb, fogColor, fog), 1.0);
}
