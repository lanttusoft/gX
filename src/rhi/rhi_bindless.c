/**********************************************************************************************
*
*   [gX: src/rhi/rhi_bindless.c]
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


//opencode: RHI Bindless — GL_ARB_bindless_texture implementation.
#include <string.h>

#include "rhi_bindless.h"
#include "rhi_gl.h"
#include "rhi.h"
#include "rhi_state.h"
#include "../common/log.h"

//opencode: function pointer types for GL_ARB_bindless_texture
//   (not in the bundled glad).
typedef gX_U64 (GLAD_API_PTR *PFNGLGETTEXTURESAMPLERHANDLEARBPROC)(GLuint texture, GLuint sampler);
typedef gX_I0 (GLAD_API_PTR *PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
typedef gX_I0 (GLAD_API_PTR *PFNGLRELEASETEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
typedef gX_I0 (GLAD_API_PTR *PFNGLUNIFORMHANDLEUI64ARBPROC)(GLint location, GLuint64 value);

static gX_RHI_ProcAddressFn s_procAddressFn = NULL;

static PFNGLGETTEXTURESAMPLERHANDLEARBPROC s_glGetTextureSamplerHandleARB = NULL;
static PFNGLMAKETEXTUREHANDLERESIDENTARBPROC s_glMakeTextureHandleResidentARB = NULL;
static PFNGLRELEASETEXTUREHANDLERESIDENTARBPROC s_glReleaseTextureHandleResidentARB = NULL;
static PFNGLUNIFORMHANDLEUI64ARBPROC s_glUniformHandleui64ARB = NULL;
//opencode: glGetTextureSamplerHandleARB requires a real sampler object on
//   drivers that reject sampler 0; use one shared immutable sampler policy.
static gX_U32 s_defaultSampler = 0;

static gX_I1 s_supported = 0;

gX_I0 gX_RHI_SetProcAddressFn(gX_RHI_ProcAddressFn fn) {
    s_procAddressFn = fn;
}

//ai: keep platform-specific procedure lookup behind the registered backend.
gX_I0 *gX_RHI_GetProcAddress(const gX_I8 *name)
{
    return s_procAddressFn ? s_procAddressFn(name) : NULL;
}

//opencode: check the extension string via glGetStringi (core-profile safe).
static gX_I1 bindless_in_extensions(gX_I0) {
    gX_I32 numExt = 0;
    gX_RHI_GetIntegerv(GL_NUM_EXTENSIONS, &numExt);
    for (gX_I32 i = 0; i < numExt; i++) {
        const gX_I8 *ext = gX_RHI_GetStringi((gX_U32)i);
        if (ext && strcmp(ext, "GL_ARB_bindless_texture") == 0) return 1;
    }
    return 0;
}

gX_I1 gX_RHI_Bindless_Init(gX_I0) {
    s_supported = 0;
    s_glGetTextureSamplerHandleARB = NULL;
    s_glMakeTextureHandleResidentARB = NULL;
    s_glReleaseTextureHandleResidentARB = NULL;
    s_glUniformHandleui64ARB = NULL;
    if (s_defaultSampler != 0) {
        glDeleteSamplers(1, &s_defaultSampler);
        s_defaultSampler = 0;
    }

    //opencode: core since GL 4.2 — check the version string via gX_RHI_GetCaps
    //   results when available, else the extension string.
    if (!bindless_in_extensions()) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_BINDLESS: GL_ARB_bindless_texture not in extension list — disabled");
        return 0;
    }
    if (!s_procAddressFn) {
        gX_TraceLog(gX_LOG_WARNING, "RHI_BINDLESS: no proc-address loader registered — disabled");
        return 0;
    }

    s_glGetTextureSamplerHandleARB = (PFNGLGETTEXTURESAMPLERHANDLEARBPROC)
        s_procAddressFn("glGetTextureSamplerHandleARB");
    s_glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)
        s_procAddressFn("glMakeTextureHandleResidentARB");
    s_glReleaseTextureHandleResidentARB = (PFNGLRELEASETEXTUREHANDLERESIDENTARBPROC)
        s_procAddressFn("glReleaseTextureHandleResidentARB");
    s_glUniformHandleui64ARB = (PFNGLUNIFORMHANDLEUI64ARBPROC)
        s_procAddressFn("glUniformHandleui64ARB");

    if (s_glGetTextureSamplerHandleARB && s_glMakeTextureHandleResidentARB &&
        s_glReleaseTextureHandleResidentARB && s_glUniformHandleui64ARB) {
        glGenSamplers(1, &s_defaultSampler);
        if (s_defaultSampler == 0) {
            gX_TraceLog(gX_LOG_WARNING, "RHI_BINDLESS: sampler allocation failed — disabled");
            return 0;
        }
        glSamplerParameteri(s_defaultSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(s_defaultSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(s_defaultSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(s_defaultSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
        s_supported = 1;
        gX_TraceLog(gX_LOG_INFO, "RHI_BINDLESS: GL_ARB_bindless_texture supported");
    } else {
        gX_TraceLog(gX_LOG_WARNING, "RHI_BINDLESS: function pointers not available — disabled");
    }
    return s_supported;
}

gX_I1 gX_RHI_Bindless_IsSupported(gX_I0) {
    return s_supported;
}

gX_U64 gX_RHI_Bindless_GetHandle(gX_U32 texId, gX_U32 sampler) {
    if (!s_supported || !s_glGetTextureSamplerHandleARB || texId == 0) return 0;
    gX_U32 samplerId = sampler != 0 ? sampler : s_defaultSampler;
    if (samplerId == 0) return 0;
    return (gX_U64)s_glGetTextureSamplerHandleARB((GLuint)texId, (GLuint)samplerId);
}

gX_I1 gX_RHI_Bindless_MakeResident(gX_U64 handle) {
    if (!s_supported || !s_glMakeTextureHandleResidentARB || handle == 0) return 0;
    s_glMakeTextureHandleResidentARB((GLuint64)handle);
    return 1;
}

gX_I0 gX_RHI_Bindless_Release(gX_U64 handle) {
    if (!s_supported || !s_glReleaseTextureHandleResidentARB || handle == 0) return;
    s_glReleaseTextureHandleResidentARB((GLuint64)handle);
}

gX_I0 gX_RHI_Bindless_UniformHandle64(gX_I32 location, gX_U64 handle) {
    if (location < 0 || !s_supported || !s_glUniformHandleui64ARB) return;
    s_glUniformHandleui64ARB((GLint)location, (GLuint64)handle);
}
