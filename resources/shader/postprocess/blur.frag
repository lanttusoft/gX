#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform vec4 box = vec4(0.0,0.0,1.0,1.0);
uniform vec4 col = vec4(1.0,1.0,1.0,1.0);

uniform vec4 uniforms[16];
uniform sampler2D input;

uniform float time = 0.0;
uniform vec2 res = 1.5*vec2(1024,768);

out vec4 finalColor;

//shout outs to https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
const float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

float gammafactor = 2.2;
vec4 gamma(vec4 col) { return pow(col,vec4(1.0/gammafactor)); }
vec4 degamma(vec4 col) { return pow(col,vec4(gammafactor)); }

void main() {

    vec2 coord = vec2(gl_FragCoord);
    finalColor = degamma(texture(input, coord / res.xy)) * weight[0];
    
    float a = uniforms[0].x;
    for (int i = 1; i < 3; i++) {
        
        float mult = 0.25;
        //diag
        finalColor += degamma(texture(input, (coord + a*vec2(+1.0, +1.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2(+1.0, -1.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2(-1.0, +1.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2(-1.0, -1.0)*offset[i]) / res.xy)) * mult*weight[i];
        //neighbor
        finalColor += degamma(texture(input, (coord + a*vec2(+1.0,  0.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2(-1.0,  0.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2( 0.0, +1.0)*offset[i]) / res.xy)) * mult*weight[i];
        finalColor += degamma(texture(input, (coord + a*vec2( 0.0, -1.0)*offset[i]) / res.xy)) * mult*weight[i];
        
    }
    
    finalColor = gamma(finalColor);
    
    finalColor *= col;
    
    //finalColor = vec4(color,1.0);
}
