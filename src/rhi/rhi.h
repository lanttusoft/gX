/**********************************************************************************************
*
*   [gX: src/rhi/rhi.h]
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


//opencode: RHI — rendering hardware interface.
//   Raw GL routing layer: mesh draw + shader uniform dispatch for
//   programs registered via gX_GL_RawRegister(). Bypasses raylib's
//   material map binding and SetShaderValue paths which can produce
//   GL_INVALID_OPERATION when the shader's locs[] table has non-sampler
//   types at RL_SHADER_LOC_MAP_ALBEDO + i entries.
#ifndef RHI_H
#define RHI_H

#include "../common/types.h"
#include "../common/text.h"
#include "rhi_enums.h"
#include "rhi_query.h"
#include "rhi_debug.h"
#include "rhi_validate.h"
#include "rhi_event_log.h"
#include "rhi_caps.h"

//opencode: register / unregister a program ID for raw GL mesh drawing.
//   Registered programs skip raylib's material map binding and instead
//   draw via gX_Raw_Mesh_Draw (glBindVertexArray + glDrawElements).
//   Similarly, gX_Shader_SetValue* functions dispatch through raw GL
//   (glUseProgram + glUniform*) when the target program is registered.
gX_I0 gX_GL_RawRegister(gX_U32 programId);
gX_I0 gX_GL_RawUnregister(gX_U32 programId);
gX_I1 gX_GL_IsRawProgram(gX_U32 programId);

//opencode: raw GL mesh draw — binds VAO, draws elements.
//   Does NOT touch material maps or raylib uniforms. The caller must have
//   already bound textures and uploaded shader uniforms.
gX_I0 gX_Raw_Mesh_Draw(gX_Mesh mesh);

//opencode: activate a shader program — glUseProgram. Sets the current program
//   for the batch flush; does NOT touch the backend's shader tracker (raylib's
//   rlSetShader is deliberately avoided to prevent a batch flush).
gX_I0 gX_RHI_Shader_Use(gX_U32 programId);

//opencode: compile a shader program from preprocessed source strings.
//   Supports V+F (geoSrc=NULL) and V+G+F. Returns program ID (0 = failure).
//   errorLog is optional (pass NULL to skip). name is optional debug label.
gX_U32 gX_RHI_Shader_Compile(const gX_I8 *vertSrc, const gX_I8 *geoSrc,
    const gX_I8 *fragSrc, const gX_I8 *name, gX_I8 *errorLog, gX_U64 errorLogSize);
//ai: compile-time presentation callback; return zero to cancel the compile wait.
typedef gX_I1 (*gX_RHI_Shader_LoadingFn)(gX_I0 *userData);
gX_I0 gX_RHI_Shader_SetLoading(gX_RHI_Shader_LoadingFn callback, gX_I0 *userData);
gX_I1 gX_RHI_Shader_HasLoading(gX_I0);

//opencode: RHI trace — read back a single pixel from an FBO and log it.
//   Useful for pipeline debugging without visual access.
//   Pass fboId=0 to read from the default framebuffer.
gX_I0 gX_RHI_Trace_ProbeFBO(gX_U32 fboId, gX_U32 x, gX_U32 y, const gX_I8 *label);
gX_I0 gX_RHI_Trace_ContextDump(const gX_I8 *label);

//opencode: RHI trace — read back a single pixel from a texture and log it.
gX_I0 gX_RHI_Trace_ProbeTex(gX_U32 texId, gX_U32 x, gX_U32 y, const gX_I8 *label);

//opencode: bind a 2D texture to a specific unit, then reset to unit 0.
gX_I0 gX_RHI_Tex_BindOnUnit(gX_U32 texId, gX_U32 slot);

//opencode: bind a cubemap texture to a specific unit, then reset to unit 0.
gX_I0 gX_RHI_Tex_BindCubemapOnUnit(gX_U32 texId, gX_U32 slot);

//opencode: delete a shader program — wraps glDeleteProgram.
gX_I0 gX_RHI_Shader_Delete(gX_U32 programId);

//opencode: draw arrays — wraps glDrawArrays for non-indexed draws.
//   Used by the GLFW draw2d system for quads and gradient rects.
gX_I0 gX_RHI_DrawArrays(gX_U32 mode, gX_I32 first, gX_U32 count);

//opencode: draw indexed elements — wraps glDrawElements.
gX_I0 gX_RHI_DrawElements(gX_U32 mode, gX_U32 count, gX_U32 type, const void *indices);

//opencode: upload a 4×4 matrix uniform with optional transpose.
//   Unlike rlSetUniformMatrix, this accepts a transpose flag for
//   row-major → column-major conversion (gxmath convention).
gX_I0 gX_RHI_UniformMatrix(gX_I32 location, gX_U32 count,
    gX_I1 transpose, const gX_F32 *value);

//opencode: get uniform location — wraps glGetUniformLocation.
gX_I32 gX_RHI_GetUniformLocation(gX_U32 program, const gX_I8 *name);

//opencode: get attribute location — wraps glGetAttribLocation.
gX_I32 gX_RHI_GetAttribLocation(gX_U32 program, const gX_I8 *name);

//opencode: get default shader ID — backend-provided (raylib: rlGetShaderIdDefault;
//   GLFW: gameX custom default). The raylib query lives in the raylib backend's
//   gx_shader_default.c so the RHI itself has no rlgl dependency.
gX_U32 gX_RHI_Shader_GetDefault(gX_I0);

//opencode: get default shader locs — backend-provided (see above).
gX_I32 *gX_RHI_Shader_GetDefaultLocs(gX_I0);

//opencode: get default texture ID — backend-provided (raylib: rlGetTextureIdDefault
//   1x1 white; GLFW: batch's white texture id 1).
gX_U32 gX_RHI_Tex_GetDefault(gX_I0);

//opencode: upload a single int uniform — wraps glUniform1i.
gX_I0 gX_RHI_UniformInt(gX_I32 location, gX_I32 value);

//opencode: uniform value readback — wraps glGetUniformfv, glGetUniformiv, glGetActiveUniform
gX_I0 gX_RHI_GetUniformfv(gX_U32 program, gX_I32 location, gX_U32 count, gX_F32 *values);
gX_I0 gX_RHI_GetUniformiv(gX_U32 program, gX_I32 location, gX_U32 count, gX_I32 *values);
gX_I32 gX_RHI_GetActiveUniform(gX_U32 program, gX_U32 index,
    gX_I8 *name, gX_U32 nameSize, gX_I32 *size, gX_U32 *type);

//opencode: upload a vec4 uniform — wraps glUniform4fv.
gX_I0 gX_RHI_UniformVec4(gX_I32 location, gX_U32 count, const gX_F32 *value);

//opencode: compute shader dispatch — wraps glDispatchCompute.
gX_I0 gX_RHI_DispatchCompute(gX_U32 groupsX, gX_U32 groupsY, gX_U32 groupsZ);

//opencode: memory barrier — wraps glMemoryBarrier.
gX_I0 gX_RHI_MemoryBarrier(gX_U32 barriers);

//opencode: bind SSBO to a binding point — wraps glBindBufferBase(gX_RHI_SHADER_STORAGE_BUFFER).
gX_I0 gX_RHI_SSBO_Bind(gX_U32 ssbo, gX_U32 binding);

//opencode: bind a texture image for compute load/store operations.
gX_I0 gX_RHI_Image_Bind(gX_U32 texture, gX_U32 unit, gX_U32 access, gX_U32 format);

//opencode: compile a compute shader program from source. Returns program ID (0 = failure).
//   name is optional debug label.
gX_U32 gX_RHI_Shader_Compile_Compute(const gX_I8 *src, const gX_I8 *name, gX_I8 *errorLog, gX_U64 errorLogSize);

//opencode: instanced draws — wraps glDrawArraysInstanced / glDrawElementsInstanced.
gX_I0 gX_RHI_DrawArraysInstanced(gX_U32 mode, gX_I32 first, gX_U32 count, gX_U32 primcount);
gX_I0 gX_RHI_DrawElementsInstanced(gX_U32 mode, gX_U32 count, gX_U32 type, const void *indices, gX_U32 primcount);

//opencode: uniform wrappers — wraps glUniform1f, glUniform2f, glUniform3f, glUniform1iv, glUniformBlockBinding
gX_I0 gX_RHI_Uniform_Float(gX_I32 location, gX_F32 value);
gX_I0 gX_RHI_Uniform_Vec2(gX_I32 location, gX_F32 x, gX_F32 y);
gX_I0 gX_RHI_Uniform_Vec3(gX_I32 location, gX_F32 x, gX_F32 y, gX_F32 z);
gX_I0 gX_RHI_Uniform_IntArray(gX_I32 location, gX_U32 count, const gX_I32 *values);
gX_I0 gX_RHI_Uniform_FloatArray(gX_I32 location, gX_U32 count, const gX_F32 *values);
gX_I0 gX_RHI_Uniform_Vec2Array(gX_I32 location, gX_U32 count, const gX_F32 *values);
gX_I0 gX_RHI_Uniform_Vec3Array(gX_I32 location, gX_U32 count, const gX_F32 *values);

//opencode: uniform block binding — wraps glUniformBlockBinding
gX_I0 gX_RHI_UniformBlockBinding(gX_U32 program, gX_U32 index, gX_U32 bindingPoint);

//opencode: bind UBO to a binding point — wraps glBindBufferBase(gX_RHI_UNIFORM_BUFFER)
gX_I0 gX_RHI_UBO_Bind(gX_U32 ubo, gX_U32 bindingPoint);

//opencode: finish pending GPU commands — wraps glFinish
gX_I0 gX_RHI_Finish(gX_I0);

//opencode: per-frame draw statistics — get+reset counters
gX_U32 gX_RHI_GetAndResetDrawCalls(gX_I0);
gX_U32 gX_RHI_GetAndResetProgramSwitches(gX_I0);
gX_U32 gX_RHI_GetAndResetTextureBinds(gX_I0);
gX_U32 gX_RHI_GetAndResetFBOSwitches(gX_I0);
gX_I0  gX_RHI_IncFBOSwitch(gX_I0);
gX_I0  gX_RHI_ResetAllCounters(gX_I0);

//opencode: uniform block index query — wraps glGetUniformBlockIndex
gX_I32 gX_RHI_GetUniformBlockIndex(gX_U32 program, const gX_I8 *name);

//opencode: program integer parameter query — wraps glGetProgramiv
gX_I0 gX_RHI_GetProgramiv(gX_U32 program, gX_U32 pname, gX_I32 *params);

//opencode: create an empty program object — wraps glCreateProgram
gX_U32 gX_RHI_CreateProgram(gX_I0);

//opencode: read a program's binary — wraps glGetProgramBinary
//   (shader binary cache; requires GL_ARB_get_program_binary)
gX_I0 gX_RHI_GetProgramBinary(gX_U32 program, gX_U32 bufSize,
    gX_I32 *length, gX_U32 *format, void *binary);

//opencode: load a program from a binary — wraps glProgramBinary
gX_I0 gX_RHI_ProgramBinary(gX_U32 program, gX_U32 format,
    const void *binary, gX_U32 length);

//opencode: GL string query — wraps glGetString
const gX_I8 *gX_RHI_GetString(gX_U32 name);

//opencode: indexed extension string query — wraps glGetStringi(gX_RHI_EXTENSIONS, i)
//   (use instead of glGetString(gX_RHI_EXTENSIONS), which is invalid in core profiles)
const gX_I8 *gX_RHI_GetStringi(gX_U32 index);

#endif
