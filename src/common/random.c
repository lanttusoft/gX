/**********************************************************************************************
*
*   [gX: src/common/random.c]
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

#include <stdlib.h>
#include <time.h>

#include "random.h"

#include "macros.h"
#include "utils.h"

#include "../main.h"
#include "../gxmath/color.h"

gX_Random rng;

gX_I32 gX_Random_Init(gX_I0) {
    //opencode: --seed N gives a deterministic RNG sequence (reproducible
    //   battle AI, damage, and backdrop variety for frame captures). 0/absent
    //   keeps the time-based seed.
    extern gX_MainArguments arguments;
    if (arguments.hasSeed) {
        rng.seed = arguments.seed;
    } else {
        rng.seed = (gX_U32)time(NULL);
    }
    //opencode: the engine draws from the PCG stream; srand is kept only for
    //   the vendored CRT port (src/external/crt_core.c), which still calls
    //   rand() and becomes deterministic with the same seed.
    srand(rng.seed);
    gX_Random32_Seed(&rng.rng32, (gX_U64)rng.seed, 0x9E3779B97F4A7C15ull);
    
    rng.colorSmoothT = 0.0f;
    rng.colorSmoothTspeed = 1.0f;
    rng.colorSmoothPrev = gX_BLACK;
    rng.colorSmoothTemp = gX_Color_Random();
    
    rng.init = true;
    
    return 0;
}

gX_I32 gX_Random_Call(gX_I0) {
    
    rng.value = (gX_I32)gX_Random32_Next(&rng.rng32);
    
    return rng.value;
}


static gX_I0 gX_Random_Update_Smooth(gX_I0) {
    
    if (rng.colorSmoothT >= 1.0f) {
        rng.colorSmoothT = 0.0f;
        rng.colorSmoothPrev = rng.colorSmoothTemp;
        rng.colorSmoothTemp = gX_Color_Random();
    }
    else {
        rng.colorSmoothT += rng.colorSmoothTspeed / maindata.targetFPS;
    }
    rng.colorSmooth = gX_Color_LerpRGB(rng.colorSmoothPrev, rng.colorSmoothTemp, rng.colorSmoothT);
    
}

gX_I0 gX_Random_Update(gX_I0) {
    
    gX_Random_Call();
    
    gX_Random_Update_Smooth();
    
}

//opencode: random float drawing fresh from the PCG stream (rng.value is the
//   once-per-frame cache, so it would repeat within a frame)
gX_F32 gX_F32_RandomRange(gX_F32 min, gX_F32 max) {
    return gX_Random32_Range(&rng.rng32, min, max);
}
