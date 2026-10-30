#version 330
//opencode: postprocess Sobel edge detect shader
//   uniforms[0].x = threshold (0.0-1.0, default 0.1)
//   uniforms[0].y = edge intensity (0.0-1.0, default 1.0)
//   uniforms[0].z = mode: 0=edges only (white on black), 1=overlay on original
//   3x3 Sobel kernel on luminance
in vec3 fragPosition; in vec2 fragTexCoord; in vec3 fragNormal; in vec4 fragColor;
uniform vec4 uniforms[16]; uniform sampler2D input;
uniform float time = 0.0; uniform vec2 res = 1.5*vec2(1024,768);
out vec4 finalColor;
void main() {
    vec2 uv = gl_FragCoord.xy / res;
    vec2 px = 1.0 / res;

    //opencode: 3x3 kernel, index order: [row*3+col]
    //  [0] [1] [2]   (row -1)
    //  [3] [4] [5]   (row  0)
    //  [6] [7] [8]   (row +1)
    float lum[9];
    for (int r = -1; r <= 1; r++) {
        for (int c = -1; c <= 1; c++) {
            vec2 off = vec2(float(c), float(r)) * px;
            vec4 samp = texture(input, uv + off);
            lum[(r+1)*3 + (c+1)] = dot(samp.rgb, vec3(0.299, 0.587, 0.114));
        }
    }

    //opencode: Sobel X and Y kernels
    float gx = -lum[0] + lum[2]
               - 2.0*lum[3] + 2.0*lum[5]
               - lum[6] + lum[8];
    float gy = -lum[0] - 2.0*lum[1] - lum[2]
               + lum[6] + 2.0*lum[7] + lum[8];

    float edge = sqrt(gx*gx + gy*gy);
    float threshold = uniforms[0].x;
    float intensity = uniforms[0].y;
    float mode = uniforms[0].z;

    float e = smoothstep(threshold - 0.05, threshold + 0.05, edge);
    e = clamp(e * intensity, 0.0, 1.0);

    vec4 orig = texture(input, uv);
    if (mode < 0.5) {
        //opencode: edges only - white lines on black background
        finalColor = vec4(vec3(e), 1.0);
    } else {
        //opencode: overlay - edges brighten the original
        vec3 edgeColor = mix(orig.rgb, vec3(1.0), e * 0.6);
        finalColor = vec4(edgeColor, 1.0);
    }
}
