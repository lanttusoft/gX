//opencode: particle velocity vertex shader — reprojects current and previous
//   SSBO positions into the unjittered camera spaces used by the velocity pass.
#version 430

layout(std430, binding = 0) buffer ParticleBuffer {
    float data[];
} particles;

layout(location = 0) uniform mat4 matCurrVP;
layout(location = 4) uniform mat4 matPrevVP;
layout(location = 8) uniform float pointSize;

layout(location = 0) out vec2 vVel;

void main()
{
    uint idx = uint(gl_VertexID);
    uint base = idx * 16u;
    float life = particles.data[base + 3u];
    if (life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        vVel = vec2(0.0);
        return;
    }

    vec3 current = vec3(particles.data[base + 0u],
                        particles.data[base + 1u],
                        particles.data[base + 2u]);
    vec3 previous = vec3(particles.data[base + 12u],
                         particles.data[base + 13u],
                         particles.data[base + 14u]);
    vec4 currClip = matCurrVP * vec4(current, 1.0);
    vec4 prevClip = matPrevVP * vec4(previous, 1.0);
    gl_Position = currClip;
    gl_PointSize = pointSize;

    float currW = (abs(currClip.w) > 1e-5) ? currClip.w : 1e-5;
    float prevW = (abs(prevClip.w) > 1e-5) ? prevClip.w : 1e-5;
    vec2 currUV = currClip.xy / currW * 0.5 + 0.5;
    vec2 prevUV = prevClip.xy / prevW * 0.5 + 0.5;
    vVel = currUV - prevUV;
}
