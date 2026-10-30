/**********************************************************************************************
*
*   [gX: src/common/log.c]
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "alloc.h"
#include "msg.h"
#include "config.h"

#include "../wrappers/wrappers.h"
#include "../wrappers/wrappers_stdlib.h"

#include "../audio.h"

//opencode: crash-context log ring — heap-backed (the static footprint would
//   blow the lean gate's bss budget), allocated on the first emitted line.
#define GX_LOG_RING_LINE 512

//opencode: GX_LOG_FILE=<path> mirrors every emitted line to a file without
//   ANSI colors (line-buffered so a crash leaves the tail on disk);
//   GX_LOG_CONSOLE=0 silences stdout. GX_LOG_DEDUP=1 collapses consecutive
//   identical messages into one line plus a "repeated N times" note (opt-in:
//   the audit gates parse every line and keep the default off).
static FILE *gX_LogFile;
static gX_I1 gX_LogFileChecked;
static gX_I1 gX_LogConsoleOn = 1;
static gX_I8 gX_LogDedupLast[512];
static gX_U32 gX_LogDedupCount;
static gX_I1 gX_LogDedupChecked;
static gX_I1 gX_LogDedupOn;
static gX_I1 gX_LogJsonChecked;
static gX_I1 gX_LogJsonOn;

//opencode: GX_LOG_MODULE_LEVELS="gfx=debug,gui=info" — per-module overrides
//   applied once at startup (the UI can still change them afterwards). Names
//   match the detail tags (MAIN/GFX/GUI/…), case-insensitive; levels accept
//   trace/trace2/trace3/debug/info/notice/warning/error.
static gX_I0 gX_Log_ApplyModuleLevels(gX_I0) {
    static gX_I1 applied = 0;
    if (applied) return;
    applied = 1;
    const char *spec = getenv("GX_LOG_MODULE_LEVELS");
    if (!spec || !spec[0]) return;
    gX_I8 buffer[256];
    snprintf((char *)buffer, sizeof(buffer), "%s", spec);
    gX_I8 *save = NULL;
    for (gX_I8 *token = strtok_r((char *)buffer, ",", &save); token;
         token = strtok_r(NULL, ",", &save)) {
        gX_I8 *equals = strchr((const char *)token, '=');
        if (!equals) continue;
        *equals = 0;
        gX_I32 module = -1;
        for (gX_I32 m = 1; m < gX_LOG_MODULE_COUNT; m++) {
            const gX_I8 *name = gX_Log_GetModuleName(m);
            if (name[0] && strcasecmp((const char *)token,
                    (const char *)name) == 0) { module = m; break; }
        }
        if (module < 0) continue;
        const char *levelName = equals + 1;
        gX_I32 level = -1;
        if (!strcasecmp(levelName, "trace")) level = gX_LOG_TRACE;
        else if (!strcasecmp(levelName, "trace2")) level = gX_LOG_TRACE2;
        else if (!strcasecmp(levelName, "trace3")) level = gX_LOG_TRACE3;
        else if (!strcasecmp(levelName, "debug")) level = gX_LOG_DEBUG;
        else if (!strcasecmp(levelName, "info")) level = gX_LOG_INFO;
        else if (!strcasecmp(levelName, "notice")) level = gX_LOG_NOTICE;
        else if (!strcasecmp(levelName, "warning")) level = gX_LOG_WARNING;
        else if (!strcasecmp(levelName, "error")) level = gX_LOG_ERROR;
        if (level >= 0) gX_LogState.moduleLevels[module] = level;
    }
}

//opencode: the clean level name for the JSON sink (the details tag is padded)
static const char *gX_Log_LevelName(gX_I32 logTypePrint) {
    switch (logTypePrint) {
        case gX_LOG_TRACE2: return "TRACE2";
        case gX_LOG_TRACE3: return "TRACE3";
        case gX_LOG_TRACE: return "TRACE";
        case gX_LOG_DEBUG: return "DEBUG";
        case gX_LOG_INFO: return "INFO";
        case gX_LOG_NOTICE: return "NOTICE";
        case gX_LOG_WARNING: return "WARNING";
        case gX_LOG_ERROR: return "ERROR";
        default: return "OTHER";
    }
}

//opencode: minimal JSON string escape for the message payload
static gX_I0 gX_Log_JsonEscape(const gX_I8 *in, gX_I8 *out, gX_I32 outSize) {
    gX_I32 o = 0;
    for (gX_I32 i = 0; in[i] && o < outSize - 3; i++) {
        gX_I8 c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if ((gX_U8)c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
}

static gX_I8 *gX_LogRing;
static gX_U32 gX_LogRingCapacity;
static gX_U32 gX_LogRingHead;
static gX_U32 gX_LogRingCount;
static gX_I1 gX_LogRingChecked;
static gX_I32 gX_LogRingSetting = -1;

//opencode: unified singleton — replaces all scattered globals + gX_TraceLog2
gX_Log gX_LogState = {
    .globalLevel = LOG_TYPE_LEVEL,
    .allowGreen = true,
    .allowMagenta = true,
    .allowBackendLog = true,
    //opencode: warningPlaysound is opt-in. Default off because warnings
    // (heap growth, missed targets, etc.) can fire per-frame during
    // scene transitions and would produce constant audible noise.
    // Set gX_LogState.warningPlaysound = true at runtime to re-enable.
    .warningPlaysound = false,
    .maxStackDepth = MAX_STACK_DEPTH,
    .maxStackDepthToLog = 8,
    .furtherTraceLevel = 3,
};

//stackoverflow
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

//game_TraceLog from game3
#define MAX_TRACELOG_MSG_LENGTH 4096
// Note: timestamp format is built dynamically in GetTimeStamp to support precision control

//opencode: new API — init/reset to defaults
gX_I0 gX_Log_Init(gX_I0) {
    gX_LogState.globalLevel = LOG_TYPE_LEVEL;
    gX_LogState.allowGreen = true;
    gX_LogState.allowMagenta = true;
    gX_LogState.allowBackendLog = true;
    //opencode: warning sound is opt-in (see comment in gX_LogState definition).
    // Re-enable in user code with: gX_LogState.warningPlaysound = true;
    gX_LogState.warningPlaysound = false;
    gX_LogState.suppress = false;
    gX_LogState.backendMode = false;
    gX_LogState.stackDepth = 0;
    gX_LogState.maxStackDepth = MAX_STACK_DEPTH;
    gX_LogState.maxStackDepthToLog = 8;
    gX_LogState.furtherTraceLevel = 3;
    gX_LogState.historyCount = 0;
    gX_LogState.clockRelative = false;
    gX_LogState.secondsDivision = 0;
    gX_LogState.errorCode = 0;
    gX_LogState.errorTimeElapsed = 0.0;
    gX_LogState.errorLineNumber = 0;
    clock_gettime(CLOCK_MONOTONIC, &gX_LogState.startTime);
    gX_LogState.errorName[0] = '\0';
    gX_LogState.errorDescription[0] = '\0';
    gX_LogState.errorFileName[0] = '\0';
    
    for (gX_I32 i = 0; i < gX_LOG_MODULE_COUNT; i++) {
        gX_LogState.moduleLevels[i] = 0;
    }
    for (gX_U32 i = 0; i < MAX_HISTORY; i++) {
        gX_LogState.history[i][0] = '\0';
    }
    for (gX_U32 i = 0; i < MAX_STACK_DEPTH; i++) {
        gX_LogState.functionStack[i][0] = '\0';
    }
}

//opencode: stack tracking
gX_I0 gX_Log_StackPush(const gX_I8 *funName) {
    gX_RawString_Copy(gX_LogState.functionStack[gX_LogState.stackDepth], funName);
    gX_LogState.stackDepth++;
    if (gX_LogState.stackDepth > gX_LogState.maxStackDepth - 1) {
        gX_LogState.stackDepth = gX_LogState.maxStackDepth - 1;
    }
}

gX_I0 gX_Log_StackPop(gX_I0) {
    if (gX_LogState.stackDepth > 0) gX_LogState.stackDepth--;
}

//opencode: suppression
gX_I0 gX_Log_SuppressStart(gX_I0) {
    gX_LogState.suppress = true;
}

gX_I0 gX_Log_SuppressEnd(gX_I0) {
    gX_LogState.suppress = false;
}

//opencode: error reporting
gX_I0 gX_Log_ErrorSet(const gX_I8 *name, const gX_I8 *desc, gX_I32 code, const gX_I8 *file, gX_I32 line) {
    if (name) strncpy(gX_LogState.errorName, name, 255);
    if (desc) strncpy(gX_LogState.errorDescription, desc, 255);
    gX_LogState.errorCode = code;
    if (file) strncpy(gX_LogState.errorFileName, file, 255);
    gX_LogState.errorLineNumber = line;
}

gX_I0 gX_Log_ErrorWriteReport(gX_I0) {
    // stub — placeholder for full error reporting
}

//opencode: module name lookup
const gX_I8 *gX_Log_GetModuleName(gX_I32 module) {
    switch (module) {
        case gX_LOG_MODULE_DEFAULT:    return "";
        case gX_LOG_MODULE_MAIN:       return "MAIN";
        case gX_LOG_MODULE_GFX:        return "GFX";
        case gX_LOG_MODULE_GUI:        return "GUI";
        case gX_LOG_MODULE_HEAP:       return "HEAP";
        case gX_LOG_MODULE_AUDIO:      return "AUDIO";
        case gX_LOG_MODULE_INPUT:      return "INPUT";
        case gX_LOG_MODULE_SCENE:      return "SCENE";
        case gX_LOG_MODULE_BACKGROUND: return "BG";
        case gX_LOG_MODULE_POSTPROCESS:return "POSTFX";
        case gX_LOG_MODULE_RENDERER:   return "RENDER";
        case gX_LOG_MODULE_COMMON:     return "COMMON";
        case gX_LOG_MODULE_RESOURCE:   return "RES";
        case gX_LOG_MODULE_ARCADE:     return "ARCADE";
        case gX_LOG_MODULE_GAME:       return "GAME";
        case gX_LOG_MODULE_CAMERA:     return "CAM";
        case gX_LOG_MODULE_WRAPPERS:   return "WRAP";
        case gX_LOG_MODULE_SCRIPTING:  return "SCRIPT";
        default: return "";
    }
}

//opencode: static helpers (unchanged from original)

inline static gX_I0 gX_TraceLog_Print_GetColor(gX_I8 *buffer, gX_I32 logTypePrint) {
    if (logTypePrint == gX_LOG_ALL ||
        logTypePrint == gX_LOG_TRACE5 ||
        logTypePrint == gX_LOG_TRACE4 ||
        logTypePrint == gX_LOG_TRACE3 ||
        logTypePrint == gX_LOG_TRACE2 ||
        logTypePrint == gX_LOG_TRACE) {          strcpy(buffer,ANSI_COLOR_BLUE);
    }
    if (logTypePrint == gX_LOG_DEBUG)            strcpy(buffer,ANSI_COLOR_CYAN);
    if (logTypePrint == gX_LOG_INFO ||
        logTypePrint == gX_LOG_NOTICE ) {        strcpy(buffer,ANSI_COLOR_RESET);
    }
    if (logTypePrint == gX_LOG_WARNING       || 
        logTypePrint == gX_LOG_DEBUG_WARNING ||
        logTypePrint == gX_LOG_TRACE_WARNING) {  strcpy(buffer,ANSI_COLOR_YELLOW);
    }
    if (logTypePrint == gX_LOG_ERROR ||
        logTypePrint == gX_LOG_ALERT || 
        logTypePrint == gX_LOG_FATAL) {          strcpy(buffer,ANSI_COLOR_RED);
    }
    if (logTypePrint == gX_LOG_GREEN)            strcpy(buffer,ANSI_COLOR_GREEN);
    if (logTypePrint == gX_LOG_MAGENTA)          strcpy(buffer,ANSI_COLOR_MAGENTA);
}

// Build timestamp prefix: "<HH:MM:SS.nnn>" (absolute) or "[+HH:MM:SS.nnn]" (relative)
// Precision controlled by secondsDivision: 3=ms, 6=µs, 9=ns
inline static gX_I0 gX_TraceLog_Print_GetTimeStamp(gX_I8 *buffer, gX_I32 logTypePrint) {
    gX_I32 hour = 0, min = 0, sec = 0, nsec = 0;
    
    if (gX_LogState.clockRelative) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        gX_I64 elapsed = (gX_I64)(now.tv_sec - gX_LogState.startTime.tv_sec);
        gX_I32 remaining = (gX_I32)(now.tv_nsec - gX_LogState.startTime.tv_nsec);
        if (remaining < 0) { remaining += 1000000000; elapsed--; }
        hour   = (gX_I32)(elapsed / 3600);
        min    = (gX_I32)((elapsed % 3600) / 60);
        sec    = (gX_I32)(elapsed % 60);
        nsec   = remaining;
    } else {
        time_t tim = time(NULL);
        struct tm tm = *localtime(&tim);
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        hour = tm.tm_hour;
        min  = tm.tm_min;
        sec  = tm.tm_sec;
        nsec = (gX_I32)ts.tv_nsec;
    }
    
    // Adjust nanosecond precision
    gX_I32 div = gX_LogState.secondsDivision;
    if (div <= 0 || div > 9) div = 9;
    gX_I32 precisionDiv = 1;
    for (gX_I32 i = 0; i < 9 - div; i++) precisionDiv *= 10;
    nsec = nsec / precisionDiv;
    
    // Format timestamp with dynamic precision
    gX_I8 tsPrefix = gX_LogState.clockRelative ? '+' : '<';
    gX_I8 tsSuffix = '>';  // always '>', same for both modes
    
    // Build: <HH:MM:SS.nnn> or +HH:MM:SS.nnn
    gX_I8 tsPart[64];
    snprintf(tsPart, sizeof(tsPart), "%c%02i:%02i:%02i.%0*i%c ",
             tsPrefix, hour, min, sec, div, nsec, tsSuffix);
    
    // Append level tag
    const gX_I8 *tag = "";
    switch (logTypePrint) {
        case gX_LOG_ALL:             tag = " ALL "; break;
        case gX_LOG_TRACE5:          tag = "TRACE5"; break;
        case gX_LOG_TRACE4:          tag = "TRACE4"; break;
        case gX_LOG_TRACE3:          tag = "TRACE3"; break;
        case gX_LOG_TRACE2:          tag = "TRACE2"; break;
        case gX_LOG_TRACE:           tag = " TRACE"; break;
        case gX_LOG_DEBUG:           tag = " DEBUG"; break;
        case gX_LOG_INFO:            tag = " INFO "; break;
        case gX_LOG_NOTICE:          tag = "NOTICE"; break;
        case gX_LOG_WARNING:         tag = " WARN "; break;
        case gX_LOG_ERROR:           tag = "ERROR!"; break;
        case gX_LOG_ALERT:           tag = "ALERT!"; break;
        case gX_LOG_FATAL:           tag = "FATAL!"; break;
        case gX_LOG_GREEN:           tag = "GREEN "; break;
        case gX_LOG_MAGENTA:         tag = "MAGEN "; break;
        case gX_LOG_DEBUG_WARNING:   tag = "DWARN"; break;
        case gX_LOG_TRACE_WARNING:   tag = "TWARN"; break;
        default:                     tag = "?????"; break;
    }
    
    strcat(buffer, tsPart);
    gX_String_Cat(buffer, gX_Text_Format("[%s] ", tag));
}

inline static gX_I0 gX_TraceLog_Print_GetDetails(gX_I8 *buffer, gX_I32 module, gX_I32 logTypePrint) {
    //opencode: at INFO+ show module name; at DEBUG/TRACE show stack depth + function name
    if (logTypePrint <= gX_LOG_DEBUG || logTypePrint == gX_LOG_DEBUG_WARNING || logTypePrint == gX_LOG_TRACE_WARNING || logTypePrint == gX_LOG_TRACE2_WARNING) {
        gX_I32 depth = 0;
        depth = (gX_I32)gX_LogState.stackDepth - 1;
        if (depth > 0) {
            if (depth > (gX_I32)gX_LogState.maxStackDepth) {
                depth = (gX_I32)gX_LogState.maxStackDepth;
                gX_String_Cat(buffer, gX_Text_Format("{%i+} ", depth));
            }
            else {
                gX_String_Cat(buffer, gX_Text_Format("{%i} ", depth));
            }
            gX_String_Cat(buffer, gX_LogState.functionStack[depth]);
            gX_String_Cat(buffer, ": ");
        }
    }
    else {
        const gX_I8 *modName = gX_Log_GetModuleName(module);
        if (modName[0] != '\0') {
            gX_String_Cat(buffer, gX_Text_Format("[%s] ", modName));
        }
    }
}

//opencode: internal core — all output goes through here with a pre-formatted message
//            (no va_list — the caller has already expanded it)
inline static gX_I0 gX_Log_PrintFormatted(gX_I32 module, gX_I32 logType, const gX_I8 *formattedMsg) {
    gX_I32 logTypePrint = logType;
    
    if (gX_LogState.backendMode) {
        logTypePrint = gX_Backend_RerouteLogtype(logTypePrint);
    }
    
    if (gX_LogState.suppress) return;
    
    //further trace levels to get more details
    //opencode: the trace sub-levels are real filter levels now (TRACE2/3 sit
    //   below TRACE in the enum = more verbose). They used to be mapped to
    //   TRACE before the filter, which made them indistinguishable from
    //   TRACE — and their display tags unreachable. furtherTraceLevel stays
    //   as a dormant marker for the TRACE level.
    if (logTypePrint == gX_LOG_TRACE) gX_LogState.furtherTraceLevel = 1;
    
    //opencode: per-module effective level check
    gX_I32 effectiveLevel = gX_LogState.globalLevel;
    if (module > 0 && module < gX_LOG_MODULE_COUNT && gX_LogState.moduleLevels[module] > 0) {
        effectiveLevel = gX_LogState.moduleLevels[module];
    }
    
    if (effectiveLevel > logTypePrint) return;

    //opencode: record the emitted line in the crash-context ring
    if (gX_LogRingSetting != 0) {
        if (!gX_LogRingChecked) {
            gX_LogRingChecked = 1;
            const char *ring = getenv("GX_LOG_RING");
            gX_LogRingSetting = (ring && ring[0]) ? atoi(ring) : 32;
            if (gX_LogRingSetting < 0) gX_LogRingSetting = 0;
            if (gX_LogRingSetting > 0) {
                gX_LogRing = (gX_I8 *)gX_Malloc(
                    (gX_U32)gX_LogRingSetting * GX_LOG_RING_LINE);
                if (!gX_LogRing) gX_LogRingSetting = 0;
                else gX_LogRingCapacity = (gX_U32)gX_LogRingSetting;
            }
        }
        if (gX_LogRing) {
            gX_I8 *slot = gX_LogRing
                + (gX_LogRingHead * GX_LOG_RING_LINE);
            snprintf((char *)slot, GX_LOG_RING_LINE, "%s", formattedMsg);
            gX_LogRingHead = (gX_LogRingHead + 1) % gX_LogRingCapacity;
            if (gX_LogRingCount < gX_LogRingCapacity) gX_LogRingCount++;
        }
    }
    
    //opencode: repeat suppression — the ring above keeps the truth, the
    //   dedup only collapses the console/file stream
    gX_Log_ApplyModuleLevels();
    if (!gX_LogDedupChecked) {
        gX_LogDedupChecked = 1;
        const char *dedup = getenv("GX_LOG_DEDUP");
        gX_LogDedupOn = (dedup && dedup[0] == '1');
    }
    if (gX_LogDedupOn) {
        if (strcmp((const char *)formattedMsg,
                (const char *)gX_LogDedupLast) == 0) {
            gX_LogDedupCount++;
            return;
        }
        if (gX_LogDedupCount > 0) {
            gX_U32 repeats = gX_LogDedupCount;
            gX_LogDedupCount = 0;
            gX_TraceLog(gX_LOG_INFO,
                repeats == 1 ? "GX_LOG: last message repeated once"
                             : "GX_LOG: last message repeated %u times",
                repeats);
        }
        snprintf((char *)gX_LogDedupLast, sizeof(gX_LogDedupLast), "%s",
            formattedMsg);
    }

    //not enough stack depth
    if (gX_LogState.stackDepth > gX_LogState.maxStackDepthToLog) return;
    
    //get debug and trace warnings
    if ((effectiveLevel > gX_LOG_DEBUG) && (logTypePrint == gX_LOG_DEBUG_WARNING)) return;
    if ((effectiveLevel > gX_LOG_TRACE) && (logTypePrint == gX_LOG_TRACE_WARNING)) return;
    
    if (!gX_LogState.allowGreen     && logTypePrint == gX_LOG_GREEN) return;
    if (!gX_LogState.allowMagenta   && logTypePrint == gX_LOG_MAGENTA) return;
    
    gX_TextLine color = { 0 };
    gX_TextLine timestamp = { 0 };
    gX_TextLine details = { 0 };
    gX_I8 output[MAX_TRACELOG_MSG_LENGTH * 2 + 256] = { 0 };
    
    gX_TraceLog_Print_GetColor(color, logTypePrint);
    gX_TraceLog_Print_GetTimeStamp(timestamp, logTypePrint);
    gX_TraceLog_Print_GetDetails(details, module, logTypePrint);
    
    if (!gX_LogJsonChecked) {
        gX_LogJsonChecked = 1;
        const char *json = getenv("GX_LOG_JSON");
        gX_LogJsonOn = (json && json[0] == '1');
    }
    if (gX_LogJsonOn) {
        //opencode: machine-readable line — the timestamp trimmed of the
        //   display brackets, the message escaped
        gX_I8 plainTs[sizeof(gX_TextLine)];
        snprintf((char *)plainTs, sizeof(plainTs), "%s",
            (const char *)timestamp);
        // the buffer is "<HH:MM:SS.nnn> [ LEVEL ] " — keep only the time
        gX_I8 *start = plainTs;
        while (*start == '<' || *start == '+') start++;
        gX_I8 *end = strchr((const char *)start, '>');
        if (end) *end = 0;
        gX_I8 escaped[MAX_TRACELOG_MSG_LENGTH * 2];
        gX_Log_JsonEscape(formattedMsg, escaped, sizeof(escaped));
        snprintf((char *)output, sizeof(output),
            "{\"ts\":\"%s\",\"level\":\"%s\",\"module\":\"%s\",\"msg\":\"%s\"}",
            start, gX_Log_LevelName(logTypePrint),
            gX_Log_GetModuleName(module), (const char *)escaped);
    } else {
        //assemble output: color + timestamp + backend tag + details + formatted message + newline + reset
        gX_String_Cat(output, color);
        gX_String_Cat(output, timestamp);
        if (gX_LogState.backendMode) {
            strcat(output, "[BACKEND] ");
        }
        gX_String_Cat(output, details);
        gX_String_Cat(output, formattedMsg);
        strcat(output, "\n");
        strcat(output, ANSI_COLOR_RESET);
    }
    
    //opencode: file sink + console switch (lazy env read; the failure path
    //   cannot recurse because the checked flag is already set)
    if (!gX_LogFileChecked) {
        gX_LogFileChecked = 1;
        const char *path = getenv("GX_LOG_FILE");
        if (path && path[0]) {
            gX_LogFile = fopen(path, "w");
            if (gX_LogFile) setvbuf(gX_LogFile, NULL, _IOLBF, 0);
            else gX_TraceLog(gX_LOG_WARNING,
                "GX_LOG_FILE: cannot open '%s'", path);
        }
        const char *console = getenv("GX_LOG_CONSOLE");
        if (console && console[0] == '0') gX_LogConsoleOn = 0;
    }
    if (gX_LogFile && gX_LogJsonOn) {
        fputs((const char *)output, gX_LogFile);
        fputs("\n", gX_LogFile);
    } else if (gX_LogFile) {
        //opencode: the plain transcript — timestamp + details + message,
        //   no ANSI escapes
        fputs((const char *)timestamp, gX_LogFile);
        if (gX_LogState.backendMode) fputs("[BACKEND] ", gX_LogFile);
        fputs((const char *)details, gX_LogFile);
        fputs((const char *)formattedMsg, gX_LogFile);
        fputs("\n", gX_LogFile);
    }

    //print — fputs is safe (no varargs), avoids the va_list-reuse UB in the old code
    if (gX_LogConsoleOn) {
        fputs(output, stdout);
        if (gX_LogJsonOn) fputs("\n", stdout);
        fflush(stdout);
    }
    
    if (logTypePrint == gX_LOG_WARNING && gX_LogState.warningPlaysound && audio.loaded) {
        gX_Sound_Parameters params = { 0 };
        params.volume = 0.25f;
        params.pitch = 1.0f;
        params.pan = 0.0f;
        gX_Sound_Play_Parameters(*(gX_Audio_SoundGet("warning")), params);
    }
    
    //opencode: only NOTICE and worse go to on-screen msg (skip INFO/DEBUG/TRACE/GREEN/MAGENTA)
    if (logTypePrint >= gX_LOG_NOTICE && logTypePrint < gX_LOG_NONE) {
        //push to msg output system with severity
        gX_Msg_Output_Push_Type(formattedMsg, logTypePrint);
        //store in local history buffer
        if (gX_LogState.historyCount < MAX_HISTORY) {
            snprintf(gX_LogState.history[gX_LogState.historyCount], 256, "%s", formattedMsg);
            gX_LogState.history[gX_LogState.historyCount][255] = '\0';
            gX_LogState.historyCount++;
        }
    }
    
    if (logTypePrint == gX_LOG_ERROR) gX_Log_ErrorWriteReport();
    
    if (logTypePrint == gX_LOG_FATAL) exit(EXIT_FAILURE);
}

gX_I0 gX_Log_RingDump(gX_I0) {
    if (!gX_LogRing || gX_LogRingCount == 0) return;
    //opencode: signal-handler safe — dprintf only, no allocation, and the
    //   ring buffer is preallocated and stable.
    dprintf(STDERR_FILENO, "LOG TAIL (%u lines):\n", gX_LogRingCount);
    gX_U32 start = (gX_LogRingCount < gX_LogRingCapacity)
        ? 0 : gX_LogRingHead;
    for (gX_U32 i = 0; i < gX_LogRingCount; i++) {
        const gX_I8 *line = gX_LogRing
            + (((start + i) % gX_LogRingCapacity) * GX_LOG_RING_LINE);
        dprintf(STDERR_FILENO, "  %s\n", line);
    }
}

//opencode: new core API — formats message then delegates to PrintFormatted
gX_I0 gX_Log_Message(gX_I32 module, gX_I32 logType, const gX_I8 *text, ...) {
    va_list args;
    va_start(args, text);
    
    gX_I8 formattedMsg[MAX_TRACELOG_MSG_LENGTH];
    formattedMsg[0] = '\0';
    vsnprintf(formattedMsg, MAX_TRACELOG_MSG_LENGTH, text, args);
    formattedMsg[MAX_TRACELOG_MSG_LENGTH - 1] = '\0';
    
    va_end(args);
    
    gX_Log_PrintFormatted(module, logType, formattedMsg);
}

//opencode: backward-compat entry point — old variadic wrapper
gX_I0 gX_TraceLog(gX_I32 logType, const gX_I8 *text, ...) {
    va_list args;
    va_start(args, text);
    
    gX_I8 formattedMsg[MAX_TRACELOG_MSG_LENGTH];
    formattedMsg[0] = '\0';
    vsnprintf(formattedMsg, MAX_TRACELOG_MSG_LENGTH, text, args);
    formattedMsg[MAX_TRACELOG_MSG_LENGTH - 1] = '\0';
    
    va_end(args);
    
    gX_Log_PrintFormatted(gX_LOG_MODULE_DEFAULT, logType, formattedMsg);
}

//opencode: backward-compat entry point — module-aware variadic
gX_I0 gX_TraceLog_Module(gX_I32 module, gX_I32 logType, const gX_I8 *text, ...) {
    va_list args;
    va_start(args, text);
    
    gX_I8 formattedMsg[MAX_TRACELOG_MSG_LENGTH];
    formattedMsg[0] = '\0';
    vsnprintf(formattedMsg, MAX_TRACELOG_MSG_LENGTH, text, args);
    formattedMsg[MAX_TRACELOG_MSG_LENGTH - 1] = '\0';
    
    va_end(args);
    
    gX_Log_PrintFormatted(module, logType, formattedMsg);
}

//opencode: backward-compat — used as raylib callback (must match SetTraceLogCallback signature)
//            va_list is used ONCE (vsnprintf), eliminating UB from the old double-use
gX_I0 gX_TraceLog_Print(gX_I32 logType, const gX_I8 *text, va_list args) {
    gX_I8 formattedMsg[MAX_TRACELOG_MSG_LENGTH];
    formattedMsg[0] = '\0';
    vsnprintf(formattedMsg, MAX_TRACELOG_MSG_LENGTH, text, args);
    formattedMsg[MAX_TRACELOG_MSG_LENGTH - 1] = '\0';
    
    gX_Log_PrintFormatted(gX_LOG_MODULE_DEFAULT, logType, formattedMsg);
}

//opencode: backward-compat — module-aware print with va_list (invoked directly by some code)
gX_I0 gX_TraceLog_Print_Module(gX_I32 module, gX_I32 logType, const gX_I8 *text, va_list args) {
    gX_I8 formattedMsg[MAX_TRACELOG_MSG_LENGTH];
    formattedMsg[0] = '\0';
    vsnprintf(formattedMsg, MAX_TRACELOG_MSG_LENGTH, text, args);
    formattedMsg[MAX_TRACELOG_MSG_LENGTH - 1] = '\0';
    
    gX_Log_PrintFormatted(module, logType, formattedMsg);
}

//opencode: backward-compat wrappers — old names delegate to new API

inline gX_I0 gX_TraceLog_SuppressStart(gX_I0) {
    gX_Log_SuppressStart();
}

inline gX_I0 gX_TraceLog_SuppressEnd(gX_I0) {
    gX_Log_SuppressEnd();
}

inline gX_I0 gX_TraceLog_StackIncrement(const gX_I8 *funName) {
    gX_Log_StackPush(funName);
}

inline gX_I0 gX_TraceLog_StackDecrement(gX_I0) {
    gX_Log_StackPop();
}

const gX_I8 *gX_TraceLog_GetModuleName(gX_I32 module) {
    return gX_Log_GetModuleName(module);
}

const gX_I8 *gX_TraceLog_Error_GetName(gX_I0) {
    return gX_LogState.errorName;
}

const gX_I8 *gX_TraceLog_Error_GetDescription(gX_I0) {
    return gX_LogState.errorDescription;
}

gX_I0 gX_TraceLog_Error_WriteReport(gX_I0) {
    gX_Log_ErrorWriteReport();
}
