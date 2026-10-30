#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexTangent;
in vec2 vertexTexCoord2;
//opencode: GPU skinning — same contract as standard.vert (raylib attribute
//   locations 7/8, per-mesh switch set by the backend at mesh draw time).
layout(location = 7) in vec4 vertexBoneIndices;
layout(location = 8) in vec4 vertexBoneWeights;
uniform mat4 boneMatrices[128];
uniform int gX_UseSkinning = 0;

uniform mat4 matModelDepth;
uniform mat4 matLightSpace;

void main() {
    vec3 localPosition = vertexPosition;
    float boneWeightSum = dot(vertexBoneWeights, vec4(1.0));
    if (gX_UseSkinning == 1 && boneWeightSum > 0.0) {
        mat4 skin = mat4(0.0);
        for (int i = 0; i < 4; i++) {
            float weight = vertexBoneWeights[i];
            if (weight <= 0.0) continue;
            skin += boneMatrices[int(vertexBoneIndices[i])] * weight;
        }
        localPosition = (skin * vec4(vertexPosition, 1.0)).xyz;
    }
    gl_Position = matLightSpace * matModelDepth * vec4(localPosition, 1.0);
}
 
 
