/**********************************************************************************************
*
*   [gX: src/common/msg.c]
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

//opencode: added gX_Msg_Output_Push_Type + timestamp for fade-out
#include <string.h>

#include "msg.h"

#include "../main.h"

gX_Msg msg;

gX_I32 gX_Msg_Init(gX_I0) {
    
    strcpy(msg.msg,"message");
    
    msg.output.startpoint = 0;
    
    return 0;
}

gX_I32 gX_Msg_Output_Push(const gX_I8 *message) {
    return gX_Msg_Output_Push_Type(message, 0); // default type (INFO)
}

//ai: RC-H1 — bounded copy into the fixed on-screen history entry. Returns 0
//   for a complete copy, 1 when the marker-truncated result was written, and
//   -1 for a missing source or invalid destination.
static gX_I32 gX_Msg_Output_Copy(gX_I8 *target, gX_U32 capacity, const gX_I8 *source) {
    if (!target || capacity < 4) return -1;
    if (!source) {
        target[0] = '\0';
        return -1;
    }
    gX_U32 length = (gX_U32)strlen(source);
    if (length >= capacity) {
        gX_U32 keep = capacity - 4;
        memcpy(target, source, keep);
        target[keep] = '.';
        target[keep + 1] = '.';
        target[keep + 2] = '.';
        target[capacity - 1] = '\0';
        return 1;
    }
    memcpy(target, source, length + 1);
    return 0;
}

gX_I32 gX_Msg_Output_Push_Type(const gX_I8 *message, gX_I32 type) {
    if (!message) return -1;
    gX_U32 pos = msg.output.startpoint;
    gX_Msg_Output_Copy(msg.output.history[pos], (gX_U32)sizeof(msg.output.history[pos]), message);
    msg.output.type[pos] = type;
    msg.output.time[pos] = maindata.timeElapsed;
    msg.output.startpoint++;
    msg.output.startpoint %= 128;
    return 0;
}

//ai: RC-H1 regression — 127 bytes fit, 128+ bytes truncate with marker, and
//   a null source is rejected without touching the destination.
gX_I32 gX_Msg_Tests(gX_I0) {
    gX_I8 buffer[128];
    gX_I8 source[201];
    gX_I32 errors = 0;
    memset(buffer, 0, sizeof(buffer));
    memset(source, 'A', 127);
    source[127] = '\0';
    if (gX_Msg_Output_Copy(buffer, (gX_U32)sizeof(buffer), source) != 0) errors++;
    if (strcmp(buffer, source) != 0) errors++;
    memset(source, 'B', 128);
    source[128] = '\0';
    if (gX_Msg_Output_Copy(buffer, (gX_U32)sizeof(buffer), source) != 1) errors++;
    if (strlen(buffer) != 127) errors++;
    if (strcmp(buffer + 124, "...") != 0) errors++;
    if (buffer[127] != '\0') errors++;
    memset(source, 'C', 200);
    source[200] = '\0';
    if (gX_Msg_Output_Copy(buffer, (gX_U32)sizeof(buffer), source) != 1) errors++;
    if (buffer[127] != '\0') errors++;
    if (gX_Msg_Output_Copy(buffer, (gX_U32)sizeof(buffer), NULL) != -1) errors++;
    if (buffer[0] != '\0') errors++;
    return errors;
}
