/**********************************************************************************************
* 
*   [wrappers/wrappers_header.h]
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
//lanttu: backend prologue/epilogue macros + convert/log includes
#ifndef GX_WRAPPERS_HEADER_H
#define GX_WRAPPERS_HEADER_H

#include "../common/config.h"

#include "convert.h"

#include "../common/log.h"

#include "../backend/raylib/raylib_wrappers.h"

#define GX_WRAPPER_BACKEND_PROLOGUE \
    if (!gX_TraceLog_allowBackendLog) { gX_TraceLog_SuppressStart(); } \
    tracelog2.backendMode = true; \
    
#define GX_WRAPPER_BACKEND_EPILOGUE \
    tracelog2.backendMode = false; \
    if (!gX_TraceLog_allowBackendLog) { gX_TraceLog_SuppressEnd(); } \

#endif //GX_WRAPPERS_HEADER_H
