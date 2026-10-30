/**********************************************************************************************
*
*   [gX: src/rhi/rhi_validate.h]
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


//opencode: RHI Validation — per-draw GL state validation for debugging.
//   Catches silent state bugs (program=0, incomplete FBO, viewport mismatch,
//   unbound textures, NaN uniforms) before they produce garbage output.
//   All checks are guarded by a global enable flag — zero overhead when off.
#ifndef RHI_VALIDATE_H
#define RHI_VALIDATE_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Global enable flag (toggled from ImGui) ───
// Set to 1 to enable per-draw validation; 0 to skip all checks.
extern gX_I1 g_rhiValidateEnabled;

// ─── NaN injection (for testing detection) ───
//   Sets all float uniforms in the given program to NaN. Only runs when
//   g_rhiValidateEnabled is 1. Subsequent draws will trigger NaN warnings.
gX_I0 gX_RHI_Validate_InjectNaN(gX_U32 program);

// ─── Per-draw validation — call before every glDrawArrays/Elements ───
// Validates: current program, FBO completeness, viewport vs FBO size,
//   bound texture validity, and current GL error state.
// Logs all issues via gX_TraceLog at WARNING level.
gX_I0 gX_RHI_Validate_PreDraw(const gX_I8 *label);

// ─── NaN check on float uniform uploads ───
// Returns 1 if any component is NaN (not-a-number).
gX_I1 gX_RHI_Validate_IsNaN(gX_F32 value);
gX_I1 gX_RHI_Validate_IsNaNv(const gX_F32 *values, gX_U32 count);

#ifdef __cplusplus
}
#endif

#endif
