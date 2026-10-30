// Double-double (DD48) emulated precision library for fractal rendering
// Provides ~48 bits of precision using pairs of 32-bit floats (Dekker's algorithm)
// All functions guarded by #if FRACTAL_PRECISION == 1

// --- Complex exponent (shared between 32-bit and DD paths) ---
vec2 cexp(vec2 z, float p) { 
    return pow(length(z), p) * vec2(cos(p * atan(z.y, z.x)), sin(p * atan(z.y, z.x)));
}

// --- Double-double scalar arithmetic (Dekker's algorithm) ---

// Magic constant for splitting: 2^12 + 1
// Splits a 32-bit float into two 12-bit-exponent + 12-bit-mantissa parts
#define DD_SPLIT 4097.0

// Quick two-sum: s = a + b, e = error term (requires |a| >= |b|)
vec2 dd_quick_two_sum(float a, float b) {
    float s = a + b;
    float e = b - (s - a);
    return vec2(s, e);
}

// Two-sum: s = a + b, e = error term (general case)
vec2 dd_two_sum(float a, float b) {
    float s = a + b;
    float v = s - a;
    float e = (a - (s - v)) + (b - v);
    return vec2(s, e);
}

// Split a float into hi (upper 12 bits) and lo (lower 12 bits)
vec2 dd_split(float a) {
    float t = DD_SPLIT * a;
    float hi = t - (t - a);
    float lo = a - hi;
    return vec2(hi, lo);
}

// Double-double addition: a + b
// a, b are vec2 where x = hi, y = lo
vec2 dd_add(vec2 a, vec2 b) {
    vec2 s = dd_two_sum(a.x, b.x);
    float v = s.x - a.x;
    float e = (a.x - (s.x - v)) + (b.x - v) + a.y + b.y;
    return dd_two_sum(s.x, e);
}

// Double-double subtraction: a - b
vec2 dd_sub(vec2 a, vec2 b) {
    return dd_add(a, vec2(-b.x, -b.y));
}

// Double-double multiplication: a * b
vec2 dd_mul(vec2 a, vec2 b) {
    float p = a.x * b.x;
    vec2 a_s = dd_split(a.x);
    vec2 b_s = dd_split(b.x);
    float e1 = a_s.x * b_s.y + a_s.y * b_s.x + a_s.y * b_s.y;
    float e2 = p - a_s.x * b_s.x - e1;
    float e = e1 - e2 + a.x * b.y + a.y * b.x;
    return dd_two_sum(p, e);
}

// Double-double multiply by scalar float: a * s
vec2 dd_mul_f(vec2 a, float s) {
    // For exact multiplication by float, just multiply both parts
    return vec2(a.x * s, a.y * s);
}

// Double-double square: a^2 (optimized)
vec2 dd_sqr(vec2 a) {
    float p = a.x * a.x;
    vec2 a_s = dd_split(a.x);
    float e = 2.0 * a_s.x * a_s.y + a_s.y * a_s.y;
    e = e - ((p - a_s.x * a_s.x) - 2.0 * a_s.x * a_s.y - a_s.y * a_s.y);
    e = e + 2.0 * a.x * a.y;
    return dd_two_sum(p, e);
}

// Double-double absolute value approximation (|a|)
float dd_abs(vec2 a) {
    return abs(a.x);
}

// --- Double-double complex arithmetic ---
// Complex numbers stored as vec4:
//   xy = hi part (re, im)
//   zw = lo part (re, im)
// Total precision: ~48 bits per component

// Create dd complex from a regular vec2
vec4 dd_c_from_vec2(vec2 v) {
    return vec4(v.x, v.y, 0.0, 0.0);
}

// Extract real part as dd (vec2)
vec2 dd_c_re(vec4 a) {
    return vec2(a.x, a.z);
}

// Extract imag part as dd (vec2)
vec2 dd_c_im(vec4 a) {
    return vec2(a.y, a.w);
}

