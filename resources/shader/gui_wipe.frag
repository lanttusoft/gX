#version 330 core

//opencode: v0.3.8 — GUI transition wipe overlay ("menu personality" pack).
//   Masks a flat color quad over the composited canvases. style selects the
//   sweep shape, progress sweeps 0→1 (out) / 1→0 (in):
//     0 = diagonal wipe, 1 = scanline curtain, 2 = iris
//   Covered fragments output the transition end color; uncovered are
//   transparent so the scene below shows through.

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform int style;
uniform float progress;
uniform vec2 res;
uniform vec4 color;

void main() {
    //opencode: uv from gl_FragCoord — the raw overlay quad has no texcoords
    vec2 uv = gl_FragCoord.xy / res;
    float mask = 0.0;
    float d = (uv.x + uv.y) * 0.5;
    mask = step(d, 0.5); //opencode: TEMP hardcoded
    finalColor = vec4(progress, 0.0, 0.0, 1.0); //opencode: TEMP progress-as-color
}