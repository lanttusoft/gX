/**********************************************************************************************
*
*   [gX: src/common/hardware.h]
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


#ifndef GX_COMMON_HARDWARE_H
#define GX_COMMON_HARDWARE_H

#include "types.h"
#include "../rhi/rhi_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

//ai: hardware requirement tiers — the engine's portable feature levels, not
//   a GPU shopping list (docs/hardware.md maps them to example hardware):
//     below-minimum: no GLSL 3.30 (or no framebuffer objects)
//     minimum:       desktop GL 3.3+ / ES 3.0+, forward renderer + GUI
//     standard:      compute tier (GL 4.3+ / ES 3.1+) — GPU light culling,
//                    compute effects, or the GL 4.2 bindless-era raster path
//     recommended:   desktop GL 4.6 with compute + SSBO + bindless +
//                    cube-map arrays (full feature set, raymarch at 1.0 scale)
typedef enum {
    gX_HW_TIER_BELOW_MINIMUM = 0,
    gX_HW_TIER_MINIMUM,
    gX_HW_TIER_STANDARD,
    gX_HW_TIER_RECOMMENDED,
} gX_HardwareTier;

//ai: pure classifier over RHI caps — headless-testable, no GL context needed.
gX_HardwareTier gX_Hardware_Tier_Classify(const gX_RHI_Caps *caps,
    gX_I8 *reason, gX_U32 reasonSize);

const gX_I8 *gX_Hardware_Tier_String(gX_HardwareTier tier);

//ai: print the capability report (vendor/renderer/version, GL level, feature
//   flags, key limits) plus the tier classification at NOTICE level. Called
//   with --hw-report or GX_HW_REPORT=1.
gX_I0 gX_Hardware_Report(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_COMMON_HARDWARE_H
