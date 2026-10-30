#version 330

in vec2 fragTexCoord; //opencode: use fragTexCoord (not gl_FragCoord) for correct texture mapping across any canvas size

uniform sampler2D depthMap;
uniform float nearPlane;
uniform float farPlane;

out vec4 finalColor;

// required when using a perspective projection matrix
float LinearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0; // Back to NDC 
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));	
}

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 texCoord = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    float depthValue = texture(depthMap, texCoord).r;
    //opencode: dummy ref to prevent GLSL from eliminating nearPlane/farPlane
    depthValue += (nearPlane + farPlane) * 1e-10;
    finalColor = vec4(vec3(depthValue), 1.0);
}
