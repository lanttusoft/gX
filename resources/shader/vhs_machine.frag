//opencode: VHS machine (VCR deck) effects - head wear, motor noise, dirty heads, playback speed

uniform float machine_head_condition;
uniform float machine_motor_noise;
uniform float machine_dirty_heads;
uniform float machine_playback_speed;

float mach_rand(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// Apply VCR deck effects, returns modified UV
vec2 apply_machine_effects(vec2 uv) {
    vec2 result = uv;

    // --- Motor noise: horizontal jitter ---
    // Each scanline shifts randomly based on motor noise
    float jitter = machine_motor_noise * 0.02 * (mach_rand(vec2(uv.y * 500.0, time)) - 0.5);
    result.x += jitter;

    // --- Dirty heads: vertical noise streaks ---
    // Random vertical bands of missing/damaged scanlines
    if (machine_dirty_heads > 0.01) {
        float line = floor(uv.y * 240.0);
        float streak = mach_rand(vec2(line * 0.3, floor(time * 30.0)));
        float thresh = 1.0 - machine_dirty_heads * 0.3;
        if (streak > thresh) {
            // Shift this line left/right randomly (head misalignment)
            float shift = (mach_rand(vec2(line, time * 0.5)) - 0.5) * machine_dirty_heads * 0.04;
            result.x += shift;
            // Could also reduce brightness - handled in apply function
        }
    }

    // --- Head wear: subtle vertical instability ---
    if (machine_head_condition < 0.99) {
        float head_wobble = (1.0 - machine_head_condition) * 0.01 *
            sin(uv.y * 3.14159 * 2.0 + time * 3.0);
        result.x += head_wobble;
    }

    // --- Playback speed: timing distortion ---
    // Fast forward / slow motion affect the sample timing
    // Applied as subtle vertical stretching
    if (abs(machine_playback_speed - 1.0) > 0.01) {
        float speed_factor = machine_playback_speed;
        // In slow-mo, there's more visible noise; in fast-motion, more tearing
        result.y = uv.y / speed_factor;
    }

    return clamp(result, 0.0, 1.0);
}

// Apply machine effects to color (dirty heads = brightness drop, noise)
vec3 apply_machine_color(vec2 uv, vec3 color) {
    // Dirty heads: random brightness loss in streaks
    if (machine_dirty_heads > 0.01) {
        float line = floor(uv.y * 240.0);
        float streak = mach_rand(vec2(line * 0.7, floor(time * 30.0) + 100.0));
        float thresh = 1.0 - machine_dirty_heads * 0.4;
        if (streak > thresh) {
            float dim = 1.0 - (streak - thresh) / (1.0 - thresh) * 0.6;
            color *= dim;
        }
    }

    // Head wear: add noise proportional to wear
    if (machine_head_condition < 0.95) {
        float wear_noise = (1.0 - machine_head_condition) * 0.1 *
            mach_rand(vec2(uv.x * 200.0, uv.y * 240.0 + time));
        color += wear_noise;
    }

    return color;
}
