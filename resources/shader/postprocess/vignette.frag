#version 330
//opencode: postprocess vignette shader
//   uniforms[0].x = vignette radius (0.0-1.0, default 0.5)
//   uniforms[0].y = vignette softness (0.0-1.0, default 0.3)
//   uniforms[0].z = vignette opacity (0.0-1.0, default 0.8)
//   Darkens corners based on distance from center
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    float radius = max(uniforms[0].x, 0.01);
    float softness = clamp(uniforms[0].y, 0.0, 1.0);
    float opacity = clamp(uniforms[0].z, 0.0, 1.0);
    vec2 center = gl_FragCoord.xy / res;
    vec2 dist = center - 0.5;
    float len = length(dist);
    float vignette = smoothstep(radius, radius - radius * softness, len);
    vignette = 1.0 - (1.0 - vignette) * opacity;
    vec3 color = texture(input, gl_FragCoord.xy / res).rgb;
    finalColor = vec4(color * vignette, 1.0);
}
