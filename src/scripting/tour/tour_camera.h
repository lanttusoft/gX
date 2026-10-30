/**********************************************************************************************
*
*   [gX: src/scripting/tour/tour_camera.h]
*   Tour camera rail — module-internal keyframe access (editor + persistence)
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


//ai: module-internal surface for the tour camera rail (the shell module owns
//   the rail, per docs/editors.md's module placement decision). The rail
//   editor panel and the script bindings both go through these; nothing here
//   is part of the exe's ABI.
#ifndef GX_TOUR_CAMERA_H
#define GX_TOUR_CAMERA_H

#include "../../common/types.h"
#include "../../gxmath/vector3.h"

#define GX_TOUR_CAMERA_MAX_KEYFRAMES 64

typedef struct gX_TourCamera_Keyframe {
    gX_F32   t;       //duration of the segment arriving at this keyframe (s)
    gX_F32v3 pos;
    gX_F32v3 target;
    gX_F32   fov;
    gX_I1    bezier;
    gX_F32v3 ctrl1;   //position handles for the segment leaving this keyframe
    gX_F32v3 ctrl2;
} gX_TourCamera_Keyframe;

#ifdef __cplusplus
extern "C" {
#endif

gX_I32 gX_TourCamera_RailMax(gX_I0);
gX_I32 gX_TourCamera_RailCount(gX_I0);
//opencode: direct keyframe access for the editor panel (NULL out of range).
gX_TourCamera_Keyframe *gX_TourCamera_RailKey(gX_I32 index);
//opencode: append the current camera.camera3D pose as a keyframe (editor grab).
gX_I32 gX_TourCamera_RailAddGrab(gX_F32 t);
//opencode: remove/reorder/jump helpers (editor + script bindings).
gX_I32 gX_TourCamera_RailRemove(gX_I32 index);
gX_I32 gX_TourCamera_RailMove(gX_I32 index, gX_I32 delta);
gX_I32 gX_TourCamera_RailApply(gX_I32 index);
gX_I0  gX_TourCamera_RailClear(gX_I0);
gX_I0  gX_TourCamera_RailPlay(gX_I0);
gX_I0  gX_TourCamera_RailStop(gX_I0);
gX_I1  gX_TourCamera_RailIsRunning(gX_I0);
//opencode: JSON persistence (0 ok / 1 fail). Saves refuse shipped preset
//   paths under the harness guard (GX_NO_SETTINGS_SAVE), like GFX presets.
gX_I32 gX_TourCamera_RailSave(const gX_I8 *path);
gX_I32 gX_TourCamera_RailLoad(const gX_I8 *path);

#ifdef __cplusplus
}
#endif

#endif //GX_TOUR_CAMERA_H
