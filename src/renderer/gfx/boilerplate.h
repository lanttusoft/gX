/**********************************************************************************************
*
*   [gX: src/renderer/gfx/boilerplate.h]
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



/**********************************************************************************************
*
*   [src/renderer/gfx/boilerplate.h]
*   gX — a game engine
*
*   Reusable boilerplate macros for GFX subsystem lifecycle:
*     RT init, shader init, unload, reload.
*
**********************************************************************************************/

#ifndef RENDERER_GFX_BOILERPLATE_H
#define RENDERER_GFX_BOILERPLATE_H

#ifdef __cplusplus
extern "C" {
#endif

//opencode: RT init — full resolution, HDR, loadAsap=true
#define GX_GFX_RT_INIT_HDR(gfx, field) \
    gX_RendererGFX_RenderTexture_Init(&(gfx)->field); \
    (gfx)->field.wrapper.width = maindata.renderWidth; \
    (gfx)->field.wrapper.height = maindata.renderHeight; \
    (gfx)->field.hdr = true; \
    (gfx)->field.wrapper.loadingState.loadAsap = true

//opencode: RT init — full resolution, LDR, loadAsap=true
#define GX_GFX_RT_INIT_LDR(gfx, field) \
    gX_RendererGFX_RenderTexture_Init(&(gfx)->field); \
    (gfx)->field.wrapper.width = maindata.renderWidth; \
    (gfx)->field.wrapper.height = maindata.renderHeight; \
    (gfx)->field.hdr = false; \
    (gfx)->field.wrapper.loadingState.loadAsap = true

//opencode: RT init — half resolution, HDR, loadAsap=true
#define GX_GFX_RT_INIT_HALF_HDR(gfx, field) \
    gX_RendererGFX_RenderTexture_Init(&(gfx)->field); \
    (gfx)->field.wrapper.width = maindata.renderWidth / 2; \
    (gfx)->field.wrapper.height = maindata.renderHeight / 2; \
    (gfx)->field.hdr = true; \
    (gfx)->field.wrapper.loadingState.loadAsap = true

//opencode: RT init — half resolution, no HDR override, no loadAsap
#define GX_GFX_RT_INIT_HALF(gfx, field) \
    gX_RendererGFX_RenderTexture_Init(&(gfx)->field); \
    (gfx)->field.wrapper.width = maindata.renderWidth / 2; \
    (gfx)->field.wrapper.height = maindata.renderHeight / 2

//opencode: unload RT if loaded
#define GX_GFX_RT_UNLOAD(gfx, field) \
    if ((gfx)->field.wrapper.loadingState.loaded) \
        gX_RendererGFX_RenderTexture_Unload(&(gfx)->field)

//opencode: reload RT (unload + reset to loadAsap)
#define GX_GFX_RT_RELOAD(gfx, field) \
    GX_GFX_RT_UNLOAD(gfx, field); \
    (gfx)->field.wrapper.loadingState = (gX_LoadingState){.loadAsap = true}

//opencode: shader init with path
#define GX_GFX_SHADER_INIT(gfx, field, name) \
    gX_RendererGFX_Shader_Init(&(gfx)->field, 0, GX_PATH("shader", name))

//opencode: iterate all USED entries in a gX_Heap.
//   var is a Type* pointer scoped to the loop body.
//   continue = skip to next heap entry, break = exits current entry (outer loop continues).
#define GX_HEAP_FOREACH(heap, type, var) \
    for (gX_U32 _gx_h_i_ = 0; _gx_h_i_ < (heap).amount; _gx_h_i_++) \
        if (!(heap).meta[_gx_h_i_].USED) continue; \
        else for (type *var = &((type*)(heap).data)[_gx_h_i_], *_gx_h_brk_ = var; \
                  _gx_h_brk_; _gx_h_brk_ = NULL)

//opencode: mark all entries in a gX_Heap as unused.
#define GX_HEAP_REMOVE_ALL(heap) \
    for (gX_U32 _gx_h_ra_ = 0; _gx_h_ra_ < (heap).amount; _gx_h_ra_++) \
        (heap).meta[_gx_h_ra_].USED = false

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_BOILERPLATE_H
