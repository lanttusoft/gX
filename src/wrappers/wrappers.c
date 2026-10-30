/**********************************************************************************************
* 
*   [wrappers/wrappers.c]
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
//lanttu: original wrapper for raylib backend init/shutdown/FPS/volume/screenshot
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "wrappers_header.h"
#include "wrappers.h"
#include "../main.h"
//ai: register the context backend's extension resolver after window creation.
#include "../rhi/rhi_bindless.h"

//opencode: GX — simple timer-based frame time using clock_gettime
static gX_F64 gX_Time_GetSeconds(gX_I0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gX_F64)ts.tv_sec + (gX_F64)ts.tv_nsec / 1000000000.0;
}

gX_I32 exitKey;

gX_I0 gX_Backend_Load(gX_U32 width, gX_U32 height, const gX_I8 *name) {
    GX_WRAPPER_BACKEND_PROLOGUE
    gX_TraceLog_WRAPPERS(gX_LOG_NOTICE, "Backend: InitWindow(%ux%u, \"%s\")", width, height, name ? name : "");
    SetTraceLogCallback(gX_TraceLog_Print);
    //ai: public-beta UI is locked to its design resolution until the A2 reflow
    //    lands; arbitrary aspect resizing mixes logical canvas and live text space.
    maindata.configFlags &= ~(gX_U32)0x00000004;
    SetConfigFlags((unsigned int)maindata.configFlags);
    InitWindow((gX_I32)width,(gX_I32)height,(const char*)name);
    SetWindowMinSize(320, 240);
    if (!IsWindowReady()) {
        gX_TraceLog_WRAPPERS(gX_LOG_FATAL, "Backend: InitWindow failed (no display?)");
        return;
    }
    gX_RHI_SetProcAddressFn((gX_RHI_ProcAddressFn)raylib_GetProcAddress());
    GX_WRAPPER_BACKEND_EPILOGUE
}

gX_I0 gX_Backend_Unload(gX_I0) {
    GX_WRAPPER_BACKEND_PROLOGUE
    CloseWindow();
    GX_WRAPPER_BACKEND_EPILOGUE
}

gX_I0 gX_BackendAudio_Load(gX_I0) {
    GX_WRAPPER_BACKEND_PROLOGUE
    InitAudioDevice();
    GX_WRAPPER_BACKEND_EPILOGUE
}

gX_I0 gX_BackendAudio_Unload(gX_I0) {
    GX_WRAPPER_BACKEND_PROLOGUE
    CloseAudioDevice();
    GX_WRAPPER_BACKEND_EPILOGUE
}

gX_I32 gX_Backend_RerouteLogtype(gX_I32 logType) {
    switch (logType) {
        case LOG_NONE:      { return gX_LOG_NONE;       break; }
        case LOG_FATAL:     { return gX_LOG_FATAL;      break; }
        case LOG_ERROR:     { return gX_LOG_ERROR;      break; }
        case LOG_WARNING:   { return gX_LOG_WARNING;    break; }
        case LOG_INFO:      { return gX_LOG_INFO;       break; }
        case LOG_DEBUG:     { return gX_LOG_DEBUG;      break; }
        case LOG_TRACE:     { return gX_LOG_TRACE;      break; }
        case LOG_ALL:       { return gX_LOG_ALL;        break; }
        default: break;
    }
    return logType;
}

gX_I0 gX_TargetFPS_Set(gX_F32 fps) {
    SetTargetFPS((gX_I32)fps);
}

//opencode: deterministic capture hook — GX_FIXED_DT overrides the backend
//   frame clock without changing normal runtime timing.
gX_F64 gX_FrameTime_Get(gX_I0) {
    const char *fixed = getenv("GX_FIXED_DT");
    if (fixed && fixed[0]) {
        char *end = NULL;
        gX_F64 dt = strtod(fixed, &end);
        if (end != fixed && dt > 0.0) return dt;
    }
    return GetFrameTime();
}

gX_I0 gX_MasterVolume_Set(gX_F32 volume) {
    SetMasterVolume(volume);
}

//opencode: release follow-up — best-effort mkdir -p for the capture parent
//   (fresh tarball extracts have no screenshot/ dir; ExportImage failed
//   silently). Warns instead of dropping errors.
static gX_I0 gX_MakeParentDirs(const gX_I8 *path)
{
    gX_I8 buf[512];
    gX_I32 len = 0;
    gX_I32 i = 0;
    if (!path)
    {
        return;
    }
    len = (gX_I32)strlen((const char *)path);
    if ((len <= 0) || (len >= 511))
    {
        return;
    }
    memcpy(buf, path, (gX_U64)len + 1);
    for (i = 1; i < len; i++)
    {
        if (buf[i] == '/')
        {
            buf[i] = '\0';
            if ((mkdir((const char *)buf, 0755) != 0) && (errno != EEXIST))
            {
                gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "Screenshot: mkdir failed for %s", buf);
                return;
            }
            buf[i] = '/';
        }
    }
}

gX_I0 gX_TakeScreenShot(const gX_I8 *fileName) {
    Image img = LoadImageFromScreen();
    gX_MakeParentDirs(fileName);
    if (!ExportImage(img, fileName))
    {
        gX_TraceLog_WRAPPERS(gX_LOG_WARNING, "Screenshot: export failed for %s", fileName ? fileName : (const gX_I8 *)"(null)");
    }
    UnloadImage(img);
}
