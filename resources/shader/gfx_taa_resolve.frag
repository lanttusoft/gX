//opencode: TAA (temporal anti-aliasing) resolve shader
//   Reprojects previous frame's color using motion vectors,
//   applies neighborhood clamping to prevent ghosting,
//   blends with current frame using exponential moving average.
//   Input: current frame (post-processed color), velocity (from depth),
//          history (previous resolved frame)
//   Output: temporally accumulated frame with reduced aliasing
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;    //opencode: current frame color (post-bloom/mb/dof)
uniform sampler2D depthMap;    //opencode: scene depth (for velocity reconstruction)
uniform sampler2D historyMap;  //opencode: previous resolved frame
uniform sampler2D velocityMap; //opencode: v0.1.38 - motion vector RT (.rg), .b valid
//ai: 0=no map, 1=vectors+coverage, 2=coverage with forced camera fallback.
uniform int hasVelocityBuffer = 0;
//ai: diagnostic 1 exports actual camera motion/previous depth; 2 exports history eligibility.
uniform int hybridReprojectionDebug = 0;
uniform mat4 prevVP;           //opencode: previous view-projection matrix
uniform mat4 invVP;            //opencode: inverse current view-projection
//ai: CPU rejects non-finite inverse/snapshot coefficients before uploading either matrix.
uniform int reprojectionValid = 1;
uniform vec2 resolution;
uniform float blendFactor = 0.95; //opencode: history weight (0.9-0.97 typical)
uniform vec2 jitter = vec2(0.0);  //opencode: current frame jitter (inverse of rendering jitter)
uniform vec2 prevJitter = vec2(0.0); //opencode: previous frame jitter
uniform int oracleAccum = 0; //opencode: oracle (Paper 1) - accumulating
    //   stochastic single-samples: skip neighborhood clamping (a single noisy
    //   frame's 3x3 box is not a validity range; clamping ratchets the mean
    //   down for skewed MC distributions). Default 0 = production TAA kept.

out vec4 finalColor;

// 3x3 neighborhood min/max for clamping
vec3 neighborhoodMin(sampler2D tex, vec2 uv) {
    vec2 off = 1.0 / resolution;
    vec3 c = texture(tex, uv).rgb;
    vec3 n = texture(tex, uv + vec2(0.0, off.y)).rgb;
    vec3 s = texture(tex, uv - vec2(0.0, off.y)).rgb;
    vec3 e = texture(tex, uv + vec2(off.x, 0.0)).rgb;
    vec3 w = texture(tex, uv - vec2(off.x, 0.0)).rgb;
    vec3 ne = texture(tex, uv + off).rgb;
    vec3 nw = texture(tex, uv + vec2(-off.x, off.y)).rgb;
    vec3 se = texture(tex, uv + vec2(off.x, -off.y)).rgb;
    vec3 sw = texture(tex, uv - off).rgb;
    return min(c, min(n, min(s, min(e, min(w, min(ne, min(nw, min(se, sw))))))));
}

vec3 neighborhoodMax(sampler2D tex, vec2 uv) {
    vec2 off = 1.0 / resolution;
    vec3 c = texture(tex, uv).rgb;
    vec3 n = texture(tex, uv + vec2(0.0, off.y)).rgb;
    vec3 s = texture(tex, uv - vec2(0.0, off.y)).rgb;
    vec3 e = texture(tex, uv + vec2(off.x, 0.0)).rgb;
    vec3 w = texture(tex, uv - vec2(off.x, 0.0)).rgb;
    vec3 ne = texture(tex, uv + off).rgb;
    vec3 nw = texture(tex, uv + vec2(-off.x, off.y)).rgb;
    vec3 se = texture(tex, uv + vec2(off.x, -off.y)).rgb;
    vec3 sw = texture(tex, uv - off).rgb;
    return max(c, max(n, max(s, max(e, max(w, max(ne, max(nw, max(se, sw))))))));
}

//ai: hybrid history uses one explicit bilinear base-level footprint for color and depth,
// independent of the render texture's legacy nearest-neighbor sampler state.
vec4 hybridHistoryTap(vec2 uv) {
    ivec2 size = textureSize(historyMap,0);
    vec2 pixel = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 fraction = fract(pixel);
    ivec2 upper = size - ivec2(1);
    vec4 a = texelFetch(historyMap,clamp(base,ivec2(0),upper),0);
    vec4 b = texelFetch(historyMap,clamp(base+ivec2(1,0),ivec2(0),upper),0);
    vec4 c = texelFetch(historyMap,clamp(base+ivec2(0,1),ivec2(0),upper),0);
    vec4 d = texelFetch(historyMap,clamp(base+ivec2(1,1),ivec2(0),upper),0);
    return mix(mix(a,b,fraction.x),mix(c,d,fraction.x),fraction.y);
}

