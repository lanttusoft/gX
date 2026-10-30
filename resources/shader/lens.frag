#version 330

uniform vec4 box = vec4(0.0,0.0,1.0,1.0);
uniform vec4 col = vec4(1.0,1.0,1.0,1.0);

uniform float time = 0.0;
uniform vec2 res = vec2(1024,768);
uniform sampler2D tex0;

out vec4 finalColor;

float asp = res.x/res.y;
const float pi = 3.141592;

void main() {
    vec2 screencenter = vec2(0.5,0.5);
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy/res);
    vec3 coord = vec3(screencoord,1.0);
    
    /*float test = 0.0;
    coord.xy *= (1.0+test);
    coord.xy -= 0.5*(test);*/
    
    /*vec2 newcoord = vec2(coord.x,coord.y);
    vec4 newbox = vec4(box.x,-box.y,box.z,box.w)/vec4(res.x,res.y,res.x,res.y);
    vec2 inside = newcoord-(newbox.xy+vec2(0.5*newbox.z,2.5*newbox.w));
    coord.z = mod(length(1.0*inside),1.0);*/
    
    //coord.xy *= coord.z;
    //coord.xy += vec2(box.x,-box.y)/res;
    
    //float boxasp = box.z/box.w;
    //coord.z = length(coord.xy-(vec2(box.x,-box.y)+vec2(0.5*box.z,boxasp*box.z))/res);
    //coord.xy -= 0.5*vec2(box.x,box.y)/res;
    //coord.xy *= coord.z;
    //coord.xy += 0.5*vec2(box.x,box.y)/res;
    
    //r = mod(length(10.0*coord.z),1.0);
    
    //float factor = 1.0;
    
    float bx = (gl_FragCoord.x-box.x)/(box.z);
    float by = (res.y-gl_FragCoord.y-box.y)/(box.w);
    float bz = length(vec2(bx-0.5,
                           by-0.5));
    float bt = atan((by-0.5),(bx-0.5));
    
    float nx = bx-0.5;
    float ny = by-0.5;
    vec3 ray = vec3(coord.x,coord.y,-1.0);
    vec3 normal = vec3(0.0,0.0,1.0);
    normal.x = sign(nx)*pow(nx*nx,0.5);
    normal.y = -sign(ny)*pow(ny*ny,0.5);
    normal.z = max(1.0-sqrt(nx*nx+ny*ny),0.0);
    float refindex = 1.05;
    ray.xy -= (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    vec3 coord_r = refract(ray,normalize(normal),1/(refindex+0.0));
    vec3 coord_g = refract(ray,normalize(normal),1/(refindex+0.001));
    vec3 coord_b = refract(ray,normalize(normal),1/(refindex+0.002));
    coord_r.xy += (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    coord_g.xy += (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    coord_b.xy += (box.xy+vec2(0.5*box.z,-0.5*box.w))/res.xy;
    
    /*if (coord.x < 0.0 || coord.x > 1.0) discard;
    if (coord.y < 0.0 || coord.y > 1.0) discard;*/
    if (coord.z == 0.0) discard;
    
    /*coord.xy *= 1.0+0.1*bz;
    coord.xy -= 0.5*0.1*bz;*/
    
    float r = texture(tex0,coord_r.xy).r;
    float g = texture(tex0,coord_g.xy).g;
    float b = texture(tex0,coord_b.xy).b;
    
    /*r += 0.1*mod(bx,1.0);
    g += 0.1*mod(by,1.0);
    b += 0.1*mod(10.0*bz,1.0);*/
    
    vec3 texColor = vec3(r,g,b);
    finalColor = col*vec4(texColor,1.0);
} 
