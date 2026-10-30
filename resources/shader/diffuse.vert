#version 330

//from vertex data
in vec3 vertexPosition;
in vec2 vertexTexCoord,vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec3 vertexTangent;

//matrices
uniform mat4 mvp,matView,matModel,matProjection,matNormal;

//to frag shader
out vec3 fragPosition;
out vec2 fragTexCoord,fragTexCoord2;
out vec3 fragNormal;
out vec3 fragTangent;
out vec4 fragColor;

void main() {
    fragPosition    = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord    = vertexTexCoord;
    fragTexCoord2   = vertexTexCoord2;
    fragNormal      = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    fragTangent     = vertexTangent;
    fragColor       = vertexColor;
    
    gl_Position = mvp*vec4(vertexPosition, 1.0);
} 
