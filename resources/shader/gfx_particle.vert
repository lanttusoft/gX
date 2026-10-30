//opencode: particle vertex shader - reads position/color from SSBO binding 0.
//   Each vertex is a point sprite. gl_VertexID indexes into the particle array.
//   16 floats per particle: pos.xyz + life + vel.xyz + maxLife + color.rgba
//   Passes per-particle color as flat varying to fragment shader.
#version 430

layout(std430, binding = 0) buffer ParticleBuffer {
    float data[];
} particles;

uniform mat4  vp;
uniform vec3  cameraPos;
uniform float pointSize;

flat out vec4 vColor;

void main() {
    uint idx = uint(gl_VertexID);
    uint base = idx * 16u;

    float life = particles.data[base + 3u];
    if (life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        return;
    }

    vec3 pos = vec3(particles.data[base + 0u],
                    particles.data[base + 1u],
                    particles.data[base + 2u]);

    gl_PointSize = pointSize;
    gl_Position = vp * vec4(pos, 1.0);

    vColor = vec4(particles.data[base + 8u],
                  particles.data[base + 9u],
                  particles.data[base + 10u],
                  particles.data[base + 11u]);
}
