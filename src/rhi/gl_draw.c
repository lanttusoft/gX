/**********************************************************************************************
*
*   [gX: src/rhi/gl_draw.c]
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


//opencode: raw GL mesh draw — program registry + direct VAO bind + drawElements.
//   Replaces raylib_Mesh_Draw for programs whose locs[] entries at
//   RL_SHADER_LOC_MAP_ALBEDO + i are not sampler types.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rhi.h"
#include "rhi_tex.h"
#include "rhi_debug.h"
#include "rhi_event_log.h"
#include "rhi_buffer_audit.h"
#include "../common/log.h"
#include "../common/macros.h"
#include "rhi_gl.h"
#include "rhi_bindless.h"

//ai: presentation is opt-in and scoped to a renderer's blocking compile call.
static gX_RHI_Shader_LoadingFn s_loadingCallback;
static gX_I0 *s_loadingUser;

//ai: expose no GL context or worker threads to the presentation callback.
gX_I0 gX_RHI_Shader_SetLoading(gX_RHI_Shader_LoadingFn callback, gX_I0 *userData)
{
    s_loadingCallback = callback;
    s_loadingUser = userData;
}

//ai: wrappers use the RHI compiler only when loading presentation is requested.
gX_I1 gX_RHI_Shader_HasLoading(gX_I0)
{
    return s_loadingCallback != NULL;
}

//ai: KHR/ARB completion queries must precede status queries, which otherwise block.
static gX_I1 shader_parallel_enable(gX_I0)
{
    typedef gX_I0 (GLAD_API_PTR *CompilerThreadsFn)(GLuint count);
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++)
    {
        const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (!ext) continue;
        const char *proc = !strcmp(ext, "GL_KHR_parallel_shader_compile")
            ? "glMaxShaderCompilerThreadsKHR"
            : !strcmp(ext, "GL_ARB_parallel_shader_compile")
            ? "glMaxShaderCompilerThreadsARB" : NULL;
        if (!proc) continue;
        CompilerThreadsFn threads = (CompilerThreadsFn)gX_RHI_GetProcAddress(proc);
        if (threads)
        {
            threads(0xffffffffu);
            return 1;
        }
    }
    return 0;
}

//ai: retain same-thread GL ownership while the driver compiler works asynchronously.
static gX_I1 shader_loading_wait(GLuint id, gX_I1 program, gX_I1 parallel)
{
    if (!s_loadingCallback) return 1;
    if (!parallel) return s_loadingCallback(s_loadingUser);
    GLint complete = 0;
    while (!complete)
    {
        if (!s_loadingCallback(s_loadingUser)) return 0;
        if (program) glGetProgramiv(id, 0x91b1, &complete);
        else glGetShaderiv(id, 0x91b1, &complete);
    }
    return 1;
}

//opencode: raw GL program registry grows with the active shader set

//opencode: per-frame draw statistics
static gX_U32 g_drawCallCount = 0;
static gX_U32 g_programSwitchCount = 0;
static gX_U32 g_textureBindCount = 0;
static gX_U32 g_fboSwitchCount = 0;
static gX_U32 g_lastProgramId = 0;

gX_U32 gX_RHI_GetAndResetDrawCalls(gX_I0) {
    gX_U32 v = g_drawCallCount; g_drawCallCount = 0; return v;
}
gX_U32 gX_RHI_GetAndResetProgramSwitches(gX_I0) {
    gX_U32 v = g_programSwitchCount; g_programSwitchCount = 0; return v;
}
gX_U32 gX_RHI_GetAndResetTextureBinds(gX_I0) {
    gX_U32 v = g_textureBindCount; g_textureBindCount = 0; return v;
}
gX_U32 gX_RHI_GetAndResetFBOSwitches(gX_I0) {
    gX_U32 v = g_fboSwitchCount; g_fboSwitchCount = 0; return v;
}
gX_I0 gX_RHI_IncFBOSwitch(gX_I0) { g_fboSwitchCount++; }
gX_I0 gX_RHI_ResetAllCounters(gX_I0) {
    g_drawCallCount = 0;
    g_programSwitchCount = 0;
    g_textureBindCount = 0;
    g_fboSwitchCount = 0;
    g_lastProgramId = 0;
}

static gX_U32 *g_rawProgramIDs = NULL;
static gX_U32 g_rawProgramCount = 0;
static gX_U32 g_rawProgramCapacity = 0;

//opencode: grow the raw program registry instead of silently dropping programs
static gX_I1 gX_GL_RawEnsureCapacity(gX_U32 required) {
    if (required <= g_rawProgramCapacity) return 1;
    gX_U32 newCapacity = g_rawProgramCapacity ? g_rawProgramCapacity * 2 : 32;
    if (newCapacity < required) newCapacity = required;
    gX_U32 *newIDs = (gX_U32 *)realloc(g_rawProgramIDs,
        (size_t)newCapacity * sizeof(*newIDs));
    if (!newIDs) {
        gX_TraceLog(gX_LOG_ERROR, "RHI: unable to grow raw shader registry to %u programs", required);
        return 0;
    }
    g_rawProgramIDs = newIDs;
    g_rawProgramCapacity = newCapacity;
    return 1;
}

gX_I0 gX_GL_RawRegister(gX_U32 programId) {
    if (programId == 0) return;
    for (gX_U32 i = 0; i < g_rawProgramCount; i++) {
        if (g_rawProgramIDs[i] == programId) return;
    }
    if (!gX_GL_RawEnsureCapacity(g_rawProgramCount + 1)) return;
    g_rawProgramIDs[g_rawProgramCount++] = programId;
}

gX_I0 gX_GL_RawUnregister(gX_U32 programId) {
    for (gX_U32 i = 0; i < g_rawProgramCount; i++) {
        if (g_rawProgramIDs[i] == programId) {
            g_rawProgramIDs[i] = g_rawProgramIDs[--g_rawProgramCount];
            return;
        }
    }
}

gX_I1 gX_GL_IsRawProgram(gX_U32 programId) {
    if (programId == 0) return 0;
    for (gX_U32 i = 0; i < g_rawProgramCount; i++) {
        if (g_rawProgramIDs[i] == programId) return 1;
    }
    return 0;
}

gX_I0 gX_Raw_Mesh_Draw(gX_Mesh mesh) {
    if (mesh.vaoId == 0) return;
    gX_RHI_Validate_PreDraw("Raw_Mesh_Draw");
    gX_RHI_EventLog_Push(GX_RHI_EVENT_DRAW, 0, (gX_U32)mesh.vaoId, (gX_U32)mesh.triangleCount, "Mesh");
    g_drawCallCount++;
    glBindVertexArray(mesh.vaoId);
    //opencode: bind the mesh's element buffer — glDrawElements requires a valid
    //   GL_ELEMENT_ARRAY_BUFFER (VAO state may not include it; without it the
    //   draw fails with GL_INVALID_OPERATION when the previous EBO binding is
    //   gone, e.g. in the point shadow pass after the batch flush)
    if (mesh.indices != NULL && mesh.vboId != NULL && mesh.vboId[6] > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.vboId[6]);
    }
#ifdef GX_GPU_SKINNING
    //opencode: bone attributes for the raw (custom-shader) path — the skinned
    //   shaders declare them at the fixed contract locations 7/8, and the
    //   renderer sets the per-mesh gX_UseSkinning switch before the draw.
    //   Without this the attributes stayed unbound and the generic (0,0,0,1)
    //   default sampled boneMatrices[1] (zero) -> collapsed skinned meshes.
    if (mesh.boneIndices != NULL && mesh.vboId != NULL
        && mesh.vboId[7] > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[7]);
        glVertexAttribPointer(7, 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(7);
    }
    if (mesh.boneWeights != NULL && mesh.vboId != NULL
        && mesh.vboId[8] > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[8]);
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(8);
    }
#endif
    //opencode: non-indexed meshes (raylib LoadOBJ emits triangle soup with
    //   indices == NULL) must draw via DrawArrays — DrawElements with no EBO
    //   reads out of bounds and segfaults the driver (guest .obj in the
    //   shadow depth pass). Same idiom as the wrappers/model.c draw paths.
    if (mesh.indices != NULL) {
        //opencode: index type is ALWAYS short: gX_Mesh.indices is U16 by
        //   struct contract (EBO uploaded as count*sizeof(u16)), so the
        //   count only sizes the draw, never the type. Guessing INT past
        //   65535 indices reads U16 pairs as U32 (all out of range -> the
        //   mesh silently vanishes; e.g. 52k-tri tree.glb with U16 indices).
        GLenum idxType = GL_UNSIGNED_SHORT;
        glDrawElements(GL_TRIANGLES, mesh.triangleCount * 3, idxType, NULL);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
    glBindVertexArray(0);
}

//opencode: activate shader — binds via GL directly (raylib's rlEnableShader
//   is literally glUseProgram; no rlgl needed).
gX_I0 gX_RHI_Shader_Use(gX_U32 programId) {
    //opencode: no skip-on-cache — raylib's rlgl batch flush and other code call
    //   glUseProgram directly, desyncing g_lastProgramId. A skipped glUseProgram
    //   then left a stale program active (e.g. point shadow pass uploaded
    //   uniforms into program 3 instead of the depth shader, silently failing
    //   with GL_INVALID_OPERATION). Always call glUseProgram — cheap and correct.
    if (programId != g_lastProgramId) g_programSwitchCount++;
    g_lastProgramId = programId;
    glUseProgram((GLuint)programId);
    //opencode: do NOT call rlSetShader here — it would flush the rlgl batch
    //   (via rlDrawRenderBatch on shader change), corrupting accumulated
    //   vertex data from pending texture draws. glUseProgram alone is
    //   sufficient; the batch flush uses RLGL.State.currentShaderId which
    //   remains the default shader from rlInit (set by rlSetShader at init).
}

//opencode: compile a shader program from preprocessed source strings.
//   Adapted from gX_Shader_Compiler_CompileProgram in shader_compiler.c.
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Shader_Compile(const gX_I8 *vertSrc, const gX_I8 *geoSrc,
    const gX_I8 *fragSrc, const gX_I8 *name, gX_I8 *errorLog, gX_U64 errorLogSize)
{
    gX_U32 vsId = 0, gsId = 0, fsId = 0;
    //ai: unsupported drivers keep their synchronous compiler, with a visible loader.
    gX_I1 parallel = s_loadingCallback && shader_parallel_enable();
    if (errorLog && errorLogSize > 0) errorLog[0] = '\0';

    if (vertSrc) {
        vsId = glCreateShader(GL_VERTEX_SHADER);
        const GLchar *src = (const GLchar *)vertSrc;
        glShaderSource(vsId, 1, &src, NULL);
        glCompileShader(vsId);
        if (!shader_loading_wait(vsId, 0, parallel))
        {
            glDeleteShader(vsId);
            return 0;
        }
        GLint ok = 0;
        glGetShaderiv(vsId, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLchar log[1024] = {0};
            glGetShaderInfoLog(vsId, sizeof(log), NULL, log);
            //opencode: error hints - shader errors carry their recipe.
            const gX_I8 *hint = gX_RHI_Hint_For((const gX_I8 *)log);
            if (hint) gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: vertex compile: %s | %s", log, hint);
            else gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: vertex compile: %s", log);
            if (errorLog && errorLogSize > 0)
                snprintf(errorLog, errorLogSize, "vertex compile: %s", log);
            glDeleteShader(vsId); vsId = 0;
        }
    }
    if (geoSrc) {
        gsId = glCreateShader(GL_GEOMETRY_SHADER);
        const GLchar *src = (const GLchar *)geoSrc;
        glShaderSource(gsId, 1, &src, NULL);
        glCompileShader(gsId);
        if (!shader_loading_wait(gsId, 0, parallel))
        {
            if (vsId) glDeleteShader(vsId);
            glDeleteShader(gsId);
            return 0;
        }
        GLint ok = 0;
        glGetShaderiv(gsId, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLchar log[1024] = {0};
            glGetShaderInfoLog(gsId, sizeof(log), NULL, log);
            const gX_I8 *hint = gX_RHI_Hint_For((const gX_I8 *)log);
            if (hint) gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: geometry compile: %s | %s", log, hint);
            else gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: geometry compile: %s", log);
            if (errorLog && errorLogSize > 0)
                snprintf(errorLog, errorLogSize, "geometry compile: %s", log);
            glDeleteShader(gsId); gsId = 0;
        }
    }
    if (fragSrc) {
        //opencode: v0.3.8-debug - dump the exact preprocessed fragment source
        //   (GX_DUMP_SHADER=<name-substring> writes /tmp/gxshader_<name>.frag)
        {
            const gX_I8 *dumpName = getenv("GX_DUMP_SHADER");
            //opencode: match against the SOURCE (preprocessor #line
            //   directives embed the original file path; the program name
            //   is just the anonymous "shader")
            if (dumpName && fragSrc && strstr(fragSrc, dumpName)) {
                gX_I8 dumpPath[256];
                snprintf(dumpPath, sizeof(dumpPath), "/tmp/gxshader_%s.frag", name);
                FILE *df = fopen(dumpPath, "wb");
                if (df) { fwrite(fragSrc, 1, strlen(fragSrc), df); fclose(df); }
            }
        }
        fsId = glCreateShader(GL_FRAGMENT_SHADER);
        const GLchar *src = (const GLchar *)fragSrc;
        glShaderSource(fsId, 1, &src, NULL);
        glCompileShader(fsId);
        if (!shader_loading_wait(fsId, 0, parallel))
        {
            if (vsId) glDeleteShader(vsId);
            if (gsId) glDeleteShader(gsId);
            glDeleteShader(fsId);
            return 0;
        }
        GLint ok = 0;
        glGetShaderiv(fsId, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLchar log[1024] = {0};
            glGetShaderInfoLog(fsId, sizeof(log), NULL, log);
            const gX_I8 *hint = gX_RHI_Hint_For((const gX_I8 *)log);
            if (hint) gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: fragment compile: %s | %s", log, hint);
            else gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: fragment compile: %s", log);
            if (errorLog && errorLogSize > 0)
                snprintf(errorLog, errorLogSize, "fragment compile: %s", log);
            glDeleteShader(fsId); fsId = 0;
        }
    }

    if ((vertSrc && !vsId) || (fragSrc && !fsId)) {
        if (vsId) glDeleteShader(vsId);
        if (gsId) glDeleteShader(gsId);
        if (fsId) glDeleteShader(fsId);
        return 0;
    }

    gX_U32 programId = glCreateProgram();
    if (vsId) glAttachShader(programId, vsId);
    if (gsId) glAttachShader(programId, gsId);
    if (fsId) glAttachShader(programId, fsId);

    glBindAttribLocation(programId, 0, "vertexPosition");
    glBindAttribLocation(programId, 1, "vertexTexCoord");
    glBindAttribLocation(programId, 2, "vertexNormal");
    glBindAttribLocation(programId, 3, "vertexColor");
    glBindAttribLocation(programId, 4, "vertexTangent");
    glBindAttribLocation(programId, 5, "vertexTexCoord2");
    if (gX_RHI_Shader_LegacyGLSL()) {
        //opencode: pre-330 GLSL has no `layout(location)` fragment outputs —
        //   bind the engine's known output names before link on every legacy
        //   rung (130/140/150). Production 330+ keeps explicit locations.
        glBindFragDataLocation(programId, 0, "finalColor");
        glBindFragDataLocation(programId, 0, "fragColor");
        glBindFragDataLocation(programId, 0, "outDepth");
        glBindFragDataLocation(programId, 0, "o");
        glBindFragDataLocation(programId, 1, "outRoughness");
    }

    glLinkProgram(programId);
    if (!shader_loading_wait(programId, 1, parallel))
    {
        glDeleteProgram(programId);
        if (vsId) glDeleteShader(vsId);
        if (gsId) glDeleteShader(gsId);
        if (fsId) glDeleteShader(fsId);
        return 0;
    }
    GLint success = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[1024] = {0};
        glGetProgramInfoLog(programId, sizeof(infoLog), NULL, infoLog);
        const gX_I8 *hint = gX_RHI_Hint_For((const gX_I8 *)infoLog);
        if (hint) gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: link error: %s | %s", infoLog, hint);
        else gX_TraceLog(gX_LOG_ERROR, "RHI_SHADER: link error: %s", infoLog);
        if (errorLog && errorLogSize > 0 && errorLog[0] == '\0')
            snprintf(errorLog, errorLogSize, "link error: %s", infoLog);
        glDeleteProgram(programId); programId = 0;
    }

    if (programId > 0) {
        gX_TraceLog(gX_LOG_INFO, "RHI_SHADER: '%s' compiled OK (id=%u)", name ? name : "?", programId);
        gX_RHI_Debug_Label(programId, GL_PROGRAM, name);
    }

    if (vsId) glDeleteShader(vsId);
    if (gsId) glDeleteShader(gsId);
    if (fsId) glDeleteShader(fsId);
    return programId;
}

//opencode: RHI trace — read back a pixel from an FBO at (x, y) and log RGBA.
//opencode: v0.2.0 TEMP — real-GL context dump before raw DrawArrays
gX_I0 gX_RHI_Trace_ContextDump(const gX_I8 *label) {
    GLint vp[4] = {0}; glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean de = glIsEnabled(GL_DEPTH_TEST);
    GLboolean bl = glIsEnabled(GL_BLEND);
    GLint prog = 0, fbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    gX_TraceLog(gX_LOG_NOTICE,
        "CTXDUMP[%s] vp=(%d,%d,%d,%d) scissor=%d depth=%d blend=%d prog=%d fbo=%d",
        label ? label : "?", vp[0],vp[1],vp[2],vp[3],
        (int)sc,(int)de,(int)bl, prog, fbo);
}

gX_I0 gX_RHI_Trace_ProbeFBO(gX_U32 fboId, gX_U32 x, gX_U32 y, const gX_I8 *label) {
    GLint prevDrawFbo = 0, prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboId);
    if (fboId) glReadBuffer(GL_COLOR_ATTACHMENT0);
    //opencode: read color as float (works for both HDR and LDR formats)
    gX_F32 pixel[4] = {0};
    glReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_FLOAT, pixel);
    //opencode: convert to 8-bit for readable logging
    gX_U8 r = (gX_U8)((pixel[0] < 0.0f ? 0.0f : (pixel[0] > 1.0f ? 1.0f : pixel[0])) * 255.0f);
    gX_U8 g = (gX_U8)((pixel[1] < 0.0f ? 0.0f : (pixel[1] > 1.0f ? 1.0f : pixel[1])) * 255.0f);
    gX_U8 b = (gX_U8)((pixel[2] < 0.0f ? 0.0f : (pixel[2] > 1.0f ? 1.0f : pixel[2])) * 255.0f);
    gX_U8 a = (gX_U8)((pixel[3] < 0.0f ? 0.0f : (pixel[3] > 1.0f ? 1.0f : pixel[3])) * 255.0f);
    //opencode: also read depth at the same position
    gX_F32 depth = 0.0f;
    glReadPixels((GLint)x, (GLint)y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (gX_U32)prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (gX_U32)prevDrawFbo);
    gX_TraceLog(gX_LOG_INFO, "RHI_TRACE FBO[%s] @(%u,%u) = rgba(%u,%u,%u,%u) depth=%.4f",
        label ? label : "?", x, y, r, g, b, a, depth);
}

//opencode: RHI trace — read back a pixel from a texture at (x, y) and log RGBA.
gX_I0 gX_RHI_Trace_ProbeTex(gX_U32 texId, gX_U32 x, gX_U32 y, const gX_I8 *label) {
    gX_U32 tmpFbo = 0;
    glGenFramebuffers(1, &tmpFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tmpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        gX_U8 pixel[4] = {0};
        glReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        gX_TraceLog(gX_LOG_INFO, "RHI_TRACE TEX[%s] @(%u,%u) = rgba(%u,%u,%u,%u)",
            label ? label : "?", x, y, pixel[0], pixel[1], pixel[2], pixel[3]);
    } else {
        gX_TraceLog(gX_LOG_WARNING, "RHI_TRACE TEX[%s] FBO incomplete (0x%x)", label ? label : "?", status);
    }
    glDeleteFramebuffers(1, &tmpFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

//opencode: bind 2D texture to a specific unit, then reset to unit 0.
gX_I0 gX_RHI_Tex_BindOnUnit(gX_U32 texId, gX_U32 slot) {
    //opencode: use the texture's registered target — hardcoding GL_TEXTURE_2D
    //   bound 2D ARRAY textures (e.g. CSM array on unit 22) through the wrong
    //   target, flooding GL_INVALID_OPERATION "target doesn't match" per draw.
    gX_U32 target = gX_RHI_Tex_GetTarget(texId);
    if (target == 0) target = GL_TEXTURE_2D;
    g_textureBindCount++;
    gX_RHI_Tex_SetUnit(slot, texId, target);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture((GLenum)target, texId);
    glActiveTexture(GL_TEXTURE0);
}

//opencode: bind cubemap texture to a specific unit, then reset to unit 0.
gX_I0 gX_RHI_Tex_BindCubemapOnUnit(gX_U32 texId, gX_U32 slot) {
    g_textureBindCount++;
    gX_RHI_Tex_SetUnit(slot, texId, GL_TEXTURE_CUBE_MAP);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texId);
    glActiveTexture(GL_TEXTURE0);
}

//opencode: RHI draw arrays — thin wrapper around glDrawArrays.
gX_I0 gX_RHI_DrawArrays(gX_U32 mode, gX_I32 first, gX_U32 count) {
    gX_RHI_Validate_PreDraw("DrawArrays");
    gX_RHI_EventLog_Push(GX_RHI_EVENT_DRAW, mode, (gX_U32)first, count, "DrawArrays");
    //opencode: count RHI draw activity against the currently bound target.
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"DrawArrays", count);
    g_drawCallCount++;
    glDrawArrays((GLenum)mode, (GLint)first, (GLsizei)count);
}

//opencode: RHI draw elements — thin wrapper around glDrawElements.
gX_I0 gX_RHI_DrawElements(gX_U32 mode, gX_U32 count, gX_U32 type, const void *indices) {
    gX_RHI_Validate_PreDraw("DrawElements");
    gX_RHI_EventLog_Push(GX_RHI_EVENT_DRAW, mode, count, type, "DrawElements");
    //opencode: include indexed RHI submissions in target activity summaries.
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"DrawElements", count);
    g_drawCallCount++;
    glDrawElements((GLenum)mode, (GLsizei)count, (GLenum)type, indices);
}

//opencode: RHI shader delete — thin wrapper around glDeleteProgram.
gX_I0 gX_RHI_Shader_Delete(gX_U32 programId) {
    if (programId == 0) return;
    glDeleteProgram((GLuint)programId);
}

//opencode: GX_UNITRACE — runtime uniform audit (gfx uniform verifier).
//   When GX_UNITRACE=1, every RHI uniform upload logs program/location/
//   occupied range, and every GetUniformLocation logs its result, as
//   machine-parseable GX_UNITRACE lines on stderr. scripts/gfx_unitrace.py
//   joins them per program to flag stale/hardcoded locs, dead queries,
//   -1 misses, and overlapping mat4 ranges. Zero cost when unset.
static gX_I1 s_unitraceInit = 0;
static gX_I1 s_unitraceOn = 0;
static void Unitrace_Check(void)
{
    if (!s_unitraceInit) {
        s_unitraceInit = 1;
        {
            const char *e = getenv("GX_UNITRACE");
            s_unitraceOn = (e && e[0] != '0') ? 1 : 0;
        }
    }
}
static void Unitrace_Up(gX_I32 loc, gX_U32 locCount, const char *kind)
{
    Unitrace_Check();
    if (!s_unitraceOn || loc < 0) return;
    {
        GLint prog = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        fprintf(stderr, "GX_UNITRACE up prog=%d loc=%d end=%d kind=%s\n",
            (int)prog, (int)loc, (int)(loc + (gX_I32)locCount), kind);
    }
}
static void Unitrace_Loc(gX_U32 prog, const gX_I8 *name, gX_I32 loc)
{
    Unitrace_Check();
    if (!s_unitraceOn) return;
    fprintf(stderr, "GX_UNITRACE loc prog=%u name=%s loc=%d\n",
        (unsigned)prog, name ? (const char *)name : "?", (int)loc);
}

//opencode: RHI upload matrix uniform with transpose flag.
//   gxmath matrices are row-major; GLSL expects column-major.
//   Pass transpose=1 to convert during upload.
gX_I0 gX_RHI_UniformMatrix(gX_I32 location, gX_U32 count,
    gX_I1 transpose, const gX_F32 *value)
{
    if (location < 0) return;
    if (g_rhiValidateEnabled && gX_RHI_Validate_IsNaNv(value, count * 16))
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: NaN in mat4[%u] uniform loc %d", count, location);
    Unitrace_Up(location, 4u * count, "mat4");
    glUniformMatrix4fv((GLint)location, (GLsizei)count,
        transpose ? GL_TRUE : GL_FALSE, (const GLfloat *)value);
}

//opencode: get uniform location — wraps glGetUniformLocation.
//   Logs a warning when the uniform is not found (location == -1) to catch
//   typos and shader mismatches at load time instead of silent -1 propagation.
gX_I32 gX_RHI_GetUniformLocation(gX_U32 program, const gX_I8 *name)
{
    if (program == 0 || !name) return -1;
    gX_I32 loc = (gX_I32)glGetUniformLocation((GLuint)program, name);
    Unitrace_Loc(program, name, loc);
    if (loc < 0) {
        //opencode: TRACE (was WARNING) — effect shaders routinely query
        //   main-shader uniforms they don't declare; WARNING flooded the load
        //   log (cookie/UBO/nearPlane/depthMap per program). Typos surface at
        //   TRACE or via the NaN validator at upload time.
        gX_TraceLog(gX_LOG_TRACE, "RHI: uniform '%s' not found in program %u (typo or unused)", name, program);
    }
    return loc;
}

//opencode: get attribute location — thin wrapper around glGetAttribLocation.
gX_I32 gX_RHI_GetAttribLocation(gX_U32 program, const gX_I8 *name)
{
    if (program == 0 || !name) return -1;
    return (gX_I32)glGetAttribLocation((GLuint)program, name);
}

//opencode: upload a single int uniform — thin wrapper around glUniform1i.
gX_I0 gX_RHI_UniformInt(gX_I32 location, gX_I32 value)
{
    if (location < 0) return;
    Unitrace_Up(location, 1u, "int");
    glUniform1i((GLint)location, (GLint)value);
}

//opencode: upload a vec4 uniform — thin wrapper around glUniform4fv.
gX_I0 gX_RHI_UniformVec4(gX_I32 location, gX_U32 count, const gX_F32 *value)
{
    if (location < 0 || !value) return;
    if (g_rhiValidateEnabled && gX_RHI_Validate_IsNaNv(value, count * 4))
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: NaN in vec4[%u] uniform loc %d", count, location);
    Unitrace_Up(location, count, "vec4");
    glUniform4fv((GLint)location, (GLsizei)count, (const GLfloat *)value);
}

gX_U32 gX_RHI_Shader_GetDefault(gX_I0) {
    //opencode: backend-provided default shader — GLFW compiles its own
    //   (gx_shader_default.c); raylib returns rlGetShaderIdDefault() from its
    //   raylib-side gx_shader_default.c. Keeps the RHI free of rlgl calls.
    extern unsigned int gx_get_default_shader(void);
    unsigned int gxProg = gx_get_default_shader();
    if (gxProg > 0) return (gX_U32)gxProg;
    return 0;
}

gX_I32 *gX_RHI_Shader_GetDefaultLocs(gX_I0) {
    extern int *gx_get_default_shader_locs(void);
    return gx_get_default_shader_locs();
}

gX_U32 gX_RHI_Tex_GetDefault(gX_I0) {
    extern unsigned int gx_get_default_texture(void);
    return (gX_U32)gx_get_default_texture();
}

//opencode: instanced draws
gX_I0 gX_RHI_DrawArraysInstanced(gX_U32 mode, gX_I32 first, gX_U32 count, gX_U32 primcount) {
    gX_RHI_Validate_PreDraw("DrawArraysInstanced");
    gX_RHI_EventLog_Push(GX_RHI_EVENT_DRAW, mode, (gX_U32)first, count, "DrawArraysInstanced");
    //opencode: include instanced RHI draws in lifecycle summaries.
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"DrawArraysInstanced", count * primcount);
    g_drawCallCount++;
    glDrawArraysInstanced((GLenum)mode, (GLint)first, (GLsizei)count, (GLsizei)primcount);
}

gX_I0 gX_RHI_DrawElementsInstanced(gX_U32 mode, gX_U32 count, gX_U32 type, const void *indices, gX_U32 primcount) {
    gX_RHI_Validate_PreDraw("DrawElementsInstanced");
    gX_RHI_EventLog_Push(GX_RHI_EVENT_DRAW, mode, count, type, "DrawElementsInstanced");
    //opencode: include instanced indexed submissions in target summaries.
    gX_RHI_BufferAudit_Draw((const gX_I8 *)"DrawElementsInstanced", count * primcount);
    g_drawCallCount++;
    glDrawElementsInstanced((GLenum)mode, (GLsizei)count, (GLenum)type, indices, (GLsizei)primcount);
}

//opencode: compute shader dispatch
gX_I0 gX_RHI_DispatchCompute(gX_U32 groupsX, gX_U32 groupsY, gX_U32 groupsZ) {
    glDispatchCompute((GLuint)groupsX, (GLuint)groupsY, (GLuint)groupsZ);
}

//opencode: memory barrier for buffer/image/framebuffer access synchronization
gX_I0 gX_RHI_MemoryBarrier(gX_U32 barriers) {
    glMemoryBarrier((GLbitfield)barriers);
}

//opencode: bind shader storage buffer to a binding point
gX_I0 gX_RHI_SSBO_Bind(gX_U32 ssbo, gX_U32 binding) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)binding, (GLuint)ssbo);
}

//opencode: bind a texture image for compute load/store operations.
gX_I0 gX_RHI_Image_Bind(gX_U32 texture, gX_U32 unit, gX_U32 access, gX_U32 format) {
    glBindImageTexture((GLuint)unit, (GLuint)texture, 0, GL_FALSE, 0,
        (GLenum)access, (GLenum)format);
}

//opencode: uniform wrappers with NaN detection
gX_I0 gX_RHI_Uniform_Float(gX_I32 location, gX_F32 value) {
    if (location < 0) return;
    if (g_rhiValidateEnabled && gX_RHI_Validate_IsNaN(value))
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: NaN uploaded to uniform loc %d", location);
    Unitrace_Up(location, 1u, "float");
    glUniform1f((GLint)location, (GLfloat)value);
}

gX_I0 gX_RHI_Uniform_Vec2(gX_I32 location, gX_F32 x, gX_F32 y) {
    if (location < 0) return;
    if (g_rhiValidateEnabled && (gX_RHI_Validate_IsNaN(x) || gX_RHI_Validate_IsNaN(y)))
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: NaN in vec2 uniform loc %d", location);
    Unitrace_Up(location, 1u, "vec2");
    glUniform2f((GLint)location, (GLfloat)x, (GLfloat)y);
}

gX_I0 gX_RHI_Uniform_Vec3(gX_I32 location, gX_F32 x, gX_F32 y, gX_F32 z) {
    if (location < 0) return;
    if (g_rhiValidateEnabled && (gX_RHI_Validate_IsNaN(x) || gX_RHI_Validate_IsNaN(y) || gX_RHI_Validate_IsNaN(z)))
        gX_TraceLog(gX_LOG_WARNING, "RHI_VALIDATE: NaN in vec3 uniform loc %d", location);
    Unitrace_Up(location, 1u, "vec3");
    glUniform3f((GLint)location, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

gX_I0 gX_RHI_Uniform_IntArray(gX_I32 location, gX_U32 count, const gX_I32 *values) {
    if (location < 0 || !values) return;
    Unitrace_Up(location, count, "intarr");
    glUniform1iv((GLint)location, (GLsizei)count, (const GLint *)values);
}

gX_I0 gX_RHI_Uniform_FloatArray(gX_I32 location, gX_U32 count, const gX_F32 *values) {
    if (location < 0 || !values) return;
    Unitrace_Up(location, count, "floatarr");
    glUniform1fv((GLint)location, (GLsizei)count, (const GLfloat *)values);
}

gX_I0 gX_RHI_Uniform_Vec2Array(gX_I32 location, gX_U32 count, const gX_F32 *values) {
    if (location < 0 || !values) return;
    Unitrace_Up(location, count, "vec2arr");
    glUniform2fv((GLint)location, (GLsizei)count, (const GLfloat *)values);
}

gX_I0 gX_RHI_Uniform_Vec3Array(gX_I32 location, gX_U32 count, const gX_F32 *values) {
    if (location < 0 || !values) return;
    Unitrace_Up(location, count, "vec3arr");
    glUniform3fv((GLint)location, (GLsizei)count, (const GLfloat *)values);
}

gX_I0 gX_RHI_UniformBlockBinding(gX_U32 program, gX_U32 index, gX_U32 bindingPoint) {
    if (program == 0) return;
    glUniformBlockBinding((GLuint)program, (GLuint)index, (GLuint)bindingPoint);
}

gX_I0 gX_RHI_UBO_Bind(gX_U32 ubo, gX_U32 bindingPoint) {
    glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)bindingPoint, (GLuint)ubo);
}

gX_I0 gX_RHI_Finish(gX_I0) {
    glFinish();
}

//opencode: uniform value readback — wraps glGetUniformfv
gX_I0 gX_RHI_GetUniformfv(gX_U32 program, gX_I32 location, gX_U32 count, gX_F32 *values) {
    if (program == 0 || location < 0 || !values) return;
    glGetUniformfv((GLuint)program, (GLint)location, values);
}

//opencode: uniform value readback — wraps glGetUniformiv
gX_I0 gX_RHI_GetUniformiv(gX_U32 program, gX_I32 location, gX_U32 count, gX_I32 *values) {
    if (program == 0 || location < 0 || !values) return;
    glGetUniformiv((GLuint)program, (GLint)location, values);
}

//opencode: enumerate active uniforms — wraps glGetActiveUniform
gX_I32 gX_RHI_GetActiveUniform(gX_U32 program, gX_U32 index,
    gX_I8 *name, gX_U32 nameSize, gX_I32 *size, gX_U32 *type) {
    if (program == 0) return -1;
    GLenum glType = 0;
    GLint glSize = 0;
    GLsizei nameLen = 0;
    glGetActiveUniform((GLuint)program, (GLuint)index,
        (GLsizei)nameSize, &nameLen, &glSize, &glType, (GLchar *)name);
    if (size) *size = (gX_I32)glSize;
    if (type) *type = (gX_U32)glType;
    return (gX_I32)glGetUniformLocation((GLuint)program, name);
}

//opencode: uniform block index query — wraps glGetUniformBlockIndex
gX_I32 gX_RHI_GetUniformBlockIndex(gX_U32 program, const gX_I8 *name)
{
    if (program == 0 || !name) return -1;
    return (gX_I32)glGetUniformBlockIndex((GLuint)program, name);
}

//opencode: program integer parameter query — wraps glGetProgramiv
gX_I0 gX_RHI_GetProgramiv(gX_U32 program, gX_U32 pname, gX_I32 *params)
{
    if (program == 0 || !params) return;
    glGetProgramiv((GLuint)program, (GLenum)pname, params);
}

//opencode: create an empty program object — wraps glCreateProgram
gX_U32 gX_RHI_CreateProgram(gX_I0)
{
    return (gX_U32)glCreateProgram();
}

//opencode: read a program's binary — wraps glGetProgramBinary
//   (used by the shader binary cache; requires GL_ARB_get_program_binary)
gX_I0 gX_RHI_GetProgramBinary(gX_U32 program, gX_U32 bufSize,
    gX_I32 *length, gX_U32 *format, void *binary)
{
    if (program == 0 || !binary || !format) return;
    GLsizei outLen = 0;
    glGetProgramBinary((GLuint)program, (GLsizei)bufSize, &outLen,
        (GLenum *)format, binary);
    if (length) *length = (gX_I32)outLen;
}

//opencode: load a program from a binary — wraps glProgramBinary
gX_I0 gX_RHI_ProgramBinary(gX_U32 program, gX_U32 format,
    const void *binary, gX_U32 length)
{
    if (program == 0 || !binary) return;
    glProgramBinary((GLuint)program, (GLenum)format, binary, (GLsizei)length);
}

//opencode: GL string query — wraps glGetString
const gX_I8 *gX_RHI_GetString(gX_U32 name)
{
    return (const gX_I8 *)glGetString((GLenum)name);
}

//opencode: indexed extension string query — wraps glGetStringi(GL_EXTENSIONS, i).
//   glGetString(GL_EXTENSIONS) is GL_INVALID_ENUM in GL 3.2+ core profiles
//   (was called from hasExt in gl_level.c — 8 errors on the raylib backend).
const gX_I8 *gX_RHI_GetStringi(gX_U32 index)
{
    return (const gX_I8 *)glGetStringi(GL_EXTENSIONS, (GLuint)index);
}

//opencode: compile a compute shader program from GLSL source
//   name: optional label for GL_KHR_debug (pass NULL for anonymous).
gX_U32 gX_RHI_Shader_Compile_Compute(const gX_I8 *src, const gX_I8 *name, gX_I8 *errorLog, gX_U64 errorLogSize) {
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, (const GLchar**)&src, NULL);
    glCompileShader(cs);
    GLint compiled = 0;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        if (errorLog && errorLogSize > 0) {
            GLsizei logLen = 0;
            glGetShaderInfoLog(cs, (GLsizei)errorLogSize, &logLen, errorLog);
        }
        glDeleteShader(cs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    glDeleteShader(cs);
    if (linked != GL_TRUE) {
        if (errorLog && errorLogSize > 0) {
            GLsizei logLen = 0;
            glGetProgramInfoLog(prog, (GLsizei)errorLogSize, &logLen, errorLog);
        }
        glDeleteProgram(prog);
        return 0;
    }
    gX_TraceLog(gX_LOG_INFO, "RHI_SHADER: '%s' compute compile OK (id=%u)", name ? name : "?", (gX_U32)prog);
    gX_RHI_Debug_Label((gX_U32)prog, GL_PROGRAM, name);
    return (gX_U32)prog;
}
