//opencode: circle of confusion from depth for depth-of-field
//   Converts non-linear depth to linear view-space distance, then computes
//   CoC as function of distance from focal plane.
#version 330

in vec2 fragTexCoord;

uniform sampler2D depthMap;
uniform vec2 resolution;
uniform float focalDistance = 5.0; //opencode: focus distance in world units
uniform float focalRange = 3.0;    //opencode: depth range for gradual blur
uniform float nearPlane = 0.1;
uniform float farPlane = 100.0;

out vec4 fragColor;

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: flip Y for RT depth source
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float depth = texture(depthMap, uv).r;
    float z_ndc = depth * 2.0 - 1.0;
    float denom = farPlane + nearPlane - z_ndc * (farPlane - nearPlane);
    if (denom == 0.0 || isnan(denom)) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float z_linear = 2.0 * nearPlane * farPlane / denom;
    float viewDist = abs(z_linear);
    float coc = abs(viewDist - focalDistance) / focalRange;
    coc = clamp(coc, 0.0, 1.0);
    fragColor = vec4(coc, 0.0, 0.0, 1.0);
}
