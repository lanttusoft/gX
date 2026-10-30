/**********************************************************************************************
*
*   [gX: src/common/msg.h]
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

#ifndef GX_MSG_H
#define GX_MSG_H

#include <stdio.h>

#include "types.h"

typedef struct gX_Msg_Output {
    gX_U32 startpoint;
    gX_I32 type[128];       // gX_LOG_INFO, gX_LOG_WARNING, gX_LOG_ERROR
    gX_F64 time[128];       // maindata.timeElapsed when pushed (for fade-out)
    gX_TextLine history[128];
} gX_Msg_Output;

typedef struct gX_Msg {
    gX_I8 name[128];
    gX_I8 description[128];
    
    gX_TextLine msg;
    
    gX_Msg_Output output;
    
} gX_Msg;

extern gX_Msg msg;

gX_I32 gX_Msg_Init(gX_I0);

gX_I32 gX_Msg_Output_Push(const gX_I8 *message);

gX_I32 gX_Msg_Output_Push_Type(const gX_I8 *message, gX_I32 type);

gX_I32 gX_Msg_Show(const gX_I8 *name);

#endif //GX_MSG_H
