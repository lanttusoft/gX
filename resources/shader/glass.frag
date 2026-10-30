#version 330

//opencode: Aero-glass element surface. Samples the accumulated backdrop
//   (gui.ping: scene capture + earlier canvases) through a small gaussian
//   blur, tints toward the element color, and adds an inner silhouette rim
//   plus a top gloss. Drawn as a screen quad over the element box.
//
//   Per-draw inputs (element shader contract):
//     rect/box   element box in the target buffer (top-left origin)
//     res        target buffer resolution
//     col        element color (rgb = tint target, a = base alpha)
//     input      accumulated backdrop texture
//     uniforms[0] = (blur px, tint 0..1, rim strength, refraction px)
//     uniforms[1] = (gloss strength, corner radius px, unused, unused)

in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec4 rect;
uniform vec2 res;
uniform vec4 col;
uniform vec4 uniforms[4];
uniform sampler2D input;

out vec4 finalColor;

float gammafactor = 2.2;
vec4 gamma(vec4 c) { return pow(c, vec4(1.0 / gammafactor)); }
vec4 degamma(vec4 c) { return pow(c, vec4(gammafactor)); }

//opencode: rounded-box SDF (same math as guisurface.frag, Y-flipped space)
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.y, q.x), 0.0) - r;
}

void main() {
    vec2 coord = gl_FragCoord.xy;
    //opencode: rect-relative space matches box's top-left origin (see
    //   guisurface.frag's Y-flip note); the backdrop is sampled with raw
    //   frag coords like the proven blur pass.
    vec2 fragPos = vec2(coord.x, res.y - coord.y);
    vec2 center = rect.xy + rect.zw * 0.5;
    vec2 half_ = max(rect.zw * 0.5 - 1.0, vec2(1.0));
    float maxR = min(rect.z, rect.w) * 0.5;
    float radius = clamp(uniforms[1].y, 0.0, maxR);
    vec2 p = fragPos - center;
    float d = sdRoundBox(p, half_, radius);
    float aa = max(fwidth(d), 0.0001);
    float mask = 1.0 - smoothstep(-aa, aa, d);
    if (mask <= 0.002 || col.a <= 0.002) discard;

    float blur = max(uniforms[0].x, 0.0);
    float refraction = uniforms[0].w;
    vec2 uv = (coord + (p / half_) * refraction) / res;

    //opencode: 3x3-tap gaussian from the blur pass (linear-sampling weights);
    //   gamma-correct like blur.frag so the tint does not darken highlights.
    const float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
    const float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);
    vec4 sum = degamma(texture(input, uv)) * weight[0];
    for (int i = 1; i < 3; i++) {
        vec2 o = blur * offset[i] / res;
        sum += degamma(texture(input, uv + vec2(+o.x, +o.y))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(+o.x, -o.y))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(-o.x, +o.y))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(-o.x, -o.y))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(+o.x, 0.0))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(-o.x, 0.0))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(0.0, +o.y))) * 0.25 * weight[i];
        sum += degamma(texture(input, uv + vec2(0.0, -o.y))) * 0.25 * weight[i];
    }
    vec3 backdrop = gamma(sum).rgb;

    //opencode: tint, inner rim near the silhouette, and a top-edge gloss
    vec3 glass = mix(backdrop, col.rgb, clamp(uniforms[0].y, 0.0, 1.0));
    float rim = (1.0 - smoothstep(0.0, 2.5, abs(d))) * uniforms[0].z;
    float glossT = clamp((fragPos.y - rect.y) / max(rect.w, 1.0), 0.0, 1.0);
    float gloss = (1.0 - smoothstep(0.0, 0.5, glossT)) * uniforms[1].x;
    glass += vec3(1.0) * clamp(rim + gloss, 0.0, 1.0);

    float alpha = clamp(col.a * mask + (rim + gloss) * 0.45, 0.0, 1.0) * mask;
    finalColor = vec4(clamp(glass, 0.0, 1.0), alpha);
}
