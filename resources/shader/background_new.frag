//opencode: dual-layer background shader — mat4 transform + 8 sinusoid modes + HDMA + 2 texture layers + blend + color math

#version 330

uniform float time =            0.0;
uniform vec2 res =              vec2(1024,768);
//ai: camera ray inputs enable the Battle's spherical projection backdrop.
uniform int battleViewEnabled = 0;
uniform vec3 battleCameraPosition = vec3(0.0,4.8,10.5);
uniform vec3 battleCameraTarget = vec3(0.0,0.5,0.0);
uniform vec3 battleCameraUp = vec3(0.0,1.0,0.0);
uniform float battleCameraFov = 40.0;
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;          //opencode: second layer texture
uniform vec2 tex0res =          vec2(1024,768);
uniform vec2 tex2res =          vec2(1024,768); //opencode: second layer resolution

uniform mat4 transform;

uniform vec4 sinusoidAmp[8];
uniform vec4 sinusoidFre[8];
uniform vec4 sinusoidPha[8];
uniform vec4 sinusoidSpd[8];
uniform vec4 sinusoidRwe[8];
uniform vec4 sinusoidIle[8];
uniform vec4 sinusoidIlm[8];
uniform float sinusoidExp[8];
uniform float sinusoidAtt[8];
uniform vec2 sinusoidCen[8]; //opencode: sinusoid center point
uniform vec4 sinusoidDcO[8]; //opencode: sinusoid DC offset
uniform vec4 sinusoidLin[8]; //opencode: sinusoid linear coefficient
uniform vec4 sinusoidQuad[8]; //opencode: sinusoid quadratic coefficient

//opencode: HDMA scanline wave — per-scanline distortion (SNES HDMA-style)
uniform int hdma_enable = 0;
uniform float hdmaAmp[8];
uniform float hdmaFre[8];
uniform float hdmaSpd[8];
uniform float hdmaPha[8];
uniform vec2 hdmaOff[8];

//transform components (individual values for origin offsets + post-divide translate)
uniform int param_enable =    1;
uniform float scale =         1.0;
uniform vec2 scaleOrigin =    vec2(0.0,0.0);
uniform vec2 rotateOrigin =   vec2(0.0,0.0);
uniform vec2 perspective =    vec2(0.0,0.0);
uniform vec2 translate =      vec2(0.0,0.0);

//opencode: procedural noise (0=off 1=simplex 2=fractal 3=voronoi 4=turbulence)
uniform int noiseMode = 0;
uniform float noiseScale = 1.0;
uniform float noiseSpeed = 0.0;
uniform int noiseOctaves = 4;
uniform float noiseLacunarity = 2.0;
uniform float noiseGain = 0.5;

//opencode: game-event reaction (0=none 1=pulse 2=shake 3=flash 4=warp)
uniform int reactType = 0;
uniform float reactIntensity = 0.0; // 0..1, auto-decays on C side

//opencode: second layer enabled (0=layer2 fully transparent, 1=active)
uniform int layer2Enabled = 0;

//opencode: dual-layer blend mode (0=alpha 1=add 2=subtract 3=multiply 4=screen 5=difference 6=overlay 7=dodge)
uniform int blendMode = 0;
//opencode: SNES-style color math (0=off 1=add 2=subtract 3=half_add)
uniform int colorMathMode = 0;
uniform vec3 colorMathMask = vec3(1.0,1.0,1.0); // per-channel enable

//palette
uniform int palette_mode =      0;
uniform int palette_size = 256;
uniform int palette_rowCount = 16;
uniform vec4 colors[256];
uniform float palette_speed =   0.1;
uniform int palette_amount =    16;
uniform float palette_freq =    1.0;
uniform vec4 color1 =           vec4(0.0,1.0,0.0,1.0);
uniform vec4 color2 =           vec4(0.0,0.0,1.0,1.0);
uniform float bg_boundary =     0.1;
uniform float fg_boundary =     0.9;
uniform vec4 color_bg =         vec4(0.0,0.0,0.0,1.0);
uniform vec4 color_fg =         vec4(1.0,1.0,1.0,1.0);

