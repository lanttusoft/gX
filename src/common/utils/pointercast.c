/**********************************************************************************************
*
*   [gX: src/common/utils/pointercast.c]
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

//opencode: suppress double-promotion — variadic float-to-double is automatic in C
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "pointercast.h"

#include "../enum.h"
#include "../log.h"

#include "../../wrappers/text.h"

//cast
#define POINTERCAST_SCALAR_PTRLEFT(dt) \
    case gX_DATATYPE_##dt: { \
        gX_TraceLog(gX_LOG_TRACE,"UTILS: case: gX_DATATYPE_%s",#dt); \
        gX_##dt *ptr = target; \
        gX_TraceLog(gX_LOG_TRACE,"UTILS: ptr value: %i",*ptr); \
        *ptr = (gX_##dt)(value->x); \
        break; \
    } \
    
//receive
#define POINTERCAST_SCALAR_PTRRIGHT(dt) \
    case gX_DATATYPE_##dt: { \
        gX_TraceLog(gX_LOG_TRACE,"UTILS: case: gX_DATATYPE_%s",#dt); \
        gX_##dt *ptr = target; \
        gX_TraceLog(gX_LOG_TRACE,"UTILS: ptr value: %i",*ptr); \
        value->x = (gX_F32)(*ptr); \
        break; \
    } \
    
#define POINTERCAST_VEC2(dt,dt2,left,right) \
    case gX_DATATYPE_##dt##V2: { \
        gX_##dt##v2 *ptr = target; \
        left->x = (gX_##dt2)right->x; \
        left->y = (gX_##dt2)right->y; \
        break; \
    } \
    
#define POINTERCAST_VEC3(dt,dt2,left,right) \
    case gX_DATATYPE_##dt##V3: { \
        gX_##dt##v3 *ptr = target; \
        left->x = (gX_##dt2)right->x; \
        left->y = (gX_##dt2)right->y; \
        left->z = (gX_##dt2)right->z; \
        break; \
    } \
    
#define POINTERCAST_VEC4(dt,dt2,left,right) \
    case gX_DATATYPE_##dt##V4: { \
        gX_##dt##v4 *ptr = target; \
        left->x = (gX_##dt2)right->x; \
        left->y = (gX_##dt2)right->y; \
        left->z = (gX_##dt2)right->z; \
        left->w = (gX_##dt2)right->w; \
        break; \
    } \
    
#define POINTERRECEIVE(dt) \
    POINTERCAST_SCALAR_PTRRIGHT(dt) \
    POINTERCAST_VEC2(dt,F32,value,ptr) \
    POINTERCAST_VEC3(dt,F32,value,ptr) \
    POINTERCAST_VEC4(dt,F32,value,ptr) \
    
#define POINTERCAST(dt) \
    POINTERCAST_SCALAR_PTRLEFT(dt) \
    POINTERCAST_VEC2(dt,dt,ptr,value) \
    POINTERCAST_VEC3(dt,dt,ptr,value) \
    POINTERCAST_VEC4(dt,dt,ptr,value) \

gX_I0 gX_PointerReceive(gX_I0 *target, gX_I32 datatype, gX_F32v4 *value) {
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Receiving pointer value");
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Target: 0x%x",target);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Datatype: %i",datatype);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Value: 0x%x",value);
    switch (datatype) {
        case gX_DATATYPE_I0: break;
        POINTERCAST_SCALAR_PTRRIGHT(I1)
        POINTERCAST_SCALAR_PTRRIGHT(I8)
        POINTERCAST_SCALAR_PTRRIGHT(I16)
        POINTERCAST_SCALAR_PTRRIGHT(I32)
        POINTERCAST_SCALAR_PTRRIGHT(I64)
        POINTERCAST_SCALAR_PTRRIGHT(U8)
        POINTERCAST_SCALAR_PTRRIGHT(U16)
        POINTERCAST_SCALAR_PTRRIGHT(U32)
        POINTERCAST_SCALAR_PTRRIGHT(U64)
        POINTERRECEIVE(F32)
        POINTERRECEIVE(F64)
        
        default: break;
    }
}

gX_I0 gX_PointerCast(gX_I0 *target, gX_I32 datatype, gX_F32v4 *value) {
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Casting pointer value");
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Target: 0x%x",target);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Datatype: %i",datatype);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Value: 0x%x",value);
    switch (datatype) {
        case gX_DATATYPE_I0: break;
        case gX_DATATYPE_I1: {
            gX_TraceLog(gX_LOG_TRACE,"UTILS: case: gX_DATATYPE_I1");
            gX_I1 *ptr = target;
            //gX_TraceLog(gX_LOG_TRACE,"UTILS: ptr value: %i",*ptr);
            gX_I32 intermediate = (gX_I32)value->x;
            if (intermediate > 1) intermediate = 1;
            if (intermediate < 0) intermediate = 0;
            *ptr = (gX_I1)(intermediate);
            break;
        }
        
        POINTERCAST_SCALAR_PTRLEFT(I8)
        POINTERCAST_SCALAR_PTRLEFT(I16)
        POINTERCAST_SCALAR_PTRLEFT(I32)
        POINTERCAST_SCALAR_PTRLEFT(I64)
        POINTERCAST_SCALAR_PTRLEFT(U8)
        POINTERCAST_SCALAR_PTRLEFT(U16)
        POINTERCAST_SCALAR_PTRLEFT(U32)
        POINTERCAST_SCALAR_PTRLEFT(U64)
        POINTERCAST(F32)
        POINTERCAST(F64)
        
        default: break;
    }
}

#define FORMATCAST_SCALAR(dt) \
    case gX_DATATYPE_##dt: { \
        gX_##dt valuevalue = (gX_##dt)(value->x); \
        strcpy(bfr,gX_Text_Format(format,valuevalue)); \
        break; \
    } \
    
#define FORMATCAST_VEC2(dt) \
    case gX_DATATYPE_##dt##V2: { \
        gX_##dt##v2 valuevalue = (gX_##dt##v2){value->x,value->y}; \
        strcpy(bfr,gX_Text_Format(format,valuevalue)); \
        break; \
    } \
    
#define FORMATCAST_VEC3(dt) \
    case gX_DATATYPE_##dt##V3: { \
        gX_##dt##v3 valuevalue = (gX_##dt##v3){value->x,value->y,value->z}; \
        strcpy(bfr,gX_Text_Format(format,valuevalue)); \
        break; \
    } \
    
#define FORMATCAST_VEC4(dt) \
    case gX_DATATYPE_##dt##V4: { \
        gX_##dt##v4 valuevalue = (gX_##dt##v4){value->x,value->y,value->z,value->w}; \
        strcpy(bfr,gX_Text_Format(format,valuevalue)); \
        break; \
    } \
    
#define FORMATCAST(dt) \
    FORMATCAST_SCALAR(dt) \
    FORMATCAST_VEC2(dt) \
    FORMATCAST_VEC3(dt) \
    FORMATCAST_VEC4(dt) \

gX_I32 gX_FormatCast(gX_I8 *target, gX_U32 targetSize, const gX_I8 *format, gX_I32 datatype, gX_F32v4 *value) {
    gX_I32 errors = 0;
    if (!target || targetSize == 0) return 1;
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Casting format to target");
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Format: %s",format);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Datatype: %i",datatype);
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Value: 0x%x",value);
    //opencode: if format doesn't contain '%', fall back to a default
    //(prevents corrupted format strings like "0.000" from ruining the output)
    gX_I8 defaultFmt[16] = {0};
    if (format) {
        gX_I1 hasPct = false;
        for (const gX_I8 *p = format; *p; p++) { if (*p == '%') { hasPct = true; break; } }
        if (!hasPct) {
            if (datatype == gX_DATATYPE_F32 || datatype == gX_DATATYPE_F64) strcpy(defaultFmt, "%.3f");
            else strcpy(defaultFmt, "%i");
            format = defaultFmt;
        }
    }
    gX_I8 bfr[1024] = { 0 };
    switch (datatype) {
        FORMATCAST_SCALAR(I8)
        FORMATCAST_SCALAR(I16)
        FORMATCAST_SCALAR(I32)
        FORMATCAST_SCALAR(I64)
        FORMATCAST_SCALAR(U8)
        FORMATCAST_SCALAR(U16)
        FORMATCAST_SCALAR(U32)
        FORMATCAST_SCALAR(U64)
        FORMATCAST(F32)
        FORMATCAST(F64)
        default: break;
    }
    //opencode: bounded copy — targetSize prevents overflowing caller buffers
    strncpy(target,bfr,targetSize - 1);
    target[targetSize - 1] = '\0';
    return errors;
}

gX_I32 gX_FormatCast_String(gX_I8 *target, gX_U32 targetSize, const gX_I8 *format, const gX_I8 *str) {
    gX_I32 errors = 0;
    if (!target || targetSize == 0) return 1;
    gX_TraceLog_COMMON(gX_LOG_TRACE,"Casting format to target [string]");
    //opencode: bounded copy — gX_Text_Format returns a 1024-byte static buffer
    strncpy(target,gX_Text_Format(format,str),targetSize - 1);
    target[targetSize - 1] = '\0';
    return errors;
}
