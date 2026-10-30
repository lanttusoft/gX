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
    float red =     texture(input,(gl_FragCoord.xy+uniforms[1].xy)/res).r;
    float green =   texture(input,(gl_FragCoord.xy+uniforms[2].xy)/res).g;
    float blue =    texture(input,(gl_FragCoord.xy+uniforms[3].xy)/res).b;
    vec3 color = vec3(red,green,blue);
    color.xyz *= vec3(uniforms[0].x,uniforms[0].y,uniforms[0].z);
    finalColor = vec4(color,1.0);
} 
