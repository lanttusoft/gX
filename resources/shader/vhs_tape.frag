//opencode: VHS tape effects - dropouts, tracking, generation loss, recording mode, tape type

uniform float tape_wear;
uniform float tape_tracking;
uniform int   tape_generation;
uniform int   tape_recording_mode; // 0=SP, 1=LP, 2=EP
uniform int   tape_type;           // 0=VHS, 1=S-VHS, 2=VHS-C

// Simple noise helper
float tape_rand(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// Apply tape-medium effects to the sample color
vec3 apply_tape_effects(vec2 uv, vec3 color) {
    // --- Dropouts (tape wear) ---
    // Random horizontal bands of static, more frequent with wear
    float dropout_thresh = 1.0 - tape_wear * 0.5;
    float dropout = tape_rand(vec2(uv.y * 1000.0, floor(uv.y * 240.0)));
    if (dropout > dropout_thresh) {
        float severity = (dropout - dropout_thresh) / (1.0 - dropout_thresh);
        vec3 static_color = vec3(tape_rand(uv + 0.5), tape_rand(uv + 1.5), tape_rand(uv + 2.5));
        color = mix(color, static_color, severity * 0.8);
    }

    // --- Tracking error ---
    // Horizontal displacement that varies by scanline
    float track_offset = tape_tracking * 0.03 * sin(uv.y * 3.14159 * 4.0 + time);
    vec2 tracked_uv = vec2(uv.x + track_offset, uv.y);
    // Re-sample at tracked position (only works in vhs.frag, here we just add artifact)
    // The displacement is applied in vhs.frag's main as a UV offset

    // --- Generation loss ---
    // Desaturation proportional to generation count
    float gen_factor = 1.0 / (1.0 + float(tape_generation) * 0.5);
    float gray = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(gray), color, gen_factor);

    // --- Recording mode ---
    // SP=full res, LP=reduced, EP=worst
    // Applied as additional high-frequency noise in EP mode
    if (tape_recording_mode == 2) { // EP
        float ep_noise = tape_rand(uv * 500.0 + floor(time * 60.0)) * 0.08;
        color += ep_noise;
    } else if (tape_recording_mode == 1) { // LP
        float lp_noise = tape_rand(uv * 300.0 + floor(time * 60.0)) * 0.03;
        color += lp_noise;
    }

    // --- Tape type ---
    // S-VHS has better color separation
    if (tape_type == 1) { // S-VHS
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        float chroma = distance(color, vec3(lum));
        color = mix(vec3(lum), color, 1.0 + chroma * 0.2);
    }

    return color;
}
