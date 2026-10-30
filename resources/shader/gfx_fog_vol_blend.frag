//opencode: volumetric fog blend - v0.3-M3 passthrough.
//   The gen pass bakes the full composite delta (scene*T + inscatter - scene);
//   this shader just echoes it so the caller can composite additively via the
//   proven gX_Texture_Draw route (same as godrays).
#version 330

uniform sampler2D fogMap;

in vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    fragColor = vec4(texture(fogMap, uv).rgb, 1.0);
}
