//opencode: RT inspector — sample a debug texture with gain/bias so
//   HDR/signed buffers (velocity, depth) display in LDR screenshots.
//   Y-flip matches the TAA resolve convention (screen Y-down vs Y-up RT).
#version 330

in vec2 fragTexCoord;

uniform sampler2D colorMap;
uniform samplerCube cubeMap;
uniform int cubeMode = 0;
//opencode: scalar AO and signed-vector debug presentations.
uniform int channelMode = 0;
uniform float gain = 1.0;
uniform float bias = 0.0;

out vec4 finalColor;

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 c;
    if (cubeMode == 1) {
        //opencode: equirectangular unwrap for cubemap probes.
        float phi = (uv.x * 2.0 - 1.0) * 3.14159265;
        float theta = (uv.y * 2.0 - 1.0) * 1.5707963;
        vec3 dir = vec3(cos(phi) * cos(theta), sin(theta), sin(phi) * cos(theta));
        c = texture(cubeMap, dir).rgb;
    } else {
        c = texture(colorMap, uv).rgb;
    }
    if (channelMode == 1) c = vec3(c.r);
    else if (channelMode == 2) c = vec3(c.rg, 0.0);
    finalColor = vec4(c * gain + bias, 1.0);
}
