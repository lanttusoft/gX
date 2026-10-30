#version 330
//opencode: debug normal lines vertex shader - pass through for geometry shader.
//   Outputs position, normal, tangent for each vertex.

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexColor;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

out VS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec3 fragTangent;
    vec2 fragTexCoord;
} gs_out;

void main() {
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    gs_out.fragPosition = worldPos.xyz;
    gs_out.fragNormal = normalize(mat3(matModel) * vertexNormal);
    gs_out.fragTexCoord = vertexTexCoord;

    // Approximate tangent from normal (for debug purposes)
    vec3 up = abs(vertexNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    gs_out.fragTangent = normalize(cross(up, vertexNormal));

    gl_Position = matProjection * matView * worldPos;
}
