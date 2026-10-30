/**********************************************************************************************
*
*   [gX: src/rhi/rhi_mesh.c]
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


#include "rhi_mesh.h"
#include "rhi_gl.h"
#include "rhi_buf.h"
#include <stdlib.h>
#include <string.h>

gX_I0 gX_RHI_Mesh_Upload(gX_Mesh *mesh, gX_I1 dynamic) {
    if (mesh->vaoId > 0) return;
    mesh->vboId = (gX_U32 *)calloc(GX_RHI_MESH_MAX_VBUFS, sizeof(gX_U32));
    mesh->vaoId = gX_RHI_VAO_Create("mesh_vao");
    gX_RHI_VAO_Bind(mesh->vaoId);

    gX_U32 usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    void *pos = (mesh->animVertices != NULL) ? (void*)mesh->animVertices : (void*)mesh->vertices;
    if (pos) {
        mesh->vboId[0] = gX_RHI_Buffer_Create("mesh_pos");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[0]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 3 * sizeof(gX_F32)), pos, usage);
        gX_RHI_Attrib_Pointer(0, 3, GL_FLOAT, 0, 0, NULL);
        gX_RHI_Attrib_Enable(0);
    }

    if (mesh->texcoords) {
        mesh->vboId[1] = gX_RHI_Buffer_Create("mesh_uv");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[1]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 2 * sizeof(gX_F32)), mesh->texcoords, usage);
        gX_RHI_Attrib_Pointer(1, 2, GL_FLOAT, 0, 0, NULL);
        gX_RHI_Attrib_Enable(1);
    } else {
        glVertexAttrib2f(1, 0, 0);
    }

    void *nrm = (mesh->animNormals != NULL) ? (void*)mesh->animNormals : (void*)mesh->normals;
    if (nrm) {
        mesh->vboId[2] = gX_RHI_Buffer_Create("mesh_normal");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[2]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 3 * sizeof(gX_F32)), nrm, usage);
        gX_RHI_Attrib_Pointer(2, 3, GL_FLOAT, 0, 0, NULL);
        gX_RHI_Attrib_Enable(2);
    } else {
        glVertexAttrib3f(2, 0, 0, 1);
    }

    if (mesh->colors) {
        mesh->vboId[3] = gX_RHI_Buffer_Create("mesh_color");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[3]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 4 * sizeof(gX_U8)), mesh->colors, usage);
        gX_RHI_Attrib_Pointer(3, 4, GL_UNSIGNED_BYTE, 1, 0, NULL);
        gX_RHI_Attrib_Enable(3);
    } else {
        glVertexAttrib4f(3, 1, 1, 1, 1);
    }

    if (mesh->tangents) {
        mesh->vboId[4] = gX_RHI_Buffer_Create("mesh_tangent");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[4]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 4 * sizeof(gX_F32)), mesh->tangents, usage);
        gX_RHI_Attrib_Pointer(4, 4, GL_FLOAT, 0, 0, NULL);
        gX_RHI_Attrib_Enable(4);
    } else {
        glVertexAttrib4f(4, 1, 0, 0, 1);
    }

    if (mesh->texcoords2) {
        mesh->vboId[5] = gX_RHI_Buffer_Create("mesh_uv2");
        gX_RHI_Buffer_Bind(GL_ARRAY_BUFFER, mesh->vboId[5]);
        gX_RHI_Buffer_Data(GL_ARRAY_BUFFER, (gX_U32)(mesh->vertexCount * 2 * sizeof(gX_F32)), mesh->texcoords2, usage);
        gX_RHI_Attrib_Pointer(5, 2, GL_FLOAT, 0, 0, NULL);
        gX_RHI_Attrib_Enable(5);
    } else {
        glVertexAttrib2f(5, 0, 0);
    }

    if (mesh->indices) {
        mesh->vboId[6] = gX_RHI_Buffer_Create("mesh_index");
        gX_RHI_Buffer_Bind(GL_ELEMENT_ARRAY_BUFFER, mesh->vboId[6]);
        gX_RHI_Buffer_Data(GL_ELEMENT_ARRAY_BUFFER, (gX_U32)(mesh->triangleCount * 3 * sizeof(gX_U16)), mesh->indices, usage);
    }

    gX_RHI_VAO_Bind(0);
}

gX_I0 gX_RHI_Mesh_Delete(gX_Mesh *mesh) {
    if (!mesh || !mesh->vboId) return;
    for (gX_I32 i = 0; i < GX_RHI_MESH_MAX_VBUFS; i++) {
        if (mesh->vboId[i]) gX_RHI_Buffer_Delete(mesh->vboId[i]);
    }
    if (mesh->vaoId) gX_RHI_VAO_Delete(mesh->vaoId);
    free(mesh->vboId);
    mesh->vboId = NULL;
    mesh->vaoId = 0;
}
