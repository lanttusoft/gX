#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform float time = 0.0;
uniform vec2 res = 1.5*vec2(1024,768);
uniform vec4 col = vec4(1.0,1.0,1.0,1.0);

uniform int backside = 0;
uniform float refIndex = 1.1;
uniform vec3 cameraPos =    vec3(0.0,0.0,-1.0);
uniform vec3 cameraTarget = vec3(0.0,0.0,0.0);
uniform vec3 cameraUp =     vec3(0.0,1.0,0.0);
uniform float cameraFov =  90.0;

uniform sampler2D texture0;
uniform sampler2D normalMap;

out vec4 finalColor;

float asp = res.x/res.y;
const float pi = 3.141592;

vec3 rayDir(float fov, vec2 size, vec2 pos) {
    vec2 xy = pos - 0.5*size;
    float a = tan(pi/2 - 0.5*fov);
    float z = 0.5 * size.y * a;
    return normalize(vec3(xy,-z));
}

mat3 setCamera( vec3 pos, in vec3 target, in vec3 up) {
    vec3 forward = normalize(target-pos);
    vec3 right = normalize(cross(forward,up));
    vec3 upward = normalize(cross(right,forward));
    return mat3( right, upward, -forward );
}

void main() {
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy-screencenter.xy/res);
    
    float dotpro = dot(rayDir(cameraFov, res, vec2(0.0))*normalize(cameraTarget-cameraPos),fragNormal);
    if (backside == 1) {if (dotpro > 0.0) discard;}
    else               {if (dotpro < 0.0) discard;}
    
    vec3 normal = fragNormal.xyz;
    /*vec3 mapNormal = texture(normalMap,fragTexCoord).rbg;
    mapNormal = normalize(mapNormal * 2.0 - 1.0);
    normal = mix(normal,mapNormal,1.0);*/
    
    //vec3 ray = vec3(coord.x,coord.y,-1.0);
    vec3 ray = setCamera(cameraPos,cameraTarget,cameraUp)*vec3(screencoord.xy/res.xy-0.5,-1.0);
    //ray.xy += screencoord.xy - 0.5*res.xy;
    ray.x *= -1;
    ray.y *= -1;
    //ray.xy -= (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    vec3 coord = refract(ray,normal,0.5*refIndex);
    //coord.xy += (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    coord *= 2.0;
    coord += 0.5;
    ray += 0.5;
    coord = mix(coord,ray,0.9);
    
    float r = texture(texture0,coord.xy).r;
    float g = texture(texture0,coord.xy).g;
    float b = texture(texture0,coord.xy).b;
    
    vec3 texColor = vec3(r,g,b);
    //texColor -= 0.5*texture(texture0,fragTexCoord).rgb;
    //texColor.b -= 1.0;
    finalColor = col*vec4(texColor,1.0);
    //finalColor = vec4(vec3(0.1,1.0,0.4),1.0);
} 
