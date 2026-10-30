#version 330

//derived from bg_postprocess.frag from game1

uniform float time =            0.0;
uniform vec2 res =              vec2(1024,768);
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform sampler2D tex3;
uniform sampler2D tex4;
uniform sampler2D tex5;
uniform sampler2D tex6;
uniform sampler2D tex7;
uniform vec2 tex0res =          vec2(1024,768);
uniform vec2 tex1res =          vec2(1024,768);

//parameters
uniform int param_enable =      1;
uniform float scale =           1.0;
uniform float rotate =          0.0;
uniform vec2 scaleorigin =      vec2(0.0,0.0);
uniform vec2 rotateorigin =     vec2(0.0,0.0);
uniform vec2 translate =        vec2(0.0,0.0);
uniform vec2 affine =           vec2(0.0,0.0);
uniform vec2 perspective =      vec2(0.0,0.0);
uniform vec4 stretch =          vec4(0.0,0.0,0.0,0.0);

//special
uniform int warp_enable =       1;
uniform vec3 warp =             vec3(0.0,0.0,0.0);
uniform float warp_exp =        1.0;
uniform int wave_enable =       1;
uniform vec4 wave_amp =         vec4(0.000,1.050,0.000,0.000);  //amplitude
uniform vec4 wave_fre =         vec4(1.000,1.000,1.000,1.000);  //frequency
uniform vec4 wave_rwe =         vec4(1.000,1.000,1.000,1.000);  //regular wave enable
uniform vec4 wave_ile =         vec4(0.000,0.000,0.000,0.000);  //interlace wave enable
uniform vec4 wave_ilm =         vec4(2.000,2.000,2.000,2.000);  //interlace multiplier
uniform vec4 wave_spd =         vec4(0.000,0.250,0.000,0.100);  //speed
uniform vec4 wave_pha =         vec4(0.000,0.000,0.000,0.000);  //phase

//palette
uniform int palette_enable =    1;
uniform int palette_mode =      0;
//mode 0
uniform int palette_size = 16;
uniform vec4 colors[16];
//mode 1
uniform float palette_speed =   0.1;
uniform int palette_amount =    16;
uniform float palette_freq =    1.0;
uniform vec4 color1 =           vec4(0.0,1.0,0.0,1.0);
uniform vec4 color2 =           vec4(0.0,0.0,1.0,1.0);
uniform float bg_boundary =     0.1;
uniform float fg_boundary =     0.9;
uniform vec4 color_bg =         vec4(0.0,0.0,0.0,1.0);
uniform vec4 color_fg =         vec4(1.0,1.0,1.0,1.0);
//mode 2

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
const float pi = 3.14159265358979323846;

//radial
void calc_warp(inout vec3 warping, inout vec3 coord) {
    warping.xy = warp.xy*(coord.xy);
    coord.xy *= vec2(asp,1); //anamorph
    float dist = pow(length(coord),warp_exp);
    coord.xy /= vec2(asp,1); //undo anamorph
    warping.z = warp.z*dist;
}

//non-radial
void calc_wave(inout vec4 wave, inout vec3 coord) {
    vec4 coord4 = vec4(coord.x,coord.y,coord.x,coord.y);
    vec4 res4 = vec4(res.x,res.y,res.x,res.y);
    vec4 waving = wave_rwe;
    if (wave_fre.x != 0.0) waving.x += wave_ile.x*res4.x/(wave_ilm.x*wave_fre.x);
    if (wave_fre.y != 0.0) waving.y += wave_ile.y*res4.y/(wave_ilm.y*wave_fre.y);
    if (wave_fre.z != 0.0) waving.z += wave_ile.z*res4.z/(wave_ilm.z*wave_fre.z);
    if (wave_fre.w != 0.0) waving.w += wave_ile.w*res4.w/(wave_ilm.w*wave_fre.w);
    vec4 offs = wave_spd*time + wave_pha;
    wave = wave_amp*sin(( (wave_fre*coord4*waving) + offs )*2.0*pi);
}

