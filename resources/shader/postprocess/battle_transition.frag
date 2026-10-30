#version 330
//opencode: retro JRPG-style battle transition shader
//   Time-based animation with RGB separation, horizontal sweep,
//   bright flash, and distortion. Designed for the PP feedback loop
//   where the previous frame's output bleeds into the current frame.
//
//   uniforms[0].x = transition progress (0.0 = start, 1.0 = done)
//   uniforms[0].y = feedback blend (0.0 = no feedback, 1.0 = full)
//   time = elapsed time (for animated noise/effects)
//   res = render resolution

in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = vec2(1024,768);
out vec4 finalColor;

//opencode: pseudo-random hash for noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = gl_FragCoord.xy / res;
    float t = uniforms[0].x;        // transition progress 0->1
    float fb = uniforms[0].y;       // feedback blend factor

    //opencode: Phase 1 - RGB separation ramps up (t: 0.0->0.3)
    float sepAmount = smoothstep(0.0, 0.3, t) * (1.0 - smoothstep(0.7, 1.0, t));
    float sepStrength = sepAmount * 0.015;

    //opencode: RGB channels shift horizontally, opposite directions
    float r = texture(input, uv + vec2(sepStrength, 0.0)).r;
    float g = texture(input, uv).g;
    float b = texture(input, uv - vec2(sepStrength, 0.0)).b;

    //opencode: Phase 2 - horizontal sweep line (t: 0.2->0.6)
    float sweepPos = smoothstep(0.2, 0.6, t);  // 0->1 as sweep moves down
    float sweepLine = 1.0 - abs(uv.y - sweepPos) * 20.0;
    sweepLine = clamp(sweepLine, 0.0, 1.0);
    sweepLine = pow(sweepLine, 4.0);  // sharp line

    //opencode: bright flash at midpoint (t ? 0.5)
    float flash = exp(-pow((t - 0.5) * 12.0, 2.0));

    //opencode: horizontal distortion wave (t: 0.1->0.7)
    float warpAmount = smoothstep(0.1, 0.4, t) * (1.0 - smoothstep(0.6, 0.8, t));
    float warp = sin(uv.y * 80.0 + time * 5.0) * warpAmount * 0.01;
    vec2 warpedUv = uv + vec2(warp, 0.0);
    vec4 warpedCol = texture(input, warpedUv);

    //opencode: vertical compression (t: 0.3->0.7) - screen squishes
    float squash = 1.0 - smoothstep(0.3, 0.6, t) * 0.3;
    vec2 squashedUv = (uv - 0.5) / squash + 0.5;
    vec4 squashedCol = texture(input, squashedUv);

    //opencode: combine effects based on phase
    vec4 color;
    if (t < 0.7) {
        //opencode: transition phase - use warped + separated color
        color = vec4(r, g, b, 1.0);
        //opencode: blend in warped version for distortion
        color = mix(color, warpedCol, warpAmount * 0.5);
        //opencode: blend in squashed version for compression
        color = mix(color, squashedCol, smoothstep(0.3, 0.6, t) * 0.4);
    } else {
        //opencode: restore phase - fade back to normal
        float restore = smoothstep(0.7, 1.0, t);
        vec4 normal = texture(input, uv);
        color = mix(vec4(r, g, b, 1.0), normal, restore);
    }

    //opencode: add sweep line (bright white)
    color.rgb += sweepLine * vec3(0.8, 0.8, 1.0);

    //opencode: add flash (white burst)
    color.rgb += flash * vec3(1.0, 0.95, 0.8);

    //opencode: feedback blend - previous frame bleeds through
    //   (the PP system already composites feedback into the input,
    //    so this is a secondary blend for the shader's own feedback)
    if (fb > 0.0) {
        vec4 prev = texture(input, uv);
        color = mix(color, prev, fb * 0.3);
    }

    finalColor = color;
}
