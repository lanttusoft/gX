#version 330

in vec3 worldPos;

uniform vec3 lightPos;
uniform float farPlane = 50.0;

out vec4 fragColor;

void main() {
    float depth = length(worldPos - lightPos);
    gl_FragDepth = depth / farPlane;
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
