/**********************************************************************************************
* 
*   [wrappers/misc.c]
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
#include "wrappers_header.h"
#include "misc.h"

//opencode: gX_Clipboard_Get — wraps GetClipboardText
const gX_I8 *gX_Clipboard_Get(gX_I0) {
    return GetClipboardText();
}

//opencode: gX_Clipboard_Set — wraps SetClipboardText
gX_I0 gX_Clipboard_Set(const gX_I8 *text) {
    SetClipboardText(text);
}

//opencode: gX_Time_Get — wraps GetTime, or use timer_ticks on N64
gX_F64 gX_Time_Get(gX_I0) {
    return (gX_F64)GetTime();
}

//opencode: gX_URL_Open — wraps OpenURL
gX_I0 gX_URL_Open(const gX_I8 *url) {
    OpenURL(url);
}

//opencode: gX_Random_GetValue — wraps GetRandomValue
gX_I32 gX_Random_GetValue(gX_I32 min, gX_I32 max) {
    return (gX_I32)GetRandomValue(min, max);
}

//opencode: gX_Random_SetSeed — wraps SetRandomSeed
gX_I0 gX_Random_SetSeed(gX_U32 seed) {
    SetRandomSeed(seed);
}

//opencode: gX_TraceLog_SetLevel — wraps SetTraceLogLevel
gX_I0 gX_TraceLog_SetLevel(gX_I32 logLevel) {
    SetTraceLogLevel(logLevel);
}
