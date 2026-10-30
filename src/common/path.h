/**********************************************************************************************
*
*   [gX: src/common/path.h]
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


//opencode: multi-tier resource path resolution — search dirs, type aliases, fallback
#ifndef GX_PATH_H
#define GX_PATH_H

#include "types.h"

//opencode: compile-time path generator — GX_PATH("texture", "title/bg2.png")
//   yields "resources/texture/title/bg2.png" at compile time.
//   Used as the default fallback when runtime resolution finds nothing.
#ifndef GX_RESOURCE_DIR
#define GX_RESOURCE_DIR "resources"
#endif
#define GX_PATH(type, file) GX_RESOURCE_DIR "/" type "/" file

//opencode: runtime path resolution — multi-tier search with type aliases.
//   Call gX_Path_Init() once at startup, then register dirs and resolve paths.

// Initialize the path system with default dirs and type aliases.
gX_I0 gX_Path_Init(gX_I0);

// Add a search root directory (e.g. "custom/", "../resources/", "resources/").
// First added = highest priority. Returns index or -1 on error.
gX_I32 gX_Path_AddSearchDir(const gX_I8 *dir);

// Register a type-to-subdirectory alias (e.g. "model" → "models").
gX_I32 gX_Path_RegisterType(const gX_I8 *type, const gX_I8 *alias);

// Resolve a resource path: tries each search dir (priority order) with
// type aliases (phase 1) then literal fallback (phase 2).
// Returns a pointer to a static buffer (valid until next call) or NULL.
const gX_I8 *gX_Path_Resolve(const gX_I8 *type, const gX_I8 *subpath);

//opencode: v0.3.8 - guest fallback for wrapper loaders. Derives the subpath
//   after the first "/<type>/" segment of a full resource path (e.g.
//   "resources/model/props/tubetech_vt.glb" -> "props/tubetech_vt.glb") and resolves it against
//   the registered search dirs (guest roots). Returns a guest path, or NULL
//   when the caller's path contains no type segment or nothing resolves.
//   Static buffer (valid until next call) like gX_Path_Resolve.
const gX_I8 *gX_Path_GuestFallback(const gX_I8 *type, const gX_I8 *fullPath);

#endif //GX_PATH_H
