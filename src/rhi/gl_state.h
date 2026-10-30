/**********************************************************************************************
*
*   [gX: src/rhi/gl_state.h]
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


/**********************************************************************************************
*
*   [src/wrappers/gl_state.h]
*   gX — a game engine
*
*   GL state save/restore stack for GFX→GUI transition safety.
*   Prevents GL state leaks across module boundaries.
*
**********************************************************************************************/

#ifndef WRAPPERS_GL_STATE_H
#define WRAPPERS_GL_STATE_H

#include "../common/types.h"

#define GX_GL_STACK_DEPTH 16

typedef struct {
    gX_U32 blendEnabled;
    gX_I32 srcRGB, dstRGB, srcA, dstA;
    gX_I32 blendEquationRGB, blendEquationAlpha;
    gX_U32 cullFaceEnabled;
    gX_I32 cullFaceMode, frontFace;
    gX_U32 depthTestEnabled;
    gX_U32 depthMask;
    gX_I32 depthFunc;
    gX_U32 scissorEnabled;
    gX_I32 activeTexture;
    gX_I32 viewport[4];
    gX_I32 currentProgram;
    gX_I32 colorMask[4];
    gX_I32 drawFbo, readFbo;
    gX_I32 polygonMode[2];
    gX_Matrix backendModelview;
    gX_Matrix backendProjection;
} gX_GL_State;

gX_I0 gX_GL_State_Save(gX_GL_State *s);
gX_I0 gX_GL_State_Restore(gX_GL_State *s);
gX_I0 gX_GL_State_Push(gX_I0);
gX_I0 gX_GL_State_Pop(gX_I0);
gX_I32 gX_GL_State_GetDepth(gX_I0);

//opencode: compare two state snapshots and log any fields that differ.
//   Outputs WARNING-level log messages for each changed field.
//   Produces no output when states match (silent on clean boundaries).
gX_I0 gX_GL_State_Diff(const gX_GL_State *before, const gX_GL_State *after, const gX_I8 *label);

#endif
