/**********************************************************************************************
* 
*   [wrappers/shader.c]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/
//lanttu: original wrapper for raylib shader functions
#include "wrappers_header.h"
#include "shader.h"
#include "../common/error.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../common/shader_compiler.h"
#include "../common/text.h"
#include "../common/utils/shader.h"
#include "../rhi/rhi.h"
#include "../rhi/rhi_tex.h"
#include "../rhi/rhi_buffer_audit.h"
#include "../main.h"


//opencode: GL program binary caching — save compiled binary to skip GLSL recompile
//   Binary stored at <fragPath>.bin. Only valid if .bin mtime >= source mtime.
//opencode: compute a simple hash of the GL driver identifier string.
//   Used to invalidate shader binary caches when the GPU driver changes.
//   DJB2a hash — simple, fast, good enough for cache invalidation.
static gX_U64 gX_Shader_DriverHash(gX_I0)
{
    const gX_I8 *renderer = (const gX_I8*)gX_RHI_GetString(gX_RHI_RENDERER);
    const gX_I8 *version  = (const gX_I8*)gX_RHI_GetString(gX_RHI_VERSION);
    if (!renderer || !version) return 0;

    gX_U64 hash = 5381;
    for (const gX_I8 *p = renderer; *p; p++)
        hash = ((hash << 5) + hash) ^ (gX_U8)*p;
    for (const gX_I8 *p = version; *p; p++)
        hash = ((hash << 5) + hash) ^ (gX_U8)*p;
    return hash;
}

// Cache file header: [driverHash(8), reserved(8), format(4), binLen(4), bin...]
#define GX_SHADER_CACHE_HEADER_SIZE 24

//opencode: compute a simple 64-bit hash from a string
static gX_U64 gX_Shader_PathHash(const gX_I8 *s) {
    gX_U64 h = 0;
    if (!s) return h;
    while (*s) { h = h * 131 + (gX_U8)(*s); s++; }
    return h;
}

static gX_I1 gX_Shader_SaveBinary(gX_U32 progId, const gX_I8 *vertPath, const gX_I8 *fragPath)
{
    if (!vertPath && !fragPath) return false;
    if (!progId) return false;
    gX_I32 binLen = 0;
    gX_RHI_GetProgramiv(progId, gX_RHI_PROGRAM_BINARY_LENGTH, &binLen);
    if (binLen <= 0) return false;
    gX_U8 *bin = (gX_U8*)malloc((gX_U64)binLen);
    gX_U32 format = 0;
    gX_RHI_GetProgramBinary(progId, (gX_U32)binLen, NULL, (gX_U32*)&format, bin);
    // Compute cache key from both paths
    gX_U64 pathHash = gX_Shader_PathHash(vertPath) ^ (gX_Shader_PathHash(fragPath) * 0x9e3779b9);
    gX_I8 binPath[1024];
    snprintf(binPath, sizeof(binPath), "%s.shader_%016llx.bin",
        fragPath ? fragPath : (vertPath ? vertPath : "shader"), (unsigned long long)pathHash);
    FILE *f = fopen(binPath, "wb");
    if (!f) { free(bin); return false; }
    // Header: driverHash | pathHash | format | binLen | binary
    gX_U64 driverHash = gX_Shader_DriverHash();
    fwrite(&driverHash, sizeof(driverHash), 1, f);
    fwrite(&pathHash, sizeof(pathHash), 1, f);
    fwrite(&format, sizeof(format), 1, f);
    fwrite(&binLen, sizeof(binLen), 1, f);
    fwrite(bin, 1, (gX_U64)binLen, f);
    fclose(f);
    free(bin);
    return true;
}

static gX_U32 gX_Shader_TryLoadBinary(const gX_I8 *vertPath, const gX_I8 *fragPath)
{
    if (!vertPath && !fragPath) return 0;
    gX_U64 pathHash = gX_Shader_PathHash(vertPath) ^ (gX_Shader_PathHash(fragPath) * 0x9e3779b9);
    gX_I8 binPath[1024];
    snprintf(binPath, sizeof(binPath), "%s.shader_%016llx.bin",
        fragPath ? fragPath : (vertPath ? vertPath : "shader"), (unsigned long long)pathHash);
    FILE *f = fopen(binPath, "rb");
    if (!f) return 0;

    // Validate: driverHash + pathHash
    gX_U64 fileDriver = 0, filePathHash = 0;
    fread(&fileDriver, sizeof(fileDriver), 1, f);
    fread(&filePathHash, sizeof(filePathHash), 1, f);
    gX_U64 currentHash = gX_Shader_DriverHash();
    if (fileDriver != currentHash || filePathHash != pathHash)
    {
        fclose(f);
        remove(binPath);
        return 0;
    }

    gX_U32 format = 0;
    gX_I32 binLen = 0;
    fread(&format, sizeof(format), 1, f);
    fread(&binLen, sizeof(binLen), 1, f);
    if (binLen <= 0 || binLen > 1024*1024) { fclose(f); return 0; }
    gX_U8 *bin = (gX_U8*)malloc((gX_U64)binLen);
    fread(bin, 1, (gX_U64)binLen, f);
    fclose(f);
    gX_U32 prog = gX_RHI_CreateProgram();
    gX_RHI_ProgramBinary(prog, (gX_U32)format, bin, (gX_U32)binLen);
    free(bin);
    gX_I32 status = 0;
    gX_RHI_GetProgramiv(prog, gX_RHI_LINK_STATUS, &status);
    if (status != gX_RHI_TRUE) { gX_RHI_Shader_Delete(prog); return 0; }
    return prog;
}

// ─── Async Shader Preprocess Farm ───
// Preprocesses shader source files on worker threads (off the GL thread).
// The main thread then compiles the preprocessed source via GL.
// Thread pool is lazily created on first use, shared across all shader loads.

#include "../common/threading.h"

typedef struct gX_Shader_PreprocessTask {
    // Input
    const gX_I8 *vertLabel;   // path label for logging
    gX_I8 *vertPath;          // actual file path (copied)
    const gX_I8 *fragLabel;
    gX_I8 *fragPath;
    gX_I8 *defines;           // shader defines (copied)
    gX_U32 maxUniforms;
    // Output (filled by worker)
    gX_I8  *processedVert;    // preprocessed vertex source (allocated)
    gX_U64  vertLen;
    gX_I8  *processedFrag;    // preprocessed fragment source (allocated)
    gX_U64  fragLen;
    gX_I32  result;           // 0 = success, -1 = error
    gX_I1   done;
    // For binary cache: store the original file path for keying
    gX_I8   cacheKey[512];
} gX_Shader_PreprocessTask;

// Worker thread function: reads + preprocesses shader files
static gX_I0 gX_Shader_WorkerFunc(gX_I0 *arg)
{
    gX_Shader_PreprocessTask *task = (gX_Shader_PreprocessTask*)arg;
    task->result = -1;

    // Preprocess vertex shader
    if (task->vertPath && task->vertPath[0])
    {
        task->processedVert = gX_Shader_Compiler_Preprocess(task->vertPath, NULL, &task->vertLen);
        if (!task->processedVert)
        {
            // Fallback: will use LoadShader directly on main thread
            task->processedVert = 0;
        }
    }

    // Preprocess fragment shader
    if (task->fragPath && task->fragPath[0])
    {
        task->processedFrag = gX_Shader_Compiler_Preprocess(task->fragPath, task->defines, &task->fragLen);
        if (!task->processedFrag)
        {
            free(task->processedVert);
            task->processedVert = 0;
        }
    }

    task->done = 1;
    task->result = 0;
}

// Global thread pool for shader preprocessing (lazily created)
static gX_ThreadPool s_shaderPool;
static gX_I1        s_shaderPoolCreated = 0;

static gX_I32 gX_Shader_EnsurePool(gX_I0)
{
    if (s_shaderPoolCreated) return 0;
    // Use 2 worker threads for preprocessing (file I/O bound)
    gX_I32 r = gX_ThreadPool_Create(&s_shaderPool, 2, gX_Shader_WorkerFunc, 0);
    if (r == 0) s_shaderPoolCreated = 1;
    return r;
}

//opencode: raw GLSL 1.30 program loading bypasses raylib's source loader so
// the RHI can bind legacy fragment outputs before link. Recreate raylib's
// standard location table for the wrapper-facing shader contract.
static gX_I0 gX_Shader_PopulateDefaultLocs(Shader *shader)
{
    if (!shader || !shader->id) return;
    shader->locs = (int *)RL_CALLOC(RL_MAX_SHADER_LOCATIONS, sizeof(int));
    if (!shader->locs) return;
    for (gX_I32 i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) shader->locs[i] = -1;
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_POSITION]   = gX_RHI_GetAttribLocation(shader->id, "vertexPosition");
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_TEXCOORD01] = gX_RHI_GetAttribLocation(shader->id, "vertexTexCoord");
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_TEXCOORD02] = gX_RHI_GetAttribLocation(shader->id, "vertexTexCoord2");
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_NORMAL]     = gX_RHI_GetAttribLocation(shader->id, "vertexNormal");
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_TANGENT]    = gX_RHI_GetAttribLocation(shader->id, "vertexTangent");
    shader->locs[gX_RHI_SHADER_LOC_VERTEX_COLOR]      = gX_RHI_GetAttribLocation(shader->id, "vertexColor");
    shader->locs[gX_RHI_SHADER_LOC_MATRIX_MVP]        = gX_RHI_GetUniformLocation(shader->id, "mvp");
    shader->locs[gX_RHI_SHADER_LOC_MATRIX_VIEW]       = gX_RHI_GetUniformLocation(shader->id, "matView");
    shader->locs[gX_RHI_SHADER_LOC_MATRIX_PROJECTION] = gX_RHI_GetUniformLocation(shader->id, "matProjection");
    shader->locs[gX_RHI_SHADER_LOC_MATRIX_MODEL]      = gX_RHI_GetUniformLocation(shader->id, "matModel");
    shader->locs[gX_RHI_SHADER_LOC_MATRIX_NORMAL]     = gX_RHI_GetUniformLocation(shader->id, "matNormal");
    shader->locs[gX_RHI_SHADER_LOC_COLOR_DIFFUSE]     = gX_RHI_GetUniformLocation(shader->id, "colDiffuse");
    shader->locs[gX_RHI_SHADER_LOC_MAP_ALBEDO]        = gX_RHI_GetUniformLocation(shader->id, "texture0");
    shader->locs[gX_RHI_SHADER_LOC_MAP_METALNESS]     = gX_RHI_GetUniformLocation(shader->id, "texture1");
    shader->locs[gX_RHI_SHADER_LOC_MAP_NORMAL]        = gX_RHI_GetUniformLocation(shader->id, "texture2");
}

// Submit a shader for async preprocessing. Returns a task handle.
// Call gX_Shader_WaitForPreprocess before GL compilation.
static gX_Shader_PreprocessTask *gX_Shader_SubmitPreprocess(
    const gX_I8 *vertPath, const gX_I8 *vertLabel,
    const gX_I8 *fragPath, const gX_I8 *fragLabel,
    const gX_I8 *defines)
{
    if (gX_Shader_EnsurePool() != 0) return 0;
    gX_Shader_PreprocessTask *task = (gX_Shader_PreprocessTask*)
        calloc(1, sizeof(gX_Shader_PreprocessTask));
    if (!task) return 0;

    if (vertPath) { task->vertPath = strdup(vertPath); task->vertLabel = vertLabel ? vertLabel : vertPath; }
    if (fragPath) { task->fragPath = strdup(fragPath); task->fragLabel = fragLabel ? fragLabel : fragPath; }
    if (defines)  { task->defines  = strdup(defines); }

    // Build cache key from frag path (most unique)
    if (fragPath) snprintf(task->cacheKey, sizeof(task->cacheKey), "%s", fragPath);
    else if (vertPath) snprintf(task->cacheKey, sizeof(task->cacheKey), "%s", vertPath);

    gX_ThreadPool_Submit(&s_shaderPool, task);
    return task;
}

// Wait for a preprocess task to complete. Frees the task.
// Returns the preprocessed source (caller must free) or 0 on failure.
static gX_I0 gX_Shader_WaitForPreprocess(gX_Shader_PreprocessTask *task,
    gX_I8 **outVert, gX_U64 *outVertLen,
    gX_I8 **outFrag, gX_U64 *outFragLen)
{
    if (!task) return;
    // Spin until done (task is being processed by a worker)
    while (!task->done) gX_Thread_Sleep(1);

    if (outVert)    *outVert = task->processedVert;
    if (outVertLen) *outVertLen = task->vertLen;
    if (outFrag)    *outFrag = task->processedFrag;
    if (outFragLen) *outFragLen = task->fragLen;

    // Don't free the task yet — caller will free it after GL compilation
}

// Free a preprocess task and its data
static gX_I0 gX_Shader_FreePreprocess(gX_Shader_PreprocessTask *task)
{
    if (!task) return;
    free(task->vertPath);
    free(task->fragPath);
    free(task->defines);
    free(task->processedVert);
    free(task->processedFrag);
    free(task);
}

//opencode: internal compile via shader compiler pipeline
//opencode: null/headless guard — this function is entirely raylib-typed
//   (Shader/LoadShader/LoadShaderFromMemory/GetShaderLocation). It is only
//   called from gX_Shader_Load_Ex (backend-guarded); the null backend never
//   compiles shaders, so provide a zeroed stub instead.
static gX_Shader gX_Shader_Compile(
    const gX_I8 *vertSrc, const gX_I8 *vertLabel,
    const gX_I8 *fragSrc, const gX_I8 *fragLabel,
    const gX_I8 *defines)
{
    gX_U64 vertLen = 0, fragLen = 0;
    gX_I8 *processedVert = NULL;
    gX_I8 *processedFrag = NULL;
    gX_Shader result = {0};
    Shader raylibShader = {0};

    if (vertSrc) {
        processedVert = gX_Shader_Compiler_Preprocess(vertSrc, NULL, &vertLen);
        //opencode: v0.2.0 — RM heap passes the shader DIRECTORY as vertPath;
        //   readFile() on a directory succeeds on Linux but yields an EMPTY
        //   buffer, and raylib's file-loader fallback would pair the fragment
        //   with RAYLIB's default vertex shader — whose attribute locations
        //   are driver-assigned and DON'T match DrawScreenQuad's fixed VAO
        //   (layout 0/1) → degenerate sub-pixel quads → element shaders
        //   invisible. Instead: treat as "no vertex stage" and CONTINUE so
        //   the gx_default_vert injection at LoadFromMemory applies.
        if (processedVert && vertLen <= 1) {
            free(processedVert);
            processedVert = NULL;
            vertLen = 0;
            gX_TraceLog(gX_LOG_INFO, "SHADER: '%s' is not a vertex source — using gx_default_vert", vertLabel);
        }
    }
    if (fragSrc) {
        processedFrag = gX_Shader_Compiler_Preprocess(fragSrc, defines, &fragLen);
        //opencode: v0.3.8-debug - dump the preprocessed fragment source
        //   (GX_DUMP_SHADER=<label-substring> writes /tmp/gxshader_dump.frag)
        {
            const gX_I8 *dumpName = getenv("GX_DUMP_SHADER");
            if (dumpName && fragLabel && processedFrag && strstr(fragLabel, dumpName)) {
                FILE *df = fopen("/tmp/gxshader_dump.frag", "wb");
                if (df) {
                    fwrite(processedFrag, 1, fragLen > 0 ? (size_t)fragLen : strlen(processedFrag), df);
                    fclose(df);
                    gX_TraceLog(gX_LOG_INFO, "GX_DUMP: preprocessed frag written for %s", fragLabel);
                }
            }
        }
        if (!processedFrag) {
            //opencode: error hints - file fallback carries its recipe.
            const gX_I8 *hint = gX_RHI_Hint_For("cannot open");
            if (hint) gX_TraceLog(gX_LOG_WARNING, "SHADER: preprocessing failed for '%s', falling back to direct load | %s", fragLabel, hint);
            else gX_TraceLog(gX_LOG_WARNING, "SHADER: preprocessing failed for '%s', falling back to direct load", fragLabel);
            free(processedVert);
            //opencode: D12 — backend loader
            return raylib_Shader_Load(vertSrc, fragSrc);
        }
    }

    gX_TraceLog_WRAPPERS(gX_LOG_INFO, "SHADER: compiled '%s' + '%s'", vertLabel, fragLabel);
    //opencode: v0.2.0-era GXRM/GXTRACE debug prints removed (log noise on
    //   every load); GX_TRACE_SHADER_SRC env hook kept for pairing audits:
    if (getenv("GX_TRACE_SHADER_SRC")) {
        fprintf(stderr, "GX_TRACE: vertLabel=%s fragLabel=%s\n", vertLabel ? vertLabel : "(null)", fragLabel ? fragLabel : "(null)");
    }

    //opencode: parse uniforms from processed source for validation
    gX_Shader_Compiler_Uniform uniforms[GX_SHADER_COMPILER_MAX_UNIFORMS];
    gX_U32 uniformCount = 0;

    if (processedVert) {
        gX_Shader_Compiler_ParseUniforms(processedVert, vertLen,
            uniforms, &uniformCount, GX_SHADER_COMPILER_MAX_UNIFORMS);
    }
    if (processedFrag) {
        gX_Shader_Compiler_ParseUniforms(processedFrag, fragLen,
            uniforms, &uniformCount, GX_SHADER_COMPILER_MAX_UNIFORMS);
    }

    //opencode: try loading cached GL program binary first (avoids slow GLSL recompile)
    if (fragLabel && !result.id)
    {
        //opencode: DISABLED — program binary cache can load stale binaries
        //   when shader source changes but file path + driver hash stay the same.
        //   Forcing recompile ensures the UBO fix (individual uniforms) takes effect.
        gX_U32 cachedProg = 0; //gX_Shader_TryLoadBinary(vertLabel, fragLabel);
        if (cachedProg)
        {
            Shader cachedShader = {0};
            cachedShader.id = cachedProg;
            //opencode: populate locs array — raylib's LoadShaderFromMemory would
            //   do this after compilation, but the binary cache skips that path.
            //   Without this, all locs are -1 → DrawMesh skips standard uniform
            //   uploads → matView/matProjection remain identity → geometry clips.
            gX_Shader_PopulateDefaultLocs(&cachedShader);
            raylibShader = cachedShader;
            gX_TraceLog(gX_LOG_INFO, "SHADER: loaded cached binary for '%s'", fragLabel);
        }
    }

    if (!raylibShader.id)
    {
        //opencode: for fragment-only shaders (processedVert == NULL), supply our
        //   own vertex shader with explicit layout(location=N) qualifiers to match
        //   DrawScreenQuad's hardcoded VAO attrib locations 0 (position) and
        //   1 (texcoord). Raylib's default vertex shader lacks these qualifiers,
        //   causing the driver to assign arbitrary locations that mismatch the VAO.
        //   ES variant uses #version 300 es + precision (desktop 330 is invalid in ES).
        const gX_I1 esMode = gX_RHI_GetCaps() && gX_RHI_GetCaps()->isGLES;
        const gX_I32 legacyGLSL = gX_Shader_Compiler_DesktopLegacyGLSL();
        //opencode: no-layout vertex fallback for the legacy rungs — `in`
        // without locations is valid GLSL on 130, 140 and 150 alike.
        static gX_I8 gx_legacy_vert[256];
        if (legacyGLSL) {
            snprintf(gx_legacy_vert, sizeof(gx_legacy_vert),
                "#version %d\n"
                "in vec3 vertexPosition;\n"
                "in vec2 vertexTexCoord;\n"
                "out vec2 fragTexCoord;\n"
                "uniform mat4 mvp;\n"
                "void main() {\n"
                "    fragTexCoord = vertexTexCoord;\n"
                "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
                "}\n", (int)legacyGLSL);
        }
        const char *gx_default_vert = esMode ?
            "#version 300 es\n"
            "precision highp float;\n"
            "layout(location = 0) in vec3 vertexPosition;\n"
            "layout(location = 1) in vec2 vertexTexCoord;\n"
            "out vec2 fragTexCoord;\n"
            "uniform mat4 mvp;\n"
            "void main() {\n"
            "    fragTexCoord = vertexTexCoord;\n"
            "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
            "}\n" : legacyGLSL ?
            gx_legacy_vert :
            "#version 330\n"
            "layout(location = 0) in vec3 vertexPosition;\n"
            "layout(location = 1) in vec2 vertexTexCoord;\n"
            "out vec2 fragTexCoord;\n"
            "uniform mat4 mvp;\n"
            "void main() {\n"
            "    fragTexCoord = vertexTexCoord;\n"
            "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
            "}\n";

        //ai: loader-enabled programs use completion polling rather than raylib's
        //   immediate blocking link-status query. Keep the normal location table.
        if ((legacyGLSL || gX_RHI_Shader_HasLoading()) && processedFrag) {
            gX_I8 errorLog[1024] = {0};
            gX_I8 progLabel[32];
            if (legacyGLSL) snprintf(progLabel, sizeof(progLabel), "glsl%d", (int)legacyGLSL);
            else snprintf(progLabel, sizeof(progLabel), "loading");
            raylibShader.id = gX_RHI_Shader_Compile(
                processedVert ? processedVert : gx_default_vert, NULL, processedFrag,
                progLabel, errorLog, sizeof(errorLog));
            if (raylibShader.id) gX_Shader_PopulateDefaultLocs(&raylibShader);
            else gX_TraceLog(gX_LOG_ERROR, "SHADER: GLSL %d compile failed for '%s' + '%s': %s",
                (int)legacyGLSL,
                vertLabel ? vertLabel : "(default)", fragLabel ? fragLabel : "(default)", errorLog);
        } else {
            //opencode: D12 — backend loader
            gX_LogState.backendMode = true;
            raylibShader = gX_Shader_ConvertToBackend(
                raylib_Shader_LoadFromMemory(
                    processedVert ? processedVert : gx_default_vert,
                    processedFrag ? processedFrag : NULL
                ));
            gX_LogState.backendMode = false;
        }
    }

        //opencode: DISABLED — program binary cache disabled to force recompile
        //   on shader source changes. See load-side disable above.
        //if (raylibShader.id > 0 && fragLabel)
        //{
        //    if (gX_Shader_SaveBinary(raylibShader.id, vertLabel, fragLabel))
        //    {
        //        gX_TraceLog(gX_LOG_INFO, "SHADER: cached binary for '%s'", fragLabel);
        //    }
        //}

    if (raylibShader.id > 0) {
        result = gX_Shader_ConvertFromBackend(raylibShader);
    }
    //opencode: warn on compile failure — both hot-reload and raylib paths failed
    if (result.id == 0) {
        //opencode: error hints - the driver detail sits on the adjacent
        //   [BACKEND] Compile error line (C1503 = unknown name: search path?
        //   hook-gated uniform? varying mismatch?); dump via GX_DUMP_SHADER.
        gX_TraceLog(gX_LOG_ERROR, "SHADER: compile FAILED for '%s' + '%s' | HINT: see [BACKEND] Compile error above; dump: GX_DUMP_SHADER=<substr> writes /tmp/gxshader_dump.frag", vertLabel, fragLabel);
        GX_ERROR_SET(GX_ERR_GL, "shader compile failed: %s + %s", vertLabel,
            fragLabel);
    }

    //opencode: validate all parsed uniforms
    if (result.id > 0 && uniformCount > 0) {
        for (gX_U32 i = 0; i < uniformCount; i++) {
            //opencode: D14 — use the backend-aware location query (RHI on GLFW)
            gX_I32 loc = gX_Shader_GetLocation(result, uniforms[i].name);
            uniforms[i].location = loc;
            if (loc == -1) {
                //opencode: warn on missing uniform (may be legitimately optimized out)
                gX_TraceLog(gX_LOG_TRACE,
                    "SHADER_COMPILER: uniform '%s' (type %s) at line %d not found in '%s'/'%s'",
                    uniforms[i].name, uniforms[i].type, uniforms[i].line,
                    vertLabel ? vertLabel : "(default)",
                    fragLabel ? fragLabel : "(default)");
            } else {
                gX_TraceLog(gX_LOG_TRACE3,
                    "SHADER_COMPILER: uniform '%s' -> location %d",
                    uniforms[i].name, loc);
            }
        }
    }

    gX_Shader_Compiler_FreeSource(processedVert);
    gX_Shader_Compiler_FreeSource(processedFrag);
    return result;
}

gX_Shader gX_Shader_Load(const gX_I8 *vertpath, const gX_I8 *fragpath) {
    return gX_Shader_Load_Ex(vertpath, fragpath, NULL);
}

gX_Shader gX_Shader_Load_Ex(const gX_I8 *vertpath, const gX_I8 *fragpath, const gX_I8 *defines) {
    if (!vertpath && !fragpath) {
        gX_Shader nullShader = {0};
        return nullShader;
    }
    gX_Shader shader = gX_Shader_Compile(vertpath, vertpath, fragpath, fragpath, defines);
    //opencode: fallback — if shader compile failed, use raylib's built-in default shader
    if (shader.id == 0) {
        gX_TraceLog(gX_LOG_WARNING, "Shader compile failed: %s/%s — using default shader",
            vertpath ? vertpath : "?", fragpath ? fragpath : "?");
        shader.id = gX_RHI_Shader_GetDefault();
        //opencode: the fallback must carry valid locs too — attaching this
        //   shader to a model material and drawing with NULL locs derefs
        //   material.shader.locs in raylib_Mesh_Draw (battle stage crash).
        shader.locs = gX_RHI_Shader_GetDefaultLocs();
    }
    return shader;
}

//opencode: Shader Feature Level System — template registry and level compile

typedef struct {
    gX_ShaderType type;
    const gX_I8  *name;
    const gX_I8  *vertPath;
    const gX_I8  *fragPath;
    gX_U32        levelCap;
    gX_I1         canSkipLevel;
} gX_ShaderTemplate;

#define SHADER_PATH "/shader/"

static const gX_ShaderTemplate gX_shader_templates[GX_SHADER_TYPE_COUNT] = {
    {GX_SHADER_TYPE_STANDARD, "standard", "resources" SHADER_PATH "standard.vert", "resources" SHADER_PATH "standard.frag", 5, 1},
    {GX_SHADER_TYPE_DEPTH, "depth", "resources" SHADER_PATH "gfx_depth.vert", "resources" SHADER_PATH "gfx_depth.frag", 0, 0},
    {GX_SHADER_TYPE_SKY, "sky", "resources" SHADER_PATH "sky.vert", "resources" SHADER_PATH "sky.frag", 3, 1},
    {GX_SHADER_TYPE_WATER, "water", "resources" SHADER_PATH "water.vert", "resources" SHADER_PATH "water.frag", 3, 1},
    {GX_SHADER_TYPE_MAP, "map", "resources" SHADER_PATH "map.vert", "resources" SHADER_PATH "map.frag", 4, 1},
    {GX_SHADER_TYPE_WIREFRAME, "wireframe", "resources" SHADER_PATH "wireframe.vert", "resources" SHADER_PATH "wireframe.frag", 0, 0},
    {GX_SHADER_TYPE_PARTICLE, "particle", "resources" SHADER_PATH "particle.vert", "resources" SHADER_PATH "particle.frag", 3, 1},
    {GX_SHADER_TYPE_BILLBOARD, "billboard", "resources" SHADER_PATH "billboard.vert", "resources" SHADER_PATH "billboard.frag", 2, 1},
    // Fixed-function shaders (post-process, depth viz) — no level variation
    {GX_SHADER_TYPE_DEPTH_VISUALIZER, "depth_visualizer", "resources" SHADER_PATH "gfx_depth_visualizer.vert", "resources" SHADER_PATH "gfx_depth_visualizer.frag", 0, 0},
    {GX_SHADER_TYPE_POST_BLUR, "blur", "resources" SHADER_PATH "postprocess/blur.vert", "resources" SHADER_PATH "postprocess/blur.frag", 0, 0},
    {GX_SHADER_TYPE_POST_TONEMAP, "tonemap", "resources" SHADER_PATH "postprocess/tonemap.vert", "resources" SHADER_PATH "postprocess/tonemap.frag", 0, 0},
    {GX_SHADER_TYPE_POST_BLOOM, "bloom", "resources" SHADER_PATH "postprocess/bloom.vert", "resources" SHADER_PATH "postprocess/bloom.frag", 0, 0},
    {GX_SHADER_TYPE_POST_SSAO, "ssao", "resources" SHADER_PATH "postprocess/ssao.vert", "resources" SHADER_PATH "postprocess/ssao.frag", 0, 0},
    {GX_SHADER_TYPE_POST_SSR, "ssr", "resources" SHADER_PATH "postprocess/ssr.vert", "resources" SHADER_PATH "postprocess/ssr.frag", 0, 0},
    {GX_SHADER_TYPE_POST_COMPOSITE, "composite", "resources" SHADER_PATH "postprocess/composite.vert", "resources" SHADER_PATH "postprocess/composite.frag", 0, 0},
};

//opencode: compile a shader type at a given feature level
gX_Shader gX_Shader_Compile_Level(gX_ShaderType type, gX_F32 level, gX_U32 extraFlags, const gX_I8 *customDefines) {
    gX_Shader result = {0};

    if (type < 0 || type >= GX_SHADER_TYPE_COUNT) {
        gX_TraceLog(gX_LOG_ERROR, "SHADER_FEATURE: invalid shader type %d", type);
        return result;
    }

    // Look up template
    if (type >= GX_SHADER_TYPE_COUNT || !gX_shader_templates[type].name) {
        gX_TraceLog(gX_LOG_ERROR, "SHADER_FEATURE: no template for type %d", type);
        return result;
    }
    const gX_ShaderTemplate *tmpl = &gX_shader_templates[type];

    // Resolve actual level: min(desired, template cap, backend cap, force override)
    const gX_BackendShaderCaps *caps = gX_Shader_GetBackendCaps();
    gX_U32 actualLevel = (gX_U32)level;
    if (actualLevel > tmpl->levelCap) actualLevel = tmpl->levelCap;
    if (actualLevel > caps->maxLevel[type]) actualLevel = caps->maxLevel[type];
    gX_I32 forced = gX_Shader_GetForcedLevel();
    if (forced >= 0 && (gX_U32)forced < actualLevel) actualLevel = (gX_U32)forced;

    // Build defines string: feature flags + custom defines
    gX_U32 flags = 0;
    if (tmpl->canSkipLevel)
        flags = gX_Shader_FlagsFromLevel((gX_F32)actualLevel, extraFlags);

    gX_I8 *featureDefines = gX_Shader_FlagsToDefines(flags);
    gX_I8 *combinedDefines = NULL;

    if (featureDefines && customDefines) {
        combinedDefines = (gX_I8*)calloc(1, (size_t)(strlen(featureDefines) + strlen(customDefines) + 2));
        if (combinedDefines) {
            strcpy(combinedDefines, featureDefines);
            strcat(combinedDefines, customDefines);
        }
    } else if (featureDefines) {
        combinedDefines = featureDefines;
        featureDefines = NULL; // ownership transferred
    } else if (customDefines) {
        combinedDefines = (gX_I8*)strdup(customDefines);
    }

    // Compile via existing raylib path
    result = gX_Shader_Load_Ex(tmpl->vertPath, tmpl->fragPath, combinedDefines);

    gX_TraceLog(gX_LOG_INFO, "SHADER_FEATURE: type=%s level=%u (desired=%g) flags=0x%x shader=%u caps[type]=%u",
        tmpl->name, actualLevel, (double)level, flags, result.id, caps->maxLevel[type]);

    free(featureDefines);
    if (combinedDefines && combinedDefines != featureDefines) free(combinedDefines);

    return result;
}

//opencode: geometry shader load — compiles V+G+F using raw OpenGL via glad.
//   Bypasses raylib's LoadShaderFromMemory (which only supports V+F).
//   Falls back to standard V+F if geometry path is NULL or empty.
gX_Shader gX_Shader_Load_Geo(const gX_I8 *vertpath, const gX_I8 *geopath, const gX_I8 *fragpath) {
    if (!geopath || geopath[0] == '\0') {
        gX_TraceLog(gX_LOG_NOTICE, "SHADER: no geometry shader path, falling back to V+F");
        return gX_Shader_Load(vertpath, fragpath);
    }
    if (!vertpath && !fragpath) {
        gX_Shader nullShader = {0};
        return nullShader;
    }

    // Preprocess sources through the shader compiler pipeline
    gX_U64 vertLen = 0, geoLen = 0, fragLen = 0;
    gX_I8 *processedVert = vertpath ? gX_Shader_Compiler_Preprocess(vertpath, NULL, &vertLen) : NULL;
    gX_I8 *processedGeo  = gX_Shader_Compiler_Preprocess(geopath,  NULL, &geoLen);
    gX_I8 *processedFrag = fragpath ? gX_Shader_Compiler_Preprocess(fragpath, NULL, &fragLen) : NULL;

    // Fall back to V+F if any preprocessing failed
    if ((vertpath && !processedVert) || (fragpath && !processedFrag) || !processedGeo) {
        gX_TraceLog(gX_LOG_WARNING, "SHADER: geo preprocessing failed, falling back to V+F");
        gX_Shader_Compiler_FreeSource(processedVert);
        gX_Shader_Compiler_FreeSource(processedGeo);
        gX_Shader_Compiler_FreeSource(processedFrag);
        return gX_Shader_Load(vertpath, fragpath);
    }

    gX_TraceLog(gX_LOG_INFO, "SHADER: compiling geometry shader '%s' + '%s' + '%s'",
        vertpath ? vertpath : "(default)", geopath, fragpath ? fragpath : "(default)");

    //opencode: compile via RHI (V+G+F, attrib binding, error capture, link)
    gX_Shader result = {0};
    gX_I8 rhiError[1024] = {0};
    gX_U32 programId = gX_RHI_Shader_Compile(processedVert, processedGeo, processedFrag,
        "shader", rhiError, sizeof(rhiError));
    if (programId == 0) {
        gX_TraceLog(gX_LOG_ERROR, "SHADER: compile/link FAILED (vert=%s geo=%s frag=%s): %s",
            vertpath ? vertpath : "none", geopath ? geopath : "none",
            fragpath ? fragpath : "none", rhiError);
        gX_Shader_Compiler_FreeSource(processedVert);
        gX_Shader_Compiler_FreeSource(processedGeo);
        gX_Shader_Compiler_FreeSource(processedFrag);
        return result;
    }

    // Build gX_Shader result (locs array matching raylib convention)
    if (programId > 0) {
        gX_I32 *locs = (gX_I32 *)RL_CALLOC(RL_MAX_SHADER_LOCATIONS, sizeof(gX_I32));
        if (locs) {
            for (gX_I32 i = 0; i < RL_MAX_SHADER_LOCATIONS; i++) locs[i] = -1;
        }
        result.id = programId;
        result.locs = locs;
        //opencode: watch for hot-reload with geo path
        gX_Shader_Compiler_Watch_Ex(result, vertpath, fragpath, geopath, NULL, NULL);
        gX_TraceLog(gX_LOG_NOTICE, "SHADER: geometry shader loaded id=%u (%s + %s + %s)",
            programId,
            vertpath ? vertpath : "(default)",
            geopath,
            fragpath ? fragpath : "(default)");
    } else {
        // Fallback on link failure
        gX_TraceLog(gX_LOG_WARNING, "SHADER: geometry link failed, falling back to V+F");
        result = gX_Shader_Load(vertpath, fragpath);
    }

    gX_Shader_Compiler_FreeSource(processedVert);
    gX_Shader_Compiler_FreeSource(processedGeo);
    gX_Shader_Compiler_FreeSource(processedFrag);
    return result;
}

gX_I0 gX_Shader_Unload(gX_Shader shader) {
    //opencode: D12 — backend loader (skips if never compiled: id=0 avoids
    //   double-free in raylib's UnloadShader)
    gX_LogState.backendMode = true;
    raylib_Shader_Unload(shader);
    gX_LogState.backendMode = false;
}

gX_I32 gX_Shader_GetLocation( gX_Shader shader, const gX_I8 *locName) {
    gX_I32 loc = 0;
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE3,"Getting shader location");
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE3,"Name %s",locName);
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE4,"Converting shader");
    Shader sha = gX_Shader_ConvertToBackend(shader);
    gX_LogState.backendMode = true;
    loc = GetShaderLocation(sha,locName);
    gX_LogState.backendMode = false;
    gX_TraceLog_WRAPPERS(gX_LOG_TRACE3,"Getting location done");
    //opencode: GX_UNITRACE — same audit line as the RHI query path so the
    //   analyzer sees wrapper-queried locs (standard/taa/velocity shaders).
    {
        static gX_I1 unitraceInit = 0;
        static gX_I1 unitraceOn = 0;
        if (!unitraceInit) {
            unitraceInit = 1;
            {
                const char *e = getenv("GX_UNITRACE");
                unitraceOn = (e && e[0] != '0') ? 1 : 0;
            }
        }
        if (unitraceOn) {
            fprintf(stderr, "GX_UNITRACE loc prog=%u name=%s loc=%d\n",
                (unsigned)shader.id, locName ? (const char *)locName : "?",
                (int)loc);
        }
    }
    return loc;
}

//opencode: backend-agnostic attribute location query (GetShaderLocation only finds uniforms)
gX_I32 gX_Shader_GetLocationAttrib(gX_Shader shader, const gX_I8 *attribName) {
    gX_I32 loc = 0;
    Shader sha = gX_Shader_ConvertToBackend(shader);
    gX_LogState.backendMode = true;
    loc = GetShaderLocationAttrib(sha,attribName);
    gX_LogState.backendMode = false;
    return loc;
}

gX_I0 gX_Shader_SetValue(gX_Shader shader, gX_I32 location, gX_I0 *ptr, gX_I32 format) {
    //opencode: deferred shader wrappers expose locations before their GPU
    //   program is ready; never upload uniforms through program 0.
    if (location < 0 || shader.id == 0) return;
    if (gX_GL_IsRawProgram(shader.id)) {
        gX_RHI_Shader_Use(shader.id);
        switch (format) {
            case RL_SHADER_UNIFORM_INT:    gX_RHI_UniformInt(location, ((gX_I32*)ptr)[0]); break;
            case RL_SHADER_UNIFORM_FLOAT:  gX_RHI_Uniform_Float(location, ((gX_F32*)ptr)[0]); break;
            case RL_SHADER_UNIFORM_VEC2:   gX_RHI_Uniform_Vec2(location, ((gX_F32*)ptr)[0], ((gX_F32*)ptr)[1]); break;
            case RL_SHADER_UNIFORM_VEC3:   gX_RHI_Uniform_Vec3(location, ((gX_F32*)ptr)[0], ((gX_F32*)ptr)[1], ((gX_F32*)ptr)[2]); break;
            case RL_SHADER_UNIFORM_VEC4:   gX_RHI_UniformVec4(location, 1, (const gX_F32*)ptr); break;
            default: break;
        }
    } else {
        Shader sha = gX_Shader_ConvertToBackend(shader);
        SetShaderValue(sha,location,ptr,format);
    }
}

gX_I0 gX_Shader_SetValue_Array(gX_Shader shader, gX_I32 location, gX_I0 *ptr, gX_I32 format, gX_U32 amount) {
    //opencode: match scalar setter guard for deferred/unloaded programs.
    if (location < 0 || amount == 0 || shader.id == 0) return;
    if (gX_GL_IsRawProgram(shader.id)) {
        gX_RHI_Shader_Use(shader.id);
        switch (format) {
            case RL_SHADER_UNIFORM_INT:    gX_RHI_Uniform_IntArray(location, amount, (const gX_I32*)ptr); break;
            case RL_SHADER_UNIFORM_FLOAT:  gX_RHI_Uniform_FloatArray(location, amount, (const gX_F32*)ptr); break;
            case RL_SHADER_UNIFORM_VEC2:   gX_RHI_Uniform_Vec2Array(location, amount, (const gX_F32*)ptr); break;
            case RL_SHADER_UNIFORM_VEC3:   gX_RHI_Uniform_Vec3Array(location, amount, (const gX_F32*)ptr); break;
            case RL_SHADER_UNIFORM_VEC4:   gX_RHI_UniformVec4(location, amount, (const gX_F32*)ptr); break;
            default: break;
        }
    } else {
        Shader sha = gX_Shader_ConvertToBackend(shader);
        SetShaderValueV(sha,location,ptr,format,(gX_I32)amount);
    }
}

gX_I0 gX_Shader_SetValue_Texture(gX_Shader shader, gX_I32 location, gX_Texture texture) {
    //opencode: texture uploads also occur during deferred shader loading.
    if (location < 0 || shader.id == 0) return;
    if (gX_GL_IsRawProgram(shader.id)) {
        gX_RHI_Shader_Use(shader.id);
        gX_RHI_UniformInt(location, 0);
        gX_RHI_Tex_BindOnUnit(texture.id, 0);
    } else {
        Shader sha = gX_Shader_ConvertToBackend(shader);
        Texture tex = gX_Texture_ConvertToBackend(texture);
        SetShaderValueTexture(sha,location,tex);
    }
}

gX_I0 gX_Shader_SetValue_TextureSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot) {
    //opencode: raylib's slot wrapper binds directly through rlgl, outside RHI texture hooks.
    gX_U32 target = gX_RHI_Tex_GetTarget(texture.id);
    if (target == 0) target = gX_RHI_TEXTURE_2D;
    gX_RHI_BufferAudit_TextureBind(texture.id, (gX_U32)slot, target);
    raylib_Shader_SetValueTextureSlot(shader, location, texture, slot);
}

gX_I0 gX_Shader_SetValue_Cubemap(gX_Shader shader, gX_I32 location, gX_Texture texture) {
    raylib_Shader_SetValueCubemap(shader,location,texture);
}

gX_I0 gX_Shader_SetValue_CubemapSlot(gX_Shader shader, gX_I32 location, gX_Texture texture, gX_I32 slot) {
    //opencode: record cubemap slot reads performed by the raylib shader wrapper.
    gX_RHI_BufferAudit_TextureBind(texture.id, (gX_U32)slot, gX_RHI_TEXTURE_CUBE_MAP);
    raylib_Shader_SetValueCubemapSlot(shader, location, texture, slot);
}

gX_I0 gX_Shader_SetValue_Matrix(gX_Shader shader, gX_I32 location, gX_Matrix matrix) {
    //opencode: avoid matrix uploads against an unloaded shader program.
    if (location < 0 || shader.id == 0) return;
    if (gX_GL_IsRawProgram(shader.id)) {
        gX_RHI_Shader_Use(shader.id);
        gX_RHI_UniformMatrix(location, 1, 1, &matrix.m0);
    } else {
        Shader sha = gX_Shader_ConvertToBackend(shader);
        Matrix mat = gX_Matrix_ConvertToBackend(matrix);
        SetShaderValueMatrix(sha,location,mat);
    }
}

//opencode: Shader_Wrapper implementation (moved from common/utils/shader.c)

gX_I0 gX_Shader_Wrapper_Load(gX_Shader_Wrapper *sw) {
    const gX_I8 *vertString = 0;
    const gX_I8 *fragString = 0;
    if (getenv("GX_TRACE_SHADER_SRC"))   //opencode: TEMP debug — raw wrapper paths
        fprintf(stderr, "GX_WRAP: pathVert=[%s] pathFrag=[%s] uDV=%d uDF=%d\n",
            sw->pathVert, sw->pathFrag, (int)sw->useDefaultVert, (int)sw->useDefaultFrag);
    if (!(sw->useDefaultVert)) vertString = gX_Text_Format(sw->pathVert, sw->version);
    if (!(sw->useDefaultFrag)) fragString = gX_Text_Format(sw->pathFrag, sw->version);
    if (sw->defines[0] != '\0') {
        sw->shader = gX_Shader_Load_Ex(vertString, fragString, sw->defines);
    } else {
        sw->shader = gX_Shader_Load(vertString, fragString);
    }
    gX_Shader_Locations_Get(&sw->locs, &sw->shader);
    sw->loadingState.loaded = true;
    if (vertString || fragString) {
        gX_Shader_Compiler_Watch(sw->shader, vertString, fragString);
        if (sw->defines[0] != '\0') {
            gX_Shader_Compiler_SetDefines(sw->shader, sw->defines);
        }
    }
}

gX_I0 gX_Shader_Wrapper_Unload(gX_Shader_Wrapper *sw) {
    if (sw->shader.id > 0) gX_Shader_Compiler_Unwatch(sw->shader);
    gX_Shader_Unload(sw->shader);
    sw->loadingState.loaded = false;
}

gX_I0 gX_Shader_Wrapper_Update(gX_Shader_Wrapper *sw) {
    gX_I1 loadasapstate = (maindata.backendLoaded && sw->loadingState.loadAsap);
    gX_I1 loadbeforestate = false;
    gX_I1 unloadafterstate = false;
    if ((sw->loadingState.load || loadasapstate || loadbeforestate) && !(sw->loadingState.loaded)) {
        gX_Shader_Wrapper_Load(sw);
    }
    if (maindata.backendLoaded && sw->loadingState.loaded && sw->shader.id > 0) {
        if (gX_Shader_Compiler_CheckReload(&sw->shader)) {
            gX_Shader_Locations_Get(&sw->locs, &sw->shader);
        }
    }
    if ((sw->loadingState.unload || unloadafterstate) && sw->loadingState.loaded) {
        gX_Shader_Wrapper_Unload(sw);
    }
    if (sw->loadingState.reload && sw->loadingState.loaded) {
        gX_Shader_Wrapper_Unload(sw);
        gX_Shader_Wrapper_Load(sw);
    }
}
