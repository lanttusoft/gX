/**********************************************************************************************
*
*   [gX: src/wrappers/wrappers_gl.c]
*   Wrappers GL helpers — matrix, texture units
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

//opencode: GL matrix/state wrappers — raylib and GLFW backends
#include "wrappers_header.h"
#include "wrappers_gl.h"

//opencode: GLFW custom matrix stack — uses gX_Matrix

gX_I0 gX_GL_Matrix_Mode(gX_I32 mode) {
    rlMatrixMode(mode);
}

gX_I0 gX_GL_Matrix_Push(gX_I0) {
    rlPushMatrix();
}

gX_I0 gX_GL_Matrix_Pop(gX_I0) {
    rlPopMatrix();
}

gX_I0 gX_GL_Matrix_LoadIdentity(gX_I0) {
    rlLoadIdentity();
}

gX_I0 gX_GL_Matrix_Translate(gX_F32 x, gX_F32 y, gX_F32 z) {
    rlTranslatef(x,y,z);
}

gX_I0 gX_GL_Matrix_Rotate(gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 angle) {
    rlRotatef(angle,x,y,z);
}

gX_I0 gX_GL_Matrix_Scale(gX_F32 x, gX_F32 y, gX_F32 z) {
    rlScalef(x,y,z);
}

gX_I0 gX_GL_Matrix_Mul(gX_F32 *mat) {
    rlMultMatrixf(mat);
}

gX_I0 gX_GL_Matrix_Frustum(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far) {
    rlFrustum(left,right,bottom,top,near,far);
}

gX_I0 gX_GL_Matrix_Ortho(gX_F64 left, gX_F64 right, gX_F64 bottom, gX_F64 top, gX_F64 near, gX_F64 far) {
    rlOrtho(left,right,bottom,top,near,far);
}

gX_I0 gX_GL_Matrix_Viewport(gX_I32 x, gX_I32 y, gX_I32 width, gX_I32 height) {
    rlViewport(x,y,width,height);
}

// Matrix getters
gX_Matrix gX_GL_Matrix_GetModelView(gX_I0) {
    Matrix mat = rlGetMatrixModelview();
    return gX_Matrix_ConvertFromBackend(mat);
}

gX_Matrix gX_GL_Matrix_GetProjection(gX_I0) {
    Matrix mat = rlGetMatrixProjection();
    return gX_Matrix_ConvertFromBackend(mat);
}

gX_Matrix gX_GL_Matrix_GetTransform(gX_I0) {
    Matrix mat = rlGetMatrixTransform();
    return gX_Matrix_ConvertFromBackend(mat);
}

gX_I0 gX_GL_Matrix_SetModelView(gX_Matrix matrix) {
    rlSetMatrixModelview(gX_Matrix_ConvertToBackend(matrix));
}

gX_I0 gX_GL_Matrix_SetProjection(gX_Matrix matrix) {
    rlSetMatrixProjection(gX_Matrix_ConvertToBackend(matrix));
}

gX_I0 gX_GL_ColorBlend_Enable(gX_I0) {
    rlEnableColorBlend();
}

gX_I0 gX_GL_ColorBlend_Disable(gX_I0) {
    rlDisableColorBlend();
}

gX_I0 gX_GL_DepthTest_Enable(gX_I0) {
    rlEnableDepthTest();
}

gX_I0 gX_GL_DepthTest_Disable(gX_I0) {
    rlDisableDepthTest();
}

gX_I0 gX_GL_DepthMask_Enable(gX_I0) {
    rlEnableDepthMask();
}

gX_I0 gX_GL_DepthMask_Disable(gX_I0) {
    rlDisableDepthMask();
}

gX_I0 gX_GL_BackFaceCull_Enable(gX_I0) {
    rlEnableBackfaceCulling();
}

gX_I0 gX_GL_BackFaceCull_Disable(gX_I0) {
    rlDisableBackfaceCulling();
}

gX_I0 gX_GL_CullFace_Set(gX_I32 mode) {
    rlSetCullFace(mode);
}

gX_I0 gX_GL_ScissorTest_Enable(gX_I0) {
    rlEnableScissorTest();
}

gX_I0 gX_GL_ScissorTest_Disable(gX_I0) {
    rlDisableScissorTest();
}

gX_I0 gX_GL_Scissor(gX_Rectangle rect) {
    rlScissor((gX_I32)rect.x, (gX_I32)rect.y, (gX_I32)rect.width, (gX_I32)rect.height);
}

gX_I0 gX_GL_Texture_Parameters(gX_U32 id, gX_I32 param, gX_I32 value) {
    rlTextureParameters(id,param,value);
}

gX_I0 gX_GL_ClearScreenBuffers(gX_I0) {
    rlClearScreenBuffers();
}

gX_I0 gX_GL_CheckErrors(gX_I0) {
    rlCheckErrors();
}

gX_I0 gX_GL_Texture_Enable(gX_U32 id) {
    rlEnableTexture(id);
}

gX_I0 gX_GL_Texture_Disable(gX_I0) {
    rlDisableTexture();
}

gX_I0 gX_GL_Enable(gX_I32 cap) {
    // rlEnableCap not available in this raylib version
    (void)cap;
}

gX_I0 gX_GL_Disable(gX_I32 cap) {
    // rlDisableCap not available in this raylib version
    (void)cap;
}

gX_I0 gX_GL_DepthClamp_Enable(gX_I0) {
    // rlEnableDepthClamp not available in this raylib version
}

gX_I0 gX_GL_DepthClamp_Disable(gX_I0) {
    // rlDisableDepthClamp not available in this raylib version
}

gX_I0 gX_GL_Dither_Enable(gX_I0) {
    // rlEnableDither not available in this raylib version
}

gX_I0 gX_GL_Dither_Disable(gX_I0) {
    // rlDisableDither not available in this raylib version
}

gX_F32 gX_GL_LineWidth_Get(gX_I0) {
    return rlGetLineWidth();
}

gX_I0 gX_GL_LineWidth_Set(gX_F32 width) {
    rlSetLineWidth(width);
}

gX_I0 gX_GL_Multisample_Enable(gX_I0) {
    // rlEnableMultiSample not available in this raylib version
}

gX_I0 gX_GL_Multisample_Disable(gX_I0) {
    // rlDisableMultiSample not available in this raylib version
}

gX_I0 gX_GL_PointMode_Enable(gX_I0) {
    rlEnablePointMode();
}

gX_I0 gX_GL_PointMode_Disable(gX_I0) {
    rlDisablePointMode();
}

gX_I0 gX_GL_SmoothLines_Enable(gX_I0) {
    rlEnableSmoothLines();
}

gX_I0 gX_GL_SmoothLines_Disable(gX_I0) {
    rlDisableSmoothLines();
}

gX_I0 gX_GL_StencilTest_Enable(gX_I0) {
    // rlEnableStencilTest not available in this raylib version
}

gX_I0 gX_GL_StencilTest_Disable(gX_I0) {
    // rlDisableStencilTest not available in this raylib version
}

gX_I0 gX_GL_WireMode_Enable(gX_I0) {
    rlEnableWireMode();
}

gX_I0 gX_GL_WireMode_Disable(gX_I0) {
    rlDisableWireMode();
}

gX_I0 gX_GL_Shader_Compile(gX_I0) {
    //opencode: no-op in all backends; shaders are managed by the renderer
}
