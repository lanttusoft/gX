#version 330

//opencode: v0.6.0 — single-channel SDF text (raylib FONT_SDF atlas).
//   Analytic edge from the distance derivative (raylib's official sdf.fs
//   pattern — no pxRange uniform needed). The tint rides a dedicated
//   uniform: the engine's default vertex shader does not pass vertex color.
in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec4 textColor;
//opencode: soft shadow controls — x = distance-field spread (dilation),
//   y = edge-band multiplier (blur), z = alpha multiplier; defaults
//   (0, 1, 1) reproduce the plain text edge exactly.
uniform vec4 soft;

out vec4 finalColor;

void main()
{
    float dist = texture(texture0, fragTexCoord).a - 0.5;
    float change = length(vec2(dFdx(dist), dFdy(dist))) * max(soft.y, 0.001);
    float alpha = smoothstep(-change, change, dist - soft.x);
    finalColor = vec4(textColor.rgb, textColor.a * soft.z * alpha);
}