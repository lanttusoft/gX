/**********************************************************************************************
*
*   [gX: src/renderer/background/imgpro.h]
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


#ifndef RENDERER_BACKGROUND_IMGPRO_H
#define RENDERER_BACKGROUND_IMGPRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "../../wrappers/image.h"
#include "../../wrappers/texture.h"

//opencode: ImageProcessor for background renderer palette quantisation
//   Load image → quantize palette → sort with HSV pivot → remap pixels → GPU textures
//   The sorted palette + index texture are bound to tex0/tex1 for palette mode 2 use.

struct gX_RendererBackground; // forward decl

// Load source image from file into imgpro.imageUnprocessed
gX_I0 gX_RendererBackground_ImgPro_Load(struct gX_RendererBackground *bg, const gX_I8 *path);

// Process: quantize, find HSV-pivot-sorted palette, remap pixels → imageResult
gX_I0 gX_RendererBackground_ImgPro_Process(struct gX_RendererBackground *bg);

// Upload processed textures to GPU (index texture + palette texture)
gX_I0 gX_RendererBackground_ImgPro_Upload(struct gX_RendererBackground *bg);

// Load + Process + Upload in one call
gX_I0 gX_RendererBackground_ImgPro_Run(struct gX_RendererBackground *bg, const gX_I8 *path);

// Free GPU textures and CPU image data
gX_I0 gX_RendererBackground_ImgPro_Unload(struct gX_RendererBackground *bg);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_IMGPRO_H