void calc_param(inout vec3 coord, inout vec3 warping, inout vec4 wave) {
    mat3 transform = mat3(0.0);
    transform[0][0] = 1.0;
    transform[1][1] = 1.0;
    transform[2][2] = 0.5;
    
    transform[0][2] += wave.x+wave.y;
    transform[1][2] += wave.z+wave.w;
    transform[2][0] += warping.x;
    transform[2][1] += warping.y;
    
    coord.xy *= vec2(asp,1); //anamorph
    if (param_enable == 1) {
        float ang = 2.0*pi*rotate;
        float c = cos(ang);
        float s = sin(ang);
        transform[0][0] = +c+stretch.x;
        transform[0][1] = -s+stretch.y;
        transform[1][0] = +s+stretch.z;
        transform[1][1] = +c+stretch.w;
        transform[0][2] += affine.x;
        transform[1][2] += affine.y;
        transform[2][0] += perspective.x;
        transform[2][1] += perspective.y;
        transform[2][2] = scale;
        coord.xy -= (rotateorigin)*(coord.x*perspective.x + coord.y*perspective.y + scale);
    }
    transform[2][2] += warping.z;
    coord *= transform;
    if (param_enable == 1) {
        coord.xy /= vec2(asp,1); //undo anamorph
        coord.xy -= scaleorigin;
    }
    coord.xy /= abs(coord.z);
    if (param_enable == 1) {
        coord.y *= -1;
        coord.xy -= translate;
        coord.xy *= vec2(asp,1); //redo anamorph
    }
}

void palette_mode0(inout vec4 color) {
    float avg = color.x;
    int factor = int(min(palette_size,128));
    //color = colors[factor*int(palette_freq*avg+palette_speed*time)%factor];
    color = colors[1+((int(factor*avg+6*time))%(factor-1))];
}

void palette_mode1(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    //float cycle = mod(palette_freq*avg+palette_speed*time,1.0);
    float cycle = 0.5*(sin(2.0*pi*(palette_freq*avg+palette_speed*time))+1.0);
    color = mix(color1,color2,cycle);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount)*color)/float(palette_amount);
}

//opencode: palette mode 2 — indexed color lookup via ImageProcessor
//   tex0 = grayscale index texture, tex1 = 1×N palette texture
void palette_mode2(inout vec4 color) {
    float idx = color.r; // grayscale index from tex0 sample
    color = texture(tex1, vec2(idx, 0.0)).rgba;
}

//opencode: palette mode 3 — rainbow/hue cycle (shared with new shader)
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
void palette_mode3(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    float hue = fract(palette_freq * avg + palette_speed * time);
    float sat = 0.8 + 0.2 * sin(avg * 10.0 + time);
    float val = avg;
    color.rgb = hsv2rgb(vec3(hue, sat, val));
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount) * color) / float(palette_amount);
}

//opencode: palette mode 4 — posterize + ordered dither
const float _bayer4x4[16] = float[16](
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
    float dither = _bayer4x4[frag.y * 4 + frag.x] / levels;
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

//opencode: palette mode 6 — HSV independent
void palette_mode6(inout vec4 color) {
    float avg = (color.x+color.y+color.z)/3.0;
    float hShift = color1.x * sin(2.0 * pi * (color2.x * avg + color1.w * time));
    float sMult = 1.0 + color1.y * sin(2.0 * pi * (color2.y * avg + color1.w * time * 0.7));
    float vMult = 1.0 + color1.z * sin(2.0 * pi * (color2.z * avg + color1.w * time * 0.5));
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
    color.rgb = avg + sMult * (color.rgb - avg);
    color.rgb *= vMult / max(avg, 0.001);
    color = clamp(color, 0.0, 1.0);
    if (avg < bg_boundary) color = color_bg;
    if (avg > fg_boundary) color = color_fg;
    color = floor(float(palette_amount) * color) / float(palette_amount);
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

void calc_bg(out vec4 color, in vec3 coord, vec2 screencenter) {
    vec3 warping = vec3(0.0);
    vec4 wave = vec4(0.0);
    if (warp_enable == 1) { calc_warp(warping, coord); }
    if (wave_enable == 1) { calc_wave(wave, coord); }
    calc_param(coord,warping,wave);
    color += texture(tex0,vec2(1.0/tex0asp,1.0)*coord.xy+screencenter).rgba;
    if (palette_enable == 1) {
        if (palette_mode == 0) { palette_mode0(color); }
        if (palette_mode == 1) { palette_mode1(color); }
        if (palette_mode == 2) { palette_mode2(color); }
        if (palette_mode == 3) { palette_mode3(color); }
        if (palette_mode == 4) { palette_mode4(color); }
        if (palette_mode == 5) { palette_mode5(color); }
        if (palette_mode == 6) { palette_mode6(color); }
    }
    if (boundaryXY_enable == 1) { calc_boundXY(color,coord); }
    if (boundaryZ_enable == 1) { calc_boundZ(color,coord); }
}

void main() {
    
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy/res-screencenter);
    vec3 coord = vec3(screencoord,1.0);
    
    vec4 color = color_bg;
    for (int i = 0; i < 1; i++) {
        calc_bg(color,coord,screencenter);
    }
    
    finalColor = color;
}
 
