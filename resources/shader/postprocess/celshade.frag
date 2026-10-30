#version 330
//opencode: postprocess shader
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec4 c = texture(input, gl_FragCoord.xy / res);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float cel = floor(l * 3.0) / 3.0;
    vec2 e = vec2(1.0) / res;
    float edge = 0.0;
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) {
        float nl = dot(texture(input, gl_FragCoord.xy / res + vec2(float(x), float(y)) * e).rgb, vec3(0.299, 0.587, 0.114));
        edge = max(edge, abs(cel - floor(nl * 3.0) / 3.0));
    }
    vec3 outline = edge > 0.0 ? vec3(0.0) : vec3(cel);
    finalColor = vec4(mix(c.rgb / max(l, 0.01) * cel, outline, step(0.01, edge)), 1.0);
}
