/**********************************************************************************************
*
*   [gX: src/rhi/rhi_event_log.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
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


//opencode: RHI Event Log — ring buffer of draw call events for debugging.
#ifndef RHI_EVENT_LOG_H
#define RHI_EVENT_LOG_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_RHI_EVENT_LOG_SIZE 256
#define GX_RHI_EVENT_LABEL_LEN 32

typedef enum {
    GX_RHI_EVENT_DRAW,
    GX_RHI_EVENT_BIND_TEX,
    GX_RHI_EVENT_USE_PROGRAM,
    GX_RHI_EVENT_BIND_FBO,
    GX_RHI_EVENT_ERROR,
} gX_RHI_EventType;

typedef struct {
    gX_F64 timestamp;
    gX_U32 frameNumber;
    gX_RHI_EventType type;
    gX_U32 id0;  // program ID, texture ID, FBO ID
    gX_U32 id1;  // VAO ID, unit, vertex count
    gX_U32 id2;  // mode, error code
    gX_I8  label[GX_RHI_EVENT_LABEL_LEN];
} gX_RHI_Event;

gX_I0 gX_RHI_EventLog_Push(gX_RHI_EventType type,
    gX_U32 id0, gX_U32 id1, gX_U32 id2, const gX_I8 *label);
gX_I0 gX_RHI_EventLog_NextFrame(gX_I0);
gX_I0 gX_RHI_EventLog_Clear(gX_I0);

// Returns pointer to ring buffer and sets *outCount to number of events
const gX_RHI_Event *gX_RHI_EventLog_Get(gX_U32 *outCount);

#ifdef __cplusplus
}
#endif
#endif
