/**********************************************************************************************
*
*   [gX: src/renderer/background/imgpro.c]
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


//opencode: ImageProcessor for background renderer palette quantisation + pivot sort
#include "imgpro.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/path.h"
#include "../../common/enum.h"
#include "../../wrappers/wrappers.h"

#include "../background.h"

// ===========================================================================
// Internal: squared Euclidean distance between two normalized colors (F32v4)
// ===========================================================================
static gX_F32 color_dist_sq(gX_F32v4 a, gX_F32v4 b)
{
    gX_F32 dr = a.x - b.x, dg = a.y - b.y, db = a.z - b.z;
    return dr*dr + dg*dg + db*db;
}

// ===========================================================================
// Internal: weighted HSV distance (hue weighted 3x)
// ===========================================================================
static gX_F32 hsv_dist_sq(gX_F32v3 a, gX_F32v3 b)
{
    gX_F32 dh = (a.x - b.x); dh = fminf(fabsf(dh), 1.0f - fabsf(dh)); // hue wraparound
    gX_F32 ds = a.y - b.y, dv = a.z - b.z;
    return 3.0f * dh*dh + ds*ds + dv*dv;
}

// ===========================================================================
// Internal: qsort comparator for sort entries (by t value)
// ===========================================================================
typedef struct { gX_I32 index; gX_F32 t; } SortEntry;
static gX_I32 sort_cmp(const void *a, const void *b)
{
    gX_F32 ta = ((const SortEntry*)a)->t;
    gX_F32 tb = ((const SortEntry*)b)->t;
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

// ===========================================================================
// Load source image
// ===========================================================================
gX_I0 gX_RendererBackground_ImgPro_Load(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Loading image %s", path);

    if (!bg) return;

    // Free previous
    if (bg->imgpro.imageUnprocessed.data)
        gX_Image_Unload(bg->imgpro.imageUnprocessed);

    bg->imgpro.imageUnprocessed = gX_Image_Load(path);
    if (!bg->imgpro.imageUnprocessed.data) {
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR, "BG_IMGPRO: Failed to load image %s", path);
        return;
    }

    // Ensure RGBA format for uniform pixel access
    if (bg->imgpro.imageUnprocessed.format != gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        gX_Image_Format(&bg->imgpro.imageUnprocessed, gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Loaded %dx%d image",
        bg->imgpro.imageUnprocessed.width,
        bg->imgpro.imageUnprocessed.height);
}

// ===========================================================================
// Process: quantize → HSV pivot sort → remap pixels
// ===========================================================================
gX_I0 gX_RendererBackground_ImgPro_Process(struct gX_RendererBackground *bg)
{
    if (!bg || !bg->imgpro.imageUnprocessed.data) return;

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Processing image");

    gX_Image *src = &bg->imgpro.imageUnprocessed;
    gX_I32 w = src->width, h = src->height;
    gX_I32 pixelCount = w * h;
    gX_U8 *srcPixels = (gX_U8*)src->data;

    // -----------------------------------------------------------------------
    // Step 1: Extract palette via median cut
    // -----------------------------------------------------------------------
    gX_I32 maxPalette = bg->imgpro.paletteMax > 0 ? bg->imgpro.paletteMax : gX_BG_PALETTE_COLORS_MAX;
    if (maxPalette < 2) maxPalette = 2;
    if (maxPalette > 256) maxPalette = 256;

    gX_I32 colorCount = 0;
    gX_Color *rawPalette = gX_Image_LoadPalette(*src, maxPalette, &colorCount);
    if (!rawPalette || colorCount < 2) {
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR, "BG_IMGPRO: Palette extraction failed (%d colors)", colorCount);
        if (rawPalette) gX_Image_UnloadPalette(rawPalette);
        return;
    }
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Extracted %d colors", colorCount);

    // Store palette size
    bg->imgpro.paletteSize = (gX_U32)colorCount;

    // Free old palette arrays
    if (bg->imgpro.paletteUnsorted) {
        gX_Image_UnloadPalette(bg->imgpro.paletteUnsorted);
    }
    bg->imgpro.paletteUnsorted = rawPalette;

    // Convert to normalized F32v4 for math
    gX_F32v4 *normColors = (gX_F32v4*)calloc((size_t)colorCount, sizeof(gX_F32v4));
    gX_F32v3 *hsvColors = (gX_F32v3*)calloc((size_t)colorCount, sizeof(gX_F32v3));
    if (!normColors || !hsvColors) {
        gX_TraceLog_BACKGROUND(gX_LOG_ERROR, "BG_IMGPRO: Out of memory for color arrays");
        free(normColors);
        free(hsvColors);
        return;
    }

    for (gX_I32 i = 0; i < colorCount; i++) {
        gX_F32v4 n = gX_Color_Normalize(rawPalette[i]);
        normColors[i] = n;
        gX_F32v3 hsv = gX_Color_ToHSV(rawPalette[i]);
        hsvColors[i] = (gX_F32v3){hsv.x / 360.0f, hsv.y, hsv.z}; // normalize hue to [0,1]
    }

    // -----------------------------------------------------------------------
    // Step 2: Find two pivots furthest apart in weighted HSV distance
    // -----------------------------------------------------------------------
    gX_I32 pivotA = 0, pivotB = 1;
    gX_F32 maxDist = 0.0f;
    for (gX_I32 i = 0; i < colorCount; i++) {
        for (gX_I32 j = i + 1; j < colorCount; j++) {
            gX_F32 d = hsv_dist_sq(hsvColors[i], hsvColors[j]);
            if (d > maxDist) {
                maxDist = d;
                pivotA = i;
                pivotB = j;
            }
        }
    }

    bg->imgpro.palettePivot1 = rawPalette[pivotA];
    bg->imgpro.palettePivot2 = rawPalette[pivotB];

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Pivots at indices %d and %d (dist=%f)",
        pivotA, pivotB, sqrtf(maxDist));

    // -----------------------------------------------------------------------
    // Step 3: Sort colors by projection onto pivotA→pivotB axis in HSV space
    //   t_i = dot(HSV_i - HSV_a, HSV_b - HSV_a) / |HSV_b - HSV_a|²
    //   sorted = [pivotA, ...colors sorted by t..., pivotB]
    // -----------------------------------------------------------------------
    gX_F32v3 ha = hsvColors[pivotA], hb = hsvColors[pivotB];
    gX_F32v3 axis = {hb.x - ha.x, hb.y - ha.y, hb.z - ha.z};
    gX_F32 axisLenSq = axis.x*axis.x + axis.y*axis.y + axis.z*axis.z;
    if (axisLenSq < 0.0001f) axisLenSq = 1.0f; // safety

    SortEntry *entries = (SortEntry*)calloc((size_t)colorCount, sizeof(SortEntry));
    if (!entries) {
        free(normColors);
        free(hsvColors);
        return;
    }

    for (gX_I32 i = 0; i < colorCount; i++) {
        entries[i].index = i;
        gX_F32v3 d = {hsvColors[i].x - ha.x, hsvColors[i].y - ha.y, hsvColors[i].z - ha.z};
        entries[i].t = (d.x*axis.x + d.y*axis.y + d.z*axis.z) / axisLenSq;
    }

    // Sort by t (quicksort via qsort)
    qsort(entries, (size_t)colorCount, sizeof(SortEntry), sort_cmp);

    // Build sorted palette: start with pivotA, then sorted colors (excluding pivotB),
    // ensure pivotB is last
    gX_Color *sortedRaw = (gX_Color*)calloc((size_t)colorCount, sizeof(gX_Color));
    if (!sortedRaw) {
        free(entries);
        free(normColors);
        free(hsvColors);
        return;
    }

    sortedRaw[0] = rawPalette[pivotA];
    gX_I32 sortedIdx = 1;
    for (gX_I32 si = 0; si < colorCount; si++) {
        gX_I32 idx = entries[si].index;
        if (idx == pivotA || idx == pivotB) continue;
        sortedRaw[sortedIdx++] = rawPalette[idx];
    }
    // Ensure pivotB is at last position
    if (pivotB != pivotA) {
        sortedRaw[colorCount - 1] = rawPalette[pivotB];
    }

    // Store in paletteSorted (reuse imgpro field)
    if (bg->imgpro.paletteSorted) {
        free(bg->imgpro.paletteSorted);
    }
    bg->imgpro.paletteSorted = sortedRaw;

    free(entries);

    // Also fill paletteResult (F32v4 array for GPU upload)
    if (bg->imgpro.paletteResult) {
        free(bg->imgpro.paletteResult);
    }
    bg->imgpro.paletteResult = (gX_F32v4*)calloc((size_t)colorCount, sizeof(gX_F32v4));
    if (bg->imgpro.paletteResult) {
        for (gX_I32 i = 0; i < colorCount; i++) {
            bg->imgpro.paletteResult[i] = gX_Color_Normalize(sortedRaw[i]);
        }
    }

    // -----------------------------------------------------------------------
    // Step 4: Remap each pixel to nearest sorted palette color
    //   Uses Floyd-Steinberg error diffusion dithering for smooth gradients
    //   instead of brute-force nearest neighbor. Scans left-to-right,
    //   top-to-bottom, distributing quantization error to neighboring pixels.
    // -----------------------------------------------------------------------
    // Free old result image
    if (bg->imgpro.imageResult.data)
        gX_Image_Unload(bg->imgpro.imageResult);

    // Create grayscale index image (8-bit, single channel)
    bg->imgpro.imageResult.data = calloc((size_t)pixelCount, 1);
    bg->imgpro.imageResult.width = w;
    bg->imgpro.imageResult.height = h;
    bg->imgpro.imageResult.mipmaps = 1;
    bg->imgpro.imageResult.format = gX_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;

    gX_U8 *idxOut = (gX_U8*)bg->imgpro.imageResult.data;

    // Pre-compute normalized sorted colors for distance comparison
    gX_F32v4 *sortedNorm = (gX_F32v4*)calloc((size_t)colorCount, sizeof(gX_F32v4));
    if (sortedNorm) {
        for (gX_I32 i = 0; i < colorCount; i++)
            sortedNorm[i] = gX_Color_Normalize(sortedRaw[i]);

        //opencode: Floyd-Steinberg error diffusion buffer (float, per-channel)
        //   errBuf[y][x] = error accumulated from previous pixels, [0]=R, [1]=G, [2]=B, [3]=A
        gX_F32 *errBuf = (gX_F32*)calloc((size_t)pixelCount * 4, sizeof(gX_F32));
        if (!errBuf) {
            gX_TraceLog_BACKGROUND(gX_LOG_ERROR, "BG_IMGPRO: Out of memory for error buffer");
            free(sortedNorm);
            free(normColors);
            free(hsvColors);
            return;
        }

        gX_I32 lastProgressPct = -1;
        for (gX_I32 py = 0; py < h; py++) {
            //opencode: scan direction alternates per row (serpentine) for better dither distribution
            gX_I32 xStart = 0, xEnd = w, xStep = 1;
            if (py % 2 == 1) { xStart = w - 1; xEnd = -1; xStep = -1; }

            for (gX_I32 px = xStart; px != xEnd; px += xStep) {
                gX_I32 srcOff = (py * w + px) * 4;
                gX_I32 errOff = (py * w + px) * 4;

                // Original pixel value + accumulated error
                gX_F32 r = srcPixels[srcOff + 0] / 255.0f + errBuf[errOff + 0];
                gX_F32 g = srcPixels[srcOff + 1] / 255.0f + errBuf[errOff + 1];
                gX_F32 b = srcPixels[srcOff + 2] / 255.0f + errBuf[errOff + 2];
                gX_F32 a = srcPixels[srcOff + 3] / 255.0f + errBuf[errOff + 3];
                // Clamp to [0,1]
                if (r < 0.0f) r = 0.0f;
                if (r > 1.0f) r = 1.0f;
                if (g < 0.0f) g = 0.0f;
                if (g > 1.0f) g = 1.0f;
                if (b < 0.0f) b = 0.0f;
                if (b > 1.0f) b = 1.0f;
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;

                gX_F32v4 pixelCol = {r, g, b, a};

                // Find nearest in sorted palette
                gX_I32 best = 0;
                gX_F32 bestDist = color_dist_sq(pixelCol, sortedNorm[0]);
                for (gX_I32 ci = 1; ci < colorCount; ci++) {
                    gX_F32 d = color_dist_sq(pixelCol, sortedNorm[ci]);
                    if (d < bestDist) {
                        bestDist = d;
                        best = ci;
                    }
                }

                idxOut[py * w + px] = (gX_U8)((best * 255) / (colorCount - 1));

                //opencode: compute quantization error and distribute via Floyd-Steinberg weights
                gX_F32 errR = r - sortedNorm[best].x;
                gX_F32 errG = g - sortedNorm[best].y;
                gX_F32 errB = b - sortedNorm[best].z;
                gX_F32 errA = a - sortedNorm[best].w;

                // Floyd-Steinberg weights (serpentine-aware: negate dx when scanning right-to-left)
                gX_I32 dx = (py % 2 == 1) ? -1 : 1;

                // Right neighbor (7/16)
                if (px + dx >= 0 && px + dx < w) {
                    gX_I32 nOff = (py * w + (px + dx)) * 4;
                    errBuf[nOff + 0] += errR * (7.0f / 16.0f);
                    errBuf[nOff + 1] += errG * (7.0f / 16.0f);
                    errBuf[nOff + 2] += errB * (7.0f / 16.0f);
                    errBuf[nOff + 3] += errA * (7.0f / 16.0f);
                }

                // Bottom-left (3/16)
                if (py + 1 < h && px - dx >= 0 && px - dx < w) {
                    gX_I32 nOff = ((py + 1) * w + (px - dx)) * 4;
                    errBuf[nOff + 0] += errR * (3.0f / 16.0f);
                    errBuf[nOff + 1] += errG * (3.0f / 16.0f);
                    errBuf[nOff + 2] += errB * (3.0f / 16.0f);
                    errBuf[nOff + 3] += errA * (3.0f / 16.0f);
                }

                // Bottom (5/16)
                if (py + 1 < h) {
                    gX_I32 nOff = ((py + 1) * w + px) * 4;
                    errBuf[nOff + 0] += errR * (5.0f / 16.0f);
                    errBuf[nOff + 1] += errG * (5.0f / 16.0f);
                    errBuf[nOff + 2] += errB * (5.0f / 16.0f);
                    errBuf[nOff + 3] += errA * (5.0f / 16.0f);
                }

                // Bottom-right (1/16)
                if (py + 1 < h && px + dx >= 0 && px + dx < w) {
                    gX_I32 nOff = ((py + 1) * w + (px + dx)) * 4;
                    errBuf[nOff + 0] += errR * (1.0f / 16.0f);
                    errBuf[nOff + 1] += errG * (1.0f / 16.0f);
                    errBuf[nOff + 2] += errB * (1.0f / 16.0f);
                    errBuf[nOff + 3] += errA * (1.0f / 16.0f);
                }
            }

            //opencode: log progress every 25%
            gX_I32 pct = (py + 1) * 100 / h;
            if (lastProgressPct < 0 || pct >= lastProgressPct + 25) {
                lastProgressPct = (pct / 25) * 25;
                gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: remapping %d%% (%d/%d rows) FS-dithered",
                    lastProgressPct, py + 1, h);
            }
        }

        free(errBuf);
        free(sortedNorm);
    }

    free(normColors);
    free(hsvColors);

    //opencode: transparency index 0 — any pixel with alpha < 128 in the original
    //   gets index 0 in the palette. This matches SNES convention where palette
    //   entry 0 is transparent and renders as color_bg in the background shader.
    //   Post-process: scan the original source alpha channel, override index 0
    //   for transparent pixels.
    {
        gX_I32 transCount = 0;
        for (gX_I32 py = 0; py < h; py++) {
            for (gX_I32 px = 0; px < w; px++) {
                gX_I32 srcOff = (py * w + px) * 4;
                if (srcPixels[srcOff + 3] < 128) {
                    idxOut[py * w + px] = 0;
                    transCount++;
                }
            }
        }
        if (transCount > 0) {
            gX_TraceLog_BACKGROUND(gX_LOG_INFO,
                "BG_IMGPRO: Transparent pixels set to index 0: %d/%d",
                transCount, pixelCount);
        }
    }

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Processing done — %d colors, %dx%d index map (Floyd-Steinberg)",
        colorCount, w, h);
}

// ===========================================================================
// Upload processed data to GPU textures
// ===========================================================================
gX_I0 gX_RendererBackground_ImgPro_Upload(struct gX_RendererBackground *bg)
{
    if (!bg) return;

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Uploading GPU textures");

    gX_U32 paletteSize = bg->imgpro.paletteSize;

    // Upload index texture (grayscale 8-bit)
    if (bg->imgpro.imageResult.data) {
        // Free old GPU texture
        if (bg->imgpro.textureUnprocessed.id > 0)
            gX_Texture_Unload(bg->imgpro.textureUnprocessed);

        bg->imgpro.textureUnprocessed = gX_LoadTextureFromImage(bg->imgpro.imageResult);
        gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Index texture id=%u (%dx%d)",
            bg->imgpro.textureUnprocessed.id,
            bg->imgpro.textureUnprocessed.width,
            bg->imgpro.textureUnprocessed.height);
    }

    // Upload palette texture (1×paletteSize, each texel = palette color)
    if (bg->imgpro.paletteResult && paletteSize > 0) {
        if (bg->imgpro.textureResult.id > 0)
            gX_Texture_Unload(bg->imgpro.textureResult);

        // Build a 2D pixel array: 1×N RGBA8 texture
        gX_U8 *palTexels = (gX_U8*)calloc(paletteSize * 4, 1);
        if (palTexels) {
            for (gX_U32 i = 0; i < paletteSize; i++) {
                palTexels[i*4 + 0] = (gX_U8)(bg->imgpro.paletteResult[i].x * 255.0f);
                palTexels[i*4 + 1] = (gX_U8)(bg->imgpro.paletteResult[i].y * 255.0f);
                palTexels[i*4 + 2] = (gX_U8)(bg->imgpro.paletteResult[i].z * 255.0f);
                palTexels[i*4 + 3] = (gX_U8)(bg->imgpro.paletteResult[i].w * 255.0f);
            }

            // Create temporary image to upload via raylib
            gX_Image palImg;
            palImg.data = palTexels;
            palImg.width = (gX_I32)paletteSize;
            palImg.height = 1;
            palImg.mipmaps = 1;
            palImg.format = gX_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

            bg->imgpro.textureResult = gX_LoadTextureFromImage(palImg);

            // Free CPU copy — Image data is NOT owned by the texture
            free(palTexels);

            gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Palette texture id=%u (1x%d)",
                bg->imgpro.textureResult.id, paletteSize);
        }
    }

    // Bind to bg palette system for renderer use
    bg->palette.paletteTexture = bg->imgpro.textureResult;
    bg->palette.mode = 2; // auto-switch to palette mode 2

    //ai: engage the palette system. The index + palette textures are inert
    //   while palette_enable stays 0 (the render would be a grayscale index
    //   map). The decomposed size drives modes 0/4/5 and mode 7's rows, and
    //   the decomposed colors also fill colors[] so every palette mode shares
    //   one palette.
    bg->palette.size.constant = (gX_F32)paletteSize;
    bg->palette.size.value = (gX_F32)paletteSize;
    if (bg->imgpro.paletteResult) {
        for (gX_U32 i = 0; i < paletteSize && i < gX_BG_PALETTE_COLORS_MAX; i++)
            bg->palette.colors[i].constant = bg->imgpro.paletteResult[i];
        bg->state.colorsDirty = true;
    }
    bg->state.paletteEnabled = true;
    gX_TraceLog_BACKGROUND(gX_LOG_INFO,
        "BG_IMGPRO: Palette bound: size=%u mode=2 enabled=1", paletteSize);

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Upload done");
}

// ===========================================================================
// Run: Load + Process + Upload
// ===========================================================================
gX_I0 gX_RendererBackground_ImgPro_Run(struct gX_RendererBackground *bg, const gX_I8 *path)
{
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Running full pipeline");
    gX_RendererBackground_ImgPro_Load(bg, path);
    gX_RendererBackground_ImgPro_Process(bg);
    gX_RendererBackground_ImgPro_Upload(bg);
}

// ===========================================================================
// Unload: free all GPU + CPU resources
// ===========================================================================
gX_I0 gX_RendererBackground_ImgPro_Unload(struct gX_RendererBackground *bg)
{
    if (!bg) return;

    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Unloading");

    if (bg->imgpro.textureUnprocessed.id > 0) {
        gX_Texture_Unload(bg->imgpro.textureUnprocessed);
        bg->imgpro.textureUnprocessed = (gX_Texture){0};
    }
    if (bg->imgpro.textureResult.id > 0) {
        gX_Texture_Unload(bg->imgpro.textureResult);
        bg->imgpro.textureResult = (gX_Texture){0};
        // Also clear bg palette texture reference
        bg->palette.paletteTexture = (gX_Texture){0};
    }
    if (bg->imgpro.imageUnprocessed.data) {
        gX_Image_Unload(bg->imgpro.imageUnprocessed);
        bg->imgpro.imageUnprocessed = (gX_Image){0};
    }
    if (bg->imgpro.imageResult.data) {
        gX_Image_Unload(bg->imgpro.imageResult);
        bg->imgpro.imageResult = (gX_Image){0};
    }
    if (bg->imgpro.paletteUnsorted) {
        gX_Image_UnloadPalette(bg->imgpro.paletteUnsorted);
        bg->imgpro.paletteUnsorted = NULL;
    }
    if (bg->imgpro.paletteSorted) {
        free(bg->imgpro.paletteSorted);
        bg->imgpro.paletteSorted = NULL;
    }
    if (bg->imgpro.paletteResult) {
        free(bg->imgpro.paletteResult);
        bg->imgpro.paletteResult = NULL;
    }

    bg->imgpro.paletteSize = 0;
    gX_TraceLog_BACKGROUND(gX_LOG_INFO, "BG_IMGPRO: Unload done");
}