// dd complex add: a + b
vec4 dd_c_add(vec4 a, vec4 b) {
    vec2 re = dd_add(dd_c_re(a), dd_c_re(b));
    vec2 im = dd_add(dd_c_im(a), dd_c_im(b));
    return vec4(re.x, im.x, re.y, im.y);
}

// dd complex multiply by scalar float: a * s
vec4 dd_c_mul_f(vec4 a, float s) {
    return vec4(a.x * s, a.y * s, a.z * s, a.w * s);
}

// dd complex square: a^2
// (re + im*i)^2 = (re^2 - im^2) + (2*re*im)*i
vec4 dd_c_sqr(vec4 a) {
    vec2 re = dd_c_re(a);
    vec2 im = dd_c_im(a);
    
    // re^2 - im^2
    vec2 re_sq = dd_sqr(re);
    vec2 im_sq = dd_sqr(im);
    vec2 real_part = dd_sub(re_sq, im_sq);
    
    // 2 * re * im
    vec2 prod = dd_mul(re, im);
    vec2 imag_part = dd_add(prod, prod);
    
    return vec4(real_part.x, imag_part.x, real_part.y, imag_part.y);
}

// dd complex squared length: |a|^2 = re^2 + im^2
vec2 dd_c_norm_sq(vec4 a) {
    vec2 re_sq = dd_sqr(dd_c_re(a));
    vec2 im_sq = dd_sqr(dd_c_im(a));
    return dd_add(re_sq, im_sq);
}

// --- Fractal iteration dispatch functions ---
// Returns float iteration count for coloring (with fractional smooth part)

#if FRACTAL_PRECISION == 1

// High-precision Mandelbrot/Julia iteration (exponent must be 2.0)
float iterate_fractal_dd(vec2 init_z, vec2 c_param, int limit, float esca, int fractal_type, inout float trap_dist) {
    vec2 z0 = init_z;
    vec2 c0 = c_param;
    
    // Convert to dd
    vec4 z = dd_c_from_vec2(z0);
    vec4 c;
    if (fractal_type == 0) {
        c = dd_c_from_vec2(c0);  // Julia: c = seed
    } else {
        c = dd_c_from_vec2(z0);  // Mandelbrot: z = 0, c = coord
        z = vec4(0.0, 0.0, 0.0, 0.0);
    }
    
    // Scale factor (for internal perturbation)
    float sc = scale * scale;
    vec4 c_scaled = dd_c_mul_f(c, sc);
    float inv_sc = 1.0 / sc;
    
    int i;
    for (i = 0; i < limit; i++) {
        // z *= scale
        z = dd_c_mul_f(z, scale);
        // z = z*z + c_scaled
        z = dd_c_add(dd_c_sqr(z), c_scaled);
        // z /= sc
        z = dd_c_mul_f(z, inv_sc);
        
        // Track orbit trap distance
        if (trapMode > 0) {
            float d = length(vec2(z.x, z.y));  // hi-part approximation of distance
            trap_dist = min(trap_dist, d);
        }
        // Check escape: |z| > esca
        vec2 nsq = dd_c_norm_sq(z);
        if (nsq.x > esca * esca) break;
    }
    
    // Return fractional iteration count for smooth coloring
    vec2 nsq = dd_c_norm_sq(z);
    float mag = sqrt(max(nsq.x, 0.0));
    float frac = 1.0 - log2(log(mag) / log(esca));
    return float(i) + frac;
}

#endif // FRACTAL_PRECISION == 1

// --- Standard Mandelbrot/Julia (works for any exponent, with scale perturbation) ---
float iterate_fractal_32(vec2 init_z, vec2 c_param, int limit, float ex, float esca, int fractal_type, inout float trap_dist) {
    vec2 z, c;
    if (fractal_type == 0) {  // Julia
        z = init_z;
        c = c_param;
    } else {                    // Mandelbrot
        z = vec2(0.0);
        c = init_z;
    }
    
    float pow_sc = pow(scale, ex);
    float inv_pow_sc = 1.0 / pow_sc;
    
    int i;
    for (i = 0; i < limit; i++) {
        z *= scale;
        z = cexp(z, ex) + pow_sc * c;
        z *= inv_pow_sc;
        //opencode: track orbit trap distance
        if (trapMode > 0) {
            float d = distance(z, trapPos);
            trap_dist = min(trap_dist, d);
        }
        if (length(z) > esca) break;
    }
    
    if (i < limit) {
        float mag = length(z);
        float frac = 1.0 - log2(log(max(mag, 1.0)) / log(esca));
        return float(i) + clamp(frac, 0.0, 1.0);
    }
    return float(limit);
}

