#version 330

//from game1

uniform float time;
uniform vec2 res = 1.5*vec2(1024,768);
uniform sampler2D tex0;

uniform float uniforms[10];
uniform sampler2D input;

uniform sampler2D field_odd;
uniform sampler2D field_even;

out vec4 finalColor;

float asp = res.x/res.y;
const float pi = 3.141592;

//analog video

//-----NOISE-----//
float rand(vec2 p) {
    float seed = 69.0 + 0.00000*time;
    float a = 12.9898 + seed;
    float b = 78.233 + seed;
    float c = 43758.5453 + seed;
    return fract(sin(dot(p.xy ,vec2(a,b))) * c);
}
float noiz(vec2 p, float freq) {
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
        n += amp*noiz(p, f); 
        f *= 2.0; 
        normK += amp; 
        amp *= persistance;
    }
    float nf = n/normK;
    return pow(nf,4);
}

//vhs glsl from natron
vec3 rgb2yiq( vec3 rgb ) {
    return mat3( 0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312 ) * rgb;
}
vec3 yiq2rgb( vec3 yiq ) {
    return mat3( 1.000, 1.000, 1.000, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703 ) * yiq;
}

//hsv <-> rgb conversion
//https://gist.github.com/983/e170a24ae8eba2cd174f
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

//old
/*vec3 scanline(vec2 c, float k, int a, int field) {
    vec3 result = vec3(0.0,0.0,0.0);
    int blurtotal = 1;
    float timescale = 1;
    float t = timescale*time;
    float noiz1 = noiz(vec2(0.0,t),4);
    float noiz1b = noiz(vec2(0.0,t),16);
    float noiz2a = noiz(1024*vec2(0.00*c.x,c.y)+100.0*vec2(t,0.0),1);
    float noiz2b = noiz(1024*vec2(0.1*c.x,0.001*c.y)+100.0*vec2(t,0.0),1);
    float noiz2c = noiz(1024*vec2(0.1*c.x,c.y)+1000.0*vec2(t,0.0),1);
    float noizy =  noiz(1024*vec2(0.0*c.x,0.1*c.y)+1000.0*vec2(0.0,t),1);
    float noizy2 =  noiz(16*vec2(0.0*c.x,0.1*c.y)+10.0*vec2(0.0,t),1);
    float bottomthing_th = 0.02;
    c.y += field/res.y;
    //-------bottom thing-------//
    if (c.y < bottomthing_th) c.x += 0.01*noiz1-1.5*c.y-0.05;
    //-------sync dist-------//
    c.x -= 0.002*(1+1*noiz1b)*c.y; //horizontal sync
    if (noizy > 0.95) c.x -= 0.01*sin((1+1*noiz1)*c.y); //horizontal magnetic line
    //if (noizy2 > 0.5) c.y = 1.0*sin((1+1*noiz1)*c.y); //vertical magnetic line
    if (noiz2a > 0.1 && c.y > bottomthing_th) c.x -= 0.5*sin((1+1*noiz2c)*c.y)*pow(1-1.0*c.y,16); //bottom thing horizontal ml
    ///-------format-------//
    result += texture(tex0,vec2(c.x,c.y)).rgb;
    for (int i = 1; i <= a; i++) { //blur
        result += texture(tex0,vec2(c.x+(k*i)/(a*res.x),c.y)).rgb;
        result += texture(tex0,vec2(c.x-(k*i)/(a*res.x),c.y)).rgb;
        blurtotal += 2;
    }
    result /= blurtotal;
    //hsv stuff
    vec3 hsv = rgb2hsv(result);
    //hsv.z += 0.1*sin(64*c.y+noiz1+t);
    if (c.y < bottomthing_th) hsv.x += 0.1*sin(64*c.y+noiz1+t);
    hsv.y -= 0.02*noiz1;
    result = hsv2rgb(hsv);
    //noise
    result += 0.05*vec3(rand(vec2(c.x,c.y))); //background noise
    result += 0.25*vec3(rand(vec2(c.x,c.y)))*pow(1-1.0*c.y,16); //bottom thing background noise
    if (noizy > 0.99) result += 1.0*vec3(5.0*noiz2b-2.5); //random speckles
    if (noiz2a > 0.9 && c.y > bottomthing_th) result += 1.0*vec3(10.0*noiz2c-5.0)*pow(1-1.0*c.y,16);
    //if (c.x > 0.5+sin(16*c.y) && c.x < 0.6+sin(16*c.y)) result.x += 0.2; //test
    if (c.x < 0.0 || c.x > 1.0) result = vec3(0.0); //oob horizontal
    if (c.y < 0.0 || c.y > 1.0) result = vec3(0.0); //oob vertical
    return result;
}*/