//palette enable
uniform int palette_enable =    1;

//misc
uniform int boundaryXY_enable = 1;
uniform int boundaryZ_enable  = 1;
uniform vec4 boundaryXY =       vec4(-0.500,+0.500,-0.500,+0.500);
uniform vec2 boundaryZ =        vec2(-0.025,10.000);
uniform int modulo_enable =     1;
uniform vec2 modulo =           vec2(1.0,1.0);

out vec4 finalColor;

float asp = res.x/res.y;
float tex0asp = tex0res.x/tex0res.y;
float tex2asp = tex2res.x/tex2res.y; //opencode: second layer aspect ratio
const float pi = 3.14159265358979323846;

//opencode: added quadratic term — quad*coord4² outside sin
vec4 compute_wave(vec4 amp, vec4 fre, vec4 pha, vec4 spd, vec4 rwe, vec4 ile, vec4 ilm, vec4 dc, vec4 lin, vec4 quad, vec3 coord) {
    vec4 coord4 = vec4(coord.x, coord.y, coord.x, coord.y);
    vec4 res4 = vec4(res.x, res.y, res.x, res.y);
    vec4 waving = rwe;
    if (fre.x != 0.0) waving.x += ile.x*res4.x/(ilm.x*fre.x);
    if (fre.y != 0.0) waving.y += ile.y*res4.y/(ilm.y*fre.y);
    if (fre.z != 0.0) waving.z += ile.z*res4.z/(ilm.z*fre.z);
    if (fre.w != 0.0) waving.w += ile.w*res4.w/(ilm.w*fre.w);
    vec4 offs = spd*time + pha;
    return quad*coord4*coord4 + lin*coord4 + amp*sin(((fre*coord4*waving) + offs)*2.0*pi) + dc;
}

//opencode: analytically-continued pow — sign(x)*|x|^y, no NaN for negative x
float ac_pow(float x, float y) {
    return sign(x) * pow(abs(x) + 0.000001, y);
}

//opencode: modes 0-5 XY transforms, 6-7 Z transforms, center+exponent for all
// NOTE: parameter named 'expVal' not 'exp' to avoid shadowing built-in exp() function
void apply_sinusoid(in vec4 wave, inout vec3 coord, int mode, float expVal, float atten, vec2 center) {
    //0=linearTranslate 1=linearScale 2=linearRotate
    //3=radialTranslate 4=radialScale 5=radialRotate
    //6=linearZ 7=radialZ
    if (mode == 0) {
        coord.xy += vec2(wave.x + wave.y, wave.z + wave.w) * expVal;
    } else if (mode == 1) {
        vec2 base = vec2(1.0 + wave.x + wave.y, 1.0 + wave.z + wave.w);
        vec2 sf = vec2(ac_pow(base.x, expVal), ac_pow(base.y, expVal));
        coord.xy = center + (coord.xy - center) * sf;
    } else if (mode == 2) {
        float ang = (wave.x + wave.y) * expVal;
        float c = cos(ang);
        float s = sin(ang);
        coord.xy = center + mat2(c, -s, s, c) * (coord.xy - center);
    } else if (mode == 3) {
        vec2 cent = coord.xy - center;
        float dist = length(cent);
        float decay = exp(-dist * atten);
        float p = ac_pow(dist, expVal);
        coord.xy += vec2(wave.x + wave.y, wave.z + wave.w) * p * decay;
    } else if (mode == 4) {
        vec2 cent = coord.xy - center;
        float dist = length(cent);
        float decay = exp(-dist * atten);
        float ang = atan(cent.y, cent.x);
        float p = ac_pow(dist, expVal);
        dist *= (1.0 + wave.x * p * decay);
        coord.xy = center + dist*vec2(cos(ang), sin(ang));
    } else if (mode == 5) {
        vec2 cent = coord.xy - center;
        float dist = length(cent);
        float decay = exp(-dist * atten);
        float ang = atan(cent.y, cent.x);
        float p = ac_pow(dist, expVal);
        ang += wave.x * p * decay;
        coord.xy = center + dist*vec2(cos(ang), sin(ang));
    } else if (mode == 6) {
        coord.z += (wave.x + wave.y + wave.z + wave.w) * expVal;
    } else if (mode == 7) {
        vec2 cent = coord.xy - center;
        float dist = length(cent);
        float decay = exp(-dist * atten);
        float p = ac_pow(dist, expVal);
        coord.z += (wave.x + wave.y + wave.z + wave.w) * p * decay;
    }
}

