/**********************************************************************************************
*
*   [gX: src/rhi/rhi_gl.h]
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


//opencode: RHI GL — single inclusion point for GL types and functions.
//   Engine code includes this instead of glad.h/rlgl.h directly.
//   Only src/rhi/ and src/backend/ files may include glad.h/rlgl.h directly.
//   gX (raylib-only) uses raylib's bundled glad.h (GL core 4.3 + GLES 3.2)
//   from the raylib submodule — raylib's rlgl.c also defines the glad_gl*
//   symbols. The RL_* enums formerly taken from rlgl.h are provided by the
//   RHI surface (gX_RHI_*).
#ifndef RHI_GL_H
#define RHI_GL_H

#include "../common/config.h"

    #include <glad.h>

#include "rhi.h"

#endif
