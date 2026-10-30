/**********************************************************************************************
*
*   [gX: src/rhi/rhi_depth_probe.h]
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

//ai: debug-only live-allocation query and isolated format-matched depth precision probe.
#ifndef GX_RHI_DEPTH_PROBE_H
#define GX_RHI_DEPTH_PROBE_H
#include "../gxmath/gxmath.h"
#ifndef GX_LEAN_RELEASE
gX_I0 gX_RHI_Depth_Probe(gX_U32 SceneDepth, gX_U32 MultisampleDepth);
#endif
#endif
