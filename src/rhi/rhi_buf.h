/**********************************************************************************************
*
*   [gX: src/rhi/rhi_buf.h]
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
//   Wraps glGenVertexArrays, glBindBuffer, glBufferData, glVertexAttribPointer, etc.
//   Engine code creates/manages all GPU buffers through this API.
#ifndef RHI_BUF_H
#define RHI_BUF_H

#include "../common/types.h"

// --- VAO ---
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_VAO_Create(const gX_I8 *name);
gX_I1  gX_RHI_HasVAOs(gX_I0);
gX_I0  gX_RHI_VAO_Bind(gX_U32 vao);
gX_I0  gX_RHI_VAO_Delete(gX_U32 vao);

// --- Buffer objects (VBO, EBO, UBO) ---
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Buffer_Create(const gX_I8 *name);
gX_I0  gX_RHI_Buffer_Bind(gX_U32 target, gX_U32 buffer);
gX_I0  gX_RHI_Buffer_BindBase(gX_U32 target, gX_U32 index, gX_U32 buffer);
gX_I0  gX_RHI_Buffer_Data(gX_U32 target, gX_U32 size, const void *data, gX_U32 usage);
gX_I0  gX_RHI_Buffer_SubData(gX_U32 target, gX_U32 offset, gX_U32 size, const void *data);
gX_I0  gX_RHI_Buffer_Delete(gX_U32 buffer);

// --- Vertex attributes ---
gX_I0  gX_RHI_Attrib_Pointer(gX_U32 index, gX_I32 size, gX_U32 type, gX_I1 normalized, gX_I32 stride, const void *pointer);
gX_I0  gX_RHI_Attrib_Enable(gX_U32 index);
gX_I0  gX_RHI_Attrib_Disable(gX_U32 index);

#endif
