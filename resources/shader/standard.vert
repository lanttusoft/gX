//opencode: primary vertex shader - replaces gfx.vert, all uniforms as plain for raylib compat
#version 330

//opencode: custom model matrix - uploaded manually from C, NOT overwritten by raylib's rlgl batch flush
uniform mat4 gX_ModelMatrix;

//gX_STANDARD_UNIFORMS

//opencode: view/projection matrices for hardware instancing MVP computation.
//   Uploaded from C in shader.c SetValues (matViewLoc, matProjectionLoc).
uniform mat4 matView;
uniform mat4 matProjection;

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
//ai: independent GLB COLOR_1 scalar mask; disabled materials never consume it.
layout(location = 9) in float vertexTerrainMask;
out float terrainMask;
//opencode: vec4 (not vec3) - raylib 6.0 binds vertexTangent as vec4 with w = bitangent
//   handedness sign. Dropping w scrambles TBN on meshes with mirrored UVs.
in vec4 vertexTangent;
//opencode: GPU skinning — bone attributes plus the per-model bone matrices
//   the renderer uploads before the mesh loop. Names and locations match
//   raylib's defaults (vertexBoneIndices=7, vertexBoneWeights=8); the
//   per-mesh switch below keeps unskinned meshes on the plain path.
layout(location = 7) in vec4 vertexBoneIndices;
layout(location = 8) in vec4 vertexBoneWeights;
uniform mat4 boneMatrices[128];
//opencode: per-mesh switch (the backend sets it at draw time) — generic vertex
//   attributes default to (0,0,0,1), which would otherwise skin unskinned
//   meshes by bone 0.
uniform int gX_UseSkinning = 0;

uniform int enableGouraud = 0;
uniform vec4 sunColor, sunDiffuseTint, sunSpecularTint;
uniform float sunBrightness, sunDiffuseFactor, sunSpecularFactor;
//opencode: per-model material uniforms (replaces former UBO - glGetUniformBlockIndex
//   returns INVALID_INDEX on this GL config for the gX_StdModel block. The UBO
//   C infrastructure (struct, buffer, upload) remains in place for future use
//   when the shader can bind it.)
uniform float albedoFactor, metalnessFactor;
uniform float modelShininess;

//opencode: TAA sub-pixel jitter - offsets clip-space position for temporal accumulation
uniform vec2 taaJitter = vec2(0.0);

//opencode: tangent threshold uniforms (configurable from C)
uniform float tangentLengthThreshold = 0.001;
uniform float normalYThreshold = 0.999;

//opencode: flat shading - non-interpolated normal from provoking vertex
flat out vec3 flatNormal;

//opencode: non-planar reflection demo - procedural wave displacement.
//   waveAmplitude > 0 displaces the vertex in world Y (sin(x)*sin(z) field)
//   and recomputes the world-space normal from the analytic gradient, so
//   lighting AND the per-fragment reflection plane both follow the waves.
//   NOTE: assumes an axis-aligned horizontal surface (world-space Y waves).
uniform float waveAmplitude = 0.0;
uniform float gxTime = 0.0;

//opencode: instancing - uniform array for per-instance model matrices.
//   When instanceCount > 0, use instanceMatrices[gl_InstanceID] instead of gX_ModelMatrix.
uniform int instanceCount;
#define MAX_INSTANCES 64
uniform mat4 instanceMatrices[MAX_INSTANCES];

out VS_OUT {
    vec3  fragPosition;
    vec2  fragTexCoord;
    vec2  fragTexCoord2;
    vec3  fragNormal;
    vec3  fragTangent;
    vec4  fragColor;
    float fragDistance;
    mat3  matTNB;
    vec3  tangentLightPosition;
    vec3  tangentViewPosition;
    vec3  tangentFragPosition;
    vec4  lightspaceFragPosition;
    vec3  gouraudLight;
} vs_out;

