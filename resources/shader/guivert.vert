#version 330

//opencode: v0.2.0 - explicit-layout vertex stage for the SDF widget program.
//   Matches gX_Drawing_DrawScreenQuad's VAO (attrib 0 = pos, 1 = uv) and
//   uploads ortho via the `mvp` uniform that DrawScreenQuad provides.

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;

out vec2 fragTexCoord;
uniform mat4 mvp;

void main() {
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
