#version 330

//opencode: postprocess-style GUI FX preview. Samples the canvas backdrop
//   (gui.ping via `inputTex`) so translucency and lens modes read live content
//   like the postprocess chain does. Mode lives in uniforms[0].z:
//   0 frost, 1 glass, 2 lens, 3 chroma, 4 grade, 5 poster.

uniform vec4 col;
uniform vec2 res;
uniform vec4 box;
uniform vec4 uniforms[4];
//opencode: `input` is a reserved interface qualifier in desktop GLSL 1.30+.
uniform sampler2D inputTex;
uniform float time;

out vec4 finalColor;

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.y, q.x), 0.0) - r;
}

vec3 grade(vec3 c, float bright, float contrast, float saturation)
{
    c += vec3(bright);
    c = (c - 0.5) * contrast + 0.5;
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, saturation);
    return clamp(c, vec3(0.0), vec3(1.0));
}

void main()
{
    if (col.a <= 0.002)
    {
        discard;
    }
    vec2 fragPos = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y);
    vec2 center = box.xy + box.zw * 0.5;
    vec2 p = fragPos - center;
    vec2 half_ = max(box.zw * 0.5 - 1.0, vec2(1.0));
    float radius = clamp(uniforms[0].x, 0.0, min(box.z, box.w) * 0.5);
    float d = sdRoundBox(p, half_, radius);
    float aa = max(fwidth(d), 0.0001);
    float silhouette = 1.0 - smoothstep(-aa, aa, d);
    vec2 baseUV = gl_FragCoord.xy / max(res, vec2(1.0));
    float mode = uniforms[0].z;
    float intensity = clamp(uniforms[0].w, 0.0, 1.0);
    vec3 tint = uniforms[1].rgb;
    vec3 fill = col.rgb;
    vec2 cardUV = clamp((fragPos - box.xy) / max(box.zw, vec2(1.0)), vec2(0.0), vec2(1.0));
    //opencode: procedural fallback — the lab replaces the title, so the
    //   backdrop sampler may read an empty buffer. A checker/gradient field
    //   keeps every warp readable while live content still shows through.
    float checker = mod(floor(fragPos.x / 26.0) + floor(fragPos.y / 26.0), 2.0);
    vec3 procedural = mix(col.rgb, tint + 0.35, checker * 0.75 + cardUV.x * 0.20);
    if (mode < 0.5)
    {
        float blurR = max(uniforms[2].x, 1.0);
        vec2 px = blurR / max(res, vec2(1.0));
        vec3 acc = texture(inputTex, baseUV).rgb * 0.2270270270;
        acc += texture(inputTex, baseUV + vec2(px.x, 0.0)).rgb * 0.1942131600;
        acc += texture(inputTex, baseUV - vec2(px.x, 0.0)).rgb * 0.1942131600;
        acc += texture(inputTex, baseUV + vec2(0.0, px.y)).rgb * 0.1942131600;
        acc += texture(inputTex, baseUV - vec2(0.0, px.y)).rgb * 0.1942131600;
        float shimmer = sin((fragPos.x + fragPos.y) * 0.35 + time * 0.7) * 0.012;
        vec3 frost = mix(acc, col.rgb, 0.28 + shimmer) + tint * 0.06;
        fill = mix(procedural, frost, 0.30);
    }
    else if (mode < 1.5)
    {
        float refr = uniforms[2].x / max(res.x, 1.0);
        vec2 wave = vec2(sin(fragPos.y * 0.045 + time * 0.8), cos(fragPos.x * 0.038 - time * 0.6));
        vec2 offset = wave * refr * (6.0 + intensity * 10.0);
        vec3 refrCol = texture(inputTex, baseUV + offset).rgb;
        vec3 refrProc = mix(procedural, procedural.brg, clamp(length(offset) * 40.0, 0.0, 1.0) * 0.5);
        vec3 refrBase = mix(refrProc, refrCol, 0.30);
        float localY = clamp((fragPos.y - box.y) / max(box.w, 1.0), 0.0, 1.0);
        float highlight = pow(max(0.0, 1.0 - abs(localY - 0.2) / 0.22), 5.0);
        fill = mix(refrBase, col.rgb, 0.22) + tint * (highlight * 0.35);
    }
    else if (mode < 2.5)
    {
        vec2 centerGL = vec2(center.x, res.y - center.y);
        vec2 centerUV = centerGL / max(res, vec2(1.0));
        vec2 delta = baseUV - centerUV;
        float r = length(delta * vec2(res.x / max(res.y, 1.0), 1.0));
        float zoom = 1.0 + intensity * 1.4 * (1.0 - smoothstep(0.0, 0.35, r));
        vec2 warpUV = centerUV + delta / max(zoom, 0.2);
        vec2 warpPos = vec2(warpUV.x * res.x, res.y - warpUV.y * res.y);
        float wchecker = mod(floor(warpPos.x / 26.0) + floor(warpPos.y / 26.0), 2.0);
        vec3 warpProc = mix(col.rgb, tint + 0.35, wchecker * 0.55 + cardUV.x * 0.20);
        vec3 lensCol = texture(inputTex, warpUV).rgb;
        vec3 lensBase = mix(warpProc, lensCol, 0.30);
        float ring = smoothstep(0.30, 0.34, r) * (1.0 - smoothstep(0.34, 0.38, r));
        fill = lensBase + tint * ring * 0.6;
    }
    else if (mode < 3.5)
    {
        float spread = uniforms[2].x / max(res.x, 1.0);
        float r = texture(inputTex, baseUV + vec2(spread, 0.0)).r;
        float g = texture(inputTex, baseUV).g;
        float b = texture(inputTex, baseUV - vec2(spread, 0.0)).b;
        vec3 splitLive = mix(texture(inputTex, baseUV).rgb, vec3(r, g, b), 0.35 + intensity * 0.65);
        float pr = mix(procedural.r, procedural.g, 0.35 + intensity * 0.4);
        float pb = mix(procedural.b, procedural.g, 0.35 + intensity * 0.4);
        vec3 splitProc = vec3(pr, procedural.g, pb);
        fill = mix(splitProc, splitLive, 0.35);
        fill = mix(fill, tint, 0.12);
    }
    else if (mode < 4.5)
    {
        vec3 src = texture(inputTex, baseUV).rgb;
        float checker = mod(floor(fragPos.x / 26.0) + floor(fragPos.y / 26.0), 2.0);
        vec3 procedural = mix(col.rgb, tint + 0.35, checker * 0.55);
        vec3 base = mix(procedural, src, 0.45 + intensity * 0.30);
        fill = grade(base, uniforms[2].x, uniforms[2].y, uniforms[2].z);
    }
    else
    {
        float levels = max(uniforms[2].x, 2.0);
        vec3 src = texture(inputTex, baseUV).rgb;
        float posterMix = 0.55 + intensity * 0.30;
        vec3 procedural = mix(col.rgb, tint + 0.30, cardUV.x);
        vec3 base = mix(procedural, src, posterMix);
        vec3 poster = floor(base * levels + 0.5) / levels;
        vec2 px = 1.0 / max(res, vec2(1.0));
        float gx = dot(texture(inputTex, baseUV + vec2(px.x, 0.0)).rgb - texture(inputTex, baseUV - vec2(px.x, 0.0)).rgb, vec3(0.333));
        float gy = dot(texture(inputTex, baseUV + vec2(0.0, px.y)).rgb - texture(inputTex, baseUV - vec2(0.0, px.y)).rgb, vec3(0.333));
        float edge = smoothstep(0.08, 0.32, length(vec2(gx, gy)) * 2.0);
        vec2 q = cardUV - vec2(0.5);
        float vignette = 1.0 - smoothstep(0.30, 0.72, length(q * vec2(1.15, 1.0)));
        fill = mix(poster, vec3(0.02), edge * 0.55) * (0.55 + vignette * 0.45);
    }
    float borderWidth = uniforms[0].y;
    if (borderWidth > 0.25)
    {
        float rim = 1.0 - smoothstep(borderWidth - aa, borderWidth + aa, abs(d));
        fill = mix(fill, tint, rim * 0.35);
    }
    finalColor = vec4(fill, col.a * silhouette);
}
