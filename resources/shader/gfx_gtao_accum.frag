#version 330
//opencode: GTAO temporal accumulation (SSGI-Phase2 mirror, AO+depth history).
//   Reprojects through the velocity buffer, validates depth-bearing history
//   taps against the previous surface, rejects AO outliers against the current
//   neighborhood, and blends history-heavy when still. AO is stored in R and
//   the corresponding raw depth in G.

in vec2 fragTexCoord;

uniform sampler2D currentMap;   // spatially denoised GTAO (blurMap)
uniform sampler2D historyMap;   // previous accumulated AO in R, previous depth in G
//ai: scene depth is packed into the AO history to validate surface ownership.
uniform sampler2D depthMap;
//ai: CPU lifecycle state gates history sampling after a reset or preset change.
uniform int   historyValid = 0;
uniform int   cameraMoving = 0;
//ai: B=2+previous depth for raster/terrain, B=3.5 for invalid depth, B=1 for legacy writers.
uniform sampler2D velocityMap;  // .rg motion, .b validity/depth payload (unit 21)
uniform int   hasVelocity = 0;
uniform vec2  resolution;

out vec4 finalColor;

//ai: half-float depth payloads need margin for encoding and history quantization.
const float DEPTH_HISTORY_TOLERANCE = 0.005;
//ai: during camera motion, contact AO needs a tighter freshness bound than broad AO gradients.
const float CONTACT_AO_CUT = 0.98;
const float CONTACT_HISTORY_MAX_DELTA = 1.0 / 255.0;

//ai: bilinear AO history includes only taps whose stored depth matches this surface.
bool sampleDepthMatchedHistory(vec2 uv, float expectedDepth, out float historyAO) {
    ivec2 size = textureSize(historyMap, 0);
    vec2 pixel = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    ivec2 upper = size - ivec2(1);
    vec2 fraction = fract(pixel);
    float sumAO = 0.0;
    float sumWeight = 0.0;
    vec4 tap = texelFetch(historyMap, clamp(base, ivec2(0), upper), 0);
    float weight = (1.0 - fraction.x) * (1.0 - fraction.y);
    if (abs(tap.g - expectedDepth) <= DEPTH_HISTORY_TOLERANCE) {
        sumAO += tap.r * weight;
        sumWeight += weight;
    }
    tap = texelFetch(historyMap, clamp(base + ivec2(1, 0), ivec2(0), upper), 0);
    weight = fraction.x * (1.0 - fraction.y);
    if (abs(tap.g - expectedDepth) <= DEPTH_HISTORY_TOLERANCE) {
        sumAO += tap.r * weight;
        sumWeight += weight;
    }
    tap = texelFetch(historyMap, clamp(base + ivec2(0, 1), ivec2(0), upper), 0);
    weight = (1.0 - fraction.x) * fraction.y;
    if (abs(tap.g - expectedDepth) <= DEPTH_HISTORY_TOLERANCE) {
        sumAO += tap.r * weight;
        sumWeight += weight;
    }
    tap = texelFetch(historyMap, clamp(base + ivec2(1, 1), ivec2(0), upper), 0);
    weight = fraction.x * fraction.y;
    if (abs(tap.g - expectedDepth) <= DEPTH_HISTORY_TOLERANCE) {
        sumAO += tap.r * weight;
        sumWeight += weight;
    }
    if (sumWeight <= 0.00001) {
        historyAO = 0.0;
        return false;
    }
    historyAO = sumAO / sumWeight;
    return true;
}

//ai: invalid history always resolves to the current spatial AO for this frame.
//ai: reject out-of-neighborhood temporal AO; preserve the existing pad and blend policy.
void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float cur = textureLod(currentMap, uv, 0.0).r;
    ivec2 depthSize = textureSize(depthMap, 0);
    ivec2 depthCoord = clamp(ivec2(floor(uv * vec2(depthSize))), ivec2(0), depthSize - ivec2(1));
    float currentDepth = texelFetch(depthMap, depthCoord, 0).r;
    vec2 currentPixelUv = (vec2(depthCoord) + 0.5) / vec2(depthSize);

    //opencode: neighborhood bounds (3x3 of current)
    float mn = cur, mx = cur;
    vec2 px = 1.0 / resolution;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float s = textureLod(currentMap, uv + vec2(px.x * float(x), px.y * float(y)), 0.0).r;
            mn = min(mn, s); mx = max(mx, s);
        }
    }
    //opencode: AO gradients are gentle (no GI bounce range): tight pad.
    float pad = 0.03;
    ivec2 velocitySize = textureSize(velocityMap, 0);
    ivec2 velocityCoord = clamp(ivec2(floor(currentPixelUv * vec2(velocitySize))),
        ivec2(0), velocitySize - ivec2(1));
    vec4 velocityTap = texelFetch(velocityMap, velocityCoord, 0);
    bool velocityValid = hasVelocity == 1 && velocityTap.b > 0.5;
    vec2 histUv = uv;
    if (velocityValid)
        histUv = currentPixelUv - velocityTap.rg;

    //opencode: no history when the velocity RT has no writer for this pixel.
    bool ok = (hasVelocity != 1 || velocityValid)
        && (histUv.x >= 0.0 && histUv.x <= 1.0 && histUv.y >= 0.0 && histUv.y <= 1.0);
    float acc = cur;
    if (historyValid == 1 && ok) {
        bool hasDepthPayload = velocityValid && velocityTap.b >= 2.0;
        bool depthPayloadInRange = velocityTap.b <= 3.0;
        float hist = cur;
        bool surfaceMatches = true;
        if (hasDepthPayload) {
            surfaceMatches = depthPayloadInRange
                && sampleDepthMatchedHistory(histUv, velocityTap.b - 2.0, hist);
        } else {
            hist = textureLod(historyMap, histUv, 0.0).r;
        }
        bool motionCompatible = cameraMoving != 1 || abs(hist - cur) <= pad;
        bool contactHistoryFresh = cameraMoving != 1 || min(hist, cur) >= CONTACT_AO_CUT
            || abs(hist - cur) <= CONTACT_HISTORY_MAX_DELTA;
        bool historyInNeighborhood = surfaceMatches && motionCompatible && contactHistoryFresh
            && hist >= mn - pad && hist <= mx + pad;
        float acceptedHistory = historyInNeighborhood ? hist : cur;
        //opencode: motion magnitude controls the existing history-weight band.
        float velLen = velocityValid ? length(velocityTap.rg) : 0.0;
        //opencode: still-frame heavy blend (converges the oracle rotation);
        //   fast motion falls back toward current (same band as SSGI).
        float blend = mix(0.85, 0.30, smoothstep(0.05, 0.25, velLen));
        acc = mix(cur, acceptedHistory, blend);
    }

    finalColor = vec4(acc, currentDepth, 0.0, 1.0);
}
