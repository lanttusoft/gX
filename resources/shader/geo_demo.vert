#version 330
//opencode: geometry shader demo - vertex shader
//   Passes position (model-space), normal, texcoord to geometry stage.

layout (location = 0) in vec3 vertexPosition;
layout (location = 2) in vec3 vertexNormal;
layout (location = 1) in vec2 vertexTexCoord;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

out VS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec2 fragTexCoord;
} vs_out;

void main() {
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    vs_out.fragPosition  = worldPos.xyz;
    vs_out.fragNormal    = normalize(mat3(matModel) * vertexNormal);
    vs_out.fragTexCoord  = vertexTexCoord;
    gl_Position = matProjection * matView * worldPos;
}
