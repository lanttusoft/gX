/**********************************************************************************************
*
*   [gX: src/rhi/rhi_buf.c]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


//opencode: RHI Buffer — VAO/VBO lifecycle + attribute binding.
#include "rhi_buf.h"
#include "rhi_debug.h"
#include "rhi_gl.h"

//opencode: VAO availability — GL 3.0+ core, GLES 3.0+, or GL_OES_vertex_array_object
//   On GLFW/raylib (GL 3.3+/4.3+) VAOs are always mandatory.
gX_I1 gX_RHI_HasVAOs(gX_I0) {
    return 1; // GL 3.3+ core profile — VAOs are mandatory
}

gX_U32 gX_RHI_VAO_Create(const gX_I8 *name) {
    //opencode: no fake-ID fallback — VAO_Bind/VAO_Delete would crash with
    //   virtual IDs on a VAO-less context. A real software-VAO emulation
    //   (attribute-state shadow table) must be implemented at GLES port time.
    if (!gX_RHI_HasVAOs()) return 0;
    gX_U32 vao = 0;
    glGenVertexArrays(1, &vao);
    //opencode: bind before labeling — NVIDIA's glObjectLabel rejects names
    //   that were never bound ("unknown vertex array object").
    glBindVertexArray(vao);
    gX_RHI_Debug_Label(vao, GL_VERTEX_ARRAY, name);
    glBindVertexArray(0);
    return vao;
}

gX_I0 gX_RHI_VAO_Bind(gX_U32 vao) {
    glBindVertexArray(vao);
}

gX_I0 gX_RHI_VAO_Delete(gX_U32 vao) {
    if (vao) glDeleteVertexArrays(1, &vao);
}

gX_U32 gX_RHI_Buffer_Create(const gX_I8 *name) {
    gX_U32 buf = 0;
    glGenBuffers(1, &buf);
    //opencode: bind before labeling — NVIDIA's glObjectLabel rejects names
    //   that were never bound ("unknown buffer object").
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    gX_RHI_Debug_Label(buf, GL_BUFFER, name);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return buf;
}

gX_I0 gX_RHI_Buffer_Bind(gX_U32 target, gX_U32 buffer) {
    glBindBuffer(target, buffer);
}

gX_I0 gX_RHI_Buffer_BindBase(gX_U32 target, gX_U32 index, gX_U32 buffer) {
    glBindBufferBase(target, index, buffer);
}

gX_I0 gX_RHI_Buffer_Data(gX_U32 target, gX_U32 size, const void *data, gX_U32 usage) {
    glBufferData(target, (GLsizeiptr)size, data, (GLenum)usage);
}

gX_I0 gX_RHI_Buffer_SubData(gX_U32 target, gX_U32 offset, gX_U32 size, const void *data) {
    glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
}

gX_I0 gX_RHI_Buffer_Delete(gX_U32 buffer) {
    if (buffer) glDeleteBuffers(1, &buffer);
}

gX_I0 gX_RHI_Attrib_Pointer(gX_U32 index, gX_I32 size, gX_U32 type, gX_I1 normalized, gX_I32 stride, const void *pointer) {
    glVertexAttribPointer((GLuint)index, (GLint)size, (GLenum)type, normalized ? GL_TRUE : GL_FALSE, (GLsizei)stride, pointer);
}

gX_I0 gX_RHI_Attrib_Enable(gX_U32 index) {
    glEnableVertexAttribArray((GLuint)index);
}

gX_I0 gX_RHI_Attrib_Disable(gX_U32 index) {
    glDisableVertexAttribArray((GLuint)index);
}
