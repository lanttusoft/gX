/**********************************************************************************************
* 
*   [wrappers/misc.h]
*   Uses borrowed code from: raylib v5.x
*   All instances are marked with respective labels
*
*   Copyright (c) 2025-2026 lanttu, opencode
* 
*   [raylib]
*   LICENSE: zlib/libpng
*
*   raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software:
*
*   Copyright (c) 2013-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "-as is", without any express or implied warranty. In no event
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
//opencode: wrapper for raylib misc (clipboard/time/URL/random/log) functions
#ifndef GX_WRAPPERS_MISC_H
#define GX_WRAPPERS_MISC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"
#include "../common/types.h"

const gX_I8 *gX_Clipboard_Get(gX_I0);
gX_I0        gX_Clipboard_Set(const gX_I8 *text);

gX_F64       gX_Time_Get(gX_I0);

gX_I0        gX_URL_Open(const gX_I8 *url);

gX_I32       gX_Random_GetValue(gX_I32 min, gX_I32 max);
gX_I0        gX_Random_SetSeed(gX_U32 seed);

gX_I0        gX_TraceLog_SetLevel(gX_I32 logLevel);

#ifdef __cplusplus
}
#endif

#endif //GX_WRAPPERS_MISC_H
