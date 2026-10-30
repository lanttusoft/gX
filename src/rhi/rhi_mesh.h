/**********************************************************************************************
*
*   [gX: src/rhi/rhi_mesh.h]
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


#ifndef RHI_MESH_H
#define RHI_MESH_H

#include "../common/types.h"

#define GX_RHI_MESH_MAX_VBUFS 10

gX_I0 gX_RHI_Mesh_Upload(gX_Mesh *mesh, gX_I1 dynamic);
gX_I0 gX_RHI_Mesh_Delete(gX_Mesh *mesh);

#endif
