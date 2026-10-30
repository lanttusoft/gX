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

//hsv <-> rgb conversion
//https://gist.github.com/983/e170a24ae8eba2cd174f
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec3 color = texture(input,gl_FragCoord.xy/res).rgb;
    vec3 newcolor = hsv2rgb(color);
    finalColor = vec4(newcolor,1.0);
} 
