//opencode: Gerstner wave + ripple vertex displacement for water surface
#version 330

//gX_STANDARD_UNIFORMS

uniform mat4 matView;
uniform mat4 matProjection;

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexColor;

uniform int   waveCount;
uniform vec2  waveDirs[6];
uniform float waveAmps[6];
uniform float waveFreqs[6];
uniform float waveSpeeds[6];
uniform float waveSteeps[6];

uniform int   rippleCount;
uniform vec2  ripplePos[16];
uniform float rippleAmp[16];
uniform float rippleRadius[16];
uniform float ripplePhase[16];

uniform float time;
uniform int   velocityMode = 0;
uniform mat4  velocityCurrVP;
uniform mat4  velocityPrevVP;
uniform float velocityPrevTime;

out vec2  fragTexCoord;
out vec3  fragNormal;
out vec3  fragPosition;
out float fragWaveHeight;
out float fragEdgeDist;   // distance to nearest plane edge, for shore foam
out vec2   vVel;           //opencode: velocity-mode clip delta in UV space

// Gerstner wave contribution
void gerstnerWave(vec2 dir, float amp, float freq, float speed, float steep,
                  vec3 pos, float t,
                  out float dy, out vec2 dxz)
{
    float theta = freq * dot(dir, pos.xz) + t * speed;
    float s = sin(theta);
    float c = cos(theta);
    float Qi = max(steep / max(freq * amp * float(waveCount), 0.001), 0.0);
    Qi = min(Qi, 0.85);

    dy = amp * s;
    dxz = dir * Qi * amp * c;
}

vec3 displacedPosition(vec3 base, float t)
{
    vec3 pos = base;
    float totalY = 0.0;
    vec2 totalHoriz = vec2(0.0);
    for (int i = 0; i < waveCount; i++)
    {
        float dy;
        vec2 dxz;
        gerstnerWave(waveDirs[i], waveAmps[i], waveFreqs[i], waveSpeeds[i],
            waveSteeps[i], pos, t, dy, dxz);
        totalY += dy;
        totalHoriz += dxz;
    }
    pos.xz += totalHoriz;
    for (int i = 0; i < rippleCount; i++)
    {
        vec2 delta = pos.xz - ripplePos[i];
        float dist = length(delta);
        if (dist < rippleRadius[i])
        {
            float r = dist / rippleRadius[i];
            totalY += rippleAmp[i] * sin(dist * 3.0 - ripplePhase[i]) * (1.0 - r * r);
        }
    }
    pos.y += totalY;
    return pos;
}

void main()
{
    //opencode: velocity replay evaluates the exact displaced surface twice;
    //   the ordinary water shading path below remains unchanged.
    if (velocityMode == 1)
    {
        vec3 currPos = displacedPosition(vertexPosition, time);
        vec3 prevPos = displacedPosition(vertexPosition, velocityPrevTime);
        vec4 currWorld = matModel * vec4(currPos, 1.0);
        vec4 prevWorld = matModel * vec4(prevPos, 1.0);
        vec4 currClip = velocityCurrVP * currWorld;
        vec4 prevClip = velocityPrevVP * prevWorld;
        vec2 currNdc = currClip.xy / currClip.w;
        vec2 prevNdc = prevClip.xy / prevClip.w;
        gl_Position = currClip;
        vVel = (currNdc - prevNdc) * 0.5;
        return;
    }
    vec3 pos = vertexPosition;

    // Gerstner wave sum
    float totalY = 0.0;
    vec2 totalHoriz = vec2(0.0);

    // Analytical normal accumulation: we compute dP/dx and dP/dz
    vec3 tangent = vec3(1.0, 0.0, 0.0);  // dP/dx
    vec3 bitangent = vec3(0.0, 0.0, 1.0); // dP/dz

    for (int i = 0; i < waveCount; i++)
    {
        float dy;
        vec2 dxz;
        gerstnerWave(waveDirs[i], waveAmps[i], waveFreqs[i], waveSpeeds[i], waveSteeps[i],
                     pos, time, dy, dxz);
        totalY += dy;
        totalHoriz += dxz;

        // Wave normal contribution to tangent/bitangent
        float theta = waveFreqs[i] * dot(waveDirs[i], pos.xz) + time * waveSpeeds[i];
        float c = cos(theta);
        float Qi = max(waveSteeps[i] / max(waveFreqs[i] * waveAmps[i] * float(waveCount), 0.001), 0.0);
        Qi = min(Qi, 0.85);
        float WA = waveFreqs[i] * waveAmps[i];
        float S = sin(theta);

        // dP/dx contribution
        tangent.x -= Qi * waveDirs[i].x * waveDirs[i].x * WA * S;
        tangent.y += waveDirs[i].x * WA * c;
        tangent.z -= Qi * waveDirs[i].x * waveDirs[i].y * WA * S;

        // dP/dz contribution
        bitangent.x -= Qi * waveDirs[i].x * waveDirs[i].y * WA * S;
        bitangent.y += waveDirs[i].y * WA * c;
        bitangent.z -= Qi * waveDirs[i].y * waveDirs[i].y * WA * S;
    }

    pos.x += totalHoriz.x;
    pos.z += totalHoriz.y;

    // Ripple sum
    for (int i = 0; i < rippleCount; i++)
    {
        vec2 delta = pos.xz - ripplePos[i];
        float dist = length(delta);
        if (dist < rippleRadius[i])
        {
            float r = dist / rippleRadius[i];
            float envelope = 1.0 - r * r;  // parabolic falloff
            float wave = sin(dist * 3.0 - ripplePhase[i]);
            float ripple = rippleAmp[i] * wave * envelope;
            totalY += ripple;

            // Ripple normal contribution (derivative of ripple height)
            float dr = 1.0 / max(rippleRadius[i], 0.001);
            float dEnvelope = -2.0 * r * dr;
            float dWave = cos(dist * 3.0 - ripplePhase[i]) * 3.0;
            float dRipple_dDist = rippleAmp[i] * (dWave * envelope + wave * dEnvelope);
            vec2 rippleDir = delta / max(dist, 0.001);

            // Add to tangent/bitangent
            tangent.y += dRipple_dDist * rippleDir.x;
            bitangent.y += dRipple_dDist * rippleDir.y;
        }
    }

    pos.y += totalY;

    // Transform to world space
    vec4 worldPos = matModel * vec4(pos, 1.0);

    // Compute world-space normal from tangent/bitangent
    vec3 worldTangent = normalize(mat3(matModel) * tangent);
    vec3 worldBitangent = normalize(mat3(matModel) * bitangent);
    vec3 worldNormal = normalize(cross(worldTangent, worldBitangent));

    gl_Position = matProjection * matView * worldPos;

    fragTexCoord  = vertexTexCoord;
    fragNormal    = worldNormal;
    fragPosition  = worldPos.xyz;
    fragWaveHeight = totalY;

    //opencode: edge distance for shore foam - distance from nearest plane edge
    float halfSize = 20.0;  // GX_WATER_PLANE_SIZE_X / 2
    float dx = abs(pos.x);
    float dz = abs(pos.z);
    fragEdgeDist = min(halfSize - dx, halfSize - dz);
    fragEdgeDist = clamp(fragEdgeDist, 0.0, 2.0);  // margin = 2.0
}
