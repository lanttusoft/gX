/**********************************************************************************************
*
*   [gX: src/common/debug.h]
*   [common/debug.h] — crash traces (registers + stack) and variable watches
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

#ifndef GX_DEBUG_H
#define GX_DEBUG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: crash traces and variable watches. Signals are not exceptions:
//   the crash path dumps registers + a stack to stderr and exits with
//   128+signal. Watches sample addresses per frame and trace changes as
//   GX_WATCH lines (headless: GX_DEBUG_WATCH="name=0xADDR:type,...").

typedef enum {
    GX_DEBUG_WATCH_F32 = 0,
    GX_DEBUG_WATCH_I32,
    GX_DEBUG_WATCH_U32,
    GX_DEBUG_WATCH_U64,
    GX_DEBUG_WATCH_V3,
    GX_DEBUG_WATCH_COLOR
} gX_Debug_WatchType;

gX_I0 gX_Debug_Init(gX_I0);
gX_I0 gX_Debug_Update(gX_I0);
gX_I0 gX_Debug_Backtrace(gX_I0);

gX_I32 gX_Debug_Watch_Add(const gX_I8 *name, const gX_I0 *addr,
    gX_Debug_WatchType type);
gX_I0 gX_Debug_Watch_Clear(gX_I0);
gX_I32 gX_Debug_Watch_Count(gX_I0);
const gX_I8 *gX_Debug_Watch_Name(gX_I32 index);
gX_I0 gX_Debug_Watch_Value(gX_I32 index, gX_I8 *out, gX_I32 outSize);

//opencode: quick-adds for engine globals (editor button + the debug gate).
gX_I32 gX_Debug_Watch_AddFrameCount(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //GX_DEBUG_H
