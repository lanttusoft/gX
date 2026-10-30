//opencode: screen-space motion blur from depth velocity
//   Reconstructs world position from depth + inverse VP, transforms by previous
//   frame's VP, and blurs along the resulting screen-space velocity vector.
//   Pure post-process - no vertex shader changes needed.
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform sampler2D depthMap;
uniform sampler2D velocityMap;
uniform mat4 prevVP;
uniform mat4 invVP;
uniform vec2 resolution;
uniform int samples = 16;
uniform float blurStrength = 1.0;
uniform int hasVelocityBuffer = 0;

out vec4 finalColor;

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    float depth = texture(depthMap, uv).r;
    
    //opencode: per-object motion — the velocity buffer carries model-aware
    //   UV deltas (same tap as the TAA resolve); unwritten scene pixels (B=0)
    //   use camera fallback, while clear sky keeps the historical no-blur
    //   behavior; glass pixels (alpha 0)
    //   and buffer-off configs fall back to depth reconstruction. Single
    //   assignments (NVIDIA DCE discipline: no uniform-gated reassignment).
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invVP * clipPos;
    worldPos /= worldPos.w;
    vec4 prevClip = prevVP * worldPos;
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
    vec2 velDepth = (uv - prevUV) * blurStrength;
    vec4 velTap = (hasVelocityBuffer == 1) ? texture(velocityMap, uv) : vec4(0.0);
    vec2 vel = (velTap.a > 0.5 && velTap.b > 0.5)
        ? (velTap.rg * blurStrength)
        : (depth < 0.999 ? velDepth : vec2(0.0));
    float velLen = length(vel);
    
    // Early-out: skip blur for static pixels (also guards against NaN from gimbal lock)
    vec3 color = texture(colorMap, uv).rgb;
    if (velLen < 0.0005 || isnan(velLen)) {
        finalColor = vec4(color, 1.0);
        return;
    }
    
    // Adaptive sample count based on velocity length
    int nSamples = min(samples, 16);
    nSamples = min(nSamples, max(4, int(velLen * 200.0)));
    float invSamples = 1.0 / float(nSamples);
    
    // Blur along velocity vector (asymmetric: sample more in direction of motion)
    vec3 accum = vec3(0.0);
    for (int i = 0; i < nSamples; i++) {
        float t = float(i) * invSamples;
        vec2 sampleUV = uv - vel * t * 0.5;
        sampleUV = clamp(sampleUV, 0.001, 0.999);
        accum += texture(colorMap, sampleUV).rgb;
    }
    accum *= invSamples;
    
    // Blend blurred with original based on velocity (preserves sharpness on slow objects)
    float blend = clamp(velLen * 5.0, 0.0, 1.0);
    color = mix(color, accum, blend);
    
    finalColor = vec4(color, 1.0);
}