void main() {
    //opencode: use gX_ModelMatrix (raylib overwrites matModel with rlgl MODELVIEW; this custom
    //   uniform is uploaded manually and not touched by raylib's batch flush)
    //   When instancing is active (instanceCount > 0), read from the instance SSBO instead.
    mat4 modelMat = gX_ModelMatrix;
    if (instanceCount > 0) modelMat = instanceMatrices[gl_InstanceID];
    mat3 normalMat = transpose(inverse(mat3(modelMat)));

    //opencode: GPU skinning — blend the bone matrices when the mesh carries
    //   weights (unskinned meshes skip this entirely).
    vec3 localPosition = vertexPosition;
    vec3 localNormal = vertexNormal;
    //opencode: skin only when the drawn mesh carries bone data.
    float boneWeightSum = dot(vertexBoneWeights, vec4(1.0));
    if (gX_UseSkinning == 1 && boneWeightSum > 0.0) {
        mat4 skin = mat4(0.0);
        for (int i = 0; i < 4; i++) {
            float weight = vertexBoneWeights[i];
            if (weight <= 0.0) continue;
            skin += boneMatrices[int(vertexBoneIndices[i])] * weight;
        }
        localPosition = (skin * vec4(vertexPosition, 1.0)).xyz;
        localNormal = mat3(skin) * vertexNormal;
    }

    vs_out.fragPosition    = vec3(modelMat * vec4(localPosition, 1.0));
    vs_out.fragTexCoord    = vertexTexCoord;
    vs_out.fragTexCoord2   = vertexTexCoord2;
    vs_out.fragNormal      = normalize(normalMat * localNormal);
    flatNormal             = vs_out.fragNormal;
    vs_out.fragTangent     = vertexTangent.xyz;
    vs_out.fragColor       = vertexColor;
    terrainMask = vertexTerrainMask;

    vec3 normal = normalize(normalMat * localNormal);
    //opencode: wave displacement - displace AFTER the world position, override
    //   the normal with the analytic gradient so the TNB/tangent path below
    //   uses the true displaced surface orientation.
    if (waveAmplitude > 0.001) {
        float ph = vs_out.fragPosition.x * 2.0 + gxTime * 3.0;
        float pz = vs_out.fragPosition.z * 2.0 + gxTime * 3.0;
        vs_out.fragPosition.y += waveAmplitude * sin(ph) * sin(pz);
        float ddx = waveAmplitude * 2.0 * cos(ph) * sin(pz);
        float ddz = waveAmplitude * 2.0 * sin(ph) * cos(pz);
        normal = normalize(vec3(-ddx, 1.0, -ddz));
        vs_out.fragNormal = normal;
        flatNormal = normal;
    }
    vec3 tangent;
    float tangentDirLen = length(vertexTangent.xyz);
    float handedness = vertexTangent.w < 0.0 ? -1.0 : 1.0;
    //opencode: NaN-tangent fallback (M5) - degenerate UV triangles yield a
    //   zero UV-area direction, and orthonormalizing zero against the normal
    //   divides 0/0 = NaN. NaN length compares false, so without this the
    //   NaN flows into matTNB and poisons lighting (only the frame-level
    //   sanitize caught it, as black pixels). Analytic tangent instead.
    if (tangentDirLen < tangentLengthThreshold || any(isnan(vertexTangent.xyz))) {
        vec3 ref = abs(normal.y) < normalYThreshold ? vec3(0.0,1.0,0.0) : vec3(0.0,0.0,1.0);
        tangent = normalize(cross(ref, normal));
    } else {
        tangent = normalize(normalMat * vertexTangent.xyz);
        tangent = normalize(tangent - dot(tangent, normal) * normal);
    }
    vec3 bitangent = cross(normal, tangent) * handedness;
    vs_out.matTNB = transpose(mat3(tangent, normal, bitangent));
    vs_out.tangentLightPosition = vs_out.matTNB * lightPosition;
    vs_out.tangentViewPosition  = vs_out.matTNB * viewPosition;
    vs_out.tangentFragPosition  = vs_out.matTNB * vs_out.fragPosition;

    vs_out.lightspaceFragPosition = matLightSpace * vec4(vs_out.fragPosition, 1.0);

    if (enableGouraud == 1) {
        vec3 N = normalize(normalMat * localNormal);
        vec3 L = normalize(lightPosition);
        vec3 V = normalize(viewPosition - vs_out.fragPosition);
        vec3 R = reflect(-L, N);
        vec3 ambient  = ambientFactor * ambientColor.rgb;
        vec3 diffuse  = albedoFactor * sunBrightness * sunColor.rgb * max(dot(N, L), 0.0) * sunDiffuseFactor * sunDiffuseTint.rgb;
        vec3 specular = metalnessFactor * sunBrightness * sunColor.rgb * pow(max(dot(V, R), 0.0), modelShininess) * sunSpecularFactor * sunSpecularTint.rgb;
        vs_out.gouraudLight = ambient + diffuse + specular;
    } else {
        vs_out.gouraudLight = vec3(1.0);
    }

    //opencode: compute clip position from separate view/projection/model matrices.
    //   The GFX renderer uploads matView/matProjection (per-frame) and matModel
    //   (per-model). This avoids the legacy mvp uniform convention.
    vec4 clipPos = matProjection * matView * modelMat * vec4(localPosition, 1.0);
    gl_Position = clipPos;
    //opencode: TAA jitter - sub-pixel offset in screen space
    gl_Position.xy += taaJitter * gl_Position.w;
    vs_out.fragDistance = length(vs_out.fragPosition - viewPosition);
}
