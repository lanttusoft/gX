#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;

out vec4 finalColor;

//ai: PRISM-1 fixed 4x4 Bayer threshold, evaluated in 320x240 output pixels.
float bayer4x4(vec2 pixel) {
    int x = int(mod(pixel.x, 4.0));
    int y = int(mod(pixel.y, 4.0));
    int index = y * 4 + x;
    if (index == 0) return 0.0;
    if (index == 1) return 8.0;
    if (index == 2) return 2.0;
    if (index == 3) return 10.0;
    if (index == 4) return 12.0;
    if (index == 5) return 4.0;
    if (index == 6) return 14.0;
    if (index == 7) return 6.0;
    if (index == 8) return 3.0;
    if (index == 9) return 11.0;
    if (index == 10) return 1.0;
    if (index == 11) return 9.0;
    if (index == 12) return 15.0;
    if (index == 13) return 7.0;
    if (index == 14) return 13.0;
    return 5.0;
}

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 color = clamp(texture(sceneTex, uv).rgb, 0.0, 1.0);
    //ai: RGB565 output — 5 red, 6 green, 5 blue levels after ordered dither.
    vec3 levels = vec3(31.0, 63.0, 31.0);
    float threshold = (bayer4x4(gl_FragCoord.xy) + 0.5) / 16.0;
    vec3 dithered = clamp(color + (threshold - 0.5) / levels, 0.0, 1.0);
    vec3 quantized = floor(dithered * levels + 0.5) / levels;
    //ai: horizontal scanlines plus a subtle RGB phosphor triad; no composite
    //   video/colour-bleed path is applied.
    float scan = mod(floor(gl_FragCoord.y), 2.0);
    float scanGain = 1.0 - scan * 0.28;
    float triad = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(1.0);
    if (triad < 1.0) mask = vec3(1.0, 0.88, 0.88);
    else if (triad < 2.0) mask = vec3(0.88, 1.0, 0.88);
    else mask = vec3(0.88, 0.88, 1.0);
    finalColor = vec4(quantized * scanGain * mask, 1.0);
}
