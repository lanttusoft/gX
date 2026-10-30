/**********************************************************************************************
*
*   [gX: src/common/hardware.c]
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


//ai: hardware requirement tiers + the runtime capability report. The tier
//   rules are deliberately feature-based (what the engine can run), so they
//   are portable and unit-testable; docs/hardware.md adds example hardware.

#include "hardware.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "../rhi/rhi.h"

const gX_I8 *gX_Hardware_Tier_String(gX_HardwareTier tier)
{
    switch (tier) {
        case gX_HW_TIER_BELOW_MINIMUM: return "below-minimum";
        case gX_HW_TIER_MINIMUM:       return "minimum";
        case gX_HW_TIER_STANDARD:      return "standard";
        case gX_HW_TIER_RECOMMENDED:   return "recommended";
        default:                       return "unknown";
    }
}

static gX_I1 hw_meets_shader_base(const gX_RHI_Caps *caps)
{
    if (!caps) return false;
    if (caps->isGLES) return caps->shaderModel >= 300;
    return caps->shaderModel >= 330;
}

static gX_I1 hw_is_recommended(const gX_RHI_Caps *caps)
{
    //ai: capability-driven — the engine's default context advertises 3.3 (a
    //   compatibility profile) while the ARB extensions carry the real
    //   features, so the tier follows the flags, not the version string.
    if (caps->isGLES) return false;
    return caps->hasCompute && caps->hasSSBO && caps->hasBindless
        && caps->hasCubeMapArray && caps->hasTessellation;
}

static gX_I1 hw_is_standard(const gX_RHI_Caps *caps)
{
    if (caps->hasCompute && caps->hasSSBO) return true;
    if (!caps->isGLES && caps->glMajor == 4 && caps->glMinor >= 2)
        return true;
    return false;
}

gX_HardwareTier gX_Hardware_Tier_Classify(const gX_RHI_Caps *caps,
    gX_I8 *reason, gX_U32 reasonSize)
{
    if (reason && reasonSize > 0) reason[0] = '\0';
    if (!caps || !hw_meets_shader_base(caps) || !caps->hasFBO) {
        if (reason && reasonSize > 0) {
            snprintf((char *)reason, reasonSize,
                "needs %s shader support and framebuffer objects",
                caps && caps->isGLES ? "GLSL ES 3.00" : "GLSL 3.30");
        }
        return gX_HW_TIER_BELOW_MINIMUM;
    }
    if (hw_is_recommended(caps)) {
        if (reason && reasonSize > 0) {
            snprintf((char *)reason, reasonSize,
                "full feature set: compute + SSBO + bindless + cube-map "
                "arrays + tessellation");
        }
        return gX_HW_TIER_RECOMMENDED;
    }
    if (hw_is_standard(caps)) {
        if (reason && reasonSize > 0) {
            snprintf((char *)reason, reasonSize,
                "compute + SSBO available (GPU light culling, compute "
                "effects)");
        }
        return gX_HW_TIER_STANDARD;
    }
    if (reason && reasonSize > 0) {
        snprintf((char *)reason, reasonSize,
            "forward renderer + GUI; compute-tier and bindless paths unavailable");
    }
    return gX_HW_TIER_MINIMUM;
}

gX_I0 gX_Hardware_Report(gX_I0)
{
    const gX_RHI_Caps *caps = gX_RHI_GetCaps();
    if (!caps) {
        gX_TraceLog(gX_LOG_ERROR, "HW_REPORT: no RHI caps (context missing)");
        return;
    }
    const gX_I8 *vendor = gX_RHI_GetString(gX_RHI_VENDOR);
    const gX_I8 *renderer = gX_RHI_GetString(gX_RHI_RENDERER);
    const gX_I8 *version = gX_RHI_GetString(gX_RHI_VERSION);
    const gX_I8 *shading = gX_RHI_GetString(gX_RHI_SHADING_LANGUAGE_VERSION);
    gX_TraceLog(gX_LOG_NOTICE, "HW_REPORT: vendor=\"%s\" renderer=\"%s\" "
        "version=\"%s\" glsl=\"%s\"",
        vendor ? (const char *)vendor : "?",
        renderer ? (const char *)renderer : "?",
        version ? (const char *)version : "?",
        shading ? (const char *)shading : "?");
    gX_TraceLog(gX_LOG_NOTICE, "HW_REPORT: gl=%d.%d es=%d shader=%d",
        caps->glMajor, caps->glMinor, (gX_I32)caps->isGLES,
        caps->shaderModel);
    gX_TraceLog(gX_LOG_NOTICE, "HW_REPORT: flags compute=%d ssbo=%d "
        "bindless=%d tess=%d subroutine=%d cubemaparray=%d immutable=%d "
        "mstex=%d instancing=%d geometry=%d multidraw=%d programbinary=%d "
        "transformfeedback=%d fbo=%d",
        (gX_I32)caps->hasCompute, (gX_I32)caps->hasSSBO,
        (gX_I32)caps->hasBindless, (gX_I32)caps->hasTessellation,
        (gX_I32)caps->hasShaderSubroutine, (gX_I32)caps->hasCubeMapArray,
        (gX_I32)caps->hasImmutableStorage, (gX_I32)caps->hasMSTexture,
        (gX_I32)caps->hasInstancing, (gX_I32)caps->hasGeometryShaders,
        (gX_I32)caps->hasMultiDrawIndirect, (gX_I32)caps->hasProgramBinary,
        (gX_I32)caps->hasTransformFeedback, (gX_I32)caps->hasFBO);
    gX_TraceLog(gX_LOG_NOTICE, "HW_REPORT: limits texture_units=%d "
        "texture_size=%d color_attachments=%d uniform_components=%d",
        caps->maxTextureUnits, caps->maxTextureSize,
        caps->maxColorAttachments, caps->maxUniformComponents);
    gX_I8 reason[192];
    gX_HardwareTier tier = gX_Hardware_Tier_Classify(caps, reason,
        sizeof(reason));
    gX_TraceLog(gX_LOG_NOTICE, "HW_REPORT: tier=%s reason=\"%s\"",
        (const char *)gX_Hardware_Tier_String(tier), (const char *)reason);
}
