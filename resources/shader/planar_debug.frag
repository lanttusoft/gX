#version 330
//opencode: v0.1.33 - solid-color output for planar pipeline verification
in vec3 vNormal;
uniform vec4 uColor;
out vec4 finalColor;
void main() {
    vec3 n = normalize(vNormal);
    float shade = 0.6 + 0.4 * max(dot(n, normalize(vec3(0.4, 1.0, 0.3))), 0.0);
    finalColor = vec4(uColor.rgb * shade, 1.0);
}
