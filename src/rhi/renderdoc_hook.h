/**********************************************************************************************
*
*   [gX: src/rhi/renderdoc_hook.h]
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
#ifndef GX_RHI_RENDERDOC_HOOK_H
#define GX_RHI_RENDERDOC_HOOK_H

//opencode: v0.2.0 — RenderDoc in-app trigger (active only under injection)

#include "../common/types.h"

gX_I0 gX_RenderDoc_Init(gX_I0);
gX_I0 gX_RenderDoc_Trigger(gX_I0);

#endif //GX_RHI_RENDERDOC_HOOK_H