// --- Burning Ship: takes abs of each component before power ---
float iterate_burning_ship_32(vec2 init_z, vec2 c_param, int limit, float ex, float esca, int fractal_type, inout float trap_dist) {
    vec2 z, c;
    if (fractal_type == 0) { z = init_z; c = c_param; }
    else { z = vec2(0.0); c = init_z; }
    
    float pow_sc = pow(scale, ex);
    float inv_pow_sc = 1.0 / pow_sc;
    
    int i;
    for (i = 0; i < limit; i++) {
        z *= scale;
        z = vec2(abs(z.x), abs(z.y));
        z = cexp(z, ex) + pow_sc * c;
        z *= inv_pow_sc;
        if (trapMode > 0) {
            float d = distance(z, trapPos);
            trap_dist = min(trap_dist, d);
        }
        if (length(z) > esca) break;
    }
    
    if (i < limit) {
        float mag = length(z);
        float frac = 1.0 - log2(log(max(mag, 1.0)) / log(esca));
        return float(i) + clamp(frac, 0.0, 1.0);
    }
    return float(limit);
}

// --- Tricorn (Mandelbar): uses conjugate before power ---
float iterate_tricorn_32(vec2 init_z, vec2 c_param, int limit, float ex, float esca, int fractal_type, inout float trap_dist) {
    vec2 z, c;
    if (fractal_type == 0) { z = init_z; c = c_param; }
    else { z = vec2(0.0); c = init_z; }
    
    float pow_sc = pow(scale, ex);
    float inv_pow_sc = 1.0 / pow_sc;
    
    int i;
    for (i = 0; i < limit; i++) {
        z *= scale;
        z = vec2(z.x, -z.y);
        z = cexp(z, ex) + pow_sc * c;
        z *= inv_pow_sc;
        if (trapMode > 0) {
            float d = distance(z, trapPos);
            trap_dist = min(trap_dist, d);
        }
        if (length(z) > esca) break;
    }
    
    if (i < limit) {
        float mag = length(z);
        float frac = 1.0 - log2(log(max(mag, 1.0)) / log(esca));
        return float(i) + clamp(frac, 0.0, 1.0);
    }
    return float(limit);
}

// --- Newton: iterates Newton's method on z^p - 1 = 0 ---
// init_z is the starting guess (screen position), no c parameter used.
// Returns (root_index * 256 + iteration_count) for dual-purpose coloring:
//   - int(result) / 256 = which root was found (0, 1, 2 for p=3)
//   - int(result) % 256 = iterations to converge
//   - fract(result) = smooth fractional convergence
float iterate_newton_32(vec2 init_z, int limit, float ex, inout float trap_dist) {
    vec2 z = init_z;
    int p = int(ex);  // polynomial degree (use integer part of exponent)
    if (p < 2) p = 3;  // default to cubic
    
    int i;
    for (i = 0; i < limit; i++) {
        vec2 f_z = cexp(z, float(p));          // z^p
        f_z -= vec2(1.0, 0.0);                  // z^p - 1
        
        vec2 fp_z = cexp(z, float(p - 1));     // z^(p-1)
        fp_z *= float(p);
        
        // Complex division: f_z / fp_z
        float denom = dot(fp_z, fp_z);
        if (denom < 1.0e-20) break;
        vec2 div = vec2(f_z.x * fp_z.x + f_z.y * fp_z.y,
                        f_z.y * fp_z.x - f_z.x * fp_z.y) / denom;
        
        z -= div;
        
        // Track orbit trap distance
        if (trapMode > 0) {
            float d = distance(z, trapPos);
            trap_dist = min(trap_dist, d);
        }
        
        if (length(f_z) < 1.0e-6) break;
    }
    
    // Determine nearest root
    float bestDist = 1.0e10;
    int root = 0;
    for (int r = 0; r < p; r++) {
        float angle = 2.0 * 3.141592 * float(r) / float(p);
        vec2 rootPos = vec2(cos(angle), sin(angle));
        float d = distance(z, rootPos);
        if (d < bestDist) {
            bestDist = d;
            root = r;
        }
    }
    
    float convergence = 1.0 - clamp(bestDist * 10.0, 0.0, 1.0);
    return float(root * 256 + i) + convergence * 0.5;
}