void palette_mode0(inout vec4 color) {
    float avg = color.x;
    int factor = int(min(palette_size,128));
    color = colors[1+((int(factor*avg+6*time))%(factor-1))];
}

void palette_mode1(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    float cycle = 0.5*(sin(2.0*pi*(palette_freq*avg+palette_speed*time))+1.0);
    color = mix(color1,color2,cycle);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount)*color)/float(palette_amount);
}

//opencode: palette mode 2 — indexed color lookup via ImageProcessor
//   tex0 = grayscale index texture, tex1 = 1×N palette texture
//   Gets pixel coord, samples tex0 for index, looks up tex1
//   Note: when ImageProcessor is active, tex0 binding is overridden to index texture
void palette_mode2(inout vec4 color) {
    float idx = color.r; // grayscale index from tex0 sample
    color = texture(tex1, vec2(idx, 0.0)).rgba;
}

//opencode: palette mode 3 — rainbow/hue cycle
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
void palette_mode3(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    //ai: discrete CGRAM-style hue rotation (SNES palette steps) — the
    //   continuous cycle made headless captures phase-sensitive frame to
    //   frame; stepping also matches the hardware palette rotation.
    float hueStep = floor(palette_speed * time * 8.0) / 8.0;
    float hue = fract(palette_freq * avg + hueStep);
    float sat = 0.8 + 0.2 * sin(avg * 10.0 + time);
    float val = avg;
    color.rgb = hsv2rgb(vec3(hue, sat, val));
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount) * color) / float(palette_amount);
}

//opencode: palette mode 4 — posterize + ordered dither (4x4 Bayer)
const float bayer4x4[16] = float[16](
    0.0000, 0.5000, 0.1250, 0.6250,
    0.7500, 0.2500, 0.8750, 0.3750,
    0.1875, 0.6875, 0.0625, 0.5625,
    0.9375, 0.4375, 0.8125, 0.3125
);
void palette_mode4(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    int n = max(palette_size, 2);
    float levels = float(n);
    ivec2 frag = ivec2(gl_FragCoord.xy) % 4;
    float dither = bayer4x4[frag.y * 4 + frag.x] / levels;
    float quant = floor((avg + dither) * (levels - 1.0) + 0.5) / (levels - 1.0);
    color.rgb *= quant / max(avg, 0.001);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
}

//opencode: palette mode 5 — heatmap gradient (N-stop from colors[16])
void palette_mode5(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    int n = max(min(palette_size, 16), 2);
    float t = clamp(avg, 0.0, 1.0) * float(n - 1);
    int idx = int(floor(t));
    float frac = t - float(idx);
    idx = clamp(idx, 0, n - 2);
    vec4 c0 = colors[idx];
    vec4 c1 = colors[idx + 1];
    color = mix(c0, c1, frac);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
}

//opencode: palette mode 6 — HSV independent (color1 = {hueShift,satMult,valMult,speed}, color2 = {hueFreq,satFreq,valFreq,0})
void palette_mode6(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    float hShift = color1.x * sin(2.0 * pi * (color2.x * avg + color1.w * time));
    float sMult = 1.0 + color1.y * sin(2.0 * pi * (color2.y * avg + color1.w * time * 0.7));
    float vMult = 1.0 + color1.z * sin(2.0 * pi * (color2.z * avg + color1.w * time * 0.5));
    // Approximate HSV transform: shift hue via rotation in UV plane of YUV-like space
    vec3 hsv = vec3(0.0, 0.0, avg);
    // Simple hue rotation using RYGCBM wheel approximation
    float r = color.r, g = color.g, b = color.b;
    float u = -0.147 * r - 0.289 * g + 0.436 * b;
    float v = 0.615 * r - 0.515 * g - 0.100 * b;
    float ca = cos(hShift * 2.0 * pi);
    float sa = sin(hShift * 2.0 * pi);
    float u2 = ca * u - sa * v;
    float v2 = sa * u + ca * v;
    color.r = clamp(r + u2 - u, 0.0, 1.0);
    color.g = clamp(g + v2 - v, 0.0, 1.0);
    color.b = avg;
    // Apply sat/val
    color.rgb = avg + sMult * (color.rgb - avg);
    color.rgb *= vMult / max(avg, 0.001);
    color = clamp(color, 0.0, 1.0);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount) * color) / float(palette_amount);
}

