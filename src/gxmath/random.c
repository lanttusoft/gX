/**********************************************************************************************
*
*   [gX: src/gxmath/random.c]
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

/**********************************************************************************************
*
*   gxmath/random.c
*   PCG32 (O'Neill 2014) — a small, fast, statistically solid generator with
*   explicit state. Chosen over libc rand() because:
*     - the sequence is fully defined by (state, inc): identical on every
*       platform/libc, so seeded captures and audits are reproducible;
*     - every consumer can own its own stream instead of sharing one global
*       sequence (the "rand() pollution" the weather/splat locals worked
*       around);
*     - the unbiased bounded draw is part of the API.
*
*   Algorithm: PCG-XSH-RR 64/32 (LCG state advance, xorshift + variable
*   rotation output). Reference: M.E. O'Neill, "PCG: A Family of Simple Fast
*   Space-Efficient Statistically Good Algorithms for Random Number
*   Generation" (2014); the reference implementation is Apache-2.0, which is
*   compatible with this project's GPLv3.
*
**********************************************************************************************/

#include "random.h"

gX_I0 gX_Random32_Seed(gX_Random32 *r, gX_U64 seed, gX_U64 stream) {
    //opencode: canonical pcg32_srandom_r: inc must be odd, and two advances
    //   mix the seed into the state (state = 0 would otherwise stay 0).
    r->state = 0u;
    r->inc = (stream << 1u) | 1u;
    gX_Random32_Next(r);
    r->state += seed;
    gX_Random32_Next(r);
}

gX_U32 gX_Random32_Next(gX_Random32 *r) {
    gX_U64 old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    gX_U32 xorshifted = (gX_U32)(((old >> 18u) ^ old) >> 27u);
    gX_U32 rot = (gX_U32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

gX_U32 gX_Random32_Bounded(gX_Random32 *r, gX_U32 bound) {
    if (bound == 0u) return 0u;
    //opencode: rejection sampling keeps every value equally likely — the
    //   modulo shortcut would bias the low residues.
    gX_U32 threshold = (gX_U32)(0u - bound) % bound;
    for (;;) {
        gX_U32 v = gX_Random32_Next(r);
        if (v >= threshold) return v % bound;
    }
}

gX_F32 gX_Random32_F32(gX_Random32 *r) {
    //opencode: 24 bits = the float mantissa; value in [0, 1).
    return (gX_F32)(gX_Random32_Next(r) >> 8) * (1.0f / 16777216.0f);
}

gX_F32 gX_Random32_Range(gX_Random32 *r, gX_F32 min, gX_F32 max) {
    return min + (max - min) * gX_Random32_F32(r);
}
