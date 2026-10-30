/**********************************************************************************************
*
*   [gX: src/common/types/state.h]
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

#ifndef gX_TYPES_STATE_H
#define gX_TYPES_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "primary.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gX_LoadingState {
    gX_I1 loaded;
    
    gX_I1 load;
    gX_I1 unload;
    gX_I1 reload;
    
    gX_I1 loadAsap; //load when backend is ready
    gX_I1 loadBefore; //load just before using program
    gX_I1 unloadAfter; //unload after using program
} gX_LoadingState;

#ifdef __cplusplus
}
#endif

#endif //gX_TYPES_STATE_H
