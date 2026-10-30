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

const float pi = 3.14159265358979323846;
const float inv2fac = 5.00000000000000000000e-1;  // 1/2!
const float inv3fac = 1.66666666666666666667e-1;  // 1/3!
const float inv4fac = 4.16666666666666666667e-2;  // 1/4!
const float inv5fac = 8.33333333333333333333e-3;  // 1/5!
const float inv6fac = 1.38888888888888888889e-3;  // 1/6!
const float inv7fac = 1.98412698412698412698e-4;  // 1/7!
const float inv8fac = 2.48015873015873015873e-5;  // 1/8!
const float inv9fac = 2.75573192239858906526e-6;  // 1/9!

float cos_taylor(float angle) {
    float addsign = 1.0;
    angle = mod(angle,2.0*pi);
    if (angle >= 1.5*pi && angle < 2.0*pi)   angle -= 2.0*pi;
    if (angle >= 0.5*pi && angle < 1.5*pi) { angle = pi-angle; addsign = -1.0; }
    float angle_pow2 = angle*angle;
    float angle_pow4 = angle_pow2*angle_pow2;
    return addsign*(1.0-inv2fac*angle_pow2+inv4fac*angle_pow4-
           inv6fac*angle_pow2*angle_pow4+inv8fac*angle_pow4*angle_pow4);
}

float sin_taylor(float angle) {
    float addsign = 1.0;
    angle = mod(angle,2.0*pi);
    if (angle >= 1.0*pi && angle < 2.0*pi) { angle -= pi; addsign = -1.0; }
    if (angle >= 0.5*pi && angle < 1.0*pi)   angle = pi-angle;
    float angle_pow2 = angle*angle;
    float angle_pow4 = angle_pow2*angle_pow2;
    return addsign*angle*(1.0-inv3fac*angle_pow2+inv5fac*angle_pow4-
           inv7fac*angle_pow2*angle_pow4+inv9fac*angle_pow4*angle_pow4);
}

void main() {
    vec2 center = vec2(0.5);
    vec2 coord = gl_FragCoord.xy/res-center;
    float aspectratio = res.x/res.y;
    coord *= vec2(aspectratio,1.0);
    coord -= uniforms[1].xy;
    float angle = uniforms[0].x+0.0*time;
    float c = cos_taylor(angle);
    float s = sin_taylor(angle);
    
    //mat2 rot = mat2(c,s,-s,c);
    //coord *= rot;
    vec2 newcoord = vec2(0.0,0.0);
    newcoord.x = c*coord.x + s*coord.y;
    newcoord.y = c*coord.y - s*coord.x;
    
    newcoord /= vec2(aspectratio,1.0);
    vec3 color = texture(input,newcoord+center).rgb;
    finalColor = vec4(color,1.0);
} 