/*float gaussian(float x, float mu, float sigma) {
    return (1.0/(sigma*sqrt(2*pi)))*exp(-0.5*pow((x)-mu,2.0)/(sigma*sigma));
}*/

//shout outs to https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
//const float gaussian5[5] = float[](924.0/4070.0,792.0/4070.0,495.0/4070.0,220.0/4070.0,66.0/4070.0);
const float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

//new
//vec3 scanline(vec2 c, float k, int a, vec2 o) {
vec3 scanline(vec2 c, int a, vec2 o) {
    //float gamma = 1.0;
    vec3 result = vec3(0.0,0.0,0.0);
    c += vec2(-o.x,o.y)/res;
    if (c.x < 0.0) return result;
    //vec3 b = texture(input,vec2(c.x,c.y)).rgb;
    //result = pow(mix(pow(result,vec3(gamma)),pow(b,vec3(gamma)),gaussian(0.0,0.0,1.0)),vec3(1.0/gamma));
    
    //-------sync-------//
    c.x += 0.001*noiz(vec2(0.0*c.x,c.y+time),16);
    
    float timescale = 1;
    float t = timescale*time;
    float noiz1 =   noiz(       vec2(0.0,       t),                             4);
    float noiz1b =  noiz(       vec2(0.0,       t),                             16);
    float noiz2a =  noiz(1024*  vec2(0.00*c.x,  c.y)        +100.0* vec2(t,0.0),1);
    float noiz2b =  noiz(1024*  vec2(0.1*c.x,   0.001*c.y)  +100.0* vec2(t,0.0),1);
    float noiz2c =  noiz(1024*  vec2(0.1*c.x,   c.y)        +1000.0*vec2(t,0.0),1);
    float noizy =   noiz(1024*  vec2(0.0*c.x,   0.1*c.y)    +1000.0*vec2(0.0,t),1);
    float noizy2 =  noiz(16*    vec2(0.0*c.x,   0.1*c.y)    +10.0*  vec2(0.0,t),1);
    float bottomthing_th = 0.01;
    
    //-------bottom thing-------//
    if (c.y < bottomthing_th) c.x += 0.01*noiz1-1.5*c.y-0.05;
    //c.x -= 0.002*(1+1*noiz1b)*c.y; //horizontal sync
    //if (noizy > 0.95) c.x -= 0.01*sin((1+1*noiz1)*c.y); //horizontal magnetic line
    //if (noizy2 > 0.5) c.y = 1.0*sin((1+1*noiz1)*c.y); //vertical magnetic line
    if (noiz2a > 0.1 && c.y > bottomthing_th) c.x += 1.0*sin((1+0*noiz2c)*c.y)*pow(1-1.0*c.y,64); //bottom thing horizontal ml
    
    //-------blur-------//
    //result += gaussian5[0]*texture(input,vec2(c.x,c.y)).rgb;
    result += weight[0]*texture(input,vec2(c.x,c.y)).rgb;
    for (int i = 1; i <= 2; i++) {
        /*float factor = (k*i)/(a*res.x);
        float gauss = gaussian(factor,0.0,32.0);
        vec3 l = vec3(0.0,0.0,0.0);
        vec3 r = vec3(0.0,0.0,0.0);
        if (c.x+factor > 0.0) l = texture(input,vec2(c.x+factor,c.y)).rgb;
        if (c.x-factor > 0.0) r = texture(input,vec2(c.x-factor,c.y)).rgb;
        result += gauss*(l+r);*/
        //result = pow(mix(pow(result,vec3(gamma)),pow(l,vec3(gamma)),gauss),vec3(1.0/gamma));
        //result = pow(mix(pow(result,vec3(gamma)),pow(r,vec3(gamma)),gauss),vec3(1.0/gamma));
        //float factor = a*float(i)/res.x;
        float factor = a*offset[i]/res.x;
        vec3 l = vec3(0.0,0.0,0.0);
        vec3 r = vec3(0.0,0.0,0.0);
        if (c.x+factor > 0.0) l = texture(input,vec2(c.x+factor,c.y)).rgb;
        if (c.x-factor > 0.0) r = texture(input,vec2(c.x-factor,c.y)).rgb;
        result += weight[i]*(l+r);
    }
    /*if (k == 2) {
        for (int i = 1; i <= 4; i++) { //ringing
            float ring_factor = 16*i/res.x;
            vec3 ring = 0.1*texture(input,vec2(c.x-ring_factor,c.y)).rgb;
            result += (1.0/pow(float(i),1.5))*ring;
        }
    }*/
    //hsv stuff
    vec3 hsv = rgb2hsv(result);
    //hsv.z += 0.1*sin(64*c.y+noiz1+t);
    //if (c.y < bottomthing_th) hsv.x += 0.1*sin(64*c.y+noiz1+t);
    hsv.y -= 0.02*noiz1;
    result = hsv2rgb(hsv);
    
    //-------noise-------//
    result += 0.02*vec3(rand(vec2(0.00001*c.x,c.y+0.01*time)));
    
    /*result += 0.05*vec3(rand(vec2(c.x,c.y))); //background noise
    result += 0.25*vec3(rand(vec2(c.x,c.y)))*pow(1-1.0*c.y,16); //bottom thing background noise
    if (noizy > 0.99) result += 1.0*vec3(5.0*noiz2b-2.5); //random speckles
    if (noiz2a > 0.9 && c.y > bottomthing_th) result += 1.0*vec3(10.0*noiz2c-5.0)*pow(1-1.0*c.y,16);
    //if (c.x > 0.5+sin(16*c.y) && c.x < 0.6+sin(16*c.y)) result.x += 0.2; //test
    if (c.x < 0.0 || c.x > 1.0) result = vec3(0.0); //oob horizontal
    if (c.y < 0.0 || c.y > 1.0) result = vec3(0.0); //oob vertical*/
    return result;
}

