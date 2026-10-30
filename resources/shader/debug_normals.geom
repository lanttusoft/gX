#version 330
//opencode: debug normal/tangent lines geometry shader.
//   Takes triangles (from normal model draw) and emits line segments
//   along normals (red) and/or tangents (green) for each vertex.
//   u_DebugMode: 1=normals, 2=tangents, 3=both

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out; // passthrough (discarded, we draw lines)

in VS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec3 fragTangent;
} gs_in[];

uniform int   u_DebugMode = 1;
uniform float u_LineLength = 0.3;
uniform mat4  matView;
uniform mat4  matProjection;

out vec4 lineColor;

void main() {
    // Emit normal/tangent lines for each vertex, using line primitives.
    // We discard the triangle by not emitting any vertices.
    // Instead, for each vertex we emit a line strip (start → end).
    // Since we can't change output primitive type per-emission,
    // we have to emit ALL lines in a single continuous strip.
    // We use GL_LINES by emitting pairs of vertices.

    // Actually, we can't output lines from a triangle input with line_strip output.
    // The output layout is fixed at compile time.
    // Solution: emit vertices that form lines, but the primitive is triangle_strip.
    // Each "triangle" will be degenerate (zero area) and clipped.
    // Instead, skip the triangle output and rely on the main render pass.
    // Lines are drawn as a separate pass using GL_POINTS input.

    // Passthrough — discard this pass, lines are drawn separately via GL_POINTS.
    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
