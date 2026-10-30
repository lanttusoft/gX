/**********************************************************************************************
* 
*   [common/types.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025 lanttu
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef gX_COMMON_TYPES_H
#define gX_COMMON_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "types/3d.h"
#include "types/audio.h"
#include "types/color.h"
#include "types/matrix.h"
#include "types/primary.h"
#include "types/shader.h"
#include "types/shape.h"
#include "types/state.h"
#include "types/text.h"
#include "types/texture.h"
#include "types/vectors.h"

// [raylib] borrowed
// NPatchInfo, n-patch layout info
typedef struct gX_NPatchInfo {
    gX_Rectangle    source; // Texture source rectangle
    gX_I32          left;   // Left border offset
    gX_I32          top;    // Top border offset
    gX_I32          right;  // Right border offset
    gX_I32          bottom; // Bottom border offset
    gX_I32          layout; // Layout of the n-patch: 3x3, 1x3 or 3x1
} gX_NPatchInfo;

// [raylib] borrowed
// GlyphInfo, font characters glyphs info
typedef struct gX_GlyphInfo {
    gX_I32      value;      // Character value (Unicode)
    gX_I32      offsetX;    // Character offset X when drawing
    gX_I32      offsetY;    // Character offset Y when drawing
    gX_I32      advanceX;   // Character advance position X
    gX_Image    image;      // Character image data
} gX_GlyphInfo;

// [raylib] borrowed
// Font, font texture and GlyphInfo array data
typedef struct gX_Font {
    gX_I32          baseSize;       // Base size (default chars height)
    gX_I32          glyphCount;     // Number of glyph characters
    gX_I32          glyphPadding;   // Padding around the glyph characters
    gX_Texture      texture;        // Texture atlas containing the glyphs
    gX_Rectangle   *recs;           // Rectangles in texture for the glyphs
    gX_GlyphInfo   *glyphs;         // Glyphs info data
    gX_Shader       shader;         //opencode: v0.6.0 — SDF shader (FONT_SDF fonts); 0 for bitmap
} gX_Font;

// [raylib] borrowed
// Camera2D, defines position/orientation in 2d space
typedef struct gX_Camera2D {
    gX_F32v2    offset;         // Camera offset (displacement from target)
    gX_F32v2    target;         // Camera target (rotation and zoom origin)
    gX_F32      rotation;       // Camera rotation in degrees
    gX_F32      zoom;           // Camera zoom (scaling), should be 1.0f by default
} gX_Camera2D;

typedef struct gX_DrawingInfo {
    gX_I0 *renderContext;
    //opencode: GL state saved by Begin3D for End3D restore (raylib not needed)
    gX_I1 hasPrevState3D;
    gX_I1 prevDepthTest;
    gX_I1 prevCullFace;
    gX_I1 prevBlend;
    //opencode: viewport rect (default framebuffer coords) for glBlitFramebuffer
    gX_I32 vpX, vpY, vpW, vpH;
} gX_DrawingInfo;

#ifdef __cplusplus
}
#endif

#endif //gX_COMMON_TYPES_H