// --- Phoenix: z = z^2 + c + y where y is the PREVIOUS z value ---
float iterate_phoenix_32(vec2 init_z, vec2 c_param, int limit, float esca, int fractal_type, vec2 y_param, inout float trap_dist) {
    vec2 z, c;
    if (fractal_type == 0) { z = init_z; c = c_param; }
    else { z = vec2(0.0); c = init_z; }
    
    vec2 y = y_param;
    
    int i;
    for (i = 0; i < limit; i++) {
        vec2 prev_z = z;
        z = z * z + c + y;
        y = prev_z;
        if (trapMode > 0) {
            float d = distance(z, trapPos);
            trap_dist = min(trap_dist, d);
        }
        if (length(z) > esca) break;
    }
    
    if (i < limit) {
        float mag = length(z);
        float frac = 1.0 - log2(log(max(mag, 1.0)) / log(esca));
        return float(i) + clamp(frac, 0.0, 1.0);
    }
    return float(limit);
}

// --- Main dispatch: routes to the selected algorithm ---
// FRACTAL_ALGORITHM is defined as a compile-time constant via defnes:
//   0 = standard Mandelbrot/Julia
//   1 = Burning Ship
//   2 = Tricorn (Mandelbar)
//   3 = Newton
//   4 = Phoenix
float iterate_fractal(vec2 init_z, vec2 c_param, int limit, float ex, float esca, int fractal_type, vec2 y_param, inout float trap_dist) {
#if FRACTAL_ALGORITHM == 3
    return iterate_newton_32(init_z, limit, ex, trap_dist);
#elif FRACTAL_ALGORITHM == 4
    return iterate_phoenix_32(init_z, c_param, limit, esca, fractal_type, y_param, trap_dist);
#else
    #if FRACTAL_PRECISION == 1
    if (FRACTAL_ALGORITHM == 0 && abs(ex - 2.0) < 0.001) {
        return iterate_fractal_dd(init_z, c_param, limit, esca, fractal_type, trap_dist);
    }
    #endif
    
    #if FRACTAL_ALGORITHM == 1
        return iterate_burning_ship_32(init_z, c_param, limit, ex, esca, fractal_type, trap_dist);
    #elif FRACTAL_ALGORITHM == 2
        return iterate_tricorn_32(init_z, c_param, limit, ex, esca, fractal_type, trap_dist);
    #else
        return iterate_fractal_32(init_z, c_param, limit, ex, esca, fractal_type, trap_dist);
    #endif
#endif
}

// --- Orbit trap coloring: map trap distance to a color ---
vec3 trap_color(float dist, float radius) {
    if (trapMode <= 0) return vec3(0.0);
    // Map distance to color: closer = brighter (orange glow)
    float t = clamp(dist / (radius * 0.5), 0.0, 1.0);
    return (1.0 - t) * vec3(1.0, 0.6, 0.1);
}

