/**********************************************************************************************
*
*   [gX: src/renderer/postprocess/schema2.h]
*   [renderer/postprocess/schema2.h] — PP offset-based schema declarations
*   Ported from gameX — GX backend: struct definitions only
*
*   Copyright (c) 2025 lanttu
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

#ifndef RENDERER_POSTPROCESS_SCHEMA2_H
#define RENDERER_POSTPROCESS_SCHEMA2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"

typedef struct gX_RendererPostProcess gX_RendererPostProcess;

gX_I32 gX_RendererPostProcess_Preset_Save_New(gX_RendererPostProcess *pp, const gX_I8 *path);
gX_I32 gX_RendererPostProcess_Preset_Load_New(gX_RendererPostProcess *pp, const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_POSTPROCESS_SCHEMA2_H
