#version 330

void main() {
    //opencode: no color output - depth-only pass, GPU writes depth automatically
    //   Writing gl_FragDepth would disable early-Z; the interpolated depth is correct.
}
