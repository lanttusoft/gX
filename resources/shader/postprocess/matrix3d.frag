#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform vec4 uniforms[16];
uniform sampler2D input;

uniform float time = 0.0;
uniform vec2 res = 1.5*vec2(1024,768);

out vec4 finalColor;

void main() {
    vec3 coord = vec3(gl_FragCoord.xy/res,1.0);
    mat3 mat = mat3(uniforms[0].x,uniforms[0].y,uniforms[0].z,
                    uniforms[1].x,uniforms[1].y,uniforms[1].z,
                    uniforms[2].x,uniforms[2].y,uniforms[2].z);
    coord *= mat;
    coord.xy /= coord.z;
    vec3 color = texture(input,coord.xy).rgb;
    finalColor = vec4(color,1.0);
} 