//ai: palette mode 7 — SNES-style row-cycling indexed lookup, generalized to
//   any decomposed palette size: nRows = palette_rowCount (clamped to n),
//   nCols = n/nRows. Texel-center lookups (the old /255.0 mapping sampled the
//   last entry as the first on repeat-wrapped textures). At the default 256
//   colors this is the 16x16 CGRAM layout and matches Mother 2's per-row
//   palette animation.
void palette_mode7(inout vec4 color) {
    float avg = (color.x + color.y + color.z) / 3.0;
    int n = max(palette_size, 2);
    int nRows = clamp(palette_rowCount, 1, n);
    int nCols = max(n / nRows, 1);
    float fRow = avg * float(nRows);
    int row = clamp(int(floor(fRow)), 0, nRows - 1);
    float intraRow = fRow - float(row);
    int col = clamp(int(intraRow * float(nCols)), 0, nCols - 1);
    int cycle = int(mod(time * palette_speed * 8.0, float(nCols)));
    col = (col + cycle) % nCols;
    float idx = (float(row * nCols + col) + 0.5) / float(n);
    color = texture(tex1, vec2(idx, 0.0)).rgba;
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount) * color) / float(palette_amount);
}

//opencode: HDMA scanline wave — per-scanline distortion using gl_FragCoord.y
//   Each channel produces a sine wave across the screen height. The wave
//   offsets UV coords in the direction of hdmaOff[i]. Multiple channels
//   stack for complex distortion (mimics SNES HDMA multi-channel effects).
//   Applied after perspective divide, before texture sampling.
void apply_hdma(inout vec3 coord) {
    float scanline = gl_FragCoord.y;
    vec2 totalOffset = vec2(0.0);
    for (int i = 0; i < 8; i++) {
        float wave = sin(scanline * hdmaFre[i] + hdmaPha[i] + time * hdmaSpd[i]);
        totalOffset += hdmaOff[i] * wave * hdmaAmp[i];
    }
    coord.xy += totalOffset;
}

//opencode: game-event reaction — modulates coordinate or color from reactType
//   Applied after perspective divide (PULSE/SHAKE/WARP distort sampling coords,
//   FLASH tints the final color in main()).
void apply_react(inout vec3 coord) {
    float ri = reactIntensity;
    if (reactType == 1) {
        // PULSE: radial scale boost from center
        float scale = 1.0 + ri * 0.15;
        coord.xy = vec2(0.0) + (coord.xy - vec2(0.0)) * scale;
    } else if (reactType == 2) {
        // SHAKE: random jitter (hash by fragcoord + time)
        float h1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + time * 50.0) * 43758.5453);
        float h2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.346, 11.135)) + time * 50.0) * 43758.5453);
        coord.xy += (vec2(h1, h2) - 0.5) * ri * 0.05;
    } else if (reactType == 4) {
        // WARP: perspective z-bend (lens distortion)
        coord.z += ri * 0.5 * length(coord.xy);
    }
}

//opencode: procedural noise library — simplex 2D, FBM, voronoi, turbulence
vec3 noise_permute(vec3 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

float noise_simplex(vec2 p) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(p + dot(p, C.yy));
    vec2 x0 = p - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p0 = noise_permute(i.y + vec3(0.0, i1.y, 1.0));
    p0 = noise_permute(p0 + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p0 * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float noise_hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise_fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        value += amplitude * noise_simplex(p * frequency);
        frequency *= noiseLacunarity;
        amplitude *= noiseGain;
    }
    return value;
}

