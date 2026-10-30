/**********************************************************************************************
*
*   [gX: src/scene/title.h]
*   Title scene — minimal demo (first visible gX frame)
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


#ifndef GX_SCENE_TITLE_H
#define GX_SCENE_TITLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

typedef struct gX_SceneTitle {
    gX_I8 name[128];
    gX_I8 description[256];

    gX_I1 loaded;
} gX_SceneTitle;

gX_I32 gX_SceneTitle_Load(gX_SceneTitle *title);
gX_I32 gX_SceneTitle_Unload(gX_SceneTitle *title);
gX_I32 gX_SceneTitle_Update(gX_SceneTitle *title);
gX_I32 gX_SceneTitle_Draw(gX_SceneTitle *title);

#ifdef __cplusplus
}
#endif

#endif //GX_SCENE_TITLE_H