//ai: comparisons alone do not reject NaNs; both finiteness and denominator guards are required.
bool finiteVector(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

void main() {
    //ai: constant-output probe separates raster coverage from sampler/reprojection control flow.
    if (hybridReprojectionDebug == 5) {
        finalColor = vec4(0.125,0.25,0.5,1.0);
        return;
    }
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 current = texture(colorMap, uv).rgb;

    float depth = texture(depthMap, uv).r;
    //ai: current-input witness lets the oracle verify rejected pixels in the production resolve.
    if (hybridReprojectionDebug == 6) {
        finalColor = vec4(current,depth);
        return;
    }
    //ai: unsafe candidates always emit fresh color/depth, not history or diagnostic sentinel data.
    if (reprojectionValid == 0) {
        finalColor = vec4(current,depth);
        return;
    }

    //opencode: v0.1.38 - velocity from the motion-vector buffer when present
    //   (geometry pass, unjittered VP both frames); fallback reconstructs the
    //   world position from depth and reprojects with prevVP (camera-only).
    //opencode: background rule — the buffer covers opaque geometry only, so
    //   sky pixels read velocity 0 (stale history under rotation). At far
    //   depth the fallback's far-plane reprojection IS the camera motion
    //   (translation vanishes at infinity), so sky takes the fallback path.
    vec2 velocity;
    vec2 motionVelocity;
    //opencode: NDC-space jitter correction is jitter * 0.5 (uv = ndc * 0.5 + 0.5),
    //   NOT jitter / resolution (past trap, kept explicit).
    //opencode: the depth buffer holds JITTERED geometry, so unproject the
    //   unjittered NDC (ndc - jitter); the previous frame rendered the same
    //   point at prevUV + prevJitter*0.5, which is where its history lives.
    //   The old form reconstructed with the jittered NDC and then sampled
    //   prevUV - prevJitter*0.5 (a full jitter offset away): the no-velocity
    //   fallback shimmered (9.8 mean / 62% pixels per frame on y2k).
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc - jitter, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invVP * clipPos;
    if (!finiteVector(worldPos) || abs(worldPos.w) <= 1.0e-5) {
        finalColor = vec4(current,depth);
        return;
    }
    worldPos /= worldPos.w;
    if (!finiteVector(worldPos)) {
        finalColor = vec4(current,depth);
        return;
    }
    vec4 prevClip = prevVP * worldPos;
    if (!finiteVector(prevClip) || prevClip.w <= 1.0e-5) {
        finalColor = vec4(current,depth);
        return;
    }
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
    float expectedPrevDepth = prevClip.z / prevClip.w * 0.5 + 0.5;
    if (!finiteVector(vec4(prevUV,expectedPrevDepth,1.0))) {
        finalColor = vec4(current,depth);
        return;
    }
    vec2 fallbackVelocity = uv - (prevUV + prevJitter * 0.5);
    vec4 velocityTap = texture(velocityMap, uv);
    //ai: negative coverage belongs to SDF surfaces, not raster vectors behind them.
    bool hybridPixel = hasVelocityBuffer > 0 && velocityTap.b < -0.5;
    //opencode: unwritten pixels (water/particles before object replay) carry
    //   clear-zero RG but must use camera fallback, not a false static vector.
    if (hasVelocityBuffer == 1 && depth < 0.999 && velocityTap.b > 0.5) {
        velocity = velocityTap.rg;
        motionVelocity = velocity;
    } else {
        velocity = fallbackVelocity;
        //opencode: fallback reprojection includes the current-to-previous
        //   Halton sample offset; remove it from motion classification so
        //   a static scene remains static (otherwise ~0.8 px of jitter
        //   lowers history weight from 0.95 to ~0.56 on y2k).
        motionVelocity = fallbackVelocity - (jitter - prevJitter) * 0.5;
    }

    //ai: hybrid depth is compared in the previous projection; out-of-view history must not clamp into an edge.
    if (!finiteVector(vec4(velocity,motionVelocity))) {
        finalColor = vec4(current,depth);
        return;
    }
    vec2 historyUV = uv - velocity;
    if (!finiteVector(vec4(historyUV,0.0,1.0))) {
        finalColor = vec4(current,depth);
        return;
    }
    bool hybridOutOfView = velocityTap.b < -1.5 || prevClip.w <= 1.0e-5
        || any(lessThan(historyUV,vec2(0.0))) || any(greaterThan(historyUV,vec2(1.0)))
        || expectedPrevDepth < 0.0 || expectedPrevDepth > 1.0;
    //ai: assign the uniform-dependent predicate once, avoiding the driver's conditional-reassignment trap.
    vec4 hybridHistory = hybridPixel && !hybridOutOfView ? hybridHistoryTap(historyUV) : vec4(0.0);
    float reprojectedDepth = hybridHistory.a;
    bool hybridRejected = hybridPixel && (hybridOutOfView || abs(reprojectedDepth - expectedPrevDepth) > 0.001);
    if (hybridReprojectionDebug == 1) {
        finalColor = hybridPixel ? vec4(motionVelocity,expectedPrevDepth,1.0) : vec4(0.0);
        return;
    }
    if (hybridReprojectionDebug == 2) {
        finalColor = hybridPixel ? vec4(historyUV,expectedPrevDepth,hybridRejected ? 0.0 : 1.0) : vec4(0.0);
        return;
    }
    //ai: inspect the actual sampled depth independently from projected coordinates and candidate ownership.
    if (hybridReprojectionDebug == 3) {
        finalColor = hybridPixel ? vec4(reprojectedDepth,expectedPrevDepth,
            abs(reprojectedDepth - expectedPrevDepth),hybridRejected ? 0.0 : 1.0) : vec4(0.0);
        return;
    }
    //ai: direct base-level metadata witness isolates texture/FBO identity from reprojection math.
    if (hybridReprojectionDebug == 4) {
        finalColor = vec4(textureLod(historyMap,uv,0.0).a,uv,1.0);
        return;
    }
    if (hybridRejected) {
        finalColor = vec4(current,depth);
        return;
    }

    // Early out: no history for first frame or static pixels with no history
    vec3 history = texture(historyMap, uv).rgb;
    float histLen = length(history);
    if (histLen < 0.001) {
        finalColor = vec4(current, depth);
        return;
    }

    // Reproject: sample history at corrected UV (de-jittered previous frame)
    //   historyUV = uv - velocity = prevUV + (jitter - prevJitter) * 0.5
    historyUV = clamp(historyUV, 0.001, 0.999);
    vec3 historySample = hybridPixel ? hybridHistory.rgb : texture(historyMap, historyUV).rgb;

    //opencode: v0.1.38 - disocclusion rejection via depth delta: when the
    //   reprojected sample's depth differs from this pixel's depth the
    //   history belongs to other geometry.
    //opencode: prev-depth disocclusion — history alpha carries LAST frame's
    //   raw depth (written below), which is the correct comparison quantity.
    //   The old current-depthMap sample answered a different question (what
    //   is behind the reprojected UV NOW). Uniform for both velocity paths;
    //   the depth<0.999 gate is gone (sky-vs-sky keeps, sky-vs-geometry
    //   rejects — both correct). Half-float alpha resolves ~0.001 against
    //   the 0.02 threshold with margin.
    bool rejected = false;
    float histDepth = texture(historyMap, historyUV).a;
    if (!hybridPixel && abs(histDepth - depth) > 0.02) rejected = true;
    if (!rejected && length(historySample) < 0.001) rejected = true;

    if (rejected) {
        finalColor = vec4(current, depth);
        return;
    }

    // Variance-aware blend: reduce history weight when motion is high
    //   (0.5 floor kept: 0.1 was tested 2026-09-15 and did not remove the
    //   motion trails - they need hit-motion (Eto 2023 dual-source), not
    //   a lower floor).
    float velLen = length(motionVelocity * resolution);
    float motionWeight = exp(-velLen * 2.0);
    float weight = mix(0.5, blendFactor, motionWeight);
    //opencode: static pixels trust the converged history harder. The EMA's
    //   current-sample term (1-weight) is exactly what shows the jitter phase
    //   as ±2-8 level shimmer on high-variance content (neon edges, mirror
    //   floor); the running-average oracle proves convergence is stable at
    //   high weights. Moving pixels keep the motion-adaptive weight, and the
    //   oracle keeps its own running-average weight (unbiasedness tests).
    if (oracleAccum == 0 && motionWeight > 0.9 && weight < 0.95) weight = 0.95;

    // Neighborhood clamping: clamp history to current frame's local range
    //   (skipped for oracle accumulation - see oracleAccum declaration).
    //opencode: static-pixel clamp bypass (2026-09-15): with per-frame SSR
    //   jitter the current frame's 3x3 box is not a validity range for the
    //   history mean; clamping ratchets the churn through instead of
    //   converging it (measured 11 levels/frame on the mirror). Static
    //   pixels (motionWeight~1) trust history directly; moving pixels keep
    //   the clamp. Disocclusion stays guarded by the velocity/depth reject.
    vec3 nMin = neighborhoodMin(colorMap, uv);
    vec3 nMax = neighborhoodMax(colorMap, uv);
    vec3 clampedHistory = (oracleAccum == 1 || motionWeight > 0.9)
        ? historySample : clamp(historySample, nMin, nMax);

    // Exponential moving average
    vec3 result = mix(current, clampedHistory, weight);

    //opencode: prev-depth disocclusion — depth rides the alpha channel into
    //   next frame's history (history rgb readers are unaffected).
    finalColor = vec4(result, depth);
}
