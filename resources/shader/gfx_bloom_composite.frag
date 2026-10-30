//opencode: sceneTex is an HDR (RGBA16F) linear target. No input sRGB->linear decode
//   is correct - would pow() values >1 into nonsense and darken [0,1] range. The
//   only gamma step is the final linear->sRGB encode for the LDR framebuffer.
//   ACES filmic tone mapping (Narkowicz 2015 fit) provides superior highlight
//   rolloff and color preservation vs the simple Reinhard operator. ACES fit
//   credit and AgX/Sobotka credit: see license/NOTICE.md and
//   license/agx/LICENSE.
#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform sampler2D ssgiTex;      //opencode: v0.3-M4
uniform float   ssgiGain = 0.0;
uniform float intensity = 0.5;
uniform float exposure = 1.0;
uniform float vignette = 0.5;
uniform float saturation = 1.0;
uniform float contrast = 1.0;
uniform int   enableACES = 1;    // ACES filmic (default) vs Reinhard (legacy)
uniform int   enableAgX = 0;     //opencode: v0.3-M1 - AgX (takes precedence)
uniform int   enableGamma = 1;   //opencode: final linear->sRGB gamma encode
uniform int   ditherMode = 1;    //opencode: 0 off, 1 IGN, 2 Bayer4, 3 seeded noise
uniform float ditherSeed = 0.0;  //opencode: frame-stable seed unless animated by hook
uniform float ditherStrength = 1.5; //opencode: amplitude in 8-bit LSBs

out vec4 finalColor;

vec3 linearToSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

//opencode: ACES filmic tone mapping - Narkowicz 2015 fit.
//   Preserves saturation in highlights, smooth rolloff, filmic toe.
vec3 toneMapACES(vec3 hdr) {
    vec3 x = hdr * exposure;
    float a = 2.51; float b = 0.03;
    float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

//opencode: v0.3-M1 - AgX display rendering transform (Troy Sobotka, minimal
//   polynomial fit popularized in Blender/wgpu ports). Scene-linear RGB is
//   inset-rotated, log2-encoded over EV[-12.47, 4.03], shaped by a polynomial
//   sigmoid, outset back and gamma-decoded. Preserves hue/saturation through
//   highlight compression with minimal clipping - no ACES red-shift.
const mat3 agxInset = mat3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0783847640618072, 0.878468636469772,   0.0788494065771596,
    0.0792237451477643, 0.0791661274605434,  0.879142973793104);

const mat3 agxOutset = mat3(
    1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
    -0.0528968517574562, 1.15190312990417,   -0.0989611768448433,
    -0.0529716355144438, -0.0989611768448433, 1.15107367264116);

vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5     * x4 * x2
         - 40.14    * x4 * x
         + 31.96    * x4
         - 6.868    * x2 * x
         + 0.4298   * x2
         + 0.1191   * x
         - 0.00232;
}

vec3 toneMapAgX(vec3 hdr) {
    vec3 val = agxInset * (hdr * exposure);
    const float minEv = -12.47393;
    const float maxEv =   4.026069;
    val = clamp(log2(max(val, vec3(1e-10))), vec3(minEv), vec3(maxEv));
    val = (val - minEv) / (maxEv - minEv);
    val = agxDefaultContrastApprox(val);
    vec3 outLin = agxOutset * val;
    outLin = pow(max(outLin, vec3(0.0)), vec3(2.2));
    return clamp(outLin, 0.0, 1.0);
}

//opencode: Reinhard tone mapping (legacy) - simple, desaturates highlights.
vec3 toneMapReinhard(vec3 hdr) {
    vec3 mapped = hdr * exposure;
    return mapped / (mapped + vec3(1.0));
}

float bayer4x4(vec2 pixel) {
    int x = int(mod(pixel.x, 4.0));
    int y = int(mod(pixel.y, 4.0));
    int index = y * 4 + x;
    if (index == 0) return 0.0;
    if (index == 1) return 8.0;
    if (index == 2) return 2.0;
    if (index == 3) return 10.0;
    if (index == 4) return 12.0;
    if (index == 5) return 4.0;
    if (index == 6) return 14.0;
    if (index == 7) return 6.0;
    if (index == 8) return 3.0;
    if (index == 9) return 11.0;
    if (index == 10) return 1.0;
    if (index == 11) return 9.0;
    if (index == 12) return 15.0;
    if (index == 13) return 7.0;
    if (index == 14) return 13.0;
    return 5.0;
}

float ditherSample(vec2 pixel) {
    if (ditherMode == 0) return 0.5;
    vec2 seededPixel = pixel + vec2(ditherSeed, ditherSeed * 1.6180339);
    if (ditherMode == 2)
        return (bayer4x4(seededPixel) + 0.5) / 16.0;
    if (ditherMode == 3)
        return fract(sin(dot(seededPixel, vec2(12.9898, 78.233))) * 43758.5453);
    return fract(52.9829189 * fract(dot(seededPixel, vec2(0.06711056, 0.00583715))));
}

//opencode: both tone mappers leave linear-light output at this stage; apply
//   the display transfer encode once, regardless of the selected operator.
void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec3 color = texture(sceneTex, uv).rgb;
    //opencode: NaN bloom samples poison even when multiplied by zero.
    if (intensity > 0.0) {
        vec3 bloom = texture(bloomTex, uv).rgb;
        bloom = vec3(bloom.r == bloom.r ? bloom.r : 0.0,
                     bloom.g == bloom.g ? bloom.g : 0.0,
                     bloom.b == bloom.b ? bloom.b : 0.0);
        color += bloom * intensity;
    }
    if (ssgiGain > 0.001)
        color += max(texture(ssgiTex, uv).rgb, vec3(0.0)) * ssgiGain;

    //opencode: tone mapping - AgX > ACES > Reinhard precedence (v0.3-M1)
    if (enableAgX == 1)
        color = toneMapAgX(color);
    else if (enableACES == 1)
        color = toneMapACES(color);
    else
        color = toneMapReinhard(color);

    //opencode: vignette - darkens screen edges for film look
    vec2 vigUv = uv - 0.5;
    float vig = 1.0 - dot(vigUv, vigUv) * 2.0 * vignette;
    color *= clamp(vig, 0.0, 1.0);

    //opencode: color grading - saturation + contrast
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, saturation);
    color = clamp((color - 0.5) * contrast + 0.5, 0.0, 1.0);

    //opencode: dither after gamma at the LDR quantization point. The default
    //   IGN mode preserves the shipped path; GX_T_DITHER_MODE selects the
    //   opt-in off/Bayer/seeded-noise experiments without changing defaults.
    if (enableGamma == 1) color = linearToSrgb(color);
    color += (ditherSample(gl_FragCoord.xy) - 0.5) * (ditherStrength / 255.0);

    finalColor = vec4(color, 1.0);
}
