/**********************************************************************************************
*
*   [gX: src/wrappers/file.c]
*   File wrapper — read/write/exists
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

//opencode: backend-aware file data operations + working directory
#include "file.h"

#include "../common/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "../backend/raylib/raylib_wrappers.h"

gX_U8 *gX_FileData_Load(const gX_I8 *fileName, gX_I32 *dataSize) {
    gX_U8 *data = LoadFileData(fileName, dataSize);
    if (!data && dataSize) {
        gX_TraceLog(gX_LOG_WARNING, "FILE: could not load: %s", fileName);
        *dataSize = 0;
    }
    return data;
}

gX_I0 gX_FileData_Unload(gX_U8 *data) {
    if (!data) return;
    UnloadFileData(data);
}

gX_I1 gX_FileData_Save(const gX_I8 *fileName, gX_I0 *data, gX_I32 dataSize) {
    return (gX_I1)SaveFileData(fileName, data, dataSize);
}

gX_I32 gX_File_Rename(const gX_I8 *fileName, const gX_I8 *fileRename) {
    gX_I32 result = rename(fileName, fileRename);
    if (result != 0) {
        gX_TraceLog(gX_LOG_WARNING, "FILE: rename failed: %s -> %s", fileName, fileRename);
    }
    return result;
}

gX_I32 gX_File_Directory_List(const gX_I8 *path, gX_File_Directory *out) {
    if (!path || !path[0] || !out) return -1;
    memset(out, 0, sizeof(gX_File_Directory));

    FilePathList files = LoadDirectoryFiles(path);
    if (!files.paths) return -1;

    for (unsigned int i = 0; i < files.count && out->count < GX_FILE_DIRECTORY_MAX; i++) {
        const gX_I8 *name = GetFileName(files.paths[i]);
        if (!name) continue;
        snprintf(out->names[out->count], sizeof(out->names[0]), "%s", name);
        out->count++;
    }
    UnloadDirectoryFiles(files);
    return out->count;
}

const gX_I8 *gX_Directory_Working_Get(gX_I0) {
    return GetWorkingDirectory();
}
