/**********************************************************************************************
*
*   [gX: src/wrappers/file.h]
*   File wrapper header
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

#ifndef WRAPPERS_FILE_H
#define WRAPPERS_FILE_H

#ifdef __cplusplus
extern "C" {
#endif
    
#include "../common/config.h"
#include "../common/types.h"
//opencode: backend-aware implementations (file I/O)
    
gX_U8 *gX_FileData_Load(const gX_I8 *fileName, gX_I32 *dataSize);
gX_I0 gX_FileData_Unload(gX_U8 *data);
gX_I1 gX_FileData_Save(const gX_I8 *fileName, gX_I0 *data, gX_I32 dataSize);

gX_I32 gX_File_Rename(const gX_I8 *fileName, const gX_I8 *fileRename);

//ai: directory listing (names, no subdirs) — backend-aware so call sites
//   never include <dirent.h> directly (raylib bundles a Win32 dirent that
//   shadows the system header under gX's include paths).
#define GX_FILE_DIRECTORY_MAX 64
typedef struct gX_File_Directory {
    gX_I32 count;
    gX_TextLine256 names[GX_FILE_DIRECTORY_MAX];
} gX_File_Directory;

gX_I32 gX_File_Directory_List(const gX_I8 *path, gX_File_Directory *out);

const gX_I8 *gX_Directory_Working_Get(gX_I0);
    
#ifdef __cplusplus
}
#endif

#endif //WRAPPERS_FILE_H
