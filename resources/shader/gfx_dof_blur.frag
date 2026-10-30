//opencode: DOF separable blur - weighted by circle of confusion
//   Reads color and CoC; blur radius scales with CoC.
//   Direction = horizontal (vec2(1,0)) or vertical (vec2(0,1)) depending on pass.
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform sampler2D cocMap;
uniform vec2 direction;    //opencode: blur direction (1,0)=H, (0,1)=V
uniform vec2 resolution;
uniform float maxRadius = 8.0; //opencode: max blur radius in pixels

out vec4 finalColor;

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: flip Y for RT source (UV(0,0) = visual bottom in raylib RTs)
    vec2 baseUv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec2 texel = 1.0 / resolution;
    float coc = texture(cocMap, baseUv).r;
    int radius = int(maxRadius * coc);
    radius = max(radius, 1);
    
    vec3 color = vec3(0.0);
    float total = 0.0;
    for (int i = -radius; i <= radius; i++) {
        vec2 offset = direction * float(i) * texel;
        vec2 uv = baseUv + offset;
        uv = clamp(uv, 0.001, 0.999);
        //opencode: non-finite tap sculpt (bloom item-11 class): a single NaN
        //   tap poisons the whole kernel (NaN*w=NaN even for w=0, so the
        //   weight must be skipped BEFORE the multiply, not zeroed after).
        //   Proven: 375 NaN hdrScene pixels amplified x74 to 27.7k by the
        //   wide kernel at high CoC (GX_DOF_TEX_STATS).
        vec3 tap = texture(colorMap, uv).rgb;
        float wc = texture(cocMap, uv).r;
        float w = (any(isnan(tap)) || any(isinf(tap))) ? 0.0 : max(wc, 0.001);
        if (w > 0.0) {
            color += tap * w;
            total += w;
        }
    }
    color /= max(total, 0.001);
    
    finalColor = vec4(color, 1.0);
}
