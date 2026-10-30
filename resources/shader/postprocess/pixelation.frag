#version 330
//opencode: postprocess pixelation shader
//   uniforms[0].x = block size in pixels (1-64, default 8)
//   Sharp nearest-neighbor blocks for a retro/Minecraft-style pixelation
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    float blockSize = max(uniforms[0].x, 1.0);
    vec2 blockCoord = floor(gl_FragCoord.xy / blockSize) * blockSize;
    vec2 uv = blockCoord / res;
    finalColor = texture(input, uv);
}
