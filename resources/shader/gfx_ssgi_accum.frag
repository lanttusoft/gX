#version 330
//opencode: v0.3-M4 Phase2 - SSGI temporal accumulation. Reprojects through
//   the velocity buffer, validates history with a neighborhood min/max clip
//   against the current frame (anti-ghosting), and blends 80/20.

in vec2 fragTexCoord;

uniform sampler2D currentMap;   // spatially denoised gather
uniform sampler2D historyMap;   // previous accumulated output
//ai: CPU lifecycle state gates history sampling after a reset or preset change.
uniform int   historyValid = 0;
uniform sampler2D velocityMap;  // .rg motion, .b written/valid (unit 21)
uniform sampler2D depthMap;
uniform int   hasVelocity = 0;
uniform vec2  resolution;

out vec4 finalColor;

float getDepth(vec2 uv) { return textureLod(depthMap, uv, 0.0).r; }

//ai: invalid history always resolves to the current spatial gather for this frame.
void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec3 cur = textureLod(currentMap, uv, 0.0).rgb;

    //opencode: neighborhood clamp bounds (3x3 of current)
    vec3 mn = cur, mx = cur;
    vec2 px = 1.0 / resolution;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 s = textureLod(currentMap, uv + vec2(px.x*x, px.y*y), 0.0).rgb;
            mn = min(mn, s); mx = max(mx, s);
        }
    }
    //opencode: v0.3.5-polish - clip pad 0.02 -> 0.06: broad GI gradients
    //   exceeded the old padding so history got clipped every frame and the
    //   accumulated map lost half its bounce energy
    vec3 bounds = (mx - mn) + vec3(0.06);

    vec4 velocityTap = textureLod(velocityMap, uv, 0.0);
    bool velocityValid = hasVelocity == 1 && velocityTap.b > 0.5;
    vec2 histUv = uv;
    if (velocityValid)
        histUv = uv - velocityTap.rg;

    //opencode: with a velocity RT, an unwritten pixel has no trustworthy
    //   reprojection; use the current gather rather than stale history.
    bool ok = (hasVelocity != 1 || velocityValid)
        && (histUv.x >= 0.0 && histUv.x <= 1.0 && histUv.y >= 0.0 && histUv.y <= 1.0);
    vec3 acc = cur;
    if (historyValid == 1 && ok) {
        vec3 hist = textureLod(historyMap, histUv, 0.0).rgb;
        //opencode: variance clip - reject history outside local bounds
        vec3 clipped = clamp(hist, mn - bounds, mx + bounds);
        //opencode: depth reject - big velocity means disocclusion risk
        float velLen = velocityValid ? length(velocityTap.rg) : 0.0;
        //opencode: v0.3.5-polish - smoothstep blend band replaces the binary
        //   0.15 jump that popped at silhouettes whenever velocity crossed it
        float blend = mix(0.85, 0.30, smoothstep(0.05, 0.25, velLen));
        acc = mix(cur, clipped, blend);
    }

    finalColor = vec4(acc, 1.0);
}
