#version 330

uniform float time,cfov;
uniform vec2 res;
uniform vec3 cpos,ctarget,cup;
uniform sampler2D tex0;

out vec4 finalColor;

float asp = res.x/res.y;
const float pi = 3.141592;
mat3 rotationXYZ(vec3 angle) {
    vec3 c = cos(angle); 
    vec3 s = sin(angle);
    mat3 matx = mat3( 1.0, 0.0, 0.0, 
                      0.0, c.x, s.x, 
                      0.0,-s.x, c.x);
    mat3 maty = mat3( c.y, 0.0, s.y, 
                      0.0, 1.0, 0.0, 
                     -s.y, 0.0, c.y);
    mat3 matz = mat3( c.z, s.z, 0.0, 
                     -s.z, c.z, 0.0, 
                      0.0, 0.0, 1.0);
    return matx*maty*matz;
}
vec3 raydir(float fov, vec2 size, vec2 pos) {
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

//-----NOISE-----//
//idk where i found this, stackoverflow maybe?
float rand(vec2 p) {
    float seed = 69.0 + 0.00000*time;
    float a = 12.9898 + seed;
    float b = 78.233 + seed;
    float c = 43758.5453 + seed;
    return fract(sin(dot(p.xy ,vec2(a,b))) * c);
}
float noise(vec2 p, float freq) {
    float unit = 1/freq; 
    vec2 ij = floor(freq*p); 
    vec2 xy = mod(p,unit)/unit; 
    xy = 0.5*(1.0-cos(pi*xy));
    float a = rand(ij+vec2(0.0,0.0)); 
    float b = rand(ij+vec2(1.0,0.0));
    float c = rand(ij+vec2(0.0,1.0)); 
    float d = rand(ij+vec2(1.0,1.0));
    float x1 = mix(a, b, xy.x); 
    float x2 = mix(c, d, xy.x);
    return mix(x1, x2, xy.y);
}
float pNoise(vec2 p, int res) {
    float persistance = 0.5;
    float n = 0.0; 
    float normK = 0.0;
    float f = 4.0; 
    float amp = 1.0; 
    int scale = 1;
    for (int i = 0; i < res; i += scale) {
        n += amp*noise(p, f); 
        f *= 2.0; 
        normK += amp; 
        amp *= persistance;
    }
    float nf = 1.0;
    if (normK != 0) nf = n/normK;
    return pow(nf,4);
}

vec3 spectrum(float t, float k, float p) {
    vec3 output = vec3(0.0);
    float x = t-0.0/6.0*k;
    float y = t-1.0/6.0*k;
    float z = t-2.0/6.0*k;
    output.x -= max(1+p*(-1),0.0)*pow(k+2,2)*(x*(x-(3.0-k)/3.0));
    output.y -= max(1+p*(+0),0.0)*pow(k+2,2)*(y*(y-(3.0-k)/3.0));
    output.z -= max(1+p*(+1),0.0)*pow(k+2,2)*(z*(z-(3.0-k)/3.0));
    return clamp(output,0.0,1.0);
}

void main() {
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = gl_FragCoord.xy-screencenter;
    vec3 coord = setCamera( cpos, ctarget, cup ) * raydir( pi*cfov/180, res.xy, screencoord.xy );
    
    vec3 up = vec3(0.0,1.0,0.0);
    vec3 sunpos = normalize(vec3(0.0,1.0,1.0));
    float updot = dot(coord,up);
    float sundot = dot(coord,sunpos);
    float upsun = dot(up,sunpos);
    
    vec3 skycolor = vec3(0.0,0.0,0.0);
    //float t = clamp(0.5*pow(updot,0.5)+0.5,0.6,0.85); //day
    //float t = clamp(1.0*pow(updot,0.5)+0.2,0.0,1.0); //sunset
    float sun = pow(upsun,1.0);
    float t = clamp((1.0-0.5*sun)*pow(updot,0.5)+0.2+0.3*sun, 0.6*sun, 1.0-0.15*sun);
    skycolor += clamp(5.0*sun+1.0,0.0,1.0)*spectrum(t,1.0,0.0);
    skycolor += 500.0*spectrum(1-sundot+0.999,0.0,-1.0); //sun
    skycolor += clamp(-sun,0.0,1.0)*pow(1-updot+0.5,2.0)*vec3(0.2,0.0,1.0); //night
    
    vec2 cloudcoord = coord.xz/(coord.y);
    cloudcoord *= 1.0-cpos.y;
    cloudcoord += cpos.xz;
    float clouds = pNoise(1.0*cloudcoord+vec2(0.1,0.1)*time,8);
    clouds /= max(1.0*length(cloudcoord-cpos.xz)-10.0,1.0);
    skycolor += clouds;
    
    vec3 color = skycolor;
    if (dot(coord,up) < max(-0.001*cpos.y,0.0)) color = vec3(0.2,0.5,0.1);
    finalColor = vec4(color,1.0);
}
