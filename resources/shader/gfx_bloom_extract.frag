#version 330

in vec2 fragTexCoord;

uniform sampler2D sceneTex;
uniform float threshold = 0.8;
uniform float knee = 0.0;

out vec4 finalColor;

void main() {
    //opencode: error sentinel - magenta if finalColor is never assigned
    finalColor = vec4(1.0, 0.0, 1.0, 1.0);
    //opencode: Y-flip - screen Y-down vs texture Y-up convention
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 color = texture(sceneTex, uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    //opencode: v0.1.18 - soft-knee extraction. The old form
    //   (bright = max(lum - thr, 0); color *= bright) scaled by the ABSOLUTE
    //   difference: at threshold 1.1 with scene luminance mostly < 0.65 and
    //   glints at 2-7, the few extracted pixels kept almost no energy after
    //   the blur spread them thin - bloom was visually a no-op. Now: a
    //   quadratic ramp from thr?(1-knee) to thr?(1+knee), output as a
    //   FRACTION of the pixel color (preserves highlight tint).
    float soft = threshold * knee;
    float kneeStart = max(threshold - soft, 0.0);
    float kneeEnd = threshold + soft;
    float t = clamp((luminance - kneeStart) / max(kneeEnd - kneeStart, 1e-4), 0.0, 1.0);
    float w = (luminance >= kneeEnd)
        ? min((luminance - threshold) / max(luminance, 1e-4), 1.0)
        : t * t * (kneeEnd - kneeStart) * 0.5 / max(luminance, 1e-4);
    finalColor = vec4(color * w, 1.0);
}
