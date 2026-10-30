/**********************************************************************************************
*
*   [gX: src/rhi/rhi_debug.c]
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


//opencode: RHI Debug — GL_KHR_debug implementations.
#include "rhi_debug.h"
#include "rhi_gl.h"
#include "../common/log.h"
#include <string.h>
#include <stdio.h>

//opencode: v0.3.8 — storm control. The planar reflection pass emits
//   ~14 GL_INVALID_OPERATION per frame from raylib's loc-based uniform
//   path (known + harmless, vision-verified; root call never isolated —
//   see gameX summary 2026-08-05). Per-frame floods filled logs and
//   stalled frame-count captures, so identical (type,id,message) repeats
//   are printed once, then counted with a periodic summary line.
#define GX_GL_DEBUG_DEDUP_SLOTS 16
#define GX_GL_DEBUG_SUMMARY_EVERY 500
typedef struct GX_GlDebugDedupEntry {
    gX_U32 type;
    gX_U32 id;
    gX_U32 count;
    gX_I8  msg[192];
} GX_GlDebugDedupEntry;
static GX_GlDebugDedupEntry s_glDedup[GX_GL_DEBUG_DEDUP_SLOTS];
static gX_U32 s_glDedupUsed = 0;

// ─── Error-hint table ───
//opencode: error hints - every entry is hard-won hunt knowledge. a/b are
//   substrings (b optional); first full match wins. Hints stay one line:
//   they ride log lines. Extend here AND in docs/error_hints.md.
typedef struct GX_HintEntry {
    const gX_I8 *a;
    const gX_I8 *b;
    const gX_I8 *hint;
} GX_HintEntry;
static const GX_HintEntry s_hints[] = {
    // GL framebuffer / feedback
    {"feedback loop", NULL, "HINT: texture sampled while attached to DRAW FBO - unbind the unit (planar unit 19?) or blit instead"},
    {"incomplete", "framebuffer", "HINT: incomplete FBO - attachments missing/mismatched (depth-exclusive rule; layered vs 2D; equal sizes)"},
    {"INCOMPLETE", NULL, "HINT: incomplete FBO - attachments missing/mismatched (depth-exclusive rule; layered vs 2D; equal sizes)"},
    // GL state / uniforms
    {"location", "-1", "HINT: invalid uniform location - hook-gated CacheLocs or optimized-out uniform (SetValues need rlEnableShader first)"},
    {"no program", NULL, "HINT: no current program - shader failed to load (NULL vertex path?) or BeginShader skipped"},
    {"program=0", NULL, "HINT: no current program - draw will be silently skipped; check shader load + BeginShader"},
    {"not a valid GL program", NULL, "HINT: deleted program still bound - check shader hot-reload swap order"},
    {"out of memory", NULL, "HINT: GPU OOM - check nvidia-smi; 110x 12MB guest loads share nothing, batch/instance instead"},
    {"viewport", NULL, "HINT: viewport vs FBO mismatch - BeginTexture missed after an RT resize?"},
    {"degenerate", NULL, "HINT: degenerate viewport - window minimized or RT sized 0?"},
    {"deleted", "texture", "HINT: use-after-delete on a unit - EvictUnits missed it?"},
    {"invalid tex", NULL, "HINT: use-after-delete on a unit - EvictUnits missed it?"},
    {"attrib", NULL, "HINT: VAO/attrib mismatch - depth shader must declare all 6 standard attributes (raylib 6.0 layout)"},
    {"nan", NULL, "HINT: NaN in pipeline - InjectNaN test left on? degenerate matrix (zero scale)?"},
    {"NaN", NULL, "HINT: NaN in pipeline - InjectNaN test left on? degenerate matrix (zero scale)?"},
    {"pending GL error", NULL, "HINT: earlier call left an error - bisect with GX_VALIDATE=1 draw labels (last label = nearest)"},
    // GLSL compile/link (RHI_SHADER lines; errors follow #line directives)
    {"undeclared identifier", NULL, "HINT: hot-reload #include search path? hook-gated uniform? vs/fs varying name mismatch?"},
    {"undefined variable", NULL, "HINT: hot-reload #include search path? hook-gated uniform? vs/fs varying name mismatch? (NVIDIA C1503 wording)"},
    {"no matching overloaded function", "texture", "HINT: sampler/texture kind mismatch (2D vs 2DArray vs shadow sampler)"},
    {"syntax error", NULL, "HINT: dump preprocessed source: GX_DUMP_SHADER=<substr> writes /tmp/gxshader_<name>.frag"},
    {"varyings", NULL, "HINT: vs-out vs fs-in varying mismatch (names AND types must match)"},
    {"link error", NULL, "HINT: linker - mismatched varyings or missing main(); dump with GX_DUMP_SHADER"},
    {"'main'", NULL, "HINT: missing main()? emptied TEMP region or bad revert?"},
    // shader file loading
    {"cannot open", NULL, "HINT: shader search path - check resources/shader + GX search dirs (falls back to direct load)"},
    {"preprocessing failed", NULL, "HINT: #include chain broken - check angle-bracket paths + search dirs (falls back to direct load)"},
};

//opencode: error-hint matcher - first entry whose substrings all occur wins.
const gX_I8 *gX_RHI_Hint_For(const gX_I8 *message)
{
    if (!message || !message[0]) return NULL;
    for (gX_U32 i = 0; i < sizeof(s_hints) / sizeof(s_hints[0]); i++) {
        if (strstr(message, s_hints[i].a) == NULL) continue;
        if (s_hints[i].b && strstr(message, s_hints[i].b) == NULL) continue;
        return s_hints[i].hint;
    }
    return NULL;
}

// ─── GL debug message callback (static, routed to log system) ───
static void GX_DebugCallback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
    (void)source;
    (void)length;
    (void)userParam;

    //opencode: dedupe — identical (type,id,message) repeats are suppressed
    //   and counted; a summary line fires every 500 suppressed repeats so
    //   steady storms stay visible without flooding the log
    if (message && message[0]) {
        gX_U32 si = 0;
        for (; si < s_glDedupUsed; si++) {
            if (s_glDedup[si].type == (gX_U32)type && s_glDedup[si].id == (gX_U32)id
                && strncmp(s_glDedup[si].msg, message, sizeof(s_glDedup[si].msg) - 1) == 0) {
                break;
            }
        }
        if (si < s_glDedupUsed) {
            s_glDedup[si].count++;
            if (s_glDedup[si].count % GX_GL_DEBUG_SUMMARY_EVERY == 0) {
                gX_TraceLog(gX_LOG_WARNING,
                    "GL_DEBUG: suppressed %u repeats of id=%u (%s)",
                    s_glDedup[si].count, s_glDedup[si].id, s_glDedup[si].msg);
            }
            return;
        }
        if (s_glDedupUsed < GX_GL_DEBUG_DEDUP_SLOTS) {
            gX_U32 slot = s_glDedupUsed++;
            s_glDedup[slot].type = (gX_U32)type;
            s_glDedup[slot].id = (gX_U32)id;
            s_glDedup[slot].count = 0;
            snprintf(s_glDedup[slot].msg, sizeof(s_glDedup[slot].msg), "%s", message);
        }
    }

    gX_I32 logLevel = gX_LOG_INFO;
    if (type == GL_DEBUG_TYPE_ERROR ||
        type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
        logLevel = gX_LOG_ERROR;
    } else if (severity == GL_DEBUG_SEVERITY_HIGH) {
        logLevel = gX_LOG_WARNING;
    } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        logLevel = gX_LOG_INFO;
    } else {
        logLevel = gX_LOG_DEBUG;
    }
    const gX_I8 *sevName = "UNKNOWN";
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:          sevName = "HIGH"; break;
        case GL_DEBUG_SEVERITY_MEDIUM:        sevName = "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_LOW:           sevName = "LOW"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:  sevName = "NOTIF"; break;
    }

    gX_TraceLog(logLevel, "GL_DEBUG [%s] src=0x%x id=%u %s", sevName, source, id, message ? message : "");
    //opencode: error hints - errors and high warnings carry their recipe.
    if ((logLevel == gX_LOG_ERROR || logLevel == gX_LOG_WARNING) && message && message[0]) {
        const gX_I8 *hint = gX_RHI_Hint_For((const gX_I8 *)message);
        if (hint) gX_TraceLog(logLevel, "GL_DEBUG id=%u %s", id, hint);
    }
}

gX_I0 gX_RHI_Debug_Init(gX_I0)
{
    if (gX_RHI_Debug_HasKHR()) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback((GLDEBUGPROC)GX_DebugCallback, NULL);
        // Suppress known-spam notifications (e.g., NVIDIA shader buffer info)
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
        gX_TraceLog(gX_LOG_INFO, "RHI_DEBUG: GL_KHR_debug initialized");
        return;
    }
    //opencode: raylib's GL 3.3 has no KHR_debug, but NVIDIA drivers expose
    //   GL_ARB_debug_output (glDebugMessageCallbackARB) which glad loads.
    //   The ARB extension shares enum values with KHR_debug (GL_DEBUG_OUTPUT,
    //   GL_DEBUG_SEVERITY_*, GL_DEBUG_TYPE_*). This surfaces GL errors on the
    //   raylib backend the same way the 4.3 callback does on GLFW — the GLFW
    //   error audit (28K errors/run) was only possible because GLFW had a
    //   callback; raylib was blind to the same bugs.
    if (GLAD_GL_ARB_debug_output) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallbackARB((GLDEBUGPROCARB)GX_DebugCallback, NULL);
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE,
            GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
        gX_TraceLog(gX_LOG_INFO, "RHI_DEBUG: GL_ARB_debug_output initialized (raylib GL 3.3)");
        return;
    }
    gX_TraceLog(gX_LOG_INFO, "RHI_DEBUG: no GL debug callback available — GL errors invisible");
}

gX_I1 gX_RHI_Debug_HasKHR(gX_I0)
{
    //opencode: raylib's bundled glad only loads the unprefixed debug functions
    //   under GL_VERSION_4_3 (no GLAD_GL_KHR_debug symbol), so a 4.3 check is
    //   required to avoid NULL function pointers. GLFW backend requests 4.3+,
    //   so labels/debug groups work there; on raylib's GL 3.3 they no-op.
    return (gX_I1)(GLAD_GL_VERSION_4_3 != 0);
}

gX_I0 gX_RHI_Debug_PushGroup(const gX_I8 *name)
{
    if (GLAD_GL_VERSION_4_3 && name)
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

gX_I0 gX_RHI_Debug_PopGroup(gX_I0)
{
    if (GLAD_GL_VERSION_4_3)
        glPopDebugGroup();
}

gX_I0 gX_RHI_Debug_Label(gX_U32 identifier, gX_U32 glEnum, const gX_I8 *label)
{
    if (!GLAD_GL_VERSION_4_3 || !label || identifier == 0) return;
    glObjectLabel((GLenum)glEnum, (GLuint)identifier, -1, label);
}
