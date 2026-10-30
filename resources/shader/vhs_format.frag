//opencode: VHS format effects - color encoding, bleed, Hi-Fi audio interference

uniform int   format_encoding; // 0=NTSC, 1=PAL, 2=SECAM
uniform float format_bleed;
uniform int   format_hifi_audio;

float fmt_rand(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// Apply format/encoding-specific color artifacts
vec3 apply_format_effects(vec2 uv, vec3 color) {
    // --- Color bleed: chroma smearing ---
    // Horizontal chroma offset proportional to bleed amount
    if (format_bleed > 0.01) {
        float bleed_px = format_bleed * 8.0;
        vec2 bleed_uv = vec2(uv.x + bleed_px / 640.0, uv.y);
        vec3 bled_color = texture(tex0, bleed_uv).rgb;

        // Only bleed the chroma, keep luma sharp
        float luma = dot(color, vec3(0.299, 0.587, 0.114));
        float bled_luma = dot(bled_color, vec3(0.299, 0.587, 0.114));
        vec3 chroma = color - luma;
        vec3 bled_chroma = bled_color - bled_luma;
        chroma = mix(chroma, bled_chroma, format_bleed * 0.5);
        color = luma + chroma;
    }

    // --- Encoding-specific artifacts ---
    if (format_encoding == 0) { // NTSC
        // Dot crawl: moving checkerboard pattern on saturated colors
        float crawl = sin(uv.x * 640.0 * 3.14159 * 0.5 + time * 4.0) *
                      cos(uv.y * 480.0 * 3.14159 * 0.25);
        float saturation = abs(dot(color, vec3(0.299, 0.587, 0.114)) - color.g);
        color += vec3(crawl * saturation * 0.05);

        // Chroma offset: slight mis-convergence
        float chroma_shift = sin(uv.y * 480.0 * 3.14159 * 0.1) * 0.005;
        vec2 chroma_uv = vec2(uv.x + chroma_shift, uv.y);
        vec3 chroma_color = texture(tex0, chroma_uv).rgb;
        float luma = dot(color, vec3(0.299, 0.587, 0.114));
        color = luma + (chroma_color - dot(chroma_color, vec3(0.299, 0.587, 0.114)));

    } else if (format_encoding == 1) { // PAL
        // Hanover bars: alternating line phase flip
        float line = floor(uv.y * 480.0);
        if (mod(line, 2.0) < 0.5) {
            // Every other line has slightly shifted hue
            float luma = dot(color, vec3(0.299, 0.587, 0.114));
            vec3 chroma = color - luma;
            chroma.yz *= -1.0; // V component inversion
            color = luma + chroma * 0.97; // Slight averaging
        }

    } else if (format_encoding == 2) { // SECAM
        // SECAM: frequency-modulated color with different noise
        float line = floor(uv.y * 480.0);
        float mod_signal = mod(line, 2.0);
        if (mod_signal < 0.5) {
            // Dr (R-Y) line: red emphasis
            float noise = (fmt_rand(vec2(uv.x * 100.0, uv.y * 240.0)) - 0.5) * 0.06;
            color.r += noise;
        } else {
            // Db (B-Y) line: blue emphasis
            float noise = (fmt_rand(vec2(uv.x * 100.0 + 50.0, uv.y * 240.0)) - 0.5) * 0.06;
            color.b += noise;
        }
    }

    // --- Hi-Fi audio interference ---
    // Thin horizontal lines caused by the Hi-Fi audio heads
    if (format_hifi_audio != 0) {
        float line = uv.y * 480.0;
        float audio_lines = sin(line * 3.14159 * 0.5 + time * 2.0);
        float interference = smoothstep(0.98, 1.0, abs(audio_lines));
        color -= interference * 0.1;

        // Audio tracking noise at very specific intervals
        float audio_track = sin(line * 3.14159 * 0.25) * 0.02;
        color += audio_track;
    }

    // --- S-VHS cross-color rejection (inverse of NTSC dot crawl) ---
    // If type is S-VHS, reduce cross-color artifacts
    // (controlled by tape_type uniform from vhs_tape.frag)

    return clamp(color, 0.0, 1.0);
}
