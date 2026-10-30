#version 330
//opencode: debug normal lines fragment shader - just outputs the line color.

in vec4 lineColor;
out vec4 finalColor;

void main() {
    finalColor = lineColor;
}
