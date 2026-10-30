/**********************************************************************************************
*
*   [gX: src/renderer/background/transition.h]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


#ifndef RENDERER_BACKGROUND_TRANSITION_H
#define RENDERER_BACKGROUND_TRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../common/types.h"
#include "param.h"

//opencode: transition easing types
typedef enum {
    GX_BG_TRANS_EASE_LINEAR = 0,
    GX_BG_TRANS_EASE_IN,
    GX_BG_TRANS_EASE_OUT,
    GX_BG_TRANS_EASE_IN_OUT
} gX_BG_Transition_Ease;

struct gX_RendererBackground;

//opencode: start a transition to the given preset path. duration in seconds.
//   ease is one of gX_BG_Transition_Ease. Returns 0 on success.
gX_I32 gX_RendererBackground_Transition_Start(struct gX_RendererBackground *bg,
    const gX_I8 *presetPath, gX_F32 duration, gX_I32 ease);

//opencode: advance the transition timer and lerp live params. Called from
//   gX_RendererBackground_Update each frame.
gX_I0 gX_RendererBackground_Transition_Update(struct gX_RendererBackground *bg);

//opencode: cancel a running transition immediately.
gX_I0 gX_RendererBackground_Transition_Stop(struct gX_RendererBackground *bg);

//opencode: apply transition easing to progress t in [0,1]. ease is one of
//   gX_BG_Transition_Ease. Returns the eased value (also in [0,1]).
gX_F32 gX_RendererBackground_Transition_Ease(gX_F32 t, gX_I32 ease);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_BACKGROUND_TRANSITION_H
