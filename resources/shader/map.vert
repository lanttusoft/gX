#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 viewProj;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform float curvature = 0.0001;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec4 fragColor;

void main()
{
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);

    // Apply curvature to world-space Y
    float dist = length(worldPos.xz);
    worldPos.y -= curvature * dist * dist;

    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));

    // Project curved world position through view-projection
    gl_Position = viewProj * worldPos;
}