float noise_voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 8.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = vec2(noise_hash2(i + neighbor), noise_hash2(i + neighbor + 13.7));
            vec2 diff = neighbor + point - f;
            float dist = dot(diff, diff);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

float noise_turbulence(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        value += amplitude * abs(noise_simplex(p * frequency));
        frequency *= noiseLacunarity;
        amplitude *= noiseGain;
    }
    return value;
}

//ai: smooth lattice noise samples a world-space 2D slice through 3D noise.
float battle_noise_hash(vec3 p) {
    p = mod(p, 289.0);
    float h = (p.x * 34.0 + 1.0) * p.x
        + ((p.y * 34.0 + 1.0) * p.y) * 17.0
        + ((p.z * 34.0 + 1.0) * p.z) * 131.0;
    return fract(mod(h, 289.0) * (1.0 / 289.0));
}

float battle_noise3(vec3 p) {
    vec3 cell = floor(p);
    vec3 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);
    float x00 = mix(battle_noise_hash(cell),
        battle_noise_hash(cell + vec3(1.0,0.0,0.0)), local.x);
    float x10 = mix(battle_noise_hash(cell + vec3(0.0,1.0,0.0)),
        battle_noise_hash(cell + vec3(1.0,1.0,0.0)), local.x);
    float x01 = mix(battle_noise_hash(cell + vec3(0.0,0.0,1.0)),
        battle_noise_hash(cell + vec3(1.0,0.0,1.0)), local.x);
    float x11 = mix(battle_noise_hash(cell + vec3(0.0,1.0,1.0)),
        battle_noise_hash(cell + vec3(1.0,1.0,1.0)), local.x);
    return mix(mix(x00, x10, local.y), mix(x01, x11, local.y), local.z);
}

//ai: project segmented orbital glyphs onto fixed directions on the sphere.
float battle_sphere_glyph(vec3 direction, vec3 center, float phase) {
    vec3 axis = normalize(center);
    vec3 reference = abs(axis.y) < 0.9
        ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0);
    vec3 tangent = normalize(cross(reference, axis));
    vec3 bitangent = cross(axis, tangent);
    float polar = acos(clamp(dot(direction, axis), -1.0, 1.0));
    float azimuth = atan(dot(direction, bitangent), dot(direction, tangent));
    float ringA = 1.0 - smoothstep(0.004, 0.014, abs(polar - 0.22));
    float ringB = 1.0 - smoothstep(0.004, 0.013, abs(polar - 0.29));
    float segment = step(0.16, fract((azimuth / (2.0 * pi) + phase) * 9.0));
    float tick = smoothstep(0.93, 0.99, abs(cos(azimuth * 18.0 + phase)));
    float spokes = 1.0 - smoothstep(0.006, 0.018,
        abs(sin(azimuth * 6.0 + phase)));
    return ringA * segment + ringB * tick * 0.65 + spokes
        * (1.0 - smoothstep(0.0, 0.17, polar)) * 0.48;
}

