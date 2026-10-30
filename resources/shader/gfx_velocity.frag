#version 330
#extension GL_ARB_explicit_uniform_location : require
//opencode: v0.1.38 - velocity buffer fragment stage.
//   RG carries the per-pixel screen-space motion (UV delta between the
//   current and previous frame). B remains >0.5 for written vectors and
//   carries 2+previous depth for raster geometry; A remains the glass flag
//   consumed by SSR (1 opaque, 0 transparent).
//ai: previous-depth values outside [0,1] use B=3.5; B=1 remains reserved for legacy writers.

//opencode: divide interpolated clip coordinates per fragment. The smooth
//   varying interpolation then tracks the actual surface point on each pixel.
in vec4 vCurrClip;
in vec4 vPrevClip;
out vec4 fragColor;

//opencode: M1-b — glass flag in .w (1 opaque, 0 transparent): the SSR
//   march skips transparent origins (background depth would mirror the
//   background, not the glass) and takes the env fallback instead.
//   A is cleared to 1 with the buffer for the opaque/background SSR contract;
//   .rg consumers (TAA/SSGI) use .b to reject unwritten pixels instead of
//   mistaking clear-zero motion for valid motion.
//opencode: explicit base (see .vert): outside all mat4 spans.
layout(location = 16) uniform float uGlass = 1.0;

//opencode: guard degenerate clip w (behind-camera points clamp to their edge).
void main()
{
    float currW = (abs(vCurrClip.w) > 1e-5) ? vCurrClip.w : 1e-5;
    float prevW = (abs(vPrevClip.w) > 1e-5) ? vPrevClip.w : 1e-5;
    vec2 currUV = vCurrClip.xy / currW * 0.5 + 0.5;
    vec2 prevUV = vPrevClip.xy / prevW * 0.5 + 0.5;
    float prevDepth = vPrevClip.z / prevW * 0.5 + 0.5;
    float depthPayload = (vPrevClip.w > 1e-5 && prevDepth >= 0.0 && prevDepth <= 1.0)
        ? 2.0 + prevDepth : 3.5;
    fragColor = vec4(currUV - prevUV, depthPayload, uGlass);
}
