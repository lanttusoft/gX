#version 330
//opencode: geometry shader demo - fragment shader
//   Colors each fragment using the face normal from the geometry stage,
//   with a simple view-direction rim light.

in GS_OUT {
    vec3 fragPosition;
    vec3 fragNormal;
    vec2 fragTexCoord;
    vec3 faceNormal;
} fs_in;

uniform vec3 viewPosition;

out vec4 finalColor;

void main() {
    // Face-normal based coloring (RGB mapped from XYZ normal direction)
    vec3 nrm = normalize(fs_in.faceNormal);
    vec3 base = nrm * 0.5 + 0.5;

    // Simple rim light using view direction
    vec3 viewDir = normalize(viewPosition - fs_in.fragPosition);
    float rim = 1.0 - max(dot(viewDir, normalize(fs_in.fragNormal)), 0.0);
    rim = pow(rim, 2.0);

    vec3 color = base + rim * vec3(0.3, 0.5, 0.8);
    finalColor = vec4(color, 1.0);
}
