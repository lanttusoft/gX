#version 330

//opencode: transparent-background alpha mask (raymarch compositing) — sets
//   the gfx layer's alpha from the scene depth: geometry pixels stay opaque,
//   sky/far pixels become transparent so the raymarch backdrop shows through
//   an ALPHA composite layer blend. The pass runs with a separate blend that
//   preserves RGB (ZERO/ONE) and replaces alpha (ONE/ZERO), so it needs no
//   color read-back.

in vec2 fragTexCoord;

uniform sampler2D maskDepth;

out vec4 finalColor;

void main()
{
    //opencode: render-texture depth samples bottom-up while the fullscreen
    //   quad's UVs run top-down, so the depth lookup flips V (verified by the
    //   compositing gate: the sky strip matches the backdrop at 0.000).
    float depth = texture(maskDepth, vec2(fragTexCoord.x, 1.0 - fragTexCoord.y)).r;
    finalColor = vec4(1.0, 1.0, 1.0, depth < 0.9999995 ? 1.0 : 0.0);
}
