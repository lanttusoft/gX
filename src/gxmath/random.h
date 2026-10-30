/**********************************************************************************************
*
*   [gX: src/gxmath/random.h]
*   Deterministic random numbers
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   This software is provided "as-is", without any express or implied warranty.
*   In no event will the authors be held liable for any damages arising from
*   the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose,
*   including commercial applications, and to alter it and redistribute it
*   freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not
*     claim that you wrote the original software. If you use this software
*     in a product, an acknowledgment would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not
*     be misrepresented as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
*
*   gX addition to gxmath; distributed under the gxmath zlib/libpng terms.
*
**********************************************************************************************/

//lanttu: original gxmath module — PCG32 (see random.c for provenance)

#ifndef GXMATH_RANDOM_H
#define GXMATH_RANDOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//opencode: PCG32 state. `inc` is normalized to odd by the seed and selects
//   the stream; `state` advances by the LCG step on every draw. Both are
//   part of the sequence identity — copy the struct to fork a stream.
typedef struct gX_Random32 {
    gX_U64 state;
    gX_U64 inc;
} gX_Random32;

//opencode: seed from a 64-bit seed + stream id (two streams with the same
//   seed diverge immediately). Deterministic on every platform.
gX_I0  gX_Random32_Seed(gX_Random32 *r, gX_U64 seed, gX_U64 stream);

//opencode: next 32 raw bits.
gX_U32 gX_Random32_Next(gX_Random32 *r);

//opencode: uniform draw in [0, bound) with rejection sampling (no modulo
//   bias); bound 0 returns 0.
gX_U32 gX_Random32_Bounded(gX_Random32 *r, gX_U32 bound);

//opencode: uniform float in [0, 1) with a full 24-bit mantissa.
gX_F32 gX_Random32_F32(gX_Random32 *r);

//opencode: uniform float in [min, max).
gX_F32 gX_Random32_Range(gX_Random32 *r, gX_F32 min, gX_F32 max);

#ifdef __cplusplus
}
#endif

#endif //GXMATH_RANDOM_H
