/**********************************************************************************************
*
*   [gX: src/common/text.h]
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

#ifndef TEXT_H
#define TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "file.h"

typedef struct gX_Text {
    
    gX_String   string;
    
    gX_Font     font;
    
    gX_F32      fontSize;
    gX_F32v2    offset;
    gX_I32      spacing;
    
    gX_Color    color;
    
} gX_Text;

typedef struct gX_TextGetter {
    
    gX_File textfile;
    
    gX_U32 lineNumbers[8];
    gX_TextLine textlines[8];
    
} gX_TextGetter;

gX_I0 gX_TextGetter_Load(gX_TextGetter *textgetter, const gX_I8 *filename);
gX_I0 gX_TextGetter_Unload(gX_TextGetter *textgetter);

gX_I0 gX_TextGetter_ClearLines(gX_TextGetter *textgetter);
gX_I0 gX_TextGetter_AddLine(gX_TextGetter *textgetter, gX_U32 linenumber);


extern gX_TextGetter texthandler;

gX_I0 gX_TextHandler_Load(const gX_I8 *filename);
gX_I0 gX_TextHandler_Unload(gX_I0);

gX_I0 gX_TextHandler_ClearLines(gX_I0);
gX_I0 gX_TextHandler_AddLine(gX_U32 linenumber);

#ifdef __cplusplus
}
#endif

#endif //TEXT_H
