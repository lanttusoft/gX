/**********************************************************************************************
*
*   [gX: src/common/random.h]
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

#ifndef GX_RANDOM_H
#define GX_RANDOM_H

#include "types.h"
#include "../gxmath/random.h"

typedef struct gX_Random {
    gX_I8 name[128];
    gX_I8 description[256];
    
    gX_I1 init;
    
    //main
    gX_U32      seed;
    //opencode: deterministic stream backing every draw (PCG32, platform-
    //   independent); `seed` records what it was seeded with.
    gX_Random32 rng32;
    gX_I32      min;
    gX_I32      max;
    gX_I32      value;
    gX_F32      slowCounter;
    gX_I32      slow;
    
    //special
    gX_F32v4 v4;
    gX_Rectangle rect;
    gX_Color color;
    gX_Color colorSmooth;
    
    gX_F32 colorSmoothT;
    gX_F32 colorSmoothTspeed;
    gX_Color colorSmoothTemp;
    gX_Color colorSmoothPrev;
    
} gX_Random;

extern gX_Random rng;

gX_I32 gX_Random_Init(gX_I0);
gX_I32 gX_Random_Call(gX_I0);

gX_I0 gX_Random_Update(gX_I0);

//opencode: returns a random float in [min, max] using rand() directly
//   (not rng.value which updates once per frame)
gX_F32 gX_F32_RandomRange(gX_F32 min, gX_F32 max);

#endif //GX_RANDOM_H
