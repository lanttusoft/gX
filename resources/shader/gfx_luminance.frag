//opencode: v0.3-M1 - luminance downsample for auto-exposure. Renders the HDR
//   scene into a small (64x64) RGBA16F target; R channel carries linear
//   luminance (Rec.709 weights). CPU side log-averages via readback.
#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;

out vec4 finalColor;

void main() {
    //opencode: 4-tap box - at 64x64 output the extra taps buy little; keep cheap
    vec2 uv = fragTexCoord;
    vec3 c = texture(sceneTex, uv).rgb;
    c += texture(sceneTex, uv + vec2( 0.5 / 2048.0,  0.5 / 2048.0)).rgb;
    c += texture(sceneTex, uv + vec2(-0.5 / 2048.0,  0.5 / 2048.0)).rgb;
    c += texture(sceneTex, uv + vec2( 0.5 / 2048.0, -0.5 / 2048.0)).rgb;
    c *= 0.25;

    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    finalColor = vec4(lum, 0.0, 0.0, 1.0);
}