//opencode: — Mode 0 — Original sinusoid palette with posterization (default)
vec3 palette_mode_sinusoid(float iter, int palette_amount, vec3 fac, vec3 spd, vec3 pha, vec3 amp, float time) {
    const float pi = 3.141592;
    vec3 color;
    color.x = amp.x * 0.5 * (sin(2.0 * pi * (fac.x * iter + 1.0/3.0 + spd.x * time - pha.x)) + 1.0);
    color.y = amp.y * 0.5 * (sin(2.0 * pi * (fac.y * iter + 2.0/3.0 + spd.y * time - pha.y)) + 1.0);
    color.z = amp.z * 0.5 * (sin(2.0 * pi * (fac.z * iter + 3.0/3.0 + spd.z * time - pha.z)) + 1.0);
    color = floor(float(palette_amount) * color) / float(palette_amount);
    return color;
}

//opencode: — Mode 1 — Raw sinusoid (no posterization)
vec3 palette_mode_raw(float iter, vec3 fac, vec3 spd, vec3 pha, vec3 amp, float time) {
    const float pi = 3.141592;
    vec3 color;
    color.x = amp.x * 0.5 * (sin(2.0 * pi * (fac.x * iter + 1.0/3.0 + spd.x * time - pha.x)) + 1.0);
    color.y = amp.y * 0.5 * (sin(2.0 * pi * (fac.y * iter + 2.0/3.0 + spd.y * time - pha.y)) + 1.0);
    color.z = amp.z * 0.5 * (sin(2.0 * pi * (fac.z * iter + 3.0/3.0 + spd.z * time - pha.z)) + 1.0);
    return color;
}

//opencode: — Mode 2 — Heatmap: black → red → orange → yellow → white
vec3 palette_mode_heatmap(float iter, int limit, float time) {
    float t = iter / float(limit);
    // Black body radiation approximation
    vec3 color;
    color.r = min(1.0, t * 4.0);
    color.g = min(1.0, max(0.0, (t - 0.25) * 4.0));
    color.b = min(1.0, max(0.0, (t - 0.5) * 4.0));
    return color;
}

//opencode: — Mode 3 — Rainbow: pure hue cycling through iteration count
vec3 palette_mode_rainbow(float iter, float time) {
    const float pi = 3.141592;
    float hue = fract(iter * 0.05 + time * 0.05);
    // HSV-to-RGB for hue
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

//opencode: — Mode 4 — HSV independent: separate hue shift, sat mult, val mult
vec3 palette_mode_hsv(float iter, vec3 fac, vec3 spd, vec3 pha, vec3 amp, float time) {
    const float pi = 3.141592;
    // Phase per channel: R= hue shift, G= saturation, B= value
    float hueShift  = amp.x * 0.5 * (sin(2.0 * pi * (fac.x * iter + spd.x * time - pha.x)) + 1.0);
    float satMult   = amp.y * 0.5 * (sin(2.0 * pi * (fac.y * iter + spd.y * time - pha.y)) + 1.0);
    float valMult   = amp.z * 0.5 * (sin(2.0 * pi * (fac.z * iter + spd.z * time - pha.z)) + 1.0);
    
    float hue = fract(iter * 0.01 + hueShift);
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    vec3 rgb = clamp(vec3(r, g, b), 0.0, 1.0);
    
    // Apply saturation and value
    float lum = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(lum), rgb, satMult);
    rgb *= valMult;
    return rgb;
}

//opencode: — Mode 5 — Grayscale: iteration count mapped to gray
vec3 palette_mode_grayscale(float iter, int limit) {
    float t = iter / float(limit);
    return vec3(t);
}

//opencode: — Palette dispatch — routes to the selected mode
vec3 palette_color(float iter, int mode, int limit, int palette_amount, vec3 fac, vec3 spd, vec3 pha, vec3 amp, float time) {
    if (mode == 1) return palette_mode_raw(iter, fac, spd, pha, amp, time);
    if (mode == 2) return palette_mode_heatmap(iter, limit, time);
    if (mode == 3) return palette_mode_rainbow(iter, time);
    if (mode == 4) return palette_mode_hsv(iter, fac, spd, pha, amp, time);
    if (mode == 5) return palette_mode_grayscale(iter, limit);
    // Default: mode 0 — sinusoid with posterization
    return palette_mode_sinusoid(iter, palette_amount, fac, spd, pha, amp, time);
}
