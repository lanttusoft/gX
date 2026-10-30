//opencode: SSR Hi-Z min-reduction chain.
//   Level 0: exact copy of the hdrScene depth texture (R32F).
//   Levels 1..N: 2x2 MIN of the previous level - conservative for ray marching
//   (min-depth guarantees a surface is never averaged away in a coarse texel,
//   unlike GL-generate averaged mips which let rays tunnel through thin walls).
#version 330

uniform sampler2D depthMap;   // hdrScene depth (level 0 source, bottom-left UV)
uniform sampler2D hizMap;     // previous Hi-Z level (reduce source)
uniform int mode;             // 0 = copy depth -> level 0, 1 = min-reduce from hizMap
uniform int srcLevel;         // hizMap level to fetch from (mode 1)

in vec2 fragTexCoord;
out float outDepth;

void main() {
    //opencode: error sentinel - outDepth=1.0 (far) if never written
    outDepth = 1.0;
    //opencode: flip Y - screen Y-down vs texture Y-up convention (matches gfx_ssr.frag)
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    if (mode == 0)
    {
        outDepth = texture(depthMap, uv).r;
    }
    else
    {
        ivec2 srcSize = textureSize(hizMap, srcLevel);
        ivec2 base = ivec2(uv * vec2(srcSize));
        //opencode: clamp the 2x2 footprint at odd-sized level edges
        base = min(base, srcSize - 1);
        float m = 1e9;
        for (int dy = 0; dy < 2; dy++)
        {
            for (int dx = 0; dx < 2; dx++)
            {
                m = min(m, texelFetch(hizMap, base + ivec2(dx, dy), srcLevel).r);
            }
        }
        outDepth = m;
    }
}
