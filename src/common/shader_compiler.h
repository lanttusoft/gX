/**********************************************************************************************
*
*   [gX: src/common/shader_compiler.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types/primary.h"
#include "types/shader.h"

#define GX_SHADER_COMPILER_MAX_INCLUDE_DEPTH 8
#define GX_SHADER_COMPILER_MAX_UNIFORMS 512
#define GX_SHADER_COMPILER_MAX_SEARCH_PATHS 16
#define GX_SHADER_COMPILER_MIN_RELOAD_INTERVAL_MS 500

//opencode: parsed uniform declaration
typedef struct gX_Shader_Compiler_Uniform {
    gX_I8  name[128];
    gX_I8  type[64];
    gX_I32 line;
    gX_I32 location;
} gX_Shader_Compiler_Uniform;

//opencode: preprocess a shader source file
// reads file at 'path', resolves #include directives recursively,
// strips #version from includes, injects 'defines' after #version,
// replaces //gX_STANDARD_UNIFORMS with standard uniform block.
// returns allocated buffer (must free with gX_Shader_Compiler_FreeSource)
// sets *outLen to total length including null terminator.
// returns NULL on failure (logged).
gX_I8 *gX_Shader_Compiler_Preprocess(
    const gX_I8 *path,
    const gX_I8 *defines,
    gX_U64 *outLen
);

gX_I0 gX_Shader_Compiler_FreeSource(gX_I8 *source);

//opencode: parse uniform declarations from preprocessed source
// scans for: uniform <type> <name>; and layout(std140) uniform <block> { ... }
// populates uniforms[] up to maxUniforms, sets *count.
gX_I0 gX_Shader_Compiler_ParseUniforms(
    const gX_I8 *source,
    gX_U64 sourceLen,
    gX_Shader_Compiler_Uniform *uniforms,
    gX_U32 *count,
    gX_U32 maxUniforms
);

//opencode: include search path management
gX_I0 gX_Shader_Compiler_AddIncludePath(const gX_I8 *path);
gX_I0 gX_Shader_Compiler_ClearIncludePaths(gX_I0);

//opencode: set the standard uniform injection marker
// default is "//gX_STANDARD_UNIFORMS"
gX_I0 gX_Shader_Compiler_SetMarker(const gX_I8 *marker);

//opencode: GLES mode (--gles3) — the preprocessor rewrites desktop GLSL to
//   the ES dialect: `#version 330` → `#version 300 es` (320 es for .geom,
//   310 es for compute .glsl), injects `precision highp float/int;` after the
//   version line, and comments out desktop-only `#extension` directives.
//   Call once at backend init when the context is OpenGL ES.
gX_I0 gX_Shader_Compiler_SetESMode(gX_I1 on);

//opencode: desktop legacy-GLSL mode (130/140/150) — emit the matching
// #version, remove explicit layout qualifiers, and apply rung-appropriate
// rewrites (non-array interface blocks only below 150; instancing/inverse
// shims only below 140; the `input` sampler rename on every legacy rung).
// Selected after backend init for a native desktop GL 3.x context; the
// GX_GLSL130=1 environment hook is for the GL3.3-hosted 130 probe.
gX_I0 gX_Shader_Compiler_SetDesktopLegacyGLSL(gX_I32 version);
gX_I32 gX_Shader_Compiler_DesktopLegacyGLSL(gX_I0);

//opencode: Vulkan mode — the preprocessor emits `#version 450` (SPIR-V needs
//   450, not 330) and the RHI runs gX_Shader_Compiler_ToVulkanDialect() on the
//   preprocessed source to rewrite the rest of the dialect (locations, UBO,
//   sampler bindings). Set at vk backend init.
gX_I0 gX_Shader_Compiler_SetVulkanMode(gX_I1 on);
gX_I1 gX_Shader_Compiler_IsVulkanMode(gX_I0);

//opencode: Vulkan dialect post-processor (Stage 1C). Rewrites preprocessed
//   GLSL into the SPIR-V-compilable dialect: explicit vertex-input/fragment-
//   output/interface-block locations, non-opaque uniforms wrapped in a single
//   layout(set=0, binding=0) uniform UBO block, sampler bindings, reserved-word
//   renames. isVertex = 1 for the vertex stage (attributes 0-5), 0 for the
//   fragment stage (interface block still placed at location 6 to match).
//   Returns an allocated buffer (free() it), or NULL on failure.
gX_I8 *gX_Shader_Compiler_ToVulkanDialect(const gX_I8 *src, gX_U64 srcLen, gX_I1 isVertex);

//opencode: hot-reload support
// Callback invoked after a successful hot-reload.
// shader points to the caller's struct (id+locs already swapped).
// userData is the pointer passed to Watch_Ex.
typedef gX_I0 (*gX_Shader_ReloadCallback)(gX_Shader *shader, gX_I0 *userData);

// Watch a shader for file changes — stores mtimes internally.
// vertPath/fragPath/geoPath strings are copied internally.
// Pass NULL for geoPath to watch V+F only.
// callback is optional (NULL = no callback on reload).
gX_I0 gX_Shader_Compiler_Watch_Ex(
    gX_Shader shader,
    const gX_I8 *vertPath, const gX_I8 *fragPath, const gX_I8 *geoPath,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
);

// Convenience wrapper — same as Watch_Ex(shader, vertPath, fragPath, NULL, NULL, NULL).
gX_I0 gX_Shader_Compiler_Watch(gX_Shader shader, const gX_I8 *vertPath, const gX_I8 *fragPath);
gX_I0 gX_Shader_Compiler_Unwatch(gX_Shader shader);

//opencode: additive reload observers — Subscribe attaches a callback to an
//   EXISTING watched program without replacing the owner's paths, mtimes,
//   callback, or user data (Watch_Ex reuses the program slot and overwrites
//   all of them, so a second subscriber would clobber the first AND wipe the
//   owner's file-change detection). Multiple observers may subscribe to the
//   same program; each is invoked after a successful reload, after the owner
//   callback. Observers are lifetime-aware: they MUST call Unsubscribe before
//   their userData memory dies (e.g. in a Clear/Remove path).
//   Returns 1 on success, 0 when the program has no active watch entry or the
//   per-program observer table is full.
gX_I1 gX_Shader_Compiler_Subscribe(
    gX_Shader shader,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
);
gX_I0 gX_Shader_Compiler_Unsubscribe(
    gX_Shader shader,
    gX_Shader_ReloadCallback callback, gX_I0 *userData
);

//opencode: count active observers for a watched program (0 when unwatched) —
//   used by diagnostics and the observer unit tests.
gX_I32 gX_Shader_Compiler_GetObserverCount(gX_Shader shader);

// Set custom #define string for a watched shader.
// The defines string is used during hot-reload preprocessing.
// Call after Watch() if the shader was loaded with custom defines.
gX_I0 gX_Shader_Compiler_SetDefines(gX_Shader shader, const gX_I8 *defines);

// Query last compile error for a watched program (returns NULL if no error / not found)
const gX_I8 *gX_Shader_Compiler_GetLastError(gX_U32 programId);

// Check if a watched shader's source files changed
// If so, recompiles and swaps shader->id and shader->locs in place
// Old program is deleted. Returns 1 if reloaded, 0 if unchanged.
gX_I1 gX_Shader_Compiler_CheckReload(gX_Shader *shader);

//opencode: deferred swap — when SetDeferSwaps(1) is active, CheckReload
//   compiles immediately but queues the program swap; FlushDeferredSwaps
//   applies queued swaps (id + locs + callback + old program delete).
//   Prevents deleting the currently-bound program mid-render-pass.
gX_I0 gX_Shader_Compiler_SetDeferSwaps(gX_I1 defer);
gX_I0 gX_Shader_Compiler_FlushDeferredSwaps(gX_I0);

//opencode: the standard uniform block injected at the marker — std140 UBO
// block name gX_Std (for glGetUniformBlockIndex), unnamed instance (global scope).
// viewPosition/lightPosition are vec3 (12 bytes), padded to 16 by std140.
// ambientColor is vec4 (16 bytes). ambientFactor is float (4 bytes) + 12b pad.
// Matrices (mvp, matModel, matNormal, matLightSpace) remain individual uniforms
// because raylib sets mvp/matModel per-draw via glUniformMatrix4fv.
#define GX_SHADER_COMPILER_STANDARD_UNIFORMS \
    "uniform vec3  viewPosition;\n" \
    "uniform vec3  lightPosition;\n" \
    "uniform vec4  ambientColor;\n" \
    "uniform float ambientFactor;\n" \
    "uniform mat4 mvp;\n" \
    "uniform mat4 matModel;\n" \
    "uniform mat4 matNormal;\n" \
    "uniform mat4 matLightSpace;\n" \
    "uniform float nearPlane;\n" \
    "uniform float farPlane;\n"

#ifdef __cplusplus
}
#endif

#endif //SHADER_COMPILER_H
