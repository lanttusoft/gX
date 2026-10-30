/**********************************************************************************************
*
*   [gX: src/renderer/gfx/animation.h]
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


//opencode: GLB clip playback (first slice) — sample the parsed animation
//   channels into bone poses and produce the GPU skinning matrices.
#ifndef RENDERER_GFX_ANIMATION_H
#define RENDERER_GFX_ANIMATION_H

#include "../../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gX_RendererGFX_Model gX_RendererGFX_Model;

//opencode: advance the model's clip playhead and refresh currentPose /
//   boneMatrices. Safe to call every frame: it returns unless a clip is
//   selected and the model carries a skinned skeleton. With animPlaying = 0 it
//   still evaluates the static pose at animTime (scrubbing).
gX_I0 gX_RendererGFX_Animation_Update(gX_RendererGFX_Model *model,
    gX_F64 deltaTime);

//opencode: resolve the trimmed playback range for a clip duration. animEnd
//   <= 0 (or an invalid start >= end pair) falls back to the full clip; both
//   outputs are clamped into [0, duration]. Shared by the evaluator and the
//   editor timeline so the displayed range matches playback.
gX_I0 gX_RendererGFX_Animation_ClipRange(gX_F32 animStart, gX_F32 animEnd,
    gX_F32 duration, gX_F32 *outStart, gX_F32 *outEnd);

//opencode: write the bone's effective pose (clip sampling plus any override)
//   into the selected clip at the playhead. Existing channels are updated in
//   place; missing translation/rotation/scale channels are created (up to the
//   clip's 8-channel capacity). Keys live in memory for the session — use
//   gX_RendererGFX_Animation_SidecarSave to persist them.
gX_I0 gX_RendererGFX_Animation_PoseKey(gX_RendererGFX_Model *model,
    gX_I32 bone);

//opencode: authored-key sidecar (`<model-stem>.anim.json`, the materials-sidecar
//   convention). Load merges every persisted key into the parsed clips
//   (insert/replace by time) after the GLB is read; Save writes every channel
//   key of every clip next to the model so editor-authored keys survive a
//   reload. Binary GLB write-back stays out of scope on purpose (a sidecar is
//   additive, diffable, and cannot corrupt the source asset).
gX_I0 gX_RendererGFX_Animation_SidecarLoad(gX_RendererGFX_Model *model);
gX_I32 gX_RendererGFX_Animation_SidecarSave(gX_RendererGFX_Model *model);

#ifdef __cplusplus
}
#endif

#endif //RENDERER_GFX_ANIMATION_H
