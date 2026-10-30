#version 330
//opencode: v0.3-M4 Phase2 - SSGI spatial denoise: depth-guided 8-tap blur.
//   Edge-stopping via linearized depth similarity keeps object silhouettes
//   sharp while smoothing gather noise inside surfaces.

in vec2 fragTexCoord;

uniform sampler2D ssgiMap;
uniform sampler2D depthMap;
uniform vec2 resolution;
uniform float cameraNear = 0.1;
uniform float cameraFar = 400.0;

out vec4 finalColor;

float getDepth(vec2 uv) { return textureLod(depthMap, uv, 0.0).r; }

//opencode: v0.3.5-polish - nonlinear->view-space depth conversion so the
//   bilateral weight is meaningful at every distance (raw stops over-reached
//   near and under-stopped far)
float viewZ(float raw) {
    return cameraNear * cameraFar / max(cameraFar - raw * (cameraFar - cameraNear), 1e-5);
}

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    float cD = viewZ(getDepth(uv));
    float cSig = cD * 0.05 + 0.1;

    vec3 sum = textureLod(ssgiMap, uv, 0.0).rgb;
    float wsum = 1.0;
    vec2 px = 1.0 / resolution;

    const int R = 3;
    for (int y = -R; y <= R; y++) {
        for (int x = -R; x <= R; x++) {
            if (x == 0 && y == 0) continue;
            vec2 suv = uv + vec2(px.x * x, px.y * y) * 1.5;
            if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
            float sD = viewZ(getDepth(suv));
            //opencode: edge stop - squared view-space delta over a distance-
            //   scaled sigma keeps silhouettes sharp at any depth
            float w = exp(-abs(sD - cD) * abs(sD - cD) / (cSig * cSig));
            sum += textureLod(ssgiMap, suv, 0.0).rgb * w;
            wsum += w;
        }
    }

    finalColor = vec4(sum / wsum, 1.0);
}