//ai: color a distant sphere and expose a hot 3D-noise slice beneath the arena.
vec3 battle_space_color(vec3 rayDirection) {
    vec3 lava = vec3(0.055,0.007,0.025);
    float lavaBlend = 0.0;
    float planeT = (-1.05 - battleCameraPosition.y) / rayDirection.y;
    if (rayDirection.y < -0.035 && planeT > 0.0 && planeT < 500.0) {
        vec3 worldPosition = battleCameraPosition + rayDirection * planeT;
        float poolRadius = length(worldPosition.xz);
        lavaBlend = 1.0 - smoothstep(19.0, 27.0, poolRadius);
        if (poolRadius > 6.7 && lavaBlend > 0.0) {
            vec3 slice = vec3(worldPosition.x * 0.72,
                worldPosition.z * 0.72, time * 0.16);
            float broad = battle_noise3(slice);
            float detail = battle_noise3(slice * 1.93 + vec3(7.1,2.7,0.0));
            float heat = broad * 0.68 + detail * 0.32;
            float flow = smoothstep(0.43, 0.73, heat);
            float core = smoothstep(0.69, 0.88, heat);
            lava = mix(vec3(0.055,0.007,0.025),
                vec3(0.62,0.035,0.018), flow);
            lava = mix(lava, vec3(1.0,0.24,0.025), core * 0.82);
            lava += vec3(0.72,0.12,0.015)
                * smoothstep(0.80,0.96,detail) * 0.45;
        } else lavaBlend = 0.0;
    } else lavaBlend = 0.0;

    float sphereB = dot(battleCameraPosition, rayDirection);
    float sphereC = dot(battleCameraPosition, battleCameraPosition) - 42.0 * 42.0;
    float sphereT = -sphereB + sqrt(max(sphereB * sphereB - sphereC, 0.0));
    vec3 sphereDirection = normalize(battleCameraPosition
        + rayDirection * sphereT);
    float latitude = sphereDirection.y * 0.5 + 0.5;
    float cloud = battle_noise3(sphereDirection * 5.2
        + vec3(time * 0.11,time * 0.035,0.0));
    float nebula = smoothstep(0.30,0.79,cloud)
        * smoothstep(0.02,0.42,1.0 - abs(latitude - 0.42));
    vec3 sky = mix(vec3(0.006,0.012,0.038),
        vec3(0.035,0.018,0.105), smoothstep(0.0,0.74,latitude));
    sky += nebula * vec3(0.21,0.045,0.32);
    float star = step(0.9975,
        battle_noise_hash(floor(sphereDirection * 245.0)));
    float twinkle = 0.55 + 0.45 * sin(time * 2.1
        + battle_noise_hash(floor(sphereDirection * 245.0)) * 19.0);
    sky += star * twinkle * vec3(0.46,0.7,1.0);
    float glyphA = battle_sphere_glyph(sphereDirection,
        vec3(-0.48,-0.16,-0.84), time * 0.025);
    float glyphB = battle_sphere_glyph(sphereDirection,
        vec3(0.52,-0.12,-0.84), 0.19 - time * 0.018);
    float glyphC = battle_sphere_glyph(sphereDirection,
        vec3(0.02,-0.48,-0.88), 0.37 + time * 0.012);
    sky += glyphA * vec3(0.055,0.48,0.66);
    sky += glyphB * vec3(0.55,0.22,0.09);
    sky += glyphC * vec3(0.2,0.31,0.65);
    return mix(sky, lava, lavaBlend);
}

//opencode: generate a procedural noise color in [0,1]
vec4 generate_noise(vec3 coord) {
    vec2 p = coord.xy * noiseScale + vec2(noiseSpeed * time, noiseSpeed * time * 0.7);
    float n = 0.0;
    if (noiseMode == 1) {
        n = noise_simplex(p);
    } else if (noiseMode == 2) {
        n = noise_fbm(p, noiseOctaves);
    } else if (noiseMode == 3) {
        n = noise_voronoi(p);
    } else if (noiseMode == 4) {
        n = noise_turbulence(p, noiseOctaves);
    }
    n = n * 0.5 + 0.5; // map to [0,1]
    return vec4(n, n, n, 1.0);
}

//opencode: run the common transform pipeline on a coordinate
//   (sinusoids are applied separately per layer to allow independent transforms)
void apply_transform_pipeline(inout vec3 coord) {
    //anamorph in
    coord.xy *= vec2(asp,1.0);
    if (param_enable == 1) {
        float z_comp = coord.x*perspective.x + coord.y*perspective.y + scale;
        coord.xy -= rotateOrigin * z_comp;
    }
    vec4 trans = transform*vec4(coord,1.0);
    coord = trans.xyz;
    coord.xy /= vec2(asp,1.0);
    if (param_enable == 1) {
        coord.xy -= scaleOrigin;
    }
    coord.xy /= abs(coord.z);
    if (param_enable == 1) {
        coord.y *= -1.0;
        coord.xy -= translate;
    }
    if (hdma_enable == 1) { apply_hdma(coord); }
    if (reactType == 1 || reactType == 2 || reactType == 4) { apply_react(coord); }
    coord.xy *= vec2(asp,1.0);
}

