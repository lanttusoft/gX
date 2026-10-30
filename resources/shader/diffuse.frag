#version 330

//from vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord,fragTexCoord2;
in vec3 fragNormal;
in vec4 fragColor;
in vec3 fragTangent;

uniform float ambientFactor = 0.1;
uniform float colorMultiplier = 1.0;
uniform vec3 lightDir = vec3(0.0,0.0,-1.0);
uniform vec4 albedoColor = vec4(vec3(1.0),1.0);

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    vec4 color = vec4(vec3(0.0),1.0);
    
    color = albedoColor;
    color.rgb *= texture(texture0,fragTexCoord).rgb; //map
    float ambient = ambientFactor;
    float diffuse = max(dot(normalize(fragNormal),normalize(lightDir)),0.0);
    color.rgb *= ambient + diffuse;
    color.rgb *= colorMultiplier;
    
    finalColor = color;
}
