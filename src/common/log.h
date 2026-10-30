/**********************************************************************************************
*
*   [gX: src/common/log.h]
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

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include <stdarg.h>
#include <string.h>
#include <time.h>
    
#include "types.h"
#include "text.h"

//opencode: tiered log history/stack — T3 (N64/PS1) shrinks the ring buffers
//   (each entry/stack slot is 256 B; the desktop sizes are debug-oriented).
#if defined(GX_TIER) && GX_TIER == 3
#define MAX_STACK_DEPTH 4
#define MAX_HISTORY 4
#else
#define MAX_STACK_DEPTH 16
#define MAX_HISTORY 16
#endif
    
enum gX_LogTypes {
    gX_LOG_ALL,
    gX_LOG_TRACE5,
    gX_LOG_TRACE4,
    gX_LOG_TRACE3,
    gX_LOG_TRACE2,
    gX_LOG_TRACE,
    gX_LOG_DEBUG,
    gX_LOG_INFO,        //default
    gX_LOG_NOTICE,      //important: not warning
    gX_LOG_WARNING,     //something went wrong, fallback
    gX_LOG_ERROR,       //something went wrong, no fallback, risky to run
    gX_LOG_ALERT,       //program must shut down, unload
    gX_LOG_FATAL,       //program must shut down now, no unload
    gX_LOG_NONE,
};

enum gX_MoreLogTypes {
    gX_LOG_GREEN              = 1000,
    gX_LOG_MAGENTA            = 1001,
    gX_LOG_DEBUG_NOTICE       = 1010,
    gX_LOG_TRACE_NOTICE       = 1011,
    gX_LOG_TRACE2_NOTICE      = 1012,
    gX_LOG_DEBUG_WARNING      = 1020,
    gX_LOG_TRACE_WARNING      = 1021,
    gX_LOG_TRACE2_WARNING     = 1022,
};

//opencode: per-module log levels enum
enum gX_LogModule {
    gX_LOG_MODULE_DEFAULT = 0,
    gX_LOG_MODULE_MAIN,
    gX_LOG_MODULE_GFX,
    gX_LOG_MODULE_GUI,
    gX_LOG_MODULE_HEAP,
    gX_LOG_MODULE_AUDIO,
    gX_LOG_MODULE_INPUT,
    gX_LOG_MODULE_SCENE,
    gX_LOG_MODULE_BACKGROUND,
    gX_LOG_MODULE_POSTPROCESS,
    gX_LOG_MODULE_RENDERER,
    gX_LOG_MODULE_COMMON,
    gX_LOG_MODULE_RESOURCE,
    gX_LOG_MODULE_ARCADE,
    gX_LOG_MODULE_GAME,
    gX_LOG_MODULE_CAMERA,
    gX_LOG_MODULE_WRAPPERS,
    gX_LOG_MODULE_SCRIPTING,
    gX_LOG_MODULE_COUNT
};

//opencode: unified log struct — replaces all scattered globals + gX_TraceLog2
typedef struct gX_Log {
    // Level filtering
    gX_I32 globalLevel;                    // was gX_TraceLog_logtypeLevel
    gX_I32 moduleLevels[gX_LOG_MODULE_COUNT];  // per-module overrides
    
    // Toggles
    gX_I1  suppress;                       // was gX_TraceLog_suppressed / tracelog2.suppress
    gX_I1  allowGreen;                     // was gX_TraceLog_allowGreen
    gX_I1  allowMagenta;                   // was gX_TraceLog_allowMagenta
    gX_I1  warningPlaysound;               // was gX_TraceLog_warning_playsound
    gX_I1  allowBackendLog;                // was gX_TraceLog_allowBackendLog
    gX_I1  backendMode;                    // was tracelog2.backendMode
    
    // Function call stack tracking
    gX_U32 stackDepth;                     // was gX_TraceLog_stackDepth
    gX_U32 maxStackDepth;                  // was MAX_STACK_DEPTH constant
    gX_I8  functionStack[MAX_STACK_DEPTH][256]; // was gX_TraceLog_functionName
    
    // History (last N log messages, formatted)
    gX_U32 historyCount;
    gX_I8  history[MAX_HISTORY][256];      // was gX_TraceLog_functionHistory
    
    // Error state
    gX_I32 errorCode;                      // was gX_TraceLog_error_code
    gX_I8  errorName[256];                 // was gX_TraceLog_error_name
    gX_I8  errorDescription[256];          // was gX_TraceLog_error_description
    gX_I8  errorFileName[256];             // was gX_TraceLog_error_fileName
    gX_I32 errorLineNumber;                // was gX_TraceLog_error_lineNumber
    gX_F64 errorTimeElapsed;               // was gX_TraceLog_error_timeElapsed
    
    // Timing
    gX_I1  clockRelative;                  // was gX_TraceLog_clockRelative
    gX_I32 secondsDivision;                // 3=ms, 6=µs, 9=ns, 0=default(9)
    struct timespec startTime;             // set in Init, used for relative timestamps
    
    // Detail level
    gX_U32 furtherTraceLevel;              // was gX_TraceLog_furtherTraceLevel
    gX_U32 maxStackDepthToLog;             // was gX_TraceLog_maxStackDepthToLog
} gX_Log;

extern gX_Log gX_LogState;

//opencode: New API

gX_I0 gX_Log_Init(gX_I0);

// Core log function — module-aware, format string
gX_I0 gX_Log_Message(gX_I32 module, gX_I32 logType, const gX_I8 *text, ...);

// Stack tracking (used by PROLOGUE/EPILOGUE macros)
gX_I0 gX_Log_StackPush(const gX_I8 *funName);
gX_I0 gX_Log_StackPop(gX_I0);

// Suppression
gX_I0 gX_Log_SuppressStart(gX_I0);
gX_I0 gX_Log_SuppressEnd(gX_I0);

// Error reporting
gX_I0 gX_Log_ErrorSet(const gX_I8 *name, const gX_I8 *desc, gX_I32 code, const gX_I8 *file, gX_I32 line);
gX_I0 gX_Log_ErrorWriteReport(gX_I0);

// Module name lookup
const gX_I8 *gX_Log_GetModuleName(gX_I32 module);

//opencode: backward-compat function wrappers — old names still work

gX_I0 gX_TraceLog_SuppressStart(gX_I0);
gX_I0 gX_TraceLog_SuppressEnd(  gX_I0);

gX_I0 gX_TraceLog_StackIncrement(const gX_I8 *funName);
gX_I0 gX_TraceLog_StackDecrement( gX_I0);

gX_I0 gX_TraceLog(gX_I32 logType, const gX_I8 *text, ...);
gX_I0 gX_TraceLog_Print(gX_I32 logType, const gX_I8 *text, va_list args);

gX_I0 gX_TraceLog_Module(gX_I32 module, gX_I32 logType, const gX_I8 *text, ...);
gX_I0 gX_TraceLog_Print_Module(gX_I32 module, gX_I32 logType, const gX_I8 *text, va_list args);

const gX_I8 *gX_TraceLog_GetModuleName(gX_I32 module);

const gX_I8 *gX_TraceLog_Error_GetName(gX_I0);
const gX_I8 *gX_TraceLog_Error_GetDescription(gX_I0);
gX_I0 gX_TraceLog_Error_WriteReport(gX_I0);

//opencode: backward-compat macro aliases — old global names resolve to gX_LogState fields

#define gX_TraceLog_logtypeLevel       gX_LogState.globalLevel
#define gX_TraceLog_allowGreen         gX_LogState.allowGreen
#define gX_TraceLog_allowMagenta       gX_LogState.allowMagenta
#define gX_TraceLog_allowBackendLog    gX_LogState.allowBackendLog
#define gX_TraceLog_suppressed         gX_LogState.suppress
#define gX_TraceLog_moduleLevels       gX_LogState.moduleLevels
#define gX_TraceLog_clockRelative      gX_LogState.clockRelative
#define gX_TraceLog_secondsDivision    gX_LogState.secondsDivision
#define gX_TraceLog_stackDepth         gX_LogState.stackDepth
#define gX_TraceLog_maxStackDepthToLog gX_LogState.maxStackDepthToLog
#define gX_TraceLog_functionName       gX_LogState.functionStack
#define gX_TraceLog_error_name         gX_LogState.errorName
#define gX_TraceLog_error_description  gX_LogState.errorDescription
#define gX_TraceLog_error_code         gX_LogState.errorCode
#define gX_TraceLog_error_fileName     gX_LogState.errorFileName
#define gX_TraceLog_error_lineNumber   gX_LogState.errorLineNumber
#define gX_TraceLog_error_timeElapsed  gX_LogState.errorTimeElapsed
#define gX_TraceLog_furtherTraceLevel  gX_LogState.furtherTraceLevel
#define gX_TraceLog_warning_playsound  gX_LogState.warningPlaysound
#define gX_TraceLog_functionHistory    gX_LogState.history

//opencode: backward-compat struct alias — tracelog2 accesses map to gX_LogState
#define tracelog2 gX_LogState
    
//opencode: per-module convenience macros
#define gX_TraceLog_MAIN(level, ...)        gX_TraceLog_Module(gX_LOG_MODULE_MAIN, level, __VA_ARGS__)
#define gX_TraceLog_GFX(level, ...)         gX_TraceLog_Module(gX_LOG_MODULE_GFX, level, __VA_ARGS__)
#define gX_TraceLog_GUI(level, ...)         gX_TraceLog_Module(gX_LOG_MODULE_GUI, level, __VA_ARGS__)
#define gX_TraceLog_HEAP(level, ...)        gX_TraceLog_Module(gX_LOG_MODULE_HEAP, level, __VA_ARGS__)
#define gX_TraceLog_AUDIO(level, ...)       gX_TraceLog_Module(gX_LOG_MODULE_AUDIO, level, __VA_ARGS__)
#define gX_TraceLog_INPUT(level, ...)       gX_TraceLog_Module(gX_LOG_MODULE_INPUT, level, __VA_ARGS__)
#define gX_TraceLog_SCENE(level, ...)       gX_TraceLog_Module(gX_LOG_MODULE_SCENE, level, __VA_ARGS__)
#define gX_TraceLog_BACKGROUND(level, ...)  gX_TraceLog_Module(gX_LOG_MODULE_BACKGROUND, level, __VA_ARGS__)
#define gX_TraceLog_POSTPROCESS(level, ...) gX_TraceLog_Module(gX_LOG_MODULE_POSTPROCESS, level, __VA_ARGS__)
#define gX_TraceLog_RENDERER(level, ...)    gX_TraceLog_Module(gX_LOG_MODULE_RENDERER, level, __VA_ARGS__)
#define gX_TraceLog_COMMON(level, ...)      gX_TraceLog_Module(gX_LOG_MODULE_COMMON, level, __VA_ARGS__)
#define gX_TraceLog_RESOURCE(level, ...)    gX_TraceLog_Module(gX_LOG_MODULE_RESOURCE, level, __VA_ARGS__)
#define gX_TraceLog_ARCADE(level, ...)      gX_TraceLog_Module(gX_LOG_MODULE_ARCADE, level, __VA_ARGS__)
#define gX_TraceLog_GAME(level, ...)        gX_TraceLog_Module(gX_LOG_MODULE_GAME, level, __VA_ARGS__)
#define gX_TraceLog_CAMERA(level, ...)      gX_TraceLog_Module(gX_LOG_MODULE_CAMERA, level, __VA_ARGS__)
#define gX_TraceLog_WRAPPERS(level, ...)    gX_TraceLog_Module(gX_LOG_MODULE_WRAPPERS, level, __VA_ARGS__)
#define gX_TraceLog_SCRIPTING(level, ...)   gX_TraceLog_Module(gX_LOG_MODULE_SCRIPTING, level, __VA_ARGS__)

#define gX_TRACELOG_PROLOGUE_LEVEL(level,funname,logpart) \
    gX_Log_StackPush(funname); \
    gX_Log_Message(gX_LOG_MODULE_DEFAULT, (level), (const gX_I8*)(logpart));

#define gX_TRACELOG_PROLOGUE(funname,logpart) \
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LogState.globalLevel,funname,logpart)
    
#define gX_TRACELOG_EPILOGUE_LEVEL(level,logpart) \
    gX_Log_Message(gX_LOG_MODULE_DEFAULT, (level), (const gX_I8*)(logpart)); \
    gX_Log_StackPop();
    
#define gX_TRACELOG_EPILOGUE(logpart) \
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LogState.globalLevel,logpart);
    
#define gX_TRACELOG_EPILOGUE_ERROR(logpart,code) \
    gX_LogState.errorCode = (gX_I32)(code); \
    strcpy(gX_LogState.errorFileName,__FILE__); \
    gX_LogState.errorLineNumber = __LINE__; \
    gX_Log_Message(gX_LOG_MODULE_DEFAULT, gX_LOG_ERROR, (const gX_I8*)(logpart)); \
    gX_Log_StackPop();

//opencode: module-aware prologue/epilogue macros
#define gX_TRACELOG_PROLOGUE_MODULE(mod,level,funname,logpart) \
    gX_Log_StackPush(funname); \
    gX_Log_Message((mod), (level), (const gX_I8*)(logpart));

#define gX_TRACELOG_EPILOGUE_MODULE(mod,level,logpart) \
    gX_Log_Message((mod), (level), (const gX_I8*)(logpart)); \
    gX_Log_StackPop();

//opencode: GX_LOG_RING=<N> keeps the last N emitted lines (default 32; 0
//   disables) as crash context; the signal handler dumps them.
gX_I0 gX_Log_RingDump(gX_I0);

#ifdef __cplusplus
}
#endif

#endif //LOG_H
