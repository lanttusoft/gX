#version 330
//opencode: geometry shader demo — "explode" effect
//   Each triangle's vertices are pushed outward along the face normal.
//   The face normal (flat shading) is computed from the three input vertices.
//   A uniform u_Explode controls the intensity (0 = no effect, >0 = explosion).

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

uniform float u_Explode;

in VS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec2 fragTexCoord;
} gs_in[];

out GS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec2 fragTexCoord;
    vec3 faceNormal;
} gs_out;

uniform mat4 matView;
uniform mat4 matProjection;

void main() {
    // Compute face normal from the three vertices (world space)
    vec3 a = gs_in[0].fragPosition;
    vec3 b = gs_in[1].fragPosition;
    vec3 c = gs_in[2].fragPosition;
    vec3 faceNorm = normalize(cross(b - a, c - a));

    for (int i = 0; i < 3; i++) {
        // Offset vertex along face normal
        vec3 exploded = gs_in[i].fragPosition + faceNorm * u_Explode;

        gs_out.fragPosition  = exploded;
        gs_out.fragNormal    = gs_in[i].fragNormal;
        gs_out.fragTexCoord  = gs_in[i].fragTexCoord;
        gs_out.faceNormal    = faceNorm;

        gl_Position = matProjection * matView * vec4(exploded, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
