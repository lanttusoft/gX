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

gX_I32 gX_Msg_Output_Push_Type(const gX_I8 *message, gX_I32 type) {
    gX_U32 pos = msg.output.startpoint;
    strcpy(msg.output.history[pos], message);
    msg.output.type[pos] = type;
    msg.output.time[pos] = maindata.timeElapsed;
    msg.output.startpoint++;
    msg.output.startpoint %= 128;
    return 0;
}
