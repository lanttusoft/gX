#version 330
//opencode: v0.1.33 - minimal planar-reflection debug shader (pipeline proof)
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
uniform mat4 matView;
uniform mat4 matProjection;
uniform mat4 gX_ModelMatrix;
out vec3 vNormal;
void main() {
    vNormal = vertexNormal;
    gl_Position = matProjection * matView * gX_ModelMatrix * vec4(vertexPosition, 1.0);
}
