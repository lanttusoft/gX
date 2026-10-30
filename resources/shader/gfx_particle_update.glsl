//opencode: particle update compute shader — GL 4.3+
//   Each invocation updates one particle. SSBO layout (16 floats per particle):
//     posX, posY, posZ, life       // 0-3
//     velX, velY, velZ, maxLife     // 4-7
//     colR, colG, colB, colA        // 8-11
//     prevX, prevY, prevZ, pad      // 12-15
//   Supports per-emitter ranges via emitOffset + emitCount uniforms.
//   Particles outside the emitter range are left unchanged (not respawned).
#version 430

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer ParticleBuffer {
    float data[];
} particles;

uniform float dt;
uniform vec3  emitPos;
uniform vec3  emitBaseVel;
uniform float emitSpread;     // spread in radians
uniform vec4  emitColor;
uniform float emitLife;
uniform float emitSpeed;      // speed multiplier
uniform int   maxCount;       // total particles in buffer
uniform int   emitOffset;     // first particle index for this emitter
uniform int   emitCount;      // number of particles for this emitter
uniform float emitRate;       // particles per second (0 = dormant, no respawn)
uniform float time;

float hash1(uint seed) {
    uint h = seed * 747796405u + 2891336453u;
    h = ((h >> 16) ^ h) * 747796405u;
    h = ((h >> 16) ^ h) * 2891336453u;
    return float(h) / 4294967295.0;
}

vec2 hash2(uint seed) {
    return vec2(hash1(seed), hash1(seed + 12345u));
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(emitOffset + emitCount) || idx < uint(emitOffset)) return;
    if (idx >= uint(maxCount)) return;

    uint base = idx * 16u;

    float life = particles.data[base + 3u];

    if (life > 0.0) {
        // Alive — physics update
        float px = particles.data[base + 0u];
        float py = particles.data[base + 1u];
        float pz = particles.data[base + 2u];
        float vx = particles.data[base + 4u];
        float vy = particles.data[base + 5u];
        float vz = particles.data[base + 6u];

        particles.data[base + 12u] = px;
        particles.data[base + 13u] = py;
        particles.data[base + 14u] = pz;

        vy += -9.8 * dt; // gravity
        px += vx * dt;
        py += vy * dt;
        pz += vz * dt;

        life -= dt;

        particles.data[base + 0u] = px;
        particles.data[base + 1u] = py;
        particles.data[base + 2u] = pz;
        particles.data[base + 3u] = life;
        particles.data[base + 4u] = vx;
        particles.data[base + 5u] = vy;
        particles.data[base + 6u] = vz;

        // Fade alpha
        float maxLife = particles.data[base + 7u];
        float age = 1.0 - life / maxLife;
        particles.data[base + 11u] = clamp(1.0 - age, 0.0, 1.0);
    } else {
        // Dead — respawn with emitter params (rate-limited by emitRate*dt,
        //   so emission is throttled instead of instant respawn of every
        //   dead particle every frame; emitRate=0 leaves them dead =
        //   dormant emitter fade-out)
        uint seed = idx * 7919u + uint(time * 1000.0);
        vec2 r = hash2(seed);
        float emitChance = emitRate * dt;
        if (hash1(seed + 4242u) >= emitChance) {
            // stay dead — emit alpha 0
            particles.data[base + 11u] = 0.0;
            return;
        }

        vec3 baseDir = normalize(emitBaseVel);
        vec3 up = abs(baseDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 right = normalize(cross(up, baseDir));
        vec3 fwd = cross(baseDir, right);

        float spreadAngle = emitSpread * r.x;
        float phi = r.y * 6.283185;
        float sinA = sin(spreadAngle);
        vec3 dir = normalize(baseDir + (right * cos(phi) + fwd * sin(phi)) * sinA);

        float speed = emitSpeed * (0.5 + hash1(seed + 777u));

        particles.data[base + 0u]  = emitPos.x;
        particles.data[base + 1u]  = emitPos.y;
        particles.data[base + 2u]  = emitPos.z;
        particles.data[base + 12u] = emitPos.x;
        particles.data[base + 13u] = emitPos.y;
        particles.data[base + 14u] = emitPos.z;
        particles.data[base + 3u]  = emitLife;
        particles.data[base + 4u]  = dir.x * speed;
        particles.data[base + 5u]  = dir.y * speed;
        particles.data[base + 6u]  = dir.z * speed;
        particles.data[base + 7u]  = emitLife;
        particles.data[base + 8u]  = emitColor.r;
        particles.data[base + 9u]  = emitColor.g;
        particles.data[base + 10u] = emitColor.b;
        particles.data[base + 11u] = emitColor.a;
    }
}
