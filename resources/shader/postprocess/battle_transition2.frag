#version 330
//opencode: retro JRPG-style battle transition - screen shrinks and rotates with a feedback trail
//   Phase 1 (0.0->0.5): Screen scales down toward center while rotating, feedback builds
//   Phase 2 (0.5->0.7): Bright flash, max rotation, max shrink
//   Phase 3 (0.7->1.0): Image restores, rotation unwinds
//
//   uniforms[0].x = transition progress (0.0 = start, 1.0 = done)
//   uniforms[0].y = feedback blend factor
//   time = elapsed time
//   res = render resolution

in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = vec2(1024,768);
out vec4 finalColor;

void main() {
    vec2 uv = gl_FragCoord.xy / res;
    float t = uniforms[0].x;

    //opencode: Phase curves
    float shrinkPhase = smoothstep(0.0, 0.5, t) * (1.0 - smoothstep(0.7, 1.0, t));
    float restorePhase = smoothstep(0.7, 1.0, t);

    //opencode: scale - 1.0 -> 0.25 -> 1.0
    float scale = 1.0 - shrinkPhase * 0.75;
    scale = mix(scale, 1.0, restorePhase);

    //opencode: rotation - 0 -> 180? -> 0 (in radians)
    float angle = shrinkPhase * 3.14159;
    angle = mix(angle, 0.0, restorePhase);

    //opencode: center UV and apply rotation + scale
    vec2 centered = uv - 0.5;
    float cosA = cos(angle);
    float sinA = sin(angle);
    vec2 rotated;
    rotated.x = centered.x * cosA - centered.y * sinA;
    rotated.y = centered.x * sinA + centered.y * cosA;
    vec2 transformed = rotated / scale + 0.5;

    //opencode: sample the transformed UV
    vec4 color = texture(input, transformed);

    //opencode: darken edges as we shrink (vignette)
    float dist = length(centered);
    float vignette = 1.0 - smoothstep(0.0, 0.7, dist) * shrinkPhase * 0.6;
    color.rgb *= vignette;

    //opencode: feedback trail - blend with previous frame (already in input via PP feedback)
    //   The feedback gets stronger as we shrink, creating a motion-blur trail
    float feedbackBlend = shrinkPhase * 0.4;
    vec4 prev = texture(input, uv);
    color = mix(color, prev, feedbackBlend);

    //opencode: bright flash at midpoint (t ? 0.5)
    float flash = exp(-pow((t - 0.5) * 10.0, 2.0));
    color.rgb += flash * vec3(1.0, 0.95, 0.9);

    //opencode: RGB separation during shrink phase
    float sep = shrinkPhase * 0.01;
    float r = texture(input, transformed + vec2(sep, 0.0)).r;
    float b = texture(input, transformed - vec2(sep, 0.0)).b;
    color.r = mix(color.r, r, shrinkPhase * 0.5);
    color.b = mix(color.b, b, shrinkPhase * 0.5);

    finalColor = color;
}
