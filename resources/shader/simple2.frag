#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;
in vec3 viewPosition;

uniform vec2 res;
uniform vec3 replaceColor;
out vec4 finalColor;

void main() {
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy/res-screencenter);
    
    vec3 lightdir = vec3(1.0,0.5,0.25);
    vec3 xTangent = dFdx( viewPosition );
    vec3 yTangent = dFdy( viewPosition );
    vec3 faceNormal = normalize( cross( xTangent, yTangent ) );
    
    float shade = 0.5*(dot(lightdir,faceNormal)+1);
    vec3 color = shade*replaceColor;
    
    color += 0.5*shade*replaceColor;
    
    finalColor = vec4(color,1.0);
} 