//opencode: sample a texture at the transformed coordinate
vec4 sample_layer(sampler2D tex, vec2 texres, vec3 coord) {
    float texasp = texres.x / max(texres.y, 0.001);
    vec2 screencenter = vec2(0.5, 0.5);
    return texture(tex, vec2(1.0/texasp, 1.0) * coord.xy + screencenter).rgba;
}

//opencode: dual-layer blend function (tex0 = layer0, tex2 = layer1)
vec4 blend_layers(vec4 c0, vec4 c1) {
    if (blendMode == 0) {
        // Alpha blend (src over dst)
        return vec4(mix(c0.rgb, c1.rgb, c1.a), c0.a + c1.a * (1.0 - c0.a));
    } else if (blendMode == 1) {
        // Add
        return vec4(c0.rgb + c1.rgb, max(c0.a, c1.a));
    } else if (blendMode == 2) {
        // Subtract (dst - src)
        return vec4(max(c0.rgb - c1.rgb, 0.0), max(c0.a, c1.a));
    } else if (blendMode == 3) {
        // Multiply
        return vec4(c0.rgb * c1.rgb, max(c0.a, c1.a));
    } else if (blendMode == 4) {
        // Screen: 1 - (1-src)*(1-dst)
        return vec4(1.0 - (1.0 - c0.rgb) * (1.0 - c1.rgb), max(c0.a, c1.a));
    } else if (blendMode == 5) {
        // Difference
        return vec4(abs(c0.rgb - c1.rgb), max(c0.a, c1.a));
    } else if (blendMode == 6) {
        // Overlay
        vec3 result;
        for (int i = 0; i < 3; i++) {
            float b = c0[i], s = c1[i];
            result[i] = (b < 0.5) ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
        }
        return vec4(result, max(c0.a, c1.a));
    } else if (blendMode == 7) {
        // Color dodge: dst / (1 - src)
        vec3 result;
        for (int i = 0; i < 3; i++)
            result[i] = min(c0[i] / max(1.0 - c1[i], 0.001), 1.0);
        return vec4(result, max(c0.a, c1.a));
    }
    return c0; // fallback: layer0 only
}

//opencode: SNES-style color math — applied after palette processing
//   Simulates CGADSUB main+sub screen addition/subtraction with per-channel mask
void apply_color_math(inout vec4 color) {
    if (colorMathMode == 1) {
        // ADD: per-channel, masked
        color.rgb += colorMathMask;
        color = clamp(color, 0.0, 1.0);
    } else if (colorMathMode == 2) {
        // SUBTRACT: per-channel, masked
        color.rgb -= colorMathMask;
        color = clamp(color, 0.0, 1.0);
    } else if (colorMathMode == 3) {
        // HALF_ADD: (color + mask) / 2
        color.rgb = (color.rgb + colorMathMask) * 0.5;
    }
}

void calc_boundXY(inout vec4 color, in vec3 coord) {
    vec3 boundcoord = coord;
    if (modulo_enable == 1) {
        boundcoord.xy = mod(boundcoord.xy+0.5*modulo.xy,modulo.xy)-0.5*modulo.xy;
    }
    if (boundcoord.x < tex0asp*boundaryXY.x) color = color_bg;
    if (boundcoord.x > tex0asp*boundaryXY.y) color = color_bg;
    if (boundcoord.y < boundaryXY.z) color = color_bg;
    if (boundcoord.y > boundaryXY.w) color = color_bg;
}

void calc_boundZ(inout vec4 color, in vec3 coord) {
    vec3 boundcoord = coord;
    if (boundcoord.z < boundaryZ.x)  color = color_bg;
    if (boundcoord.z > boundaryZ.y)  color = color_bg;
}

