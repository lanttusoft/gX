#version 330
#extension GL_ARB_explicit_uniform_location : require
//opencode: v0.1.38 - velocity buffer vertex stage (per-pixel motion vectors).
//   Carries current/previous clip coordinates to the fragment stage so their
//   perspective divide happens per pixel rather than before interpolation.
//   Halton jitter is applied by standard.vert at render time and is
//   compensated explicitly by consumers (taa resolve subtracts
//   0.5*(jitter-prevJitter)).
//opencode: explicit uniform bases: the linker assigned dense bases
//   (0,1,2,4) for 4xmat4+float, which overlap under consecutive mat4
//   packing and varied run to run (motion smear came and went with the
//   packing). Non-overlapping bases remove the whole class by
//   construction; C queries pick them up automatically.

in vec3 vertexPosition;

layout(location = 0) uniform mat4 matCurrVP;      //opencode: current unjittered view-projection
layout(location = 4) uniform mat4 matPrevVP;      //opencode: previous frame unjittered VP
layout(location = 8) uniform mat4 gX_ModelMatrix;
layout(location = 12) uniform mat4 uPrevModel;     //opencode: previous frame model transform

//opencode: clip varyings preserve the perspective-correct surface point across
//   large triangles; interpolating already-divided UV deltas creates false flow.
out vec4 vCurrClip;
out vec4 vPrevClip;

void main()
{
    vec4 worldCurr = gX_ModelMatrix * vec4(vertexPosition, 1.0);
    vec4 clipCurr = matCurrVP * worldCurr;
    vec4 clipPrev = matPrevVP * uPrevModel * vec4(vertexPosition, 1.0);

    gl_Position = clipCurr;

    vCurrClip = clipCurr;
    vPrevClip = clipPrev;
}
