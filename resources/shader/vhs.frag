#version 330

//opencode: VHS combined shader - includes tape, machine, and format effect components

uniform sampler2D tex0;
uniform float time;
uniform vec2 res;

out vec4 finalColor;

#include "vhs_tape.frag"
#include "vhs_machine.frag"
#include "vhs_format.frag"

void main() {
    vec2 uv = gl_FragCoord.xy / res;

    // 1. Machine effects modify the UV coordinates (jitter, tracking, stretching)
    vec2 machined_uv = apply_machine_effects(uv);

    // Clamp to avoid sampling outside the texture
    machined_uv = clamp(machined_uv, 0.0, 1.0);

    // Sample the input texture
    vec3 color = texture(tex0, machined_uv).rgb;

    // 2. Machine color effects (dirty head dimming, head wear noise)
    color = apply_machine_color(machined_uv, color);

    // 3. Tape effects (dropouts, generation loss, recording mode)
    color = apply_tape_effects(machined_uv, color);

    // 4. Format effects (color bleed, encoding artifacts, Hi-Fi interference)
    color = apply_format_effects(machined_uv, color);

    finalColor = vec4(color, 1.0);
}
