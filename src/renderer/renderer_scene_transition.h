/**********************************************************************************************
*
*   [gX: src/renderer/renderer_scene_transition.h]
*   GFX content ownership across renderer scene switches
*
*   Copyright (c) 2026 lanttu, gX contributors
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


#ifndef GX_RENDERER_SCENE_TRANSITION_H
#define GX_RENDERER_SCENE_TRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/types.h"

//ai: GFX scene-content owners for the heap-transition contract. The GFX
//   renderer heap holds content for exactly one owner at a time (demo /
//   battle / physics sandbox); a scene switch clears only when the owners
//   differ.
#define GX_RENDERER_CONTENT_NONE    0
#define GX_RENDERER_CONTENT_DEMO    1
#define GX_RENDERER_CONTENT_BATTLE  2
#define GX_RENDERER_CONTENT_PHYSICS 3

//ai: clear GFX content only when the current and destination owners differ.
static inline gX_I1 gX_Renderer_SceneTransition_ShouldClearGFX(
    gX_I1 hasGFXContent, gX_I32 contentOwner, gX_I32 targetOwner) {
    return hasGFXContent && contentOwner != targetOwner;
}

#ifdef __cplusplus
}
#endif

#endif //GX_RENDERER_SCENE_TRANSITION_H
