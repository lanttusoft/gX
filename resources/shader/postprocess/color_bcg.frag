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
    vec3 color = texture(input,gl_FragCoord.xy/res).rgb;
    
    color = pow(color,uniforms[2].xyz);
    color *= uniforms[1].xyz;
    color += uniforms[0].xyz;
    color = clamp(color,vec3(0.0),vec3(1.0));
    
    finalColor = vec4(color,1.0);
} 
