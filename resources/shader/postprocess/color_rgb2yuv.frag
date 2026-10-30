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

//vhs glsl from natron
vec3 rgb2yuv( vec3 rgb ) {
    return vec3(0.0);
}

void main() {
    vec3 color = texture(input,gl_FragCoord.xy/res).rgb;
    vec3 newcolor = rgb2yuv(color);
    finalColor = vec4(newcolor,1.0);
} 