void main() {
    vec2 coord = vec2(1.0,1.0)*gl_FragCoord.xy/res;
    //float a = 0.25*time;
    /*coord -= vec2(0.5);
    coord *= vec2(asp,1);
    coord *= mat2(cos(a),sin(a),-sin(a),cos(a));
    coord /= vec2(asp,1);
    coord += vec2(0.5);*/
    /*int field = int(floor(0.0*time))%2;
    float y = rgb2yiq(scanline(coord,2,16,field)).r;
    float i = rgb2yiq(scanline(coord,16,16,field)).g;
    float q = rgb2yiq(scanline(coord,16,16,field)).b;
    color = yiq2rgb(vec3(y,i,q));*/
    
    vec3 color = vec3(0.0,0.0,0.0);
    
    float lineres =    1.0*640.0;
    float linenumber = 1.0*480.0;
    coord.x = (1.0/lineres)*(floor(lineres*coord.x)); //line res
    coord.y = (1.0/linenumber)*(floor(linenumber*coord.y)); //lines
    float y = 0.0;
    float i = 0.0;
    float q = 0.0;
    y = rgb2yiq(scanline( coord,1,vec2(0.0,0.0) ) ).r;
    i = rgb2yiq(scanline( coord,8,vec2(4.0,2.0) ) ).g;
    q = rgb2yiq(scanline( coord,8,vec2(4.0,2.0) ) ).b;
    
    color = yiq2rgb(vec3(y,i,q));
    
    vec3 colorburst = rgb2hsv(color);
    //colorburst.x += 0.1*time;
    colorburst.y *= 1.25;
    colorburst.z *= 1.5;
    color = hsv2rgb(colorburst);
    
    finalColor = vec4(color,1.0);
}
 
