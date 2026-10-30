//opencode: Paper 2 spike 3 — camera-motion vectors for non-model pixels.
//   Depth-reprojection quad (same math as the TAA depth fallback, baked
//   into the buffer): runs right after the velocity clear, models paint
//   over it with object motion. Unjittered VPs both frames, matching the
//   model path convention (cuv - puv). Sky reprojects as-is (far plane),
//   same as the fallback would compute. MSAA-gated off in C (multisample
//   depth is unsampleable); hook-gated with the terrain pass.
//ai: valid previous projected depths use B=2+depth; out-of-range depth uses B=3.5.
#version 330

in vec2 fragTexCoord;

uniform sampler2D depthMap;
uniform mat4 invVP;
uniform mat4 prevVP;
uniform vec2 resolution;

out vec4 fragColor;

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up (all post shaders)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    float rawDepth = texture(depthMap, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, rawDepth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invVP * clipPos;
    if (abs(worldPos.w) <= 1e-5) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    worldPos /= worldPos.w;
    vec4 prevClip = prevVP * worldPos;
    if (prevClip.w <= 1e-5) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
    float prevDepth = prevClip.z / prevClip.w * 0.5 + 0.5;
    float depthPayload = (prevClip.w > 1e-5 && prevDepth >= 0.0 && prevDepth <= 1.0)
        ? 2.0 + prevDepth : 3.5;
    fragColor = vec4(uv - prevUV, depthPayload, 1.0);
}