//opencode: apply a range of sinusoids to a coordinate
void apply_sinusoids_range(inout vec3 coord, int start, int end) {
    for (int i = start; i < end; i++) {
        vec4 wave;
        if ((i >= 3 && i <= 5) || i == 7) {
            float d = length(coord.xy - sinusoidCen[i]);
            wave = compute_wave(
                sinusoidAmp[i], sinusoidFre[i], sinusoidPha[i], sinusoidSpd[i],
                sinusoidRwe[i], sinusoidIle[i], sinusoidIlm[i],
                sinusoidDcO[i], sinusoidLin[i], sinusoidQuad[i], vec3(d, d, d)
            );
        } else {
            wave = compute_wave(
                sinusoidAmp[i], sinusoidFre[i], sinusoidPha[i], sinusoidSpd[i],
                sinusoidRwe[i], sinusoidIle[i], sinusoidIlm[i],
                sinusoidDcO[i], sinusoidLin[i], sinusoidQuad[i], coord
            );
        }
        apply_sinusoid(wave, coord, i, sinusoidExp[i], sinusoidAtt[i], sinusoidCen[i]);
    }
}

void main() {
    if (battleViewEnabled != 0) {
        vec2 screen = gl_FragCoord.xy / res * 2.0 - 1.0;
        vec3 forward = normalize(battleCameraTarget - battleCameraPosition);
        vec3 right = normalize(cross(forward, battleCameraUp));
        vec3 upward = normalize(cross(right, forward));
        float tanHalfFov = tan(radians(battleCameraFov) * 0.5);
        vec3 rayDirection = normalize(forward
            + right * screen.x * (res.x / res.y) * tanHalfFov
            + upward * screen.y * tanHalfFov);
        vec3 spaceColor = battle_space_color(rayDirection);
        if (palette_enable == 1 && palette_mode != 1) {
            vec4 paletteColor = vec4(spaceColor, 1.0);
            if (palette_mode == 0) palette_mode0(paletteColor);
            if (palette_mode == 2) palette_mode2(paletteColor);
            if (palette_mode == 3) palette_mode3(paletteColor);
            if (palette_mode == 4) palette_mode4(paletteColor);
            if (palette_mode == 5) palette_mode5(paletteColor);
            if (palette_mode == 6) palette_mode6(paletteColor);
            if (palette_mode == 7) palette_mode7(paletteColor);
            spaceColor = paletteColor.rgb;
        }
        finalColor = vec4(spaceColor, 1.0);
        return;
    }
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy/res-screencenter);
    vec3 coord0 = vec3(screencoord,1.0);
    vec3 coord1 = vec3(screencoord,1.0);

    //opencode: sinusoids 0-3 affect layer0, 4-7 affect layer1
    apply_sinusoids_range(coord0, 0, 4);
    apply_sinusoids_range(coord1, 4, 8);

    //opencode: common transform pipeline for both layers
    apply_transform_pipeline(coord0);
    apply_transform_pipeline(coord1);

    //opencode: sample layer0 (texture or procedural noise)
    vec4 color0;
    if (noiseMode != 0) {
        color0 = generate_noise(coord0);
    } else {
        color0 = sample_layer(tex0, tex0res, coord0);
    }
    //opencode: sample layer1 (transparent when not enabled)
    vec4 color1 = (layer2Enabled != 0)
        ? sample_layer(tex2, tex2res, coord1)
        : vec4(0.0, 0.0, 0.0, 0.0);

    //opencode: blend layers
    vec4 color = blend_layers(color0, color1);

    if (palette_enable == 1) {
        if (palette_mode == 0) { palette_mode0(color); }
        if (palette_mode == 1) { palette_mode1(color); }
        if (palette_mode == 2) { palette_mode2(color); }
        if (palette_mode == 3) { palette_mode3(color); }
        if (palette_mode == 4) { palette_mode4(color); }
        if (palette_mode == 5) { palette_mode5(color); }
        if (palette_mode == 6) { palette_mode6(color); }
        if (palette_mode == 7) { palette_mode7(color); }
    }

    //opencode: color math post-process
    if (colorMathMode != 0) { apply_color_math(color); }

    //opencode: FLASH reaction — brighten/tint the final color
    if (reactType == 3) {
        color.rgb += reactIntensity * 0.5;
        color = clamp(color, 0.0, 1.0);
    }

    if (boundaryXY_enable == 1) { calc_boundXY(color, coord0); }
    if (boundaryZ_enable == 1) { calc_boundZ(color, coord0); }
    finalColor = color;
}
