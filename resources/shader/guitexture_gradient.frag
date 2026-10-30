#version 330

uniform vec4 box = vec4(0.0,0.0,1.0,1.0);
uniform vec4 col = vec4(1.0,1.0,1.0,1.0);

uniform vec2 scale = vec2(1.0,1.0);

uniform float time = 0.0;
uniform vec2 res = vec2(1024,768);
uniform sampler2D tex0;

out vec4 finalColor;

void main() {
    vec2 screencoord = vec2(1.0,1.0)*(gl_FragCoord.xy/res);
    vec3 coord = vec3(screencoord,1.0);
    vec4 color = vec4(1.0);
    
    float boxScale_x = box.x + 0.5*(1.0-scale.x)*box.z;
    float boxScale_y = (0.667*res.y-box.y) + 0.5*(1.0-scale.y)*box.w;
    //where did 0.667 come from?
    
    float tex_coordx = (gl_FragCoord.x-boxScale_x)/(scale.x*box.z);
    float tex_coordy = (gl_FragCoord.y-boxScale_y)/(scale.y*box.w);
    
    if (tex_coordx < 0.0 || tex_coordx > 1.0) discard;
    if (tex_coordy < 0.0 || tex_coordy > 1.0) discard;
    
    color = texture(tex0,vec2(tex_coordx,-tex_coordy)).rgba;
    color *= col;
    float factor = 0.5*(sin(10.0*coord.y+1.5*time)+1.0)+0.9;
    color.rgb *= vec3(factor);
    
    if (color.a < 0.5) discard;
    
    finalColor = color;
}
