//opencode: screen-space contact shadows - sun-direction depth march.
//   Spike 1: linear 12-step march over a 0-2u band (BeamNG-style short
//   range). Sky pixels (depth ~1) pass through lit. Output: lit fraction
//   (1 = fully lit, 0 = fully occluded within the band). No Hi-Z yet
//   (spike-1 simplicity; linear is exact, Hi-Z is a later optimization).
//   Fixed bias 0.05 + first-step lift 0.05 (acne noted, unmetered).
#version 330

uniform sampler2D sceneDepth;
uniform mat4 invProjection;
uniform mat4 invView;
uniform mat4 viewProj;
uniform vec3 cameraPos;
uniform vec3 sunDir;            // direction TOWARD the sun (normalized)
uniform vec2 resolution;
uniform float nearPlane = 0.1;
uniform float farPlane = 1000.0;

in vec2 fragTexCoord;
out vec4 fragColor;

float linearizeDepth(float d, float near, float far) {
    float z = d * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

vec3 worldFromDepth(float depth, vec2 uv) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos.xyz /= viewPos.w;
    vec4 worldPos = invView * viewPos;
    return worldPos.xyz;
}

void main() {
    //opencode: error sentinel - magenta if fragColor is never assigned
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    float rawDepth = texture(sceneDepth, uv).r;
    if (rawDepth > 0.999) {
        fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 origin = worldFromDepth(rawDepth, uv);
    //opencode: 12 linear steps over the 2u contact band (spike-1 exactness
    //   over performance; first step lifted 0.05 off the surface).
    const int STEPS = 12;
    const float BAND = 2.0;
    const float BIAS = 0.05;
    float occluded = 0.0;
    for (int i = 0; i < STEPS; i++) {
        float t = 0.05 + (float(i) + 0.5) / float(STEPS) * (BAND - 0.05);
        vec3 sampleW = origin + sunDir * t;
        vec4 clip = viewProj * vec4(sampleW, 1.0);
        vec3 ndc = clip.xyz / max(clip.w, 0.0001);
        if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z > 1.0)
            continue;
        vec2 suv = ndc.xy * 0.5 + 0.5;
        //opencode: depth texture rows run opposite to NDC: sample and
        //   reconstruct with the same flipped uv.
        vec2 suvT = vec2(suv.x, 1.0 - suv.y);
        float sampleDepth = texture(sceneDepth, suvT).r;
        //opencode: compare in EUCLIDEAN world distance (the old view-Z vs
        //   euclidean mix false-hit off-axis at 90deg FOV).
        vec3 hitW = worldFromDepth(sampleDepth, suvT);
        //opencode: occlusion test along the SAME camera ray (suv!): the
        //   march point is hidden iff the visible surface there is closer
        //   to the camera (both legs euclidean from the camera).
        float rayD = length(sampleW - cameraPos);
        float surfD = length(hitW - cameraPos);
        if (surfD + BIAS < rayD) {
            //opencode: spike-2.2 backface cull (grazing self-acne): a real
            //   sun-caster faces the sun; silhouette/backface hits are the
            //   march grazing its own origin surface (ball-top rings).
            vec3 sR = worldFromDepth(texture(sceneDepth, suvT + vec2(1.0 / resolution.x, 0.0)).r,
                                     suvT + vec2(1.0 / resolution.x, 0.0));
            vec3 sU = worldFromDepth(texture(sceneDepth, suvT + vec2(0.0, 1.0 / resolution.y)).r,
                                     suvT + vec2(0.0, 1.0 / resolution.y));
            vec3 sN = cross(sR - hitW, sU - hitW);
            if (length(sN) > 0.0001 && dot(normalize(sN), sunDir) < -0.05)
                occluded += 1.0;
        }
    }
    float lit = 1.0 - occluded / float(STEPS);
    fragColor = vec4(lit, lit, lit, 1.0);
}
