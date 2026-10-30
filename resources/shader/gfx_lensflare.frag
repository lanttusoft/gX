//ai: procedural lens flare - bounded elements on the fixed sun->screen-center
//   axis. Source depth occludes all elements; source brightness drives ghosts.
//   Element positions: ghosts along the axis + a central halo + horizontal anamorphic streak.
#version 330

uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;
uniform vec2  sunScreenPos;   // sun position in UV space (0..1)
uniform float intensity = 1.0;
uniform float dispersal = 1.0;  // ghost spread
uniform vec2  resolution;

in vec2 fragTexCoord;
out vec4 fragColor;

//opencode: simple occlusion test - does the scene geometry cover this UV?
float occlusion(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 0.0;
    float d = texture(sceneDepth, uv).r;
    return (d > 0.999) ? 1.0 : 0.0; // 1.0 = sky/unoccluded
}

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    //ai: fixed optical axis and height-normalized distances, not per-pixel rays.
    vec2 metric = vec2(resolution.x / max(resolution.y, 1.0), 1.0);
    vec2 axis = vec2(0.5) - sunScreenPos;
    float sourceOcc = occlusion(sunScreenPos);
    vec3 sourceColor = texture(sceneColor, sunScreenPos).rgb;
    float sourceLum = dot(sourceColor, vec3(0.2126, 0.7152, 0.0722));

    vec3 flare = vec3(0.0);

    //opencode: central halo (at the sun position)
    {
        float d = length((uv - sunScreenPos) * metric);
        float halo = exp(-d * d * 200.0) * 0.5;
        flare += vec3(1.0, 0.9, 0.7) * halo * sourceOcc;
    }

    //opencode: ghost elements along the sun->screen-center axis
    for (int i = 0; i < 5; i++) {
        //ai: bounded elements on the sun-to-center axis; occlusion belongs to the source.
        float t = float(i + 1) * 0.3 * dispersal;
        vec2 ghostUV = sunScreenPos + axis * t;
        vec2 offset = (uv - ghostUV) * metric;
        float radius = 0.025 + float(i) * 0.008;
        float envelope = exp(-dot(offset, offset) / (radius * radius));
        float ghostIntensity = max(sourceLum - 0.1, 0.0) * sourceOcc * 0.5 * envelope;

        //opencode: chromatic shift - different channels offset slightly
        ghostIntensity *= (1.0 - float(i) * 0.12);
        vec3 tint = vec3(1.0 - float(i) * 0.1, 0.8 - float(i) * 0.05, 0.6 + float(i) * 0.1);
        flare += tint * ghostIntensity;
    }

    //opencode: horizontal anamorphic streak across the sun position
    {
        //ai: transverse falloff keeps the anamorphic streak horizontal and bounded.
        vec2 offset = (uv - sunScreenPos) * metric;
        float streak = exp(-offset.x * offset.x * 40.0 - offset.y * offset.y * 20000.0) * 0.3;
        flare += vec3(0.9, 0.8, 0.6) * streak * sourceOcc;
    }

    //opencode: blend flare over scene.
    //   GL_ONE,GL_ONE additive blend mode: output only the flare contribution
    //   (not scene + flare). The scene is already on the canvas from bloom.
    //   Clamp to 10.0 to prevent extreme HDR values from wrapping in LDR output
    //   when bloom tonemapping is not active.
    fragColor = vec4(min(flare * intensity, 10.0), 1.0);
}
